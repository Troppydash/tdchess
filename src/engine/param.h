#pragma once
#include <cstdint>

namespace param
{
constexpr int32_t INF = 60000;
constexpr int32_t CHECKMATE = 50000;
constexpr int32_t NNUE_MAX = CHECKMATE - 100;
constexpr int32_t VALUE_INF = INF + 10;
constexpr int32_t VALUE_NONE = VALUE_INF + 10;
constexpr int32_t VALUE_DRAW = 0;

// TODO: use dtz to get a true mate in x
constexpr int32_t VALUE_SYZYGY = CHECKMATE - 50;
// constexpr int32_t SYZYGY50 = 10;

constexpr uint8_t EXACT_FLAG = 3;
constexpr uint8_t BETA_FLAG = 2;
constexpr uint8_t ALPHA_FLAG = 1;
constexpr uint8_t NO_FLAG = 0;

constexpr int32_t MAX_DEPTH = 255;
constexpr int32_t TB_DEPTH = 254;
constexpr int32_t UNSEARCHED_DEPTH = -1;
constexpr int32_t QDEPTH = 0;
constexpr int32_t QUIET_MOVES = 64;

constexpr bool IS_VALID(int32_t value)
{
    return value != VALUE_NONE;
}

constexpr int32_t MATED_IN(int32_t ply)
{
    return -INF + ply;
}

constexpr int32_t MATE_IN(int32_t ply)
{
    return INF - ply;
}

constexpr bool IS_WIN(int32_t value)
{
    return value > CHECKMATE;
}

constexpr bool IS_LOSS(int32_t value)
{
    return value < -CHECKMATE;
}

constexpr bool IS_DECISIVE(int32_t value)
{
    return IS_WIN(value) || IS_LOSS(value);
}

constexpr int64_t TIME_MAX = 1'000'000'000;

} // namespace param
