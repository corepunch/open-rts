#include "facing.h"

enum { SLOPE_RANGE = 2048 };

static const angle_t tangent_to_angle[SLOPE_RANGE + 1] = {
#include "p_tantoangle.inc"
};

static unsigned slope_div(unsigned numerator, unsigned denominator) {
    if (denominator < 512) return SLOPE_RANGE;
    unsigned result = (numerator << 3) / (denominator >> 8);
    return result <= SLOPE_RANGE ? result : SLOPE_RANGE;
}

angle_t angle_from_screen_vector(float dx, float dy) {
    if (dx * dx + dy * dy < 1e-6f) return 0;

    double largest = fmax(fabs((double)dx), fabs((double)dy));
    double scale = largest > 0.0 ? 1073741824.0 / largest : 0.0;
    int32_t x = (int32_t)((double)dx * scale);
    int32_t y = (int32_t)(-(double)dy * scale);

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

void angle_to_screen_vector(angle_t angle, float *dx, float *dy) {
    if (!dx || !dy) return;
    double radians = (double)angle * (6.28318530717958647692 / 4294967296.0);
    *dx = (float)cos(radians);
    *dy = (float)-sin(radians);
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
