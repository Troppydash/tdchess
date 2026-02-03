#pragma once

#include "param.h"

#include <algorithm>
#include <cstdint>
#include <limits>

struct search_param
{
    int32_t wtime{};
    int32_t btime{};
    int32_t winc{};
    int32_t binc{};
    int32_t depth{};
    int32_t movetime{};
    int32_t move_overhead{};
    double original_time_adjust{};

    struct result
    {
        int32_t depth;
        int32_t time;
    };

    explicit search_param()
    {
        reset();
    }

    static search_param from_game_state(int32_t wtime, int32_t btime, int32_t winc, int32_t binc)
    {
        search_param param{};
        param.wtime = wtime;
        param.btime = btime;
        param.winc = winc;
        param.binc = binc;
        return param;
    }

    void clear_some()
    {
        wtime = std::numeric_limits<int32_t>::max();
        btime = std::numeric_limits<int32_t>::max();
        winc = std::numeric_limits<int32_t>::max();
        binc = std::numeric_limits<int32_t>::max();
        depth = param::MAX_DEPTH;
        movetime = std::numeric_limits<int32_t>::max();
        move_overhead = 0;
    }

    void reset()
    {
        clear_some();
        original_time_adjust = -1;
    }

    [[nodiscard]] result time_control(int moves, chess::Color side2move)
    {
        int inc, time;
        if (side2move == chess::Color::WHITE)
        {
            inc = winc;
            time = wtime;
        }
        else
        {
            inc = binc;
            time = btime;
        }

        // sf style, movestogo = 0
        int ply = (moves - 1) * 2;
        int scaled_time = time;
        int cent_mtg = 5051;
        if (scaled_time < 1000)
            cent_mtg = static_cast<int>(scaled_time * 5.051);

        int time_left =
            std::max(1, time + (inc * (cent_mtg - 100) - move_overhead * (200 + cent_mtg)) / 100);

        if (original_time_adjust < 0)
            original_time_adjust = 0.3128 * std::log10(time_left) - 0.4354;

        double logtime_in_sec = std::log10(scaled_time / 1000.0);
        double opt_constant = std::min(0.0032116 + 0.000321123 * logtime_in_sec, 0.00508017);
        double opt_scale = std::min(0.0121431 + std::pow(ply + 2.94693, 0.461073) * opt_constant,
                                    0.213035 * (double)time / (double)time_left) *
                           original_time_adjust;

        int32_t optimum_time = std::max(100, static_cast<int32_t>(opt_scale * time_left));
        int32_t true_time = std::min(std::min(optimum_time, time - move_overhead), movetime);
        return {depth, true_time};
    }
};