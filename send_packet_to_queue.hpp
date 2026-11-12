#ifndef _SEND_PACKET_TO_QUEUE_HPP_
#define _SEND_PACKET_TO_QUEUE_HPP_

#include <cstdint>
#include <memory>
#include <vector>

#include "media_packet.hpp"
#include "pacer_controller.hpp"

class TimerTask;

class PacketToQueue {
public:
    explicit PacketToQueue(PacerController* controller) : controller_(controller) {}

    // 绑定定时层后，入队走 TimerTask::Enqueue（与发送线程共用锁并唤醒）。
    void SetWaker(TimerTask* waker) { waker_ = waker; }

    // 返回值：0=入队成功；-1=背压丢弃；-2=参数错误
    int SendToQueue(uint64_t stream_id, const uint8_t* payload, size_t size);
    int SendToQueue(uint64_t stream_id, const std::vector<uint8_t>& payload)
    {
        return SendToQueue(stream_id, payload.data(), payload.size());
    }

private:
    PacerController* controller_;
    TimerTask* waker_ = nullptr; // 可为空（单线程直接入队）
};

#endif
