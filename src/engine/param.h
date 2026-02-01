#pragma once
#include <cstdint>

namespace param
{
constexpr int32_t INF = 10000000;
constexpr int32_t CHECKMATE = 9000000;
constexpr int32_t VALUE_DRAW = 0;
constexpr int32_t NNUE_MAX = CHECKMATE - 100;
constexpr int16_t MVV_OFFSET = std::numeric_limits<int16_t>::max() - 256;

constexpr int32_t SYZYGY = 100 * 50;
constexpr int32_t SYZYGY50 = 10;

constexpr uint8_t EXACT_FLAG = 0;
constexpr uint8_t ALPHA_FLAG = 1;
constexpr uint8_t BETA_FLAG = 2;

constexpr int16_t MAX_DEPTH = 255;
constexpr int16_t TB_DEPTH = 254;
constexpr size_t QUIET_MOVES = 32;

constexpr int32_t MATE_IN(int ply)
{
    return INF - ply;
}

constexpr int32_t MATED_IN(int ply)
{
    return -INF + ply;
}
} // namespace param
