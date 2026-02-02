#pragma once
#include <cstdint>

namespace param
{
constexpr int32_t INF = 10000000;
constexpr int32_t CHECKMATE = 9000000;
constexpr int32_t NNUE_MAX = CHECKMATE - 100;
constexpr int32_t VALUE_INF = INF+10;

// TODO: use dtz to get a true mate in x
constexpr int32_t SYZYGY = 100 * 50;
constexpr int32_t SYZYGY50 = 10;

constexpr uint8_t EXACT_FLAG = 3;
constexpr uint8_t ALPHA_FLAG = 1;
constexpr uint8_t BETA_FLAG = 2;

constexpr int32_t MAX_DEPTH = 255;
constexpr int32_t TB_DEPTH = 254;
constexpr int32_t QUIET_MOVES = 64;

constexpr int32_t MATED_IN(int32_t ply)
{
    return -INF + ply;
}

constexpr int32_t MATE_IN(int32_t ply)
{
    return INF - ply;
}

} // namespace param
