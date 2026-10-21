#ifndef _PACER_PACKET_QUEUE_HPP_
#define _PACER_PACKET_QUEUE_HPP_

#include <iostream>
#include <memory>
#include <deque>
#include <time.h>
#include <stdint.h>
#include <list>
#include "media_packet.hpp"


class PacerPacketQueue{
public:
    void Push(uint64_t timestamp, unsigned char *packet);
    unsigned char *Pop();
    bool Empty() const;
    int SizeInPackets() const;
    uint64_t SizeInPayloadBytes() const;
    uint64_t OldestEnqueueTime() const;
    void RemovePacketsForSsrc(uint32_t ssrc);
    private:
        static constexpr int kNumPriorityLevels = 5;
    class QueuedPacket {
    public:
        uint64_t PacketSize() const;
        std::unique_ptr<unsigned char *> packet;
        int64_t enqueue_time;
        std::list<int64_t>::iterator enqueue_time_iterator;
    } ;
    class StreamQueue{
    public:
        explicit StreamQueue(int64_t creation_time);
        StreamQueue(StreamQueue&&) = default;
        StreamQueue& operator=(StreamQueue&&) = default;
        StreamQueue(const StreamQueue&) = delete;
        StreamQueue& operator=(const StreamQueue&) = delete;

        bool EnqueuePacket(QueuedPacket packet, int priority_level);//入队优先级
        QueuedPacket DequeuePacket(int priority_level);//从某优先级出队
        bool HasPacketsAtPrio(int priority_level);//该优先级是否有包
        bool IsEmpty();//所有优先级都空

        std::deque<QueuedPacket> packets_[kNumPriorityLevels];  // 5个优先级FIFO
        int64_t last_enqueue_time_;                           // 最近入队时间
        int num_keyframe_packets_;                              // 关键帧计数

    private:
    };
private:

};

#endif