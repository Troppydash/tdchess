#pragma once
#include <random>
#include <cstring>

namespace util
{
inline uint64_t ZOBRIST_50MR[150];
inline bool ZOB_INIT = false;

inline void init() {
    if (ZOB_INIT)
        return;

    ZOB_INIT = true;
    std::mt19937_64 gen(42);
    std::uniform_int_distribution<uint64_t> dis;

    memset(ZOBRIST_50MR, 0, sizeof(ZOBRIST_50MR));
    for (int i = 14; i <= 100; i += 10) {
        uint64_t key = dis(gen);
        for (int j = 0; j < 10; j++)
            ZOBRIST_50MR[i+j] = key;
    }
}
}