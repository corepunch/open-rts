#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The robot coordinate attack and the CPU's building attack task 6
 * (ZED.EXE 0x7c817 -> 0x7cb27 -> 0x75046, 0x9a801, 0x9a9ae). */

static int fail(const char *msg) { return rts_fail("z point attack", msg); }
static RtsGameModel *load(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/Z"};
    if (model && rts_game_model_load(model, &config)) {
        P_AiSetFeatures(rts_game_model_ai(model), 0);
        return model;
    }
    rts_game_model_destroy(model);
    return NULL;
}
static bool tick(RtsGameModel *model, int tics) {
    RtsRenderSnapshot snap;
    for (int t = 0; t < tics; ++t) if (!rts_tick(model, &snap)) return false;
    return true;
}
static int count_type(int type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == type && !mo->remove) ++n;
    }
    return n;
}
/* A robot whose first picture's top-left pixel is `pixel`. */
static mobj_t *robot(int type, int team, ivec2_t pixel, int squad, int slot, int grenades) {
    fixed2_t centre;
    if (!Z_UnitCentre(level.native_data, type - MT_UNITS, pixel, &centre)) return NULL;
    mobj_t *mo = P_SpawnMobj(fixed3_from_fixed2(centre, 0), type);
    if (!mo) return NULL;
    Z_SetTeam(mo, team);
    mo->z.squad = (uint8_t)squad;
    mo->z.squad_slot = (uint8_t)slot;
    mo->z.grenades = (uint8_t)grenades;
    for (thinker_t *th = thinkercap.next; th != &thinkercap && !mo->z.robot; th = th->next) { /* Native valuation needs the ROBOTS.DAT record. */
        const mobj_t *other = (const mobj_t *)th;
        if (th->function == P_MobjThinker && other != mo && other->type_id == type && other->z.robot) mo->z.robot = other->z.robot;
    }
    return mo;
}
static mobj_t *first_building(int team) {
    mobj_t *best = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->z.building && !mo->remove && (team < 0 || mo->team == team) &&
            (!best || mo->z.building < best->z.building)) best = mo;
    }
    return best;
}
static ivec2_t aim_of(const mobj_t *b) {
    static const ivec2_t offsets[] = {[0] = {31, 28}, [1] = {32, 60}, [4] = {40, 48}, [8] = {23, 9}};
    return ivec2_add(Z_BuildingOrigin(b), offsets[b->native_type_id]);
}
static void put(mobj_t *mo, ivec2_t pixel) {
    fixed2_t centre;
    Z_UnitCentre(level.native_data, Z_UnitType(mo), pixel, &centre);
    mo->core.position = fixed3_from_fixed2(centre, 0);
}
static int total_grenades(int squad) {
    int sum = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && !mo->remove && mo->z.squad == squad) sum += mo->z.grenades;
    }
    return sum;
}

static int test_range_rule(void) {
    RtsGameModel *model = load();
    if (!model) return fail("range load");
    mobj_t *grunt = robot(MT_UNIT_GRUNT, 0, (ivec2_t){400, 400}, 201, UINT8_MAX, 1);
    if (!grunt) return fail("range fixture");
    ivec2_t at = Z_AITopLeft(grunt);
    /* 0x75285: root of the squared 16-pixel cells, times four, below 2 * (120 >> 3). */
    static const struct { int dx; bool in; } cases[] = {
        {127, true}, {128, false}, {-112, true}, {-113, false}, {0, true}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        ivec2_t point = {at.x - cases[i].dx, at.y};
        if (Z_PointInRange(grunt, point) != cases[i].in) return fail("throw range boundary of the 16-pixel root rule");
    }
    if (Z_PointInRange(grunt, (ivec2_t){at.x - 96, at.y - 96}) || !Z_PointInRange(grunt, (ivec2_t){at.x + 80, at.y - 80}))
        return fail("diagonal throw range");
    /* 0x7bf85: the no-grenade move stops at squared distance <= (range byte * 8)^2. */
    if (!Z_PointMoveDone(grunt, (ivec2_t){at.x + 120, at.y}) || Z_PointMoveDone(grunt, (ivec2_t){at.x + 121, at.y}) ||
        !Z_PointMoveDone(grunt, (ivec2_t){at.x + 72, at.y + 96}) || Z_PointMoveDone(grunt, (ivec2_t){at.x + 73, at.y + 96}))
        return fail("stop distance boundary is inclusive and squared");
    puts("PASS: z coordinate attack range and stop distance rules");
    rts_game_model_destroy(model);
    return 0;
}

static int test_attackable_point(void) {
    RtsGameModel *model = load();
    if (!model) return fail("attackable load");
    mobj_t *building = first_building(-1);
    if (!building) return fail("building fixture");
    if (!Z_PointAttackable(aim_of(building))) return fail("terrain under a BUILD aim point is attackable (0x193f0)");
    zmap_t *map = level.native_data;
    ivec2_t aim = aim_of(building);
    map->terrain[(aim.y / Z_TILE) * Z_MAP_STRIDE + aim.x / Z_TILE] = 0;
    if (Z_PointAttackable(aim)) return fail("plain ground is not attackable");
    if (Z_PointAttackable((ivec2_t){-4, 10})) return fail("outside the map");
    mobj_t *leader = robot(MT_UNIT_GRUNT, 0, (ivec2_t){aim.x - 40, aim.y}, 202, UINT8_MAX, 2);
    if (!leader || Z_PointAttackOrder(leader, aim) || Z_PointActive(leader)) return fail("rejected point leaves no order");
    puts("PASS: z coordinate attack acceptance");
    rts_game_model_destroy(model);
    return 0;
}

static int test_leader_throw(void) {
    RtsGameModel *model = load();
    if (!model) return fail("leader throw load");
    mobj_t *building = first_building(-1);
    if (!building) return fail("building fixture");
    ivec2_t aim = aim_of(building);
    mobj_t *leader = robot(MT_UNIT_GRUNT, 0, (ivec2_t){aim.x - 40, aim.y}, 203, UINT8_MAX, 3);
    mobj_t *follower = robot(MT_UNIT_GRUNT, 0, (ivec2_t){aim.x - 44, aim.y + 16}, 203, 2, 4);
    if (!leader || !follower) return fail("leader fixture");
    int hp = building->hp, before = count_type(MT_GRENADE);
    if (!Z_PointAttackOrder(leader, aim) || leader->z.point_mode != 1 || leader->attack.target) return fail("order accepted");
    bool thrown = false;
    for (int t = 0; t < 120 && !thrown; ++t) {
        if (!tick(model, 1)) return fail("leader tick");
        thrown = count_type(MT_GRENADE) > before;
    }
    /* 0x753bb: the leader throws itself with no target object; one grenade. */
    if (!thrown || leader->z.grenades != 2 || follower->z.grenades != 4) return fail("the leader throws one of its own grenades");
    if (!tick(model, 60) || Z_PointActive(leader) || leader->z.grenades != 2 || follower->z.grenades != 4)
        return fail("the order completes after the leader's throw (0x79049, group target 0xff)");
    /* Within 48 pixels there is no scatter (0x64250), so the burst lands on the aim point. */
    if (building->hp >= hp) return fail("the grenade burst damages the building at the aim point");
    puts("PASS: z leader throws one grenade at a coordinate and completes");
    rts_game_model_destroy(model);
    return 0;
}

static int test_carrier_choice(void) {
    RtsGameModel *model = load();
    if (!model) return fail("carrier load");
    mobj_t *building = first_building(-1);
    ivec2_t aim = aim_of(building);
    mobj_t *leader = robot(MT_UNIT_GRUNT, 0, (ivec2_t){aim.x - 40, aim.y}, 204, UINT8_MAX, 0);
    mobj_t *empty = robot(MT_UNIT_GRUNT, 0, (ivec2_t){aim.x - 44, aim.y}, 204, 1, 0);
    mobj_t *busy = robot(MT_UNIT_GRUNT, 0, (ivec2_t){aim.x - 44, aim.y + 16}, 204, 2, 5);
    mobj_t *second = robot(MT_UNIT_GRUNT, 0, (ivec2_t){aim.x - 44, aim.y - 16}, 204, 3, 2);
    mobj_t *third = robot(MT_UNIT_GRUNT, 0, (ivec2_t){aim.x - 40, aim.y - 32}, 204, 7, 1);
    if (!leader || !empty || !busy || !second || !third) return fail("carrier fixture");
    int total = total_grenades(204);
    if (!Z_PointAttackOrder(leader, aim) || leader->z.point_mode != 1) return fail("carrier order");
    P_MoveUnitTo(&level, busy, fixed2_add(fixed3_xy(busy->core.position), (fixed2_t){60 * FIXED_ONE, 0}));
    if (!P_HasMoveOrder(busy)) return fail("moving follower fixture");
    if (!tick(model, 1)) return fail("carrier tick");
    /* 0x7545c: the first slot in order holding grenades that is standing. */
    if (!second->z.point_throw || third->z.point_throw || empty->z.point_throw || busy->z.point_throw ||
        second->core.state_id != S_GRUNT_THROW)
        return fail("first standing grenade holder in slot order throws");
    if (!ivec2_equal(second->z.point, aim)) return fail("the carrier receives the point");
    if (!tick(model, 1) || !third->z.point_throw) return fail("the next tick picks the next free holder");
    if (!tick(model, 150)) return fail("carrier drain tick");
    /* The leader keeps waiting while a grenade remains in the group, even one
     * nobody standing can throw (0x7a776 sum, 0x75411 wait). */
    if (second->z.grenades || third->z.grenades || empty->z.grenades || busy->z.grenades != 5 ||
        leader->z.point_mode != 1 || total_grenades(204) != 5 || total - total_grenades(204) != 3)
        return fail("carriers throw their grenades one by one; the moving holder is skipped");
    P_ClearMove(busy);
    if (!tick(model, 300) || leader->z.point_mode || busy->z.grenades)
        return fail("the order ends when the group holds no grenade");
    puts("PASS: z grenade carrier choice and grenade consumption");
    rts_game_model_destroy(model);
    return 0;
}

static int test_approach(void) {
    RtsGameModel *model = load();
    if (!model) return fail("approach load");
    mobj_t *building = first_building(-1);
    ivec2_t aim = aim_of(building);
    mobj_t *leader = robot(MT_UNIT_GRUNT, 0, (ivec2_t){aim.x + 300, aim.y}, 205, UINT8_MAX, 2);
    if (!leader) return fail("approach fixture");
    int before = count_type(MT_GRENADE);
    if (!Z_PointAttackOrder(leader, aim)) return fail("out-of-range order accepted");
    if (!tick(model, 2) || !P_HasMoveOrder(leader) || leader->z.grenades != 2) return fail("out of range: approach first, no throw");
    ivec2_t start = Z_AITopLeft(leader);
    bool thrown = false;
    for (int t = 0; t < 900 && !thrown; ++t) {
        tick(model, 1);
        thrown = count_type(MT_GRENADE) > before;
    }
    if (!thrown) return fail("the leader reaches range and throws");
    ivec2_t at = Z_AITopLeft(leader);
    if (at.x >= start.x - 100 || !Z_PointInRange(leader, aim) || P_HasMoveOrder(leader))
        return fail("the throw starts only inside the range rule and the unit stops");
    puts("PASS: z coordinate attack approaches before throwing");
    rts_game_model_destroy(model);
    return 0;
}

static int test_no_grenade_move(void) {
    RtsGameModel *model = load();
    if (!model) return fail("move load");
    mobj_t *building = first_building(-1);
    ivec2_t aim = aim_of(building);
    mobj_t *leader = robot(MT_UNIT_GRUNT, 0, (ivec2_t){aim.x + 300, aim.y}, 206, UINT8_MAX, 0);
    if (!leader) return fail("move fixture");
    int before = count_type(MT_GRENADE);
    if (!Z_PointAttackOrder(leader, aim) || leader->z.point_mode != 2) return fail("0x7cb93 rewrites to a ranged move");
    bool moved = false;
    for (int t = 0; t < 900 && leader->z.point_mode; ++t) { tick(model, 1); moved |= P_HasMoveOrder(leader); }
    if (!moved || leader->z.point_mode || P_HasMoveOrder(leader) || !Z_PointMoveDone(leader, aim) ||
        count_type(MT_GRENADE) != before)
        return fail("stops within weapon range and throws nothing");
    ivec2_t at = Z_AITopLeft(leader);
    int64_t dx = at.x - aim.x, dy = at.y - aim.y;
    if (dx * dx + dy * dy <= 100 * 100 || dx * dx + dy * dy > 120 * 120)
        return fail("the move ends at weapon range, not at the point");
    puts("PASS: z grenade-less squad moves to range and stops");
    rts_game_model_destroy(model);
    return 0;
}

static int test_tough(void) {
    RtsGameModel *model = load();
    if (!model) return fail("tough load");
    mobj_t *building = first_building(-1);
    ivec2_t aim = aim_of(building);
    mobj_t *tough = robot(MT_UNIT_TOUGH, 0, (ivec2_t){aim.x - 60, aim.y}, 207, UINT8_MAX, 0);
    if (!tough) return fail("tough fixture");
    if (!Z_PointAttackOrder(tough, aim) || tough->z.point_mode != 1) return fail("a Tough takes the order without grenades");
    int seen = 0, shells = 0;
    for (int t = 0; t < 400 && tough->z.point_mode; ++t) {
        tick(model, 1);
        int now = count_type(MT_SHELL);
        if (now > seen) shells += now - seen;
        seen = now;
    }
    if (tough->z.point_mode || shells != 2 || count_type(MT_GRENADE)) return fail("action 0x13 fires two launcher shells and ends");
    puts("PASS: z Tough launcher fires twice at a coordinate");
    rts_game_model_destroy(model);
    return 0;
}

static int test_save_mid_order(void) {
    RtsGameModel *model = load();
    if (!model) return fail("save load");
    mobj_t *building = first_building(-1);
    ivec2_t aim = aim_of(building);
    mobj_t *leader = robot(MT_UNIT_GRUNT, 0, (ivec2_t){aim.x - 60, aim.y}, 208, UINT8_MAX, 0);
    mobj_t *a = robot(MT_UNIT_GRUNT, 0, (ivec2_t){aim.x - 80, aim.y}, 208, 1, 3);
    if (!leader || !a) return fail("save fixture");
    uint32_t leader_id = leader->id, a_id = a->id;
    if (!Z_PointAttackOrder(leader, aim) || !tick(model, 2)) return fail("save order");
    a = P_MobjById(a_id);
    if (!a->z.point_throw) return fail("carrier mid-throw before saving");
    uint32_t hash = G_Consistency();
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Point", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save and load mid-order");
    leader = P_MobjById(leader_id); a = P_MobjById(a_id);
    if (!leader || !a || leader->z.point_mode != 1 || !ivec2_equal(leader->z.point, aim) || !a->z.point_throw ||
        !ivec2_equal(a->z.point, aim) || G_Consistency() != hash)
        return fail("point, provenance and hash survive the save");
    if (!tick(model, 200) || Z_PointActive(P_MobjById(leader_id)) || P_MobjById(a_id)->z.grenades != 0)
        return fail("the restored order finishes the same way");
    /* A malformed saved order is rejected. */
    P_MobjById(leader_id)->z.point_mode = 9;
    free(save); save = NULL;
    if (!G_SaveGameBlob(&save, &size, "Bad point", &app, rts_game_model_ai(model), &hud)) return fail("save malformed order");
    if (G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("malformed saved order is rejected");
    free(save);
    puts("PASS: z coordinate attack survives save and load");
    rts_game_model_destroy(model);
    return 0;
}

/* Commander task 6: a CPU group in a human territory with a human unit near its flag. */
static zaitask_t *task_of(const mobj_t *mo) {
    zmap_t *map = level.native_data;
    for (int i = 0; i < Z_AI_TASKS; ++i) if (map->ai.tasks[i].unit == mo->id) return &map->ai.tasks[i];
    return NULL;
}
static void think(int tics) {
    leveltime += tics;
    mobjlist_t list = P_ListMobjs();
    G_AiInterface()->tactics(&level, 1, list.items, list.count);
    P_FreeMobjList(&list);
}
static int territory_of(const mobj_t *b) {
    return Z_AITerritory(level.native_data, (fixed2_t){Z_BuildingOrigin(b).x * (FIXED_ONE / Z_TILE),
                                                         Z_BuildingOrigin(b).y * (FIXED_ONE / Z_TILE)});
}

static int task_six(bool destroy_first) {
    RtsGameModel *model = load();
    if (!model) return fail("task 6 load");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    zmap_t *map = level.native_data;
    mobj_t *building = first_building(-1);
    int t = territory_of(building);
    if (t < 0) return fail("building territory");
    mobj_t *first = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->z.building && !mo->remove && territory_of(mo) == t &&
            (!first || mo->z.building < first->z.building)) first = mo;
    }
    if (destroy_first) { first->hp = 0; first->traits &= ~MF_SELECTABLE; }
    map->territories[t].importance = 200;
    ivec2_t flag = ivec2_scale(map->territories[t].flag, Z_TILE);
    mobj_t *lead = robot(MT_UNIT_GRUNT, 1, (ivec2_t){flag.x + 24, flag.y + 8}, 209, UINT8_MAX, 2);
    mobj_t *human = robot(MT_UNIT_GRUNT, 0, (ivec2_t){flag.x + 64, flag.y - 64}, 210, UINT8_MAX, 0);
    if (!lead || !human) return fail("task 6 fixture");
    map->computer.intelligence = 127;
    map->computer.allocation = 0;
    think(0);
    map->territories[t].team = 0;
    map->ai.territories[t].owner = 0;
    map->ai.territories[t].flags &= ~3;
    zaitask_t *task = task_of(lead);
    if (!task) return fail("task for the squad");
    task->task = 3;
    task->target = task->destination = (int16_t)t;
    task->current = (int16_t)t;
    task->flags |= 8 | 16;
    task->pending = false;
    map->ai.delay = 0;
    think(1);
    ivec2_t aim = aim_of(first);
    task = task_of(lead);
    if (!task || task->task != 6 || task->pending || task->point)
        return fail("task 6 is set and its command was delivered (execute_at is now, intelligence 127)");
    if (lead->z.point_mode != 1 || !ivec2_equal(lead->z.point, aim))
        return fail("task 6 aims at the first BUILD record's descriptor point, destroyed or live");
    /* Capture retires the task (0x99818). */
    task = task_of(lead);
    map->territories[t].team = 1;
    map->ai.territories[t].owner = 1;
    map->ai.delay = 0;
    think(1);
    if (task->task == 6 || task->task == 1 || task->task == 3) return fail("capturing the destination retires task 6");
    printf("PASS: z task 6 %s first BUILD record\n", destroy_first ? "destroyed" : "live");
    rts_game_model_destroy(model);
    return 0;
}

static int task_six_filters(void) {
    RtsGameModel *model = load();
    if (!model) return fail("task 6 filter load");
    zmap_t *map = level.native_data;
    mobj_t *building = first_building(-1);
    int t = territory_of(building);
    map->territories[t].importance = 200;
    ivec2_t flag = ivec2_scale(map->territories[t].flag, Z_TILE);
    mobj_t *lead = robot(MT_UNIT_GRUNT, 1, (ivec2_t){flag.x + 24, flag.y + 8}, 211, UINT8_MAX, 0);
    mobj_t *human = robot(MT_UNIT_GRUNT, 0, (ivec2_t){flag.x + 64, flag.y - 64}, 212, UINT8_MAX, 0);
    if (!lead || !human) return fail("filter fixture");
    map->computer.allocation = 0;
    map->computer.intelligence = 127;
    think(0);
    map->territories[t].team = 0;
    map->ai.territories[t].owner = 0;
    map->ai.territories[t].flags &= ~3;
    zaitask_t *task = task_of(lead);
    task->task = 3; task->target = task->destination = task->current = (int16_t)t;
    task->flags |= 24; task->pending = false;
    map->ai.delay = 0;
    think(1);
    if (task->task == 6) return fail("a grenadier group with no grenades is not sent (0x9a89c)");
    lead->z.grenades = 1;
    put(human, (ivec2_t){flag.x + 65, flag.y});
    map->ai.delay = 0;
    think(1);
    if (task->task == 6) return fail("a human unit outside the 128-pixel square (inclusive) is ignored");
    put(human, (ivec2_t){flag.x + 64, flag.y + 64});
    map->ai.delay = 0;
    think(1);
    if (task->task != 6) return fail("a human unit on the inclusive boundary triggers task 6");
    puts("PASS: z task 6 grenade and sighting filters");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    return test_range_rule() || test_attackable_point() || test_leader_throw() || test_carrier_choice() ||
           test_approach() || test_no_grenade_move() || test_tough() || test_save_mid_order() ||
           task_six(false) || task_six(true) || task_six_filters();
}
