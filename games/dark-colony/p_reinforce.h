#ifndef __P_REINFORCE__
#define __P_REINFORCE__

#include "game.h"

void DC_SpawnReinforcement(const level_t *map, mobj_t *units, int *unit_count,
                           int team, int gx, int gy, int type,
                           const gameinfo_t *game_info);

#endif
