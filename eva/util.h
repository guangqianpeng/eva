//
// Created by frank on 18-1-1.
//

#ifndef EVA_UTIL_H
#define EVA_UTIL_H

#include <string>

#include <muduo/net/InetAddress.h>
#include <muduo/base/Timestamp.h>
#include <muduo/base/Logging.h>
#include <muduo/base/LogStream.h>
#include <muduo/base/LogFile.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/EventLoopThread.h>
#include <muduo/net/TcpServer.h>

namespace eva
{

using muduo::net::EventLoop;
using muduo::net::EventLoopThread;
using muduo::net::InetAddress;
using muduo::net::TcpServer;
using muduo::net::TcpConnection;
using muduo::net::TcpConnectionPtr;
using muduo::net::Buffer;
using muduo::Logger;
using muduo::LogFile;
using muduo::LogStream;
using muduo::Timestamp;
using muduo::noncopyable;

inline bool operator==(const InetAddress& lhs, const InetAddress& rhs)
{
    return lhs.ipNetEndian() == rhs.ipNetEndian() &&
           lhs.portNetEndian() == rhs.portNetEndian();
}

struct Sequence
{
    // not explicit
    Sequence(uint32_t seq_ = 0):
            seq(seq_)
    {}

    uint32_t seq;
};

inline bool operator<(Sequence lhs, Sequence rhs)
{
    return static_cast<int32_t>(lhs.seq - rhs.seq) < 0;
}

inline bool operator==(Sequence lhs, Sequence rhs)
{
    return lhs.seq == rhs.seq;
}

inline bool operator!=(Sequence lhs, Sequence rhs)
{
    return lhs.seq != rhs.seq;
}

inline bool operator>(Sequence lhs, Sequence rhs)
{
    return static_cast<int32_t>(lhs.seq - rhs.seq) > 0;
}

inline bool operator<=(Sequence lhs, Sequence rhs)
{
    return static_cast<int32_t>(lhs.seq - rhs.seq) <= 0;
}

inline bool operator>=(Sequence lhs, Sequence rhs)
{
    return static_cast<int32_t>(lhs.seq - rhs.seq) >= 0;
}

inline int32_t operator-(Sequence lhs, Sequence rhs)
{
    return static_cast<int32_t>(lhs.seq - rhs.seq);
}

inline Sequence operator+(Sequence lhs, uint32_t offset)
{
    return Sequence(lhs.seq + offset);
}

inline int64_t operator-(Timestamp lhs, Timestamp rhs)
{
    return lhs.microSecondsSinceEpoch() -
           rhs.microSecondsSinceEpoch();
}

inline muduo::string extractHours(Timestamp ts)
{
    auto str = ts.toFormattedString();
    return str.substr(9);
}

}

#endif //EVA_UTIL_H
