#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>

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
    if (!P_MoveUnitTo(&level, grunt, entrance)) return fail("a path into the fort");
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

int main(void) {
    if (test_unharmed() || test_player_takes_fort() || test_player_loses_fort()) return 1;
    return 0;
}
