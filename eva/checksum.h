//
// Created by frank on 17-10-30.
//

#ifndef EVA_CHECKSUM_H
#define EVA_CHECKSUM_H

#include <netinet/ip.h>
#include <netinet/tcp.h>

namespace eva
{

int ipChecksumValid (const struct ip* pip);
int tcpChecksumValid (const struct ip* pip, const struct tcphdr* ptcp);

}

#endif //EVA_CHECKSUM_H
