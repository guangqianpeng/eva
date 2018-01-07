//
// Created by frank on 18-1-2.
//

#ifndef EVA_HASH_H
#define EVA_HASH_H

#include <cstddef>
#include <cstdint>

namespace eva
{

size_t generateHashCode(uint32_t srcIP, uint32_t dstIP,
                        uint16_t srcPort, uint16_t dstPort);

}

#endif //EVA_HASH_H
