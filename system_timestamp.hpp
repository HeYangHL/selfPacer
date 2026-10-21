#ifndef _SYSTEM_TIMESTAMP_HPP_
#define _SYSTEM_TIMESTAMP_HPP_

#include <iostream>


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

private:

};


#endif