#include "dark-colony.h"
/* Humans: the Barrager waits on the second-tier Robo-Ftr+ and Sci-Pod+, which each need the Sci-Pod and
 * Barracks; a second weapon level needs the first and the better Sci-Pod. */
#define TECH_STARTERS { 0 }
#define TECH_RACE 0
#define TECH_REAL_MAP
#define TECH_MODEL_TICK
#define TECH_CASES { \
    { 89, 1, { 80 } }, \
    { 93, 5, { 81, 80, 82, 86, 85 } }, \
    { 111, 4, { 80, 81, 110, 85 } }, \
}
#define TECH_AI_CASE 1
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
