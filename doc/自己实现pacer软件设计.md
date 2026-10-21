# 自己实现 Pacer：软件设计

> 目标：把 WebRTC pacer 功能摘出来，自己实现一个独立 pacer——接收任意格式网络数据，入队，定时器触发，按指定 IP 和端口发送到网络。

## 一、结论先行

**完全可行，而且这正好是 WebRTC pacer 的原生设计意图**——它的逻辑层根本不关心"发到哪"，只调一个抽象回调 `packet_sender_->SendPacket()`（pacing_controller.cc:506）。只需要解开最后一个绑定点（包类型 `RtpPacketToSend`），并把回调实现成"按 ip/port 发 UDP"即可。

## 二、WebRTC pacer 与外部世界的绑定点（只有 2 个）

| 绑定点 | 在 WebRTC 中的体现 | 方案如何解开 |
|--------|------------------|----------------|
| 包类型 | `PacingController` 处理 `std::unique_ptr<RtpPacketToSend>` | 换成本项目通用包（字节缓冲+元数据），见下文 |
| 发到哪 | `PacketSender` 抽象接口 → `PacketRouter` → RTP模块 → transport | `PacketSender` 接口 + 参考实现 `UdpTransport`（`sendto` 指定 ip/port） |

**核心关键**：所有算法（债务双桶、`NextSendTime()`、突发、优先级队列）只依赖两样东西——**包大小**和**入队时刻**，完全不碰包内容。pacing_controller.cc:669-682 的债务记账只看 `size`。所以把 RTP 包钩掉换成任意字节块，算法一行不用改。

## 三、整体架构（四层，向下依赖）

```
┌────────────────────────────────────────────┐
│  PacedSender (定时层)                        │  std::thread + std::condition_variable
│    wait_until(NextSendTime) → ProcessPackets │  (参照 TaskQueueStdlib 模式)
│    入队/改速率 → 唤醒重算;  shutdown → 退出   │
├────────────────────────────────────────────┤
│  PacingController (逻辑层, 纯算法, 无IO)      │
│    双桶债务 media_debt_/padding_debt_        │
│    NextSendTime() / ProcessPackets()         │
│    依赖注入: Clock*, PacketSender*           │
├────────────────────────────────────────────┤
│  PacerPacketQueue (队列层)                   │
│    5优先级 × 多流(queue按flow_id)            │
│    round-robin 选流 + 关键帧优先, O(logN)    │
├────────────────────────────────────────────┤
│  UdpTransport : PacketSender (发送层)        │  用户提供 / 参考实现
│    SendPacket() → sendto(sock, ip, port)    │
└────────────────────────────────────────────┘
```

## 四、关键设计决策

### 4.1 通用包类型（解耦 RTP 的一招）

推荐**通用缓冲包**（贴合 selfPacer 已建的队列骨架），替代 RtpPacketToSend：

```cpp
struct PacedPacket {
  std::vector<uint8_t> data;    // 任意格式网络数据（264/265/rtp/自定义）
  size_t payload_size;          // 计入债务的大小 = data.size()
  uint32_t flow_id;             // 流标识，替代 WebRTC 的 SSRC
  int priority;                 // 0音频 1音频重传 2视频重传 3媒体 4填充
  Timestamp enqueue_time;       // 入队时刻（队列用它算 oldest）
  bool is_key_frame = false;    // 关键帧优先
};
```

- **flow_id 替代 SSRC**：WebRTC 队列按 SSRC 分流做轮询。`flow_id` 就是"输入流的身份证"——264 流、265 流、rtp 流各一个 id，`UdpTransport` 内部存 `flow_id → sockaddr_in` 映射即可。
- 这是模板方案（`Pacer<T>`）的简化替代：不引入模板复杂度，还能直接复用队列层的 `std::deque<QueuedPacket>`。

### 4.2 "发到哪" = 一个接口 + 一个参考实现

```cpp
class PacketSender {                       // PacingController 只认识这个
 public:
  virtual void SendPacket(std::unique_ptr<PacedPacket> packet) = 0;
  virtual void OnBatchComplete() {}        // 可选，中配可直接留空
};

class UdpTransport : public PacketSender { // 参考实现，可放在 selfPacer
 public:
  UdpTransport();                          // 构造 socket
  void SetDestination(uint32_t flow_id, const std::string& ip, uint16_t port);
  bool SendPacket(std::unique_ptr<PacedPacket> packet) override {
    auto addr = destinations_.find(packet->flow_id);
    // sendto(fd_, packet->data.data(), packet->payload_size, 0, &addr..., sizeof)
    return retry/记录丢包;
  }
 private:
  std::map<uint32_t, sockaddr_in> destinations_;
};
```

于是"**指定 ip 和端口**"就体现在 `SetDestination` 里。PacingController 完全不知道 ip/port 的存在——这正是解耦的来源。

### 4.3 时钟注入 + FakeClock（可测性的命门）

- 生产：`SystemTimeStamp`（需先修掉换算常数 bug）或自研 Clock
- 测试：抽象 `Clock` 接口 + `FakeClock`（可手动 `AdvanceTime`），这是 WebRTC 单元测试的根基，pacer 必须这样设计，否则无法验证节奏。

### 4.4 定时触发（推荐 thread+condvar，自研最顺手）

```cpp
// 伪代码：PacedSender 线程主循环
while (!shutdown) {
  Timestamp next = controller->NextSendTime();   // 计算下次该处理时刻
  std::unique_lock lk(mutex_);
  condvar_.wait_until(lk, next, [&]{ return next_send_time_changed || shutdown; });
  if (shutdown) break;
  controller->ProcessPackets();                  // 时间到了就处理
}
// EnqueuePacket/SetPacerConfig 加锁改 target → condvar_.notify_all() 唤醒重算
```

事件驱动：**入队、改速率、定时到期**三类事件都会醒来处理；`NextSendTime` 每次处理完重算并继续 wait——这就是 WebRTC 的"自驱动定时循环"（task_queue_paced_sender.cc 的 MaybeProcessPackets 同样逻辑）。

### 4.5 债务算法平移（一行原理，内容无关）

- 入账：`media_debt_ += payload_size`，封顶 `rate × 500ms`（kMaxDebtInTime）
- 出账：`media_debt_ -= min(media_debt_, rate × Δt)`（pacing_controller.cc:669-671）
- `NextSendTime()`：`drain_time = media_debt_/rate`；若剩余突发窗口（默认63KB 或 `send_burst_interval`）能在 `drain_time` 内清完 → 立即发；否则 `last_process_time_ + drain_time`

## 五、必须提前意识到的 3 个坑（中配版边界）

1. **速率来源**：pacer 自己不知道目标码率，必须由调用方 `SetPacerConfig(send_rate)` 喂。WebRTC 由拥塞控制器给；自研先固定速率，或接受外部回调周期性更新。
2. **不负责分片/MTU**：WebRTC 的分片在 packetizer（送入 pacer 之前）完成。接任意格式数据时，**入队前应保证 `data.size() ≤ MTU`**，否则 UDP `sendto` 会因超长被截断/丢弃。pacer 只负责"按节奏递交，不负责裁切"。
3. **"网络数据"若指 TCP 流式**：pacer 面向"有包边界"的传输（UDP/RTP 式）。流式数据需先自己分帧（自定义包边界）再入队，否则 pacer 无从计量每包大小。

## 六、分阶段实施计划

| 阶段 | 内容 | 参考物 |
|------|------|--------|
| **P0** | 修 selfPacer 基础：`system_timestamp` 换算常数+`<cstdint>`、补 `Queue` 缺失的 `enqueue_time` 列表成员、纠正 `unique_ptr<unsigned char*>` 所有权 | — |
| **P1** | 队列层：`PacedPacket` 定义 + `StreamQueue`（5优先级 FIFO + round-robin 选流）+ `PacerPacketQueue` 聚合 | `prioritized_packet_queue.h` |
| **P2** | 逻辑层：`PacingController`（PacerConfig、双桶、`NextSendTime`、`ProcessPackets`、`EnqueuePacket`）+ `Clock`/`FakeClock` | `pacing_controller.cc` |
| **P3** | 发送层：`PacketSender` 接口 + `UdpTransport`（`SetDestination(flow_id, ip, port)` + `sendto`） | — |
| **P4** | 定时层：`PacedSender`（thread+condvar 自驱动循环，shutdown 安全退出） | `task_queue_stdlib.cc` |
| **P5** | 验证：FakeClock 单测（仿 `pacing_controller_unittest` 的 `while: AdvanceTimeUntil(NextSendTime()); ProcessPackets()`）+ UDP loopback demo（本地 `127.0.0.1` 自环，接收端记录每包到达时刻，验证按速率均匀铺开） | `pacing_controller_unittest.cc` |

## 七、一句话可行性评估

- **可行**，且比直接魔改 WebRTC 更干净：解耦出 RTP 后，得到的是一个"节流器+调度器"通用组件，`flow_id→(ip,port)` 映射让它天生支持多目标。
- 唯一需要承担的设计决定：**目标速率的管理方式**（固定/外部喂入）和 **MTU 约定**（调用方保证包不超长）。