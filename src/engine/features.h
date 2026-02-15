#pragma once

#include <cmath>
#include <string>
#include <vector>

#ifdef TDCHESS_TUNE
#define TUNE(name, value, delta, min, max)                                                         \
    inline int name = value;                                                                       \
    inline volatile __attribute__((used)) tunable_feature_register reg_##name{#name, value, delta,          \
                                                                     min,   max,   &name};

// actual feature representation
struct tunable_feature
{
    std::string name;
    double value;
    int delta;
    int min;
    int max;
    int *ref;

    void apply() const
    {
        *ref = std::round(value);
    }

    tunable_feature add(double bonus) const
    {
        return tunable_feature{
            name, std::clamp(value + bonus, (double)min, (double)max), delta, min, max, ref};
    }
};

// temp list for features
inline std::vector<tunable_feature> &tunable_features_list()
{
    static std::vector<tunable_feature> list{};
    return list;
}

// reg temp class
struct tunable_feature_register
{
    tunable_feature_register(const std::string &name, double value, int delta, int min, int max,
                             int *ref)
    {
        tunable_features_list().push_back({name, value, delta, min, max, ref});
    }
};

#else
#define TUNE(name, value, delta, min, max) constexpr int name = value
#endif

namespace features
{

TUNE(HISTORY_MULT, 43, 2, 16, 128);
TUNE(HISTORY_BASE, 19, 3, -100, 100);

TUNE(HISTORY_MALUS_MULT, 43, 2, 16, 128);
TUNE(HISTORY_MALUS_BASE, 28, 3, -100, 100);

} // namespace features