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

    bool is_active() const
    {
        return delta == 0;
    }

    [[deprecated]]
    int get() const
    {
        return std::clamp(std::round(value), (double)min, (double)max);
    }

    [[deprecated]]
    tunable_feature add(double bonus) const
    {
        // ignore change if positive delta
        if (delta > 0)
            return *this;

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
TUNE(HISTORY_MULT, 300, 0, 100, 400);
TUNE(HISTORY_BASE, -250, 0, -400, -100);
TUNE(MAX_HISTORY_UPDATE, 1500, 0, 1000, 3000);

TUNE(HISTORY_MALUS_MULT, 300, 0, 100, 400);
TUNE(HISTORY_MALUS_BASE, -250, 0, -400, -100);

TUNE(SNM_MARGIN, 60, 0, 25, 150);

TUNE(NMP_DEPTH, 0, 1, 0, 10);
TUNE(NMP_REDUCTION_MULT, 4, 1, 1, 10);
TUNE(NMP_REDUCTION_BASE, 7, 1, 1, 10);

TUNE(QUIET_LMR_DIV, 8000, 1, 1000, 15000);
TUNE(CAPTURE_LMR_DIV, 7000, 1, 1000, 15000);

TUNE(RESEARCH_HIGH, 50, 1, 0, 100);
TUNE(RESEARCH_LOW, 10, 1, 0, 50);

TUNE(QSEARCH_FUT_OFFSET, 400, 1, 100, 500);
TUNE(QSEARCH_SEE_PRUNE, -50, 1, -150, -50);
TUNE(HISTORY_EARLY_CUTOFF, 3, 1, 0, 5);

TUNE(RAZOR_BASE, 0, 1, 300, 700);
TUNE(RAZOR_DEPTH_MULT, 400, 1, 200, 400);

TUNE(PROBC_BETA_OFFSET, 200, 1, 200, 400);
TUNE(PROBC_DEPTH, 5, 1, 2, 6);
TUNE(PROBC_DEPTH_REDUCTION, 4, 1, 2, 6);

TUNE(SEE_CAP_BASE, 100, 1, 100, 300);
TUNE(SEE_CAP_DEPTH_MULT, 100, 1, 300, 500);

TUNE(FUT_QUIET_DEPTH_LIMIT, 12, 1, 10, 14);
TUNE(SEE_QUIET_BASE, 50, 1, 50, 150);
TUNE(SEE_QUIET_DEPTH_MULT, 50, 1, 25, 100);

TUNE(SINGULAR_MARGIN, 100, 1, 100, 300);

/// MOVE ORDERING PARAMETERS ///
TUNE(CAPTURE_MVV_SCALE, 7, 1, 5, 9);

TUNE(GOOD_CAPTURE_SEE_DIV, 25, 1, 20, 30);

TUNE(QUIET_LOW_PLY_SCALE, 8, 1, 6, 10);
TUNE(QUIET_BAD_THRESHOLD, -13000, 1, -7000, -3000);

TUNE(TB_HIT_DEPTH, 1, 1, 0, 10);

} // namespace features