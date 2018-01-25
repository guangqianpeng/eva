//
// Created by frank on 17-10-24.
//

#ifndef EVA_PACKET_H
#define EVA_PACKET_H

#include <pcap.h>
#include <netinet/tcp.h>

#include <eva/util.h>

namespace eva
{



struct Unit
{
    Timestamp   when;
    InetAddress srcAddress;
    InetAddress dstAddress;
    uint32_t    srcIP, dstIP;
    uint16_t    srcPort, dstPort;
    Sequence    dataSequence;
    Sequence    ackSequence;
    uint32_t    recvWindow;
    uint32_t    dataLength;
    uint32_t    optionLength;
    uint8_t     flag;
    bool        seeMss;
    uint32_t    mss;
    bool        seeWsc;
    uint32_t    wsc;
    size_t      hashCode;


    static const uint32_t kMaxSackCount = 4;
    uint32_t sackCount;
    struct {
        Sequence leftEdge;
        Sequence rightEdge;
    } sackBlock[kMaxSackCount];

    bool isSACK() const { return sackCount > 0; }
    bool isFIN()  const { return flag & TH_FIN; }
    bool isSYN()  const { return flag & TH_SYN; }
    bool isRST()  const { return flag & TH_RST; }
    bool isPSH()  const { return flag & TH_PUSH; }
    bool isACK()  const { return flag & TH_ACK; }
    bool isURG()  const { return flag & TH_URG; }
};

struct DataUnit
{
    explicit DataUnit(Unit* u_): u(u_) {}
    Unit* u;
};

struct AckUnit
{
    explicit AckUnit(Unit* u_): u(u_) {}
    Unit* u;
};

inline bool operator==(const Unit& lhs, const Unit& rhs)
{
    return (lhs.srcIP == rhs.srcIP &&
            lhs.srcPort == rhs.srcPort &&
            lhs.dstIP == rhs.dstIP &&
            lhs.dstPort == rhs.dstPort) ||

            (lhs.srcIP == rhs.dstIP &&
             lhs.srcPort == rhs.dstPort &&
             lhs.dstIP == rhs.srcIP &&
             lhs.dstPort == rhs.srcPort);
}

bool unpack(struct pcap_pkthdr* pkthdr,
            const unsigned char* data,
            int linkType,
            Unit* u,
            bool printfError = false);

}

namespace std
{

// custom specialization of std::hash can be injected in namespace std
template<> struct hash<eva::Unit>
{
    size_t operator()(const eva::Unit& unit) const
    {
        return unit.hashCode;
    }
};

}

#endif //EVA_PACKET_H
