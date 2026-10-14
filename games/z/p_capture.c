#include "z_local.h"
#include "info.h"

/* What robots do besides fight: take flags, get into empty vehicles and
 * guns, and pick up and throw grenades. ZED.EXE runs these from the terrain
 * a robot steps on (0x1157ea: 0x23 a flag, 0x24 a crate) and from orders;
 * here one pass a tic looks at where everyone stands. */

enum {
    REACH_FLAG_HOLD = 18,          /* 43 ticks of 70 Hz (0x9cb40) before a flag can turn again. */
    REACH_ENTER = 20,              /* Pixels from a vehicle at which a robot gets in (0x7af62). */
    REACH_CRATE = Z_TILE,          /* Pixels from a crate at which a robot picks it up (0x79b25). */
    MAX_GRENADES = 127,
    BOARD_RETRY = RTS_TICRATE / 2, /* Tics between walks towards a vehicle that has not been reached. */
};

static const int throw_states[] = {
#define Z_UNIT(t, type, ...) S_##type##_THROW,
#include "units.inc"
#undef Z_UNIT
};
static const int cheer_states[] = {
#define Z_UNIT(t, type, ...) S_##type##_CHEER,
#include "units.inc"
#undef Z_UNIT
};

static bool live(const mobj_t *mo) { return mo && !mo->remove && mo->hp > 0; }

static int robot_type(const mobj_t *mo) {
    int t = Z_UnitType(mo);
    return live(mo) && Z_IsRobot(t) && mo->team < Z_NEUTRAL ? t : -1;
}

static bool structure(const mobj_t *mo) {
    return mo && mo->type_id >= MT_FORT && mo->type_id < MT_UNITS;
}

static fixed_t pixels(int n) { return n * (FIXED_ONE / Z_TILE); }

static bool within(const mobj_t *a, const mobj_t *b, int reach) {
    return fixed2_distance_squared64(fixed3_xy(a->core.position), fixed3_xy(b->core.position)) <=
           fixed_sq64(pixels(reach));
}

/* ── vehicles and guns ────────────────────────────────────────────────── */

/* 0x61022 lets robots into every vehicle but the truck and into all four
 * guns; one that has a driver is a unit and takes nobody. */
static bool enterable(const mobj_t *mo) {
    int t = Z_UnitType(mo);
    return live(mo) && t >= 0 && !Z_IsRobot(t) && t != MT_UNIT_TRUCK - MT_UNITS &&
           mo->team == Z_NEUTRAL && !mo->z.driver;
}

static void walk_to(mobj_t *robot, const mobj_t *vehicle) {
    robot->attack.target = NULL;
    P_MoveUnitTo(&level, robot, fixed3_xy(vehicle->core.position));
}

/* 0x7b072: the unit stands off the empty object's top-left by its own
 * offset (0x14adb4 by object; the heavy tank 6 right and 2 down more,
 * 0x14aea4). */
static const struct { int state; ivec2_t at; } entry_offsets[] = {
    {S_GUN, {4, 0}}, {S_MEDIUM, {8, 6}}, {S_GATTLING, {4, 1}}, {S_HOWITZER, {1, 6}},
    {S_M_MISSILE, {6, 3}}, {S_HEAVY, {6, 4}}, {S_LIGHT, {11, 7}}, {S_A_P_C, {6, 6}},
    {S_JEEP, {4, 7}}, {S_CRANE, {6, 6}}, {S_FIRE_TRUCK, {3, 6}},
};

/* 0x7af62: the empty object goes and the robot drives in its place. The
 * vehicle keeps the robot's team and squad and stands where the object
 * did. It faces west, the way the crewless pictures do (0x7b21a); the
 * A.P.C. keeps facing north. */
static void get_in(mobj_t *robot, mobj_t *vehicle) {
    const zmap_t *map = level.native_data;
    int t = Z_UnitType(vehicle);
    isize2_t size;
    fixed2_t centre;
    if (!map || !Z_SpriteSize(map, states[vehicle->core.state_id].frame, &size)) return;
    ivec2_t top_left = {(int)((vehicle->core.position.x * Z_TILE) >> FIXED_FRAC_BITS) - size.w / 2,
                        (int)((vehicle->core.position.y * Z_TILE) >> FIXED_FRAC_BITS) - size.h / 2};
    for (size_t i = 0; i < sizeof(entry_offsets) / sizeof(*entry_offsets); ++i)
        if (entry_offsets[i].state == vehicle->core.state_id) top_left = ivec2_add(top_left, entry_offsets[i].at);
    if (!Z_UnitCentre(map, t, top_left, &centre)) return;
    vehicle->core.position = fixed3_from_fixed2(centre, vehicle->core.position.z);
    vehicle->core.angle = t == MT_UNIT_APC - MT_UNITS ? P_PointToAngle(0, -FIXED_ONE)
                                                      : P_PointToAngle(-FIXED_ONE, 0);
    P_SetMobjState(vehicle, mobjinfo[vehicle->type_id].spawnstate);
    vehicle->traits |= vehicle->info->traits & (MF_SELECTABLE | MF_MOBILE | MF_ATTACK | MF_TURRET);
    Z_SetTeam(vehicle, robot->team);
    vehicle->z.driver = (uint8_t)(Z_UnitType(robot) + 1);
    vehicle->z.squad = robot->z.squad;
    P_ClearMove(vehicle);
    vehicle->movement.goal = fixed3_xy(vehicle->core.position);
    vehicle->movement.order_arrived = true;
    P_MobjSetSelected(vehicle, P_MobjIsSelected(robot));
    P_MobjSetSelected(robot, false);
    P_RemoveMobj(robot);
}

static void tick_boarding(mobj_t *robot) {
    mobj_t *vehicle = P_MobjById(robot->z.board);
    if (!enterable(vehicle)) {
        robot->z.board = 0;
        return;
    }
    if (within(robot, vehicle, REACH_ENTER)) get_in(robot, vehicle);
    else if (!P_HasMoveOrder(robot) && leveltime % BOARD_RETRY == 0) walk_to(robot, vehicle);
}

/* ── grenades ─────────────────────────────────────────────────────────── */

/* Grunts, Pyros and Lasers carry grenades (0x79b25, unit classes 0, 0xf
 * and 0x10). */
static bool carries_grenades(int t) {
    return t == MT_UNIT_GRUNT - MT_UNITS || t == MT_UNIT_PYRO - MT_UNITS || t == MT_UNIT_LASER - MT_UNITS;
}

static bool squad_mate(const mobj_t *robot, const mobj_t *other) {
    return other == robot || (robot->z.squad && other->z.squad == robot->z.squad &&
                              other->team == robot->team && robot_type(other) >= 0);
}

/* A crate gives 10 or 20 grenades to a squad that has none, shared out
 * over its members with the remainder to the first (0x79bea). */
static bool pick_up(mobj_t *robot, mobj_t *crate) {
    int held = 0, members = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *other = (const mobj_t *)th;
        if (th->function != P_MobjThinker || !squad_mate(robot, other)) continue;
        held += other->z.grenades;
        members++;
    }
    if (held || !members) return false;
    int total = crate->type_id == MT_GRENADES ? 10 : 20, share = total / members;
    bool first = true;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *other = (mobj_t *)th;
        if (th->function != P_MobjThinker || !squad_mate(robot, other)) continue;
        int count = share + (first ? total % members : 0);
        other->z.grenades = (uint8_t)(count < MAX_GRENADES ? count : MAX_GRENADES);
        first = false;
    }
    P_RemoveMobj(crate);
    return true;
}

static bool crate(const mobj_t *mo) {
    return live(mo) && (mo->type_id == MT_GRENADES || mo->type_id == MT_GRENADES_2);
}

/* 0x75939: a robot with grenades throws them at vehicles and guns (a Laser
 * keeps its laser for those) and at buildings, which nothing else it
 * carries can hurt (0x753bb). */
static bool throws_at(const mobj_t *robot, const mobj_t *target) {
    int t = robot_type(robot), u = Z_UnitType(target);
    if (t < 0 || !robot->z.grenades || !live(target)) return false;
    if (structure(target)) return true;
    return u >= 0 && !Z_IsRobot(u) && t != MT_UNIT_LASER - MT_UNITS;
}

bool Z_CanTarget(const mobj_t *attacker, const mobj_t *victim) {
    return !structure(victim) || robot_type(attacker) < 0 || attacker->z.grenades;
}

/* A robot about to fire that will throw instead plays the throw. */
void Z_MobjTicker(mobj_t *mo) {
    int t = robot_type(mo);
    if (t < 0 || mo->core.state_id != (int)mobjinfo[mo->type_id].missilestate || mo->core.state_frame ||
        !throws_at(mo, mo->attack.target)) return;
    P_SetMobjState(mo, throw_states[t]);
}

/* The grenade leaves on the throw's last picture (0x636a0). */
bool Z_Attack(mobj_t *attacker, const weapondef_t *weapon, mobj_t *target) {
    (void)weapon;
    if (!throws_at(attacker, target) || !P_SpawnMissile(attacker, target, MT_GRENADE)) return false;
    attacker->z.grenades--;
    return true;
}

/* ── flags ────────────────────────────────────────────────────────────── */

/* 0x79ddc: a robot takes the flag under its centre; a vehicle also takes
 * one on the cells right of and below it (0x79936). Guns never move. */
static bool on_flag(const mobj_t *unit, ivec2_t flag) {
    int t = Z_UnitType(unit);
    if (!live(unit) || t < 0 || unit->team >= Z_NEUTRAL || !(unit->traits & MF_MOBILE)) return false;
    ivec2_t cell = fixed2_cell(fixed3_xy(unit->core.position));
    ivec2_t d = ivec2_sub(flag, cell);
    return Z_IsRobot(t) ? d.x == 0 && d.y == 0 : d.x >= 0 && d.x <= 1 && d.y >= 0 && d.y <= 1;
}

/* The territory and the buildings that stand in it change sides at once;
 * a robot that took it stops to cheer (0x62e3c). */
static void take_flag(mobj_t *flag, mobj_t *taker) {
    const zmap_t *map = level.native_data;
    const zterritory_t *territory = &map->territories[flag->z.territory - 1];
    Z_SetTeam(flag, taker->team);
    flag->z.hold = REACH_FLAG_HOLD;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || !live(mo) || !structure(mo) || mo->type_id == MT_FORT) continue;
        ivec2_t cell = fixed2_cell(fixed3_xy(mo->core.position));
        if (cell.x >= territory->min.x && cell.x < territory->max.x &&
            cell.y >= territory->min.y && cell.y < territory->max.y) Z_SetTeam(mo, taker->team);
    }
    int t = robot_type(taker);
    if (t < 0) return;
    P_ClearMove(taker);
    taker->movement.goal = fixed3_xy(taker->core.position);
    taker->movement.order_arrived = true;
    taker->core.angle = P_PointToAngle(0, FIXED_ONE);
    P_SetMobjState(taker, cheer_states[t]);
}

static void tick_flag(mobj_t *flag) {
    const zmap_t *map = level.native_data;
    if (!map || !flag->z.territory || flag->z.territory > map->territory_count) return;
    if (flag->z.hold > 0) {
        flag->z.hold--;
        return;
    }
    ivec2_t cell = map->territories[flag->z.territory - 1].flag;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || mo->team == flag->team || !on_flag(mo, cell)) continue;
        take_flag(flag, mo);
        return;
    }
}

/* ── each tic ─────────────────────────────────────────────────────────── */

void Z_Ticker(void) {
    if (!level.native_data || !thinkercap.next) return;
    /* Things taken out stay linked until the next tic (P_RemoveThinker). */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || mo->remove) continue;
        if (mo->type_id == MT_FLAG) {
            tick_flag(mo);
            continue;
        }
        int t = robot_type(mo);
        if (t < 0) continue;
        if (carries_grenades(t)) {
            for (thinker_t *c = thinkercap.next; c != &thinkercap; c = c->next) {
                mobj_t *box = (mobj_t *)c;
                if (c->function == P_MobjThinker && crate(box) && within(mo, box, REACH_CRATE) &&
                    pick_up(mo, box)) break;
            }
        }
        if (mo->z.board) tick_boarding(mo);
    }
}

/* Robots told onto an empty vehicle or gun walk to it and the first there
 * gets in (0x61022); the rest stop beside it. */
bool Z_BoardOrder(mobj_t *const *units, int count, mobj_t *target) {
    if (!enterable(target)) return false;
    bool any = false;
    for (int i = 0; i < count; ++i) {
        if (robot_type(units[i]) < 0) continue;
        units[i]->z.board = target->id;
        units[i]->waypoints = (waypoints_t){0};
        walk_to(units[i], target);
        any = true;
    }
    return any;
}
