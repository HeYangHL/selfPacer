#ifndef __PACER_CONTROLLER_HPP__
#define __PACER_CONTROLLER_HPP__

#include <cstdint>
#include <limits>
#include <memory>

#include "pacer_packet_queue.hpp"
#include "system_timestamp.hpp"

class PacketSender;

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

    // 队列为空时 NextSendTimeMs() 的返回值（表示“没有待发时刻”）
    static constexpr int64_t kNoPendingSendTime = std::numeric_limits<int64_t>::max();
    // 初版：单次 ProcessPackets 最多发送的包数，避免长时间占用锁
    static constexpr int kMaxPacketsPerProcess = 64;

    explicit PacerController(SystemTimeStamp &clock, const PacerConfig& config = PacerConfig());
    bool BlockedByBackpressure();
    PacerController::EnqueueResult EnqueuePacket(std::unique_ptr<MediaPacket> packet);

    // ---- 定时层（DelayTask）依赖的接口 ----
    bool Empty() const { return queue_.Empty(); }
    // 下次允许发送的时刻(ms)；队列为空返回 kNoPendingSendTime
    int64_t NextSendTimeMs() const;
    // 发送所有“已到期”的包；返回本次发送的包数
    int ProcessPackets(int64_t now_ms, PacketSender& sender);

private:
    PacerPacketQueue queue_;
    PacerConfig config_;
    SystemTimeStamp& clock_;
    int64_t next_send_time_ms_ = 0; // 下次允许发送时刻
};


#endif
