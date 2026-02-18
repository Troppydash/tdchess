#pragma once
#include <random>

namespace util
{
inline uint64_t ZOBRIST_50MR[150];
inline bool ZOB_INIT = false;

constexpr void init() {
    if (ZOB_INIT)
        return;

    ZOB_INIT = true;
    std::mt19937_64 gen(42);
    std::uniform_int_distribution<uint64_t> dis;

    memset(ZOBRIST_50MR, 0, sizeof(ZOBRIST_50MR));
    for (int i = 14; i <= 100; i += 8) {
        uint64_t key = dis(gen);
        for (int j = 0; j < 8; j++)
            ZOBRIST_50MR[i+j] = key;
    }
}
}