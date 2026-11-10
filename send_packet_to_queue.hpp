#ifndef _SEND_PACKET_TO_QUEUE_HPP_
#define _SEND_PACKET_TO_QUEUE_HPP_

#include <cstdint>
#include <memory>
#include <vector>

#include "media_packet.hpp"
#include "pacer_controller.hpp"

class PacketToQueue {
public:
    explicit PacketToQueue(PacerController* controller) : controller_(controller) {}

    // 返回值：0=入队成功；-1=背压丢弃；-2=参数错误
    int SendToQueue(uint64_t stream_id, const uint8_t* payload, size_t size);
    int SendToQueue(uint64_t stream_id, const std::vector<uint8_t>& payload)
    {
        return SendToQueue(stream_id, payload.data(), payload.size());
    }

private:
    PacerController* controller_;
};

#endif
