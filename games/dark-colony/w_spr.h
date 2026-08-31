#ifndef __W_SPR__
#define __W_SPR__
#include <stdbool.h>
typedef struct {
    bool valid;
    int glow_left;
    int glow_top;
} DarkColonyVentPlacement;
bool dark_colony_vent_placement_from_sprites(const char *map_path, DarkColonyVentPlacement *out);
#endif
