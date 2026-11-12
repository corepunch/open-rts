#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

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

static int test_reconstruction(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load reconstruction fixture");
    disarm();
    mobj_t *building = nth(MT_TANK_FACTORY, -1, 0);
    if (!building) building = nth(MT_ROBOT_FACTORY, -1, 0);
    if (!building) return fail("reconstruction building");
    zmap_t *map = level.native_data;
    if (map->debris_trig[0] != -898 || map->debris_trig[1] != 493)
        return fail("native SINCOS projection values");
    int kind = building->type_id == MT_TANK_FACTORY ? 1 : 2;
    building->hp = 0;
    building->z.rebuild_setting = UINT16_MAX;
    building->z.rebuild_stock = -1;
    if (!tick(model, 2) || building->hp > 0 || building->z.rebuild_stock != -1)
        return fail("disabled reconstruction timer stays disabled");
    mobj_t *edges[4];
    int xs[] = {-192, -193, (level.width / 2) * 32 - 161, (level.width / 2) * 32 - 160};
    for (int i = 0; i < 4; ++i) {
        edges[i] = P_SpawnMobj((fixed3_t){0}, MT_BUILDING_DEBRIS);
        if (!edges[i]) return fail("spawn projection boundary fixture");
        edges[i]->z.debris.origin = (ivec2_t){xs[i], -50};
        edges[i]->z.debris.rise = -4;
    }
    Z_RebuildTicker();
    if (edges[0]->remove || !edges[1]->remove || edges[2]->remove || !edges[3]->remove)
        return fail("recording uses native projected horizontal bounds");
    P_RemoveMobj(edges[0]); P_RemoveMobj(edges[2]);
    int ys[] = {-132, -133, (level.height / 2) * 32 - 101, (level.height / 2) * 32 - 100};
    for (int i = 0; i < 4; ++i) {
        edges[i] = P_SpawnMobj((fixed3_t){0}, MT_BUILDING_DEBRIS);
        if (!edges[i]) return fail("spawn vertical boundary fixture");
        edges[i]->z.debris.origin = (ivec2_t){0, ys[i]};
    }
    Z_RebuildTicker();
    if (edges[0]->remove || !edges[1]->remove || edges[2]->remove || !edges[3]->remove)
        return fail("recording uses native projected vertical bounds");
    P_RemoveMobj(edges[0]); P_RemoveMobj(edges[2]);
    mobj_t *bad = P_SpawnMobj((fixed3_t){0}, MT_BUILDING_DEBRIS);
    if (!bad) return fail("spawn invalid recorder fixture");
    bad->z.debris.kind = UINT8_MAX;
    Z_RebuildTicker();
    if (!bad->remove) return fail("invalid saved recorder kind is discarded");
    for (int i = 0; i < 2; ++i) {
        mobj_t *p = P_SpawnMobj((fixed3_t){0}, MT_BUILDING_DEBRIS);
        if (!p) return fail("spawn descriptor overwrite fixture");
        p->z.debris.kind = (uint8_t)kind;
        p->z.debris.origin = (ivec2_t){0, 0};
        p->z.debris.samples = (uint8_t)(i ? 7 : 11);
        p->z.debris.rise = 1;
    }
    Z_RebuildTicker();
    if (map->rebuild.frames[kind] != 7) return fail("last completed particle overwrites descriptor length");
    mobj_t *bridge_effect = P_SpawnMobj((fixed3_t){0}, MT_BRIDGE_DEBRIS);
    if (!bridge_effect) return fail("spawn shared effect slot fixture");
    P_SetMobjState(bridge_effect, S_BRIDGE_REBUILD);
    for (int i = 0; i < 63; ++i)
        if (!Z_StartBuildingRebuild(building)) return fail("allocate native reconstruction slots");
    if (Z_StartBuildingRebuild(building)) return fail("shared effect capacity is bounded");
    P_RemoveMobj(bridge_effect);
    if (!Z_StartBuildingRebuild(building)) return fail("released bridge slot admits reconstruction");
    mobj_t *effect;
    while ((effect = nth(MT_BUILDING_REBUILD, -1, 0))) P_RemoveMobj(effect);
    building->z.rebuild_setting = 1;
    building->z.rebuild_stock = 100000;
    Z_BuildingDestroyed(building);
    if (!tick(model, 1)) return fail("advance recorded debris");
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    uint32_t id = building->id, hash = G_Consistency();
    int stock = building->z.rebuild_stock;
    ++map->rebuild.seed;
    if (G_Consistency() == hash) return fail("rebuild random state contributes to consistency");
    --map->rebuild.seed;
    if (!G_SaveGameBlob(&save, &size, "Reconstruction", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud))
        return fail("save reconstruction particles");
    free(save);
    building = Z_ServiceTarget(id);
    map = level.native_data;
    if (!building || G_Consistency() != hash || building->z.rebuild_stock != stock)
        return fail("load reconstruction state");
    if (!tick(model, 100) || map->rebuild.frames[kind] < 2)
        return fail("destroyed building records a debris animation");
    building->z.gun_count = 1;
    building->z.guns[0] = 1;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->z.building) mo->team = Z_NEUTRAL;
    }
    map->rebuild.owner = 4;
    building->z.rebuild_stock = 100 * Z_NativeSteps(leveltime, Z_NATIVE_HZ);
    Z_RebuildTicker();
    if (building->z.rebuild_stock != 0 || nth(MT_BUILDING_REBUILD, -1, 0))
        return fail("zero timer stock does not reconstruct");
    Z_RebuildTicker();
    if (building->hp > 0 || !nth(MT_BUILDING_REBUILD, -1, 0))
        return fail("negative timer stock queues delayed reconstruction");
    /* 0x24ba3 completion does not test HP again. A crane can restore the
     * target, and a subsequent destruction can precede this old effect. */
    building->hp = building->max_hp;
    building->hp = 0;
    Z_SetTeam(building, 1);
    Z_BuildingDestroyed(building);
    production_t *production = P_EnsureMobjProduction(building);
    if (!production) return fail("allocate stale production fixture");
    production->queue_count = 1;
    production->release_active = true;
    effect = nth(MT_BUILDING_REBUILD, -1, 0);
    effect->z.debris.samples = 2;
    Z_RebuildTicker();
    if (building->hp != building->max_hp || building->z.gun_count || building->team != 1 ||
        building->z.rebuild_stock != 1700 || building->production)
        return fail("old effect restores a captured, redestroyed building and resets its stock");
    const StaticProductDefinition *products[32];
    int offered = Z_ProductsFor(building, products, 32);
    if (offered <= 0 || !G_QueueProduct(building, products[0]))
        return fail("restored factory can start fresh production");
    rts_game_model_destroy(model);
    return 0;
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
    static const int groups[] = {
#define Z_PRODUCT(group, type, stock) group,
#include "../../games/z/products.inc"
#undef Z_PRODUCT
    };
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
        int made = 1;
        if (Z_IsRobot(cases[i].type - MT_UNITS)) {
            made = 0;
            for (int member = 0, type; member < Z_CREW_MAX &&
                 (type = Z_GroupType(map, groups[product->row_id], member)) >= 0; ++member) {
                if (!Z_IsRobot(type)) return fail("native robot product group contains only robots");
                made += type == cases[i].type - MT_UNITS;
            }
        }
        if (i < 4) {
            if (!Z_GunReady(fort) || count(cases[i].type, 0) != before) return fail("only guns wait for placement");
        } else if (Z_GunReady(fort) || count(cases[i].type, 0) != before + made) {
            return fail("every robot and vehicle is released at its native deadline");
        }
        order(fort, TC_CANCEL_PRODUCTION, 0);
        fort->z.gun_count = 0; /* Isolate each product's deadline. */
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
    if (Z_ProductTimeMs(fort, gun) != INT_MAX || !Z_GunReady(fort)) return fail("building damage stalls the next gun, not the stored one");
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
    if (!Z_GunReady(fort) || fort->production) return fail("cancel stops production while retaining stored guns");
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
    zmap_t *map = level.native_data;
    int members = 0, grunts = 0;
    while (members < Z_CREW_MAX && Z_GroupType(map, 2, members) >= 0) {
        grunts += Z_GroupType(map, 2, members) == MT_UNIT_GRUNT - MT_UNITS;
        ++members;
    }
    if (members < 2) return fail("authored Grunt group contains multiple members");
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
    if (count(MT_UNIT_GRUNT, 0) != before + grunts || Z_GunReady(fort)) return fail("Grunt squad leaves the door at its native deadline");
    mobj_t *made = nth(MT_UNIT_GRUNT, 0, before);
    irect_t fort_cells = P_MobjCells(fort);
    ivec2_t at = fixed2_cell(fixed3_xy(made->core.position));
    if (at.x >= fort_cells.x && at.x < fort_cells.x + fort_cells.w && at.y >= fort_cells.y &&
        at.y < fort_cells.y + fort_cells.h) return fail("the Grunt stands outside the fort");
    if (!L_IsWalkable(&level, at.x, at.y) || !made->z.squad || made->z.squad_slot != UINT8_MAX ||
        made->z.formation != Z_GroupFormation(map, 2) || made->owner != 0 || !(made->traits & MF_MOBILE))
        return fail("produced leader retains native group identity and formation");
    uint32_t last_id = made->id;
    uint32_t member_ids[Z_CREW_MAX] = {made->id};
    ivec2_t anchor = Z_BlastAnchor(made);
    for (int i = 1; i < members; ++i) {
        mobj_t *follower = NULL;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *mo = (mobj_t *)th;
            if (th->function == P_MobjThinker && !mo->remove && mo->z.squad == made->z.squad && mo->z.squad_slot == i)
                follower = mo;
        }
        if (!follower || follower->type_id != MT_UNITS + Z_GroupType(map, 2, i) ||
            follower->z.robot != Z_GroupRobot(map, 2, i) + 1 || !ivec2_equal(Z_BlastAnchor(follower), anchor) ||
            follower->z.route.count || follower->z.route.active)
            return fail("produced squad matches authored members at common origin");
        member_ids[i] = follower->id;
        if (follower->id > last_id) last_id = follower->id;
    }
    uint8_t next = Z_AllocateSquad(0), cpu = Z_AllocateSquad(1);
    if (!next || cpu < 121) return fail("allocator uses free native group ranges");
    mobj_t *departed = P_SpawnMobj(made->core.position, MT_UNIT_GRUNT);
    mobj_t *survivor = P_SpawnMobj(made->core.position, MT_UNIT_GRUNT);
    if (!departed || !survivor) return fail("spawn cooldown fixture");
    departed->z.squad = next; departed->z.squad_slot = UINT8_MAX;
    survivor->z.squad = next; survivor->z.squad_slot = 1;
    uint32_t hash = G_ConsistencyExtra(0);
    Z_SquadLeave(departed);
    if (map->squad_cooldown[next - 1] || survivor->z.squad_slot != UINT8_MAX)
        return fail("surviving member keeps its group without cooldown");
    Z_SquadLeave(survivor);
    if (map->squad_cooldown[next - 1] != 200 || Z_AllocateSquad(0) == next ||
        G_ConsistencyExtra(0) == hash) return fail("last departure cools its slot and changes consistency");
    P_RemoveMobj(departed); P_RemoveMobj(survivor);
    uint8_t preferred = Z_AllocateSquad(0);
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    uint32_t leader_id = made->id, fort_id = fort->id;
    if (!G_SaveGameBlob(&save, &size, "Produced squad", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud))
        return fail("save produced squad");
    free(save);
    made = P_MobjById(leader_id); fort = P_MobjById(fort_id);
    map = level.native_data;
    if (!made || !fort || map->squad_cooldown[next - 1] != 200 ||
        Z_AllocateSquad(0) != preferred || Z_AllocateSquad(1) != cpu)
        return fail("save retains produced squad and deterministic allocation");
    for (int i = 0; i < members; ++i) {
        mobj_t *member = P_MobjById(member_ids[i]);
        if (!member || member->z.squad != made->z.squad ||
            member->z.squad_slot != (i ? i : UINT8_MAX)) return fail("save retains produced squad membership");
    }
    mobj_t *foreign = P_SpawnMobj(made->core.position, MT_UNIT_GRUNT);
    if (!foreign) return fail("spawn foreign squad allocation fixture");
    Z_SetTeam(foreign, 1);
    foreign->z.squad = preferred; foreign->z.squad_slot = UINT8_MAX;
    if (Z_AllocateSquad(0) == preferred) return fail("group IDs are globally unique across teams");
    P_RemoveMobj(foreign);
    if (Z_AllocateSquad(0) != preferred) return fail("removed group slot can be reused");
    memset(map->squad_cooldown, 1, sizeof(map->squad_cooldown));
    if (Z_AllocateSquad(0) != next) return fail("allocator falls back to a cooling free slot");
    map->squad_cooldown[next - 1] = 200;
    int elapsed = 0, tics = 0;
    while (elapsed + Z_NativeSteps(leveltime + tics, Z_NATIVE_HZ) < 200)
        elapsed += Z_NativeSteps(leveltime + tics++, Z_NATIVE_HZ);
    int remaining = 200 - elapsed;
    if (!tick(model, tics) || map->squad_cooldown[next - 1] != remaining)
        return fail("cooldown remains active until its final native-frame boundary");
    save = NULL; size = 0;
    if (!G_SaveGameBlob(&save, &size, "Cooldown", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud))
        return fail("save nearly expired cooldown");
    free(save);
    map = level.native_data; fort = P_MobjById(fort_id);
    if (!fort || map->squad_cooldown[next - 1] != remaining || !tick(model, 1))
        return fail("load continues cooldown at the saved boundary");
    if (map->squad_cooldown[next - 1] || Z_AllocateSquad(0) != next)
        return fail("cooldown expires and permits preferred reuse");
    if (!fort->production || fort->production->queue_count < 1 || fort->production->time_left_ms <= 0)
        return fail("the fort starts the next Grunt by itself");
    order(fort, TC_CANCEL_PRODUCTION, 0);
    if (fort->production && fort->production->queue_count) return fail("STOP empties the queue");
    G_ProductionTicker(1380588);
    if (newer_than(MT_UNIT_GRUNT, 0, last_id)) return fail("nothing more is made after STOP");
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

static int test_rally(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load rally fixture");
    disarm();
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    mobj_t *fort = nth(MT_FORT, 0, 0), *enemy = nth(MT_FORT, 1, 0);
    if (!fort || !enemy) return fail("rally producers");
    ivec2_t point = fixed2_cell(fixed3_xy(nth(MT_UNIT_GRUNT, 0, 0)->core.position));
    ticcmd_t enemy_rally = {.order = TC_ORDER, .count = 1, .units = {enemy->id}, .queued = true,
                            .position = fixed3_from_fixed2(fixed2_cell_center(point), 0)};
    G_RunTiccmd(1, &enemy_rally);
    Z_RouteTick(enemy);
    if (enemy->z.route.count != 1 || enemy->z.route.owner != 1)
        return fail("first Shift rally retains second player's ownership");
    ticcmd_t cmd = {.order = TC_ORDER, .count = 1, .units = {fort->id},
                    .position = fixed3_from_fixed2(fixed2_cell_center(point), 0)};
    G_RunTiccmd(1, &cmd);
    if (fort->z.route.count) return fail("foreign producer rejects rally");
    G_RunTiccmd(0, &cmd);
    if (fort->z.route.count != 1 || !ivec2_equal(fixed2_cell(fixed3_xy(fort->z.route.steps[0].position)), point))
        return fail("map order sets producer rally");
    ticcmd_t bad = {.order = TC_PATH, .count = 1, .units = {fort->id},
                    .path = {.count = 1, .mode = WP_ONCE, .points = {{-1, 0}}}};
    G_RunTiccmd(0, &bad);
    if (fort->z.route.count != 1 || !ivec2_equal(fixed2_cell(fixed3_xy(fort->z.route.steps[0].position)), point))
        return fail("invalid rally route is rejected");
    int before = count(MT_UNIT_GRUNT, 0);
    order(fort, TC_BUILD, Z_ProductId(MT_UNIT_GRUNT));
    G_ProductionTicker(1380588);
    mobj_t *made = nth(MT_UNIT_GRUNT, 0, before);
    if (!made || made->z.route.active != TC_ORDER) return fail("produced unit inherits rally");
    if (!P_HasMoveOrder(made)) return fail("produced unit starts toward rally");
    ivec2_t next = fixed2_cell(fixed3_xy(fort->core.position));
    cmd.position = fixed3_from_fixed2(fixed2_cell_center(next), 0);
    G_RunTiccmd(0, &cmd);
    if (fort->z.route.count != 1 || !ivec2_equal(fixed2_cell(fixed3_xy(fort->z.route.steps[0].position)), next) ||
        made->z.route.active != TC_ORDER)
        return fail("a new rally replaces the factory route without changing an existing unit");
    fixed3_t start = made->core.position;
    if (!tick(model, 200) || !memcmp(&made->core.position, &start, sizeof(start)))
        return fail("produced unit travels toward rally");
    order(fort, TC_STOP, 0);
    if (fort->z.route.count) return fail("stop clears producer rally");
    order(fort, TC_CANCEL_PRODUCTION, 0);
    rts_game_model_destroy(model);
    return 0;
}

static int test_mixed_routes(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load mixed route fixture");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    mobj_t *unit = nth(MT_UNIT_GRUNT, 0, 0), *enemy = nth(MT_UNIT_GRUNT, 1, 0);
    mobj_t *mate = nth(MT_UNIT_GRUNT, 0, 1);
    if (!unit || !enemy || !mate) return fail("mixed route units");
    Z_SquadLeave(unit);
    Z_SquadLeave(mate);
    ticcmd_t group = {.order = TC_MOVE, .count = 2, .units = {unit->id, mate->id},
                      .position = fixed3_from_fixed2(fixed2_add(fixed3_xy(unit->core.position),
                                                               (fixed2_t){FIXED_FROM_INT(3), 0}), 0)};
    G_RunTiccmd(0, &group);
    if (!unit->movement.order_id || unit->movement.order_id != mate->movement.order_id)
        return fail("immediate group move retains one formation order");
    P_MobjSetSelected(unit, true);
    fixed2_t origin = fixed3_xy(unit->core.position);
    RtsGameCommand move = {.kind = RTS_GAME_COMMAND_MOVE_SELECTED,
                           .data.move_selected = {.target = fixed2_add(origin, (fixed2_t){FIXED_FROM_INT(3), 0})}};
    if (!rts_game_model_command(model, &move) || !tick(model, 1)) return fail("start model move");
    SDL_SetModState(KMOD_SHIFT);
    move.data.move_selected.target = fixed2_add(origin, (fixed2_t){FIXED_FROM_INT(4), FIXED_ONE});
    bool accepted = rts_game_model_command(model, &move);
    SDL_SetModState(KMOD_NONE);
    if (!accepted || !tick(model, 1) || unit->z.route.count != 1 || unit->z.route.steps[0].order != TC_MOVE)
        return fail("model Shift move appends to active order");
    ticcmd_t attack = {.order = TC_ATTACK, .target = enemy->id, .position = enemy->core.position,
                       .queued = true, .count = 1, .units = {unit->id}};
    G_RunTiccmd(0, &attack);
    if (unit->z.route.count != 2 || unit->z.route.steps[1].target != enemy->id)
        return fail("mixed route retains target ID");
    ticcmd_t path = {.order = TC_PATH, .queued = true, .count = 1, .units = {unit->id},
                     .path = {.count = 2, .mode = WP_ONCE, .points = {{3,3}, {4,4}}}};
    G_RunTiccmd(0, &path);
    if (unit->z.route.count != 4 || unit->z.route.steps[2].order != TC_ORDER ||
        !ivec2_equal(fixed2_cell(fixed3_xy(unit->z.route.steps[3].position)), (ivec2_t){4,4}))
        return fail("queued one-shot path appends all points");
    path.path.mode = WP_LOOP;
    G_RunTiccmd(0, &path);
    if (unit->z.route.count != 4) return fail("queued looping path preserves existing route");
    for (int i = 0; i < 9; ++i) G_RunTiccmd(0, &attack);
    if (unit->z.route.count != MAXWAYPOINTS) return fail("mixed route is bounded");
    ticcmd_t invalid = attack; invalid.queued = false; invalid.target = UINT32_MAX;
    G_RunTiccmd(0, &invalid);
    if (unit->z.route.count != MAXWAYPOINTS) return fail("invalid target preserves active route");
    invalid.order = TC_ORDER;
    invalid.position = group.position;
    G_RunTiccmd(0, &invalid);
    if (unit->z.route.count != MAXWAYPOINTS) return fail("stale order target preserves active route");
    path.path.mode = WP_ONCE;
    G_RunTiccmd(0, &path);
    if (unit->z.route.count != MAXWAYPOINTS) return fail("queued path capacity is atomic");
    order(unit, TC_STOP, 0);
    if (unit->z.route.count || unit->z.route.active) return fail("stop clears mixed route");
    unit->z.route = (zroute_t){.owner = 0, .active = TC_MOVE, .count = 2,
        .steps = {{.order = TC_UNLOAD}, {.order = TC_MOVE, .position = group.position}}};
    P_ClearMove(unit); unit->movement.order_arrived = true;
    Z_RouteTick(unit);
    if (unit->z.route.active != TC_MOVE || unit->z.route.count)
        return fail("malformed saved step is skipped");
    mobj_t *fort = nth(MT_FORT, 0, 0);
    if (!fort) return fail("mixed rally factory");
    ticcmd_t mixed = {.order = TC_ORDER, .position = group.position, .count = 2, .units = {unit->id, fort->id}};
    G_RunTiccmd(0, &mixed);
    if (fort->z.route.count != 1 || unit->z.route.active != TC_ORDER)
        return fail("mixed producer and mobile selection receives both orders");
    G_RunTiccmd(1, &mixed);
    if (fort->z.route.count != 1 || unit->z.route.active != TC_ORDER)
        return fail("foreign player cannot replace mixed routes");
    ticcmd_t rally = {.order = TC_ORDER, .position = fixed3_from_fixed2(move.data.move_selected.target, 0),
                      .count = 1, .units = {fort->id}};
    G_RunTiccmd(0, &rally);
    rally.order = TC_ATTACK; rally.target = enemy->id; rally.position = enemy->core.position; rally.queued = true;
    G_RunTiccmd(0, &rally);
    if (fort->z.route.count != 2 || fort->z.route.steps[1].target != enemy->id)
        return fail("factory stores mixed movement and attack rally");
    mobj_t *made = P_SpawnMobj(fort->core.position, MT_UNIT_GRUNT);
    if (!made) return fail("spawn mixed rally unit");
    Z_SetTeam(made, 0);
    G_UnitProduced(made, fort);
    if (made->z.route.active != TC_ORDER || made->z.route.count != 1 || made->z.route.steps[0].target != enemy->id)
        return fail("produced unit receives independent mixed rally");
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    uint32_t made_id = made->id, enemy_id = enemy->id, fort_id = fort->id, hash = G_Consistency();
    if (!G_SaveGameBlob(&save, &size, "Mixed rally", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud))
        return fail("save mixed rally and active route");
    free(save);
    made = P_MobjById(made_id); enemy = P_MobjById(enemy_id); fort = P_MobjById(fort_id);
    if (!made || !enemy || !fort || G_Consistency() != hash || made->z.route.count != 1)
        return fail("load mixed rally and active route");
    Z_SetTeam(fort, 1);
    if (fort->z.route.count || made->z.route.count != 1) return fail("capture clears only factory rally");
    P_ClearMove(made); made->movement.order_arrived = true;
    Z_RouteTick(made);
    if (made->z.route.active != TC_ATTACK || made->attack.target != enemy)
        return fail("rally advances from movement to attack target");
    ticcmd_t after = {.order = TC_MOVE, .position = group.position, .queued = true,
                      .count = 1, .units = {made->id}};
    G_RunTiccmd(0, &after);
    Z_SetTeam(enemy, 0);
    Z_RouteTick(made);
    if (made->z.route.active != TC_MOVE || made->z.route.count || made->attack.target)
        return fail("captured attack target advances immediately to next order");
    Z_SetTeam(enemy, 1);
    attack.units[0] = made->id; attack.target = enemy->id; attack.queued = false;
    G_RunTiccmd(0, &attack);
    G_RunTiccmd(0, &after);
    enemy->hp = 0;
    Z_RouteTick(made);
    if (made->z.route.active != TC_MOVE || made->z.route.count || made->attack.target)
        return fail("dead attack target advances immediately to next order");
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
    order(fort, TC_CANCEL_PRODUCTION, 0);
    const StaticProductDefinition *grunt = G_ModelProductByUIId(NULL, Z_ProductId(MT_UNIT_GRUNT));
    int before = count(MT_UNIT_GRUNT, 1);
    if (!grunt || !G_QueueProduct(fort, grunt)) return fail("CPU queues a robot squad");
    fort->production->time_left_ms = 1;
    G_ProductionTicker(1);
    mobj_t *leader = nth(MT_UNIT_GRUNT, 1, before);
    if (!leader || !leader->z.manufactured || leader->z.squad < 121 || leader->z.squad_slot != UINT8_MAX)
        return fail("CPU robot production creates a native-range squad leader");
    rts_game_model_destroy(model);
    return 0;
}

static bool cannon(int type) {
    return type == MT_UNIT_LIGHT_GUN || type == MT_UNIT_GATLING ||
           type == MT_UNIT_HOWITZER || type == MT_UNIT_MISSILE_GUN;
}

static int test_computer_next_product(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load CPU next-product fixture");
    mobj_t *fort = nth(MT_FORT, 1, 0);
    zmap_t *map = level.native_data;
    if (!fort || !map) return fail("CPU next-product fort");
    irect_t base = P_MobjCells(fort);
    int territory = Z_AITerritory(map, fixed2_from_cell((ivec2_t){base.x, base.y}));
    if (territory < 0) return fail("CPU fort territory");
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *mo = (mobj_t *)th;
        if (mo->team == 1 && cannon(mo->type_id)) P_RemoveMobj(mo);
    }
    fort->z.rating = 5000;
    map->ai.total = 1000; map->ai.mobile = 500;
    map->ai.territories[territory].value = 998;
    const StaticProductDefinition *grunt = G_ModelProductByUIId(NULL, Z_ProductId(MT_UNIT_GRUNT));
    if (!grunt || !G_QueueProduct(fort, grunt)) return fail("queue initial CPU product");
    mobj_t *unit = P_SpawnMobj(fort->core.position, MT_UNIT_GRUNT);
    if (!unit) return fail("spawn completed CPU product");
    Z_SetTeam(unit, 1);
    G_UnitProduced(unit, fort);
    if (fort->production->queue_count != 2 || cannon(fort->production->pending[0].product_type))
        return fail("CPU retains mobile selection below native reserve threshold");
    G_RemoveProductionAt(fort, 1);
    map->ai.territories[territory].value = 1000;
    fort->z.rating = 1000;
    G_UnitProduced(unit, fort);
    if (fort->production->queue_count != 2 || cannon(fort->production->pending[0].product_type))
        return fail("CPU fort does not select guns at rating 1000");
    G_RemoveProductionAt(fort, 1);
    fort->z.rating = 5000;
    G_UnitProduced(unit, fort);
    if (fort->production->queue_count != 2 || !cannon(fort->production->pending[0].product_type))
        return fail("CPU selects a gun at the native reserve threshold");
    G_RemoveProductionAt(fort, 0);
    const StaticProductDefinition *gun = Z_ProductInProduction(fort);
    fort->z.gun_count = 3;
    fort->z.guns[0] = fort->z.guns[1] = fort->z.guns[2] = MT_UNIT_LIGHT_GUN - MT_UNITS + 1;
    if (!gun || !G_ModelStartProductionRelease(NULL, fort, gun, fort->production->actor_id) ||
        fort->z.gun_count != 4 || !fort->production || fort->production->queue_count != 1 ||
        cannon(fort->production->product_type))
        return fail("CPU resumes mobile production when its fourth gun fills the stock");
    rts_game_model_destroy(model);
    return 0;
}

static bool finish_now(mobj_t *producer) {
    if (!producer->production) return false;
    producer->production->time_left_ms = 1;
    G_ProductionTicker(1);
    return true;
}

static int test_gun_stock(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for gun stock");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    disarm();
    mobj_t *fort = nth(MT_FORT, 0, 0);
    const zmap_t *map = level.native_data;
    if (!fort) return fail("fort for gun stock");
    irect_t base = P_MobjCells(fort);
    int territory = Z_AITerritory(map, fixed2_from_cell((ivec2_t){base.x,base.y}));
    if (!fort || territory < 0) return fail("fort gun territory");
    fort->z.rating = 5000;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        int t = Z_UnitType(mo);
        if (th->function == P_MobjThinker && t >= 0 && mo->team == 0 && mo->info &&
            !Z_IsRobot(t) && !(mo->info->traits & MF_MOBILE) &&
            Z_AITerritory(map, fixed3_xy(mo->core.position)) == territory) P_RemoveMobj(mo);
    }
    const StaticProductDefinition *gat = G_ModelProductByUIId(NULL, Z_ProductId(MT_UNIT_GATLING));
    const StaticProductDefinition *how = G_ModelProductByUIId(NULL, Z_ProductId(MT_UNIT_HOWITZER));
    if (!gat || !how || !G_QueueProduct(fort, gat) || !finish_now(fort) || fort->z.gun_count != 1 ||
        Z_StoredGun(fort, 0) != gat || !Z_ProductInProduction(fort)) return fail("stored gun and next production coexist");
    if (!G_QueueProduct(fort, how) || !finish_now(fort) || fort->z.gun_count != 2 ||
        Z_ProductInProduction(fort) != how || !G_QueueProduct(fort, gat))
        return fail("queued gun changes product and keeps prior stock");
    ivec2_t other_cell = fixed2_cell(fixed3_xy(fort->core.position));
    mobj_t *other = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center(other_cell), 0), MT_ROBOT_FACTORY);
    if (!other) return fail("spawn second producer");
    Z_SetTeam(other, 0); other->z.rating = 5000;
    if (!G_QueueProduct(other, gat) || !finish_now(fort) || fort->z.gun_count != 3 ||
        !finish_now(fort) || !fort->production || !fort->production->release_active || fort->z.gun_count != 3)
        return fail("another producer's reservation blocks a completed queued gun");
    order(other, TC_CANCEL_PRODUCTION, 0);
    other->z.guns[0] = (uint8_t)(MT_UNIT_GATLING - MT_UNITS + 1);
    other->z.gun_count = 1;
    P_DamageMobj(other, NULL, other->hp);
    if (other->z.gun_count != 1 || Z_GunReady(other)) return fail("destroyed factory retains inaccessible stock");
    other->hp = other->max_hp;
    P_SetMobjState(other, mobjinfo[other->type_id].spawnstate);
    if (!Z_GunReady(other)) return fail("rebuilt factory exposes retained stock");
    P_RemoveMobj(other);
    G_ProductionTicker(1);
    if (fort->z.gun_count != 4 || fort->production ||
        Z_StoredGun(fort, 0) != gat || Z_StoredGun(fort, 1) != how ||
        Z_StoredGun(fort, 2) != gat || Z_StoredGun(fort, 3) != gat || G_QueueProduct(fort, gat))
        return fail("freed capacity stores the waiting gun and stops repeat production");
    Z_SetTeam(fort, 1);
    if (!Z_GunReady(fort) || Z_StoredGun(fort, 2) != gat) return fail("capture transfers factory stock");
    Z_SetTeam(fort, 0);
    fort->z.gun_count = 255;
    if (Z_GunReady(fort) || Z_StoredGun(fort, 0) || Z_PlaceGun(fort, MT_UNIT_GATLING, (ivec2_t){0,0}))
        return fail("invalid stored count is rejected");
    (void)G_Consistency();
    fort->z.gun_count = 4;
    uint32_t hash = G_Consistency();
    fort->z.guns[0]++;
    if (G_Consistency() == hash) return fail("stored gun contributes to consistency");
    fort->z.guns[0]--;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0; uint32_t id = fort->id;
    if (!G_SaveGameBlob(&save, &size, "Gun stock", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save four stored guns");
    free(save);
    fort = P_MobjById(id);
    if (!fort || fort->z.gun_count != 4 || Z_StoredGun(fort, 2) != gat) return fail("load mixed gun stock");
    base = P_MobjCells(fort);
    fixed3_t spot; ivec2_t cell = {0}; bool found = false;
    for (int y = base.y; y < base.y + 9 && !found; ++y)
        for (int x = base.x; x < base.x + 9 && !found; ++x)
            if (Z_GunPosition(fort, MT_UNIT_GATLING, (ivec2_t){x,y}, &spot)) { cell = (ivec2_t){x,y}; found = true; }
    if (!found || !Z_PlaceGun(fort, MT_UNIT_GATLING, cell) || fort->z.gun_count != 3 || G_QueueProduct(fort, gat))
        return fail("placing an older stored type keeps the territory cap full");
    mobj_t *placed = nth(MT_UNIT_GATLING, 0, 0);
    if (!placed) return fail("find placed gun");
    P_RemoveMobj(placed);
    if (!G_QueueProduct(fort, gat)) return fail("destroying the placed gun frees production capacity");
    puts("PASS: z territory gun stock, production cap and mixed placement");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    if (test_catalog() || test_factory_thresholds() || test_campaign_ratings() || test_native_times() ||
        test_native_rates() || test_gun_placement() ||
        test_fort_makes_grunts() || test_vehicles_come_manned() || test_rally() || test_gun_stock() ||
        test_computer_builds() || test_computer_next_product() || test_reconstruction() || test_mixed_routes()) return 1;
    printf("z production tests passed\n");
    return 0;
}
