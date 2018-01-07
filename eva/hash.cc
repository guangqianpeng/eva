//
// Created by frank on 18-1-2.
//

#include <random>

#include <eva/util.h>
#include <eva/hash.h>

namespace
{

template <int N>
struct RandomHash
{
    RandomHash()
    {
        auto seed = static_cast<unsigned long>(time(nullptr));
        std::default_random_engine generator(seed);
        std::uniform_int_distribution<uint8_t> distribution;
        auto dice = std::bind(distribution, generator);

        std::generate(xor_.begin(), xor_.end(), dice);
        std::generate(perm.begin(), perm.end(), dice);

        int p[N];
        for (int i = 0; i < N; i++)
            p[i] = i;
        for (int i = 0; i < N; i++)
        {
            int n = perm[i] % (N - i);
            perm[i] = static_cast<uint8_t>(p[n]);

            for (int j = 0; j < N - 1 - n; j++)
                p[n + j] = p[n + j + 1];
        }
    }

    std::array<uint8_t, N> xor_;
    std::array<uint8_t, N> perm;
};

RandomHash<6> g_RandomHash;

}

namespace eva
{

size_t generateHashCode(uint32_t srcIP, uint32_t dstIP,
                        uint16_t srcPort, uint16_t dstPort)
{
    uint32_t flag1 = srcIP ^ dstIP;
    uint16_t flag2 = srcPort ^ dstPort;

    uint8_t data[sizeof(flag1) + sizeof(flag2)];

    memmove(data, &flag1, sizeof(flag1));
    memmove(data + sizeof(flag1), &flag2, sizeof(flag2));

    auto& perm = g_RandomHash.perm;
    auto& xor_ = g_RandomHash.xor_;

    size_t ret = 0;
    for (size_t i = 0; i < sizeof(data); i++)
        ret = ((ret << 8) + (data[perm[i]] ^ xor_[i])) % 0xff100f;

    return ret;
}

}