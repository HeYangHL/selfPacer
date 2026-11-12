// selfPacer 初版演示 / 组合根：
//   SystemTimeStamp + PacerController + TimerTask + PacketSender + PacketToQueue
// 串起来的方式：SetWaker -> Start -> SendToQueue -> Stop。
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "timer_task.hpp"
#include "pacer_controller.hpp"
#include "send_packet_to_network.hpp"
#include "send_packet_to_queue.hpp"
#include "system_timestamp.hpp"

// 演示用的发送出口：记录每个包的发送时刻/大小，并打印间隔。
class ConsoleSender : public PacketSender {
public:
    explicit ConsoleSender(SystemTimeStamp& clock) : clock_(clock) {}

    void SendPacket(std::unique_ptr<MediaPacket> packet) override {
        const int64_t now = clock_.TimeMillis();
        const size_t size = packet->get_size();
        const uint32_t flow = packet->get_flow_id();

        std::lock_guard<std::mutex> lock(mutex_);
        int64_t dt = last_send_time_ms_ < 0 ? -1 : now - last_send_time_ms_;
        last_send_time_ms_ = now;

        ++count_;
        if (dt >= 0) {
            if (min_dt_ms_ < 0 || dt < min_dt_ms_) min_dt_ms_ = dt;
            if (dt > max_dt_ms_) max_dt_ms_ = dt;
            dt_sum_ms_ += dt;
            ++dt_count_;
        }
        printf("  send #%d flow=%u size=%zu dt=%lldms\n",
               count_, flow, size, static_cast<long long>(dt));
    }

    void PrintSummary(int accepted, int dropped) {
        std::lock_guard<std::mutex> lock(mutex_);
        const double avg = dt_count_ > 0 ? (double)dt_sum_ms_ / dt_count_ : 0.0;
        printf("\nsummary: enqueue accepted=%d dropped=%d sent=%d\n",
               accepted, dropped, count_);
        printf("         interval min=%lldms avg=%.2fms max=%lldms\n",
               static_cast<long long>(min_dt_ms_), avg,
               static_cast<long long>(max_dt_ms_));
    }

private:
    SystemTimeStamp& clock_;
    std::mutex mutex_;
    int count_ = 0;
    int64_t last_send_time_ms_ = -1;
    int64_t min_dt_ms_ = -1;
    int64_t max_dt_ms_ = 0;
    int64_t dt_sum_ms_ = 0;
    int64_t dt_count_ = 0;
};

int main() {
    // 1) 时钟与配置
    SystemTimeStamp clock;
    PacerConfig config;
    config.media_rate_bps = 2000000;   // 2 Mbps -> 1000B 包约 4ms 一个

    // 2) 逻辑层 / 发送层 / 定时层 / 入队适配层
    PacerController controller(clock, config);
    ConsoleSender sender(clock);
    TimerTask timer(controller, clock, sender);
    PacketToQueue queue(&controller);

    // 3) 关键接线：入队路径挂到定时层，然后启动自驱动线程
    queue.SetWaker(&timer);
    timer.Start();

    // 4) 第一批：20 个 1000B 包，观察均匀铺开
    std::vector<uint8_t> payload(1000, 0xAB);
    printf("SendToQueue: 20 x 1000B at %d bps (expect ~4ms/pkt)\n",
           config.media_rate_bps);
    int accepted = 0;
    int dropped = 0;
    for (int i = 0; i < 20; ++i) {
        if (queue.SendToQueue(1, payload) == 0) {
            ++accepted;
        } else {
            ++dropped;
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // 5) 第二批：验证“空闲等待”能被入队唤醒
    printf("\nqueue drained, SendToQueue 5 more (wake from idle)\n");
    for (int i = 0; i < 5; ++i) {
        if (queue.SendToQueue(2, payload) == 0) {
            ++accepted;
        } else {
            ++dropped;
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 6) 停止定时器并输出统计
    timer.Stop();
    sender.PrintSummary(accepted, dropped);
    return 0;
}
