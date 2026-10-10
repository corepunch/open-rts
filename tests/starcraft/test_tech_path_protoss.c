#include "starcraft.h"
#include "sc_local.h"
/* Protoss: a Carrier needs the Fleet Beacon over a Stargate over the Cybernetics Core; an Arbiter the whole
 * Templar tree and a Stargate. */
#define TECH_STARTERS { 0 }
#define TECH_RACE 2
#define TECH_REAL_MAP
#define TECH_MODEL_TICK
#define TECH_OWNER 1
#define TECH_CASES { \
    { 66, 1, { 161 } }, \
    { 73, 4, { 161, 165, 168, 170 } }, \
    { 72, 6, { 161, 165, 164, 166, 171, 168 } }, \
}
#define TECH_AI_CASE 1
#define TECH_AI_PRELUDE { 157, 1 },
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
