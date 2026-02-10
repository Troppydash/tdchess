#pragma once
#include <cstdint>

namespace param
{

constexpr int32_t MAX_DEPTH = 255;
constexpr int32_t TB_DEPTH = 254;
constexpr int32_t UNSEARCHED_DEPTH = -1;
constexpr int32_t UNINIT_DEPTH = -2;
constexpr int32_t QDEPTH = 0;

constexpr int16_t INF = 30000;
constexpr int16_t CHECKMATE = INF - MAX_DEPTH;
constexpr int16_t NNUE_MAX = CHECKMATE - 10;
constexpr int16_t VALUE_INF = INF + 10;
constexpr int16_t VALUE_NONE = VALUE_INF + 10;
constexpr int16_t VALUE_DRAW = 0;

constexpr int16_t VALUE_SYZYGY = NNUE_MAX - 10;

constexpr size_t NUMBER_KILLERS = 2;

constexpr int16_t COUNTER_BONUS = 5;

constexpr uint8_t EXACT_FLAG = 3;
constexpr uint8_t BETA_FLAG = 2;
constexpr uint8_t ALPHA_FLAG = 1;
constexpr uint8_t NO_FLAG = 0;

constexpr int32_t QUIET_MOVES = 32;

constexpr bool IS_VALID(int16_t value)
{
    return value != VALUE_NONE;
}

constexpr int16_t MATED_IN(int32_t ply)
{
    return -INF + ply;
}

constexpr int16_t MATE_IN(int32_t ply)
{
    return INF - ply;
}

constexpr bool IS_WIN(int16_t value)
{
    return value > CHECKMATE;
}

constexpr bool IS_LOSS(int16_t value)
{
    return value < -CHECKMATE;
}

constexpr bool IS_DECISIVE(int16_t value)
{
    return IS_WIN(value) || IS_LOSS(value);
}

constexpr int64_t TIME_MAX = 1'000'000'000;

} // namespace param
