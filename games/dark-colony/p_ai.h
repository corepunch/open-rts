#ifndef __P_AI__
#define __P_AI__

#include "game.h"

typedef struct {
    int elapsed_ms;
    int wave_elapsed_ms;
    uint32_t wave_target_id;
} AiState;

void DC_UpdateAI(AiState *ai, const level_t *map, mobj_t *const *units, int unit_count, int dt_ms);
#endif
