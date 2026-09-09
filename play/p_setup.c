#include "game.h"
#include <stdlib.h>

void P_FreeLevel(level_t *map) {
    if (map == &level) P_FreeThinkers();
    P_FreeFlowFields(map);
    free(map->tile_ids);
    for (int i = 0; i < MAX_TILE_OVERLAYS; ++i) free(map->tile_overlays[i]);
    for (int i = 0; i < MAX_TILE_OVERLAYS + 1; ++i) free(map->tile_transforms[i]);
    free(map->blocked);
    free(map->cell_colors);
    free(map->decorations);
    free(map->resource_vents);
    free(map->extras);
    if (map->destroy_mission) map->destroy_mission(map->mission);
    if (map->destroy_native_data) map->destroy_native_data(map->native_data);
    memset(map, 0, sizeof(*map));
}
