#pragma once

#include "param.h"

#include <algorithm>
#include <cstdint>
#include <limits>

struct search_param
{
    int64_t wtime{};
    int64_t btime{};
    int64_t winc{};
    int64_t binc{};
    int32_t depth{};
    int64_t movetime{};
    int64_t move_overhead{};
    double original_time_adjust{};

    struct result
    {
        int32_t depth;
        int64_t time;
        int64_t opt_time;
    };

    explicit search_param()
    {
        reset();
    }

    static search_param from_game_state(int64_t wtime, int64_t btime, int64_t winc, int64_t binc)
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
        wtime = param::TIME_MAX;
        btime = param::TIME_MAX;
        winc = param::TIME_MAX;
        binc = param::TIME_MAX;
        depth = param::MAX_DEPTH;
        movetime = param::TIME_MAX;
        move_overhead = 0;
    }

    void reset()
    {
        clear_some();
        original_time_adjust = -1;
    }

    [[nodiscard]] result time_control(int moves, chess::Color side2move)
    {
        int64_t inc, time;
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

        // ignore if time and inc are invalid
        if (time == param::TIME_MAX || inc == param::TIME_MAX)
            return {depth, movetime, movetime};

        if (movetime != param::TIME_MAX)
            return {depth, movetime, movetime};

        // https://github.com/gab8192/Obsidian/blob/main/src/timeman.cpp
        int mtg = 50;
        int64_t time_left = std::max(1LL, time + inc * (mtg - 1) - move_overhead * (2 + mtg));

        double opt_scale = std::min(0.025, 0.214 * time / double(time_left));
        int64_t max_time = time * 0.8 - move_overhead;
        int64_t optimum_time = opt_scale * time_left;

        return {depth, max_time, optimum_time};
    }
};