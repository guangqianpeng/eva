//
// Created by frank on 18-1-2.
//

#ifndef EVA_RATESAMPLE_H
#define EVA_RATESAMPLE_H

#include <eva/util.h>

namespace eva
{

struct RateSample
{
    Timestamp  rtt;

    Timestamp  ackReceivedTime;
    Timestamp  dataSentTime;

    int64_t    deliveryRate; // B/ms = kB/s
    Timestamp  interval;
    uint32_t   delivered;
    uint32_t   priorDelivered;
    Timestamp  priorTime;
    Timestamp  sendElapsed;
    Timestamp  ackElapsed;
    bool       isSenderLimited;
    bool       isReceiverLimited;
    bool       seeSmallUnit;
};

}


#endif //EVA_RATESAMPLE_H
