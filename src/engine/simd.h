#pragma once

#include <arm_neon.h>
#include <stdlib.h>

namespace simd
{

// needs this to prevent aliasing in evaluate/catchup
using Vec __attribute__((may_alias)) = int16x8x4_t;
constexpr size_t WIDTH = 32;
constexpr size_t ALIGN = 64;

inline __attribute__((always_inline)) Vec add16(Vec a, Vec b)
{
    return {vaddq_s16(a.val[0], b.val[0]), vaddq_s16(a.val[1], b.val[1]),
            vaddq_s16(a.val[2], b.val[2]), vaddq_s16(a.val[3], b.val[3])};
}

inline __attribute__((always_inline)) Vec sub16(Vec a, Vec b)
{
    return {vsubq_s16(a.val[0], b.val[0]), vsubq_s16(a.val[1], b.val[1]),
            vsubq_s16(a.val[2], b.val[2]), vsubq_s16(a.val[3], b.val[3])};
}

} // namespace simd
