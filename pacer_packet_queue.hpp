#ifndef _PACER_PACKET_QUEUE_HPP_
#define _PACER_PACKET_QUEUE_HPP_

#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <unordered_map>

#include "media_packet.hpp"

class PacerPacketQueue {
public:
    void Push(int64_t enqueue_time, std::unique_ptr<MediaPacket> packet);
    std::unique_ptr<MediaPacket> Pop();
    bool Empty() const;
    int SizeInPackets() const;
    int64_t SizeInPayloadBytes() const;
    int64_t OldestEnqueueTime() const;
    void RemovePacketsForStream(uint32_t flow_id);

private:
    // 优先级档位数 = 包类型数（当前枚举值即优先级，恒等映射）
    static constexpr int kNumPriorityLevels = static_cast<int>(kNumMediaTypes);

    class QueuedPacket {
    public:
        int64_t PacketSize() const;
        std::unique_ptr<MediaPacket> packet;
        int64_t enqueue_time = 0;
        std::list<int64_t>::iterator enqueue_time_iterator;
    };

    class StreamQueue {
    public:
        explicit StreamQueue(int64_t creation_time);
        StreamQueue(StreamQueue&&) = default;
        StreamQueue& operator=(StreamQueue&&) = default;
        StreamQueue(const StreamQueue&) = delete;
        StreamQueue& operator=(const StreamQueue&) = delete;

        bool EnqueuePacket(QueuedPacket packet, int priority_level);//入队优先级
        QueuedPacket DequeuePacket(int priority_level);//从某优先级出队
        bool HasPacketsAtPrio(int priority_level) const;//该优先级是否有包
        bool IsEmpty() const;//所有优先级都空

        std::deque<QueuedPacket> packets_[kNumPriorityLevels];  // 每个优先级一个 FIFO
        int64_t last_enqueue_time_;                              // 最近入队时间
    };

    // 每个流一个子队列；flow_id 不存在时懒创建
    std::unordered_map<uint32_t, std::unique_ptr<StreamQueue>> streams_;
    // 每个优先级一个轮询表，存放该优先级暂时有包的流向
    std::deque<StreamQueue*> streams_by_prio_[kNumPriorityLevels];
    // 当前非空的最低（最优先，优先级最高）优先级
    int top_active_prio_level_ = kNumPriorityLevels - 1;
    // 所有在队包的入队时刻，队首即 oldest
    std::list<int64_t> enqueue_times_;
    int size_packets_ = 0;
    int64_t size_payload_ = 0;
};

#endif
