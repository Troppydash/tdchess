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
TUNE(HISTORY_MULT, 35, 0, 16, 128);
TUNE(HISTORY_BASE, 74, 0, -100, 100);

TUNE(HISTORY_MALUS_MULT, 86, 0, 16, 128);
TUNE(HISTORY_MALUS_BASE, -5, 0, -100, 100);

TUNE(SNM_MARGIN, 75, 0, 25, 150);

TUNE(NMP_DEPTH, 0, 1, 0, 10);
TUNE(NMP_REDUCTION_MULT, 6, 0, 1, 10);
TUNE(NMP_REDUCTION_BASE, 6, 0, 1, 10);

TUNE(QUIET_LMR_DIV, 11888, 0, 1000, 15000);
TUNE(CAPTURE_LMR_DIV, 12085, 0, 1000, 15000);

TUNE(RESEARCH_HIGH, 59, 0, 0, 100);
TUNE(RESEARCH_LOW, 19, 0, 0, 50);

TUNE(QSEARCH_FUT_OFFSET, 300, 0, 100, 500);
TUNE(QSEARCH_SEE_PRUNE, -80, 0, -150, -50);
TUNE(HISTORY_EARLY_CUTOFF, 3, 1, 0, 5);

TUNE(RAZOR_BASE, 500, 0, 300, 700);
TUNE(RAZOR_DEPTH_MULT, 300, 0, 200, 400);

TUNE(PROBC_BETA_OFFSET, 300, 0, 200, 400);
TUNE(PROBC_DEPTH, 3, 1, 2, 6);
TUNE(PROBC_DEPTH_REDUCTION, 4, 1, 2, 6);

TUNE(FUT_CAP_DEPTH_LIMIT, 7, 1, 5, 9);
TUNE(FUT_CAP_BASE, 300, 0, 200, 400);
TUNE(FUT_CAP_DEPTH_MULT, 300, 0, 200, 400);
TUNE(SEE_CAP_BASE, 200, 0, 100, 300);
TUNE(SEE_CAP_DEPTH_MULT, 400, 0, 300, 500);

TUNE(FUT_QUIET_DEPTH_LIMIT, 12, 1, 10, 14);
TUNE(FUT_QUIET_BASE, 200, 0, 100, 300);
TUNE(FUT_QUIET_DEPTH_MULT, 300, 0, 200, 400);
TUNE(SEE_QUIET_BASE, 100, 0, 50, 150);
TUNE(SEE_QUIET_DEPTH_MULT, 50, 0, 25, 100);

TUNE(SINGULAR_MARGIN, 200, 0, 100, 300);

/// MOVE ORDERING PARAMETERS ///
TUNE(CAPTURE_MVV_SCALE, 7, 1, 5, 9);

TUNE(GOOD_CAPTURE_SEE_DIV, 25, 0, 20, 30);
TUNE(PROB_GOOD_CAPTURE_SEE_DIV, 60, 0, 50, 70);

TUNE(QUIET_LOW_PLY_SCALE, 8, 1, 6, 10);
TUNE(QUIET_BAD_THRESHOLD, -5000, 0, -7000, -3000);

//
// TUNE(HISTORY_MULT, 60, 0, 16, 128);
// TUNE(HISTORY_BASE, 36, 0, -100, 100);
// TUNE(HISTORY_MALUS_MULT, 96, 0, 16, 128);
// TUNE(HISTORY_MALUS_BASE, -65, 0, -100, 100);
// TUNE(SNM_MARGIN, 74, 0, 25, 150);
// TUNE(NMP_DEPTH, 0, 1, 0, 10);
// TUNE(NMP_REDUCTION_MULT, 6, 1, 1, 10);
// TUNE(NMP_REDUCTION_BASE, 6, 1, 1, 10);
// TUNE(QUIET_LMR_DIV, 11911, 0, 1000, 17000);
// TUNE(CAPTURE_LMR_DIV, 11849, 0, 1000, 17000);
// TUNE(RESEARCH_HIGH, 58, 0, 0, 100);
// TUNE(RESEARCH_LOW, 40, 0, 0, 60);
// TUNE(QSEARCH_FUT_OFFSET, 241, 0, 100, 500);
// TUNE(QSEARCH_SEE_PRUNE, -88, 0, -150, -50);
// TUNE(HISTORY_EARLY_CUTOFF, 3, 1, 0, 5);
// TUNE(RAZOR_BASE, 576, 0, 300, 700);
// TUNE(RAZOR_DEPTH_MULT, 260, 0, 200, 400);
// TUNE(PROBC_BETA_OFFSET, 336, 0, 200, 400);
// TUNE(PROBC_DEPTH, 3, 1, 2, 6);
// TUNE(PROBC_DEPTH_REDUCTION, 4, 1, 2, 6);
// TUNE(FUT_CAP_DEPTH_LIMIT, 7, 1, 5, 9);
// TUNE(FUT_CAP_BASE, 346, 0, 200, 400);
// TUNE(FUT_CAP_DEPTH_MULT, 273, 0, 200, 400);
// TUNE(SEE_CAP_BASE, 185, 0, 100, 300);
// TUNE(SEE_CAP_DEPTH_MULT, 455, 0, 300, 500);
// TUNE(FUT_QUIET_DEPTH_LIMIT, 12, 1, 10, 14);
// TUNE(FUT_QUIET_BASE, 266, 0, 100, 300);
// TUNE(FUT_QUIET_DEPTH_MULT, 287, 0, 200, 400);
// TUNE(SEE_QUIET_BASE, 109, 0, 50, 150);
// TUNE(SEE_QUIET_DEPTH_MULT, 68, 0, 25, 100);
// TUNE(SINGULAR_MARGIN, 200, 0, 100, 300);
// TUNE(CAPTURE_MVV_SCALE, 7, 1, 5, 9);
// TUNE(GOOD_CAPTURE_SEE_DIV, 28, 0, 20, 30);
// TUNE(PROB_GOOD_CAPTURE_SEE_DIV, 60, 0, 50, 70);
// TUNE(QUIET_LOW_PLY_SCALE, 8, 1, 6, 10);
// TUNE(QUIET_BAD_THRESHOLD, -4755, 0, -7000, -3000);

// TUNE(HISTORY_MULT, 55, 0, 16, 128);
// TUNE(HISTORY_BASE, 21, 0, -100, 100);
// TUNE(HISTORY_MALUS_MULT, 99, 0, 16, 128);
// TUNE(HISTORY_MALUS_BASE, -68, 0, -100, 100);
// TUNE(SNM_MARGIN, 78, 0, 25, 150);
// TUNE(NMP_DEPTH, 0, 1, 0, 10);
// TUNE(NMP_REDUCTION_MULT, 6, 1, 1, 10);
// TUNE(NMP_REDUCTION_BASE, 6, 1, 1, 10);
// TUNE(QUIET_LMR_DIV, 13132, 0, 1000, 17000);
// TUNE(CAPTURE_LMR_DIV, 11708, 0, 1000, 17000);
// TUNE(RESEARCH_HIGH, 55, 0, 0, 100);
// TUNE(RESEARCH_LOW, 45, 0, 0, 60);
// TUNE(QSEARCH_FUT_OFFSET, 210, 0, 100, 500);
// TUNE(QSEARCH_SEE_PRUNE, -78, 0, -150, -50);
// TUNE(HISTORY_EARLY_CUTOFF, 3, 1, 0, 5);
// TUNE(RAZOR_BASE, 573, 0, 300, 700);
// TUNE(RAZOR_DEPTH_MULT, 263, 0, 200, 400);
// TUNE(PROBC_BETA_OFFSET, 330, 0, 200, 400);
// TUNE(PROBC_DEPTH, 3, 1, 2, 6);
// TUNE(PROBC_DEPTH_REDUCTION, 4, 1, 2, 6);
// TUNE(FUT_CAP_DEPTH_LIMIT, 7, 1, 5, 9);
// TUNE(FUT_CAP_BASE, 346, 0, 200, 400);
// TUNE(FUT_CAP_DEPTH_MULT, 282, 0, 200, 400);
// TUNE(SEE_CAP_BASE, 180, 0, 100, 300);
// TUNE(SEE_CAP_DEPTH_MULT, 446, 0, 300, 500);
// TUNE(FUT_QUIET_DEPTH_LIMIT, 12, 1, 10, 14);
// TUNE(FUT_QUIET_BASE, 280, 0, 100, 300);
// TUNE(FUT_QUIET_DEPTH_MULT, 297, 0, 200, 400);
// TUNE(SEE_QUIET_BASE, 108, 0, 50, 150);
// TUNE(SEE_QUIET_DEPTH_MULT, 72, 0, 25, 100);
// TUNE(SINGULAR_MARGIN, 197, 0, 100, 300);
// TUNE(CAPTURE_MVV_SCALE, 7, 1, 5, 9);
// TUNE(GOOD_CAPTURE_SEE_DIV, 29, 0, 20, 30);
// TUNE(PROB_GOOD_CAPTURE_SEE_DIV, 59, 0, 50, 70);
// TUNE(QUIET_LOW_PLY_SCALE, 8, 1, 6, 10);
// TUNE(QUIET_BAD_THRESHOLD, -4482, 0, -7000, -3000);

} // namespace features