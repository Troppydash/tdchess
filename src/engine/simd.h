#pragma once

#include <stdlib.h>

#if defined(__AVX2__)
#include <immintrin.h>
#else
#include <arm_neon.h>
#endif

namespace simd
{

#if defined(__AVX2__)

using Vec = __m256i;
constexpr size_t WIDTH = 16;
constexpr size_t ALIGN = 64;

inline __attribute__((always_inline)) Vec add16(Vec a, Vec b)
{
    return _mm256_add_epi16(a, b);
}

inline __attribute__((always_inline)) Vec sub16(Vec a, Vec b)
{
    return _mm256_sub_epi16(a, b);
}
#else
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

#endif
} // namespace simd
