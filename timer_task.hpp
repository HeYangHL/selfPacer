#ifndef _DELAY_TASK_HPP_
#define _DELAY_TASK_HPP_

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include "pacer_controller.hpp"
#include "send_packet_to_network.hpp"
#include "system_timestamp.hpp"

// 定时层：自驱动循环（参考 webrtc TaskQueuePacedSender / MaybeProcessPackets）。
//
// 模型：常驻线程阻塞在 condition_variable 上（空队列时无限等待、CPU 0）；
//       入队/改配置 -> WakeUp() 唤醒重算；到期 -> wait_until 自然返回 -> ProcessPackets。
// 因此不是“周期性定时器”，而是“一次性 deadline，每次醒来重算并重新等待”。
class TimerTask {
public:
    TimerTask(PacerController& controller,
              SystemTimeStamp& clock,
              PacketSender& sender);
    ~TimerTask();

    TimerTask(const TimerTask&) = delete;
    TimerTask& operator=(const TimerTask&) = delete;

    void Start();
    void Stop();

    // 入队/改配置后调用，唤醒循环提前重算 next_send_time。
    void WakeUp();

    // 线程安全入队：与发送循环共用同一把锁，入队成功后自动唤醒。
    PacerController::EnqueueResult Enqueue(std::unique_ptr<MediaPacket> packet);

private:
    void Loop();

    PacerController& controller_;
    SystemTimeStamp& clock_;
    PacketSender& sender_;

    std::mutex mutex_;
    std::condition_variable cv_;
    bool running_ = false;
    uint64_t wake_seq_ = 0; // 防丢唤醒：wait 前取快照，唤醒时自增
    std::thread thread_;
};

#endif
