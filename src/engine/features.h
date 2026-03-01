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
TUNE(HISTORY_MULT, 300, 0, 250, 350);
TUNE(HISTORY_BASE, -250, 0, -300, -200);
TUNE(MAX_HISTORY_UPDATE, 1500, 0, 1000, 3000);

TUNE(HISTORY_MALUS_MULT, 300, 0, 250, 350);
TUNE(HISTORY_MALUS_BASE, -250, 0, -300, -200);

TUNE(SNM_MARGIN, 60, 0, 25, 150);

TUNE(NMP_DEPTH, 0, 1, 0, 10);
TUNE(NMP_REDUCTION_MULT, 4, 0, 1, 10);
TUNE(NMP_REDUCTION_BASE, 7, 0, 1, 10);

TUNE(QUIET_LMR_DIV, 10000, 0, 1000, 15000);
TUNE(CAPTURE_LMR_DIV, 6000, 0, 1000, 15000);

TUNE(RESEARCH_HIGH, 50, 0, 0, 100);
TUNE(RESEARCH_LOW, 10, 0, 0, 50);

TUNE(QSEARCH_FUT_OFFSET, 300, 0, 100, 500);
TUNE(QSEARCH_SEE_PRUNE, -50, 0, -150, -50);
TUNE(HISTORY_EARLY_CUTOFF, 3, 1, 0, 5);

TUNE(RAZOR_BASE, 0, 0, 300, 700);
TUNE(RAZOR_DEPTH_MULT, 300, 0, 200, 400);

TUNE(PROBC_BETA_OFFSET, 200, 0, 200, 400);
TUNE(PROBC_DEPTH, 5, 1, 2, 6);
TUNE(PROBC_DEPTH_REDUCTION, 4, 1, 2, 6);

TUNE(SEE_CAP_BASE, 100, 0, 100, 300);
TUNE(SEE_CAP_DEPTH_MULT, 100, 0, 300, 500);

TUNE(FUT_QUIET_DEPTH_LIMIT, 12, 1, 10, 14);
TUNE(SEE_QUIET_BASE, 50, 0, 50, 150);
TUNE(SEE_QUIET_DEPTH_MULT, 50, 0, 25, 100);

TUNE(SINGULAR_MARGIN, 100, 0, 100, 300);

/// MOVE ORDERING PARAMETERS ///
TUNE(CAPTURE_MVV_SCALE, 7, 1, 5, 9);

TUNE(GOOD_CAPTURE_SEE_DIV, 25, 0, 20, 30);
TUNE(PROB_GOOD_CAPTURE_SEE_DIV, 40, 0, 50, 70);

TUNE(QUIET_LOW_PLY_SCALE, 8, 1, 6, 10);
TUNE(QUIET_BAD_THRESHOLD, -8000, 0, -7000, -3000);

TUNE(TB_HIT_DEPTH, 1, 0, 0, 10);

//
// TUNE(HISTORY_MULT, 306, 0, 250, 350);
// TUNE(HISTORY_BASE, -246, 0, -300, -200);
// TUNE(HISTORY_MALUS_MULT, 287, 0, 250, 350);
// TUNE(HISTORY_MALUS_BASE, -243, 0, -300, -200);
// TUNE(SNM_MARGIN, 69, 0, 25, 150);
// TUNE(NMP_DEPTH, 0, 1, 0, 10);
// TUNE(NMP_REDUCTION_MULT, 6, 0, 1, 10);
// TUNE(NMP_REDUCTION_BASE, 7, 0, 1, 10);
// TUNE(QUIET_LMR_DIV, 13711, 0, 1000, 15000);
// TUNE(CAPTURE_LMR_DIV, 11188, 0, 1000, 15000);
// TUNE(RESEARCH_HIGH, 64, 0, 0, 100);
// TUNE(RESEARCH_LOW, 27, 0, 0, 50);
// TUNE(QSEARCH_FUT_OFFSET, 292, 0, 100, 500);
// TUNE(QSEARCH_SEE_PRUNE, -84, 0, -150, -50);
// TUNE(HISTORY_EARLY_CUTOFF, 3, 1, 0, 5);
// TUNE(RAZOR_BASE, 438, 0, 300, 700);
// TUNE(RAZOR_DEPTH_MULT, 323, 0, 200, 400);
// TUNE(PROBC_BETA_OFFSET, 317, 0, 200, 400);
// TUNE(PROBC_DEPTH, 3, 1, 2, 6);
// TUNE(PROBC_DEPTH_REDUCTION, 4, 1, 2, 6);
// TUNE(FUT_CAP_DEPTH_LIMIT, 7, 1, 5, 9);
// TUNE(FUT_CAP_BASE, 305, 0, 200, 400);
// TUNE(FUT_CAP_DEPTH_MULT, 265, 0, 200, 400);
// TUNE(SEE_CAP_BASE, 200, 0, 100, 300);
// TUNE(SEE_CAP_DEPTH_MULT, 394, 0, 300, 500);
// TUNE(FUT_QUIET_DEPTH_LIMIT, 12, 1, 10, 14);
// TUNE(FUT_QUIET_BASE, 183, 0, 100, 300);
// TUNE(FUT_QUIET_DEPTH_MULT, 324, 0, 200, 400);
// TUNE(SEE_QUIET_BASE, 92, 0, 50, 150);
// TUNE(SEE_QUIET_DEPTH_MULT, 54, 0, 25, 100);
// TUNE(SINGULAR_MARGIN, 207, 0, 100, 300);
// TUNE(CAPTURE_MVV_SCALE, 7, 1, 5, 9);
// TUNE(GOOD_CAPTURE_SEE_DIV, 24, 0, 20, 30);
// TUNE(PROB_GOOD_CAPTURE_SEE_DIV, 61, 0, 50, 70);
// TUNE(QUIET_LOW_PLY_SCALE, 8, 1, 6, 10);
// TUNE(QUIET_BAD_THRESHOLD, -5360, 0, -7000, -3000);
//
// TUNE(HISTORY_MULT, 306, 0, 250, 350);
// TUNE(HISTORY_BASE, -246, 0, -300, -200);
// TUNE(HISTORY_MALUS_MULT, 282, 0, 250, 350);
// TUNE(HISTORY_MALUS_BASE, -245, 0, -300, -200);
// TUNE(SNM_MARGIN, 73, 0, 25, 150);
// TUNE(NMP_DEPTH, 0, 1, 0, 10);
// TUNE(NMP_REDUCTION_MULT, 8, 0, 1, 10);
// TUNE(NMP_REDUCTION_BASE, 7, 0, 1, 10);
// TUNE(QUIET_LMR_DIV, 12281, 0, 1000, 15000);
// TUNE(CAPTURE_LMR_DIV, 11184, 0, 1000, 15000);
// TUNE(RESEARCH_HIGH, 55, 0, 0, 100);
// TUNE(RESEARCH_LOW, 26, 0, 0, 50);
// TUNE(QSEARCH_FUT_OFFSET, 312, 0, 100, 500);
// TUNE(QSEARCH_SEE_PRUNE, -92, 0, -150, -50);
// TUNE(HISTORY_EARLY_CUTOFF, 3, 1, 0, 5);
// TUNE(RAZOR_BASE, 417, 0, 300, 700);
// TUNE(RAZOR_DEPTH_MULT, 315, 0, 200, 400);
// TUNE(PROBC_BETA_OFFSET, 304, 0, 200, 400);
// TUNE(PROBC_DEPTH, 3, 1, 2, 6);
// TUNE(PROBC_DEPTH_REDUCTION, 4, 1, 2, 6);
// TUNE(FUT_CAP_DEPTH_LIMIT, 7, 1, 5, 9);
// TUNE(FUT_CAP_BASE, 298, 0, 200, 400);
// TUNE(FUT_CAP_DEPTH_MULT, 244, 0, 200, 400);
// TUNE(SEE_CAP_BASE, 201, 0, 100, 300);
// TUNE(SEE_CAP_DEPTH_MULT, 361, 0, 300, 500);
// TUNE(FUT_QUIET_DEPTH_LIMIT, 12, 1, 10, 14);
// TUNE(FUT_QUIET_BASE, 204, 0, 100, 300);
// TUNE(FUT_QUIET_DEPTH_MULT, 348, 0, 200, 400);
// TUNE(SEE_QUIET_BASE, 78, 0, 50, 150);
// TUNE(SEE_QUIET_DEPTH_MULT, 47, 0, 25, 100);
// TUNE(SINGULAR_MARGIN, 226, 0, 100, 300);
// TUNE(CAPTURE_MVV_SCALE, 7, 1, 5, 9);
// TUNE(GOOD_CAPTURE_SEE_DIV, 24, 0, 20, 30);
// TUNE(PROB_GOOD_CAPTURE_SEE_DIV, 60, 0, 50, 70);
// TUNE(QUIET_LOW_PLY_SCALE, 8, 1, 6, 10);
// TUNE(QUIET_BAD_THRESHOLD, -5840, 0, -7000, -3000);

} // namespace features