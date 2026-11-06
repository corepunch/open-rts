#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *msg) { return rts_fail("z commander", msg); }
static RtsGameModel *load(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/Z"};
    if (model && rts_game_model_load(model, &config)) return model;
    rts_game_model_destroy(model);
    return NULL;
}
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
int main(void) {
    return test_settings() || test_arithmetic() || test_factory_choice() || test_intelligence_and_radar() ||
           test_initial_and_save() || test_scores() ||
           test_strength_gate() || test_vehicles() || test_retreat() || test_recapture();
}
