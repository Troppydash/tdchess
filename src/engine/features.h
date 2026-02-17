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

/// SEARCH PARAMETERS ///
TUNE(HISTORY_MULT, 35, 0, 16, 128);
TUNE(HISTORY_BASE, 74, 0, -100, 100);

TUNE(HISTORY_MALUS_MULT, 86, 0, 16, 128);
TUNE(HISTORY_MALUS_BASE, -5, 0, -100, 100);

TUNE(SNM_MARGIN, 75, 0, 25, 150);

TUNE(NMP_DEPTH, 0, 0, 0, 10);
TUNE(NMP_REDUCTION_MULT, 6, 0, 1, 10);
TUNE(NMP_REDUCTION_BASE, 6, 0, 1, 10);

TUNE(QUIET_LMR_DIV, 11888, 0, 1000, 15000);
TUNE(CAPTURE_LMR_DIV, 12085, 0, 1000, 15000);

TUNE(RESEARCH_HIGH, 59, 0, 0, 100);
TUNE(RESEARCH_LOW, 19, 0, 0, 50);

TUNE(QSEARCH_FUT_OFFSET, 300, 0, 100, 500);
TUNE(QSEARCH_SEE_PRUNE, -80, 0, -150, -50);
TUNE(HISTORY_EARLY_CUTOFF, 3, 0, 0, 5);

TUNE(RAZOR_BASE, 500, 0, 300, 700);
TUNE(RAZOR_DEPTH_MULT, 300, 0, 200, 400);

TUNE(PROBC_BETA_OFFSET, 300, 0, 100, 500);
TUNE(PROBC_DEPTH, 3, 0, 2, 6);
TUNE(PROBC_DEPTH_REDUCTION, 3, 0, 2, 6);

TUNE(FUT_CAP_BASE, 300, 0, 200, 400);
TUNE(FUT_CAP_DEPTH_MULT, 300, 0, 200, 400);
TUNE(SEE_CAP_BASE, 200, 0, 100, 300);
TUNE(SEE_CAP_DEPTH_MULT, 400, 0, 300, 500);

TUNE(FUT_QUIET_BASE, 200, 0, 100, 300);
TUNE(FUT_QUIET_DEPTH_MULT, 300, 0, 200, 400);
TUNE(SEE_QUIET_BASE, 100, 0, 50, 150);
TUNE(SEE_QUIET_DEPTH_MULT, 50, 0, 25, 100);

TUNE(SINGULAR_MARGIN, 200, 0, 100, 300);

/// MOVE ORDERING PARAMETERS ///






} // namespace features