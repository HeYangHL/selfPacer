#ifndef __PACER_CONTROLLER_HPP__
#define __PACER_CONTROLLER_HPP__

#include <iostream>
#include <memory>
#include "system_timestamp.hpp"
#include "pacer_packet_queue.hpp"

struct PacerConfig{
    int media_rate_bps = 2000000; //码率
    int max_queue_time_ms = 500; //背压阈值
    bool enable_pacer_backpressure = true; //是否开启背压
};

class PacerController{
public:
    enum class EnqueueResult {
        kAccepted = 0,
        kDroppedBackpressure = -1,  // 队列过长，丢弃新包
    };

    explicit PacerController(SystemTimeStamp &clock, const PacerConfig& config = PacerConfig());
    bool BlockedByBackpressure();
    PacerController::EnqueueResult EnqueuePacket(std::unique_ptr<MediaPacket> packet);
    
private:
    PacerPacketQueue queue_;
    PacerConfig config_;
    SystemTimeStamp& clock_;
};


#endif