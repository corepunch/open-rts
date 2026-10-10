/* Dark Colony's computer players follow g_ruleset's factions: the Human and
 * Gray openings and doctrine rosters decide every purchase of their owner. */
#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include "t_local.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c, "dark-colony factions", #c)

static bool in_faction(const faction_t *faction, int product) {
    for (int i = 0; i < faction->opening_count; ++i)
        if (faction->opening[i].product == product) return true;
    for (int i = 0; i < faction->doctrine.roster_count; ++i)
        if (faction->doctrine.roster[i].product == product) return true;
    return false;
}

static int play(int race) {
    static const char map[] = "SCENARIO/MPLAYER/D2PLAY01.MAP";
    dc_skirmish_t setup = {.quantity = 4, .flow = 4, .rank = 0};
    for (int i = 0; i < 8; ++i)
        setup.players[i] = (dc_skirmish_player_t){.type = DC_PLAYER_NONE, .color = i, .team = i};
    setup.players[0] = (dc_skirmish_player_t){.type = DC_PLAYER_HUMAN, .race = 0, .color = 0, .team = 0};
    setup.players[1] = (dc_skirmish_player_t){.type = DC_PLAYER_AI, .race = race, .color = 1, .team = 1};
    DC_RequestSkirmish(map, &setup);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = map};
    CHECK(model && rts_game_model_load(model, &config));
    AiContext *ai = rts_game_model_ai(model);
    const faction_t *faction = &g_ruleset.factions[race];
    CHECK(R_OwnerFaction(&level, 1) == faction);
    int purchases = 0, foreign = 0, doctrine_buys = 0;
    for (int t = 0; t < 30 * 60 * 10; ++t) {
        CHECK(rts_tick(model, NULL));
        RtsGameEvent skip;
        while (rts_game_model_poll_event(model, &skip)) {}
        AiEvent event;
        while (P_AiPollEvent(ai, &event)) {
            if (event.type != AI_EVENT_PURCHASE || event.owner != 1) continue;
            ++purchases;
            foreign += !in_faction(faction, event.value);
            for (int i = 0; i < faction->doctrine.roster_count; ++i)
                doctrine_buys += faction->doctrine.roster[i].product == event.value;
        }
    }
    const AiTeamState *team = &ai->teams[1];
    CHECK(team->plan_loaded && team->plan.goal_count == faction->opening_count);
    CHECK(team->plan.doctrine.roster_count == faction->doctrine.roster_count);
    CHECK(purchases >= 20 && foreign == 0 && doctrine_buys > 0);
    printf("  %s: %d purchases, none outside its faction\n", faction->name, purchases);
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    CHECK(g_ruleset.faction_count == 2);
    RTS_RUN(play(0));
    RTS_RUN(play(1));
    puts("PASS: Human and Gray skirmish AIs buy from their own faction's opening and roster");
    return 0;
}
