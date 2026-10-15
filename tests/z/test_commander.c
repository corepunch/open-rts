#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>

static int fail(const char *msg) { return rts_fail("z commander", msg); }

enum { CPU = 1 };

static RtsGameModel *load(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/Z" };
    if (model && rts_game_model_load(model, &config)) return model;
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

static int flags_of(int team) {
    int n = 0;
    while (nth(MT_FLAG, team, n)) n++;
    return n;
}

static bool tick(RtsGameModel *model, int tics) {
    RtsRenderSnapshot snap;
    for (int t = 0; t < tics; ++t)
        if (!rts_tick(model, &snap)) return false;
    return true;
}

/* Nobody fights, so the tests watch only what the commander does. */
static void disarm(void) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        ((mobj_t *)th)->traits &= ~MF_ATTACK;
}

/* Left alone, the computer's squads walk out and take every flag, the
 * player's included; nobody of the player's is sent anywhere. */
static int test_takes_flags(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level 1");
    disarm();
    int flags = flags_of(-1), before = flags_of(CPU);
    if (before >= flags || !flags_of(0)) return fail("flags to take, one of them the player's");
    if (!tick(model, 5 * RTS_TICRATE)) return fail("tick");
    for (int i = 0; nth(MT_UNIT_GRUNT, CPU, i); ++i)
        if (!P_HasMoveOrder(nth(MT_UNIT_GRUNT, CPU, i))) return fail("every computer Grunt is on its way");
    for (int i = 0; nth(MT_UNIT_GRUNT, 0, i); ++i)
        if (P_HasMoveOrder(nth(MT_UNIT_GRUNT, 0, i))) return fail("the player's Grunts wait for orders");
    if (!tick(model, 85 * RTS_TICRATE)) return fail("tick");
    if (flags_of(CPU) != flags) return fail("the computer holds every flag");
    printf("PASS: z the computer takes all %d flags (held %d)\n", flags, before);
    rts_game_model_destroy(model);
    return 0;
}

/* With every flag its own, the computer walks into the player's fort,
 * which falls, and the player loses. */
static int test_takes_fort(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for a fort");
    disarm();
    for (int i = 0; nth(MT_FLAG, -1, i); ++i) Z_SetTeam(nth(MT_FLAG, -1, i), CPU);
    mobj_t *fort = nth(MT_FORT, 0, 0);
    if (!fort) return fail("the player's fort");
    for (int t = 0; t < 180 * RTS_TICRATE && !fort->z.hold; t += RTS_TICRATE)
        if (!tick(model, RTS_TICRATE)) return fail("tick");
    if (!fort->z.hold) return fail("a computer unit gets inside the player's fort");
    if (!tick(model, 6 * RTS_TICRATE) || G_LevelOutcome() != OUTCOME_DEFEAT) return fail("the player loses");
    puts("PASS: z the computer walks into the player's fort and wins");
    rts_game_model_destroy(model);
    return 0;
}

/* A squad on its way to a flag turns as one on whoever shoots a member. */
static int test_rally(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for a rally");
    mobj_t *mark = nth(MT_UNIT_GRUNT, CPU, 0), *enemy = nth(MT_UNIT_GRUNT, 0, 0);
    if (!mark || !enemy || !mark->z.squad) return fail("a computer squad and a player's Grunt");
    int squad = mark->z.squad;
    disarm();
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->z.squad == squad && mo->team == CPU && mo->info) mo->traits |= mo->info->traits & MF_ATTACK;
    }
    if (!tick(model, RTS_TICRATE)) return fail("tick");
    if (!P_HasMoveOrder(mark)) return fail("the squad is on its way");
    enemy->hp = enemy->max_hp = 10000;
    P_DamageMobj(mark, enemy, 1);
    if (!tick(model, RTS_TICRATE / 2)) return fail("tick");
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->hp > 0 && mo->z.squad == squad && mo->team == CPU &&
            mo->attack.target != enemy) return fail("the whole squad turns on the shooter");
    }
    puts("PASS: z a marching squad turns on whoever shoots one of it");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    if (test_takes_flags() || test_takes_fort() || test_rally()) return 1;
    return 0;
}
