#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>

/* Forts and factories make units (products.inc): a fort makes robots and
 * vehicles, a robot factory robots; what is made leaves by the door and
 * the factory starts the next one; STOP empties the queue; holding
 * territories makes everything faster; the computer runs its own. */

static int fail(const char *msg) { return rts_fail("z production", msg); }

static RtsGameModel *load(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/Z" };
    if (model && rts_game_model_load(model, &config)) return model;
    rts_game_model_destroy(model);
    return NULL;
}

static mobj_t *nth(int type, int team, int n) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == type && mo->hp > 0 && !mo->remove &&
            (team < 0 || mo->team == team) && n-- == 0) return mo;
    }
    return NULL;
}

static int count(int type, int team) {
    int n = 0;
    while (nth(type, team, n)) n++;
    return n;
}

/* A live unit of `type` and `team` made after the one with `id`. */
static mobj_t *newer_than(int type, int team, uint32_t id) {
    for (int i = 0; nth(type, team, i); ++i)
        if (nth(type, team, i)->id > id) return nth(type, team, i);
    return NULL;
}

static void disarm(void) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        ((mobj_t *)th)->traits &= ~MF_ATTACK;
}

static bool tick(RtsGameModel *model, int tics) {
    RtsRenderSnapshot snap;
    for (int t = 0; t < tics; ++t)
        if (!rts_tick(model, &snap)) return false;
    return true;
}

static bool order(const mobj_t *producer, ticorder_t what, int product) {
    ticcmd_t cmd = {.order = what, .product = product, .count = 1, .units = {producer->id}};
    G_RunTiccmd(producer->owner, &cmd);
    return true;
}

static bool lists(const mobj_t *producer, int type) {
    const StaticProductDefinition *list[32];
    int n = Z_ProductsFor(producer, list, 32);
    for (int i = 0; i < n; ++i)
        if (list[i]->product_type == type) return true;
    return false;
}

static int test_catalog(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level 1");
    mobj_t *fort = nth(MT_FORT, 0, 0), *robots = nth(MT_ROBOT_FACTORY, -1, 0), *tanks = nth(MT_TANK_FACTORY, -1, 0);
    if (!fort || !robots) return fail("the player's fort and a robot factory");
    if (!lists(fort, MT_UNIT_GRUNT) || !lists(fort, MT_UNIT_JEEP)) return fail("a fort makes robots and vehicles");
    if (!lists(robots, MT_UNIT_GRUNT) || lists(robots, MT_UNIT_JEEP)) return fail("a robot factory makes robots only");
    if (tanks && (lists(tanks, MT_UNIT_GRUNT) || !lists(tanks, MT_UNIT_MED_TANK)))
        return fail("a vehicle factory makes vehicles only");
    if (lists(fort, MT_UNIT_GATLING) || lists(fort, MT_UNIT_TRUCK)) return fail("guns and trucks are not made");
    const StaticProductDefinition *grunt = G_ModelProductByUIId(NULL, Z_ProductId(MT_UNIT_GRUNT));
    if (!grunt || !grunt->label || strcmp(grunt->label, "Grunt") || grunt->cost) return fail("the Grunt costs nothing");
    if (G_ModelProductTrainingTimeMs(grunt) != 20000) return fail("a Grunt takes twenty seconds");
    if (Z_Producer(nth(MT_UNIT_GRUNT, 0, 0)) || Z_Producer(nth(MT_REPAIR, -1, 0))) return fail("only forts and factories produce");
    rts_game_model_destroy(model);
    return 0;
}

static int test_fort_makes_grunts(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level 1");
    disarm();
    mobj_t *fort = nth(MT_FORT, 0, 0);
    if (!fort) return fail("the player's fort");
    int before = count(MT_UNIT_GRUNT, 0), total, held = Z_TerritoriesHeld(0, &total);
    if (!total || held <= 0 || held >= total) return fail("the player holds some flags");
    order(fort, TC_BUILD, Z_ProductId(MT_UNIT_GRUNT));
    if (!fort->production || fort->production->queue_count != 1 || fort->production->time_left_ms != 20000)
        return fail("the fort starts on a Grunt");
    if (!tick(model, 1)) return fail("tick");
    int expected = 20000 - RTS_TICK_MS - RTS_TICK_MS * held / total;
    if (fort->production->time_left_ms != expected) return fail("held flags speed the work up");
    int tics = 0;
    while (count(MT_UNIT_GRUNT, 0) == before && tics++ < 25 * RTS_TICRATE)
        if (!tick(model, 1)) return fail("tick");
    if (count(MT_UNIT_GRUNT, 0) != before + 1) return fail("a Grunt comes out within twenty seconds");
    if (tics > 20 * RTS_TICRATE) return fail("it came out early, the flags helped");
    mobj_t *made = nth(MT_UNIT_GRUNT, 0, before);
    irect_t fort_cells = P_MobjCells(fort);
    ivec2_t at = fixed2_cell(fixed3_xy(made->core.position));
    if (at.x >= fort_cells.x && at.x < fort_cells.x + fort_cells.w && at.y >= fort_cells.y &&
        at.y < fort_cells.y + fort_cells.h) return fail("the Grunt stands outside the fort");
    if (!L_IsWalkable(&level, at.x, at.y) || made->z.squad || made->owner != 0 || !(made->traits & MF_MOBILE))
        return fail("the Grunt stands on open ground, the player's, in no squad");
    if (!fort->production || fort->production->queue_count < 1 || fort->production->time_left_ms <= 0)
        return fail("the fort starts the next Grunt by itself");
    order(fort, TC_CANCEL_PRODUCTION, 0);
    if (fort->production && fort->production->queue_count) return fail("STOP empties the queue");
    /* Grunts may fall meanwhile (the computer marches on the fort), so
     * look for a newer one rather than counting. */
    if (!tick(model, 25 * RTS_TICRATE) || newer_than(MT_UNIT_GRUNT, 0, made->id)) return fail("nothing more is made");
    rts_game_model_destroy(model);
    return 0;
}

static int test_vehicles_come_manned(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level 1");
    disarm();
    mobj_t *fort = nth(MT_FORT, 0, 0);
    if (!fort) return fail("the player's fort");
    int before = count(MT_UNIT_JEEP, 0);
    order(fort, TC_BUILD, Z_ProductId(MT_UNIT_JEEP));
    if (!tick(model, 41 * RTS_TICRATE)) return fail("tick");
    mobj_t *jeep = nth(MT_UNIT_JEEP, 0, before);
    if (!jeep) return fail("a Jeep within forty seconds");
    if (!jeep->z.driver || !(jeep->traits & MF_MOBILE) || !(jeep->traits & MF_ATTACK) || jeep->team != 0)
        return fail("the Jeep is driven and armed");
    rts_game_model_destroy(model);
    return 0;
}

static int test_computer_builds(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level 1");
    disarm();
    mobj_t *fort = nth(MT_FORT, 1, 0);
    if (!fort) return fail("the computer's fort");
    int tics = 0;
    while (!Z_ProductInProduction(fort) && tics++ < 10 * RTS_TICRATE)
        if (!tick(model, 1)) return fail("tick");
    if (!Z_ProductInProduction(fort)) return fail("the computer sets its fort going");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    if (test_catalog() || test_fort_makes_grunts() || test_vehicles_come_manned() || test_computer_builds()) return 1;
    printf("z production tests passed\n");
    return 0;
}
