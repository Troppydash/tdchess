#pragma once
#include <array>
#include <format>
#include <string>


class pentanomial
{
private:
    std::array<int, 5> result;

    explicit pentanomial(std::array<int, 5> result)
        : result(result)
    {
    }

public:
    pentanomial operator+(const pentanomial &other) const
    {
        std::array<int, 5> total = result;
        for (size_t i = 0; i < 5; ++i)
            total[i] += other.result[i];

        return pentanomial{total};
    }

    static pentanomial from_scores(std::pair<double, double> scores)
    {
        int index = 0;
        if (scores == std::make_pair(2.0, 0.0))
            index = 0;
        else if (scores == std::make_pair(1.5, 0.5))
            index = 1;
        else if (scores == std::make_pair(1.0, 1.0))
            index = 2;
        else if (scores == std::make_pair(0.5, 1.5))
            index = 3;
        else if (scores == std::make_pair(0.0, 2.0))
            index = 4;
        else
            throw std::runtime_error{"impossible scores"};

        std::array<int, 5> result{0, 0, 0, 0, 0};
        result[index] += 1;
        return pentanomial{result};
    }

    [[nodiscard]] std::string display() const
    {
        return std::format("[{}, {}, {}, {}, {}]", result[0], result[1], result[2], result[3], result[4]);
    }

    [[nodiscard]] std::pair<double, double> to_score() const
    {
        if (result[0])
            return {2.0, 0.0};

        if (result[1])
            return {1.5, 0.5};

        if (result[2])
            return {1, 1};

        if (result[3])
            return {0.5, 1.5};

        if (result[4])
            return {0.0, 2.0};

        throw std::runtime_error{"impossible scores"};
    }
};
