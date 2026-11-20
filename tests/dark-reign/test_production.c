#define OWNER_PRODUCER MT_FG_CONSTRUCTION_CREW
#define PLAYER_PRODUCER MT_FG_CONSTRUCTION_CREW
#define PLAYER_PRODUCT 10001
#define AI_FIRST_UNIT MT_FG_HQ1
#define AI_FIRST_COUNT 1
#define AI_FIRST_MAX 1
/* HQs upgrade in place, and the time that takes delays the tanks. */
#define AI_FIRST_COUNTED(owner) (G_CountPlannedActors(owner, MT_FG_HQ1) + \
    G_CountPlannedActors(owner, MT_FG_HQ2) + G_CountPlannedActors(owner, MT_FG_HQ3))
#define AI_GOAL_SECONDS 900
#define AI_ADVANCED_UNIT MT_FG_MEDIUM_TANK
#define AI_ADVANCED_COUNT 2
#define AI_ADVANCED_MAX 4
#include "../production_regression.h"
