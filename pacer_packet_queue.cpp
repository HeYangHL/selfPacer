#include "pacer_packet_queue.hpp"

int64_t PacerPacketQueue::QueuedPacket::PacketSize() const
{
    return packet ? static_cast<int64_t>(packet->get_size()) : 0;
}

PacerPacketQueue::StreamQueue::StreamQueue(int64_t creation_time)
    : last_enqueue_time_(creation_time)
{
}

bool PacerPacketQueue::StreamQueue::EnqueuePacket(QueuedPacket packet, int priority_level)
{
    // 该优先级从空变非空时返回 true，上层据此把流挂进轮询表
    bool first_packet_at_level = packets_[priority_level].empty();
    last_enqueue_time_ = packet.enqueue_time;
    packets_[priority_level].push_back(std::move(packet));
    return first_packet_at_level;
}

PacerPacketQueue::QueuedPacket PacerPacketQueue::StreamQueue::DequeuePacket(int priority_level)
{
    QueuedPacket packet = std::move(packets_[priority_level].front());
    packets_[priority_level].pop_front();
    return packet;
}

bool PacerPacketQueue::StreamQueue::HasPacketsAtPrio(int priority_level) const
{
    return !packets_[priority_level].empty();
}

bool PacerPacketQueue::StreamQueue::IsEmpty() const
{
    for (int prio = 0; prio < kNumPriorityLevels; ++prio) {
        if (!packets_[prio].empty()) {
            return false;
        }
    }
    return true;
}

void PacerPacketQueue::Push(int64_t enqueue_time, std::unique_ptr<MediaPacket> packet)
{
    uint32_t flow_id = packet->get_flow_id();
    auto[it, inserted] = streams_.emplace(flow_id, nullptr);
    if(inserted)
    {
        it->second = std::make_unique<StreamQueue>(enqueue_time);
    }
    StreamQueue *stream_queue = it->second.get();
    int prio_level = packet->get_priority();
    if(prio_level < 0)
        prio_level = 0;
    if(prio_level >= kNumPriorityLevels)
        prio_level = kNumPriorityLevels - 1;
    
    auto enqueue_time_iterator = enqueue_times_.insert(enqueue_times_.end(), enqueue_time);

    QueuedPacket queued_packet;
    queued_packet.enqueue_time = enqueue_time;
    queued_packet.packet = std::move(packet);
    queued_packet.enqueue_time_iterator =  enqueue_time_iterator;

    size_packets_ += 1;
    size_payload_ += queued_packet.PacketSize();

    if(stream_queue->EnqueuePacket(std::move(queued_packet), prio_level)){
        streams_by_prio_[prio_level].push_back(stream_queue);
    }
    if (prio_level < top_active_prio_level_) {
        top_active_prio_level_ = prio_level;
    }
}

std::unique_ptr<MediaPacket> PacerPacketQueue::Pop()
{
    if (Empty()) {
        return nullptr;
    }

    StreamQueue* stream_queue = streams_by_prio_[top_active_prio_level_].front();
    QueuedPacket queued_packet = stream_queue->DequeuePacket(top_active_prio_level_);

    size_packets_ -= 1;
    size_payload_ -= queued_packet.PacketSize();
    enqueue_times_.erase(queued_packet.enqueue_time_iterator);

    // 流转到轮询表队尾；该优先级已空则移出轮询表
    streams_by_prio_[top_active_prio_level_].pop_front();
    if (stream_queue->HasPacketsAtPrio(top_active_prio_level_)) {
        streams_by_prio_[top_active_prio_level_].push_back(stream_queue);
    } else {
        while (top_active_prio_level_ < kNumPriorityLevels &&
               streams_by_prio_[top_active_prio_level_].empty()) {
            ++top_active_prio_level_;
        }
    }

    return std::move(queued_packet.packet);
}

bool PacerPacketQueue::Empty() const
{
    return size_packets_ == 0;
}

int PacerPacketQueue::SizeInPackets() const
{
    return size_packets_;
}

int64_t PacerPacketQueue::SizeInPayloadBytes() const
{
    return size_payload_;
}

int64_t PacerPacketQueue::OldestEnqueueTime() const
{
    if (enqueue_times_.empty()) {
        return 0;
    }
    return enqueue_times_.front();
}

void PacerPacketQueue::RemovePacketsForStream(uint32_t flow_id)
{
    auto it = streams_.find(flow_id);
    if (it == streams_.end()) {
        return;
    }
    StreamQueue* stream_queue = it->second.get();

    for (int prio = 0; prio < kNumPriorityLevels; ++prio) {
        while (stream_queue->HasPacketsAtPrio(prio)) {
            QueuedPacket queued_packet = stream_queue->DequeuePacket(prio);
            size_packets_ -= 1;
            size_payload_ -= queued_packet.PacketSize();
            enqueue_times_.erase(queued_packet.enqueue_time_iterator);
        }
        auto& round_robin = streams_by_prio_[prio];
        for (auto entry = round_robin.begin(); entry != round_robin.end();) {
            if (*entry == stream_queue) {
                entry = round_robin.erase(entry);
            } else {
                ++entry;
            }
        }
    }

    streams_.erase(it);

    top_active_prio_level_ = kNumPriorityLevels - 1;
    for (int prio = 0; prio < kNumPriorityLevels; ++prio) {
        if (!streams_by_prio_[prio].empty()) {
            top_active_prio_level_ = prio;
            break;
        }
    }
}
