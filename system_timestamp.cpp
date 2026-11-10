#include "system_timestamp.hpp"

int64_t SystemTimeStamp::SystemTimeNanos(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  return kNumNanosecsPerSec * static_cast<int64_t>(ts.tv_sec) + static_cast<int64_t>(ts.tv_nsec);
}

int64_t SystemTimeStamp::TimeNanos(void)
{
  return SystemTimeNanos();
}

int64_t SystemTimeStamp::TimeMillis(void)
{
  return TimeNanos() / kNumMicrosecsPerSec;
}

int64_t SystemTimeStamp::TimeMicros(void)
{
  return TimeNanos() / kNumMillisecsPerSec;
}

uint64_t SystemTimeStamp::RandUint64()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return engine_();
}
uint32_t SystemTimeStamp::Rand()
{
  // 用 64 位随机源做乘法-移位，保证近似均匀
  uint64_t x = RandUint64();
  uint64_t result = x * (static_cast<uint64_t>(65536) + 1);
  return static_cast<uint32_t>(result >> 32);
}