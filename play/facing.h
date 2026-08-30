#ifndef __FACING__
#define __FACING__

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* Canonical facing representation used by simulation and shared engine code.
 * 0 = north, increases clockwise; full circle = 65536 (wraps on uint16 overflow). */
typedef uint16_t facing_t;

/* Per-plugin facing layout: maps the plugin's native sprite index 0 to a
 * compass direction, and describes whether indices increase CW or CCW. */
typedef struct {
    int   count;      /* number of discrete facing directions in this format */
    float first_deg;  /* compass degrees (N=0, E=90, S=180, W=270) of index 0 */
    bool  clockwise;  /* true = index increases clockwise, false = CCW */
} facing_scheme_t;

/* Engine-level schemes — defined in facing.c, used by engine_units.c and plugins. */
extern const facing_scheme_t compass16_facing_scheme; /* 8-way, east=0, CCW, result×2 */
extern const facing_scheme_t dc8_facing_scheme;       /* Dark Colony 8-way, south=0, CCW */
extern const facing_scheme_t dc16_facing_scheme;      /* Dark Colony 16-way, south=0, CCW */
extern const facing_scheme_t dr16_facing_scheme;      /* Dark Reign/KKnD 16-way, north=0, CW */

static inline facing_t facing_from_degrees(float deg) {
    return (facing_t)((deg / 360.0f) * 65536.0f);
}

static inline float facing_to_degrees(facing_t f) {
    return (f / 65536.0f) * 360.0f;
}

/* Convert a movement vector (screen-space: +x=east, +y=south) to a canonical
 * compass facing.  Returns 0 for zero-length vectors. */
static inline facing_t facing_from_vector(float dx, float dy) {
    if (dx * dx + dy * dy < 1e-6f) return 0;
    float rad = atan2f(dx, -dy);  /* north=0, CW */
    if (rad < 0.0f) rad += 6.28318530f;
    return facing_from_degrees(rad * (180.0f / 3.14159265f));
}

/* Convert a canonical facing to a 0-based index into a plugin's sprite table.
 * Returns a value in [0, scheme->count). */
int facing_to_index(facing_t f, const facing_scheme_t *scheme);

#endif
