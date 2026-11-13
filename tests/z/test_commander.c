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
/* 0x993ff: an attack that becomes too weak, with nobody to help, falls
 * back and forgets its target. */
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
    puts("PASS: z task 7 retains damaged owned buildings and returns when healed");
    rts_game_model_destroy(model);
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
    tank->hp = tank->max_hp / 2;
    think(0); think(1);
    if (tank->z.service) return fail("exactly half health does not trigger repairs");
    tank->hp--;
    think(1);
    if (tank->z.service != facility->id || task_of(tank)->task != 11)
        return fail("idle damaged reserve goes to its nearest friendly facility");
    for (int i = 0; i < 4 * RTS_TICRATE && tank->z.service_work >= 0; ++i)
        if (!tick(model, 1)) return fail("repair approach simulation");
    if (tank->z.service_work >= 0) return fail("tank enters the repair bay");
    uint32_t tank_id = tank->id, facility_id = facility->id;
    int remaining = tank->z.service_work;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Vehicle in repair", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save active vehicle repair");
    free(save);
    tank = P_MobjById(tank_id); facility = P_MobjById(facility_id);
    if (!tank || !facility || tank->z.service != facility_id || tank->z.service_work != remaining)
        return fail("restore bay occupant and exact timer");
    Z_SetTeam(facility, 0);
    if (!tick(model, 1) || tank->z.service || !(tank->traits & MF_MOBILE) || tank->hp == tank->max_hp)
        return fail("loss of the bay releases the tank with its current health");
    Z_SetTeam(facility, 1);
    tank->hp = 40;
    if (!tick(model, 15 * RTS_TICRATE) || tank->z.service || tank->hp != tank->max_hp)
        return fail("friendly repair bay restores health and releases the tank");
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
           test_initial_and_save() || test_scores() ||
           test_strength_gate() || test_fort_followup() || test_frontier_overlap() || test_restricted_territory() || test_restricted_dispatch() || test_vehicles() || test_retreat() || test_recapture() ||
           test_bridge_and_crane() || test_automatic_bridge() || test_task_seven() ||
           test_repair_facility() || test_building_service() || test_building_alert();
}
