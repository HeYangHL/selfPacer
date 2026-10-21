# 媒体队列（PrioritizedPacketQueue）设计要点

基于对 WebRTC 源码的完整阅读（`modules/pacing/prioritized_packet_queue.cc/.h`）。队列和债务机制是自研 pacer 的两大基础，这篇专门讲媒体队列要实现的方法与内部结构。

## 一、对外 API（核心）

| 方法 | 作用 |
|------|------|
| `Push(enqueue_time, packet)` | 入队，记录入队时间 |
| `Pop()` | 出队（按优先级 + 轮询取最高优先级下一个包） |
| `Empty()` | 是否为空 |
| `SizeInPackets()` | 队列中包的数量 |
| `SizeInPayloadBytes()` | 队列中所有包的负载总字节数 |
| `OldestEnqueueTime()` | 最老包的入队时间（算延迟/排队时间） |
| `RemovePacketsForSsrc(ssrc)` | 删除某个流的所有包（关键帧刷新生效） |

## 二、可选的统计/管理方法

| 方法 | 作用 |
|------|------|
| `LeadingPacketEnqueueTime(type)` | 某类型队首包的入队时间（用于"不参与 pacing 的包"立即送） |
| `SizeInPacketsPerRtpPacketMediaType()` | 按媒体类型统计包数 |
| `AverageQueueTime()` | 平均排队时长（大队列排水用，中配版可不做） |
| `HasKeyframePackets(ssrc)` | 该流是否有关键帧在排队 |

## 三、内部结构（决定这些方法怎么实现）

### 优先级分级（数字小优先）

```
0 音频 > 1 音频重传 > 2 视频重传 > 3 视频/FEC > 4 填充
```

### 两层队列结构 —— 这是"媒体队列"的本质

```
PrioritizedPacketQueue
├── streams_: unordered_map<ssrc, StreamQueue>    每个流一个子队列
│       每个 StreamQueue 内部：
│       └── packets_[5]: deque<QueuedPacket>      按5优先级各一个FIFO
│
├── streams_by_prio_[5]: deque<StreamQueue*>      每优先级一个轮询表
│       （记录"该优先级有待发包的流"）
│
└── enqueue_times_: list<Timestamp>               有序的入队时间（算最老包）
```

### Pop() 的核心逻辑（轮询 round-robin）

```
Pop():
  1. 找当前最高非空优先级 top_active_prio_level_
  2. 从 streams_by_prio_[top] 表头取一个流
  3. 取该流该优先级 deque 的队首包
  4. 把该流移到 streams_by_prio_[top] 表尾  ← 公平轮转
  5. 若该流该优先级已空，从轮询表移除
```

**关键：为什么轮询？** 同优先级多流公平发送，避免某个流饿死其他流。

### 包怎么包装

```cpp
struct QueuedPacket {
  std::unique_ptr<RtpPacketToSend> packet;  // 或你自己的包类型
  Timestamp enqueue_time;                   // 入队时间
};
```

## 四、中配版推荐：最少要实现的方法

如果目标明确，**最小集合**是：

```
Push / Pop / Empty / SizeInPackets / SizeInPayloadBytes
+ 内部：优先级分级、每流FIFO、轮询
```

其余（`OldestEnqueueTime`、`RemovePacketsForSsrc`、按类型统计）在需要时才加。但**轮询和优先级是媒体队列的灵魂**，缺了就不是"媒体队列"了。
