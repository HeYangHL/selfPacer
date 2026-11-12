#include "pacer_controller.hpp"

#include "send_packet_to_network.hpp"

PacerController::PacerController(SystemTimeStamp &clock, const PacerConfig& config):config_(config),clock_(clock)
{}

bool PacerController::BlockedByBackpressure()
{
    if(config_.media_rate_bps <= 0)
        return false;
    return (config_.enable_pacer_backpressure && (queue_.SizeInPayloadBytes()* 8 * 1000/config_.media_rate_bps > config_.max_queue_time_ms));
}

PacerController::EnqueueResult PacerController::EnqueuePacket(std::unique_ptr<MediaPacket> packet)
{
    if(!packet)
        return EnqueueResult::kAccepted;
    //背压检查
    if(BlockedByBackpressure())
        return EnqueueResult::kDroppedBackpressure;
    int64_t now = clock_.TimeMillis();
    const bool was_empty = queue_.Empty();
    queue_.Push(now, std::move(packet));
    // 队列由空变非空：首包尽快发（对应 webrtc 入队后 PostMaybeProcessPackets 立即处理）
    if (was_empty) {
        next_send_time_ms_ = now;
    }
    return EnqueueResult::kAccepted;
}

int64_t PacerController::NextSendTimeMs() const
{
    if (queue_.Empty()) {
        return kNoPendingSendTime;
    }
    return next_send_time_ms_;
}

int PacerController::ProcessPackets(int64_t now_ms, PacketSender& sender)
{
    int sent = 0;
    while (!queue_.Empty() && sent < kMaxPacketsPerProcess) {
        if (now_ms < next_send_time_ms_) {
            break;
        }
        std::unique_ptr<MediaPacket> packet = queue_.Pop();
        if (!packet) {
            break;
        }
        const int64_t size = static_cast<int64_t>(packet->get_size());
        sender.SendPacket(std::move(packet));
        ++sent;

        // 初版：按码率做简单等间隔推进。
        // TODO: 后续替换成 webrtc 的双桶债务算法（media_debt_/padding_debt_ + 突发窗口）。
        int64_t interval_ms = 0;
        if (config_.media_rate_bps > 0) {
            interval_ms = size * 8 * 1000 / config_.media_rate_bps;
        }
        if (interval_ms <= 0) {
            interval_ms = 1;
        }
        // 累加而非“从 now 起算”，避免每包累积调度延迟；
        // 若已经落后于 now，本轮会连续补发（catch-up），由 kMaxPacketsPerProcess 兜底。
        next_send_time_ms_ += interval_ms;
    }

    if (sent > 0) {
        sender.OnBatchComplete();
    }
    if (queue_.Empty()) {
        // 空队列时重置，下一次入队会重新置为零延迟
        next_send_time_ms_ = 0;
    }
    return sent;
}
