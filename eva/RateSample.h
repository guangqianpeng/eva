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
    int64_t    rtt = -1; //us

    Timestamp  ackReceivedTime;
    Timestamp  dataSentTime;

    int64_t    deliveryRate = 0; // B/ms = kB/s
    int64_t    interval = -1; //us
    int64_t    delivered = 0;
    int64_t    priorDelivered = 0;
    Timestamp  priorTime;
    int64_t    sendElapsed = -1; //us
    int64_t    ackElapsed = -1;  //us
    bool       isSenderLimited = false;
    bool       isReceiverLimited = false;
    bool       seeSmallUnit = false;
};

}


#endif //EVA_RATESAMPLE_H
