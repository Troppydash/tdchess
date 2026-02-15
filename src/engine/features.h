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
    double delta;
    int min;
    int max;
    int *ref;

    void apply() const
    {
        *ref = get();
    }

    int get() const
    {
        return std::clamp(std::round(value), (double)min, (double)max);
    }

    tunable_feature add(double bonus) const
    {
        return tunable_feature{
            name, value + bonus, delta, min, max, ref};
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
    tunable_feature_register(const std::string &name, double value, double delta, int min, int max,
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
//
// TUNE(HISTORY_MULT, 64, 2, 16, 128);
// TUNE(HISTORY_BASE, 0, 3, -100, 100);
//
// TUNE(HISTORY_MALUS_MULT, 64, 2, 16, 128);
// TUNE(HISTORY_MALUS_BASE, 0, 3, -100, 100);
//
// TUNE(SNM_MARGIN, 85, 2, 25, 150);
//
// TUNE(NMP_DEPTH, 0, 0.2, 0, 10);
// TUNE(NMP_REDUCTION_MULT, 4, 0.2, 1, 10);
// TUNE(NMP_REDUCTION_BASE, 6, 0.2, 1, 10);


TUNE(HISTORY_MULT, 63, 4, 16, 128);
TUNE(HISTORY_BASE, -1, 6, -100, 100);

TUNE(HISTORY_MALUS_MULT, 64, 4, 16, 128);
TUNE(HISTORY_MALUS_BASE, -2, 6, -100, 100);

TUNE(SNM_MARGIN, 87, 4, 25, 150);

TUNE(NMP_DEPTH, 2, 0.1, 0, 10);
TUNE(NMP_REDUCTION_MULT, 4, 0.1, 1, 10);
TUNE(NMP_REDUCTION_BASE, 4, 0.1, 1, 10);

} // namespace features