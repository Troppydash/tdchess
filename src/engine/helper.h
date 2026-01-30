#pragma once

namespace helper
{
constexpr int16_t clamp(int16_t x, int16_t lower, int16_t upper)
{
    if (x < lower)
        return lower;
    if (x > upper)
        return upper;

    return x;
}
} // namespace helper
