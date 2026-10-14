#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>
#include <stdlib.h>

static int fail(const char *msg) { return rts_fail("z combat", msg); }

static RtsGameModel *load(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/Z" };
    if (model && rts_game_model_load(model, &config)) return model;
    rts_game_model_destroy(model);
    return NULL;
}

static bool alive(const mobj_t *target) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if ((const mobj_t *)th == target && th->function == P_MobjThinker) return true;
    return false;
}

static mobj_t *nth(int type, int team, int n) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == type && mo->hp > 0 &&
            (team < 0 || mo->team == team) && n-- == 0) return mo;
    }
    return NULL;
}

/* The `n`th live member of `squad`. */
static mobj_t *member(int squad, int n) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->hp > 0 && mo->z.squad == squad && n-- == 0) return mo;
    }
    return NULL;
}

/* A computer Grunt of another squad than `squad`. */
static mobj_t *other_squad(int squad) {
    for (int i = 0; nth(MT_UNIT_GRUNT, 1, i); ++i)
        if (nth(MT_UNIT_GRUNT, 1, i)->z.squad != squad) return nth(MT_UNIT_GRUNT, 1, i);
    return NULL;
}

static bool tick(RtsGameModel *model, int tics) {
    RtsRenderSnapshot snap;
    for (int t = 0; t < tics; ++t)
        if (!rts_tick(model, &snap)) return false;
    return true;
}

static void place(mobj_t *mo, ivec2_t cell) {
    mo->core.position = fixed3_from_fixed2(fixed2_cell_center(cell), mo->core.position.z);
    P_ClearMove(mo);
    mo->movement.goal = fixed3_xy(mo->core.position);
    mo->movement.order_arrived = true;
}

/* Only `squad`'s members and `keep` fight; everyone else stands down. */
static void disarm_except(int squad, const mobj_t *keep) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo != keep && (!squad || mo->z.squad != squad)) mo->traits &= ~MF_ATTACK;
        else if (mo->info) mo->traits |= mo->info->traits & MF_ATTACK;
    }
}

static int cells_apart(const mobj_t *a, const mobj_t *b) {
    ivec2_t d = ivec2_sub(fixed2_cell(fixed3_xy(a->core.position)), fixed2_cell(fixed3_xy(b->core.position)));
    return abs(d.x) > abs(d.y) ? abs(d.x) : abs(d.y);
}

/* A shot robot bursts into bits (290..295) and is gone within a second;
 * it no longer sits on the field for seconds. */
static int test_death(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for a death");
    mobj_t *grunt = nth(MT_UNIT_GRUNT, 1, 0);
    if (!grunt) return fail("a computer Grunt");
    const state_t *die = &states[mobjinfo[grunt->type_id].deathstate];
    if (die->frame != Z_ROBOT_DIE || P_StateFrames(die) != Z_ROBOT_DIE_FRAMES - 1)
        return fail("the death runs the burst pictures");
    P_DamageMobj(grunt, NULL, grunt->hp);
    if (grunt->core.state_id != S_GRUNT_DIE) return fail("the Grunt starts dying");
    int frames = 0, last = -1;
    for (int t = 0; t < RTS_TICRATE && alive(grunt); ++t) {
        if (grunt->core.frame != last) frames++;
        last = grunt->core.frame;
        if (!tick(model, 1)) return fail("tick");
    }
    if (alive(grunt)) return fail("the dead Grunt goes");
    if (frames < Z_ROBOT_DIE_FRAMES) return fail("it shows every burst picture");
    puts("PASS: z a robot bursts and is gone");
    rts_game_model_destroy(model);
    return 0;
}

/* A Grunt ordered onto a robot beyond its reach keeps that target and
 * walks in, even with another enemy in reach on the way. */
static int test_ordered_target(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for an order");
    mobj_t *grunt = nth(MT_UNIT_GRUNT, 0, 0), *mark = nth(MT_UNIT_GRUNT, 1, 0);
    mobj_t *other = mark ? other_squad(mark->z.squad) : NULL;
    if (!grunt || !mark || !other) return fail("a Grunt and two computer squads");
    disarm_except(0, grunt);
    ivec2_t at = fixed2_cell(fixed3_xy(mark->core.position));
    place(grunt, (ivec2_t){at.x, at.y + 9});
    place(other, (ivec2_t){at.x + 3, at.y + 9});
    grunt->hp = grunt->max_hp = 10000;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        P_MobjSetSelected((mobj_t *)th, (mobj_t *)th == grunt);
    RtsGameCommand order = { .kind = RTS_GAME_COMMAND_ATTACK_UNIT, .data.attack_unit = { .target_id = mark->id } };
    if (!rts_game_model_command(model, &order)) return fail("order the attack");
    int start = cells_apart(grunt, mark);
    for (int t = 0; t < 2 * RTS_TICRATE; ++t) {
        if (!tick(model, 1)) return fail("tick");
        if (grunt->attack.target != mark) return fail("the ordered target sticks");
    }
    if (cells_apart(grunt, mark) >= start) return fail("the Grunt closes in");
    puts("PASS: z an ordered target is chased, not swapped for one in reach");
    rts_game_model_destroy(model);
    return 0;
}

/* A Grunt shot at from beyond its squad's reach turns on the shooter and
 * its squad mates go for it too; an idle squad notices an enemy that comes
 * within a few cells of its reach. */
static int test_alert(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for alerts");
    mobj_t *mark = nth(MT_UNIT_GRUNT, 1, 0), *jeep = nth(MT_UNIT_JEEP, Z_NEUTRAL, 0);
    if (!mark || !jeep || !mark->z.squad) return fail("a squad and a Jeep");
    int squad = mark->z.squad;
    Z_SetTeam(jeep, 0);
    jeep->traits |= jeep->info->traits & (MF_SELECTABLE | MF_MOBILE);
    jeep->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    jeep->hp = jeep->max_hp = 10000;
    disarm_except(squad, NULL);
    ivec2_t at = fixed2_cell(fixed3_xy(mark->core.position));
    place(jeep, (ivec2_t){at.x + 12, at.y});
    if (!tick(model, RTS_TICRATE)) return fail("tick");
    for (int i = 0; member(squad, i); ++i)
        if (member(squad, i)->attack.target) return fail("the squad starts idle");
    P_DamageMobj(mark, jeep, 1);
    if (mark->attack.target != jeep) return fail("the shot Grunt turns on the Jeep");
    if (!tick(model, RTS_TICRATE)) return fail("tick");
    for (int i = 0; member(squad, i); ++i)
        if (member(squad, i)->attack.target != jeep) return fail("its squad joins in");

    mobj_t *guard = other_squad(squad);
    if (!guard) return fail("a second squad");
    disarm_except(guard->z.squad, NULL);
    at = fixed2_cell(fixed3_xy(guard->core.position));
    place(jeep, (ivec2_t){at.x, at.y + 12});
    if (!tick(model, RTS_TICRATE)) return fail("tick");
    if (guard->attack.target) return fail("a far Jeep goes unnoticed");
    place(jeep, (ivec2_t){at.x, at.y + 7});
    if (!tick(model, RTS_TICRATE)) return fail("tick");
    if (guard->attack.target != jeep) return fail("a near Jeep is noticed");
    puts("PASS: z squads answer for their members and notice enemies nearby");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    if (test_death() || test_ordered_target() || test_alert()) return 1;
    return 0;
}
