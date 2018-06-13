//
// Created by frank on 18-1-2.
//

#ifndef EVA_ANALYZER_H
#define EVA_ANALYZER_H

#include <atomic>

#include <eva/TcpFlow.h>
#include <eva/Filter.h>

namespace eva
{

enum Result
{
    SLOW_STAR_LIMITED,
    BANDWIDTH_LIMITED,
    SENDER_LIMITED,
    RECEIVER_LIMITED,
    CONGESTION_LIMITED,
    UNKNOWN_LIMITED,
    N_RESULT_TYPES,
};



class Analyzer: public TcpFlow<Analyzer>
{
public:
    explicit Analyzer(const DataUnit& dat):
            TcpFlow(dat),
            bandwidthFilter_(10, 0, 0),
            rtprop_(-1),
            rtpropTimestamp_(Timestamp::invalid()),
            votes_(N_RESULT_TYPES),
            maxDeliveryRate_(0),
            smallUnitCount_(0),
            prevSmallUnitCount_(0),
            prevFlightSize1_(0),
            prevFlightSize2_(0),
            prevFlightSize3_(0),
            rttTooLongCount_(0),
            rttHugeCount_(0),
            ackCount_(0),
            seeRexmit_(false),
            isSlowStart_(true)
    {}

    explicit Analyzer(const AckUnit& ack):
            TcpFlow(ack),
            bandwidthFilter_(10, 0, 0),
            rtprop_(-1),
            rtpropTimestamp_(Timestamp::invalid()),
            votes_(N_RESULT_TYPES),
            maxDeliveryRate_(0),
            smallUnitCount_(0),
            prevSmallUnitCount_(0),
            prevFlightSize1_(0),
            prevFlightSize2_(0),
            prevFlightSize3_(0),
            rttTooLongCount_(0),
            rttHugeCount_(0),
            ackCount_(0),
            seeRexmit_(false),
            isSlowStart_(true)
    {}

    ~Analyzer();

    void onRateSample(const RateSample& rs, const AckUnit& ackUnit);
    void onNewRoundtrip(Timestamp now,
                        Timestamp lastAckTime,
                        int64_t bytesAcked,
                        int64_t totalAckInterval,
                        int64_t totalAckCount,
                        int32_t currFlightSize);
    void AfterRoundTrip(int32_t currFlightSize);
    void onTimeoutRxmit(Timestamp first, Timestamp rexmit);
    void onQuitSlowStart(Timestamp when);

    int64_t bdp() const;

private:
    Result countVotes();

private:
    typedef WindowedFilter<
            int64_t,
            MaxFilter<int64_t>,
            uint32_t,
            uint32_t>
            MaxBandwidthFilter;

    MaxBandwidthFilter bandwidthFilter_;
    int64_t   rtprop_;
    Timestamp rtpropTimestamp_;
    std::vector<int> votes_;

    int64_t maxDeliveryRate_;

    int smallUnitCount_;
    int prevSmallUnitCount_;
    int32_t prevFlightSize1_;
    int32_t prevFlightSize2_;
    int32_t prevFlightSize3_;
    int rttTooLongCount_;
    int rttHugeCount_;
    int ackCount_;


    Timestamp firstAckTime_;   // first ack time in this round trip

    bool seeRexmit_;
    bool isSlowStart_;
    Timestamp slowStartQuitTime;
};

}


#endif //EVA_ANALYZER_H
