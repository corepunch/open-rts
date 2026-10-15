#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>
#include <limits.h>

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
    if (G_ModelProductTrainingTimeMs(grunt) != 1380588) return fail("Grunt's native stock timer at level speed 10");
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

/* Stock outputs from ZED.EXE 0x98acc, scaled by 0x41a69/0x41afc.
 * Speed 20 includes odd native tick counts: halve before converting to ms. */
static int test_native_times(void) {
    static const struct { int type, speed10, speed20; } cases[] = {
        {MT_UNIT_GATLING, 1908352, 954176},
        {MT_UNIT_LIGHT_GUN, 2269764, 1134882},
        {MT_UNIT_HOWITZER, 3393000, 1696470},
        {MT_UNIT_MISSILE_GUN, 4486294, 2243117},
        {MT_UNIT_GRUNT, 1380588, 690294},
        {MT_UNIT_PSYCHO, 1926588, 963294},
        {MT_UNIT_TOUGH, 2373764, 1186882},
        {MT_UNIT_SNIPER, 2917176, 1458588},
        {MT_UNIT_PYRO, 3166764, 1583352},
        {MT_UNIT_LASER, 3530764, 1765352},
        {MT_UNIT_JEEP, 1625000, 812470},
        {MT_UNIT_APC, 2366000, 1183000},
        {MT_UNIT_LIGHT_TANK, 2797588, 1398764},
        {MT_UNIT_MED_TANK, 4394000, 2197000},
        {MT_UNIT_HEAVY_TANK, 5361176, 2680588},
        {MT_UNIT_M_MISSILE, 6214000, 3107000},
        {MT_UNIT_CRANE, 2614294, 1307117},
    };
    RtsGameModel *model = load();
    if (!model) return fail("load native time cases");
    zmap_t *map = level.native_data;
    mobj_t *fort = nth(MT_FORT, 0, 0);
    if (!fort || map->production_speed != 10) return fail("level-1 production speed");
    fort->z.rating = 5000;
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        const StaticProductDefinition *product = G_ModelProductByUIId(NULL, Z_ProductId(cases[i].type));
        if (!product || G_ModelProductTrainingTimeMs(product) != cases[i].speed10)
            return fail("all 17 native stock timers at speed 10");
        map->production_speed = 20;
        if (G_ModelProductTrainingTimeMs(product) != cases[i].speed20)
            return fail("level speed divides native ticks before converting to milliseconds");
        map->production_speed = 10;
        int before = count(cases[i].type, 0);
        if (!G_QueueProduct(fort, product)) return fail("start native deadline case");
        G_ProductionTicker((cases[i].speed10 - 1) / 3);
        if (count(cases[i].type, 0) != before || Z_GunReady(fort)) return fail("no product finishes before its native deadline");
        G_ProductionTicker(1);
        if (i < 4) {
            if (!Z_GunReady(fort) || count(cases[i].type, 0) != before) return fail("only guns wait for placement");
        } else if (Z_GunReady(fort) || count(cases[i].type, 0) != before + 1) {
            return fail("every robot and vehicle is released at its native deadline");
        }
        order(fort, TC_CANCEL_PRODUCTION, 0);
    }
    rts_game_model_destroy(model);
    return 0;
}

static void hold_flags(int held) {
    for (int i = 0; nth(MT_FLAG, -1, i); ++i) Z_SetTeam(nth(MT_FLAG, -1, i), i < held ? 0 : Z_NEUTRAL);
}

/* 0x42405 counts nine territories, including the flagless neutral one;
 * 0x422cf rounds health first, then the combined rate. Changes mid-build
 * must affect both progression and the popup without resetting work. */
static int test_native_rates(void) {
    static const struct { int flags, hp, rate; } cases[] = {
        {1,1000,3}, {1,999,2}, {1,670,2}, {1,669,1}, {1,340,1}, {1,339,0},
        {0,1000,1}, {0,999,0}, {2,1000,6}, {3,1000,9}, {4,1000,12},
        {8,1000,23}, {8,500,11}, {8,1,0}, {1,1000,3},
    };
    static const struct { int maker, type; } products[] = {
        {MT_FORT, MT_UNIT_GRUNT}, {MT_FORT, MT_UNIT_JEEP}, {MT_FORT, MT_UNIT_GATLING},
        {MT_ROBOT_FACTORY, MT_UNIT_GRUNT}, {MT_TANK_FACTORY, MT_UNIT_JEEP},
        {MT_TANK_FACTORY, MT_UNIT_GATLING},
    };
    RtsGameModel *model = load();
    if (!model) return fail("load rate cases");
    zmap_t *map = level.native_data;
    int total;
    if (map->territory_count != 9 || count(MT_FLAG, -1) != 8 ||
        Z_TerritoriesHeld(0, &total) != 1 || total != 9 || Z_TerritoriesHeld(Z_NEUTRAL, NULL) != 7)
        return fail("native counts include the flagless neutral territory");
    for (size_t p = 0; p < sizeof(products) / sizeof(*products); ++p) {
        mobj_t *maker = nth(products[p].maker, -1, 0);
        const StaticProductDefinition *product = G_ModelProductByUIId(NULL, Z_ProductId(products[p].type));
        /* LEVEL01 has no tank factory; exercise that producer on the same
         * nine-territory fixture without changing the map's stock inputs. */
        if (!maker && products[p].maker == MT_TANK_FACTORY)
            maker = P_SpawnMobj(nth(MT_FORT, 0, 0)->core.position, MT_TANK_FACTORY);
        if (!maker || !product) return fail("native rate producer and product");
        Z_SetTeam(maker, 0);
        maker->z.rating = 1000;
        maker->max_hp = maker->hp = 1000;
        int base = G_ModelProductTrainingTimeMs(product), left = base;
        hold_flags(1);
        if (Z_ProductTimeMs(maker, product) != base / 3 || !G_QueueProduct(maker, product))
            return fail("idle estimate uses the same native rate");
        for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
            hold_flags(cases[i].flags);
            maker->hp = cases[i].hp;
            if (Z_TerritoriesHeld(0, &total) != cases[i].flags || total != 9 ||
                Z_ProductTimeMs(maker, product) != (cases[i].rate ? left / cases[i].rate : INT_MAX))
                return fail("capture and health update the remaining-time estimate immediately");
            G_ProductionTicker(1000);
            left -= 1000 * cases[i].rate;
            if (maker->production->time_left_ms != left || maker->production->time_ms != base)
                return fail("native health/territory rate, stall and resume preserve completed work");
        }
        hold_flags(8);
        map->territories[0].team = 0;
        if (Z_TerritoriesHeld(0, &total) != 9 || total != 9 || Z_ProductTimeMs(maker, product) != left / 26)
            return fail("a flagless owned territory contributes to the native rate 26");
        G_ProductionTicker(1000);
        if (maker->production->time_left_ms != left - 26000) return fail("all territories give native rate 26");
        map->territories[0].team = Z_NEUTRAL;
        order(maker, TC_CANCEL_PRODUCTION, 0);
    }
    rts_game_model_destroy(model);
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
    fort->hp = 1;
    if (Z_ProductTimeMs(fort, gun) != 0) return fail("building damage cannot stall a completed gun");
    fort->hp = health;
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
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    mobj_t *fort = nth(MT_FORT, 0, 0);
    if (!fort) return fail("the player's fort");
    int before = count(MT_UNIT_GRUNT, 0), total, held = Z_TerritoriesHeld(0, &total);
    if (total != 9 || held != 1) return fail("one of nine native territories belongs to the player");
    order(fort, TC_BUILD, Z_ProductId(MT_UNIT_GRUNT));
    if (!fort->production || fort->production->queue_count != 1 || fort->production->time_left_ms != 1380588)
        return fail("the fort starts on a Grunt");
    if (!tick(model, 1)) return fail("tick");
    int expected = 1380588 - RTS_TICK_MS * 3;
    if (fort->production->time_left_ms != expected) return fail("native rate three advances Grunt production");
    G_ProductionTicker((expected - 1) / 3);
    if (count(MT_UNIT_GRUNT, 0) != before || Z_GunReady(fort)) return fail("Grunt cannot finish early or become a stored gun");
    G_ProductionTicker(1);
    if (count(MT_UNIT_GRUNT, 0) != before + 1 || Z_GunReady(fort)) return fail("Grunt leaves the door at its native deadline");
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
    G_ProductionTicker(1380588);
    if (newer_than(MT_UNIT_GRUNT, 0, made->id)) return fail("nothing more is made after STOP");
    rts_game_model_destroy(model);
    return 0;
}

static int test_vehicles_come_manned(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level 1");
    disarm();
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    mobj_t *fort = nth(MT_FORT, 0, 0);
    if (!fort) return fail("the player's fort");
    int before = count(MT_UNIT_JEEP, 0);
    order(fort, TC_BUILD, Z_ProductId(MT_UNIT_JEEP));
    G_ProductionTicker(541666);
    if (count(MT_UNIT_JEEP, 0) != before || Z_GunReady(fort)) return fail("Jeep cannot finish before 1625000 stock ms at rate three");
    G_ProductionTicker(1);
    mobj_t *jeep = nth(MT_UNIT_JEEP, 0, before);
    if (!jeep || Z_GunReady(fort)) return fail("Jeep leaves the door at its native deadline");
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
    if (test_catalog() || test_factory_thresholds() || test_campaign_ratings() || test_native_times() ||
        test_native_rates() || test_gun_placement() ||
        test_fort_makes_grunts() || test_vehicles_come_manned() || test_computer_builds()) return 1;
    printf("z production tests passed\n");
    return 0;
}
