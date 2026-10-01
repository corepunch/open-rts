#include "engine.h"
#include "t_local.h"
#include <math.h>
#define CHECK(c) RTS_CHECK(c, "engine facing", #c)

/* Reference BAM angle for a screen vector: east=0, increasing CCW. */
static angle_t reference_angle(double dx, double dy) {
    double turns = atan2(-dy, dx) / (2.0 * M_PI);
    if (turns < 0.0) turns += 1.0;
    return (angle_t)(uint64_t)llround(turns * 4294967296.0);
}

int main(void) {
    /* Doom's SlopeDiv shifts the numerator left by three; scaling the larger
     * component to 2^30 overflowed for every ratio in [0.5, 1) and mapped an
     * exact diagonal to ANG90 - 1. Units then jittered between headings. */
    const angle_t tolerance = ANG45 / 256u; /* 0.18 degrees */
    for (int tenth = 0; tenth < 3600; ++tenth) {
        double radians = tenth * (M_PI / 1800.0);
        for (int scale = 0; scale < 3; ++scale) {
            double length = scale == 0 ? 0.01 : scale == 1 ? 1.0 : 5000.0;
            float dx = (float)(cos(radians) * length), dy = (float)(-sin(radians) * length);
            angle_t got = angle_from_screen_vector(dx, dy);
            angle_t want = reference_angle(dx, dy);
            if (angle_distance(got, want) > tolerance) {
                fprintf(stderr, "%.1f deg x%g: got %08x want %08x\n", tenth / 10.0, length,
                        got, want);
                return rts_fail("engine facing", "angle_from_screen_vector accuracy");
            }
        }
    }
    /* Exact diagonals and cardinals are the eight route steps of Dark Colony. */
    CHECK(angle_distance(angle_from_screen_vector(1, -1), ANG45) <= 1);
    CHECK(angle_distance(angle_from_screen_vector(-1, -1), ANG90 + ANG45) <= 1);
    CHECK(angle_distance(angle_from_screen_vector(-1, 1), ANG180 + ANG45) <= 1);
    CHECK(angle_distance(angle_from_screen_vector(1, 1), ANG270 + ANG45) <= 1);
    CHECK(angle_from_screen_vector(1, 0) == 0);
    CHECK(angle_from_screen_vector(0, -1) == ANG90 - 1 || angle_from_screen_vector(0, -1) == ANG90);
    CHECK(angle_distance(angle_from_screen_vector(-1, 0), ANG180) <= 1);
    CHECK(angle_distance(angle_from_screen_vector(0, 1), ANG270) <= 1);
    CHECK(angle_from_screen_vector(0, 0) == 0);
    /* A heading must be stable under sub-cell rounding of an equal-axis step. */
    angle_t base = angle_from_screen_vector(0.02f, -0.02f);
    CHECK(angle_distance(base, ANG45) <= tolerance);
    CHECK(angle_distance(angle_from_screen_vector(0.02f, -0.0200001f), base) <= tolerance);
    CHECK(angle_distance(angle_from_screen_vector(0.0200001f, -0.02f), base) <= tolerance);
    /* Monotonic CCW sweep in the previously broken 26.5..45 degree band. */
    angle_t previous = angle_from_screen_vector(1.0f, -0.5f);
    for (int i = 1; i <= 500; ++i) {
        angle_t next = angle_from_screen_vector(1.0f, -(0.5f + i * 0.001f));
        CHECK(next >= previous);
        previous = next;
    }
    /* Direction quantization round-trips the sixteen authored facings. */
    for (int direction = 0; direction < 16; ++direction) {
        angle_t angle = direction_to_angle(direction, 16, ANG90, true);
        float dx, dy;
        angle_to_screen_vector(angle, &dx, &dy);
        CHECK(angle_to_direction(angle_from_screen_vector(dx, dy), 16, ANG90, true) == direction);
    }
    puts("PASS: engine facing angles match atan2 in every octant, on exact diagonals and under rounding");
    return 0;
}
