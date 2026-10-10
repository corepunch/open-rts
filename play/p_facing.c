#include "engine.h"
#include "fixed.h"

enum { SLOPE_RANGE = 2048 };

/* Sine samples for the deterministic facing LUT (see include/fixed.h). */
const int32_t fixed_sin_quarter[1025] = {
#include "fixed_sin.inc"
};

static const angle_t tangent_to_angle[SLOPE_RANGE + 1] = {
#include "p_tantoangle.inc"
};

/* Doom's SlopeDiv: numerator << 3 must not overflow, so callers scale the
 * larger component to 2^24 rather than a full fixed_t range. */
static unsigned slope_div(unsigned numerator, unsigned denominator) {
    if (denominator < 512) return SLOPE_RANGE;
    unsigned result = (numerator << 3) / (denominator >> 8);
    return result <= SLOPE_RANGE ? result : SLOPE_RANGE;
}

/* Core of both entry points: x, y are already normalised so the larger
 * magnitude is 2^24, and y points up (Doom orientation). */
static angle_t angle_from_octant(int32_t x, int32_t y) {
    if (x >= 0) {
        if (y >= 0) {
            if (x > y) return tangent_to_angle[slope_div((unsigned)y, (unsigned)x)];
            return ANG90 - 1 - tangent_to_angle[slope_div((unsigned)x, (unsigned)y)];
        }
        y = -y;
        if (x > y) return 0u - tangent_to_angle[slope_div((unsigned)y, (unsigned)x)];
        return ANG270 + tangent_to_angle[slope_div((unsigned)x, (unsigned)y)];
    }
    x = -x;
    if (y >= 0) {
        if (x > y) return ANG180 - 1 - tangent_to_angle[slope_div((unsigned)y, (unsigned)x)];
        return ANG90 + tangent_to_angle[slope_div((unsigned)x, (unsigned)y)];
    }
    y = -y;
    if (x > y) return ANG180 + tangent_to_angle[slope_div((unsigned)y, (unsigned)x)];
    return ANG270 - 1 - tangent_to_angle[slope_div((unsigned)x, (unsigned)y)];
}

/* Integer-only: the heading of a screen-space vector given in 16.16 (or any
 * common scale). Screen y grows downward, so it is flipped. */
angle_t angle_from_screen_vector_fixed(fixed_t dx, fixed_t dy) {
    int64_t ax = dx < 0 ? -(int64_t)dx : dx, ay = dy < 0 ? -(int64_t)dy : dy;
    int64_t largest = ax > ay ? ax : ay;
    if (!largest) return 0;
    int32_t x = (int32_t)(((int64_t)dx * 16777216) / largest);
    int32_t y = (int32_t)(((int64_t)-dy * 16777216) / largest);
    return angle_from_octant(x, y);
}

/* Unit vector for an angle from the integer sine table: bit-identical on every
 * platform. Screen space, so y is negated. */
void angle_to_screen_vector_fixed(angle_t angle, fixed_t *dx, fixed_t *dy) {
    if (dx) *dx = fixed_cos_bam(angle);
    if (dy) *dy = -fixed_sin_bam(angle);
}

uint32_t angle_distance(angle_t a, angle_t b) {
    uint32_t delta = a - b;
    return delta > ANG180 ? 0u - delta : delta;
}

int angle_to_direction(angle_t angle, int count, angle_t first_angle, bool clockwise) {
    if (count <= 0 || count > 32) return 0;
    uint32_t relative = clockwise ? first_angle - angle : angle - first_angle;
    uint64_t scaled = (uint64_t)relative * (uint32_t)count + UINT64_C(0x80000000);
    return (int)((scaled >> 32) % (uint32_t)count);
}

angle_t direction_to_angle(int direction, int count, angle_t first_angle, bool clockwise) {
    if (count <= 0 || count > 32) return first_angle;
    int slot = direction % count;
    if (slot < 0) slot += count;
    angle_t offset = (angle_t)(((uint64_t)(uint32_t)slot << 32) / (uint32_t)count);
    return clockwise ? first_angle - offset : first_angle + offset;
}
