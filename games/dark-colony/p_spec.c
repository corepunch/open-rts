/* Level-owned mission lifecycle and deterministic subsystem order. */
#include "p_mission.h"
#include <stdlib.h>

Mission *load_mission(const char *map_path) {
    if (!map_path) return NULL;
    Mission *mission = calloc(1, sizeof(*mission));
    if (!mission) return NULL;
    mission->script = DC_LoadScript(map_path);
    if (!mission->script) {
        destroy_mission(mission);
        return NULL;
    }
    return mission;
}

void update_mission(level_t *map, mobj_t *units, int *unit_count,
                    effect_t *effects, int max_effects,
                    const gameinfo_t *game_info, hudtext_t *hud, float dt) {
    (void)effects;
    (void)max_effects;
    Mission *mission = map ? map->mission : NULL;
    if (!mission || !units || !unit_count ||
        mission_get_state(mission) != MISSION_ACTIVE) return;
    DC_UpdateAI(&mission->ai, map, units, *unit_count, (int)(dt * 1000.0f));
    DC_UpdateScript(mission->script, map, units, unit_count,
                    game_info, hud, dt);
}

MissionState mission_get_state(const void *ptr) {
    const Mission *mission = ptr;
    return mission ? DC_ScriptState(mission->script) : MISSION_ACTIVE;
}

void destroy_mission(void *ptr) {
    Mission *mission = ptr;
    if (!mission) return;
    DC_FreeScript(mission->script);
    free(mission);
}
