#pragma once
#include <cstdint>

namespace param
{

constexpr int32_t MAX_DEPTH = 255;
constexpr int32_t TB_DEPTH = 254;
constexpr int32_t UNSEARCHED_DEPTH = -1;
constexpr int32_t UNINIT_DEPTH = -2;
constexpr int32_t QDEPTH = 0;

constexpr int16_t INF = 31000;
constexpr int16_t CHECKMATE = INF - MAX_DEPTH;
constexpr int16_t NNUE_MAX = CHECKMATE - 10;
constexpr int16_t VALUE_INF = INF + 10;
constexpr int16_t VALUE_NONE = VALUE_INF + 10;
constexpr int16_t VALUE_DRAW = 0;

constexpr int16_t VALUE_SYZYGY = NNUE_MAX - 10;

// move ordering
// PV_OFFSET
// PROMOTION_OFFSET - 200
// GOOD_CAPTURE_OFFSET - 200
// KILLER_OFFSET - 100
// BAD_CAPTURE_OFFSET - 200
// HISTORY - rest

constexpr int16_t PV_OFFSET = std::numeric_limits<int16_t>::max() - 10;

constexpr int16_t PROMOTION_OFFSET = PV_OFFSET - 50;
constexpr std::array<int16_t, 7> PROMOTION_SCORES = {0, 10, 20, 30, 40, 0, 0};

// allow 200 for captures
constexpr int16_t GOOD_CAPTURE_OFFSET = PROMOTION_OFFSET - 200;

// allow 100 for killer
constexpr int16_t KILLER_OFFSET = GOOD_CAPTURE_OFFSET - 100;
constexpr size_t NUMBER_KILLERS = 2;
constexpr std::array<int16_t, NUMBER_KILLERS> KILLER_SCORE = {20, 10};
constexpr int16_t MATE_KILLER_BONUS = 30;

// allow 200 for bad captures
constexpr int16_t BAD_CAPTURE_OFFSET = KILLER_OFFSET - 200;

// history
constexpr int16_t MAX_HISTORY = BAD_CAPTURE_OFFSET - 10;

constexpr int16_t COUNTER_BONUS = 5;

constexpr uint8_t EXACT_FLAG = 3;
constexpr uint8_t BETA_FLAG = 2;
constexpr uint8_t ALPHA_FLAG = 1;
constexpr uint8_t NO_FLAG = 0;

constexpr int32_t QUIET_MOVES = 64;

constexpr bool IS_VALID(int16_t value)
{
    return value != VALUE_NONE;
}

constexpr int16_t MATED_IN(int16_t ply)
{
    return -INF + ply;
}

constexpr int16_t MATE_IN(int16_t ply)
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
