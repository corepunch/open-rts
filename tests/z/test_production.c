#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>

/* Native producer ratings and group requirements gate all production.
 * Mobile units leave the door; guns wait for placement; STOP empties the
 * queue, and the computer uses the same catalog and placement checks. */

static int fail(const char *msg) { return rts_fail("z production", msg); }

static RtsGameModel *load_map(const char *path) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/Z", .map_path = path };
    if (model && rts_game_model_load(model, &config)) return model;
    rts_game_model_destroy(model);
    return NULL;
}

static RtsGameModel *load(void) { return load_map(NULL); }

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
    mobj_t *fort = nth(MT_FORT, 0, 0), *robots = nth(MT_ROBOT_FACTORY, -1, 0);
    if (!fort || !robots) return fail("the player's fort and a robot factory");
    Z_SetTeam(robots, 0);
    const StaticProductDefinition *list[32];
    if (fort->z.rating != 1000 || Z_ProductsFor(fort, list, 32) != 3 ||
        list[0]->product_type != MT_UNIT_GATLING || list[1]->product_type != MT_UNIT_GRUNT ||
        list[2]->product_type != MT_UNIT_JEEP) return fail("level 1 offers exactly Gatling, Grunt, Jeep in native order");
    if (!lists(fort, MT_UNIT_GRUNT) || !lists(fort, MT_UNIT_JEEP)) return fail("a fort makes robots and vehicles");
    if (robots->z.rating != 1000 || Z_ProductsFor(robots, list, 32) != 2 ||
        !lists(robots, MT_UNIT_GRUNT) || !lists(robots, MT_UNIT_GATLING) || lists(robots, MT_UNIT_JEEP))
        return fail("a captured level-1 robot factory offers Grunt and Gatling");
    if (lists(fort, MT_UNIT_TRUCK) || lists(fort, MT_UNIT_FIRE_TRUCK)) return fail("trucks are outside the single-player cycle");
    const StaticProductDefinition *heavy = G_ModelProductByUIId(NULL, Z_ProductId(MT_UNIT_HEAVY_TANK));
    if (!heavy || G_ModelProductAvailable(model, 0, heavy) || G_QueueProduct(fort, heavy))
        return fail("unavailable technology cannot be queued through the model");
    order(fort, TC_BUILD, heavy->ui_id);
    if (fort->production) return fail("direct build commands cannot bypass technology");
    const StaticProductDefinition *grunt = G_ModelProductByUIId(NULL, Z_ProductId(MT_UNIT_GRUNT));
    if (!grunt || !grunt->label || strcmp(grunt->label, "Grunt") || grunt->cost) return fail("the Grunt costs nothing");
    if (G_ModelProductTrainingTimeMs(grunt) != 20000) return fail("a Grunt takes twenty seconds");
    if (Z_Producer(nth(MT_UNIT_GRUNT, 0, 0)) || Z_Producer(nth(MT_REPAIR, -1, 0))) return fail("only forts and factories produce");
    rts_game_model_destroy(model);
    return 0;
}

static int test_factory_thresholds(void) {
    RtsGameModel *model = load_map("LEVEL02/LEVEL02.MAP");
    if (!model) return fail("load level 2");
    mobj_t *low = NULL, *high = NULL, *tank = nth(MT_TANK_FACTORY, -1, 0);
    for (int i = 0; nth(MT_ROBOT_FACTORY, -1, i); ++i) {
        mobj_t *factory = nth(MT_ROBOT_FACTORY, -1, i);
        Z_SetTeam(factory, 0);
        if (factory->z.rating == 1000) low = factory;
        if (factory->z.rating == 2000) high = factory;
    }
    if (!low || !high || !tank || lists(low, MT_UNIT_PSYCHO) || !lists(high, MT_UNIT_PSYCHO))
        return fail("level-2 factories retain their distinct native ratings");
    Z_SetTeam(tank, 0);
    if (tank->z.rating != 1000 || !lists(tank, MT_UNIT_LIGHT_TANK) || !lists(tank, MT_UNIT_JEEP) ||
        !lists(tank, MT_UNIT_GATLING) || lists(tank, MT_UNIT_GRUNT) || lists(tank, MT_UNIT_MED_TANK))
        return fail("tier-one tank factory makes Light, Jeep and Gatling");
    high->z.rating = 1999;
    if (lists(high, MT_UNIT_PSYCHO)) return fail("technology does not round up below the threshold");
    high->z.rating = 2000;
    if (!lists(high, MT_UNIT_PSYCHO)) return fail("technology unlocks at the exact threshold");
    high->z.rating = 5000;
    if (lists(high, MT_UNIT_JEEP) || lists(high, MT_UNIT_MISSILE_GUN) || !lists(high, MT_UNIT_HOWITZER))
        return fail("zero requirement excludes a product even at maximum rating");
    zmap_t *map = level.native_data;
    uint8_t enabled = map->roster.bytes[18000 + 6 * 82];
    map->roster.bytes[18000 + 6 * 82] = 0;
    if (lists(high, MT_UNIT_PSYCHO)) return fail("inactive native groups are not offered");
    map->roster.bytes[18000 + 6 * 82] = enabled;
    rts_game_model_destroy(model);
    return 0;
}

static int test_campaign_ratings(void) {
    static const struct { int mission, rating, products; } cases[] = {
        {1,1000,3}, {5,2000,7}, {9,3000,10}, {13,4000,12}, {17,5000,17},
        {22,4000,12}, {26,3000,10}, {33,3000,10}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        char path[64];
        snprintf(path, sizeof(path), "LEVEL%02d/LEVEL%02d.MAP", cases[i].mission, cases[i].mission);
        RtsGameModel *model = load_map(path);
        if (!model) return fail("load campaign rating case");
        mobj_t *fort = nth(MT_FORT, 0, 0);
        const StaticProductDefinition *list[32];
        if (!fort || fort->z.rating != (unsigned)cases[i].rating ||
            Z_ProductsFor(fort, list, 32) != cases[i].products) return fail("native campaign fort rating and unlock count");
        rts_game_model_destroy(model);
    }
    return 0;
}

static int test_gun_placement(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for gun placement");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    disarm();
    mobj_t *fort = nth(MT_FORT, 0, 0), *enemy = nth(MT_FORT, 1, 0);
    const StaticProductDefinition *gun = G_ModelProductByUIId(NULL, Z_ProductId(MT_UNIT_GATLING));
    if (!fort || !enemy || !gun || !G_QueueProduct(fort, gun)) return fail("start available Gatling");
    const zmap_t *map = level.native_data;
    if (map->production_speed != 10 || G_ModelProductTrainingTimeMs(gun) != (1468 * 221 / 10) * 1000 / 17)
        return fail("Gatling timer uses the native stock and level speed");
    int health = fort->hp, left = fort->production->time_left_ms;
    fort->hp = 1;
    G_ProductionTicker(RTS_TICK_MS);
    if (fort->production->time_left_ms != left) return fail("zero rounded native health rate stalls gun production");
    fort->hp = health;
    int before = count(MT_UNIT_GATLING, 0);
    G_ProductionTicker(G_ModelProductTrainingTimeMs(gun));
    if (!Z_GunReady(fort) || count(MT_UNIT_GATLING, 0) != before)
        return fail("finished gun waits for placement rather than leaving the door");
    irect_t base = P_MobjCells(fort), foreign = P_MobjCells(enemy);
    fixed3_t position;
    if (Z_GunPosition(fort, MT_UNIT_GATLING, (ivec2_t){base.x + 1,base.y + 1}, &position))
        return fail("the starting gun blocks its native fort mount");
    if (Z_PlaceGun(fort, MT_UNIT_JEEP, (ivec2_t){base.x,base.y}) ||
        Z_PlaceGun(fort, MT_UNIT_GATLING, (ivec2_t){foreign.x,foreign.y}))
        return fail("placement rejects a wrong product and foreign territory");
    ivec2_t cell = {0};
    bool found = false;
    for (int y = base.y; y < base.y + 9 && !found; ++y)
        for (int x = base.x; x < base.x + 9 && !found; ++x)
            if (Z_GunPosition(fort, MT_UNIT_GATLING, (ivec2_t){x,y}, &position)) {
                cell = (ivec2_t){x,y}; found = true;
            }
    if (!found || position.z != FIXED_ONE) return fail("a free native fort mount");
    ticcmd_t cmd = {.order = TC_CONSTRUCT, .product = MT_UNIT_GATLING, .count = 1,
                    .units = {fort->id}, .position = fixed3_from_fixed2(fixed2_cell_center(cell), 0)};
    G_RunTiccmd(1, &cmd);
    if (count(MT_UNIT_GATLING, 0) != before) return fail("another player cannot place this gun");
    G_RunTiccmd(0, &cmd);
    mobj_t *made = nth(MT_UNIT_GATLING, 0, before);
    if (!made || memcmp(&made->core.position, &position, sizeof(position)) || !made->z.driver || Z_GunReady(fort))
        return fail("the command places a manned gun at the native mount and consumes it");
    G_RunTiccmd(0, &cmd);
    if (count(MT_UNIT_GATLING, 0) != before + 1) return fail("a repeated placement cannot duplicate a gun");
    G_ProductionTicker(G_ModelProductTrainingTimeMs(gun));
    order(fort, TC_CANCEL_PRODUCTION, 0);
    if (Z_GunReady(fort) || fort->production) return fail("cancel clears a stored gun");
    if (!G_QueueProduct(enemy, gun)) return fail("computer starts an available gun");
    G_ProductionTicker(G_ModelProductTrainingTimeMs(gun));
    int enemy_before = count(MT_UNIT_GATLING, 1);
    P_AiSetFeatures(rts_game_model_ai(model), AI_FEATURE_ATTACK);
    if (!tick(model, 5 * RTS_TICRATE) || Z_GunReady(enemy) || count(MT_UNIT_GATLING, 1) != enemy_before + 1)
        return fail("computer places its finished gun without local-player dependence");
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
    if (test_catalog() || test_factory_thresholds() || test_campaign_ratings() || test_gun_placement() ||
        test_fort_makes_grunts() || test_vehicles_come_manned() || test_computer_builds()) return 1;
    printf("z production tests passed\n");
    return 0;
}
