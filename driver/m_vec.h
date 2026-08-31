#ifndef M_VEC_H
#define M_VEC_H

#include <SDL.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct { int x, y; }   ivec2_t;
typedef struct { float x, y; } fvec2_t;
typedef struct { int w, h; }   isize2_t;

typedef int32_t fixed_t;
typedef struct { fixed_t x, y, z; } fixedvec3_t;

enum {
    FIXED_FRAC_BITS = 16,
    FIXED_ONE = 1 << FIXED_FRAC_BITS,
};

/* Engine-facing rectangle names with exact SDL ABI compatibility. */
typedef SDL_Rect  irect_t;
typedef SDL_FRect frect_t;

static inline fvec2_t fvec2_add(fvec2_t a, fvec2_t b) { return (fvec2_t){ a.x + b.x, a.y + b.y }; }
static inline fvec2_t fvec2_sub(fvec2_t a, fvec2_t b) { return (fvec2_t){ a.x - b.x, a.y - b.y }; }
static inline fvec2_t fvec2_scale(fvec2_t value, float scale) {
    return (fvec2_t){ value.x * scale, value.y * scale };
}
static inline float fvec2_length_squared(fvec2_t value) {
    return value.x * value.x + value.y * value.y;
}
static inline float fvec2_distance_squared(fvec2_t a, fvec2_t b) {
    return fvec2_length_squared(fvec2_sub(a, b));
}
static inline bool fvec2_near(fvec2_t a, fvec2_t b, float epsilon) {
    fvec2_t delta = fvec2_sub(a, b);
    return delta.x > -epsilon && delta.x < epsilon &&
           delta.y > -epsilon && delta.y < epsilon;
}
static inline fvec2_t fvec2_cell_center(ivec2_t cell) {
    return (fvec2_t){ (float)cell.x + 0.5f, (float)cell.y + 0.5f };
}
static inline bool ivec2_equal(ivec2_t a, ivec2_t b) {
    return a.x == b.x && a.y == b.y;
}

static inline fixed_t fixed_from_float(float value) {
    double scaled = (double)value * (double)FIXED_ONE;
    if (isnan(scaled)) return 0;
    if (scaled >= (double)INT32_MAX) return INT32_MAX;
    if (scaled <= (double)INT32_MIN) return INT32_MIN;
    return (fixed_t)llround(scaled);
}

static inline float fixed_to_float(fixed_t value) {
    return (float)value / (float)FIXED_ONE;
}

static inline fixed_t fixed_add_saturated(fixed_t a, fixed_t b) {
    int64_t sum = (int64_t)a + (int64_t)b;
    if (sum > INT32_MAX) return INT32_MAX;
    if (sum < INT32_MIN) return INT32_MIN;
    return (fixed_t)sum;
}

static inline fixed_t fixed_sub_saturated(fixed_t a, fixed_t b) {
    int64_t difference = (int64_t)a - (int64_t)b;
    if (difference > INT32_MAX) return INT32_MAX;
    if (difference < INT32_MIN) return INT32_MIN;
    return (fixed_t)difference;
}

static inline fixedvec3_t fixedvec3_from_fvec2(fvec2_t value, fixed_t z) {
    return (fixedvec3_t){ fixed_from_float(value.x), fixed_from_float(value.y), z };
}

static inline fvec2_t fixedvec3_xy_to_fvec2(fixedvec3_t value) {
    return (fvec2_t){ fixed_to_float(value.x), fixed_to_float(value.y) };
}

static inline fixedvec3_t fixedvec3_add(fixedvec3_t a, fixedvec3_t b) {
    return (fixedvec3_t){
        fixed_add_saturated(a.x, b.x),
        fixed_add_saturated(a.y, b.y),
        fixed_add_saturated(a.z, b.z),
    };
}

static inline fixedvec3_t fixedvec3_zero(void) {
    return (fixedvec3_t){ 0, 0, 0 };
}

static inline fixedvec3_t fixedvec3_planar_delta(fvec2_t delta) {
    return fixedvec3_from_fvec2(delta, 0);
}

static inline fixedvec3_t fixedvec3_add_planar(fixedvec3_t position,
                                                fixedvec3_t displacement) {
    return (fixedvec3_t){
        fixed_add_saturated(position.x, displacement.x),
        fixed_add_saturated(position.y, displacement.y),
        position.z,
    };
}

static inline fixedvec3_t fixedvec3_with_xy(fixedvec3_t value, fvec2_t xy) {
    return (fixedvec3_t){ fixed_from_float(xy.x), fixed_from_float(xy.y), value.z };
}

static inline fixedvec3_t fixedvec3_planar_displacement(fixedvec3_t from,
                                                         fixedvec3_t to) {
    return (fixedvec3_t){
        fixed_sub_saturated(to.x, from.x),
        fixed_sub_saturated(to.y, from.y),
        0,
    };
}

static inline irect_t irect_from_points(ivec2_t a, ivec2_t b) {
    return (irect_t){
        a.x < b.x ? a.x : b.x,
        a.y < b.y ? a.y : b.y,
        a.x < b.x ? b.x - a.x : a.x - b.x,
        a.y < b.y ? b.y - a.y : a.y - b.y,
    };
}

static inline bool irect_contains(irect_t rect, ivec2_t point) {
    return point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.w && point.y < rect.y + rect.h;
}

static inline bool irect_intersects(irect_t a, irect_t b) {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

static inline frect_t frect_from_points(fvec2_t a, fvec2_t b) {
    return (frect_t){
        a.x < b.x ? a.x : b.x,
        a.y < b.y ? a.y : b.y,
        a.x < b.x ? b.x - a.x : a.x - b.x,
        a.y < b.y ? b.y - a.y : a.y - b.y,
    };
}

static inline bool frect_contains(frect_t rect, fvec2_t point) {
    return point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.w && point.y < rect.y + rect.h;
}

static inline bool frect_intersects(frect_t a, frect_t b) {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

#endif
