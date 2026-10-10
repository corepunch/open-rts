#ifndef __FIXED__
#define __FIXED__

/* Deterministic integer math for the simulation. Every peer must compute the
 * same bits, so simulation code must not call libm (sin/cos/pow differ between
 * libc implementations) and should not depend on float rounding. Values are
 * 16.16 fixed point (fixed_t, FIXED_ONE in engine.h); angles are the 32-bit
 * BAM angle_t used by p_facing.c (ANG90 == 0x40000000). */

#include <stdint.h>

typedef struct { int32_t x, y; } fixed2_t; /* 16.16 planar vector */

/* Compile-time 16.16 literal (constant-folded, correctly rounded). */
#define FIXED_LIT(v) ((int32_t)((v) * 65536.0 + ((v) < 0 ? -0.5 : 0.5)))
/* 16.16 vector from two literal coordinates (authored data and tests). */
#define FIXED2_LIT(x, y) ((fixed2_t){ FIXED_LIT(x), FIXED_LIT(y) })
/* Same for a 32.32 squared-distance threshold. */
#define FIXED_LIT_64(v) ((int64_t)((v) * 4294967296.0 + 0.5))


/* Sine of a quarter turn, 1025 samples (index i = i * 90deg / 1024), 16.16.
 * Generated once, round-to-nearest; checked into the repo as an integer table. */
extern const int32_t fixed_sin_quarter[1025]; /* defined in play/p_facing.c */

/* Floor of sqrt(n), exact for all 64-bit inputs (no floating point). */
static inline uint32_t fixed_isqrt64(uint64_t n) {
    uint64_t result = 0, bit = UINT64_C(1) << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= result + bit) { n -= result + bit; result = (result >> 1) + bit; }
        else result >>= 1;
        bit >>= 2;
    }
    return (uint32_t)result;
}

/* 16.16 multiply / divide, rounded toward zero, 64-bit intermediate. */
static inline int32_t fixed_mul32(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * b + 32768) >> 16);
}
static inline int32_t fixed_div32(int32_t a, int32_t b) {
    return b ? (int32_t)(((int64_t)a * 65536) / b) : (a >= 0 ? INT32_MAX : INT32_MIN);
}
/* sqrt of a 16.16 value as 16.16: sqrt(x * 2^16) * 2^8 == isqrt(x << 16). */
static inline int32_t fixed_sqrt32(int32_t x) {
    return x <= 0 ? 0 : (int32_t)fixed_isqrt64((uint64_t)x << 16);
}
/* Length of a 16.16 vector, 16.16. */
static inline int32_t fixed_hypot32(int32_t x, int32_t y) {
    uint64_t sum = (uint64_t)((int64_t)x * x) + (uint64_t)((int64_t)y * y); /* 32.32 */
    return (int32_t)fixed_isqrt64(sum);
}

/* sin(angle) as 16.16, angle in BAM units (2^32 == full turn). The table has
 * 4096 steps per turn; the low 20 bits interpolate linearly between samples
 * (error < 4e-7, below one 16.16 unit). Exact at multiples of 90 degrees. */
static inline int32_t fixed_sin_sample(uint32_t step) {
    step &= 4095u;
    uint32_t quadrant = step >> 10, i = step & 1023u;
    switch (quadrant) {
    case 0: return fixed_sin_quarter[i];
    case 1: return fixed_sin_quarter[1024 - i];
    case 2: return -fixed_sin_quarter[i];
    default: return -fixed_sin_quarter[1024 - i];
    }
}
static inline int32_t fixed_sin_bam(uint32_t angle) {
    uint32_t step = angle >> 20, frac = angle & 0xFFFFFu;
    int32_t a = fixed_sin_sample(step), b = fixed_sin_sample(step + 1);
    return a + (int32_t)(((int64_t)(b - a) * frac + (1 << 19)) >> 20);
}
static inline int32_t fixed_cos_bam(uint32_t angle) {
    return fixed_sin_bam(angle + 0x40000000u);
}

/* Dot / cross product of 16.16 vectors, 16.16 result. */
static inline int32_t fixed2_dot(fixed2_t a, fixed2_t b) {
    return (int32_t)(((int64_t)a.x * b.x + (int64_t)a.y * b.y) >> 16);
}
static inline int32_t fixed2_cross(fixed2_t a, fixed2_t b) {
    return (int32_t)(((int64_t)a.x * b.y - (int64_t)a.y * b.x) >> 16);
}
static inline fixed2_t fixed2_add(fixed2_t a, fixed2_t b) { return (fixed2_t){ a.x + b.x, a.y + b.y }; }
static inline fixed2_t fixed2_sub(fixed2_t a, fixed2_t b) { return (fixed2_t){ a.x - b.x, a.y - b.y }; }
static inline fixed2_t fixed2_scale(fixed2_t a, int32_t k) {
    return (fixed2_t){ fixed_mul32(a.x, k), fixed_mul32(a.y, k) };
}
static inline int32_t fixed2_length(fixed2_t a) { return fixed_hypot32(a.x, a.y); }
static inline int64_t fixed2_length_squared64(fixed2_t a) { /* 32.32 */
    return (int64_t)a.x * a.x + (int64_t)a.y * a.y;
}
/* `v` (whose length is `length` > 0) rescaled to `new_length`, in 64-bit so a
 * short vector keeps its precision. */
static inline fixed2_t fixed2_rescale(fixed2_t v, int32_t length, int32_t new_length) {
    return (fixed2_t){ (int32_t)((int64_t)v.x * new_length / length),
                       (int32_t)((int64_t)v.y * new_length / length) };
}
/* Unit vector; (0,0) stays (0,0). */
static inline fixed2_t fixed2_normalize(fixed2_t a) {
    int32_t len = fixed2_length(a);
    if (!len) return (fixed2_t){ 0, 0 };
    return (fixed2_t){ fixed_div32(a.x, len), fixed_div32(a.y, len) };
}

#endif
