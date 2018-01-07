//
// Created by frank on 18-1-2.
//

#include <iostream>
#include <numeric>

#include <eva/Analyzer.h>

using namespace eva;

namespace
{

const Timestamp kRtpropExpration(10 * Timestamp::kMicroSecondsPerSecond);

}

void Analyzer::onRateSample(const RateSample& rs)
{
    if (!rtprop_.valid() ||
        rtprop_ > rs.rtt ||
        rs.ackReceivedTime - rtpropTimestamp_ >= kRtpropExpration)
    {
        rtprop_ = rs.rtt;
        rtpropTimestamp_ = rs.ackReceivedTime;
        LOG_DEBUG << "[" << roundtripCount() << "]"
                  << " [update delay] "
                  << rtprop_.microSecondsSinceEpoch() << "us";
    }

    if (!firstAckTime_.valid()) {
        firstAckTime_ = rs.ackReceivedTime;
    }

    auto btlbw = bandwidthFilter_.GetBest();


    if (rs.deliveryRate >= btlbw ||
        ( !rs.isSenderLimited &&
          !rs.isReceiverLimited)) {
        bandwidthFilter_.Update(rs.deliveryRate,
                                roundtripCount());
    }

    if (rs.seeSmallUnit) {
        seeSmallUnit_ = true;
    }
    if (seeSmallUnit_ && rs.isSenderLimited) {
        LOG_DEBUG << "[" << roundtripCount() << "]"
                  << " [sender limited]"
                  <<" pipe size = "
                  << pipeSize()
                  << " bdp = "
                  << bdp();
        votes_[SENDER_LIMITED]++;
    }
    else if (isSlowStart_ || slowStartQuitTime >= rs.dataSentTime) {
        LOG_DEBUG << "[" << roundtripCount() << "]"
                  << " [slow start]";
        votes_[SLOW_STAR_LIMITED]++;
    }
    else if (rs.isReceiverLimited) {
        LOG_DEBUG << "[" << roundtripCount() << "]"
                  << " [receiver limited] pipe size = "
                  << pipeSize()
                  << " receiver window = "
                  << recvWindow();
        votes_[RECEIVER_LIMITED]++;
    }
    else if (rs.isSenderLimited) {
        LOG_DEBUG << "[" << roundtripCount() << "]"
                  << " [sender limited]"
                  <<" pipe size = "
                  << pipeSize()
                  << " bdp = "
                  << bdp();
        votes_[SENDER_LIMITED]++;
    }
    else if (rs.deliveryRate >= btlbw * 4 / 5) {
        LOG_DEBUG << "[" << roundtripCount() << "]"
                  << " [bandwidth limited] BtlBw = "
                  << btlbw
                  << " delivery rate = "
                  << rs.deliveryRate;
        votes_[BANDWIDTH_LIMITED]++;
    }
    else if (rs.rtt.microSecondsSinceEpoch() >
             rtprop_.microSecondsSinceEpoch() * 6 / 5) {

        LOG_DEBUG << "[" << roundtripCount() << "]"
                  << " [congestion limited] delay = "
                  << rtprop_.microSecondsSinceEpoch()
                  << " rtt = "
                  << rs.rtt.microSecondsSinceEpoch();
        votes_[CONGESTION_LIMITED]++;
    }
    else {
        LOG_DEBUG << "[" << roundtripCount() << "]"
                  << " [unknown limited]";
        votes_[UNKNOWN_LIMITED]++;
    }

    LOG_DEBUG << "[" << roundtripCount() << "]"
              << "delivery rate: " << rs.deliveryRate
              << " rtt: " << rs.rtt.microSecondsSinceEpoch();
}

void Analyzer::onNewRoundtrip(Timestamp when)
{
    LOG_DEBUG << "[" << roundtripCount() << "]"
              << "\n\tslow start: " << votes_[SLOW_STAR_LIMITED]
              << "\n\tbandwidth limited: " << votes_[BANDWIDTH_LIMITED]
              << "\n\tsender limited: " << votes_[SENDER_LIMITED]
              << "\n\treceiver limited: " << votes_[RECEIVER_LIMITED]
              << "\n\tcongestion limited: " << votes_[CONGESTION_LIMITED]
              << "\n\tunknown limited: " << votes_[UNKNOWN_LIMITED];

    assert(firstAckTime_.valid());

    std::cout << "[" << roundtripCount() << "]"
              << " " << bandwidthFilter_.GetBest() << "kB/s"
              << " " << rtprop_.microSecondsSinceEpoch() << "us "
              << extractHours(firstAckTime_) << " -> "
              << extractHours(when)
              << " ";

    int total = std::accumulate(votes_.begin(), votes_.end(), 0);
    Result ret = countVotes();
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
        case SENDER_LIMITED:
            if (seeSmallUnit_) {
                std::cout << "[application limited]"
                          << " (" << votes_[ret] << "/" << total << ")"
                          << "\n";
            }
            else {
                std::cout << "[kernel limited]"
                          << " (" << votes_[ret] << "/" << total << ")"
                          << "\n";
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
                    << " (" << votes_[ret] << "/" << total << "/" << ")"
                    << "\n";
            break;
    }
    std::fill(votes_.begin(), votes_.end(), 0);
    seeSmallUnit_ = false;
    firstAckTime_ = Timestamp::invalid();
}

void Analyzer::onTimeoutRxmit(Timestamp first, Timestamp rexmit)
{
    std::cout << "[" << roundtripCount() << "]"
              << " " << bandwidthFilter_.GetBest() << "kB/s"
              << " " << rtprop_.microSecondsSinceEpoch() << "us "
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
              << " " << rtprop_.microSecondsSinceEpoch() << "us "
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

    return votes_[ret] == 0 ?
           UNKNOWN_LIMITED :
           static_cast<Result>(ret);
}

uint32_t Analyzer::bdp() const
{
    auto milliseconds = rtprop_.microSecondsSinceEpoch() / 1000;
    auto btlbw = bandwidthFilter_.GetBest();
    return static_cast<uint32_t>(milliseconds * btlbw);
}