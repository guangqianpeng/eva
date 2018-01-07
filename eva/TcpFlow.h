//
// Created by frank on 18-1-2.
//

#ifndef EVA_TCPFLOW_H
#define EVA_TCPFLOW_H

#include <deque>

#include <eva/Unit.h>
#include <eva/RateSample.h>
#include <eva/util.h>

namespace eva
{

template <typename Analyzer>
class TcpFlow: noncopyable
{
public:
    explicit TcpFlow(const DataUnit& dat);
    explicit TcpFlow(const AckUnit& ack);

    void onDataUnit(const DataUnit& dataUnit);
    void onAckUnit(const AckUnit& ackUnit);

    uint32_t pipeSize()       const { return pipeSize_; }
    uint32_t recvWindow()     const { return recvWindow_; }
    uint32_t mss()            const { return mss_; }

    uint32_t roundtripCount() const { return roundTripCount_; }

    const InetAddress& srcAddress() const { return srcAddress_; }
    const InetAddress& dstAddress() const { return dstAddress_; }

private:
    struct P
    {
        Sequence   sequence;
        uint32_t   length;
        uint32_t   delivered;
        uint32_t   ackUnitCount;
        Timestamp  sentTime;
        Timestamp  deliveredTime;
        Timestamp  firstSentTime;
        bool       isSlowStart;
        bool       isSenderLimited;
        bool       isReceiverLimited;
        bool       isSmallUnit;
    };
    std::deque<P> flow_;

    struct Roundtrip
    {
        bool       started;
        Sequence   startSequence;
        Sequence   endSequence;
        bool       seeSmallUnit;

        int32_t flightSize() const
        {
            assert(started);
            return endSequence - startSequence;
        }
    };
    Roundtrip currRoundtrip_;

private:
    void preHandleDataUnit(const DataUnit& dataUnit);
    bool handleDataUnit(const DataUnit& dataUnit);
    void postHandleDataUnit(const DataUnit& dataUnit);

    void preHandleAckUnit(const AckUnit& ackUnit);
    bool handleAckUnit(const AckUnit& ackUnit);
    void postHandleAckUnit(const AckUnit& ackUnit);
    bool updateRoundtripCount(const AckUnit& ackUnit);
    void updateRateSample(P& p, const AckUnit& ack, RateSample* rs);


    Analyzer& convert()
    {
        return static_cast<Analyzer&>(*this);
    }


private:
    bool seeMss_;
    bool seeWsc_;
    uint32_t mss_; // peer send mss in SYN
    uint32_t wsc_; // peer send wsc in SYN
    const InetAddress srcAddress_;
    const InetAddress dstAddress_;

    Sequence     nextSendSequence_;

    uint32_t     ackUnitCount_; // number of acks received, including dup ack
    uint32_t     roundTripCount_;
    int32_t      prevFlightSize_;

    uint32_t     delivered_;
    Timestamp    deliveredTime_;
    Timestamp    firstSentTime_;
    uint32_t     pipeSize_;
    uint32_t     recvWindow_;
    bool         isSlowStart_;
    bool         isSenderLimited_;
    bool         isReceiverLimited_;
};

class Analyzer;
extern template class TcpFlow<Analyzer>;

}

#endif //EVA_TCPFLOW_H
