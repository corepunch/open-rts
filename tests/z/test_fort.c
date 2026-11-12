#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>
#include <stdlib.h>

static int fail(const char *msg) { return rts_fail("z fort", msg); }

enum { CPU = 1, FALL_TICS = 170 * RTS_TICRATE / 70 };

/* No computer player: the tests move every unit themselves. */
static RtsGameModel *load(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/Z" };
    if (model && rts_game_model_load(model, &config)) {
        P_AiSetFeatures(rts_game_model_ai(model), 0);
        return model;
    }
    rts_game_model_destroy(model);
    return NULL;
}

/* The `n`th live thing of `type` (any team when team < 0). */
static mobj_t *nth(int type, int team, int n) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == type && mo->hp > 0 &&
            (team < 0 || mo->team == team) && n-- == 0) return mo;
    }
    return NULL;
}

static int units_of(int team) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        n += th->function == P_MobjThinker && mo->hp > 0 && !mo->remove && Z_UnitType(mo) >= 0 && mo->team == team;
    }
    return n;
}

static bool tick(RtsGameModel *model, int tics) {
    RtsRenderSnapshot snap;
    for (int t = 0; t < tics; ++t)
        if (!rts_tick(model, &snap)) return false;
    return true;
}

/* Nobody fights, so the tests watch only the forts. */
static void disarm(void) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        ((mobj_t *)th)->traits &= ~MF_ATTACK;
}

static void place(mobj_t *mo, fixed2_t at) {
    mo->core.position = fixed3_from_fixed2(at, mo->core.position.z);
    P_ClearMove(mo);
    mo->movement.goal = at;
    mo->movement.order_arrived = true;
}

static bool wrecked(const mobj_t *fort) {
    return fort->core.state_id == S_FORT_WRECK || fort->core.state_id == S_FORT_CPU_WRECK;
}

/* Grenades and shells leave a fort whole, and nobody aims at one. */
static int test_unharmed(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level 1");
    mobj_t *fort = nth(MT_FORT, CPU, 0), *grunt = nth(MT_UNIT_GRUNT, 0, 0);
    if (!fort || !grunt) return fail("the computer's fort and a Grunt");
    grunt->z.grenades = 10;
    if (P_CanTarget(grunt, fort)) return fail("a Grunt with grenades cannot aim at a fort");
    P_DamageMobj(fort, grunt, fort->max_hp - 1);
    if (fort->hp != fort->max_hp) return fail("the fort takes no harm");
    puts("PASS: z forts take no harm");
    rts_game_model_destroy(model);
    return 0;
}

/* The player's Grunt walks into the computer's fort: the fort is a wreck
 * after 17 native steps and the level is won after 170. A Grunt of the
 * fort's own team inside changes nothing. */
static int test_player_takes_fort(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for the computer's fort");
    disarm();
    mobj_t *fort = nth(MT_FORT, CPU, 0), *own = nth(MT_UNIT_GRUNT, CPU, 0), *grunt = nth(MT_UNIT_GRUNT, 0, 0);
    fixed2_t entrance;
    if (!fort || !own || !grunt || !Z_FortEntrance(fort, &entrance)) return fail("a fort with an entrance");
    place(own, entrance);
    if (!tick(model, RTS_TICRATE) || fort->z.hold || G_LevelOutcome()) return fail("its own team leaves it be");
    place(own, fixed2_add(entrance, (fixed2_t){20 * FIXED_ONE, 0}));

    int players = units_of(0);
    /* The cliff detour crosses a territory flag. Keep this test about
     * entering the fort, without stopping to cheer at an incidental capture. */
    for (int i = 0; nth(MT_FLAG, -1, i); ++i) Z_SetTeam(nth(MT_FLAG, -1, i), 0);
    ticcmd_t enter = {.order = TC_ATTACK, .position = fort->core.position, .target = fort->id,
                      .count = 1, .units = {grunt->id}};
    ticcmd_t move = {.order = TC_MOVE, .position = grunt->core.position, .count = 1, .units = {grunt->id}};
    G_RunTiccmd(0, &move);
    fixed3_t saved_position = grunt->core.position;
    grunt->core.position = (fixed3_t){-FIXED_FROM_INT(100), -FIXED_FROM_INT(100), 0};
    G_RunTiccmd(0, &enter);
    if (grunt->z.route.active != TC_MOVE) return fail("unreachable fort preserves existing order");
    grunt->core.position = saved_position;
    G_RunTiccmd(0, &enter);
    if (!P_HasMoveOrder(grunt)) return fail("fort order enters through the native entrance");
    for (int t = 0; t < 120 * RTS_TICRATE && !fort->z.hold; ++t)
        if (!tick(model, 1)) return fail("tick");
    if (!fort->z.hold) return fail("the Grunt walks in and the fort falls");
    if (!tick(model, 17 * RTS_TICRATE / 70) || !wrecked(fort)) return fail("the fort is a wreck");
    if (G_LevelOutcome()) return fail("the level runs on while the fort falls");
    if (!tick(model, FALL_TICS) || G_LevelOutcome() != OUTCOME_VICTORY) return fail("the level is won");
    if (units_of(0) != players) return fail("the winner keeps its units");
    puts("PASS: z the player takes the computer's fort and wins");
    rts_game_model_destroy(model);
    return 0;
}

/* A computer Grunt inside the player's fort: the player's units die while
 * it falls and the level is lost. */
static int test_player_loses_fort(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for the player's fort");
    disarm();
    mobj_t *fort = nth(MT_FORT, 0, 0), *grunt = nth(MT_UNIT_GRUNT, CPU, 0);
    fixed2_t entrance;
    if (!fort || !grunt || !Z_FortEntrance(fort, &entrance)) return fail("the player's fort and a computer Grunt");
    place(grunt, entrance);
    if (!tick(model, 1) || !fort->z.hold) return fail("the fort falls");
    if (!tick(model, FALL_TICS / 2) || units_of(0)) return fail("the player's units die one by one");
    if (!units_of(CPU)) return fail("the computer keeps its units");
    if (!tick(model, FALL_TICS) || G_LevelOutcome() != OUTCOME_DEFEAT) return fail("the level is lost");
    puts("PASS: z the computer takes the player's fort and the player loses");
    rts_game_model_destroy(model);
    return 0;
}

static int test_network_falls(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for network forts");
    mobj_t *first = nth(MT_FORT, 0, 0), *second = nth(MT_FORT, 1, 0);
    mobj_t *own = nth(MT_UNIT_GRUNT, 0, 0), *enemy = nth(MT_UNIT_GRUNT, 1, 0);
    if (!first || !second || !own || !enemy) return fail("network fort fixture");
    mobj_t *third = P_SpawnMobj(first->core.position, MT_FORT);
    mobj_t *attacker1 = P_SpawnMobj(first->core.position, MT_UNIT_GRUNT);
    mobj_t *attacker2 = P_SpawnMobj(second->core.position, MT_UNIT_GRUNT);
    if (!third || !attacker1 || !attacker2) return fail("spawn network sides");
    Z_SetTeam(third, 2); Z_SetTeam(attacker1, 2); Z_SetTeam(attacker2, 2);
    fixed2_t a, b;
    if (!Z_FortEntrance(first, &a) || !Z_FortEntrance(second, &b)) return fail("network entrances");
    place(attacker1, a); place(attacker2, b);
    mobj_t *units[] = {first, second, third, own, enemy, attacker1, attacker2};
    netgame = true;
    G_LevelObjectives(units, 7);
    if (!first->z.hold || !second->z.hold || third->z.hold || own->hp <= 0 || enemy->hp <= 0 || G_LevelOutcome())
        return fail("network fort captures queue concurrent falls before team elimination");
    G_LevelObjectives(units, 7);
    if (own->hp > 0 || enemy->hp > 0 || G_LevelOutcome())
        return fail("network falls eliminate both captured teams on their first update");
    uint32_t ids[] = {first->id, second->id, third->id, attacker1->id, attacker2->id};
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
    if (!G_SaveGameBlob(&save, &bytes, "Concurrent forts", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud)) return fail("save concurrent network fort falls");
    free(save);
    for (int i = 0; i < 5; ++i) {
        units[i] = P_MobjById(ids[i]);
        if (!units[i]) return fail("restore concurrent network fort fixture");
    }
    first = units[0]; second = units[1];
    if (first->z.hold != 2 || second->z.hold != 2) return fail("restore concurrent fall timers");
    for (int i = 0; i < FALL_TICS; ++i) G_LevelObjectives(units, 5);
    if (!wrecked(first) || !wrecked(second) || G_LevelOutcome() != OUTCOME_DEFEAT)
        return fail("network match ends after concurrent falls with one team left");
    netgame = false;
    rts_game_model_destroy(model);
    return 0;
}

static int test_network_continues(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for three network sides");
    mobj_t *first = nth(MT_FORT, 0, 0), *second = nth(MT_FORT, 1, 0);
    mobj_t *victim = nth(MT_UNIT_GRUNT, 0, 0);
    if (!first || !second || !victim) return fail("network continuation fixture");
    mobj_t *third = P_SpawnMobj(first->core.position, MT_FORT);
    mobj_t *taker = P_SpawnMobj(first->core.position, MT_UNIT_GRUNT);
    if (!third || !taker) return fail("spawn third network side");
    Z_SetTeam(third, 2); Z_SetTeam(taker, 2);
    fixed2_t at;
    if (!Z_FortEntrance(first, &at)) return fail("network continuation entrance");
    place(taker, at);
    mobj_t *units[] = {first, second, third, victim, taker};
    int territory = Z_AITerritory(level.native_data, fixed2_from_cell(fixed2_footprint_corner(fixed3_xy(first->core.position), first->info->footprint)));
    mobj_t *flag = NULL;
    for (int i = 0; nth(MT_FLAG, -1, i); ++i)
        if (nth(MT_FLAG, -1, i)->z.territory == territory + 1) flag = nth(MT_FLAG, -1, i);
    if (territory < 0 || !flag) return fail("network fort territory flag");
    netgame = true;
    G_LevelObjectives(units, 5);
    if (flag->team != 2) return fail("first network capture transfers its territory to the taker");
    for (int i = 0; i < FALL_TICS; ++i) G_LevelObjectives(units, 5);
    if (!wrecked(first) || G_LevelOutcome() || !Z_Producer(second) || !Z_Producer(third))
        return fail("network battle continues after one of three forts falls");
    if (!Z_FortEntrance(second, &at)) return fail("second network entrance");
    place(taker, at);
    int old_console = consoleplayer;
    consoleplayer = 2;
    G_LevelObjectives(units, 5);
    for (int i = 0; i < FALL_TICS; ++i) G_LevelObjectives(units, 5);
    if (G_LevelOutcome() != OUTCOME_VICTORY) return fail("last surviving network side wins");
    consoleplayer = old_console;
    netgame = false;
    rts_game_model_destroy(model);
    return 0;
}

static int test_network_cross_capture(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for simultaneous network captures");
    mobj_t *first = nth(MT_FORT, 0, 0), *second = nth(MT_FORT, 1, 0);
    mobj_t *a = nth(MT_UNIT_GRUNT, 0, 0), *b = nth(MT_UNIT_GRUNT, 1, 0);
    fixed2_t entrance_a, entrance_b;
    if (!first || !second || !a || !b || !Z_FortEntrance(first, &entrance_a) || !Z_FortEntrance(second, &entrance_b))
        return fail("cross capture fixture");
    place(a, entrance_b); place(b, entrance_a);
    mobj_t *units[] = {first, second, a, b};
    netgame = true;
    G_LevelObjectives(units, 4);
    if (!first->z.hold || !second->z.hold || a->hp <= 0 || b->hp <= 0)
        return fail("both opposing captures are queued before either team is eliminated");
    G_LevelObjectives(units, 4);
    if (a->hp > 0 || b->hp > 0) return fail("cross capture eliminates both teams on update");
    for (int i = 0; i < FALL_TICS; ++i) G_LevelObjectives(units, 4);
    if (G_LevelOutcome() != OUTCOME_DRAW) return fail("cross capture with no survivor is a draw");
    netgame = false;
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    if (test_unharmed() || test_player_takes_fort() || test_player_loses_fort() || test_network_falls() || test_network_continues() || test_network_cross_capture()) return 1;
    return 0;
}
