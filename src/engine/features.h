#pragma once

#include <cmath>
#include <string>
#include <vector>

#ifdef TDCHESS_TUNE
#define TUNE(name, value, delta, min, max)                                                         \
    inline int name = value;                                                                       \
    inline volatile __attribute__((used)) tunable_feature_register reg_##name{                     \
        #name, value, delta, min, max, &name};

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
        bonus *= (max - min);
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
// TUNE(HISTORY_MULT, 63, 4, 16, 128);
// TUNE(HISTORY_BASE, -1, 6, -100, 100);
//
// TUNE(HISTORY_MALUS_MULT, 64, 4, 16, 128);
// TUNE(HISTORY_MALUS_BASE, -2, 6, -100, 100);
//
// TUNE(SNM_MARGIN, 87, 4, 25, 150);
//
// TUNE(NMP_DEPTH, 0, 0.1, 0, 10);
// TUNE(NMP_REDUCTION_MULT, 4, 0.1, 1, 10);
// TUNE(NMP_REDUCTION_BASE, 6, 0.1, 1, 10);
//
// TUNE(QUIET_LMR_DIV, 10000, 300, 1000, 15000);
// TUNE(CAPTURE_LMR_DIV, 10000, 300, 1000, 15000);
//
// TUNE(RESEARCH_HIGH, 55, 1, 0, 100);
// TUNE(RESEARCH_LOW, 10, 0.2, 0, 50);

TUNE(HISTORY_MULT, 41, 4, 16, 128);
TUNE(HISTORY_BASE, 53, 6, -100, 100);

TUNE(HISTORY_MALUS_MULT, 85, 4, 16, 128);
TUNE(HISTORY_MALUS_BASE, -27, 6, -100, 100);

TUNE(SNM_MARGIN, 67, 4, 25, 150);

TUNE(NMP_DEPTH, 0, 0.1, 0, 10);
TUNE(NMP_REDUCTION_MULT, 6, 0.1, 1, 10);
TUNE(NMP_REDUCTION_BASE, 7, 0.1, 1, 10);

TUNE(QUIET_LMR_DIV, 12707, 300, 1000, 15000);
TUNE(CAPTURE_LMR_DIV, 12335, 300, 1000, 15000);

TUNE(RESEARCH_HIGH, 55, 1, 0, 100);
TUNE(RESEARCH_LOW, 19, 0.2, 0, 50);

// TUNE(HISTORY_MULT, 64, 4, 16, 128);
// TUNE(HISTORY_BASE, -2, 6, -100, 100);
//
// TUNE(HISTORY_MALUS_MULT, 64, 4, 16, 128);
// TUNE(HISTORY_MALUS_BASE, -2, 6, -100, 100);
//
// TUNE(SNM_MARGIN, 89, 4, 25, 150);
//
// TUNE(NMP_DEPTH, 2, 0.1, 0, 10);
// TUNE(NMP_REDUCTION_MULT, 4, 0.1, 1, 10);
// TUNE(NMP_REDUCTION_BASE, 7, 0.1, 1, 10);
//
// TUNE(QUIET_LMR_DIV, 10000, 100, 1000, 15000);
// TUNE(CAPTURE_LMR_DIV, 10000, 100, 1000, 15000);
//
// TUNE(RESEARCH_HIGH, 57, 2, 0, 100);
// TUNE(RESEARCH_LOW, 9, 0.5, 0, 50);

} // namespace features