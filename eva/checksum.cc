//
// Created by frank on 17-10-30.
//

#include <eva/checksum.h>

namespace
{

// cksum - Return 16-bit ones complement of 16-bit ones complement sum
uint16_t checksum(const void* data, int nbytes)
{
    auto p = static_cast<const uint8_t*>(data);
    uint64_t sum = 0;

    while (nbytes >= 2)
    {
        /* can't assume pointer alignment :-( */
        sum += (p[0] << 8);
        sum += p[1];

        p += 2;
        nbytes -= 2;
    }

    /* special check for odd length */
    if (nbytes == 1)
    {
        sum += (p[0] << 8);
        /* lower byte is assumed to be 0 */
    }

    sum = (sum >> 16) + (sum & 0xffff);	/* add in carry   */
    sum += (sum >> 16);		/* maybe one more */

    return static_cast<uint16_t>(sum);
}

/* compute IP checksum */
uint16_t ipChecksum(const struct ip* pip)
{
    return checksum(pip, pip->ip_hl * 4);
}


/* compute the TCP checksum */
uint16_t tcpChecksum(const struct ip* pip, const struct tcphdr* ptcp) {

    uint64_t sum = 0;

    /* TCP checksum includes: */
    /* - IP source */
    /* - IP dest */
    /* - IP type */
    /* - TCP header length + TCP data length */
    /* - TCP header and data */

    /* quick sanity check, if the packet is fragmented,
       pretend it's valid */
    if ((ntohs(pip->ip_off) << 2) != 0) {
        /* both the offset AND the MF bit must be 0 */
        /* (but we shifted off the DF bit */
        return 0;
    }

    /* 2 4-byte numbers, next to each other */
    sum += checksum(&pip->ip_src, 4 * 2);

    /* type */
    sum += pip->ip_p;

    /* length (TCP header length + TCP data length) */
    uint32_t tcpLength = ntohs(pip->ip_len) - (4 * pip->ip_hl);
    sum += htons(static_cast<uint16_t>(tcpLength));

    /* checksum the TCP header and data */
    sum += checksum(ptcp, tcpLength);

    /* roll down into a 16-bit number */
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);

    return static_cast<uint16_t>(~sum & 0xffff);
}

}


namespace eva
{

int ipChecksumValid (const struct ip* pip)
{
    uint16_t sum = ipChecksum(pip);
    return sum == 0 || sum == 0xffff;
}

int tcpChecksumValid (const struct ip* pip, const struct tcphdr* ptcp)
{
    uint16_t sum = tcpChecksum(pip, ptcp);
    return sum == 0;
}

}