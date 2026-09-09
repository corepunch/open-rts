#ifndef __P_SCRIPT__
#define __P_SCRIPT__

#include "game.h"

typedef enum {
    MISSION_ACTIVE, MISSION_WON, MISSION_LOST, MISSION_ALLY_LOST,
} MissionState;
typedef struct ScriptState ScriptState;

ScriptState *DC_LoadScript(const char *map_path);
void DC_FreeScript(ScriptState *script);
MissionState DC_ScriptState(const ScriptState *script);
void DC_UpdateScript(ScriptState *mission, level_t *map, mobj_t *const *units,
                     int *unit_count, hudtext_t *hud, float dt);

#endif
