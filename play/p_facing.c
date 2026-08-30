#include "facing.h"

/* Compass convention throughout: N=0°, E=90°, S=180°, W=270°, CW.
 *
 * compass16: 8 actual directions starting from east, CCW.  The caller
 * multiplies the result by 2 to produce the 0/2/4…14 codes that
 * Dark-Colony-style states[] direction_codes arrays use.
 *
 * dc8/dc16: south=index 0, indices increase CCW (toward east, then north).
 * This matches the atan2(dx,dy) convention used in the original per-mode
 * functions that these schemes replace.
 *
 * dr16: north=index 0, indices increase CW.  Used by Dark Reign, KKnD MOBD,
 * and 7th Legion — all formats where facing 0 faces the top of the screen
 * and higher indices rotate clockwise. */
const facing_scheme_t compass16_facing_scheme = { 8,  90.0f, false };
const facing_scheme_t dc8_facing_scheme       = { 8,  180.0f, false };
const facing_scheme_t dc16_facing_scheme      = { 16, 180.0f, false };
const facing_scheme_t dr16_facing_scheme      = { 16, 0.0f,   true  };

int facing_to_index(facing_t f, const facing_scheme_t *scheme) {
    float deg  = facing_to_degrees(f);
    float rel  = scheme->clockwise ? (deg - scheme->first_deg)
                                   : (scheme->first_deg - deg);
    float step = 360.0f / (float)scheme->count;
    /* Add step/2 so we round to nearest bucket rather than truncate, then
     * add 360 to keep fmodf's input positive before normalising. */
    int index  = (int)floorf(fmodf(rel + step * 0.5f + 360.0f, 360.0f) / step);
    return index % scheme->count;
}
