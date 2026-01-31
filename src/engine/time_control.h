#pragma once

#include "param.h"

#include <algorithm>
#include <cstdint>
#include <limits>

struct search_param
{
    int wtime = std::numeric_limits<int>::max();
    int btime = std::numeric_limits<int>::max();
    int winc = std::numeric_limits<int>::max();
    int binc = std::numeric_limits<int>::max();
    int depth = param::MAX_DEPTH;
    int movetime = std::numeric_limits<int>::max();

    struct result
    {
        int16_t depth;
        int16_t time;
    };

    [[nodiscard]] result time_control(int side2move) const
    {
        int inc, time;
        if (side2move == 0)
        {
            inc = winc;
            time = wtime;
        }
        else
        {
            inc = binc;
            time = btime;
        }

        int target_time = std::floor(time / 20.0 + inc / 2.0);

        return {static_cast<int16_t>(depth), static_cast<int16_t>(std::min(target_time, movetime))};
    }
};