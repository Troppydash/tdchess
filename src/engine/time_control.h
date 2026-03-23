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
    bool ponder = false;
    bool is_main_thread = true;

    struct result
    {
        int32_t depth;
        int64_t time;
        int64_t opt_time;
        bool comp;
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
        winc = 0;
        binc = 0;
        depth = param::MAX_DEPTH;
        movetime = param::TIME_MAX;
        move_overhead = 0;
        ponder = false;
        is_main_thread = true;
    }

    void reset()
    {
        clear_some();
    }

    [[nodiscard]] result time_control(int moves, chess::Color side2move)
    {
        if (ponder)
        {
            return {depth, movetime, movetime, false};
        }

        int64_t inc, time;
        bool bonus = false;
        if (side2move == chess::Color::WHITE)
        {
            inc = winc;
            time = wtime;
            bonus = time > btime;
        }
        else
        {
            inc = binc;
            time = btime;
            bonus = time > wtime;
        }

        // ignore if time and inc are invalid
        if (time == param::TIME_MAX || inc == param::TIME_MAX)
            return {depth, movetime, movetime, false};

        if (movetime != param::TIME_MAX)
            return {depth, movetime, movetime, false};

        // https://github.com/gab8192/Obsidian/blob/main/src/timeman.cpp
        int mtg = 40;
        int64_t time_left =
            std::max(int64_t(1), time + inc * (mtg - 1) - move_overhead * (2 + mtg));

        double opt_scale = std::min(0.025, 0.21 * time / double(time_left));
        int64_t optimum_time = opt_scale * time_left;
        int64_t max_time = std::min((double)optimum_time * 3.0, time * 0.7 - move_overhead);

        return {depth, std::max((int64_t)1, max_time), std::max((int64_t)1, optimum_time), true};
    }
};