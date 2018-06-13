//
// Created by frank on 17-10-25.
//

#include <eva/Unit.h>
#include <eva/Exception.h>
#include <eva/checksum.h>
#include <eva/hash.h>
#include "Unit.h"

using namespace eva;

namespace
{

uint32_t unpackLoopback(const unsigned char* data, uint32_t len)
{
    const uint32_t ipv4Family1 = 0x02000000;
    const uint32_t ipv4Family2 = 0x00000002;
    const uint32_t offset = sizeof(uint32_t);

    if (len < offset)
        throw Exception("data truncated in loopback frame");

    // ipv4 packet only
    if (memcmp(data, &ipv4Family1, offset) == 0 ||
        memcmp(data, &ipv4Family2, offset) == 0) {
        return offset;
    }
    throw Exception("not ipv4 packet");
}

uint32_t unpackEthernet(const unsigned char* data, uint32_t len)
{
    uint32_t typeOffset = 12;
    uint32_t hdrOffset = 14;

    if (len < hdrOffset)
        return 0;

    // skip IEEE 802.1Q tag
    // see https://en.wikipedia.org/wiki/IEEE_802.1Q
    while (data[typeOffset] == 0x81 && data[typeOffset+1] == 0x00)
    {
        typeOffset += 4;
        hdrOffset += 4;
        if (len < hdrOffset)
            throw Exception("data truncated in ethernet frame");
    }

    if (data[typeOffset] == 0x08 && data[typeOffset+1] == 0x00)
        return hdrOffset;
    throw Exception("not ipv4 packet");
}

uint32_t unpackLinuxSll(const unsigned char* data, uint32_t len)
{
    const uint32_t typeOffset = 14;
    const uint32_t hdrOffset = 16;

    if (len < hdrOffset)
        throw Exception("data truncated in linux sll frame");

    // ipv4 packet only
    if (data[typeOffset] == 0x08 && data[typeOffset+1] == 0x00)
        return hdrOffset;
    throw Exception("not ipv4 packet");
}

uint32_t unpackIP(const unsigned char* data, uint32_t len,
                  uint32_t* tcpLen, Unit* unit)
{
    if (len < sizeof(struct ip))
        throw Exception("data truncated in ipv4 header");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
    auto hdr = (const struct ip*)(data);
#pragma GCC diagnostic pop

    // ipv4 only
    if (hdr->ip_v != 4)
        throw Exception("not ipv4 frame");

    const uint32_t hdrOffset = hdr->ip_hl * 4;

    // bad length
    if (len < hdrOffset || hdrOffset < 20)
        throw Exception("data truncated in ipv4 header");

    // totLength != len is a bug
    // since ethernet frame padding bytes
    uint16_t totLength = be16toh(hdr->ip_len);
    if (totLength > len || totLength < hdrOffset)
        throw Exception("data truncated in ipv4 body");

    // tcp only
    if (hdr->ip_p != IPPROTO_TCP)
        throw Exception("not tcp segment");

    // checksum
    if (!ipChecksumValid(hdr))
        throw Exception("bad ip packet checksum");

    unit->srcIP = hdr->ip_src.s_addr;
    unit->dstIP = hdr->ip_dst.s_addr;

    *tcpLen = totLength - hdrOffset;
    return hdrOffset;
}

void parseTcpOptions(const unsigned char* data, uint32_t len,
                     Unit* unit)
{
#define ensure_len(minimal) do { \
    if (len < (minimal)) \
        throw Exception("data truncated in tcp option"); \
} while(false)
#define update_len(diff) ({data += (diff); len -= (diff);})

    unit->optionLength = len;
    unit->sackCount = 0;
    unit->seeMss = false;
    unit->seeWsc = false;

    while (len > 0) {
        switch (data[0]) {
            case TCPOPT_EOL:
                return;
            case TCPOPT_NOP:
                update_len(1);
                break;
            case TCPOPT_MAXSEG:
                ensure_len(TCPOLEN_MAXSEG);
                memcpy(&unit->mss, data+2, 2);
                unit->mss = htobe16(unit->mss);
                unit->seeMss = true;
                update_len(TCPOLEN_MAXSEG);
                break;
            case TCPOPT_WINDOW:
                ensure_len(TCPOLEN_WINDOW);
                unit->wsc = data[2];
                unit->seeWsc = true;
                update_len(TCPOLEN_WINDOW);
                break;
            case TCPOPT_SACK: {
                ensure_len(2);
                uint8_t length = data[1];
                ensure_len(length);

                unit->sackCount = (length - 2u) >> 3u;
                if (unit->sackCount > Unit::kMaxSackCount) {
                    throw Exception("too many SACK block");
                }

                for (uint32_t i = 0; i < unit->sackCount; i++) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
                    uint32_t leftEdge  = *(uint32_t*)(data + 2 + 8 * i);
                    uint32_t rightEdge = *(uint32_t*)(data + 2 + 8 * i + 4);
#pragma GCC diagnostic pop
                    auto& block = unit->sackBlock[i];
                    block.leftEdge = be32toh(leftEdge);
                    block.rightEdge = be32toh(rightEdge);
                }

                update_len(length);
                break;
            }
            default: {
                ensure_len(2);
                uint8_t length = data[1];
                ensure_len(length);
                update_len(length);
                break;
            }
        }
    }
#undef ensure_len
#undef update_len
}

uint32_t unpackTCP(const unsigned char* data, uint32_t len,
                   uint32_t prevIpLen, Unit* unit)
{
    if (len < sizeof(struct tcphdr))
        return 0;

    auto any = static_cast<const void*>(data);
    auto hdr = static_cast<const struct tcphdr*>(any);

    uint32_t optOffset = 20;
    uint32_t hdrOffset = 4 * hdr->th_off;
    if (len < hdrOffset || hdrOffset < optOffset)
        throw Exception("bad header length");

    unit->srcPort = hdr->th_sport;
    unit->dstPort = hdr->th_dport;
    unit->dataSequence = be32toh(hdr->seq);
    unit->ackSequence = be32toh(hdr->ack_seq);
    unit->recvWindow = be16toh(hdr->th_win);
    unit->flag = hdr->th_flags;

    parseTcpOptions(data + optOffset, hdrOffset - optOffset, unit);

    // tcp checksum
    any = data - prevIpLen;
    auto iphdr = static_cast<const struct ip*>(any);
    if (!tcpChecksumValid(iphdr ,hdr))
        throw Exception("bad tcp segment checksum");

    return hdrOffset;
}

InetAddress createInetAddress(uint32_t ip, uint16_t port)
{
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = port;

    return InetAddress(addr);
}

void unpack(int linkType, const unsigned char* data, uint32_t len, Unit* unit)
{
    uint32_t offset = 0;

    switch (linkType) {
        case DLT_NULL:
        case DLT_LOOP:
            offset = unpackLoopback(data, len);
            break;
        case DLT_EN10MB:
        case DLT_IEEE802:
            offset = unpackEthernet(data, len);
            break;
        case DLT_LINUX_SLL:
            offset = unpackLinuxSll(data, len);
            break;
        default:
            fprintf(stderr, "Packet link type not know (%d)! "
                    "Interpret at Ethernet now - but be carefull!\n", linkType);
            offset = unpackEthernet(data, len);
            break;
    }

    data += offset;
    len -= offset;

    uint32_t tcpLen;
    offset = unpackIP(data, len, &tcpLen, unit);

    data += offset;
    len = tcpLen; // tcpLen may not equal to (len-offset) because of ethernet frame padding
    offset = unpackTCP(data, len, offset, unit);

    data += offset;
    len -= offset;
    unit->dataLength = static_cast<uint16_t>(len);
}



Unit unpackReturnUnit(struct pcap_pkthdr* pkthdr, const unsigned char* data, int linkType)
{
    Unit u;

    auto seconds = static_cast<int64_t>(pkthdr->ts.tv_sec);
    auto microSeconds = static_cast<int64_t>(pkthdr->ts.tv_usec);

    u.when = Timestamp(seconds * Timestamp::kMicroSecondsPerSecond + microSeconds);
    // pakcet header is OK!
//    if (pkthdr->caplen < pkthdr->len) {
//        throw Exception("caplen is less then len");
//    }
    unpack(linkType, data, pkthdr->len, &u);
    u.srcAddress = createInetAddress(u.srcIP, u.srcPort);
    u.dstAddress = createInetAddress(u.dstIP, u.dstPort);
    u.hashCode = generateHashCode(u.srcIP, u.dstIP, u.srcPort, u.dstPort);
    return u;
}

}


namespace eva
{

bool unpack(struct pcap_pkthdr* pkthdr,
            const unsigned char* data,
            int linkType,
            Unit* u,
            bool printfError)
{
    try {
        *u = unpackReturnUnit(pkthdr, data, linkType);
    }
    catch (Exception& e) {
        if (printfError)
            LOG_ERROR << "unpack error: " << e.what();
        return false;
    }
    return true;
}

}