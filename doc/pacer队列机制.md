# PrioritizedPacketQueue 队列机制详解

基于对 `modules/pacing/prioritized_packet_queue.cc/.h` 的完整阅读。这是 pacer 队列层的核心结构。

## 一、两级结构

```
PrioritizedPacketQueue（总队列）
├── streams_: unordered_map<ssrc, StreamQueue>      ← 按 SSRC 分流
│       每流一个 StreamQueue
│       └── packets_[kNumPriorityLevels]: deque<QueuedPacket>   ← 每流5个优先级FIFO
│
├── streams_by_prio_[kNumPriorityLevels]: deque<StreamQueue*>   ← 每优先级一个轮询表
│
└── enqueue_times_: list<Timestamp>                   ← 有序入队时间（算最老包）
```

两点精确说明：
1. `StreamQueue` 按 **SSRC**（流）分组，不是"媒体流"（媒体流是音频/视频的抽象，这里的 key 是 `uint32_t ssrc`）。
2. `packets_[5]` 是**每流内部**的 5 个优先级 FIFO。

## 二、为什么每流要 5 个 deque？

`StreamQueue::packets_[kNumPriorityLevels]`（`prioritized_packet_queue.h:143`）表示**一个流内部**，同一 SSRC 发的不同包可以属于不同优先级（比如一个视频流可能既有正常视频包，又有因丢包产生的重传包、还有 FEC）。所以每个流把 5 个优先级各放一个 FIFO。

优先级映射（数字小优先，`prioritized_packet_queue.cc:34-63`）：
```
0 音频      (kAudio)
1 音频重传  (kRetransmission, original=audio)
2 视频重传  (kRetransmission, original=video)
3 视频/FEC  (kVideo / kForwardErrorCorrection)
4 填充      (kPadding)
```

## 三、完整协作流程

### 1. 入队 Push（`prioritized_packet_queue.cc:162`）

```
Push(ssrc, packet):
  1. streams_ 里找到/创建该 SSRC 的 StreamQueue
  2. 读 packet->packet_type() → 算 prio_level（GetPriorityForType）
  3. 调用 stream_queue->EnqueuePacket(queued, prio_level)
     → 把包 push 进 packets_[prio_level]，返回"该优先级从空变非空"
  4. 若该级从空变非空 → 把这个流加入 streams_by_prio_[prio_level] 轮询表
  5. 若新优先级 < 当前 top → 更新 top_active_prio_level_
```

**核心**：`EnqueuePacket` 返回 bool 触发"挂入轮询表"（`prioritized_packet_queue.cc:197`）：
```cpp
if (stream_queue->EnqueuePacket(std::move(queued_packed), prio_level)) {
  streams_by_prio_[prio_level].push_back(stream_queue);  // 流挂进该优先级轮询表
}
```

### 2. 出队 Pop（round-robin 轮询，`prioritized_packet_queue.cc:219`）

```
Pop():
  1. 取 top_active_prio_level_（当前最高优先级）
  2. 从 streams_by_prio_[top].front() 拿一个流
  3. 调 stream_queue->DequeuePacket(top) 取该流该优先级队首包
  4. 把该流移到 streams_by_prio_[top] 队尾   ← 公平轮转
  5. 若该流该优先级已空 → 从轮询表移除
```

源码（`prioritized_packet_queue.cc:231-236`）：
```cpp
// Remove the stream from head, readd at end if it still has packets
streams_by_prio_[top].pop_front();
if (stream_queue.HasPacketsAtPrio(top)) {
  streams_by_prio_[top].push_back(&stream_queue);   // 循环
} else {
  MaybeUpdateTopPrioLevel();
}
```

**为什么轮询？** 同优先级的多个流轮流发，防止一个流饿死其他流（比如两个视频流都是优先级 3）。

### 3. top_active_prio_level_ 的维护

`MaybeUpdateTopPrioLevel`（`prioritized_packet_queue.cc:417`）：当某个优先级的轮询表变空时，向下找下一个非空优先级的轮询表，更新 `top_active_prio_level_`。保证 `Pop` 永远优先取当前最高优先级。

## 四、无优先级差别时的行为对比（直观）

假设两个流 A、B 都有视频包（都优先级 3）：

```
轮询表 streams_by_prio_[3] = [A, B]

Pop() → 取 A 队首包，A 移到队尾 → [B, A]
Pop() → 取 B 队首包，B 移到队尾 → [A, B]
... 公平交替
```

如果有音频包来了（优先级 0），`top` 变为 0，`Pop` 优先取音频，视频暂时让路——保证音频最低延迟。

## 五、一句话总结

"每个媒体流一个 StreamQueue，每个流内部又有 5 个优先级的 deque（`packets_[5]`）"。

这套机制的灵魂：
- **PUSH**：包入队时按 `packet_type` 定优先级，存进**该流对应优先级**的 deque，且该优先级从空变非空时把流**挂入轮询表**；
- **POP**：始终取**当前最高优先级**轮询表**队首流**的一个包，发完把流移到队尾（公平轮转），发空则移除并下调 `top`。

这样既保证**高优先级先发**（音频>重传>视频>填充），又保证**同优先级多流公平**（轮询）。
