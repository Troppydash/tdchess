#pragma once
#include <cstdint>

namespace param
{
constexpr int32_t INF = 10000000;
constexpr int32_t CHECKMATE = 9000000;

// TODO: use dtz to get a true mate in x
constexpr int32_t SYZYGY = 100 * 50;
constexpr int32_t SYZYGY50 = 10;

constexpr uint8_t EXACT_FLAG = 0;
constexpr uint8_t ALPHA_FLAG = 1;
constexpr uint8_t BETA_FLAG = 2;

constexpr int16_t MAX_DEPTH = 255;
constexpr int16_t TB_DEPTH = 254;
} // namespace param
