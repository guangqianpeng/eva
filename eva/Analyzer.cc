//
// Created by frank on 18-1-2.
//

#include <iostream>
#include <numeric>

#include <eva/Analyzer.h>

using namespace eva;

namespace
{

const int64_t kRtpropExpration = 30 * Timestamp::kMicroSecondsPerSecond;

}

void Analyzer::onRateSample(const RateSample& rs, const AckUnit& ackUnit)

{
    assert(rs.ackReceivedTime.valid());
    assert(rs.dataSentTime.valid());

    bool rttIsValid = (!rs.seeRexmit && !ackUnit.u->isSACK()) ||
                       rs.rtt > rtprop_;

    if (rttIsValid)
        ackCount_++;

    if (rtprop_ < 0 ||
        (rttIsValid && rtprop_ > rs.rtt) ||
        rs.ackReceivedTime - rtpropTimestamp_ >= kRtpropExpration)
    {
        rtprop_ = rs.rtt;
        rtpropTimestamp_ = rs.ackReceivedTime;
        LOG_DEBUG << "[" << roundtripCount() << "]"
                  << " [update delay] "
                  << rtprop_ << "us";
    }


    if (!firstAckTime_.valid()) {
        firstAckTime_ = rs.ackReceivedTime;
    }

    auto btlbw = bandwidthFilter_.GetBest();

    if (rs.seeSmallUnit)
        smallUnitCount_++;

    if (maxDeliveryRate_ < rs.deliveryRate)
        maxDeliveryRate_ = rs.deliveryRate;

    if (rs.seeRexmit || ackUnit.u->isSACK())
        seeRexmit_ = true;

    bool rttTooLong = (rttIsValid && rs.rtt > rtprop_ * 7 / 5);
    if (rttTooLong)
        rttTooLongCount_++;

    if (rttIsValid && rs.rtt > rtprop_ * 5 / 2)
        rttHugeCount_++;

    if (rs.deliveryRate >= btlbw ||
        rttTooLong ||
        (!rs.isSenderLimited &&
         !rs.isReceiverLimited)) {
        bandwidthFilter_.Update(rs.deliveryRate,
                                roundtripCount());
    }

    if (rs.isReceiverLimited) {
        votes_[RECEIVER_LIMITED]++;
    }
    else if (rs.isSenderLimited) {
        votes_[SENDER_LIMITED]++;
    }
    else if (isSlowStart_ || slowStartQuitTime >= rs.dataSentTime) {
        votes_[SLOW_STAR_LIMITED]++;
    }
    else if (rs.deliveryRate >= btlbw * 4 / 5) {
        votes_[BANDWIDTH_LIMITED]++;
    }
    else if (rttTooLong) {
        votes_[CONGESTION_LIMITED]++;
    }
    else {
        votes_[UNKNOWN_LIMITED]++;
    }

    LOG_DEBUG << "[" << roundtripCount() << "]"
              << "delivery rate: " << rs.deliveryRate
              << " rtt: " << rs.rtt;
}

void Analyzer::onNewRoundtrip(Timestamp now,
                              Timestamp lastAckTime,
                              int64_t bytesAcked,
                              int64_t totalAckInterval,
                              int64_t totalAckCount,
                              int32_t currFlightSize)
{
    auto port = dstAddress().toPort();

    LOG_DEBUG << "[" << roundtripCount() << "]"
              << "\n\tslow start: " << votes_[SLOW_STAR_LIMITED]
              << "\n\tbandwidth limited: " << votes_[BANDWIDTH_LIMITED]
              << "\n\tsender limited: " << votes_[SENDER_LIMITED]
              << "\n\treceiver limited: " << votes_[RECEIVER_LIMITED]
              << "\n\tcongestion limited: " << votes_[CONGESTION_LIMITED]
              << "\n\tunknown limited: " << votes_[UNKNOWN_LIMITED];

    if(!firstAckTime_.valid()) {
        return;
    }

    std::cout << "[" << roundtripCount() << "]" << " ["<< port <<"]"
              << " " << bandwidthFilter_.GetBest() << "kB/s"
              << " " << rtprop_ << "us "
              << extractHours(firstAckTime_) << " -> "
              << extractHours(now)
              << " ";

    if (rttHugeCount_ == ackCount_) {
        std::cout << "(buffer bloat) ";
    }

    int total = std::accumulate(votes_.begin(), votes_.end(), 0);
    Result ret = countVotes();


    // change the results
    if (votes_[RECEIVER_LIMITED] > 0) {
        ret = RECEIVER_LIMITED;
    }
    else if (ret == BANDWIDTH_LIMITED ||
             ret == UNKNOWN_LIMITED) {
        // fixme: don't trust small unit!
        // fixme: use flight acks spacing to figure out BANDWIDTH_LIMITED
        auto spacing = (now - lastAckTime) / ((bytesAcked + mss()) / mss());
        if (spacing * 20 > rtprop_) {
            if ((rttTooLongCount_ > 0 && seeRexmit_) ||
                 rttTooLongCount_ == ackCount_)
            {
                ret = CONGESTION_LIMITED;
            }
            else {
                ret = SENDER_LIMITED;
            }
        }
    }
    else if (ret == SENDER_LIMITED) {
        if ((rttTooLongCount_ > 0 && seeRexmit_) ||
             rttTooLongCount_ == ackCount_)
        {
            ret = CONGESTION_LIMITED;
        }
    }
    else if (smallUnitCount_) {
        if (ret == SLOW_STAR_LIMITED)
            ret = SENDER_LIMITED;
    }

    switch (ret)
    {
        case SLOW_STAR_LIMITED:
            std::cout << "[slow start]"
                      << " (" << votes_[ret] << "/" << total << ")"
                      << "\n";
            break;
        case BANDWIDTH_LIMITED:
            std::cout << "[bandwidth limited]"
                          << " (" << votes_[ret] << "/" << total << ")"
                          << "\n";
            break;
        case SENDER_LIMITED: {

            auto diff1 = static_cast<uint32_t>(std::abs(currFlightSize - prevFlightSize1_));
            auto diff2 = static_cast<uint32_t>(std::abs(currFlightSize - prevFlightSize2_));
            auto diff3 = static_cast<uint32_t>(std::abs(currFlightSize - prevFlightSize3_));
            bool allZero = (diff1 == 0 && diff2 == 0 && diff3 == 0);



            if (currFlightSize > static_cast<int32_t>(mss()) &&
                (prevSmallUnitCount_ == 0 ||
                 smallUnitCount_ == 0 ||
                 allZero))
            {
                if (allZero) {
                    std::cout << "(buffer)";
                }
                else {
                    std::cout << "(cc)";
                }
                std::cout << "[kernel limited]"
                          << " (" << votes_[ret] << "/" << total << ")"
                          << "\n";
            }
            else {
                std::cout << "[application limited]"
                          << " (" << votes_[ret] << "/" << total << ")"
                          << "\n";
            }
        }
            break;
        case RECEIVER_LIMITED:
            std::cout
                    << "[receiver limited]"
                    << " (" << votes_[ret] << "/" << total << ")"
                    << "\n";

            break;
        case CONGESTION_LIMITED:
            std::cout
                    << "[congestion limited]"
                    << " (" << votes_[ret] << "/" << total << ")"
                    << "\n";

            break;
        default:
            std::cout
                    << "[unknown limited]"
                    << " (" << votes_[ret] << "/" << total << ")"
                    << "\n";
            break;
    }
    std::fill(votes_.begin(), votes_.end(), 0);
    prevSmallUnitCount_ = smallUnitCount_;
    smallUnitCount_ = 0;
    maxDeliveryRate_ = 0;
    prevFlightSize1_ = prevFlightSize2_;
    prevFlightSize2_ = prevFlightSize3_;
    prevFlightSize3_ = currFlightSize;
    rttTooLongCount_ = 0;
    rttHugeCount_ = 0;
    ackCount_ = 0;
    seeRexmit_ = false;
    firstAckTime_ = Timestamp::invalid();
}

void Analyzer::onTimeoutRxmit(Timestamp first, Timestamp rexmit)
{
    std::cout << "[" << roundtripCount() << "]"
              << " " << bandwidthFilter_.GetBest() << "kB/s"
              << " " << rtprop_ << "us "
              << extractHours(first) << " -> "
              << extractHours(rexmit)
              << " [timeout rexmit]" << "\n";
}

void Analyzer::onQuitSlowStart(Timestamp when)
{
    isSlowStart_ = false;
    slowStartQuitTime = when;
    std::cout << "[" << roundtripCount() << "]"
              << " " << bandwidthFilter_.GetBest() << "kB/s"
              << " " << rtprop_ << "us "
              << extractHours(when)
              << " [quit slow start]" << "\n";
}

Result Analyzer::countVotes()
{
    int ret = SLOW_STAR_LIMITED;
    for (int i = 1; i < UNKNOWN_LIMITED; i++) {
        if (votes_[i] > votes_[ret])
            ret = i;
    }

    if (isSlowStart_)
        return SLOW_STAR_LIMITED;

    return votes_[ret] == 0 ?
           UNKNOWN_LIMITED :
           static_cast<Result>(ret);
}

int64_t Analyzer::bdp() const
{
    auto milliseconds = rtprop_ / 1000;
    auto btlBw = bandwidthFilter_.GetBest();
    return (milliseconds * btlBw);
}