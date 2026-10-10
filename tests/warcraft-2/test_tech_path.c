#include "engine.h"
#define TECH_STARTERS { 0 }
#define TECH_REAL_MAP
#define TECH_MODEL_TICK
/* The orc computer players of ALAMO (slots 2 to 7) start with a great hall and peons. The
 * Ogre needs a stronghold (a barracks first), the mound that it unlocks and
 * a blacksmith; Light Axes is the Berserker research, which wants a stronghold
 * and a lumber mill; the Dragon waits on a fortress and its roost. */
#define TECH_OWNER 3
#define TECH_CASES { \
    { 2, 1, { 30 } }, \
    { 10, 4, { 30, 25, 48, 36 } }, \
    { 244, 7, { 30, 25, 48, 36, 34, 26, 54 } }, \
    { 130, 4, { 30, 25, 34, 126 } }, \
}
#define TECH_AI_TICKS (30 * 60 * 12)
#include "../tech_path_regression.h"
static int tech_setup(RtsGameModel **model) {
    *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = g_game_default_root, .map_path = "ALAMO.PUD" };
    CHECK(*model && rts_game_model_load(*model, &config));
    return 0;
}
