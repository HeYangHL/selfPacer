#include "delay_task.hpp"

#include <chrono>

TimerTask::TimerTask(PacerController& controller,
                     SystemTimeStamp& clock,
                     PacketSender& sender)
    : controller_(controller),
      clock_(clock),
      sender_(sender)
{
}

TimerTask::~TimerTask()
{
    Stop();
}

void TimerTask::Start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return;
    }
    running_ = true;
    wake_seq_ = 0;
    thread_ = std::thread([this] { Loop(); });
}

void TimerTask::Stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void TimerTask::WakeUp()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++wake_seq_;
    }
    cv_.notify_one();
}

PacerController::EnqueueResult TimerTask::Enqueue(std::unique_ptr<MediaPacket> packet)
{
    PacerController::EnqueueResult result;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result = controller_.EnqueuePacket(std::move(packet));
        if (result == PacerController::EnqueueResult::kAccepted) {
            ++wake_seq_;
        }
    }
    // 锁外 notify，避免被唤醒的线程立刻又抢同一把锁
    cv_.notify_one();
    return result;
}

void TimerTask::Loop()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_) {
        // 1) 空队列：无限等待，直到入队/停止（避免空转）
        if (controller_.Empty()) {
            const uint64_t seq = wake_seq_;
            cv_.wait(lock, [this, seq] {
                return !running_ || wake_seq_ != seq;
            });
            continue;
        }

        const int64_t now = clock_.TimeMillis();
        const int64_t next = controller_.NextSendTimeMs();

        // 防御：非空却拿不到待发时刻时，短暂退让，避免忙等
        if (next == PacerController::kNoPendingSendTime) {
            const uint64_t seq = wake_seq_;
            cv_.wait_for(lock, std::chrono::milliseconds(1), [this, seq] {
                return !running_ || wake_seq_ != seq;
            });
            continue;
        }

        // 2) 已到期：发送到期的包（可能一次多个，由 controller 决定），再重算
        if (next <= now) {
            controller_.ProcessPackets(now, sender_);
            continue;
        }

        // 3) 未到期：等到 next；入队/改配置会自增 wake_seq_ 提前唤醒
        const uint64_t seq = wake_seq_;
        cv_.wait_until(
            lock,
            std::chrono::steady_clock::now() + std::chrono::milliseconds(next - now),
            [this, seq] { return !running_ || wake_seq_ != seq; });
    }
}



