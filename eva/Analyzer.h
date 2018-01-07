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
            seeSmallUnit_(false),
            isSlowStart_(true)
    {}

    explicit Analyzer(const AckUnit& ack):
            TcpFlow(ack),
            bandwidthFilter_(10, 0, 0),
            rtprop_(-1),
            rtpropTimestamp_(Timestamp::invalid()),
            votes_(N_RESULT_TYPES),
            seeSmallUnit_(false),
            isSlowStart_(true)
    {}

    ~Analyzer(){}

    void onRateSample(const RateSample& rs);
    void onNewRoundtrip(Timestamp when);
    void onTimeoutRxmit(Timestamp first, Timestamp rexmit);
    void onQuitSlowStart(Timestamp when);

    uint32_t bdp() const;

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

    bool seeSmallUnit_;
    Timestamp firstAckTime_;   // first ack time in this round trip

    bool isSlowStart_;
    Timestamp slowStartQuitTime;
};

}


#endif //EVA_ANALYZER_H
