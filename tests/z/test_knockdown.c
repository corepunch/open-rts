#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Vehicles knock down or run over opposing robots (ZED.EXE 0x58679,
 * 0x58910, 0x60fe6); a knocked robot lies while a vehicle is within 24
 * pixels (0x61f43), gets up in eleven frames (0x61f94) and stands in
 * action 1 keeping its move (0x79049). */

static int fail(const char *msg) { return rts_fail("z knockdown", msg); }

static RtsGameModel *load(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/Z" };
    if (model && rts_game_model_load(model, &config)) {
        P_AiSetFeatures(rts_game_model_ai(model), 0);
        return model;
    }
    rts_game_model_destroy(model);
    return NULL;
}

static void disarm(void) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        ((mobj_t *)th)->traits &= ~MF_ATTACK;
}

/* Put a unit's native top-left at `pixel`. */
static bool place(mobj_t *mo, ivec2_t pixel) {
    fixed2_t centre;
    if (!Z_UnitCentre(level.native_data, Z_UnitType(mo), pixel, &centre)) return false;
    mo->core.position = fixed3_from_fixed2(centre, 0);
    P_ClearMove(mo);
    mo->movement.goal = fixed3_xy(mo->core.position);
    mo->movement.order_arrived = true;
    ivec2_t at = Z_AITopLeft(mo);
    return at.x == pixel.x && at.y == pixel.y;
}

static mobj_t *spawn(int type, int team, ivec2_t pixel, int hp) {
    mobj_t *mo = P_SpawnMobj(fixed3_zero(), (uint16_t)type);
    if (!mo) return NULL;
    Z_SetTeam(mo, team);
    mo->hp = mo->max_hp = hp;
    static uint8_t squad = 100;
    mo->z.squad = squad++; /* 0x64138 bins only grouped units; each its own leader. */
    mo->z.squad_slot = UINT8_MAX;
    mo->traits &= ~MF_ATTACK;
    if (!place(mo, pixel)) return NULL;
    return mo;
}

static int knock_state(const mobj_t *mo) {
    switch (mo->type_id) {
    case MT_UNIT_GRUNT: return S_GRUNT_KNOCK;
    case MT_UNIT_TOUGH: return S_TOUGH_KNOCK;
    default: return -1;
    }
}
static int rise_state(const mobj_t *mo) { return knock_state(mo) + 1; }
static bool lying(const mobj_t *mo) { return mo->core.state_id == knock_state(mo); }
static bool dead(const mobj_t *mo) { return mo->remove || mo->hp <= 0; }

/* Run native updates one engine tic at a time; returns the number run. */
static int run_tic(void) {
    int steps = Z_NativeSteps(leveltime, Z_NATIVE_HZ);
    Z_KnockdownTicker();
    ++leveltime;
    return steps;
}

static void discard(mobj_t *mo) { if (mo && !mo->remove) P_RemoveMobj(mo); }

static int test_rectangle(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level");
    disarm();
    ivec2_t at = {1200, 1200};
    mobj_t *tank = spawn(MT_UNIT_MED_TANK, 0, at, 200);
    static const ivec2_t inside[] = {{-8,-8}, {16,16}, {-8,16}, {16,-8}, {0,0}};
    static const ivec2_t outside[] = {{-9,0}, {17,0}, {0,-9}, {0,17}};
    mobj_t *in[5], *out[4];
    for (int i = 0; i < 5; ++i) in[i] = spawn(MT_UNIT_GRUNT, 1, ivec2_add(at, inside[i]), 30);
    for (int i = 0; i < 4; ++i) out[i] = spawn(MT_UNIT_GRUNT, 1, ivec2_add(at, outside[i]), 30);
    mobj_t *friend = spawn(MT_UNIT_GRUNT, 0, at, 30);
    mobj_t *loner = spawn(MT_UNIT_GRUNT, 1, ivec2_add(at, (ivec2_t){4, 4}), 30);
    if (!tank || !friend || !loner) return fail("spawn");
    loner->z.squad = 0;
    run_tic();
    for (int i = 0; i < 5; ++i)
        if (!in[i] || !lying(in[i]) || in[i]->hp != 20) return fail("the inclusive rectangle knocks down and costs 10");
    for (int i = 0; i < 4; ++i)
        if (!out[i] || Z_Knocked(out[i]) || out[i]->hp != 30) return fail("outside the rectangle nothing happens");
    if (Z_Knocked(friend) || friend->hp != 30) return fail("the vehicle's own team is not touched");
    if (Z_Knocked(loner) || loner->hp != 30) return fail("an ungrouped unit is not in the bins");
    /* A knocked robot is not hit again while it lies. */
    for (int i = 0; i < 10; ++i) run_tic();
    if (in[4]->hp != 20 || !lying(in[4])) return fail("a lying robot is skipped");
    /* An empty (neutral) vehicle does nothing. */
    discard(tank);
    mobj_t *empty = spawn(MT_UNIT_MED_TANK, Z_NEUTRAL, ivec2_add(at, (ivec2_t){0, 200}), 200);
    mobj_t *near = spawn(MT_UNIT_GRUNT, 1, ivec2_add(at, (ivec2_t){0, 200}), 30);
    if (!empty || !near) return fail("spawn empty vehicle");
    run_tic();
    if (Z_Knocked(near) || near->hp != 30) return fail("an empty vehicle knocks nobody down");
    rts_game_model_destroy(model);
    puts("PASS: z vehicle contact rectangle, team and bin filters");
    return 0;
}

static int test_run_over(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level");
    disarm();
    ivec2_t at = {1200, 1200};
    mobj_t *tank = spawn(MT_UNIT_LIGHT_TANK, 0, at, 100);
    mobj_t *ten = spawn(MT_UNIT_GRUNT, 1, at, 10);
    mobj_t *eleven = spawn(MT_UNIT_GRUNT, 1, ivec2_add(at, (ivec2_t){8, 0}), 11);
    mobj_t *zero = spawn(MT_UNIT_TOUGH, 1, ivec2_add(at, (ivec2_t){0, 8}), 30);
    if (!tank || !ten || !eleven || !zero) return fail("spawn");
    Z_SetNativeHp(zero, 0);
    if (!zero->z.zero_hp) return fail("zero fixture");
    run_tic();
    if (!dead(ten)) return fail("+0x3a of 10 is run over");
    if (!dead(zero)) return fail("native zero health is run over");
    if (dead(eleven) || !lying(eleven) || Z_NativeHp(eleven) != 1) return fail("+0x3a of 11 is knocked down to 1");
    rts_game_model_destroy(model);
    puts("PASS: z run over at +0x3a <= 10, knocked down above");
    return 0;
}

static int test_lying_picture(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level");
    disarm();
    for (int d = 0; d < 8; ++d) {
        ivec2_t at = {400 + d * 100, 1200};
        mobj_t *tank = spawn(MT_UNIT_MED_TANK, 0, at, 200);
        mobj_t *robot = spawn(MT_UNIT_GRUNT, 1, at, 30);
        if (!tank || !robot) return fail("spawn");
        robot->core.angle = direction_to_angle(d, 8, ANG90, true);
        run_tic();
        int want = (d >= 2 && d <= 5) ? 1 : 0; /* 0x2e when (facing + 2) & 4. */
        if (!lying(robot) || robot->core.state_frame != want) return fail("0x17 faces 6, 7, 0, 1; 0x2e the rest");
        if (angle_to_direction(robot->core.angle, 8, ANG90, true) != 0) return fail("facing resets to 0");
        int sprite = states[robot->core.state_id].frame + robot->core.state_frame;
        if (sprite != Z_ROBOT_KNOCK + want) return fail("lying picture");
        discard(tank);
        discard(robot);
    }
    rts_game_model_destroy(model);
    puts("PASS: z knockdown action and picture by facing");
    return 0;
}

static int test_hold_and_rise(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level");
    disarm();
    ivec2_t at = {1200, 1200};
    mobj_t *tank = spawn(MT_UNIT_MED_TANK, 0, at, 200);
    mobj_t *robot = spawn(MT_UNIT_GRUNT, 1, ivec2_add(at, (ivec2_t){16, 0}), 30);
    if (!tank || !robot) return fail("spawn");
    robot->core.angle = direction_to_angle(3, 8, ANG90, true);
    run_tic();
    if (!lying(robot) || robot->core.state_frame != 1) return fail("knocked down in 0x2e");
    /* Any vehicle within 23 pixels holds it, whatever its team. */
    discard(tank);
    mobj_t *friend = spawn(MT_UNIT_JEEP, 1, ivec2_add(at, (ivec2_t){16 + 23, -23}), 50);
    if (!friend) return fail("spawn friend");
    for (int i = 0; i < 20; ++i) run_tic();
    if (!lying(robot)) return fail("a vehicle strictly within 24 pixels holds it down");
    if (!place(friend, ivec2_add(at, (ivec2_t){16 + 24, 0}))) return fail("move friend");
    int ticks = 0;
    while (lying(robot) && ticks < 4) { run_tic(); ++ticks; }
    if (robot->core.state_id != rise_state(robot)) return fail("24 pixels away the robot gets up");
    int frame = robot->core.state_frame;
    if (frame < Z_ROBOT_RISE_STEPS) return fail("0x2f is the second rise run");
    int updates = 0, evens = 0, left = Z_ROBOT_RISE_STEPS - frame % Z_ROBOT_RISE_STEPS;
    int64_t from = (int64_t)leveltime * Z_NATIVE_HZ / RTS_TICRATE;
    while (Z_Knocked(robot) && updates < 200) {
        int steps = run_tic();
        for (int k = 1; k <= steps; ++k) evens += !((from + updates + k) & 1);
        updates += steps;
        if (Z_Knocked(robot) && robot->core.state_frame < frame) return fail("rise frames only advance");
        if (Z_Knocked(robot)) frame = robot->core.state_frame;
    }
    if (Z_Knocked(robot) || robot->core.state_id != S_GRUNT_STND) return fail("0x79049 stands it in action 1");
    /* From frame 0 the stand comes on the eleventh update with 0x10e91b
     * clear; the tic it lands in may hold one more even update. */
    if (evens < left || evens > left + 1) return fail("eleven frames at every second update");
    if (robot->hp != 20) return fail("getting up costs nothing more");
    rts_game_model_destroy(model);
    puts("PASS: z knockdown hold within 24 pixels and the eleven-frame get-up");
    return 0;
}

/* Open ground away from both forts. */
static const ivec2_t open_ground = {312, 312};

static int test_order_kept(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level");
    disarm();
    ivec2_t at = open_ground;
    mobj_t *robot = spawn(MT_UNIT_GRUNT, 1, at, 30);
    if (!robot) return fail("spawn robot");
    fixed2_t goal = fixed2_add(fixed3_xy(robot->core.position), (fixed2_t){5 * FIXED_ONE, 0});
    if (!P_MoveUnitTo(&level, robot, goal)) return fail("move order");
    RtsRenderSnapshot snap;
    if (!rts_tick(model, &snap) || !rts_tick(model, &snap)) return fail("tick");
    mobj_t *tank = spawn(MT_UNIT_HEAVY_TANK, 0, Z_AITopLeft(robot), 300);
    if (!tank) return fail("spawn tank");
    tank->traits &= ~MF_MOBILE;
    if (!rts_tick(model, &snap)) return fail("tick");
    if (!Z_Knocked(robot)) return fail("the tank knocks the walker down");
    fixed3_t down = robot->core.position;
    for (int i = 0; i < 30; ++i) if (!rts_tick(model, &snap)) return fail("tick");
    if (!lying(robot) || !fixed2_equal(fixed3_xy(robot->core.position), fixed3_xy(down))) return fail("a lying robot does not move");
    if (!P_HasMoveOrder(robot)) return fail("the order is kept through the knockdown");
    if (!place(tank, ivec2_add(at, (ivec2_t){0, 160}))) return fail("move tank away");
    for (int i = 0; i < 30 && Z_Knocked(robot); ++i) if (!rts_tick(model, &snap)) return fail("tick");
    if (Z_Knocked(robot)) return fail("gets up once the tank is gone");
    /* It lies facing north (+0x39 = 0) and turns back before walking. */
    for (int i = 0; i < 60 && fixed2_equal(fixed3_xy(robot->core.position), fixed3_xy(down)); ++i)
        if (!rts_tick(model, &snap)) return fail("tick");
    if (fixed2_equal(fixed3_xy(robot->core.position), fixed3_xy(down))) return fail("it walks on after getting up");
    /* A coordinate attack (+0x5a = -5) ends when the leader gets up. */
    ivec2_t from = ivec2_add(at, (ivec2_t){64, 0});
    mobj_t *leader = spawn(MT_UNIT_GRUNT, 1, from, 30);
    if (!leader) return fail("spawn leader");
    leader->z.grenades = 2;
    ivec2_t aim = ivec2_add(from, (ivec2_t){300, 0}); /* Out of reach: it approaches. */
    zmap_t *map = level.native_data;
    map->terrain[(aim.y / Z_TILE) * Z_MAP_STRIDE + aim.x / Z_TILE] = 1; /* 0x1171d1: attackable. */
    if (!Z_PointAttackOrder(leader, aim)) return fail("point order");
    for (int i = 0; i < 3; ++i) if (!rts_tick(model, &snap)) return fail("tick");
    if (leader->z.point_mode != 1 || !P_HasMoveOrder(leader)) return fail("the leader approaches the point");
    mobj_t *crusher = spawn(MT_UNIT_HEAVY_TANK, 0, Z_AITopLeft(leader), 300);
    if (!crusher) return fail("spawn crusher");
    crusher->traits &= ~MF_MOBILE;
    if (!rts_tick(model, &snap) || !Z_Knocked(leader)) return fail("leader knocked down");
    discard(crusher);
    for (int i = 0; i < 30 && Z_Knocked(leader); ++i) if (!rts_tick(model, &snap)) return fail("tick");
    if (Z_Knocked(leader) || leader->z.point_mode || P_HasMoveOrder(leader))
        return fail("the coordinate attack ends at 0x790c9");
    rts_game_model_destroy(model);
    puts("PASS: z a knocked-down robot keeps its move and drops a coordinate attack");
    return 0;
}

static int test_save(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level");
    disarm();
    ivec2_t at = {1200, 1200};
    mobj_t *tank = spawn(MT_UNIT_MED_TANK, 0, at, 200);
    mobj_t *robot = spawn(MT_UNIT_GRUNT, 1, at, 30);
    if (!tank || !robot) return fail("spawn");
    run_tic();
    if (!lying(robot)) return fail("knocked");
    uint32_t id = robot->id;
    int frame = robot->core.state_frame, state = robot->core.state_id;
    uint32_t hash = G_Consistency();
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
    bool ok = G_SaveGameBlob(&save, &bytes, "knock", &app, rts_game_model_ai(model), &hud) &&
              G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud);
    free(save);
    if (!ok) return fail("save and load");
    mobj_t *back = P_MobjById(id);
    if (!back || !Z_Knocked(back) || back->core.state_id != state || back->core.state_frame != frame || back->hp != 20) return fail("the knockdown is saved");
    if (G_Consistency() != hash) return fail("hash survives the save");
    if (G_SaveLayout() != 30) return fail("save layout");
    rts_game_model_destroy(model);
    puts("PASS: z knockdown survives save and load");
    return 0;
}

int main(void) {
    if (test_rectangle() || test_run_over() || test_lying_picture() || test_hold_and_rise() ||
        test_order_kept() || test_save()) return 1;
    return 0;
}
