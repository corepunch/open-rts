#ifndef __P_MISSION__
#define __P_MISSION__

#include "game.h"
#include "p_ai.h"
#include "p_script.h"

/* The level owns this aggregate; each subsystem owns its private state. */
typedef struct {
    ScriptState *script;
    AiState ai;
} Mission;

Mission *load_mission(const char *map_path);
void destroy_mission(void *mission);
void update_mission(level_t *map, mobj_t *units, int *unit_count,
                    effect_t *effects, int max_effects,
                    const gameinfo_t *game_info, hudtext_t *hud, float dt);
MissionState mission_get_state(const void *mission);

#endif
