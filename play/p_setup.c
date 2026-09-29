#include "game.h"
#include <stdlib.h>
#ifdef RTS_GAME_DARK_COLONY
#include "p_path.h"
#endif

void P_FreeLevel(level_t *map) {
    if (map == &level) P_FreeThinkers();
    P_FreeFlowFields(map);
#ifdef RTS_GAME_DARK_COLONY
    DC_FreePaths(map);
#endif
    free(map->tile_ids);
    for (int i = 0; i < MAX_TILE_OVERLAYS; ++i) free(map->tile_overlays[i]);
    for (int i = 0; i < MAX_TILE_OVERLAYS + 1; ++i) free(map->tile_transforms[i]);
    free(map->blocked);
    free(map->tile_flags);
    free(map->sight.cells);
    free(map->cell_colors);
    free(map->decorations);
    free(map->resource_vents);
    free(map->extras);
    if (map->destroy_mission) map->destroy_mission(map->mission);
    if (map->destroy_native_data) map->destroy_native_data(map->native_data);
    memset(map, 0, sizeof(*map));
}
