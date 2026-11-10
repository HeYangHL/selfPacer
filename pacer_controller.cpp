#include "pacer_controller.hpp"

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
    queue_.Push(now, std::move(packet));
    return EnqueueResult::kAccepted;
}