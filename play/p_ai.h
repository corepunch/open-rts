#ifndef __P_AI__
#define __P_AI__

#include "actor.h"
#include "map.h"

#define AI_MAX_TEAMS 4
#define AI_MAX_HARVEST_ASSIGNMENTS 32
#define AI_DEFENSE_RADIUS 15.0f
#define AI_ATTACK_WAVE_INTERVAL_MS 30000
#define AI_ATTACK_WAVE_MIN_SIZE 3
#define AI_ATTACK_WAVE_MAX_SIZE 8

typedef struct {
    int vent_index;
    int slug_unit_index;
} AiHarvestAssignment;

typedef struct {
    fvec2_t base_position;
    bool has_base;
    int combat_unit_count;
    int harvester_count;
    AiHarvestAssignment harvest_assignments[AI_MAX_HARVEST_ASSIGNMENTS];
    int harvest_assignment_count;
    int attack_wave_timer_ms;
    int attack_wave_size;
    bool attack_wave_active;
} AiTeamState;

typedef struct {
    AiTeamState teams[AI_MAX_TEAMS];
    bool initialized;
} AiContext;

void P_AiInit(AiContext *ctx);
void P_AiTick(AiContext *ctx, level_t *map, mobj_t *units, int unit_count,
              const gameinfo_t *game_info, int dt_ms);

#endif
