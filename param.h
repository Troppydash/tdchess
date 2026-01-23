#pragma once
#include <cstdint>


namespace param
{
constexpr int32_t INF =      10000000;
constexpr int32_t CHECKMATE = 9000000;
constexpr int32_t SYZYGY = 100000;
constexpr int32_t SYZYGY50 = 10;

constexpr uint8_t EXACT_FLAG = 0;
constexpr uint8_t ALPHA_FLAG = 1;
constexpr uint8_t BETA_FLAG = 2;

constexpr int16_t MAX_DEPTH = 255;

constexpr int16_t base_score = (1 << 14);

constexpr int16_t pv_move_score = 500;
constexpr int16_t promo_move_score = 490;
constexpr int16_t capture_move_score = 485;
constexpr int16_t killer_move_score = 480;
constexpr int16_t killer_move_score2 = 470;
constexpr int16_t end_move_score = 490;
}
