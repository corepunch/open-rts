/* Robots knocked down and run over by vehicles (ZED.EXE). Every class-1
 * vehicle's update (actions 9/10 and the per-type stubs that call 0x57dd6)
 * runs the contact pass 0x58679 on the native updates where 0x10e91b is
 * clear. An opposing robot whose top-left lies in the inclusive rectangle
 * x-8..x+16, y-8..y+16 of the vehicle's top-left is knocked down (0x58910)
 * when its health byte +0x3a is above 10 (signed), losing 10, or killed
 * outright (0x60fe6, no 0x86ca4 damage call) otherwise.
 *
 * A knocked robot lies in action 0x17 or 0x2e while any vehicle's top-left
 * is strictly within 24 pixels on both axes (0x61f43), then gets up through
 * action 0x18 or 0x2f, a frame on each update with 0x10e91b clear, and at
 * frame 11 0x79049 stands it in action 1. Nothing of its order is written
 * on the way down, so the robot keeps its move: the engine holds it while
 * the knock states (group 3) last and walks on afterwards. The native
 * action and frame are the engine state and frame, so nothing is added to
 * the saved unit. See docs/Z_EXE_FINDINGS.md for what is not modelled. */
#include "z_local.h"
#include "info.h"

enum {
    KNOCK_COST = 10,   /* 0x58910: sub byte [+0x3a], 10. */
    KNOCK_LOW = 8, KNOCK_HIGH = 16, /* 0x586ac..0x586b8: x-8..x+16, y-8..y+16. */
    HOLD_REACH = 24,   /* 0x61f64 / 0x61f72: cmp 0x18, strictly less. */
    BIN_LIMIT = 32,    /* 0x64180: at most 32 units a bin. */
    VEHICLE_LIMIT = 1024,
};

/* 0x1047b8: the nine bins 0x7911a copies, around the vehicle's bin +0x38. */
static const int16_t near_bins[9] = {0, -1, 1, -17, -16, -15, 15, 16, 17};

static const int knock_states[] = {
#define Z_UNIT(t, type, ...) S_##type##_KNOCK,
#include "units.inc"
#undef Z_UNIT
};
static const int rise_states[] = {
#define Z_UNIT(t, type, ...) S_##type##_RISE,
#include "units.inc"
#undef Z_UNIT
};
static const int stand_states[] = {
#define Z_UNIT(t, type, ...) S_##type##_STND,
#include "units.inc"
#undef Z_UNIT
};

static bool live(const mobj_t *mo) { return mo && !mo->remove && mo->hp > 0; }

bool Z_Knocked(const mobj_t *mo) {
    int t = Z_UnitType(mo);
    return t >= 0 && Z_IsRobot(t) &&
           (mo->core.state_id == knock_states[t] || mo->core.state_id == rise_states[t]);
}

/* An active class-1 unit: a manned vehicle (0x575a3 lists +0x79 == 1 with
 * +0x31 bit 2; an emptied vehicle is a neutral object here). */
static bool vehicle(const mobj_t *mo) {
    return live(mo) && Z_IsClassOneUnit(Z_UnitType(mo)) && mo->team < Z_NEUTRAL;
}

static int bin_of(ivec2_t at) { return (at.x >> 7) + 16 * (at.y >> 7); }

/* 0x64138 before the unit pass: 256 bins of 128 pixels, live grouped units
 * in slot order, at most 32 a bin. 0x575a3: the vehicle list 0x18b81c. */
typedef struct {
    mobj_t *bins[256][BIN_LIMIT];
    uint8_t counts[256];
    ivec2_t vehicles[VEHICLE_LIMIT]; /* Native 240 unit slots; the port may hold more. */
    int vehicle_count;
} zknock_pass_t;

static void build_pass(zknock_pass_t *pass) {
    memset(pass->counts, 0, sizeof(pass->counts));
    pass->vehicle_count = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *mo = (mobj_t *)th;
        if (Z_UnitType(mo) < 0 || !live(mo)) continue;
        ivec2_t at = Z_AITopLeft(mo);
        if (vehicle(mo) && pass->vehicle_count < VEHICLE_LIMIT) pass->vehicles[pass->vehicle_count++] = at;
        if (!mo->z.squad || at.x < 0 || at.y < 0) continue;
        int bin = bin_of(at);
        if (bin < 256 && pass->counts[bin] < BIN_LIMIT) pass->bins[bin][pass->counts[bin]++] = mo;
    }
}

/* 0x58910: +0x3a less 10, the action saved to +0x72 (which nothing reads
 * back), action 0x17 or 0x2e by (facing + 2) & 4, frame and facing 0. */
static void knock_down(mobj_t *robot) {
    int t = Z_UnitType(robot);
    int facing = angle_to_direction(robot->core.angle, 8, ANG90, true);
    int lying = ((facing + 2) & 4) ? 1 : 0;
    Z_SetNativeHp(robot, Z_NativeHp(robot) - KNOCK_COST);
    robot->z.point_throw = false; /* Action 0x31 is replaced before the grenade leaves. */
    robot->core.angle = direction_to_angle(0, 8, ANG90, true);
    P_SetMobjStateFrame(robot, knock_states[t], lying);
}

/* 0x587e7..0x587f5: run over, killed through 0x60fe6 with 0x10f334 set. */
static void run_over(mobj_t *robot, mobj_t *by) {
    P_DamageMobj(robot, by, robot->hp);
}

/* 0x58679..0x58837 for one vehicle: the units 0x7911a copies from the
 * nine bins around it. */
static void contact(const zknock_pass_t *pass, mobj_t *source) {
    ivec2_t at = Z_AITopLeft(source);
    if (at.x < 0 || at.y < 0) return;
    int bin = bin_of(at);
    for (int i = 0; i < 9; ++i) {
        int s = bin + near_bins[i];
        if (s < 0 || s >= 256) continue;
        for (int j = 0; j < pass->counts[s]; ++j) {
            mobj_t *mo = pass->bins[s][j];
            /* 0x5875c..0x58781: not class 1 or 2, not in action 0x12, 0x17 or
             * 0x2e, not the vehicle's team. */
            if (!live(mo) || !Z_IsRobot(Z_UnitType(mo)) || Z_Knocked(mo) || mo->team == source->team) continue;
            ivec2_t pos = Z_AITopLeft(mo);
            if (pos.x < at.x - KNOCK_LOW || pos.x > at.x + KNOCK_HIGH ||
                pos.y < at.y - KNOCK_LOW || pos.y > at.y + KNOCK_HIGH) continue;
            if (Z_NativeHp(mo) > KNOCK_COST) knock_down(mo);
            else run_over(mo, source);
        }
    }
}

/* 0x61f43: any listed vehicle holds it; team is not tested. */
static bool held(const zknock_pass_t *pass, const mobj_t *robot) {
    ivec2_t at = Z_AITopLeft(robot);
    for (int i = 0; i < pass->vehicle_count; ++i) {
        ivec2_t v = pass->vehicles[i];
        if (abs(v.x - at.x) < HOLD_REACH && abs(v.y - at.y) < HOLD_REACH) return true;
    }
    return false;
}

static void knocked_update(const zknock_pass_t *pass, mobj_t *robot, bool even) {
    int t = Z_UnitType(robot);
    int frame = robot->core.state_frame;
    if (robot->core.state_id == knock_states[t]) {
        if (!held(pass, robot)) P_SetMobjStateFrame(robot, rise_states[t], (frame & 1) * Z_ROBOT_RISE_STEPS);
        return;
    }
    if (!even) return; /* 0x61f94: the frame waits while 0x10e91b is set. */
    int run = frame / Z_ROBOT_RISE_STEPS, step = frame % Z_ROBOT_RISE_STEPS + 1;
    if (step < Z_ROBOT_RISE_STEPS) {
        P_SetMobjStateFrame(robot, rise_states[t], run * Z_ROBOT_RISE_STEPS + step);
        return;
    }
    /* 0x79049: action 1. A move in progress carries on by itself; a
     * coordinate attack (+0x5a = -5) ends at 0x790c9. */
    P_SetMobjState(robot, stand_states[t]);
    Z_PointGetUp(robot);
}

/* Each native update of this tic, in thinker (slot) order: a knocked robot
 * runs its hold or get-up action, a vehicle its contact pass. */
void Z_KnockdownTicker(void) {
    if (!level.native_data || !thinkercap.next) return;
    static zknock_pass_t pass;
    int64_t first = (int64_t)leveltime * Z_NATIVE_HZ / RTS_TICRATE;
    int steps = Z_NativeSteps(leveltime, Z_NATIVE_HZ);
    for (int k = 1; k <= steps; ++k) {
        /* 0x19e72 toggles 0x10e91b each update; it is clear on even steps,
         * as A_ZGroundFire counts them. */
        bool even = !((first + k) & 1);
        build_pass(&pass);
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            mobj_t *mo = (mobj_t *)th;
            if (!live(mo)) continue;
            if (Z_Knocked(mo)) knocked_update(&pass, mo, even);
            else if (even && vehicle(mo)) contact(&pass, mo);
        }
    }
}
