#include "system_timestamp.hpp"

int64_t SystemTimeStamp::SystemTimeNanos(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC ,&ts);

    return kNumNanosecsPerSec * static_cast<int64_t>(ts.tv_sec) + static_cast<int64_t>(ts.tv_nsec);
}

int64_t SystemTimeStamp::TimeNanos(void) {
  return SystemTimeNanos();
}

int64_t SystemTimeStamp::TimeMillis(void) {
  return TimeNanos() / kNumMicrosecsPerSec;
}

int64_t SystemTimeStamp::TimeMicros(void) {
  return TimeNanos() / kNumMillisecsPerSec;
}