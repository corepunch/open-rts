#ifndef __P_DROP__
#define __P_DROP__

#include "game.h"

enum { DROPSHIP_MAX_PAYLOAD_TYPES = 5 };
typedef struct { int type; int count; } DropshipPayload;
typedef struct DropshipSystem DropshipSystem;

DropshipSystem *DC_LoadDropships(const char *map_path);
void DC_FreeDropships(DropshipSystem *ships);
bool DC_StartDropship(level_t *map, effect_t *effects, int max_effects,
                      int team, ivec2_t origin,
                      const DropshipPayload *payload, int payload_count);
void DC_UpdateDropships(level_t *map, mobj_t *units, int *unit_count,
                        effect_t *effects, int max_effects,
                        const gameinfo_t *game_info, float dt);
#endif
