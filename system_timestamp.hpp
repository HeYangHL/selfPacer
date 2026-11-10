#ifndef _SYSTEM_TIMESTAMP_HPP_
#define _SYSTEM_TIMESTAMP_HPP_

#include <iostream>
#include <random>
#include <mutex>
#include <cstdint>

static const int64_t kNumMillisecsPerSec = INT64_C(1000);
static const int64_t kNumMicrosecsPerSec = INT64_C(1000000);
static const int64_t kNumNanosecsPerSec = INT64_C(1000000000);

class SystemTimeStamp{
public:
    SystemTimeStamp(){};
    ~SystemTimeStamp(){};
    int64_t SystemTimeNanos(void);
    int64_t TimeNanos(void);
    int64_t TimeMillis(void);
    int64_t TimeMicros(void);
    uint32_t Rand();
    uint64_t RandUint64();
private:
    std::mt19937_64 engine_ = std::mt19937_64(std::random_device{}());
    std::mutex mutex_;
};


#endif