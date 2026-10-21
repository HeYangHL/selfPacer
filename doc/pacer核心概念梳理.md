# WebRTC Pacer 核心概念梳理

以下全部基于对 WebRTC 源码的逐行阅读（`modules/pacing/` 下的 `pacing_controller.cc/.h`、`prioritized_packet_queue.cc/.h`、`interval_budget.cc/.h`、`task_queue_paced_sender.cc`）。

## 一、Pacer 要解决什么问题

网络是**带宽受限**的。编码器可能瞬间产生一大批数据（比如关键帧），但如果一股脑全发出去，会瞬间打爆网络缓存 → 拥塞、丢包、延迟飙升。

**Pacer 的作用**：把"突发到达的数据"按照设定的**速率**均匀地"摊平"到时间轴上发送。本质是一个**速率整形器（rate shaper）**。

核心矛盾：**数据到达是突发的，但发送必须是匀速的**。

## 二、最根本的概念：漏桶（Leaky Bucket）

Pacer 的本质是一个**漏桶**。想象一个桶：

- **往桶里倒水** = 要发送的数据（突发到达）
- **桶底的小孔匀速漏水** = 按速率发送
- **桶的容量** = 允许积累的"欠账"上限

关键洞察：**速率限制不是"每秒只发 N 字节"这么简单**，那样每隔一段时间就会"哗啦"地突发一次。真正的平滑需要把速率"积分"成时间轴上的连续约束。

## 三、"债务"（Debt）——WebRTC 的核心

WebRTC 用**债务（debt）** 这个量来转译速率限制。

### 债务是什么？

债务 = **"多发了"多少字节**（相对于匀速应该发到的量）。

- 债务 = 0 → 完全跟得上匀速，空桶
- 债务 > 0 → 之前发得太猛，欠了账，必须等债务"还清"才能再发

### 两个方向的操作（理解的核心）

**1. 借债（发包时增加）：`UpdateBudgetWithSentData`**
```
media_debt_ += packet_size      // 发一个包，欠账增加这个包的大小
```
每发出一个包，债务就增加"这个包的字节数"。**发得越多，欠得越多。**

**2. 还债（时间流逝时减少）：`UpdateBudgetWithElapsedTime`**
```
media_debt_ -= min(media_debt_, rate * Δt)   // 时间过了 Δt，按速率"还掉" rate×Δt
```
每过 Δt 时间，债务自动减少 `rate × Δt` 字节。**关键公式**：`rate × Δt` 就是"这段时间里匀速应该能发掉多少字节"。

### 为什么要双桶？

WebRTC 维护**两个桶**（对应两种流量）：

```cpp
DataSize media_debt_;    // 媒体流量（视频+音频）的债务
DataSize padding_debt_;  // 填充流量（padding）的债务
```

为什么分开？因为媒体和填充的**速率不同**（`pacing_rate_` 和 `padding_rate_`），要各自独立还债。它们互相补充：媒体空闲时，用填充维持带宽占位。

### 债务会变负吗？

债务被 `min()` 钳制在 `>= 0`，不会变负。变负本来可以表示"我欠得少，可以多借点"，但 WebRTC 选择直接封底为 0 —— 也就是**不允许把"没发够的额度"存起来以后用**。

## 四、"还债务清了才能发" —— 发送门控

这是 pacer 的**决策核心**：**什么时候允许发一个包？**

WebRTC 用 `NextSendTime()` 计算"什么时候还允许发"：

```cpp
// 队列非空时：
TimeDelta drain_time = media_debt_ / pacing_rate_;      // 还清债务需要多久
TimeDelta send_burst  = min(send_burst_interval_,       // 允许的突发窗口
                             kMaxBurstSize / pacing_rate_);
next_send_time = last_process_time_
               + ((send_burst > drain_time) ? 0 : drain_time);
```

解读这个公式：
- `drain_time` = 把当前债务还清需要的时间
- 如果债务能在**突发窗口**内还清（`send_burst > drain_time`）→ **现在就能发**（+0）
- 否则 → 等到 `last_process_time + drain_time` 再发

**一句话**：只有"欠的债在允许的突发范围内"才发，否则等到债务自然还清。

## 五、"突发"（Burst）——为什么能稍微超速

如果严格匀速，每个包都要精确对齐，延迟会很高。WebRTC 允许**突发（burst）**：

- 有一个 `send_burst_interval_`（默认如 5ms）
- 在这个窗口内，允许**多发一个突发窗口的量**——也就是债务可以适度"超支"
- 对应 `kMaxBurstSize = 63KB` 也限制突发不能无限大（防撑爆 socket 缓冲区）

这就是为什么 `send_burst > drain_time` 时能立即发：**允许"预支"突发窗口的发送预算**，换取更低的延迟。

## 六、队列（Queue）——存储与优先级

`PrioritizedPacketQueue` 存待发包，设计要点：

### 多优先级

数字越小越优先（`prioritized_packet_queue.cc`）：

| 优先级 | 包类型 | 原因 |
|--------|--------|------|
| 0 | 音频 | 实时性最强，绝不能等 |
| 1 | 音频重传 | 音频丢了要尽快补 |
| 2 | 视频重传 | 视频重传次之 |
| 3 | 视频 / FEC | 正常视频 |
| 4 | 填充 | 可有可无 |

### 两层结构（重点理解）

- **每个 SSRC（流）一个 `StreamQueue`**，内部再按 5 个优先级各一个 FIFO `deque`
- **每个优先级一个轮询表 `streams_by_prio_[prio]`**：存放"该优先级有待发包的流"

### Pop 怎么决定发哪个？（round-robin 轮询）

```cpp
// 取当前最高非空优先级
// 从该优先级的轮询表头取一个流
// 取该流队首的包
// 把这个流移到轮询表尾  ← 关键：公平轮转
```

**为什么轮询？** 假设两个视频流都是优先级 3，如果不轮询，流 A 可能被流 B 饿死。轮询保证同一优先级的多流**公平轮流**送出。

## 七、定时器（Timer / TaskQueue）——谁来触发？

Pacer 是**事件/定时驱动**的。分层：

### 关键分离：定时器与逻辑解耦

- **`PacingController`**：纯逻辑，**不含定时器**。只暴露 `NextSendTime()`（下次该处理的时间）
- **`TaskQueuePacedSender`**：含定时器，负责在 `NextSendTime()` 的时刻调用 `ProcessPackets()`

这个解耦很重要：**pacer 自己"不知道"现在几点，只告诉宿主"什么时候该叫我"**。宿主用自己的定时器去唤醒它。

### 定时流程（`TaskQueuePacedSender::MaybeProcessPackets`）

```
1. 调用 NextSendTime() 得到下次该处理的时间
2. 如果 "下次该处理的时间 ≤ 当前时间"：
     → 调用 ProcessPackets() 处理（可能处理多个周期）
     → 更新统计
3. 重新算 NextSendTime()
4. 用 PostDelayedTask 安排延迟任务，延时 = 直到下次处理的时间
5. 定时器到点 → 回到步骤1，形成闭环
```

伪代码：
```cpp
void OnTimerFired() {
  auto next = PacingController::NextSendTime();
  while (next <= now) {
    ProcessPackets();
    next = PacingController::NextSendTime();
  }
  ScheduleTimer(next - now);  // 睡到下次
}
```

### NextSendTime 会返回哪些"下一次"

- 暂停 → 500ms 后发 keepalive（保持链路活跃）
- 探测中 → 探测时间
- 队列非空 → 还清债务的时间（前面算的公式）
- 队列空 + 有填充 → 填充还债时间
- 无事可做 → 500ms 兜底

## 八、ProcessPackets —— 发送主循环

这是 pacer 的"引擎"。一次调用完成一次（或多次）发送决策：

```
ProcessPackets():
  // 1. 若需 keepalive，先发 keepalive
  // 2. 若暂停，返回
  // 3. target_send_time = NextSendTime()
  //    若 now 还没到 target（且非探测）→ 只还债，不发包，返回
  // 4. 用流逝时间还债：UpdateBudgetWithElapsedTime(Δt)
  // 5. 主循环：
  //    a. 取包 GetPendingPacket() → Pop 队列
  //    b. 若队列空且可填充 → GeneratePadding 生成填充，继续
  //    c. SendPacket 发出 → OnPacketSent 借债(+size)
  //    d. 重新算 target_send_time
  //       若 target > now → 退出循环（发够了）
  // 6. 统计、更新
```

## 九、整个机制的"时间线"直观动画

假设速率 1Mbps，一个 6000 字节（48kb）的视频包：

```
t=0s   入队，债务=0，突发窗口内 → 立即允许发
t=0s   发出包1 → 债务 = +48kb
       NextSendTime 重算：还债时间 = 48kb / 1Mbps = 48ms
       → 定时器设 48ms 后唤醒
t=48ms 定时器唤醒 → 还债 1Mbps×48ms = 48kb → 债务=0
       又到了可发状态 → 发出下一个包
```

看，**包被"摊平"成 48ms 一个**，而不是瞬间全发。这就是平滑的效果。而且债务自动地在时间中"熔解"，无需精确计时器，容忍定时器调度误差（因为债务是基于"真实流逝时间"算的，而非固定 tick）。

## 十、一图总结核心概念关系

```
   数据突发到达(编码器)
        │
        ▼
  [队列 PrioritizedPacketQueue]   ← 按优先级 + 每流FIFO + 轮询存储
        │  Pop 取包
        ▼
  [债务桶 LeakyBucket]  media_debt_ / padding_debt_
        │  gate：债务是否在突发允许范围内？
        ▼  允许
  发包 → 借债(debt += size)   ← 发一个包子就多欠一点
        │
      时间流逝
        ▼
  还债(debt -= rate×Δt)       ← 债务随时间自动熔解
        │
        ▼
  NextSendTime() → 定时器到点 → ProcessPackets()  → 循环
```

## 核心概念速查表

| 概念 | 本质 | 关键公式/行为 |
|------|------|--------------|
| **债务 debt** | 相对匀速"多发了多少" | `debt += size`（发包） |
| **还债** | 债务随时间熔解 | `debt -= min(debt, rate×Δt)` |
| **队列 queue** | 待发包按优先级存储 | 每SSRC FIFO + 每优先级轮询 |
| **突发 burst** | 允许短时超速降延迟 | `send_burst > drain_time → 可立即发` |
| **门控 gate** | 决定能否发包 | `now 是否到了 NextSendTime()` |
| **定时器** | 驱动处理的唤醒机制 | `NextSendTime()` → 延迟任务 → ProcessPackets |
| **NextSendTime** | 下一次该处理的时间 | 队列非空 = `last_process + drain_time`（或立即） |

---

理解了这些概念，就可以开始自己写代码了。按"队列层 → 逻辑层(债务/门控) → 定时层"的顺序实现最清晰。
