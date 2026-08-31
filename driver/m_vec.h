#ifndef M_VEC_H
#define M_VEC_H

#include <SDL.h>
#include <stdbool.h>

typedef struct { int x, y; }   ivec2_t;
typedef struct { float x, y; } fvec2_t;
typedef struct { int w, h; }   isize2_t;

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
