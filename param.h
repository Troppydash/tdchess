#pragma once
#include <cstdint>


namespace param
{
constexpr int32_t CHECKMATE = 9000000;
constexpr int32_t INF = 10000000;

// int exact_flag = 0;
// int alpha_flag = 1;
// int beta_flag = 2;

constexpr uint8_t MAX_DEPTH = 255;

constexpr int32_t BASE_SCORE = (1 << 30);

constexpr int32_t pv_move_score = 500;
constexpr int32_t killer_move_score = 480;
constexpr int32_t killer_move_score2 = 470;
constexpr int32_t end_move_score = 490;
}
