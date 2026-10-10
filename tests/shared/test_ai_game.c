#include "engine.h"
#include "t_local.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, g_game_id, #c)

/* Plays ten simulated minutes of the game's default map with its real computer
 * players (owner 0 is the human) and checks what the shared AI did. KKnD's
 * first mission gives the enemy loose units and no buildings, so its AI has
 * nothing to build from; tests/kknd/test_ai.c covers it with a base. */
int main(void) {
    if (!strcmp(g_game_id, "dark-colony")) return 0; /* Own skirmish suite. */
    bool kknd = !strcmp(g_game_id, "kknd");
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = g_game_default_root };
    if (!strcmp(g_game_id, "dark-reign")) config.map_path = "scenario/MULTI/2NIC/2NIC.SCN";
    CHECK(rts_game_model_load(model, &config));
    AiContext *ai = rts_game_model_ai(model);
    CHECK(ai && ai->game && G_AiInterface());
    /* The human watches consoleplayer (Warcraft's ALAMO puts it in slot 1);
     * the enemy judged is the computer slot that bought the most, since a
     * crowded map (ALAMO seats six) starves some of its players. */
    int human_owner = consoleplayer;
    int human_credits = level.player_resources[human_owner][0];
    for (int t = 0; t < 30 * 60 * 10; ++t) CHECK(rts_game_model_tick(model, RTS_TICK_MS));
    const AiStats *human = P_AiStats(ai, human_owner), *enemy = NULL;
    int enemy_owner = -1;
    for (int owner = 0; owner < AI_MAX_TEAMS; ++owner) {
        const AiStats *stats = P_AiStats(ai, owner);
        if (owner != human_owner && stats->thinks > 0 && (!enemy || stats->purchases > enemy->purchases)) {
            enemy = stats;
            enemy_owner = owner;
        }
    }
    CHECK(human->thinks == 0 && human->purchases == 0);
    CHECK(level.player_resources[human_owner][0] >= human_credits);
    CHECK(enemy && enemy->thinks > 0);
    if (kknd) {
        CHECK(enemy->purchases == 0 && enemy->waves == 0); /* No producer, no structure. */
    } else {
        CHECK(enemy->purchases >= 10);
        /* Every game runs its computer player by doctrine after the opening. */
        CHECK(ai->teams[enemy_owner].plan.doctrine.roster_count > 0);
        CHECK(enemy->waves >= 1 && enemy->wave_units >= enemy->waves);
    }
    if (!strcmp(g_game_id, "dark-reign")) CHECK(enemy->harvest_orders >= 1);
    printf("PASS: %s computer player ran 10 minutes (buys %d, waves %d, harvest %d)\n",
           g_game_id, enemy->purchases, enemy->waves, enemy->harvest_orders);
    rts_game_model_destroy(model);
    return 0;
}
