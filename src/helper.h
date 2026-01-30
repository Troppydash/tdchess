#pragma once

#include <cstdint>
#include <vector>
#include <string>

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

inline std::vector<std::string> string_split(std::string const &input)
{
    std::stringstream ss(input);

    std::vector<std::string> words((std::istream_iterator<std::string>(ss)), std::istream_iterator<std::string>());

    return words;
}
} // namespace helper
