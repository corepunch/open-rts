#include "dark-colony.h"
/* Grays: the Zisp needs a Pod-Upgrd, the War. Fold and the Neur-Hive (which needs the upgrade too)
 * before a Gene-Sac to make it. */
#define TECH_STARTERS { 0 }
#define TECH_RACE 1
#define TECH_REAL_MAP
#define TECH_MODEL_TICK
#define TECH_CASES { \
    { 48, 1, { 41 } }, \
    { 51, 5, { 42, 41, 43, 98, 97 } }, \
    { 134, 5, { 42, 97, 41, 44, 43 } }, \
}
#define TECH_AI_CASE 2
#define TECH_AI_TICKS (30 * 60 * 14)
#include "../tech_path_regression.h"
static int tech_setup(RtsGameModel **model) {
    static const char map[] = "SCENARIO/MPLAYER/D2PLAY01.MAP";
    dc_skirmish_t setup = {.quantity = 4, .flow = 4, .rank = 0};
    for (int i = 0; i < 8; ++i)
        setup.players[i] = (dc_skirmish_player_t){.type = DC_PLAYER_NONE, .color = i, .team = i};
    setup.players[0] = (dc_skirmish_player_t){.type = DC_PLAYER_HUMAN, .race = 0, .color = 0, .team = 0};
    setup.players[1] = (dc_skirmish_player_t){.type = DC_PLAYER_AI, .race = TECH_RACE, .color = 1, .team = 1};
    DC_RequestSkirmish(map, &setup);
    *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = map};
    CHECK(*model && rts_game_model_load(*model, &config));
    return 0;
}
