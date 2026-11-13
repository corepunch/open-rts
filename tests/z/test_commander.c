#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *msg) { return rts_fail("z commander", msg); }
static RtsGameModel *load_map(const char *path) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/Z", .map_path = path};
    if (model && rts_game_model_load(model, &config)) return model;
    rts_game_model_destroy(model);
    return NULL;
}
static RtsGameModel *load(void) { return load_map(NULL); }
static mobj_t *nth(int type, int team, int n) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == type && !mo->remove && mo->hp > 0 &&
            (team < 0 || mo->team == team) && n-- == 0) return mo;
    }
    return NULL;
}
static bool tick(RtsGameModel *model, int tics) {
    RtsRenderSnapshot snap;
    for (int t = 0; t < tics; ++t) if (!rts_tick(model, &snap)) return false;
    return true;
}
static void think(int tics) {
    leveltime += tics;
    mobjlist_t list = P_ListMobjs();
    G_AiInterface()->tactics(&level, 1, list.items, list.count);
    P_FreeMobjList(&list);
}
static zaitask_t *task_of(const mobj_t *mo) {
    zmap_t *map = level.native_data;
    for (int i = 0; i < Z_AI_TASKS; ++i)
        if (map->ai.tasks[i].unit == mo->id) return &map->ai.tasks[i];
    return NULL;
}
static int test_settings(void) {
    static const uint8_t intelligence[] = {55,50,60,60,70,70,75,70,75,80,90,80,95,85,90,90,110,127,127,127};
    static const uint8_t allocation[] = {40,50,40,50,50,50,40,40,40,40,40,50,50,50,40,40,30,40,40,50};
    for (int n = 1; n <= 20; ++n) {
        char path[128];
        snprintf(path, sizeof(path), "data/Z/LEVEL%02d/LEVEL%02d.MAP", n, n);
        level_t map = {0};
        if (!Z_LoadLevel(path, &map)) return fail("campaign settings load");
        const zmap_t *native = map.native_data;
        if (native->computer.intelligence != intelligence[n - 1] ||
            native->computer.allocation != allocation[n - 1] ||
            native->computer.territory_weight != (n == 14 || n == 16 ? 0 : 10))
            return fail("CPUPLR per-level intelligence/allocation/importance weight");
        P_FreeLevel(&map);
    }
    puts("PASS: z all 20 campaign intelligence, reserve and territory settings");
    return 0;
}
static int test_arithmetic(void) {
    RtsGameModel *model = load();
    if (!model) return fail("valuation load");
    zmap_t *map = level.native_data;
    mobj_t *grunt = nth(MT_UNIT_GRUNT, 1, 0), *gun = nth(MT_UNIT_GATLING, 0, 0),
           *jeep = nth(MT_UNIT_JEEP, Z_NEUTRAL, 0);
    if (!grunt || !gun || !jeep) return fail("native valuation fixtures");
    /* 0x9830b vectors; Grunt = floor(1045/6) + floor(270*2/3).
     * Gatling's multiply-before-divide gives 1469, not stock's 1468. */
    if (Z_AIEstimate(1000,127) != 1000 || Z_AIEstimate(1000,55) != 969 ||
        Z_AIEstimate(1000,110) != 993 || Z_AIEstimate(32768,55) != 47360 ||
        Z_AIEstimate(65535,0) != 45568)
        return fail("perfect intelligence and repeatable errors");
    if (Z_AIUnitValue(grunt) != 354 || Z_AIUnitValue(gun) != 1469 || Z_AIEmptyValue(jeep) != 1138)
        return fail("native combat valuations differ from manufacturing stock");
    grunt->z.grenades = 1;
    if (Z_AIUnitValue(grunt) != 379) return fail("grenade adds native value 25");
    grunt->z.grenades = 0;
    grunt->hp /= 2;
    if (Z_AIUnitValue(grunt) != 349) return fail("live health affects perceived strength");
    if (Z_AIRank(0x10010,(ivec2_t){0,0},(ivec2_t){8,0},map->square_roots.bytes) != 0x10008 ||
        Z_AIRank(-1,(ivec2_t){0},(ivec2_t){0},map->square_roots.bytes))
        return fail("native distance adjusts the low word only");
    if (Z_AIMobileBudget(10000,0,0,0,40) != 6000 ||
        Z_AIMobileBudget(10000,1000,3000,1000,40) != 5200 ||
        Z_AIMobileBudget(10000,0,4000,8000,40) != 6000 ||
        Z_AIMobileBudget(10000,0,9000,12000,50) != 1000)
        return fail("native reserve formula and home strength correction");
    zai_t probe = {0};
    probe.total = 100; probe.mobile = 50; probe.territories[0].value = -354;
    if (Z_AIReserveValue(&probe, 0) != 2147483471u) return fail("unsigned negative reserve value");
    probe.mobile = 0; probe.territories[0].value = -1;
    if (Z_AIReserveValue(&probe, 0) != UINT32_MAX) return fail("unsigned reserve value maximum");
    probe.mobile = 100;
    if (Z_AIReserveValue(&probe, 0) != 0) return fail("zero reserve share");
    puts("PASS: z native integer ranking and reserve arithmetic");
    rts_game_model_destroy(model);
    return 0;
}

static int test_factory_choice(void) {
    RtsGameModel *model = load();
    if (!model) return fail("production choice load");
    zmap_t *map = level.native_data;
    mobj_t *fort = nth(MT_FORT,1,0);
    map->ai.territories[0].strength[0] = 10000;
    map->ai.territories[0].strength[1] = 1;
    const StaticProductDefinition *choice = Z_AIProduct(fort);
    if (!choice || choice->product_type != MT_UNIT_GRUNT) return fail("weaker army chooses low stock product");
    map->ai.territories[0].strength[0] = 1;
    map->ai.territories[0].strength[1] = 10000;
    choice = Z_AIProduct(fort);
    if (!choice || choice->product_type != MT_UNIT_JEEP) return fail("stronger army chooses higher stock product");
    puts("PASS: z native initial manufacturing selector follows army strength, not unit counts");
    rts_game_model_destroy(model);
    return 0;
}

static int test_intelligence_and_radar(void) {
    RtsGameModel *model = load();
    if (!model) return fail("observation load");
    zmap_t *map = level.native_data;
    /* One CPU territory and two human territories: neutral land does not
     * dilute the native <=40 percent test. 55 + floor(55/2) = 82. */
    map->territories[0].team = 0;
    think(0); think(1);
    if (map->ai.intelligence != 82 || map->ai.delay != 89)
        return fail("territory disadvantage raises effective intelligence and changes cadence");
    for (int t = 0; t < map->territory_count; ++t) map->territories[t].team = 1;
    for (int i = 0; nth(MT_FLAG,-1,i); ++i) Z_SetTeam(nth(MT_FLAG,-1,i),1);
    mobj_t *radar = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center(map->territories[4].flag),0),MT_RADAR);
    if (!radar) return fail("radar fixture");
    Z_SetTeam(radar,1);
    radar->z.building = 2;
    think(1);
    if (map->ai.intelligence != 55 || map->ai.territories[4].known)
        return fail("native radar scan skips an unaligned building slot");
    radar->z.building = 1;
    think(1);
    if (!map->ai.territories[4].known || map->ai.territories[1].known || map->ai.territories[7].known)
        return fail("aligned radar slot reveals ordinary territories, excluding forts");
    puts("PASS: z native intelligence boost and radar observation rules");
    rts_game_model_destroy(model);
    return 0;
}
static int test_initial_and_save(void) {
    RtsGameModel *model = load();
    if (!model) return fail("initial orders load");
    mobj_t *lead = nth(MT_UNIT_GRUNT, 1, 0);
    uint32_t id = lead->id;
    mobj_t *follower = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo != lead && mo->team == lead->team &&
            mo->z.squad == lead->z.squad && mo->z.squad_slot && mo->z.squad_slot != UINT8_MAX) {
            follower = mo; break;
        }
    }
    if (!follower) return fail("CPU squad follower fixture");
    uint32_t follower_id = follower->id;
    fixed2_t follower_start = fixed3_xy(follower->core.position);
    think(0);
    zmap_t *map = level.native_data;
    zaitask_t *task = task_of(lead);
    if (!task || task->target != 0 || !task->pending || task->execute_at != 144 || P_HasMoveOrder(lead))
        return fail("CPUPLR destination is queued with intelligence delay");
    if (!tick(model, 2 * RTS_TICRATE)) return fail("delay tick");
    task = task_of(P_MobjById(id));
    if (!task || !task->pending || P_HasMoveOrder(P_MobjById(id))) return fail("command waits 144 native frames");
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    uint32_t hash = G_ConsistencyExtra(0);
    zai_t saved = map->ai;
    if (!G_SaveGameBlob(&save, &size, "Native AI", &app, rts_game_model_ai(model), &hud)) return fail("save AI queue");
    if (!tick(model, RTS_TICRATE)) return fail("execute queued order");
    if (task_of(P_MobjById(id))->pending || !P_HasMoveOrder(P_MobjById(id))) return fail("queued command executes");
    follower = P_MobjById(follower_id);
    if (!follower || !P_MobjById(id)->z.formation_active || follower->z.route.active ||
        (!P_HasMoveOrder(follower) && fixed2_equal(follower_start, fixed3_xy(follower->core.position))))
        return fail("CPU order uses leader route and moving follower formation");
    if (!G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("restore AI queue");
    free(save);
    map = level.native_data;
    if (memcmp(&saved, &map->ai, sizeof(saved)) || G_ConsistencyExtra(0) != hash) return fail("restore timers, seed, scores and tasks");
    if (!tick(model, RTS_TICRATE) || task_of(P_MobjById(id))->pending || !P_HasMoveOrder(P_MobjById(id)))
        return fail("restored delayed command executes identically");
    puts("PASS: z initial native destinations, 70 Hz command delays and saved strategy state");
    rts_game_model_destroy(model);
    return 0;
}

/* One squad can reach two neutral territories. Give the farther one a
 * native importance value; nearest-flag behavior would pick the other. */
static mobj_t *scenario(void) {
    zmap_t *map = level.native_data;
    mobj_t *lead = nth(MT_UNIT_GRUNT, 1, 0);
    int squad = lead->z.squad;
    map->computer.intelligence = 127;
    map->computer.allocation = 0;
    for (int t = 0; t < map->territory_count; ++t) {
        map->territories[t].team = 1;
        map->territories[t].importance = t == 8 ? 200 : 0;
    }
    map->territories[2].team = map->territories[8].team = Z_NEUTRAL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        mo->traits &= ~MF_ATTACK;
        mo->z.home = 0;
        if (mo->type_id > MT_FORT && mo->type_id < MT_UNITS) mo->z.rating = 0;
        if (mo->type_id == MT_FLAG) Z_SetTeam(mo, mo->z.territory == 3 || mo->z.territory == 9 ? Z_NEUTRAL : 1);
        if (Z_Enterable(mo)) mo->z.driver = 1;
        if (mo->team == 1 && Z_UnitType(mo) >= 0 && mo->z.squad != squad) mo->hp = 0;
    }
    return lead;
}
static int test_scores(void) {
    RtsGameModel *model = load();
    if (!model) return fail("strategic ranking load");
    mobj_t *lead = scenario();
    think(0); think(1);
    zmap_t *map = level.native_data;
    zaitask_t *task = task_of(lead);
    if (!task || task->target != 8 || map->ai.territories[8].value != 32000 ||
        map->ai.territories[2].value != 0) return fail("farther valuable territory outranks nearby empty flag");
    int human = 0;
    for (int i = 0; nth(MT_UNIT_GRUNT, 0, i); ++i) human++;
    int unit = Z_AIUnitValue(nth(MT_UNIT_GRUNT,0,0));
    if (map->ai.territories[7].strength[0] != human * 3 * unit + Z_AIUnitValue(nth(MT_UNIT_GATLING,0,0)))
        return fail("native strength includes squad sum once per member");
    puts("PASS: z native value ranking supersedes nearest flag; native squad accounting retained");
    rts_game_model_destroy(model);
    return 0;
}

/* 0x987ee counts every present BUILD record, including a wreck. */
/* 0x98840 scores active slots by native object descriptor. */
/* 0x983e0 values mutable unit +2 after a body-damage threshold. */
static int test_damaged_accuracy_value(void) {
    RtsGameModel *model = load();
    if (!model) return fail("damaged accuracy value load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (Z_UnitType(mo) >= 0 && mo->team < Z_NEUTRAL) mo->hp = 0;
    }
    mobj_t *tank = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center((ivec2_t){25,25}), 0), MT_UNIT_MED_TANK);
    if (!tank) return fail("damaged accuracy value tank");
    Z_SetTeam(tank, 1);
    tank->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    tank->z.crew_count = 1;
    tank->z.crew[0].hp = mobjinfo[MT_UNIT_GRUNT].spawnhealth;
    tank->z.robot = lead->z.robot;
    tank->traits &= ~MF_ATTACK;
    map->territory_count = 1; map->fort_count = 0;
    map->territories[0].min = (ivec2_t){0,0};
    map->territories[0].max = (ivec2_t){level.width,level.height};
    map->territories[0].team = 1; map->territories[0].importance = 0;
    map->computer.intelligence = 127;
    P_DamageMobj(tank, NULL, tank->max_hp / 4);
    if (!tank->z.body_tracked || tank->z.body_accuracy >= Z_UnitAccuracy(tank->z.driver - 1))
        return fail("damage crosses native accuracy threshold");
    think(0); think(1);
    int damaged_value = Z_AIUnitValue(tank);
    int damaged_strength = map->ai.territories[0].strength[1];
    uint8_t damaged_accuracy = tank->z.body_accuracy;
    tank->z.body_accuracy = (uint8_t)Z_UnitAccuracy(tank->z.driver - 1);
    think(1);
    int restored_value = Z_AIUnitValue(tank);
    if (restored_value <= damaged_value || damaged_strength != damaged_value ||
        map->ai.territories[0].strength[1] != restored_value)
        return fail("mutable accuracy affects AI force estimate at unchanged body HP");
    tank->z.body_accuracy = damaged_accuracy;
    uint8_t damaged_speed = tank->z.body_speed, damaged_fire = tank->z.body_fire;
    const uint8_t *weapon = map->roster.bytes + 22000 + (size_t)Z_UnitType(tank) * 42;
    if (weapon[1] != 1 || map->weights.bytes[0x3b + 4] != 2 ||
        damaged_speed >= weapon[4] || damaged_fire >= weapon[34])
        return fail("Medium weight and native body speed/fire bytes");
    int damaged = Z_AIUnitValue(tank);
    tank->z.body_speed = weapon[4];
    think(1);
    int faster = Z_AIUnitValue(tank);
    if (faster <= damaged || map->ai.territories[0].strength[1] != faster)
        return fail("mutable speed affects AI force estimate at unchanged body HP");
    tank->z.body_speed = damaged_speed;
    tank->z.body_fire = weapon[34];
    think(1);
    int faster_fire = Z_AIUnitValue(tank);
    if (faster_fire <= damaged || map->ai.territories[0].strength[1] != faster_fire)
        return fail("mutable body fire byte affects AI force estimate at unchanged body HP");
    tank->z.body_fire = damaged_fire;
    rts_game_model_destroy(model);
    puts("PASS: z body accuracy, speed and fire rate affect strategic force value");
    return 0;
}

static int test_driver_health_value(void) {
    RtsGameModel *model = load();
    if (!model) return fail("driver value load");
    mobj_t *driver = nth(MT_UNIT_GRUNT, 1, 0), *jeep = nth(MT_UNIT_JEEP, Z_NEUTRAL, 0);
    if (!driver || !jeep) return fail("driver value fixtures");
    driver->core.position = jeep->core.position;
    driver->hp = driver->max_hp * 2 / 3;
    int wounded = driver->hp;
    if (!Z_DetourBoard(driver, jeep) || jeep->z.crew[0].hp != wounded)
        return fail("wounded driver boards Jeep");
    int wounded_value = Z_AIUnitValue(jeep);
    jeep->z.crew[0].hp = mobjinfo[MT_UNIT_GRUNT].spawnhealth;
    int full_value = Z_AIUnitValue(jeep);
    jeep->z.crew[0].hp = wounded;
    if (full_value <= wounded_value) return fail("boarding preserves wounded driver value");
    mobj_t *shooter = P_SpawnMobj(jeep->core.position, MT_UNIT_SNIPER);
    if (!shooter) return fail("driver value shooter");
    Z_SetTeam(shooter, 0);
    int body_hp = jeep->hp;
    for (int n = 0; n < 256 && !jeep->z.crew_hurt; ++n, ++leveltime)
        Z_HitDamage(shooter, &shooter->info->attack, jeep, 5, 1);
    if (jeep->z.crew_hurt != 5 || jeep->hp != body_hp || Z_AIUnitValue(jeep) >= wounded_value)
        return fail("driver hit reduces value without body damage");
    rts_game_model_destroy(model);
    puts("PASS: z AI values boarded driver health and subsequent driver hits");
    return 0;
}

static int test_apc_driver_value(void) {
    RtsGameModel *model = load();
    if (!model) return fail("APC driver value load");
    mobj_t *reference = nth(MT_UNIT_GRUNT, 1, 0);
    fixed3_t at = fixed3_from_fixed2(fixed2_cell_center((ivec2_t){25,25}), 0);
    mobj_t *apc = P_SpawnMobj(at, MT_UNIT_APC), *sniper = P_SpawnMobj(at, MT_UNIT_SNIPER);
    if (!reference || !apc || !sniper) return fail("APC driver value fixtures");
    Z_SetTeam(apc, Z_NEUTRAL); Z_SetTeam(sniper, 1);
    sniper->z.robot = reference->z.robot;
    apc->traits &= ~(MF_MOBILE | MF_ATTACK);
    P_SetMobjState(apc, S_A_P_C);
    if (!Z_DetourBoard(sniper, apc)) return fail("Sniper boards APC for AI value");
    int sniper_value = Z_AIUnitValue(apc);
    apc->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    apc->z.crew[0].hp = mobjinfo[MT_UNIT_GRUNT].spawnhealth;
    int grunt_value = Z_AIUnitValue(apc);
    apc->z.driver = MT_UNIT_LASER - MT_UNITS + 1;
    apc->z.crew[0].hp = mobjinfo[MT_UNIT_LASER].spawnhealth;
    int laser_value = Z_AIUnitValue(apc);
    if (sniper_value - grunt_value != 12 || laser_value - grunt_value != 8)
        return fail("APC AI range follows driver weapon");
    apc->z.driver = MT_UNIT_PSYCHO - MT_UNITS + 1;
    P_DamageMobj(apc, NULL, apc->max_hp / 4);
    if (!apc->z.body_tracked || apc->z.body_fire != 88)
        return fail("damaged APC starts from driver's fire byte");
    apc->z.driver = MT_UNIT_SNIPER - MT_UNITS + 1;
    apc->z.crew[0].hp = mobjinfo[MT_UNIT_SNIPER].spawnhealth;
    if (!Z_EjectCrew(apc)) return fail("damaged APC driver exits");
    mobj_t *psycho = P_SpawnMobj(apc->core.position, MT_UNIT_PSYCHO);
    if (!psycho) return fail("replacement APC driver");
    Z_SetTeam(psycho, 1); psycho->z.robot = reference->z.robot;
    if (!Z_DetourBoard(psycho, apc) || apc->z.body_fire != 98)
        return fail("replacement APC driver restores driver's fire byte");
    P_DamageMobj(apc, NULL, apc->max_hp / 4);
    if (apc->z.body_fire != 83 || apc->z.body_speed != 38 || apc->z.body_base_speed != 45)
        return fail("reboarded APC retains its boarding speed baseline");
    uint32_t apc_id = apc->id, hash = G_Consistency();
    apc->z.body_base_speed++;
    if (G_Consistency() == hash) return fail("repair baseline contributes to consistency");
    apc->z.body_base_speed--;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
    if (!G_SaveGameBlob(&save, &bytes, "APC repair baseline", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud))
        return fail("save APC repair baseline");
    free(save); apc = P_MobjById(apc_id);
    if (!apc || apc->z.body_base_speed != 45) return fail("restore APC repair baseline");
    Z_RepairBody(apc);
    if (apc->z.body_speed != 45 || apc->z.body_fire != 83 || apc->z.body_accuracy != Z_UnitAccuracy(MT_UNIT_PSYCHO - MT_UNITS))
        return fail("repair restores reboarded speed and driver accuracy only");
    rts_game_model_destroy(model);
    puts("PASS: z APC strategic range and damaged body fire follow driver");
    return 0;
}

static int test_empty_object_value_slots(void) {
    RtsGameModel *model = load();
    if (!model) return fail("empty object value load");
    scenario();
    zmap_t *map = level.native_data;
    mobj_t *jeep = nth(MT_UNIT_JEEP, Z_NEUTRAL, 0);
    if (!jeep) return fail("empty object value jeep");
    jeep->z.driver = 0;
    if (!Z_Enterable(jeep)) return fail("empty object value boarding fixture");
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (Z_UnitType(mo) >= 0 && mo->team < Z_NEUTRAL) mo->hp = 0;
        if (mo != jeep && Z_Enterable(mo)) mo->z.driver = 1;
    }
    map->territory_count = 1; map->fort_count = 0;
    map->territories[0].min = (ivec2_t){0,0};
    map->territories[0].max = (ivec2_t){level.width,level.height};
    map->territories[0].team = 1; map->territories[0].importance = 0;
    map->computer.intelligence = 127;
    int value = Z_AIEmptyValue(jeep);
    if (value <= 0 || !jeep->z.object_slot) return fail("empty object value fixture slot");
    uint8_t slot = jeep->z.object_slot, object = jeep->z.object;
    think(0); think(1);
    int baseline = map->ai.territories[0].value;
    jeep->z.object_slot = 0;
    map->ai.phase = 1; think(1);
    if (map->ai.territories[0].value != baseline - value || !Z_Enterable(jeep))
        return fail("unslotted boardable hull does not add effect value");
    jeep->z.object_slot = slot; jeep->z.object = MT_TRUCK;
    map->ai.phase = 1; think(1);
    if (map->ai.territories[0].value != baseline - value)
        return fail("unlisted object descriptor does not add effect value");
    jeep->z.object = object;
    map->ai.phase = 1; think(1);
    if (map->ai.territories[0].value != baseline)
        return fail("listed slotted object restores effect value");
    rts_game_model_destroy(model);
    puts("PASS: z strategic empty-object value follows native effect slots");
    return 0;
}

/* 0x98a80 reads the represented effect's health byte directly. */
static int test_ejected_object_health_value(void) {
    RtsGameModel *model = load();
    if (!model) return fail("ejected object value load");
    fixed2_t at = fixed2_cell_center((ivec2_t){25,25});
    mobj_t *hull = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_UNIT_APC);
    if (!hull) return fail("ejected object value spawn");
    Z_SetTeam(hull, 1);
    hull->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    P_DamageMobj(hull, NULL, hull->max_hp / 4);
    if (!hull->z.body_tracked || hull->z.body_speed != 45)
        return fail("damaged APC retains native speed");
    hull->hp = 1;
    if (!Z_EjectCrew(hull) || !hull->z.object_slot || hull->z.object_hp != 1 || hull->hp != 1)
        return fail("ejected object value fixture");
    zmap_t *map = level.native_data;
    map->territory_count = 1; map->fort_count = 0;
    map->territories[0].min = (ivec2_t){0,0};
    map->territories[0].max = (ivec2_t){level.width,level.height};
    map->territories[0].team = 1; map->territories[0].importance = 0;
    map->computer.intelligence = 127;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id > MT_FORT && mo->type_id < MT_UNITS) mo->z.rating = 0;
        if (mo != hull && Z_Enterable(mo)) mo->z.driver = 1;
        if (mo->team < Z_NEUTRAL && Z_UnitType(mo) >= 0) mo->hp = 0;
    }
    think(0); think(1);
    int before = Z_AIEmptyValue(hull), score = map->ai.territories[0].value;
    hull->z.body_speed = 50;
    map->ai.phase = 1; think(1);
    if (Z_AIEmptyValue(hull) <= before || map->ai.territories[0].value <= score)
        return fail("empty APC valuation uses retained speed");
    hull->z.body_speed = 45;
    map->ai.phase = 1; think(1);
    if (map->ai.territories[0].value != score) return fail("empty APC speed restores value");
    hull->hp = 25;
    map->ai.phase = 1; think(1);
    int after = Z_AIEmptyValue(hull);
    if (!before || before != after || map->ai.territories[0].value != score)
        return fail("effect health, not hull health, determines AI value");
    hull->z.object_hp = 2;
    map->ai.phase = 1; think(1);
    if (Z_AIEmptyValue(hull) <= after || map->ai.territories[0].value <= score)
        return fail("effect health changes AI territory value");
    mobj_t *gun = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_UNIT_LIGHT_GUN);
    if (!gun) return fail("empty gun value spawn");
    Z_SetTeam(gun, 1); gun->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    P_DamageMobj(gun, NULL, gun->max_hp / 4);
    mobj_t *shooter = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_UNIT_SNIPER);
    if (!shooter) return fail("empty gun value shooter");
    Z_SetTeam(shooter, 0);
    for (int n = 0; n < 256 && gun->z.driver; ++n, ++leveltime)
        Z_HitDamage(shooter, &shooter->info->attack, gun, 255, 1);
    if (!gun->z.body_tracked || gun->z.body_fire != 54 || gun->z.driver ||
        gun->remove || !gun->z.object_slot)
        return fail("fatal driver hit retains damaged gun fire byte");
    shooter->hp = 0;
    map->ai.phase = 1; think(1);
    int damaged_score = map->ai.territories[0].value;
    int damaged_gun = Z_AIEmptyValue(gun);
    gun->z.body_fire = 60;
    map->ai.phase = 1; think(1);
    if (Z_AIEmptyValue(gun) <= damaged_gun || map->ai.territories[0].value <= damaged_score)
        return fail("empty gun valuation uses retained fire byte");
    gun->z.body_fire = 54;
    int saved_hull = Z_AIEmptyValue(hull);
    uint32_t hull_id = hull->id, gun_id = gun->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
    if (!G_SaveGameBlob(&save, &bytes, "empty values", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud))
        return fail("save damaged empty object values");
    free(save);
    hull = P_MobjById(hull_id); gun = P_MobjById(gun_id);
    if (!hull || !gun || hull->z.body_speed != 45 || gun->z.body_fire != 54 || Z_AIEmptyValue(hull) != saved_hull ||
        Z_AIEmptyValue(gun) != damaged_gun)
        return fail("saved empty object stats affect valuation after load");
    rts_game_model_destroy(model);
    puts("PASS: z empty-object valuation reads represented health");
    return 0;
}

static int test_destroyed_building_value(void) {
    RtsGameModel *model = load();
    if (!model) return fail("destroyed building value load");
    scenario();
    zmap_t *map = level.native_data;
    mobj_t *factory = nth(MT_ROBOT_FACTORY, -1, 0);
    if (!factory) return fail("destroyed building value factory");
    irect_t cells = P_MobjCells(factory);
    int territory = Z_AITerritory(map, fixed2_from_cell((ivec2_t){cells.x,cells.y}));
    if (territory < 0) return fail("destroyed building value territory");
    for (int t = 0; t < map->territory_count; ++t) map->territories[t].importance = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->team == 0 && Z_UnitType(mo) >= 0) mo->hp = 0;
    }
    factory->z.rating = 1234;
    think(0); think(1);
    int before = map->ai.territories[territory].value;
    if (before != 1234) return fail("live building contributes its rating");
    P_DamageMobj(factory, NULL, factory->hp);
    if (factory->hp > 0 || factory->remove || factory->z.rating != 1234)
        return fail("building persists as a rated wreck");
    map->ai.phase = 1;
    think(1);
    if (map->ai.territories[territory].value != before)
        return fail("destroyed building retains its territory rating");
    rts_game_model_destroy(model);
    puts("PASS: z destroyed building retains strategic territory value");
    return 0;
}

static int test_blocked_flag_retry(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load blocked flag fixture");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    ivec2_t flag = map->territories[8].flag;
    ivec2_t start = ivec2_add(flag, (ivec2_t){4, 0});
    if (!L_Contains(&level, start.x, start.y)) return fail("blocked flag fixture bounds");
    map->territory_count = 1;
    map->fort_count = 0;
    map->territories[0].min = (ivec2_t){0, 0};
    map->territories[0].max = (ivec2_t){level.width, level.height};
    map->territories[0].flag = flag;
    map->territories[0].team = Z_NEUTRAL;
    map->ai.territories[0].flags = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->team == 1 && mo->z.squad == lead->z.squad) {
            mo->core.position = fixed3_from_fixed2(fixed2_cell_center(start), 0);
            P_ClearMove(mo);
        }
    }
    level.blocked[L_Index(&level, flag.x, flag.y)] = 1;
    P_NavFree(&level);
    int cls = P_MobjMoveClass(lead);
    if (P_NavReachable(&level, cls, P_NavNodeAt(&level, cls, fixed3_xy(lead->core.position)),
                       P_NavNodeAt(&level, cls, fixed2_cell_center(flag))))
        return fail("flag fixture destination is unreachable");
    think(0);
    zaitask_t *task = task_of(lead);
    if (!task || task->current != 0) return fail("register blocked flag group");
    task->retry = 0;
    map->ai.delay = 1000;
    map->ai.fraction = 0;
    think(1);
    if (task->retry != 99 || P_HasMoveOrder(lead))
        return fail("blocked flag correction starts native retry countdown");
    level.blocked[L_Index(&level, flag.x, flag.y)] = 0;
    P_NavFree(&level);
    for (int i = 0; i < 30; ++i) {
        think(1);
        if (P_HasMoveOrder(lead) || task->retry <= 0) return fail("opened flag waits for retry countdown");
    }
    for (int i = 0; i < 30 && !P_HasMoveOrder(lead); ++i) think(1);
    if (!P_HasMoveOrder(lead)) return fail("flag correction retries after countdown");
    rts_game_model_destroy(model);
    return 0;
}
static int test_strength_gate(void) {
    RtsGameModel *model = load();
    if (!model) return fail("force gate load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    /* Make every other territory friendly and make the desired enemy too
     * strong: importance adds to the native required-force calculation. */
    map->territories[2].team = 1;
    for (int i = 0; nth(MT_FLAG,-1,i); ++i)
        if (nth(MT_FLAG,-1,i)->z.territory == 3) Z_SetTeam(nth(MT_FLAG,-1,i),1);
    mobj_t *enemy = nth(MT_UNIT_GRUNT,0,0);
    enemy->core.position = fixed3_from_fixed2(fixed2_cell_center(map->territories[8].flag),0);
    think(0); think(1);
    /* An idle force inside its own land may still move up to the
     * frontier (0x98d95), but nothing marches on the strong territory. */
    const zaitask_t *t = task_of(lead);
    if (t->destination == 8 || t->target == 8 || (t->task >= 0 && t->task <= 4))
        return fail("insufficient combined force leaves target unassigned");
    puts("PASS: z strategic orders require enough squad strength");
    rts_game_model_destroy(model);
    return 0;
}

/* 0x9a126 ranks available forces without testing the flag route. */
/* 0x9a126 includes idle task records even if an older order is queued. */
static int test_pending_assault_force(void) {
    RtsGameModel *model = load();
    if (!model) return fail("pending assault load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->territories[2].team = 1;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG && mo->z.territory == 3) Z_SetTeam(mo, 1);
        if (mo->team == 0 && Z_UnitType(mo) >= 0) mo->hp = 0;
    }
    map->computer.intelligence = 126;
    think(0);
    zaitask_t *task = task_of(lead);
    if (!task) return fail("pending assault register");
    task->pending = true;
    task->execute_at = map->ai.clock + 1000;
    task->order = fixed3_xy(lead->core.position);
    map->ai.delay = 0;
    think(1);
    if (task->task != 0 || task->target != 8 || task->destination != 8 ||
        !task->pending || task->execute_at >= map->ai.clock + 1000)
        return fail("idle force with queued order joins assault");
    rts_game_model_destroy(model);
    puts("PASS: z queued idle force joins assault pool");
    return 0;
}

/* 0x991be assigns just the first group after checking sufficient strength. */
/* 0x999e5 uses unsigned multiplication even for a negative territory value. */
static int test_negative_garrison_value(void) {
    RtsGameModel *model = load();
    if (!model) return fail("negative garrison reserve load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->computer.intelligence = 127; map->computer.allocation = 50;
    map->computer.territory_weight = 10;
    for (int t = 0; t < map->territory_count; ++t) {
        map->territories[t].team = 1; map->territories[t].importance = 0;
    }
    fixed3_t at = fixed3_from_fixed2(fixed2_cell_center(map->territories[4].flag), 0);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) Z_SetTeam(mo, 1);
        if (mo->team == 0 && Z_UnitType(mo) >= 0) mo->hp = 0;
        if (mo->team == 1 && mo->z.squad == lead->z.squad) mo->core.position = at;
    }
    mobj_t *reserve = P_SpawnMobj(at, MT_UNIT_HEAVY_TANK);
    mobj_t *enemy = P_SpawnMobj(at, MT_UNIT_HEAVY_TANK);
    if (!reserve || !enemy) return fail("negative garrison reserve spawn");
    Z_SetTeam(reserve, 1); Z_SetTeam(enemy, 0);
    reserve->z.driver = enemy->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    reserve->z.robot = enemy->z.robot = lead->z.robot;
    reserve->traits &= ~MF_ATTACK; enemy->traits &= ~MF_ATTACK;
    think(0);
    zaitask_t *task = task_of(lead);
    if (!task || task->current != 4) return fail("negative garrison register");
    task->task = 4; task->target = 4;
    map->ai.delay = 0; map->ai.fraction = 0;
    think(1);
    uint32_t worth = Z_AIReserveValue(&map->ai, 4);
    if (!(task->flags & 16) || map->ai.territories[4].value >= 0 || worth < 500 ||
        (worth & 0x80000000u) || task->task != 4 || task->target != 4)
        return fail("negative territory retains garrison when native signed result is positive");
    rts_game_model_destroy(model);
    puts("PASS: z garrison uses native unsigned reserve valuation");
    return 0;
}

static int test_garrison_one_per_pass(void) {
    RtsGameModel *model = load();
    if (!model) return fail("garrison pass load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->computer.intelligence = 126;
    map->computer.allocation = 50;
    map->computer.territory_weight = 10;
    map->territory_count = 2;
    map->fort_count = 0;
    for (int t = 0; t < 2; ++t) {
        map->territories[t].min = (ivec2_t){t * 50, 0};
        map->territories[t].max = (ivec2_t){(t + 1) * 50, 50};
        map->territories[t].flag = (ivec2_t){t * 50 + 25, 25};
        map->territories[t].team = t ? 0 : 1;
        map->territories[t].importance = t ? 0 : 8;
        map->territories[t].options = 0;
        map->ai.territories[t].flags = 0;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) P_RemoveMobj(mo);
        if (mo->team == 1 && Z_UnitType(mo) >= 0 && mo != lead) mo->hp = 0;
        if (mo->team == 0 && Z_UnitType(mo) >= 0)
            mo->core.position = fixed3_from_fixed2(fixed2_cell_center(map->territories[1].flag), 0);
    }
    lead->z.squad = 0;
    lead->core.position = fixed3_from_fixed2(fixed2_cell_center(map->territories[0].flag), 0);
    for (int i = 0; i < 4; ++i) {
        mobj_t *mo = P_SpawnMobj(lead->core.position, MT_UNIT_GRUNT);
        if (!mo) return fail("garrison pass group spawn");
        Z_SetTeam(mo, 1);
        mo->z.robot = lead->z.robot;
        mo->traits &= ~MF_ATTACK;
        mo->z.squad = 0;
    }
    think(0);
    map->ai.delay = 0;
    map->ai.fraction = 0;
    think(1);
    int guards = 0, idle = 0, strength = 0;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &map->ai.tasks[i];
        if (!task->unit) continue;
        if (task->task == 4) { guards++; strength = task->strength; }
        if (task->task == -1 && (task->flags & 16)) idle++;
    }
    int wanted = Z_AIReserveValue(&map->ai, 0);
    if (guards != 1 || idle < 1 || wanted <= strength)
        return fail("one group holds despite a larger required force");
    map->ai.delay = 0;
    map->ai.fraction = 0;
    think(1);
    guards = 0;
    for (int i = 0; i < Z_AI_TASKS; ++i) guards += map->ai.tasks[i].unit && map->ai.tasks[i].task == 4;
    if (guards != 2) return fail("next pass assigns the next garrison");
    rts_game_model_destroy(model);
    puts("PASS: z garrisons add one group per heavy pass");
    return 0;
}

static int test_blocked_assault_flag(void) {
    RtsGameModel *model = load();
    if (!model) return fail("blocked assault flag load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->territories[2].team = 1;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG && mo->z.territory == 3) Z_SetTeam(mo, 1);
        if (mo->team == 0 && Z_UnitType(mo) >= 0) mo->hp = 0;
    }
    ivec2_t flag = map->territories[8].flag;
    level.blocked[L_Index(&level, flag.x, flag.y)] = 1;
    P_NavFree(&level);
    int cls = P_MobjMoveClass(lead);
    if (P_NavReachable(&level, cls, P_NavNodeAt(&level, cls, fixed3_xy(lead->core.position)),
                       P_NavNodeAt(&level, cls, fixed2_cell_center(flag))))
        return fail("assault flag fixture is unreachable");
    think(0); think(1);
    zaitask_t *task = task_of(lead);
    if (!task || task->task != 0 || task->target != 8 || task->destination != 8 ||
        (!task->pending && !P_HasMoveOrder(lead)))
        return fail("blocked flag does not exclude force from assault staging");
    rts_game_model_destroy(model);
    puts("PASS: z assault force selection does not require flag route");
    return 0;
}

static int test_fort_followup(void) {
    RtsGameModel *model = load();
    if (!model) return fail("fort followup load");
    think(0);
    zmap_t *map = level.native_data;
    mobj_t *lead = nth(MT_UNIT_GRUNT, 1, 0), *units[4];
    if (!lead || !(map->ai.territories[7].flags & 2)) return fail("human fort fixture");
    memset(map->ai.tasks, 0, sizeof(map->ai.tasks));
    map->ai.intelligence = 127;
    for (int i = 0; i < 4; ++i) {
        units[i] = P_SpawnMobj(lead->core.position, MT_UNIT_MED_TANK);
        if (!units[i]) return fail("spawn fort followup unit");
        Z_SetTeam(units[i], 1);
        map->ai.tasks[i] = (zaitask_t){.unit=units[i]->id, .current=i == 1 ? 0 : 4,
            .task=i == 3 ? 3 : 4, .target=i == 3 ? 7 : 4, .destination=i == 3 ? 7 : 4,
            .flags=i == 2 ? 8 : 24};
    }
    Z_AIFortFollowup(map, 7);
    if (map->ai.tasks[0].task != 3 || map->ai.tasks[0].target != 7 ||
        map->ai.tasks[0].destination != 7 || !map->ai.tasks[0].pending)
        return fail("adjacent mobile force joins fort assault");
    if (map->ai.tasks[1].task != 4 || map->ai.tasks[1].pending ||
        map->ai.tasks[2].task != 4 || map->ai.tasks[2].pending ||
        map->ai.tasks[3].task != 3 || map->ai.tasks[3].pending)
        return fail("distant, nonmobile and already assigned forces stay unchanged");
    ivec2_t entrance = map->forts[1];
    map->forts[1] = (ivec2_t){-10000, -10000};
    map->ai.tasks[0] = (zaitask_t){.unit=units[0]->id, .current=4, .task=4,
        .target=4, .destination=4, .flags=24};
    Z_AIFortFollowup(map, 7);
    if (map->ai.tasks[0].task != 3 || map->ai.tasks[0].target != 7 ||
        map->ai.tasks[0].destination != 7 || map->ai.tasks[0].pending)
        return fail("fort followup records assignment when no staging point is reachable");
    map->forts[1] = entrance;
    const zterritory_t fort = map->territories[7];
    map->territories[0].min = ivec2_add(fort.min, (ivec2_t){1,1});
    map->territories[0].max = ivec2_sub(fort.max, (ivec2_t){1,1});
    map->territories[1].min = ivec2_sub(fort.min, (ivec2_t){1,1});
    map->territories[1].max = ivec2_sub(fort.max, (ivec2_t){2,2});
    map->territories[2].min = (ivec2_t){fort.min.x - 8, fort.min.y + 2};
    map->territories[2].max = (ivec2_t){fort.min.x, fort.max.y - 2};
    map->territories[3].min = fort.max;
    map->territories[3].max = ivec2_add(fort.max, (ivec2_t){8,8});
    for (int i = 0; i < 4; ++i)
        map->ai.tasks[i] = (zaitask_t){.unit=units[i]->id, .current=i, .task=4,
            .target=i, .destination=i, .flags=24};
    Z_AIFortFollowup(map, 7);
    if (map->ai.tasks[0].pending || map->ai.tasks[1].pending ||
        !map->ai.tasks[2].pending || !map->ai.tasks[3].pending)
        return fail("fort adjacency excludes overlap and includes touching edges and corners");
    rts_game_model_destroy(model);

    model = load();
    if (!model) return fail("load fort attack integration");
    lead = scenario(); map = level.native_data;
    for (int t = 0; t < map->territory_count; ++t) {
        map->territories[t].team = t == 7 ? 0 : 1;
        map->territories[t].importance = 0;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) Z_SetTeam(mo, mo->z.territory == 8 ? 0 : 1);
        if (mo->team == 0 && Z_UnitType(mo) >= 0) mo->hp = 0;
    }
    mobj_t *near = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center(map->territories[4].flag), 0), MT_UNIT_MED_TANK);
    if (!near) return fail("spawn nearby fort support");
    Z_SetTeam(near, 1); near->hp = near->max_hp = 200;
    near->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1; near->z.robot = lead->z.robot; near->traits &= ~MF_ATTACK;
    for (int i = 0; i < 10; ++i) {
        mobj_t *enemy = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center(map->territories[8].flag), 0), MT_UNIT_HEAVY_TANK);
        if (!enemy) return fail("spawn opposing army fixture");
        Z_SetTeam(enemy, 0); enemy->hp = enemy->max_hp = 300;
        enemy->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1; enemy->z.robot = lead->z.robot; enemy->traits &= ~MF_ATTACK;
    }
    think(0);
    zaitask_t *support = task_of(near);
    if (!support) return fail("register fort support task");
    support->task = 4; support->target = support->destination = 4;
    map->ai.delay = 0;
    think(1);
    if (task_of(lead)->target != 7 || support->task != 3 || support->target != 7)
        return fail("normal fort attack pulls adjacent active task into assault");
    rts_game_model_destroy(model);
    return 0;
}

static int test_unreachable_staging(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load staging fixture");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->computer.allocation = 0;
    map->computer.intelligence = 126;
    map->territories[7].team = 0;
    fixed2_t at = fixed2_cell_center(map->territories[4].flag);
    mobj_t *other = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_UNIT_MED_TANK);
    if (!other) return fail("spawn staging peer");
    Z_SetTeam(other, 1);
    other->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    other->z.robot = lead->z.robot;
    other->traits &= ~MF_ATTACK;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->team == 1 && mo->z.squad == lead->z.squad)
            mo->core.position = fixed3_from_fixed2(at, 0);
        else if (mo->team == 0 && Z_UnitType(mo) >= 0) mo->hp = 0;
    }
    think(0);
    zaitask_t *first = task_of(lead), *second = task_of(other);
    if (!first || !second) return fail("register staging tasks");
    first->task = 0;
    second->task = 1;
    first->target = second->target = first->destination = second->destination = 7;
    first->pending = false;
    second->pending = true;
    second->execute_at = map->ai.clock + 10000;
    map->territories[4].flag = (ivec2_t){-1000, -1000};
    map->ai.delay = 0;
    think(1);
    if (first->task != 2 || first->target != 4 || first->destination != 7 || first->pending)
        return fail("staging assignment survives rejected staging point");
    rts_game_model_destroy(model);
    return 0;
}

static int test_unreachable_flag(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load flag arrival fixture");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    fixed2_t at = fixed2_cell_center(map->territories[8].flag);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->team == 1 && mo->z.squad == lead->z.squad)
            mo->core.position = fixed3_from_fixed2(at, 0);
        else if (mo->team == 0 && Z_UnitType(mo) >= 0) mo->hp = 0;
    }
    think(0);
    zaitask_t *task = task_of(lead);
    if (!task || task->current != 8) return fail("register group at enemy territory");
    task->task = 3;
    task->target = task->destination = 8;
    task->pending = false;
    map->territories[8].flag = (ivec2_t){-1000, -1000};
    map->ai.delay = 0;
    think(1);
    if (task->task != 1 || task->target != 8 || task->destination != 8 || task->pending)
        return fail("flag assignment survives rejected flag destination");
    rts_game_model_destroy(model);
    return 0;
}

static int test_unreachable_initial_staging(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load initial staging fixture");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    for (int t = 0; t < map->territory_count; ++t) {
        map->territories[t].team = t == 7 ? 0 : 1;
        map->territories[t].importance = 0;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) Z_SetTeam(mo, mo->z.territory == 8 ? 0 : 1);
        if (mo->team == 0 && Z_UnitType(mo) >= 0) mo->hp = 0;
    }
    mobj_t *support = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center(map->territories[4].flag), 0), MT_UNIT_MED_TANK);
    if (!support) return fail("spawn initial staging support");
    Z_SetTeam(support, 1);
    support->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    support->z.robot = lead->z.robot;
    support->traits &= ~MF_ATTACK;
    think(0);
    zaitask_t *first = task_of(lead), *near = task_of(support);
    if (!first || !near) return fail("register initial staging groups");
    near->task = 4;
    near->target = near->destination = 4;
    map->territories[7].flag = fixed2_cell(fixed3_xy(lead->core.position));
    map->forts[1] = (ivec2_t){-10000, -10000};
    map->ai.delay = 0;
    think(1);
    if (first->task != 0 || first->destination != 7 || first->target != 7 || first->pending ||
        near->task != 3 || near->target != 7 || near->destination != 7 || near->pending)
        return fail("initial assignment and followup survive rejected staging point");
    rts_game_model_destroy(model);
    return 0;
}

/* 0x9cb5e does not test delayed commands before sending a quiet reserve home. */
static int test_pending_reserve_home(void) {
    RtsGameModel *model = load();
    if (!model) return fail("pending reserve home load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->computer.allocation = 100;
    map->computer.intelligence = 126;
    for (int t = 0; t < map->territory_count; ++t) {
        map->territories[t].team = t == 8 ? Z_NEUTRAL : t == 2 ? 0 : 1;
        map->territories[t].importance = 0;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) Z_SetTeam(mo, mo->z.territory == 9 ? Z_NEUTRAL : mo->z.territory == 3 ? 0 : 1);
        if (mo->team == 0 && Z_UnitType(mo) >= 0) mo->hp = 0;
        if (mo->team == 1 && mo->z.squad == lead->z.squad) {
            mo->core.position = fixed3_from_fixed2(fixed2_cell_center(map->territories[8].flag), 0);
            mo->z.manufactured = false;
            P_ClearMove(mo);
        }
    }
    mobj_t *enemy = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center(map->territories[2].flag), 0), MT_UNIT_HEAVY_TANK);
    if (!enemy) return fail("pending reserve home enemy");
    Z_SetTeam(enemy, 0);
    enemy->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    enemy->z.robot = lead->z.robot;
    enemy->traits &= ~MF_ATTACK;
    think(0);
    zaitask_t *task = task_of(lead);
    if (!task || task->current != 8) return fail("pending reserve home register");
    task->home = 4;
    task->task = -1;
    task->pending = true;
    task->execute_at = map->ai.clock + 1000;
    task->order = fixed2_cell_center(map->territories[4].flag);
    map->ai.delay = 0; map->ai.fraction = 0;
    think(1);
    if ((task->flags & 16) || !task->pending || task->execute_at >= map->ai.clock + 1000 ||
        Z_AITerritory(map, task->order) != 4)
        return fail("quiet reserve replaces pending command with home order");
    rts_game_model_destroy(model);
    puts("PASS: z quiet reserve returns home despite pending order");
    return 0;
}

/* Several return orders can delay the last past the next heavy AI pass. */
static int test_home_order_backlog(void) {
    RtsGameModel *model = load();
    if (!model) return fail("home backlog load");
    mobj_t *lead = scenario(), *last = NULL;
    zmap_t *map = level.native_data;
    map->computer.allocation = 100;
    map->computer.intelligence = 50;
    for (int t = 0; t < map->territory_count; ++t) {
        map->territories[t].team = t == 8 ? Z_NEUTRAL : t == 2 ? 0 : 1;
        map->territories[t].importance = 0;
    }
    fixed3_t start = fixed3_from_fixed2(fixed2_cell_center(map->territories[8].flag), 0);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) Z_SetTeam(mo, mo->z.territory == 9 ? Z_NEUTRAL : mo->z.territory == 3 ? 0 : 1);
        if (mo->team == 0 && Z_UnitType(mo) >= 0) mo->hp = 0;
        if (mo->team == 1 && mo->z.squad == lead->z.squad) {
            mo->core.position = start;
            mo->z.manufactured = false;
            P_ClearMove(mo);
        }
    }
    for (int i = 0; i < 3; ++i) {
        last = P_SpawnMobj(start, MT_UNIT_GRUNT);
        mobj_t *enemy = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center(map->territories[2].flag), 0), MT_UNIT_HEAVY_TANK);
        if (!last || !enemy) return fail("home backlog spawn");
        Z_SetTeam(last, 1); Z_SetTeam(enemy, 0);
        last->z.robot = enemy->z.robot = lead->z.robot;
        enemy->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
        last->traits &= ~MF_ATTACK; enemy->traits &= ~MF_ATTACK;
    }
    think(0);
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &map->ai.tasks[i];
        if (task->unit && task->current == 8) { task->home = 4; task->task = -1; task->pending = false; }
    }
    map->ai.delay = 0; map->ai.fraction = 0;
    think(1);
    zaitask_t *task = task_of(last);
    if (!task || !task->pending || Z_AITerritory(map, task->order) != 4 ||
        task->execute_at - map->ai.clock <= (uint32_t)(map->ai.delay + 1))
        return fail("home backlog crosses next heavy pass");
    uint32_t due = task->execute_at;
    while (task->pending && map->ai.clock <= due + 2) think(1);
    if (task->pending || !P_HasMoveOrder(last)) return fail("backlogged home order eventually executes");
    rts_game_model_destroy(model);
    puts("PASS: z backlogged home order survives later heavy passes");
    return 0;
}

static int test_stray_home_reassignment(void) {
    for (int unreachable = 0; unreachable < 2; ++unreachable) {
        RtsGameModel *model = load();
        if (!model) return fail("load stray home fixture");
        mobj_t *lead = scenario();
        zmap_t *map = level.native_data;
        map->computer.allocation = 100;
        for (int t = 0; t < map->territory_count; ++t)
            map->territories[t].team = t == 4 ? 1 : Z_NEUTRAL;
        map->territories[8].importance = 255;
        fixed2_t at = fixed2_cell_center(map->territories[8].flag);
        fixed2_t home = fixed2_cell_center(map->territories[4].flag);
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *mo = (mobj_t *)th;
            if (mo->type_id == MT_FLAG) Z_SetTeam(mo, mo->z.territory == 5 ? 1 : Z_NEUTRAL);
            if (mo->team == 1 && mo->z.squad == lead->z.squad) {
                mo->core.position = fixed3_from_fixed2(at, 0);
                mo->z.manufactured = true;
            }
        }
        think(0);
        zaitask_t *task = task_of(lead);
        if (!task || task->current != 8) return fail("register manufactured stray");
        task->home = 8;
        task->task = -1;
        task->target = task->destination = -1;
        task->pending = false;
        task->retry = 100;
        if (unreachable) {
            P_ClearMove(lead);
            map->territories[4].flag = (ivec2_t){-1000, -1000};
        } else {
            P_MoveUnitTo(&level, lead, home);
            if (!P_HasMoveOrder(lead) || Z_AITerritory(map, lead->movement.goal) != 4)
                return fail("stray already heads for chosen territory");
        }
        map->ai.stray_cursor = lead->id - 1;
        map->ai.delay = 0;
        think(1);
        if (task->home != (unreachable ? 4 : 8) || task->pending || (unreachable && P_HasMoveOrder(lead)))
            return fail("stray home follows the native existing-order and failed-dispatch branches");
        rts_game_model_destroy(model);
    }
    return 0;
}

static int test_reserve_task_not_expired(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load reserve task fixture");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->computer.allocation = 100;
    for (int t = 0; t < map->territory_count; ++t) map->territories[t].team = 1;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) Z_SetTeam(mo, 1);
        if (mo->team == 1 && mo->z.squad == lead->z.squad) P_ClearMove(mo);
    }
    think(0);
    zaitask_t *task = task_of(lead);
    if (!task) return fail("register reserve task");
    task->task = 4;
    task->target = 4;
    task->destination = -1;
    task->pending = false;
    task->idle = 0;
    for (int i = 0; i < 5; ++i) {
        map->ai.delay = 0;
        think(1);
    }
    if ((task->flags & 24) != 8 || task->task != 4 || task->target != 4 || task->idle != 0)
        return fail("idle reserve task survives mobile-only expiration");
    map->computer.allocation = 0;
    task->task = 8;
    task->idle = 0;
    for (int i = 0; i < 5; ++i) {
        map->ai.delay = 0;
        think(1);
    }
    if (!(task->flags & 16) || task->task != -1)
        return fail("idle mobile task still expires");
    rts_game_model_destroy(model);
    return 0;
}

static int test_frontier_overlap(void) {
    RtsGameModel *model = load();
    if (!model) return fail("frontier overlap load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->territory_count = 2; map->fort_count = 0;
    map->territories[1].min = (ivec2_t){0,0}; map->territories[1].max = (ivec2_t){100,100};
    map->territories[1].flag = (ivec2_t){40,40}; map->territories[1].team = 1;
    map->territories[0].min = (ivec2_t){60,60}; map->territories[0].max = (ivec2_t){70,70};
    map->territories[0].flag = (ivec2_t){65,65}; map->territories[0].team = 0;
    map->territories[0].options = map->territories[1].options = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (Z_UnitType(mo) < 0) continue;
        if (mo->team == 1 && mo->z.squad == lead->z.squad) {
            mo->core.position = fixed3_from_fixed2(fixed2_cell_center((ivec2_t){40,40}), 0);
            P_ClearMove(mo);
        } else if (mo->team == 0) {
            mo->core.position = fixed3_from_fixed2(fixed2_cell_center((ivec2_t){65,65}), 0);
            P_ClearMove(mo);
        }
    }
    think(0); map->ai.delay = 0; think(1);
    zaitask_t *task = task_of(lead);
    if (!task || task->task != -1 || task->current != 1 || task->passes < 1)
        return fail("nested enemy territory does not turn inland force into frontier force");
    rts_game_model_destroy(model);
    return 0;
}

static int test_unreachable_frontier(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load unreachable frontier fixture");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->computer.intelligence = 126;
    map->territory_count = 3;
    map->fort_count = 0;
    for (int t = 0; t < 3; ++t) {
        map->territories[t].min = (ivec2_t){t * 40, 0};
        map->territories[t].max = (ivec2_t){(t + 1) * 40, 40};
        map->territories[t].flag = (ivec2_t){t * 40 + 20, 20};
        map->territories[t].team = t == 2 ? 0 : 1;
        map->territories[t].importance = 0;
        map->territories[t].options = 0;
        map->ai.territories[t].flags = 0;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) P_RemoveMobj(mo);
        if (mo->team == 1 && mo->z.squad == lead->z.squad) {
            mo->core.position = fixed3_from_fixed2(fixed2_cell_center(map->territories[0].flag), 0);
            P_ClearMove(mo);
        } else if (mo->team == 0 && Z_UnitType(mo) >= 0)
            mo->core.position = fixed3_from_fixed2(fixed2_cell_center(map->territories[2].flag), 0);
    }
    ivec2_t flag = map->territories[1].flag;
    level.blocked[L_Index(&level, flag.x, flag.y)] = 1;
    P_NavFree(&level);
    int cls = P_MobjMoveClass(lead);
    if (P_NavReachable(&level, cls, P_NavNodeAt(&level, cls, fixed3_xy(lead->core.position)),
                       P_NavNodeAt(&level, cls, fixed2_cell_center(flag))))
        return fail("frontier fixture destination is unreachable");
    think(0);
    zaitask_t *task = task_of(lead);
    if (!task || task->current != 0) return fail("register inland force");
    task->passes = 1;
    task->pending = true;
    task->execute_at = map->ai.clock + 1000;
    map->ai.delay = 0;
    map->ai.fraction = 0;
    think(1);
    if (task->passes || task->task != -1 || !task->pending)
        return fail("pending order resets frontier quiet passes");
    task->pending = false;
    task->passes = 1;
    if (!P_MoveUnitTo(&level, lead, fixed2_cell_center(ivec2_add(map->territories[0].flag, (ivec2_t){3, 0}))))
        return fail("frontier moving fixture");
    map->ai.delay = 0;
    map->ai.fraction = 0;
    think(1);
    if (task->passes != 1 || task->task != -1)
        return fail("moving force preserves frontier quiet passes");
    P_ClearMove(lead);
    task->passes = 0;
    map->ai.delay = 0;
    map->ai.fraction = 0;
    think(1);
    if (task->passes != 1 || task->task != -1)
        return fail("frontier waits for a new first quiet pass");
    map->ai.delay = 0;
    map->ai.fraction = 0;
    think(1);
    if (task->task != 10 || task->destination != 1 || task->pending || task->passes)
        return fail("unreachable frontier still receives native assignment");
    rts_game_model_destroy(model);
    return 0;
}

static int test_restricted_territory(void) {
    for (int test = 0; test < 12; ++test) {
        RtsGameModel *model = load();
        if (!model) return fail("restricted territory load");
        mobj_t *lead = scenario(), *follower = NULL;
        zmap_t *map = level.native_data;
        uint8_t squad = lead->z.squad;
        map->territories[2].team = 1;
        bool apc_case = (test >= 3 && test <= 5) || test == 8 || test == 9;
        map->territories[8].options &= (uint8_t)~0x60;
        if (test < 8 || test >= 10) map->territories[8].options |= 0x40;
        if (test == 5 || test == 8) map->territories[8].options |= 0x20;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            mobj_t *mo = (mobj_t *)th;
            if (mo->type_id == MT_FLAG && mo->z.territory == 3) Z_SetTeam(mo, 1);
            if (mo->team != 1 || mo->z.squad != squad || !Z_IsRobot(Z_UnitType(mo))) continue;
            mo->z.grenades = 0;
            if (mo != lead && !follower) follower = mo;
            if (apc_case && mo != lead) mo->hp = 0;
        }
        if (!follower) return fail("restricted territory squad follower");
        if (test == 1) follower->z.grenades = 1;
        if (test == 2) {
            mobj_t *foreign = P_SpawnMobj(lead->core.position, MT_UNIT_GRUNT);
            if (!foreign) return fail("restricted territory foreign robot");
            Z_SetTeam(foreign, 0);
            foreign->z.squad = squad; foreign->z.grenades = 1;
        }
        if (apc_case) {
            const mobjtype_t *apc = P_ActorType(MT_UNIT_APC);
            if (!apc) return fail("restricted territory APC type");
            P_ApplyActorTypeDefaults(lead, apc);
            lead->z.squad = 0;
            lead->z.driver = (test == 4 ? MT_UNIT_GRUNT : MT_UNIT_TOUGH) - MT_UNITS + 1;
        }
        if (test == 6 || test == 7 || test == 10 || test == 11) {
            const mobjtype_t *robot = P_ActorType(test == 6 || test == 10 ? MT_UNIT_SNIPER : MT_UNIT_PSYCHO);
            if (!robot) return fail("restricted territory robot type");
            P_ApplyActorTypeDefaults(lead, robot);
            if (test >= 10) lead->z.grenades = 1;
        }
        think(0); think(1);
        const zaitask_t *task = task_of(lead);
        bool sent = task && task->target == 8 && task->task == 0 &&
                    (task->pending || P_HasMoveOrder(lead));
        if (sent != (test == 1 || test == 3 || test == 9 || test == 10 || test == 11))
            return fail("native restricted territory respects squad grenades and APC driver class");
        rts_game_model_destroy(model);
    }
    puts("PASS: z restricted territory uses squad grenades and Tough APC crew");
    return 0;
}

static int test_restricted_dispatch(void) {
    static const int types[] = {MT_UNIT_JEEP, MT_UNIT_APC, MT_UNIT_GRUNT, MT_UNIT_SNIPER,
                                MT_UNIT_PSYCHO, MT_UNIT_SNIPER, MT_UNIT_PSYCHO};
    for (int test = 0; test < 7; ++test) {
        RtsGameModel *model = load();
        if (!model) return fail("restricted dispatch load");
        mobj_t *lead = scenario();
        zmap_t *map = level.native_data;
        uint8_t squad = lead->z.squad;
        map->territories[8].options |= 0x40;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            mobj_t *mo = (mobj_t *)th;
            if (mo->team != 1 || mo->z.squad != squad || !Z_IsRobot(Z_UnitType(mo))) continue;
            mo->z.grenades = 0;
            if (test < 2 && mo != lead) mo->hp = 0;
        }
        const mobjtype_t *type = P_ActorType((uint16_t)types[test]);
        if (!type) return fail("restricted dispatch type");
        P_ApplyActorTypeDefaults(lead, type);
        if (test >= 5) lead->z.grenades = 1;
        if (test < 2) lead->z.squad = 0;
        lead->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
        lead->z.home = 9;
        fixed2_t at = fixed3_xy(lead->core.position);
        P_MoveUnitTo(&level, lead, fixed2_add(at, FIXED2_LIT(2, 0)));
        if (!P_HasMoveOrder(lead)) return fail("restricted dispatch starts with an active move");
        think(0);
        const zaitask_t *task = task_of(lead);
        bool allowed = test < 2 || test >= 5;
        if (!task || task->target != 8 || !task->pending || fixed2_equal(task->order, at) == allowed)
            return fail("restricted dispatch queues a stop at the current position");
        if (!allowed) {
            map->ai.delay = 1000;
            think(1);
            if (P_HasMoveOrder(lead)) return fail("restricted dispatch cancels the previous move");
        }
        rts_game_model_destroy(model);
    }
    return 0;
}
static int test_vehicles(void) {
    RtsGameModel *model = load();
    if (!model) return fail("vehicle task load");
    zmap_t *map = level.native_data;
    map->computer.intelligence = 127; map->computer.allocation = 0;
    for (int t=0;t<map->territory_count;t++) map->territories[t].team = 1;
    for (thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        mobj_t *mo=(mobj_t *)th;
        mo->z.home=0; mo->traits &= ~MF_ATTACK;
        if(mo->type_id==MT_FLAG) Z_SetTeam(mo,1);
    }
    think(0); think(1);
    int assigned=0;
    for(int i=0;i<Z_AI_TASKS;i++) {
        const zaitask_t *task=&map->ai.tasks[i];
        if(!task->boarding) continue;
        assigned++;
        for(int j=0;j<i;j++)
            if(map->ai.tasks[j].boarding && map->ai.tasks[j].target==task->target)
                return fail("one vehicle reservation per territory");
    }
    if(!assigned) return fail("native vehicle discovery makes a boarding task");
    if(!tick(model,60*RTS_TICRATE) || !nth(MT_UNIT_JEEP,1,0)) return fail("computer boards native vehicle target");
    puts("PASS: z native squad vehicle discovery and territory reservations lead to boarding");
    rts_game_model_destroy(model);
    return 0;
}
/* 0x98fec chooses by distance before the boarding order is dispatched. */
static int test_unreachable_vehicle_choice(void) {
    RtsGameModel *model = load();
    if (!model) return fail("unreachable vehicle load");
    mobj_t *lead = scenario(), *near = nth(MT_UNIT_JEEP, Z_NEUTRAL, 0);
    zmap_t *map = level.native_data;
    if (!near) return fail("unreachable vehicle fixture");
    mobj_t *far = P_SpawnMobj(near->core.position, MT_UNIT_JEEP);
    if (!far) return fail("second vehicle fixture");
    far->z.object = near->z.object;
    Z_SetTeam(far, Z_NEUTRAL);
    mobj_t *radar = P_SpawnMobj(lead->core.position, MT_RADAR);
    if (!radar) return fail("vehicle radar fixture");
    Z_SetTeam(radar, 1);
    radar->z.building = 1;
    for (int t = 0; t < map->territory_count; ++t) {
        map->territories[t].team = 1;
        map->territories[t].importance = 0;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) Z_SetTeam(mo, 1);
    }
    ivec2_t start = map->territories[4].flag;
    radar->core.position = fixed3_from_fixed2(fixed2_cell_center(start), 0);
    ivec2_t cell = ivec2_add(start, (ivec2_t){3, 0});
    near->z.driver = 0;
    near->core.position = fixed3_from_fixed2(fixed2_cell_center(cell), 0);
    far->core.position = fixed3_from_fixed2(fixed2_cell_center(ivec2_add(start, (ivec2_t){6, 0})), 0);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->team == 1 && mo->hp > 0 && mo->z.squad == lead->z.squad)
            mo->core.position = fixed3_from_fixed2(fixed2_cell_center(start), 0);
    }
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
            level.blocked[L_Index(&level, cell.x + x, cell.y + y)] = 1;
    P_NavFree(&level);
    int cls = P_MobjMoveClass(lead);
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
            if (P_NavReachable(&level, cls, P_NavNodeAt(&level, cls, fixed3_xy(lead->core.position)),
                               P_NavNodeAt(&level, cls, fixed2_cell_center(ivec2_add(cell, (ivec2_t){x,y})))))
                return fail("vehicle and its approach are unreachable");
    if (!P_NavReachable(&level, cls, P_NavNodeAt(&level, cls, fixed3_xy(lead->core.position)),
                        P_NavNodeAt(&level, cls, fixed3_xy(far->core.position))))
        return fail("farther vehicle fixture is reachable");
    think(0);
    zaitask_t *task = task_of(lead);
    if (!task) return fail("unreachable vehicle task registration");
    task->pending = true;
    task->execute_at = map->ai.clock + 1000;
    task->order = fixed3_xy(lead->core.position);
    map->ai.delay = 0;
    think(1);
    if (task->task != 5 || task->boarding != near->id ||
        task->execute_at >= map->ai.clock + 1000)
        return fail("nearest unreachable vehicle replaces prior pending order");
    bool dispatched = false, released = false;
    for (int i = 0; i < 15 * RTS_TICRATE; ++i) {
        if (!tick(model, 1)) return fail("unreachable vehicle order execution");
        dispatched |= lead->z.board == near->id;
        if (dispatched && !lead->z.board && !task->boarding) { released = true; break; }
    }
    if (!dispatched || !released || near->team != Z_NEUTRAL)
        return fail("isolated vehicle order is dispatched then released");
    near->z.driver = 1;
    far->z.driver = 1;
    for (int i = 0; i < 10 && task->task == 5; ++i) think(1);
    if (task->task == 5 || task->boarding || lead->z.board)
        return fail("idle vehicle task clears when no target remains");
    rts_game_model_destroy(model);
    puts("PASS: z vehicle selection precedes route dispatch");
    return 0;
}

/* 0x98f29 ends the pass after the first eligible group finds no object. */
static int test_vehicle_scan_stops(void) {
    RtsGameModel *model = load();
    if (!model) return fail("vehicle scan load");
    mobj_t *lead = scenario(), *jeep = nth(MT_UNIT_JEEP, Z_NEUTRAL, 0);
    zmap_t *map = level.native_data;
    if (!jeep) return fail("vehicle scan jeep");
    mobj_t *second = P_SpawnMobj(lead->core.position, MT_UNIT_GRUNT);
    if (!second) return fail("vehicle scan second group");
    Z_SetTeam(second, 1);
    second->traits &= ~MF_ATTACK;
    second->z.squad = 0;
    for (int t = 0; t < map->territory_count; ++t) {
        map->territories[t].team = 1;
        map->territories[t].importance = 0;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) Z_SetTeam(mo, 1);
    }
    jeep->z.driver = 0;
    jeep->core.position = fixed3_from_fixed2(fixed2_cell_center(map->territories[4].flag), 0);
    map->computer.intelligence = 126;
    think(0);
    zaitask_t *first = task_of(lead), *other = task_of(second);
    if (!first || !other) return fail("vehicle scan register groups");
    uint16_t seed = 0, after = 0;
    for (unsigned candidate = 0; candidate <= UINT16_MAX; ++candidate) {
        uint16_t next = (uint16_t)candidate;
        if (!(Z_NextRandom(&next) & 1)) {
            uint16_t next2 = next;
            if (Z_NextRandom(&next2) & 1) { seed = (uint16_t)candidate; after = next; break; }
        }
    }
    map->ai.seed = seed;
    map->ai.delay = 0;
    think(1);
    if (map->ai.seed != after || first->task == 5 || other->task == 5)
        return fail("failed first observation ends vehicle scan");
    map->ai.delay = 0;
    think(1);
    if (first->task != 5 || first->boarding != jeep->id)
        return fail("later pass can find the same vehicle");
    rts_game_model_destroy(model);
    puts("PASS: z failed vehicle observation ends the current pass");
    return 0;
}

/* 0x993ff: an attack that becomes too weak, with nobody to help, falls
 * back and forgets its target. */
/* 0x99e67 refuses reinforcement with no idle group before borrowing a garrison. */
/* 0x9d330 may choose a fixed gun, whose subsequent 0x9d42c call returns. */
/* 0x9d209 compares importance bytes with signed JG. */
static int test_defence_signed_importance(void) {
    RtsGameModel *model = load();
    if (!model) return fail("signed defence importance load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->computer.allocation = 100;
    map->computer.intelligence = 127;
    map->computer.territory_weight = 0;
    map->territory_count = 2; map->fort_count = 0;
    for (int t = 0; t < 2; ++t) {
        map->territories[t].min = (ivec2_t){t * 60, 0};
        map->territories[t].max = (ivec2_t){t * 60 + 40, 40};
        map->territories[t].flag = (ivec2_t){t * 60 + 20, 20};
        map->territories[t].team = 1;
        map->territories[t].importance = t ? 128 : 1;
        map->territories[t].options = 0;
        map->ai.territories[t].flags = 0;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) P_RemoveMobj(mo);
        if (Z_UnitType(mo) >= 0 && mo != lead) mo->hp = 0;
    }
    fixed3_t donor = fixed3_from_fixed2(fixed2_cell_center(map->territories[0].flag), 0);
    fixed3_t threatened = fixed3_from_fixed2(fixed2_cell_center(map->territories[1].flag), 0);
    lead->z.squad = 0; lead->core.position = donor; lead->traits &= ~MF_ATTACK;
    mobj_t *defender = P_SpawnMobj(threatened, MT_UNIT_GRUNT);
    mobj_t *enemy = P_SpawnMobj(threatened, MT_UNIT_HEAVY_TANK);
    if (!defender || !enemy) return fail("signed defence importance spawn");
    Z_SetTeam(defender, 1); Z_SetTeam(enemy, 0);
    enemy->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    defender->z.robot = enemy->z.robot = lead->z.robot;
    defender->traits &= ~MF_ATTACK; enemy->traits &= ~MF_ATTACK;
    defender->attack.target = enemy;
    think(0);
    zaitask_t *lt = task_of(lead), *dt = task_of(defender);
    if (!lt || !dt) return fail("signed defence importance tasks");
    map->ai.delay = 0; map->ai.fraction = 0;
    think(1);
    if ((lt->flags & 16) || !map->ai.territories[1].strength[0] || lt->home != -1 ||
        lt->pending || P_HasMoveOrder(lead) || dt->home != 0)
        return fail("signed importance rejects the distant donor");
    rts_game_model_destroy(model);
    puts("PASS: z defence compares territory importance as signed bytes");
    return 0;
}

/* 0x9dc25 retains unique sightings across the native unit slots. */
static int test_defence_many_sightings(void) {
    RtsGameModel *model = load();
    if (!model) return fail("many defence sightings load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->computer.allocation = 100; map->computer.intelligence = 127;
    map->computer.territory_weight = 0;
    map->territory_count = 3; map->fort_count = 0;
    for (int t = 0; t < 3; ++t) {
        map->territories[t].min = (ivec2_t){t * 40, 0};
        map->territories[t].max = (ivec2_t){(t + 1) * 40, 40};
        map->territories[t].flag = (ivec2_t){t * 40 + 20, 20};
        map->territories[t].team = t == 2 ? 0 : 1;
        map->territories[t].importance = 0;
        map->territories[t].options = 0;
        map->ai.territories[t].flags = 0;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) P_RemoveMobj(mo);
        if (Z_UnitType(mo) >= 0 && mo != lead) mo->hp = 0;
    }
    fixed3_t donor = fixed3_from_fixed2(fixed2_cell_center(map->territories[0].flag), 0);
    fixed3_t threatened = fixed3_from_fixed2(fixed2_cell_center(map->territories[1].flag), 0);
    fixed3_t hostile = fixed3_from_fixed2(fixed2_cell_center(map->territories[2].flag), 0);
    lead->z.squad = 0; lead->core.position = donor; lead->traits &= ~MF_ATTACK;
    for (int i = 0; i < 84; ++i) {
        mobj_t *observer = i ? P_SpawnMobj(donor, MT_UNIT_GRUNT) : lead;
        mobj_t *enemy = P_SpawnMobj(hostile, MT_UNIT_GRUNT);
        if (!observer || !enemy) return fail("many defence sightings spawn");
        Z_SetTeam(observer, 1); Z_SetTeam(enemy, 0);
        observer->z.robot = enemy->z.robot = lead->z.robot;
        observer->z.squad = 0;
        observer->traits &= ~MF_ATTACK; enemy->traits &= ~MF_ATTACK;
        observer->attack.target = enemy;
    }
    mobj_t *defender = P_SpawnMobj(threatened, MT_UNIT_GRUNT);
    mobj_t *enemy = P_SpawnMobj(threatened, MT_UNIT_HEAVY_TANK);
    if (!defender || !enemy) return fail("last defence sighting spawn");
    Z_SetTeam(defender, 1); Z_SetTeam(enemy, 0);
    defender->z.robot = enemy->z.robot = lead->z.robot;
    enemy->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    defender->traits &= ~MF_ATTACK; enemy->traits &= ~MF_ATTACK;
    defender->attack.target = enemy;
    think(0);
    map->ai.delay = 0; map->ai.fraction = 0;
    think(1);
    bool sent = false;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &map->ai.tasks[i];
        if (task->unit && task->current == 0 && task->home == 0) sent = true;
    }
    if (!sent) return fail("last distinct sighting triggers reserve defence");
    rts_game_model_destroy(model);
    puts("PASS: z defence processes more than 84 distinct sightings");
    return 0;
}

static int test_fixed_gun_defence_donor(void) {
    RtsGameModel *model = load();
    if (!model) return fail("fixed gun donor load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->computer.allocation = 100;
    map->computer.intelligence = 127;
    map->computer.territory_weight = 10;
    map->territory_count = 2; map->fort_count = 0;
    for (int t = 0; t < 2; ++t) {
        map->territories[t].min = (ivec2_t){t * 50, 0};
        map->territories[t].max = (ivec2_t){(t + 1) * 50, 50};
        map->territories[t].flag = (ivec2_t){t * 50 + 25, 25};
        map->territories[t].team = 1;
        map->territories[t].importance = 0;
        map->territories[t].options = 0;
        map->ai.territories[t].flags = 0;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) P_RemoveMobj(mo);
        if (Z_UnitType(mo) >= 0 && mo != lead) mo->hp = 0;
    }
    fixed3_t donor = fixed3_from_fixed2(fixed2_cell_center(map->territories[0].flag), 0);
    fixed3_t threatened = fixed3_from_fixed2(fixed2_cell_center(map->territories[1].flag), 0);
    lead->z.squad = 0; lead->core.position = donor; lead->traits &= ~MF_ATTACK;
    mobj_t *gun = P_SpawnMobj(donor, MT_UNIT_GATLING);
    mobj_t *defender = P_SpawnMobj(threatened, MT_UNIT_GRUNT);
    mobj_t *enemy = P_SpawnMobj(threatened, MT_UNIT_HEAVY_TANK);
    if (!gun || !defender || !enemy) return fail("fixed gun donor spawn");
    Z_SetTeam(gun, 1); Z_SetTeam(defender, 1); Z_SetTeam(enemy, 0);
    gun->z.driver = enemy->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    gun->z.robot = defender->z.robot = enemy->z.robot = lead->z.robot;
    gun->traits &= ~(MF_MOBILE | MF_ATTACK);
    defender->traits &= ~MF_ATTACK; enemy->traits &= ~MF_ATTACK;
    defender->attack.target = enemy;
    think(0);
    zaitask_t *gt = task_of(gun), *lt = task_of(lead), *dt = task_of(defender);
    if (!gt || !lt || !dt) return fail("fixed gun donor register");
    map->ai.delay = 0; map->ai.fraction = 0;
    think(1);
    if (Z_AIUnitValue(gun) <= Z_AIUnitValue(lead) || !map->ai.territories[1].strength[0] ||
        gt->home != 0 || gt->pending || lt->pending || P_HasMoveOrder(lead))
        return fail("fixed gun selected as donor without a move order");
    rts_game_model_destroy(model);
    puts("PASS: z fixed gun can be selected as a defence donor");
    return 0;
}

static int test_reinforce_requires_idle(void) {
    RtsGameModel *model = load();
    if (!model) return fail("reinforcement load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->computer.allocation = 0;
    map->computer.intelligence = 127;
    map->computer.territory_weight = 10;
    for (int t = 0; t < map->territory_count; ++t) {
        map->territories[t].team = t == 8 ? 0 : 1;
        map->territories[t].importance = t == 4 ? 5 : t == 8 ? 10 : 0;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) Z_SetTeam(mo, mo->z.territory == 9 ? 0 : 1);
        if (mo->team == 0 && Z_UnitType(mo) >= 0) mo->hp = 0;
    }
    mobj_t *guard = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center(map->territories[4].flag), 0), MT_UNIT_HEAVY_TANK);
    mobj_t *enemy = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center(map->territories[8].flag), 0), MT_UNIT_GRUNT);
    if (!guard || !enemy) return fail("reinforcement spawn");
    Z_SetTeam(guard, 1); Z_SetTeam(enemy, 0);
    guard->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    guard->z.robot = enemy->z.robot = lead->z.robot;
    guard->traits &= ~MF_ATTACK;
    enemy->traits &= ~MF_ATTACK;
    think(0);
    zaitask_t *attack = task_of(lead), *garrison = task_of(guard);
    if (!attack || !garrison) return fail("reinforcement register tasks");
    attack->task = 0; attack->target = attack->destination = 8;
    garrison->task = 4; garrison->target = 4;
    map->ai.delay = 0;
    map->ai.fraction = 0;
    think(1);
    int required = 2 * map->ai.territories[8].strength[0] + map->ai.territories[8].value;
    if (garrison->strength < required || attack->strength >= required || attack->task != -1)
        return fail("attack retreats when no idle group can initiate reinforcement");
    rts_game_model_destroy(model);
    puts("PASS: z reinforcement needs an idle group before borrowing garrison");
    return 0;
}

/* 0x993ab/0x99ead compare garrison and attack values as unsigned dwords. */
static int test_reinforce_negative_garrison(void) {
    RtsGameModel *model = load();
    if (!model) return fail("negative garrison load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->computer.allocation = 0;
    map->computer.intelligence = 127;
    map->computer.territory_weight = 10;
    for (int t = 0; t < map->territory_count; ++t) {
        map->territories[t].team = t == 8 ? 0 : 1;
        map->territories[t].importance = t == 8 ? 10 : 0;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) Z_SetTeam(mo, mo->z.territory == 9 ? 0 : 1);
        if (mo->team == 0 && Z_UnitType(mo) >= 0) mo->hp = 0;
    }
    fixed3_t home = fixed3_from_fixed2(fixed2_cell_center(map->territories[4].flag), 0);
    fixed3_t target = fixed3_from_fixed2(fixed2_cell_center(map->territories[8].flag), 0);
    mobj_t *guard = P_SpawnMobj(home, MT_UNIT_HEAVY_TANK);
    mobj_t *idle = P_SpawnMobj(home, MT_UNIT_GRUNT);
    mobj_t *invader = P_SpawnMobj(home, MT_UNIT_GRUNT);
    mobj_t *enemy = P_SpawnMobj(target, MT_UNIT_GRUNT);
    if (!guard || !idle || !invader || !enemy) return fail("negative garrison spawn");
    Z_SetTeam(guard, 1); Z_SetTeam(idle, 1); Z_SetTeam(invader, 0); Z_SetTeam(enemy, 0);
    guard->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    guard->z.robot = idle->z.robot = invader->z.robot = enemy->z.robot = lead->z.robot;
    guard->traits &= ~MF_ATTACK; idle->traits &= ~MF_ATTACK;
    invader->traits &= ~MF_ATTACK; enemy->traits &= ~MF_ATTACK;
    think(0);
    zaitask_t *attack = task_of(lead), *garrison = task_of(guard), *spare = task_of(idle);
    if (!attack || !garrison || !spare) return fail("negative garrison register");
    attack->task = 0; attack->target = attack->destination = 8;
    garrison->task = 4; garrison->target = 4;
    map->ai.delay = 0; map->ai.fraction = 0;
    think(1);
    int required = 2 * map->ai.territories[8].strength[0] + map->ai.territories[8].value;
    if (map->ai.territories[4].value >= 0 || map->ai.territories[8].value <= 0 ||
        spare->strength >= required || garrison->strength < required || attack->task != -1)
        return fail("negative garrison value is not cheaper unsigned");
    rts_game_model_destroy(model);
    puts("PASS: z reinforcement compares garrison value unsigned");
    return 0;
}

static int test_retreat(void) {
    RtsGameModel *model = load();
    if (!model) return fail("retreat load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    think(0); think(1);
    if (task_of(lead)->task != 0 || task_of(lead)->destination != 8) return fail("retreat fixture marches on territory 8");
    for (int i = 0; nth(MT_UNIT_GRUNT, 0, i); ++i)
        nth(MT_UNIT_GRUNT, 0, i)->core.position = fixed3_from_fixed2(fixed2_cell_center(map->territories[8].flag), 0);
    think(1);
    const zaitask_t *t = task_of(lead);
    /* It may take the weaker neutral territory 2 instead on a later pass. */
    if (t->destination == 8 || t->target == 8) return fail("outmatched attack falls back");
    puts("PASS: z an outmatched attack falls back and looks elsewhere");
    rts_game_model_destroy(model);
    return 0;
}

static int test_retreat_without_staging(void) {
    for (int restricted = 0; restricted < 2; ++restricted) {
        RtsGameModel *model = load();
        if (!model) return fail("load retreat staging fixture");
        mobj_t *lead = scenario();
        zmap_t *map = level.native_data;
        map->computer.intelligence = 126;
        think(0);
        zaitask_t *task = task_of(lead);
        if (!task || task->current < 0 || task->current == 8) return fail("register retreat staging task");
        int current = task->current;
        map->ai.territories[current].flags &= ~(32 | 64);
        if (restricted) {
            map->ai.territories[current].flags |= 64;
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                mobj_t *mo = (mobj_t *)th;
                if (mo->team == lead->team && mo->z.squad == lead->z.squad) mo->z.grenades = 0;
            }
        } else if (map->ai.territories[current].flags & 1) map->forts[0] = (ivec2_t){-10000, -10000};
        else if (map->ai.territories[current].flags & 2) map->forts[1] = (ivec2_t){-10000, -10000};
        else map->territories[current].flag = (ivec2_t){-1000, -1000};
        fixed2_t old_order = fixed2_cell_center(map->territories[8].flag);
        fixed2_t expected = restricted ? fixed3_xy(lead->core.position) : old_order;
        task->task = 0;
        task->target = task->destination = 8;
        task->pending = true;
        task->order = old_order;
        task->execute_at = map->ai.clock + 10000;
        for (int i = 0; nth(MT_UNIT_GRUNT, 0, i); ++i)
            nth(MT_UNIT_GRUNT, 0, i)->core.position = fixed3_from_fixed2(old_order, 0);
        map->ai.delay = 0;
        map->ai.fraction = 0;
        think(1);
        if (task->task != -1 || task->target != -1 || task->destination != -1 ||
            !task->pending || !fixed2_equal(task->order, expected))
            return fail("retreat preserves a blocked order and stops for a restricted territory");
        rts_game_model_destroy(model);
    }
    return 0;
}

/* 0x9c174/0x9cab4: a reserve standing in a territory whose flag was just
 * taken, with no enemy there, runs back onto the flag and retakes it. */
static int test_recapture(void) {
    RtsGameModel *model = load();
    if (!model) return fail("recapture load");
    mobj_t *lead = scenario();
    zmap_t *map = level.native_data;
    map->computer.allocation = 100;
    mobj_t *flag = NULL;
    for (int i = 0; nth(MT_FLAG, -1, i); ++i)
        if (nth(MT_FLAG, -1, i)->z.territory == 5) flag = nth(MT_FLAG, -1, i);
    if (!flag) return fail("recapture fixture flag");
    ivec2_t cell = ivec2_add(map->territories[4].flag, (ivec2_t){0, 3});
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->team == 1 && mo->hp > 0 && mo->z.squad == lead->z.squad)
            mo->core.position = fixed3_from_fixed2(fixed2_cell_center(cell), 0);
    }
    think(0); think(1);
    if (task_of(lead)->flags & 16) return fail("recapture fixture keeps the squad in reserve");
    Z_SetTeam(flag, 0);
    think(1);
    if (map->ai.territories[4].lockout == 0 || !(task_of(lead)->flags & 2) ||
        (!task_of(lead)->pending && !P_HasMoveOrder(lead))) return fail("reserve runs for the lost flag");
    if (!tick(model, 20 * RTS_TICRATE) || flag->team != 1) return fail("reserve retakes the flag");
    if (task_of(lead)->flags & 2) return fail("retaking clears the recapture mark");
    puts("PASS: z a reserve retakes a flag lost in its territory");
    rts_game_model_destroy(model);
    return 0;
}

/* 0x9c25c marks the reserve even if its delayed order cannot be routed. */
static int test_unreachable_recapture(void) {
    RtsGameModel *model = load();
    if (!model) return fail("unreachable recapture load");
    mobj_t *lead = scenario(), *flag = NULL;
    zmap_t *map = level.native_data;
    map->computer.allocation = 100;
    for (int i = 0; nth(MT_FLAG, -1, i); ++i)
        if (nth(MT_FLAG, -1, i)->z.territory == 5) flag = nth(MT_FLAG, -1, i);
    if (!flag) return fail("unreachable recapture flag");
    ivec2_t cell = ivec2_add(map->territories[4].flag, (ivec2_t){0, 3});
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->team == 1 && mo->hp > 0 && mo->z.squad == lead->z.squad)
            mo->core.position = fixed3_from_fixed2(fixed2_cell_center(cell), 0);
    }
    ivec2_t target = map->territories[4].flag;
    level.blocked[L_Index(&level, target.x, target.y)] = 1;
    P_NavFree(&level);
    int cls = P_MobjMoveClass(lead);
    if (P_NavReachable(&level, cls, P_NavNodeAt(&level, cls, fixed3_xy(lead->core.position)),
                       P_NavNodeAt(&level, cls, fixed2_cell_center(target))))
        return fail("recapture fixture destination is unreachable");
    think(0); think(1);
    if (task_of(lead)->flags & 16) return fail("unreachable recapture reserve");
    Z_SetTeam(flag, 0);
    think(1);
    if (!map->ai.territories[4].lockout || !(task_of(lead)->flags & 2) || task_of(lead)->pending)
        return fail("unreachable recapture records mark without queue");
    Z_SetTeam(flag, 1);
    think(1);
    if (task_of(lead)->flags & 2) return fail("unreachable recapture mark clears on retake");
    rts_game_model_destroy(model);
    puts("PASS: z unreachable recapture retains native mark");
    return 0;
}

static void service_fixture(void) {
    zmap_t *map = level.native_data;
    map->computer.intelligence = 127;
    map->computer.allocation = 100;
    for (int t = 0; t < map->territory_count; ++t) map->territories[t].team = 1;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (Z_UnitType(mo) >= 0) P_RemoveMobj(mo);
        else if (mo->type_id == MT_FLAG) Z_SetTeam(mo, 1);
        else if (Z_Producer(mo)) mo->z.rating = 0;
    }
    P_SyncBuildingBlocking();
}

static mobj_t *service_unit(int type, fixed2_t near) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(near, 0), (uint16_t)type);
    if (!unit) return NULL;
    Z_SetTeam(unit, 1);
    ivec2_t centre = fixed2_cell(near);
    for (int d = 1; d <= 6; ++d)
        for (int y = -d; y <= d; ++y)
            for (int x = -d; x <= d; ++x) {
                fixed2_t at = fixed2_cell_center(ivec2_add(centre, (ivec2_t){x,y}));
                if (!P_CheckPosition(&level, unit, at)) continue;
                unit->core.position = fixed3_from_fixed2(at, 0);
                return unit;
            }
    return NULL;
}

static int test_unreachable_service_return(void) {
    for (int unreachable = 0; unreachable < 2; ++unreachable) {
        RtsGameModel *model = load();
        if (!model) return fail("load service return fixture");
        service_fixture();
        zmap_t *map = level.native_data;
        mobj_t *unit = service_unit(MT_UNIT_FIRE_TRUCK, fixed2_cell_center(map->territories[4].flag));
        if (!unit) return fail("spawn returning fire truck");
        think(0);
        zaitask_t *task = task_of(unit);
        if (!task || task->current != 4 || task->task != -1) return fail("idle service unit outside fort");
        task->target = 4;
        task->destination = 7;
        task->pending = true;
        task->execute_at = map->ai.clock + 1000;
        task->order = fixed3_xy(unit->core.position);
        if (unreachable) map->forts[0] = (ivec2_t){-10000, -10000};
        map->ai.delay = 0;
        think(1);
        if (task->task != 8 || task->target != 4 || task->destination != 7 ||
            task->pending || P_HasMoveOrder(unit) == (bool)unreachable)
            return fail("service return replaces pending order and preserves task coordinates");
        rts_game_model_destroy(model);
    }
    return 0;
}

static int test_unreachable_crane_assignment(void) {
    RtsGameModel *model = load_map("LEVEL12/LEVEL12.MAP");
    if (!model) return fail("load crane reservation fixture");
    service_fixture();
    mobj_t *building = nth(MT_REPAIR, -1, 0);
    if (!building) return fail("crane reservation building");
    Z_SetTeam(building, 1);
    building->hp = 0;
    zmap_t *map = level.native_data;
    mobj_t *crane = service_unit(MT_UNIT_CRANE, fixed2_cell_center(map->territories[4].flag));
    if (!crane) return fail("spawn crane reservation fixture");
    ivec2_t cell = fixed2_cell(fixed3_xy(crane->core.position));
    for (int y = -3; y <= 3; ++y)
        for (int x = -3; x <= 3; ++x)
            if ((x || y) && L_Contains(&level, cell.x + x, cell.y + y))
                level.blocked[L_Index(&level, cell.x + x, cell.y + y)] = 1;
    P_NavFree(&level);
    fixed2_t goal;
    if (Z_ServiceApproach(crane, building, &goal)) return fail("crane fixture has no reachable approach");
    mobj_t *other = service_unit(MT_UNIT_CRANE, fixed3_xy(building->core.position));
    if (!other || !Z_ServiceApproach(other, building, &goal)) return fail("second crane can reach building");
    think(0);
    zaitask_t *task = task_of(crane);
    zaitask_t *other_task = task_of(other);
    if (!task || !other_task) return fail("register crane reservation tasks");
    task->pending = true;
    task->enemy = other->id;
    task->execute_at = map->ai.clock + 10000;
    map->ai.delay = 0;
    think(1);
    if (task->task != 9 || task->service != building->id || task->pending || task->enemy || crane->z.service ||
        other_task->service == building->id)
        return fail("unreachable building remains assigned to crane");
    uint32_t crane_id = crane->id, building_id = building->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    uint32_t hash = G_ConsistencyExtra(0);
    if (!G_SaveGameBlob(&save, &size, "Crane assignment", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud))
        return fail("save and load unreachable crane assignment");
    free(save);
    crane = P_MobjById(crane_id);
    building = Z_ServiceTarget(building_id);
    map = level.native_data;
    task = crane ? task_of(crane) : NULL;
    if (!crane || !building || !task || task->task != 9 || task->service != building_id ||
        G_ConsistencyExtra(0) != hash) return fail("saved crane reservation and hash survive load");
    building->hp = building->max_hp;
    map->ai.delay = 0;
    think(1);
    if (task->task != 8 || task->service) return fail("completed building releases unreachable crane assignment");
    rts_game_model_destroy(model);
    return 0;
}

static int test_unreachable_vehicle_repair(void) {
    for (int mode = 0; mode < 3; ++mode) {
        RtsGameModel *model = load_map("LEVEL12/LEVEL12.MAP");
        if (!model) return fail("load unreachable vehicle repair fixture");
        service_fixture();
        zmap_t *map = level.native_data;
        map->computer.intelligence = 126;
        mobj_t *facility = nth(MT_REPAIR, -1, 0);
        if (!facility) return fail("unreachable vehicle repair facility");
        Z_SetTeam(facility, 1);
        fixed2_t near = fixed2_add(fixed3_xy(facility->core.position), fixed2_from_cell((ivec2_t){6, 0}));
        mobj_t *tank = service_unit(MT_UNIT_MED_TANK, near);
        if (!tank) return fail("spawn unreachable damaged tank");
        tank->hp = tank->max_hp / 2 - 1;
        ivec2_t cell = fixed2_cell(fixed3_xy(tank->core.position));
        for (int y = -3; y <= 3; ++y)
            for (int x = -3; x <= 3; ++x)
                if ((x || y) && L_Contains(&level, cell.x + x, cell.y + y))
                    level.blocked[L_Index(&level, cell.x + x, cell.y + y)] = 1;
        P_NavFree(&level);
        fixed2_t goal;
        if (Z_ServiceApproach(tank, facility, &goal)) return fail("tank fixture has no reachable approach");
        think(0);
        zaitask_t *task = task_of(tank);
        if (!task) return fail("register unreachable damaged tank");
        task->target = 4;
        task->destination = 7;
        map->ai.delay = 0;
        think(1);
        if (task->task != 11 || task->pending || task->service || tank->z.service ||
            task->target != 4 || task->destination != 7 || (task->flags & 16))
            return fail("unreachable reserve vehicle retains repair assignment and task fields");
        uint32_t id = tank->id, facility_id = facility->id, hash = G_ConsistencyExtra(0);
        app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
        if (!G_SaveGameBlob(&save, &size, "Unreachable vehicle repair", &app, rts_game_model_ai(model), &hud) ||
            !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud))
            return fail("save unreachable vehicle repair");
        free(save);
        tank = P_MobjById(id);
        facility = P_MobjById(facility_id);
        map = level.native_data;
        task = tank ? task_of(tank) : NULL;
        if (!task || !facility || task->task != 11 || G_ConsistencyExtra(0) != hash)
            return fail("unreachable repair assignment survives save and hash");
        for (int y = -3; y <= 3; ++y)
            for (int x = -3; x <= 3; ++x)
                if ((x || y) && L_Contains(&level, cell.x + x, cell.y + y))
                    level.blocked[L_Index(&level, cell.x + x, cell.y + y)] = 0;
        P_NavFree(&level);
        if (mode) {
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                mobj_t *mo = (mobj_t *)th;
                if (th->function == P_MobjThinker && mo->type_id == MT_REPAIR && mo != facility) Z_SetTeam(mo, 0);
            }
            if (mode == 1) Z_SetTeam(facility, 0);
            else facility->hp = 0;
            for (int i = 0; i < 5; ++i) { map->ai.delay = 0; think(1); }
            if (task->task == 11 || task->pending || tank->z.service)
                return fail("lost repair facility releases failed assignment");
            mobj_t *replacement = P_SpawnMobj(facility->core.position, MT_REPAIR);
            if (!replacement) return fail("spawn replacement repair facility");
            Z_SetTeam(replacement, 1);
            facility = replacement;
        }
        if (!Z_ServiceApproach(tank, facility, &goal)) return fail("repair route opens after load");
        for (int i = 0; i < 10 && tank->z.service != facility->id; ++i) { map->ai.delay = 0; think(1); }
        if (tank->z.service != facility->id)
            return fail("damaged vehicle retries repair after route or facility changes");
        rts_game_model_destroy(model);
    }
    return 0;
}

static int test_bridge_takeover_unreachable_return(void) {
    RtsGameModel *model = load_map("LEVEL14/LEVEL14.MAP");
    if (!model) return fail("load bridge takeover fixture");
    service_fixture();
    zmap_t *map = level.native_data;
    mobj_t *bridge = nth(MT_BRIDGE, Z_NEUTRAL, 0);
    if (!bridge || !map->bridges[bridge->z.bridge - 1].cpu_rebuild)
        return fail("bridge takeover target");
    bridge->hp = 0;
    mobj_t *old = service_unit(MT_UNIT_CRANE, fixed2_cell_center(map->territories[4].flag));
    mobj_t *near = service_unit(MT_UNIT_CRANE, fixed3_xy(bridge->core.position));
    if (!old || !near) return fail("spawn bridge takeover cranes");
    think(0);
    zaitask_t *previous = task_of(old), *next = task_of(near);
    if (!previous || !next) return fail("register bridge takeover tasks");
    previous->task = 9;
    previous->service = bridge->id;
    previous->pending = true;
    previous->execute_at = map->ai.clock + 10000;
    map->forts[0] = (ivec2_t){-10000, -10000};
    map->ai.delay = 0;
    think(1);
    if (previous->task != 8 || previous->pending || previous->service || old->z.service ||
        next->task != 9 || (next->service != bridge->id && near->z.service != bridge->id))
        return fail("bridge takeover clears displaced pending assignment when fort is unreachable");
    for (int i = 0; i < 3; ++i) think(1);
    if (previous->service == bridge->id || old->z.service == bridge->id ||
        (next->service != bridge->id && near->z.service != bridge->id))
        return fail("displaced crane does not reclaim bridge");
    rts_game_model_destroy(model);
    return 0;
}

/* 0x9c7f2 stops at the first crane that cannot take any bridge. */
static int test_bridge_scan_stops(void) {
    RtsGameModel *model = load_map("LEVEL14/LEVEL14.MAP");
    if (!model) return fail("bridge scan load");
    service_fixture();
    zmap_t *map = level.native_data;
    mobj_t *bridge = nth(MT_BRIDGE, Z_NEUTRAL, 0);
    if (!bridge || !map->bridges[bridge->z.bridge - 1].cpu_rebuild) return fail("bridge scan target");
    for (int i = 0; i < Z_BRIDGES; ++i)
        if (i != bridge->z.bridge - 1) map->bridges[i].cpu_rebuild = false;
    bridge->hp = 0;
    fixed2_t centre = fixed3_xy(bridge->core.position);
    mobj_t *old = service_unit(MT_UNIT_CRANE, centre);
    mobj_t *far = service_unit(MT_UNIT_CRANE, fixed2_cell_center(map->territories[4].flag));
    mobj_t *near = service_unit(MT_UNIT_CRANE, centre);
    if (!old || !far || !near) return fail("bridge scan cranes");
    /* Place the existing owner between the first and second candidate. */
    old->core.position = fixed3_from_fixed2(fixed2_add(centre, (fixed2_t){FIXED_ONE * 3, 0}), 0);
    near->core.position = fixed3_from_fixed2(centre, 0);
    if (fixed2_distance_squared64(fixed3_xy(far->core.position), centre) <=
        fixed2_distance_squared64(fixed3_xy(old->core.position), centre))
        return fail("bridge scan first crane is farther away");
    think(0);
    zaitask_t *owner = task_of(old), *first = task_of(far), *second = task_of(near);
    if (!owner || !first || !second) return fail("bridge scan register");
    owner->task = 9; owner->service = bridge->id; owner->pending = true;
    owner->execute_at = map->ai.clock + 10000;
    map->ai.delay = 0;
    think(1);
    if (owner->task != 9 || owner->service != bridge->id || second->task == 9 || near->z.service)
        return fail("farther first crane ends bridge pass");
    far->hp = 0;
    map->ai.delay = 0;
    think(1);
    if (second->task != 9 || (second->service != bridge->id && near->z.service != bridge->id))
        return fail("closer crane takes over on next bridge pass");
    rts_game_model_destroy(model);
    puts("PASS: z bridge search stops at first unsuccessful crane");
    return 0;
}

static int test_bridge_and_crane(void) {
    RtsGameModel *model = load_map("LEVEL14/LEVEL14.MAP");
    if (!model) return fail("bridge campaign load");
    service_fixture();
    zmap_t *map = level.native_data;
    mobj_t *bridge = nth(MT_BRIDGE, Z_NEUTRAL, 0);
    if (!bridge || bridge->hp != 600 || bridge->z.bridge != 1 || !map->bridges[0].cpu_rebuild || bridge->z.hold)
        return fail("LEVEL14 bridge slot, health and CPU eligibility");
    fixed3_t firing_position = bridge->core.position;
    firing_position.x -= FIXED_ONE;
    mobj_t *tank = P_SpawnMobj(firing_position, MT_UNIT_LIGHT_TANK);
    if (!tank) return fail("spawn bridge attacker");
    Z_SetTeam(tank, 0);
    if (!Z_Attack(tank, &tank->info->attack, bridge)) return fail("fire shell at bridge");
    mobj_t *shot = nth(MT_SHELL, 0, 0);
    if (!shot) return fail("bridge shell exists");
    P_ExplodeMissile(shot);
    if (bridge->hp != 550)
        return fail("shell deals its direct damage to a bridge exactly once");
    P_RemoveMobj(tank);
    P_DamageMobj(bridge, NULL, bridge->hp);
    ivec2_t deck = {368 / Z_TILE + 2, 304 / Z_TILE + 1};
    if (!level.blocked[L_Index(&level, deck.x, deck.y)] ||
        (map->terrain[deck.y * Z_MAP_STRIDE + deck.x] & 0x7f) != 0x2f || !nth(MT_BRIDGE_DEBRIS, -1, 0))
        return fail("bridge destruction blocks the deck and leaves native debris");
    mobj_t *crane = service_unit(MT_UNIT_CRANE, fixed3_xy(bridge->core.position));
    if (!crane) return fail("crane fixture at bridge approach");
    think(0); think(1);
    if (crane->z.service != bridge->id || task_of(crane)->task != 9)
        return fail("CPU sends its crane to the destroyed bridge");
    bool deploying = false, working = false, stowing = false;
    for (int i = 0; i < 10 * RTS_TICRATE && crane->z.service; ++i) {
        if (!tick(model, 1)) return fail("crane work simulation");
        if (crane->core.state_id != S_CRANE_WORK) continue;
        int frame = crane->core.state_frame % Z_CRANE_SITE_FRAMES;
        int site = crane->core.state_frame / Z_CRANE_SITE_FRAMES;
        if (site < Z_CRANE_SOUTH || site > Z_CRANE_EAST) return fail("native bridge approach pose");
        if (frame < Z_CRANE_POSES && !working && !bridge->z.service_work) deploying = true;
        if (frame >= Z_CRANE_WORK_FRAME && deploying && bridge->z.service_work > 0) working = true;
        if (frame >= Z_CRANE_POSES && frame < Z_CRANE_WORK_FRAME && working &&
            bridge->z.service_work >= bridge->max_hp) stowing = true;
    }
    if (!deploying || !working || !stowing) return fail("crane deploys, moves its hook, then stows");
    if (!tick(model, 15 * RTS_TICRATE) || bridge->hp != bridge->max_hp ||
        level.blocked[L_Index(&level, deck.x, deck.y)])
        return fail("crane rebuilds the bridge and opens its deck");
    for (int i = 0; nth(MT_BRIDGE_DEBRIS, -1, i); ++i)
        if (nth(MT_BRIDGE_DEBRIS, -1, i)->z.bridge == bridge->z.bridge)
            return fail("reconstruction clears the bridge's debris");
    puts("PASS: z native bridge records, crane dispatch and rebuilt crossing");
    rts_game_model_destroy(model);
    return 0;
}

static int test_automatic_bridge(void) {
    RtsGameModel *model = load_map("LEVEL31/LEVEL31.MAP");
    if (!model) return fail("automatic bridge map load");
    service_fixture();
    zmap_t *map = level.native_data;
    mobj_t *bridge = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == MT_BRIDGE && mo->z.bridge == 1) bridge = mo;
    }
    if (!bridge || bridge->hp || bridge->z.hold != 58 * 17 + 1)
        return fail("LEVEL31 slot 0 starts collapsed with its native timer");
    irect_t bounds = map->bridges[0].bounds;
    size_t deck = L_Index(&level, bounds.x / Z_TILE + 1, bounds.y / Z_TILE + 1);
    if (!level.blocked[deck]) return fail("automatic bridge starts with a closed deck");

    zaitask_t *reservation = &map->ai.tasks[0];
    *reservation = (zaitask_t){.pending = true, .service = bridge->id};
    Z_BridgeTick(bridge);
    if (bridge->z.hold != 58 * 17 + 1) return fail("queued crane reserves automatic bridge");
    *reservation = (zaitask_t){0};
    mobj_t *crane = service_unit(MT_UNIT_CRANE, fixed3_xy(bridge->core.position));
    if (!crane) return fail("automatic bridge crane fixture");
    crane->z.service = bridge->id;
    Z_BridgeTick(bridge);
    if (bridge->z.hold != 58 * 17 + 1) return fail("active crane reserves automatic bridge");
    P_RemoveMobj(crane);

    if (!tick(model, 422) || bridge->hp || bridge->z.hold != 3 || !level.blocked[deck])
        return fail("bridge stays closed while its native stock remains");
    if (!tick(model, 1) || bridge->hp || bridge->z.hold || !bridge->z.service_ticks || level.blocked[deck])
        return fail("timer expiry opens the deck and starts segment effects before HP returns");
    uint32_t id = bridge->id;
    int effects = bridge->z.service_ticks;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Rebuilding bridge", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save automatic reconstruction");
    free(save);
    bridge = Z_ServiceTarget(id);
    if (!bridge || bridge->hp || bridge->z.service_ticks != effects || level.blocked[deck] ||
        !tick(model, RTS_TICRATE) || bridge->hp != 600 || bridge->z.service_ticks ||
        bridge->z.hold != 58 * 17 + 1) return fail("restored effects finish and reset the bridge timer");
    puts("PASS: z native bridge timer, crane reservation, segment effects and save/load");
    rts_game_model_destroy(model);
    return 0;
}

static int test_task_seven(void) {
    RtsGameModel *model = load_map("LEVEL12/LEVEL12.MAP");
    if (!model) return fail("task 7 campaign load");
    service_fixture();
    mobj_t *facility = nth(MT_REPAIR, -1, 0);
    if (!facility) return fail("task 7 building fixture");
    Z_SetTeam(facility, 1);
    zmap_t *map = level.native_data;
    ivec2_t fort_exit = ivec2_add(map->forts[0], (ivec2_t){80,200});
    fixed2_t goal = fixed2_scale(fixed2_from_cell(fort_exit), FIXED_ONE / Z_TILE);
    mobj_t *unit = service_unit(MT_UNIT_FIRE_TRUCK, goal);
    if (!unit) return fail("task 7 unit fixture");
    think(0); think(1);
    zaitask_t *task = task_of(unit);
    if (!task) return fail("task 7 task fixture");
    /* The executable has a task-7 consumer but no confirmed assignment. */
    task->task = 7;
    task->idle = 0;
    unit->target = facility;
    if (!P_MoveUnitTo(&level, unit, goal)) return fail("task 7 movement fixture");
    facility->hp--;
    think(1);
    if (task->task != 7 || unit->target != facility) return fail("task 7 keeps a damaged owned building");
    facility->hp = facility->max_hp;
    think(1);
    if (task->task != 8 || unit->target) return fail("task 7 returns to fort at full HP");
    facility->hp--;
    ivec2_t origin = Z_BuildingOrigin(facility);
    int territory = Z_AITerritory(map, fixed2_scale(fixed2_from_cell(origin), FIXED_ONE / Z_TILE));
    if (territory < 0) return fail("task 7 territory fixture");
    map->territories[territory].team = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *flag = (mobj_t *)th;
        if (th->function == P_MobjThinker && flag->type_id == MT_FLAG && flag->z.territory == territory + 1)
            Z_SetTeam(flag, 0);
    }
    task->task = 7;
    task->idle = 0;
    unit->target = facility;
    think(1);
    if (task->task != 8 || unit->target) return fail("task 7 returns when building territory changes hands");
    task->task = 7;
    task->idle = 0;
    task->pending = true;
    task->service = facility->id;
    task->enemy = facility->id;
    task->execute_at = map->ai.clock + 10000;
    unit->target = facility;
    map->forts[0] = (ivec2_t){-10000, -10000};
    map->ai.delay = 0;
    think(1);
    if (task->task != 8 || task->pending || task->service || task->enemy || unit->target)
        return fail("unreachable service return cancels the superseded pending order");
    puts("PASS: z task 7 retains damaged owned buildings and returns when healed");
    rts_game_model_destroy(model);
    return 0;
}

/* 0x9c373 stops the repair pass if its first vehicle has no nearby bay. */
static int test_repair_scan_stops(void) {
    RtsGameModel *model = load_map("LEVEL12/LEVEL12.MAP");
    if (!model) return fail("repair scan load");
    service_fixture();
    mobj_t *facility = nth(MT_REPAIR, -1, 0);
    if (!facility) return fail("repair scan facility");
    for (int i = 0; nth(MT_REPAIR, -1, i); ++i) Z_SetTeam(nth(MT_REPAIR, -1, i), 0);
    Z_SetTeam(facility, 1);
    mobj_t *far = service_unit(MT_UNIT_MED_TANK, fixed3_xy(facility->core.position));
    mobj_t *near = service_unit(MT_UNIT_MED_TANK, fixed3_xy(facility->core.position));
    if (!far || !near) return fail("repair scan vehicles");
    far->core.position = fixed3_from_fixed2(fixed2_cell_center((ivec2_t){1,1}), 0);
    ivec2_t origin = Z_BuildingOrigin(facility);
    isize2_t size = facility->info->footprint;
    ivec2_t centre = ivec2_add(origin, (ivec2_t){size.w * 8 - 8, size.h * 8 - 8});
    if (ivec2_distance_squared64(Z_AITopLeft(far), centre) < 256 * 256)
        far->core.position = fixed3_from_fixed2(fixed2_cell_center((ivec2_t){level.width - 2,level.height - 2}), 0);
    if (ivec2_distance_squared64(Z_AITopLeft(far), centre) < 256 * 256 ||
        ivec2_distance_squared64(Z_AITopLeft(near), centre) >= 256 * 256)
        return fail("repair scan distance fixtures");
    far->hp = far->max_hp / 2 - 1;
    near->hp = near->max_hp / 2 - 1;
    think(0); think(1);
    zaitask_t *first = task_of(far), *second = task_of(near);
    if (!first || !second || (first->flags & 16) || (second->flags & 16) ||
        first->task != -1 || second->task != -1 || near->z.service)
        return fail("first out-of-range vehicle stops repair pass");
    far->hp = far->max_hp;
    think(1);
    if (second->task != 11 || near->z.service != facility->id)
        return fail("next repair pass can service the nearby vehicle");
    rts_game_model_destroy(model);
    puts("PASS: z out-of-range vehicle stops the repair pass");
    return 0;
}

static int test_full_health_repair(void) {
    for (int mode = 0; mode < 3; ++mode) {
        bool queued = mode == 1;
        RtsGameModel *model = load_map("LEVEL12/LEVEL12.MAP");
        if (!model) return fail("full-health repair load");
        service_fixture();
        mobj_t *facility = nth(MT_REPAIR, -1, 0);
        if (!facility) return fail("full-health repair facility");
        Z_SetTeam(facility, 1);
        mobj_t *tank = service_unit(MT_UNIT_MED_TANK, fixed3_xy(facility->core.position));
        if (!tank) return fail("full-health repair tank");
        if (mode == 2) tank->hp--;
        if (queued) {
            ticcmd_t move = {.order = TC_MOVE, .position = tank->core.position, .count = 1, .units = {tank->id}};
            G_RunTiccmd(1, &move);
        }
        ticcmd_t repair = {.order = TC_REPAIR, .target = facility->id, .position = facility->core.position,
                           .queued = queued, .count = 1, .units = {tank->id}};
        G_RunTiccmd(1, &repair);
        if (queued && !tank->z.route.count) return fail("full-health repair is queued");
        if (queued) {
            uint32_t tank_id = tank->id, facility_id = facility->id;
            app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
            if (!G_SaveGameBlob(&save, &bytes, "Full-health repair", &app, rts_game_model_ai(model), &hud) ||
                !G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud))
                return fail("save queued full-health repair");
            free(save); tank = P_MobjById(tank_id); facility = P_MobjById(facility_id);
            if (!tank || !facility || !tank->z.route.count) return fail("restore queued full-health repair");
        }
        if (mode == 2) tank->hp = tank->max_hp; /* Health changes during approach. */
        for (int i = 0; i < 8 * RTS_TICRATE && tank->z.service_work >= 0; ++i)
            if (!tick(model, 1)) return fail("full-health tank approaches repair bay");
        const zmap_t *map = level.native_data;
        if (tank->z.service_work != -(2 * 880 / map->production_speed) ||
            tank->hp != tank->max_hp || tank->z.service != facility->id)
            return fail("full-health tank enters friendly repair bay");
        for (int i = 0; i < 15 * RTS_TICRATE && tank->z.service; ++i)
            if (!tick(model, 1)) return fail("full-health repair completes");
        if (tank->z.service || tank->hp != tank->max_hp || !(tank->traits & MF_MOBILE))
            return fail("full-health tank leaves repair bay");
        rts_game_model_destroy(model);
    }
    puts("PASS: z full-health vehicles accept immediate and queued repair orders");
    return 0;
}

static int test_repair_facility(void) {
    RtsGameModel *model = load_map("LEVEL12/LEVEL12.MAP");
    if (!model) return fail("repair campaign load");
    service_fixture();
    mobj_t *facility = nth(MT_REPAIR, -1, 0);
    if (!facility) return fail("native repair facility fixture");
    Z_SetTeam(facility, 1);
    mobj_t *tank = service_unit(MT_UNIT_MED_TANK, fixed3_xy(facility->core.position));
    if (!tank) return fail("tank fixture near repair bay");
    P_DamageMobj(tank, NULL, tank->max_hp / 2);
    if (!tank->z.body_tracked || tank->z.body_speed != 34 || tank->z.body_accuracy != 68 || tank->z.body_fire != 59)
        return fail("repair fixture has actual body degradation");
    think(0); think(1);
    if (tank->z.service) return fail("exactly half health does not trigger repairs");
    zaitask_t *task = task_of(tank);
    if (!task || task->task != -1) return fail("register idle repair candidate");
    task->pending = true;
    task->execute_at = ((zmap_t *)level.native_data)->ai.clock + 1000;
    task->order = fixed3_xy(tank->core.position);
    tank->hp--;
    think(1);
    if (tank->z.service != facility->id || task->task != 11 ||
        task->execute_at >= ((zmap_t *)level.native_data)->ai.clock + 1000)
        return fail("damaged reserve replaces its queued order with repair");
    for (int i = 0; i < 4 * RTS_TICRATE && tank->z.service_work >= 0; ++i)
        if (!tick(model, 1)) return fail("repair approach simulation");
    if (tank->z.service_work >= 0) return fail("tank enters the repair bay");
    if (tank->hp >= tank->max_hp || tank->z.body_speed != 40 || tank->z.body_base_speed != 40 ||
        tank->z.body_accuracy != 80 || tank->z.body_fire != 59)
        return fail("bay entry restores speed and accuracy before health, retaining fire rate");
    uint32_t tank_id = tank->id, facility_id = facility->id;
    int remaining = tank->z.service_work;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Vehicle in repair", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save active vehicle repair");
    free(save);
    tank = P_MobjById(tank_id); facility = P_MobjById(facility_id);
    if (!tank || !facility || tank->z.service != facility_id || tank->z.service_work != remaining ||
        tank->z.body_speed != 40 || tank->z.body_base_speed != 40 || tank->z.body_accuracy != 80 || tank->z.body_fire != 59)
        return fail("restore bay occupant and exact timer");
    Z_SetTeam(facility, 0);
    if (!tick(model, 1) || tank->z.service || !(tank->traits & MF_MOBILE) || tank->hp == tank->max_hp)
        return fail("loss of the bay releases the tank with its current health");
    Z_SetTeam(facility, 1);
    tank->hp = 40;
    if (!tick(model, 15 * RTS_TICRATE) || tank->z.service || tank->hp != tank->max_hp)
        return fail("friendly repair bay restores health and releases the tank");
    P_DamageMobj(tank, NULL, tank->max_hp / 4);
    if (tank->z.body_speed != 36 || tank->z.body_accuracy != 72 || tank->z.body_fire != 53)
        return fail("new damage scales restored speed and accuracy but retained fire rate");
    P_RemoveMobj(tank);
    mobj_t *apc = service_unit(MT_UNIT_APC, fixed3_xy(facility->core.position));
    if (!apc) return fail("reboarded APC repair fixture");
    apc->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    P_DamageMobj(apc, NULL, apc->max_hp / 4);
    if (!Z_EjectCrew(apc)) return fail("damaged APC exits before repair");
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->team == 1 && Z_IsRobot(Z_UnitType(mo))) P_RemoveMobj(mo);
    }
    mobj_t *psycho = P_SpawnMobj(apc->core.position, MT_UNIT_PSYCHO);
    if (!psycho) return fail("APC replacement driver for repair");
    Z_SetTeam(psycho, 1);
    if (!Z_DetourBoard(psycho, apc)) return fail("replacement driver boards APC for repair");
    P_DamageMobj(apc, NULL, apc->max_hp / 4 + 1);
    if (apc->z.body_speed != 38 || apc->z.body_base_speed != 45 || apc->z.body_fire != 83)
        return fail("reboarded APC has independently damaged speed and fire");
    fixed2_t goal;
    if (!Z_ServiceApproach(apc, facility, &goal) || !Z_ServiceOrder(apc, facility, goal))
        return fail("reboarded APC starts repair order");
    for (int i = 0; i < 8 * RTS_TICRATE && apc->z.service_work >= 0; ++i)
        if (!tick(model, 1)) return fail("reboarded APC approaches repair bay");
    if (apc->z.service_work >= 0 || apc->z.body_speed != 45 ||
        apc->z.body_accuracy != Z_UnitAccuracy(MT_UNIT_PSYCHO - MT_UNITS) || apc->z.body_fire != 83)
        return fail("repair bay restores speed retained when APC was boarded");
    puts("PASS: z reserve repair threshold, bay ownership and release");
    rts_game_model_destroy(model);
    return 0;
}

static int test_building_service(void) {
    RtsGameModel *model = load_map("LEVEL12/LEVEL12.MAP");
    if (!model) return fail("building service campaign load");
    service_fixture();
    mobj_t *facility = nth(MT_REPAIR, -1, 0);
    if (!facility) return fail("building service fixture");
    Z_SetTeam(facility, 1);
    mobj_t *crane = service_unit(MT_UNIT_CRANE, fixed3_xy(facility->core.position));
    if (!crane) return fail("crane fixture near destroyed building");
    P_DamageMobj(facility, NULL, facility->hp);
    if (facility->remove || facility->core.state_id != S_REPAIR_WRECK)
        return fail("ruin stays on the map with its native wreck state");
    ticcmd_t move = {.order = TC_MOVE, .position = crane->core.position, .count = 1, .units = {crane->id}};
    G_RunTiccmd(1, &move);
    zroute_t previous = crane->z.route;
    fixed3_t crane_position = crane->core.position;
    crane->core.position = (fixed3_t){-FIXED_FROM_INT(100), -FIXED_FROM_INT(100), 0};
    ticcmd_t unreachable = {.order = TC_REPAIR, .target = facility->id, .position = facility->core.position,
                            .count = 1, .units = {crane->id}};
    G_RunTiccmd(1, &unreachable);
    if (memcmp(&crane->z.route, &previous, sizeof(previous)) || crane->z.service)
        return fail("unreachable repair preserves the active order");
    unreachable.queued = true;
    G_RunTiccmd(1, &unreachable);
    P_ClearMove(crane); crane->movement.order_arrived = true;
    Z_RouteTick(crane);
    if (crane->z.route.count || crane->z.route.active || crane->z.service)
        return fail("unreachable queued repair is skipped");
    crane->core.position = crane_position;
    G_RunTiccmd(1, &move);
    ticcmd_t repair = {.order = TC_ORDER, .position = facility->core.position, .queued = true,
                       .count = 1, .units = {crane->id}};
    G_RunTiccmd(1, &repair);
    if (crane->z.route.count != 1 || crane->z.route.steps[0].order != TC_REPAIR ||
        crane->z.route.steps[0].target != facility->id) return fail("queued ground click resolves wreck ID");
    P_ClearMove(crane); crane->movement.order_arrived = true;
    Z_RouteTick(crane);
    if (crane->z.service != facility->id) return fail("queued wreck order starts crane service");
    Z_CancelService(crane); P_ClearMove(crane); crane->z.route = (zroute_t){0};
    think(0); think(1);
    if (crane->z.service != facility->id) return fail("crane gets the dead building before bridge work");
    for (int i = 0; i < 8 * RTS_TICRATE && crane->core.state_id != S_CRANE_WORK; ++i)
        if (!tick(model, 1)) return fail("building crane approach simulation");
    if (crane->core.state_id != S_CRANE_WORK || crane->core.state_frame / Z_CRANE_SITE_FRAMES != Z_CRANE_BUILDING)
        return fail("ruin uses the building work poses");
    if (!tick(model, 12 * RTS_TICRATE) || facility->hp != facility->max_hp ||
        facility->core.state_id != S_REPAIR || !(facility->traits & MF_SELECTABLE))
        return fail("crane restores the destroyed repair facility");
    puts("PASS: z native building reconstruction and wreck state");
    rts_game_model_destroy(model);
    return 0;
}

static int test_building_alert(void) {
    RtsGameModel *model = load();
    if (!model) return fail("building alert campaign load");
    service_fixture();
    mobj_t *factory = nth(MT_ROBOT_FACTORY, -1, 0);
    if (!factory) return fail("building alert fixture");
    Z_SetTeam(factory, 1);
    zmap_t *map = level.native_data;
    mobj_t *defender = service_unit(MT_UNIT_GRUNT, fixed3_xy(factory->core.position));
    mobj_t *enemy = service_unit(MT_UNIT_GRUNT, fixed2_cell_center(map->territories[8].flag));
    if (!defender || !enemy) return fail("building alert unit fixtures");
    Z_SetTeam(enemy, 0);
    P_DamageMobj(factory, enemy, 1);
    think(0); think(1);
    if (defender->attack.target != enemy) return fail("a struck building calls its local defenders to the attacker");
    puts("PASS: z building damage triggers the local reserve response");
    rts_game_model_destroy(model);
    return 0;
}
int main(void) {
    return test_settings() || test_arithmetic() || test_factory_choice() || test_intelligence_and_radar() ||
           test_initial_and_save() || test_scores() || test_damaged_accuracy_value() || test_driver_health_value() || test_apc_driver_value() || test_empty_object_value_slots() || test_ejected_object_health_value() || test_destroyed_building_value() ||
           test_blocked_flag_retry() || test_strength_gate() || test_pending_assault_force() || test_negative_garrison_value() || test_garrison_one_per_pass() || test_blocked_assault_flag() || test_fort_followup() || test_unreachable_staging() || test_unreachable_flag() || test_unreachable_initial_staging() || test_pending_reserve_home() || test_home_order_backlog() || test_stray_home_reassignment() || test_reserve_task_not_expired() || test_frontier_overlap() || test_unreachable_frontier() || test_restricted_territory() || test_restricted_dispatch() || test_vehicles() || test_unreachable_vehicle_choice() || test_vehicle_scan_stops() || test_defence_signed_importance() || test_defence_many_sightings() || test_fixed_gun_defence_donor() || test_reinforce_requires_idle() || test_reinforce_negative_garrison() || test_retreat() || test_retreat_without_staging() || test_recapture() || test_unreachable_recapture() ||
           test_unreachable_service_return() || test_unreachable_crane_assignment() || test_unreachable_vehicle_repair() || test_bridge_takeover_unreachable_return() || test_bridge_scan_stops() || test_bridge_and_crane() || test_automatic_bridge() || test_task_seven() ||
           test_repair_scan_stops() || test_full_health_repair() || test_repair_facility() || test_building_service() || test_building_alert();
}
