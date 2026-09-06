#ifndef __DC_FACING__
#define __DC_FACING__

#include "facing.h"

static inline angle_t dc_direction_to_angle(int direction) {
    return direction_to_angle(direction, 16, ANG270, false);
}

static inline angle_t dc_fin_direction_to_angle(int direction) {
    return direction_to_angle(direction, 16, ANG270, true);
}

static inline int dc_angle_to_direction(angle_t angle) {
    return angle_to_direction(angle, 16, ANG270, false);
}

#endif