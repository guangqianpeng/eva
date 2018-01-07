//
// Created by frank on 18-1-2.
//

#include <eva/hash.h>
#include <eva/TcpFlow.h>
#include <eva/Analyzer.h>
#include "Unit.h"

using namespace eva;

namespace
{

const Timestamp kMinRtt(1000); // 1ms
const uint32_t  kMinMss = 536;
const uint32_t  kMinWsc = 0;
const uint32_t  kMaxWsc = 7;
const int       kMaxReordered = 2000;

}

namespace eva
{

template class TcpFlow<Analyzer>;

}

template <typename Analyzer>
TcpFlow<Analyzer>::TcpFlow(const DataUnit& dat):
        seeMss_(false),
        seeWsc_(false),
        mss_(kMinMss),
        wsc_(kMinWsc),
        srcAddress_(dat.u->srcAddress),
        dstAddress_(dat.u->dstAddress),
        nextSendSequence_(dat.u->dataSequence),
        ackUnitCount_(0),
        roundTripCount_(0),
        prevFlightSize_(0),
        delivered_(0),
        deliveredTime_(dat.u->when),
        firstSentTime_(dat.u->when),
        pipeSize_(0),
        recvWindow_(0),
        isSlowStart_(true),
        isSenderLimited_(false),
        isReceiverLimited_(false)
{
    assert(!dat.u->isFIN() && !dat.u->isRST());
}

template <typename Analyzer>
TcpFlow<Analyzer>::TcpFlow(const AckUnit& ack):
        seeMss_(false),
        seeWsc_(false),
        mss_(kMinMss),
        wsc_(kMinWsc),
        srcAddress_(ack.u->dstAddress), // ack src and dst address should be reversed
        dstAddress_(ack.u->srcAddress),
        nextSendSequence_(0),
        ackUnitCount_(0),
        roundTripCount_(0),
        prevFlightSize_(0),
        delivered_(0),
        deliveredTime_(ack.u->when),
        firstSentTime_(ack.u->when),
        pipeSize_(0),
        isSlowStart_(true),
        isSenderLimited_(false),
        isReceiverLimited_(false)
{
    assert(ack.u->isSYN());
}


template <typename Analyzer>
void TcpFlow<Analyzer>::onDataUnit(const DataUnit& dataUnit)
{
    assert(dataUnit.u->srcAddress == srcAddress_);
    assert(dataUnit.u->dstAddress == dstAddress_);
    assert(dataUnit.u->dataLength > 0 ||
           dataUnit.u->isSYN() ||
           dataUnit.u->isFIN());

    if (dataUnit.u->isSYN()) {
        LOG_INFO << "[" << roundTripCount_ << "]" << " sender SYN";
    }
    else if (dataUnit.u->isFIN()) {
        LOG_INFO << "[" << roundTripCount_ << "]" << " sender FIN";
    }

    preHandleDataUnit(dataUnit);

    bool hasNewData = handleDataUnit(dataUnit);
    if (hasNewData) {
        postHandleDataUnit(dataUnit);
    }
}

template <typename Analyzer>
void TcpFlow<Analyzer>::preHandleDataUnit(const DataUnit& dataUnit)
{
    auto& u = *dataUnit.u;

    bool smallUnit = !u.isSYN() && !u.isFIN() &&
                       dataUnit.u->optionLength + dataUnit.u->dataLength < mss_;
    bool pipeNotFull = pipeSize_ < convert().bdp() * 9 / 10;



    // fixme: remove magic number
    isReceiverLimited_ = (pipeSize_ > recvWindow_ * 9 / 10 || recvWindow_ < mss_);
    isSenderLimited_ = (!isReceiverLimited_ &&
                        (smallUnit || pipeNotFull));

    auto dataAndOptionLen = dataUnit.u->dataLength +
                            dataUnit.u->optionLength;
    if (!seeMss_ && dataAndOptionLen > mss_) {
        // continuously estimate max segment size option
        // if we miss receiver's SYN
        mss_ = dataAndOptionLen;
    }
}

template <typename Analyzer>
bool TcpFlow<Analyzer>::handleDataUnit(const DataUnit& dataUnit)
{
    auto& u = *dataUnit.u;

    // new or idle connection
    if (pipeSize_ == 0)
        firstSentTime_ = deliveredTime_ = u.when;

    // append flow sequence
    P p;
    p.sequence = u.dataSequence;
    p.length = u.dataLength;
    p.delivered = delivered_;
    p.ackUnitCount = ackUnitCount_;
    p.sentTime = u.when;
    p.deliveredTime = deliveredTime_;
    p.firstSentTime = firstSentTime_;
    p.isSlowStart = isSlowStart_;
    p.isSenderLimited = isSenderLimited_;
    p.isReceiverLimited = isReceiverLimited_;
    p.isSmallUnit = !u.isSYN() &&
                    !u.isFIN() &&
                    u.optionLength + u.dataLength < mss_;

    if (nextSendSequence_ > u.dataSequence) {

        // sender reorder is rare, because we are at sender side
        // so it should be retransmit
        LOG_DEBUG << "[" << roundTripCount_ << "]"
                  << " sender retransmit";

        int step = 0;
        auto r = flow_.rbegin();
        for (; r != flow_.rend(); r++) {
            if (r->sequence == u.dataSequence) {
                if (r->ackUnitCount == ackUnitCount_) {
                    convert().onTimeoutRxmit(r->sentTime, u.when);

                    // fall back to slow start!!!
                    isSlowStart_ = true;
                }
                *r = p;
                break;
            }
            else if (r->sequence < u.dataSequence) {
                // spurious rexmit
                LOG_ERROR << "[" << roundTripCount_ << "]"
                          << " no matching data unit for rexmit, may be reordered unit. "
                          << " please run at sender side! ";
                break;
            }
            else if (++step >= kMaxReordered) {
                LOG_WARN << "[" << roundTripCount_ << "]"
                         << " sender back up too many steps, give up";
                break;
            }
        }
        if (r == flow_.rend()) {
            LOG_WARN << "[" << roundTripCount_ << "]"
                     << " spurious rexmit";
        }
        return false;
    }
    else if (nextSendSequence_ < u.dataSequence) {
        LOG_ERROR << "[" << roundTripCount_ << "]"
                  << " find reordered unit. please run at sender side!";
        return false;
    }
    else {
        flow_.push_back(p);
        return true;
    }
}

template <typename Analyzer>
void TcpFlow<Analyzer>::postHandleDataUnit(const DataUnit& dataUnit)
{
    auto &u = *dataUnit.u;

    pipeSize_ += u.dataLength;
    nextSendSequence_ = u.dataSequence +
                        u.dataLength +
                        u.isSYN() +
                        u.isFIN();

    if (!currRoundtrip_.started)
    {
        currRoundtrip_.started = true;
        currRoundtrip_.startSequence = u.dataSequence;
        currRoundtrip_.seeSmallUnit = false;
    }

    bool smallUnit = !u.isSYN() && !u.isFIN() &&
                     dataUnit.u->optionLength + dataUnit.u->dataLength < mss_;

    if (smallUnit) {
        currRoundtrip_.seeSmallUnit = true;
    }
}

template <typename Analyzer>
void TcpFlow<Analyzer>::onAckUnit(const AckUnit& ackUnit)
{
    assert(ackUnit.u->srcAddress == dstAddress_);
    assert(ackUnit.u->dstAddress == srcAddress_);
    assert(ackUnit.u->isSYN() ||
           ackUnit.u->isACK() ||
           ackUnit.u->isFIN());
    if (ackUnit.u->isSYN()) {
        LOG_INFO << "[" << roundTripCount_ << "]" << " receiver SYN";
    }
    else if (ackUnit.u->isFIN()) {
        LOG_INFO << "[" << roundTripCount_ << "]" << " receiver FIN";
    }

    preHandleAckUnit(ackUnit);
    bool hasDataAcked = handleAckUnit(ackUnit);
    if (hasDataAcked)
        postHandleAckUnit(ackUnit);
}

template <typename Analyzer>
void TcpFlow<Analyzer>::preHandleAckUnit(const AckUnit& ackUnit)
{
    if (ackUnit.u->isSYN()) {
        // luckily, we see the option in receiver's SYN
        seeMss_ = ackUnit.u->seeMss;
        seeWsc_ = ackUnit.u->seeWsc;
        mss_ = seeMss_ ? ackUnit.u->mss : kMinMss;
        wsc_ = seeWsc_ ? ackUnit.u->wsc : kMinWsc;
    }

    if (!seeWsc_) {
        // continuously estimate window scale option
        // if we miss receiver's SYN
        while (pipeSize_ > (ackUnit.u->recvWindow << wsc_))
            wsc_++;
        if (wsc_ > kMaxWsc) {
            LOG_ERROR << "bad window scale option = " << wsc_;
            wsc_ = kMaxWsc;
        }
    }

    ackUnitCount_++;

    // update latest receiver window
    recvWindow_ = ackUnit.u->recvWindow << wsc_;
}

template <typename Analyzer>
bool TcpFlow<Analyzer>::handleAckUnit(const AckUnit& ackUnit)
{
    auto& u = *ackUnit.u;

    uint32_t bytesAcked = 0;

    // a cumulative ack?
    auto it = flow_.begin();
    for (; it != flow_.end(); it++) {
        if (it->sequence < u.ackSequence) {
            if (it->deliveredTime.valid())
                // ensure this unit was not sacked
                bytesAcked += it->length;
        }
        else
            break;
    }

    // a selective ack?
    std::vector<P*> sacked;
    if (u.sackCount > 0) {
        sacked.reserve(u.sackCount);
        for (uint32_t i = 0; i < u.sackCount; i++) {
            auto &block = u.sackBlock[i];
            auto start = it;
            for (; start != flow_.end(); start++) {
                if (start->sequence >= block.leftEdge) {
                    if (start->sequence < block.rightEdge) {
                        if (start->deliveredTime.valid()) {
                            sacked.push_back(&*start);
                            bytesAcked += start->length;
                        }
                    }
                    else break;
                }
            }
            if (start == flow_.end()) {
                LOG_DEBUG << "[" << roundTripCount_ << "]"
                          << " SACK block not found in flow";
            }
        }
    }

    // not a cumulative or selective ack
    if (it == flow_.begin() && sacked.empty())
        return false;

    assert(pipeSize_ >= bytesAcked);
    pipeSize_ -= bytesAcked;

    if (updateRoundtripCount(ackUnit)) {
        // this ack is the end of current round trip.
        // units left in the pipe after this ack constitute the next round trip flights.
        // we are now setting next to current
        // and expecting a new data unit, which will become the first unit of next round trip

        assert(currRoundtrip_.started);
        if (roundTripCount_ > 0) {
            convert().onNewRoundtrip(ackUnit.u->when);
        }
        roundTripCount_++;
        currRoundtrip_.started = false;
    }

    RateSample rs;
    rs.priorDelivered = 0;
    rs.priorTime = Timestamp::invalid();
    rs.seeSmallUnit = false;

    // deal with accumulative acked P
    std::for_each(flow_.begin(), it, [&](P& p){
        updateRateSample(p, ackUnit, &rs);
    });
    flow_.erase(flow_.begin(), it);

    // deal with selective acked P
    for (auto& p: sacked) {
        updateRateSample(*p, ackUnit, &rs);
    }

    if (!rs.priorTime.valid()) {
        /* nothing delivered on this ACK */
        return false;
    }


    /* Use the longer of the send_elapsed and ack_elapsed */
    rs.interval = std::max(rs.sendElapsed, rs.ackElapsed);
    rs.delivered = delivered_ - rs.priorDelivered;

    if (rs.interval < kMinRtt) {
        LOG_WARN << srcAddress_.toIpPort() << "->"
                 << dstAddress_.toIpPort()
                 << " interval too small, drop";
    }
    else {
        rs.deliveryRate = rs.delivered / (rs.interval.microSecondsSinceEpoch() / 1000);
        convert().onRateSample(rs);
    }
    return true;
}

template <typename Analyzer>
void TcpFlow<Analyzer>::postHandleAckUnit(const AckUnit& ackUnit)
{
}


template <typename Analyzer>
bool TcpFlow<Analyzer>::updateRoundtripCount(const AckUnit& ackUnit)
{
    if (currRoundtrip_.started &&
        ackUnit.u->ackSequence > currRoundtrip_.startSequence) {

        currRoundtrip_.endSequence = nextSendSequence_;

        int32_t currFlightSize = currRoundtrip_.flightSize();
        if (!currRoundtrip_.seeSmallUnit && isSlowStart_) {
            if (currFlightSize < prevFlightSize_ * 3 / 2) {
                isSlowStart_ = false;
                convert().onQuitSlowStart(firstSentTime_);
                LOG_DEBUG << "[" << roundTripCount_ << "]"
                          << " quit slow start";
            }
        }
        prevFlightSize_ = currFlightSize;
        return true;
    }
    return false;
}

template <typename Analyzer>
void TcpFlow<Analyzer>::updateRateSample(P& p, const AckUnit& ack, RateSample* rs)
{
    if (!p.deliveredTime.valid()) {
        /* P already SACKed */
        return;
    }

    delivered_ += p.length;
    deliveredTime_ = ack.u->when;

    // update info using the newest packet
    if (p.delivered >= rs->priorDelivered) {
        rs->rtt = ack.u->when - p.sentTime;
        if (!rs->dataSentTime.valid()) {
            rs->dataSentTime = p.sentTime;
        }
        rs->ackReceivedTime = ack.u->when;
        rs->priorDelivered = p.delivered;
        rs->priorTime = p.deliveredTime;
        rs->sendElapsed = p.sentTime - p.firstSentTime;
        rs->ackElapsed = deliveredTime_ - p.deliveredTime;
        rs->isSenderLimited = p.isSenderLimited;
        rs->isReceiverLimited = p.isReceiverLimited;
        if (p.isSmallUnit)
            rs->seeSmallUnit = true;
        firstSentTime_ = p.sentTime;

        // all timestamp should be valid
        assert(rs->rtt.valid());
        assert(rs->priorTime.valid());
        assert(rs->sendElapsed.valid());
        assert(rs->ackElapsed.valid());
        assert(firstSentTime_.valid());
    }

    /* Mark the packet as delivered once it's SACKed to
     * avoid being used again when it's cumulatively acked.
     */
    p.deliveredTime = Timestamp::invalid();
}
