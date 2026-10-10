#include "starcraft.h"
#include "sc_local.h"
/* Zerg: a Hydralisk needs the den, which needs the pool; melee attacks go one level at a time. */
#define TECH_STARTERS { 0 }
#define TECH_RACE 1
#define TECH_REAL_MAP
#define TECH_MODEL_TICK
#define TECH_OWNER 1
#define TECH_CASES { \
    { 38, 1, { 143 } }, \
    { 39, 2, { 143, 136 } }, \
    { 1042, 3, { 140, 1040, 1041 } }, \
}
#define TECH_AI_CASE 1
#define TECH_AI_TICKS (30 * 60 * 16)
#include "../tech_path_regression.h"
static int tech_setup(RtsGameModel **model) {
    static const char road[] = "maps/(2)road war.scm/staredit/scenario.chk";
    int kinds[8] = {SC_SLOT_HUMAN, SC_SLOT_COMPUTER, SC_SLOT_CLOSED, SC_SLOT_CLOSED,
                    SC_SLOT_CLOSED, SC_SLOT_CLOSED, SC_SLOT_CLOSED, SC_SLOT_CLOSED};
    /* The lobby counts Terran, Zerg, Protoss. */
    int races[8] = {0, TECH_RACE};
    sc_set_custom_slots(kinds, races);
    *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = g_game_default_root, .map_path = road };
    CHECK(*model && rts_game_model_load(*model, &config));
    CHECK(G_AiInterface()->player_level(&level, TECH_OWNER) == AI_LEVEL_NORMAL);
    return 0;
}
