#ifndef __FACING__
#define __FACING__

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* Hexen/Doom binary angle measurement: east=0, north=ANG90, increasing CCW. */
typedef uint32_t angle_t;

#define ANG45  UINT32_C(0x20000000)
#define ANG90  UINT32_C(0x40000000)
#define ANG180 UINT32_C(0x80000000)
#define ANG270 UINT32_C(0xc0000000)
#define ANGLE_MAX UINT32_MAX

/* Movement vectors use screen coordinates: +x=east, +y=south. */
angle_t angle_from_screen_vector(float dx, float dy);
void angle_to_screen_vector(angle_t angle, float *dx, float *dy);
uint32_t angle_distance(angle_t a, angle_t b);

/* Quantize a BAM angle into an authored rotation table.  first_angle is the
 * direction represented by slot zero; count may be up to 32. */
int angle_to_direction(angle_t angle, int count, angle_t first_angle, bool clockwise);
angle_t direction_to_angle(int direction, int count, angle_t first_angle, bool clockwise);

#endif
