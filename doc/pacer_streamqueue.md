# StreamQueue（每流子队列）中配版设计

基于对 `modules/pacing/prioritized_packet_queue.h/.cc` 的完整阅读。`StreamQueue` 是"每个流"的子队列，内部有 5 个优先级 FIFO（`packets_[5]`），被 `PrioritizedPacketQueue` 持有。

## 一、构造函数 / 特殊成员函数

| 方法 | 作用 |
|------|------|
| `explicit StreamQueue(Timestamp creation_time)` | 构造，用 `creation_time` 初始化 `last_enqueue_time_` |
| `StreamQueue(StreamQueue&&) = default` | 移动构造（能放进容器/被重分配） |
| `StreamQueue& operator=(StreamQueue&&) = default` | 移动赋值 |
| `StreamQueue(const StreamQueue&) = delete` | 禁止拷贝（资源型对象） |
| `StreamQueue& operator=(const StreamQueue&) = delete` | 禁止拷贝赋值 |

## 二、存/取核心

| 方法 | 作用 |
|------|------|
| `bool EnqueuePacket(QueuedPacket packet, int priority_level)` | 入队到某优先级。**返回值：该优先级从 0→非0 时返回 true**（用于通知上层把流加入轮询表） |
| `QueuedPacket DequeuePacket(int priority_level)` | 从某优先级出队（返回队首包） |
| `bool HasPacketsAtPrio(int priority_level)` | 该优先级是否有包 |
| `bool IsEmpty()` | 所有优先级都空？ |

## 三、统计 / 查询

| 方法 | 作用 |
|------|------|
| `bool has_keyframe_packets()` | 该流是否有关键帧在排队（中配版可做可不做） |
| `Timestamp LeadingPacketEnqueueTime(int priority_level)` | 某优先级队首包的入队时间 |
| `Timestamp LastEnqueueTime()` | 该流最近入队时间（供惰性清理 inactive 流用） |
| `std::array<std::deque<QueuedPacket>, kNumPriorityLevels> DequeueAll()` | 一次性取走该流所有包（`RemovePacketsForSsrc` 用，中配版可选） |

## 四、内部状态

```cpp
std::deque<QueuedPacket> packets_[kNumPriorityLevels];  // 5个优先级FIFO
Timestamp last_enqueue_time_;                           // 最近入队时间
int num_keyframe_packets_;                              // 关键帧计数
```

## 五、关键方法语义

**`EnqueuePacket` 返回 bool 是关键设计：**
```cpp
bool StreamQueue::EnqueuePacket(QueuedPacket packet, int priority_level) {
  if (packet.packet->is_key_frame()) ++num_keyframe_packets_;
  bool first_packet_at_level = packets_[priority_level].empty();  // 该级从空变非空？
  packets_[priority_level].push_back(std::move(packet));
  return first_packet_at_level;   // true → 上层需把此流加入轮询表
}
```
返回 true 表示"这个优先级从无到有了"，上层 `PrioritizedPacketQueue` 据此把流挂进 `streams_by_prio_[prio]` 轮询表。

**`DequeuePacket` 同步维护关键帧计数：**
```cpp
QueuedPacket DequeuePacket(int priority_level) {
  QueuedPacket packet = std::move(packets_[priority_level].front());
  packets_[priority_level].pop_front();
  if (packet.packet->is_key_frame()) --num_keyframe_packets_;
  return packet;
}
```

## 六、中配版最简集合（推荐）

**必选：**
```
构造函数(creation_time) + 移动构造/赋值
EnqueuePacket / DequeuePacket / HasPacketsAtPrio / IsEmpty
```

**可选**（看是否要多流公平/统计/删除）：`has_keyframe_packets`、`LeadingPacketEnqueueTime`、`LastEnqueueTime`、`DequeueAll`。

**关键提醒**：`StreamQueue` 单独存在意义不大，必须配合上层的**轮询表**和 **`PrioritizedPacketQueue::Pop()` 的 round-robin** 才构成完整媒体队列。
