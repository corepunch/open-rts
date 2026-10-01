/* Dark Colony base income, independent of networking: SCN load gives each
 * team 3 exo credits (DC.EXE 0x41ae71); the world ticker adds them every
 * sixteen 66 ms native ticks while the team's city slot 0 (Exo-Ctr /
 * Mind-Hive) stands (0x418c52..0x418c9a); trigger action 12, `exomoney`,
 * replaces the amount (0x43a218). */
#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include "t_local.h"
#include <stdio.h>

#define REQUIRE(cond, msg) do { if (!(cond)) return rts_fail("exo_income", msg); } while (0)
#define TICKS_PER_SECOND 30

/* Native ticks elapsed after `tics` engine tics (p_tick.c cumulative clock). */
static int native_clock(int tics) { return (int)((int64_t)tics * 1000 / (66 * 30)); }

static RtsGameModel *skirmish(const int races[2]) {
    const char *map = "SCENARIO/MPLAYER/D2PLAY01.MAP";
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = map};
    dc_skirmish_t setup = {.quantity = 4, .flow = 4};
    for (int i = 0; i < 8; ++i)
        setup.players[i] = (dc_skirmish_player_t){.type = i < 2 ? DC_PLAYER_HUMAN : DC_PLAYER_NONE,
                                                  .race = i < 2 ? races[i] : 0, .color = i, .team = i};
    DC_RequestSkirmish(map, &setup);
    if (!model || !rts_game_model_load(model, &config)) return NULL;
    return model;
}

static mobj_t *base_of(int owner) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *m = (mobj_t *)th;
        if (m->owner == owner && !m->remove && m->hp > 0 &&
            (m->type_id == MT_EXCOPOD || m->type_id == MT_ALIEN_MINDHIVE)) return m;
    }
    return NULL;
}

static int test_trickle(const int races[2]) {
    RtsGameModel *model = skirmish(races);
    REQUIRE(model, "skirmish loads");
    for (int p = 0; p < 8; ++p) REQUIRE(level.exo_income[p] == 3, "every team starts with 3 exo credits");
    int start[2] = {level.player_resources[0][0], level.player_resources[1][0]};
    REQUIRE(start[0] == 1500 && start[1] == 1500, "1500 starting credits");

    int tics = 0, first = -1;
    for (; tics < 60 * TICKS_PER_SECOND; ++tics) {
        REQUIRE(rts_tick(model, NULL), "simulation runs");
        if (first < 0 && level.player_resources[0][0] != start[0]) first = tics + 1;
    }
    int expected = 3 * (native_clock(tics) / 16);
    for (int p = 0; p < 2; ++p)
        REQUIRE(level.player_resources[p][0] == start[p] + expected,
                "both races earn exactly 3 credits per sixteen native ticks");
    REQUIRE(expected >= 165 && expected <= 171, "about 3 credits every second (16 x 66 ms)");
    REQUIRE(first >= 31 && first <= 33, "the first credit arrives after ~1.06 s, not at load");

    /* Losing the base stops the trickle; the other player keeps earning. */
    mobj_t *base = base_of(1);
    REQUIRE(base, "player 1 owns a base");
    P_RemoveMobj(base);
    int lost = level.player_resources[1][0], kept = level.player_resources[0][0];
    for (int t = 0; t < 10 * TICKS_PER_SECOND; ++t) REQUIRE(rts_tick(model, NULL), "simulation runs");
    REQUIRE(level.player_resources[1][0] == lost, "no exo income without the Exo-Ctr / Mind-Hive");
    REQUIRE(level.player_resources[0][0] > kept, "the player with a base keeps earning");
    rts_game_model_destroy(model);
    return 0;
}

static int test_exomoney_script(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = "SCENARIO/HUMAN/HUMAN01.MAP"};
    REQUIRE(model && rts_game_model_load(model, &config), "HUMAN01 loads");
    REQUIRE(level.exo_income[0] == 3, "load default precedes the mission script");
    for (int t = 0; t < 5 * TICKS_PER_SECOND; ++t) REQUIRE(rts_tick(model, NULL), "simulation runs");
    for (int p = 0; p < 5; ++p)
        REQUIRE(level.exo_income[p] == 0, "HUMAN01.TRO `exomoney <team> 0` turns the trickle off");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    static const int combos[3][2] = {{0, 1}, {1, 0}, {1, 1}};
    for (int c = 0; c < 3; ++c) RTS_RUN(test_trickle(combos[c]));
    RTS_RUN(test_exomoney_script());
    puts("PASS: 3 exo credits per sixteen native ticks while the base stands; exomoney overrides it");
    return 0;
}
