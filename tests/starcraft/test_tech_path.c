#include "starcraft.h"
#include "sc_local.h"
/* Terran: a Battlecruiser needs a Starport and a Science Facility, each above the Factory; the third level of
 * infantry weapons the second level, which wants the Armory. */
#define TECH_STARTERS { 0 }
#define TECH_RACE 0
#define TECH_REAL_MAP
#define TECH_MODEL_TICK
#define TECH_OWNER 1
#define TECH_CASES { \
    { 1, 1, { 112 } }, \
    { 13, 4, { 112, 114, 115, 117 } }, \
    { 1030, 6, { 123, 1028, 112, 114, 124, 1029 } }, \
}
#define TECH_AI_CASE 1
#define TECH_AI_PRELUDE { 110, 1 },
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
