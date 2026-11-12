#include "send_packet_to_queue.hpp"

#include "delay_task.hpp"

int PacketToQueue::SendToQueue(uint64_t stream_id, const uint8_t* payload, size_t size)
{
    if (controller_ == nullptr) {
        return -2;
    }

    auto packet = std::make_unique<MediaPacket>();
    // 设置流ID（多流时分流用）
    packet->set_flow_id(static_cast<uint32_t>(stream_id));
    // 设置包类型（网络包）
    packet->set_type(RtpPacketMediaType::NetWork);
    // 设置负载
    if (packet->set_playload(payload, size) != 0) {
        return -2;
    }

    // 包入队：有定时层时走它的线程安全入口（入队成功会自动唤醒发送线程）
    PacerController::EnqueueResult result;
    if (waker_ != nullptr) {
        result = waker_->Enqueue(std::move(packet));
    } else {
        result = controller_->EnqueuePacket(std::move(packet));
    }

    if (result == PacerController::EnqueueResult::kDroppedBackpressure) {
        return -1;
    }
    return 0;
}
