#include "z_local.h"
#include "info.h"

/* What robots do besides fight: take flags, get into empty vehicles and
 * guns, and pick up and throw grenades. ZED.EXE runs these from the terrain
 * a robot steps on (0x1157ea: 0x23 a flag, 0x24 a crate) and from orders;
 * here one pass a tic looks at where everyone stands. */

enum {
    REACH_FLAG_HOLD = 18,          /* 43 ticks of 70 Hz (0x9cb40) before a flag can turn again. */
    REACH_ENTER = 20,              /* Pixels from a vehicle at which a robot gets in (0x7af62). */
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

/* Native 0x4a5d7 picks the first free effect slot. We can account only for
 * represented effects; other native effects can occupy these slots too. */
static int empty_slot(void) {
    bool used[200] = {false};
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *mo = (mobj_t *)th;
        if (!mo->remove && mo->z.object_slot && mo->z.object_slot <= 200)
            used[mo->z.object_slot - 1] = true;
    }
    for (int i = 0; i < 200; ++i) if (!used[i]) return i + 1;
    return 0;
}

/* 0x6178d..0x617ed and the Jeep's 0x14af94 row place its empty
 * object at (-4,-7) from the unit origin. 0x6147d checks four terrain
 * cells at the object's rounded position before allocating the effect. */
static bool empty_jeep_position(const mobj_t *mo, fixed2_t *position) {
    const zmap_t *map = level.native_data;
    isize2_t size;
    if (!map || !Z_SpriteSize(map, states[S_JEEP].frame, &size)) return false;
    ivec2_t origin = ivec2_sub(Z_BlastAnchor(mo), (ivec2_t){20, 23});
    ivec2_t tile = {(origin.x + 7) / Z_TILE, (origin.y + 7) / Z_TILE};
    if (origin.x + 7 < 0 || origin.y + 7 < 0 || tile.x + 1 >= level.width || tile.y + 1 >= level.height)
        return false;
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        int terrain = map->terrain[(tile.y + y) * Z_MAP_STRIDE + tile.x + x] & 0x7f;
        if (terrain != 0 && terrain != 7 && terrain != 8 && terrain != 10 &&
            terrain != 22 && terrain != 23 && terrain != 59) return false;
    }
    origin = ivec2_scale(tile, Z_TILE);
    *position = fixed2_scale(fixed2_from_cell(ivec2_add(origin, (ivec2_t){size.w / 2, size.h / 2})), FIXED_ONE / Z_TILE);
    return true;
}

/* Gun types bypass the grid-rounding check; 0x14af94 supplies these
 * offsets before the common four-pixel x subtraction at 0x617ed. */
static bool empty_gun_position(const mobj_t *mo, int object, fixed2_t *position) {
    ivec2_t offset = object == MT_GUN ? (ivec2_t){-4, 0} :
                      object == MT_MISSILE ? (ivec2_t){0, 0} :
                      object == MT_GATTLING ? (ivec2_t){-4, -1} : (ivec2_t){-1, -6};
    isize2_t size;
    if (!level.native_data || !Z_SpriteSize(level.native_data, states[mobjinfo[object].spawnstate].frame, &size)) return false;
    ivec2_t origin = ivec2_add(ivec2_sub(Z_BlastAnchor(mo), (ivec2_t){16, 16}), offset);
    *position = fixed2_scale(fixed2_from_cell(ivec2_add(origin, (ivec2_t){size.w / 2, size.h / 2})), FIXED_ONE / Z_TILE);
    return true;
}

static int robot_type(const mobj_t *mo) {
    int t = Z_UnitType(mo);
    return live(mo) && Z_IsRobot(t) && mo->team < Z_NEUTRAL ? t : -1;
}

static int base_accuracy(const mobj_t *mo) {
    int t = Z_UnitType(mo);
    if (t >= 0 && !Z_IsRobot(t) && mo->z.crew_count && mo->z.driver) t = mo->z.driver - 1;
    return Z_UnitAccuracy(t);
}

/* The speed byte is ten times the plain-ground level in units.inc for all
 * vehicle and gun records in the shipped ROBOTS.DAT. */
static const struct { uint8_t speed, fire; } body_stats[] = {
#define Z_UNIT(t, type, label, kind, mobile, hp, dmg, lvl, range, wait, ...) {(lvl) * 10, 100 - (wait)},
#include "units.inc"
#undef Z_UNIT
};

/* 0x1157ea's signed terrain modifiers, divided by ten as at 0x798c3.
 * Entries 100..112 dispatch terrain interactions and return zero speed term. */
static void terrain_speed(mobj_t *mo) {
    static const int8_t levels[67] = {
        [7] = 2, [8] = -2, [9] = -1, [17] = 2, [22] = -2,
        [23] = 2, [31] = 9, [39] = -2, [42] = -3, [59] = -2,
    };
    const zmap_t *map = level.native_data;
    int t = Z_UnitType(mo);
    if (!map || t < 0 || !(mo->traits & MF_MOBILE)) return;
    ivec2_t pixel = Z_BlastAnchor(mo);
    if (!Z_IsRobot(t)) pixel = ivec2_sub(pixel, (ivec2_t){8, 8});
    ivec2_t cell = {pixel.x / Z_TILE, pixel.y / Z_TILE};
    if (pixel.x < 0 || pixel.y < 0 || !L_Contains(&level, cell.x, cell.y)) return;
    unsigned terrain = map->terrain[cell.y * Z_MAP_STRIDE + cell.x] & 0x7f;
    int base = mo->z.body_tracked ? mo->z.body_speed / 10 : body_stats[t].speed / 10;
    mo->speed = Z_LevelSpeed(base + (terrain < sizeof(levels) ? levels[terrain] : 0) +
                             (mo->z.detour.phase == Z_DETOUR_TRAVEL ? 3 : 0));
}

/* 0x86d55 scales the body's mutable speed byte only when damage crosses a
 * health threshold; a single hit crossing several uses the lowest one. */
void Z_BodyDamaged(mobj_t *mo, int old_hp) {
    int t = Z_UnitType(mo);
    if (t < 0 || Z_IsRobot(t) || mo->max_hp <= 0) return;
    if (!mo->z.body_tracked) {
        mo->z.body_speed = body_stats[t].speed;
        mo->z.body_fire = body_stats[t].fire;
        mo->z.body_accuracy = (uint8_t)base_accuracy(mo);
        mo->z.body_tracked = true;
    }
    int half = mo->max_hp / 2, quarter = half / 2;
    int threshold = 0, scale = 100;
    if (mo->hp <= quarter) { threshold = quarter; scale = 80; }
    else if (mo->hp <= half) { threshold = half; scale = 85; }
    else if (mo->hp <= half + quarter) { threshold = half + quarter; scale = 90; }
    if (threshold && old_hp > threshold && mo->hp > 0) {
        mo->z.body_speed = mo->z.body_speed * scale / 100;
        mo->z.body_accuracy = mo->z.body_accuracy * scale / 100;
        /* 0x86ded exempts native unit types 0x22 (Jeep) and 0x16
         * (Gatling), assigned at 0x639a3 and 0x50b91 respectively. */
        if (t != MT_UNIT_JEEP - MT_UNITS && t != MT_UNIT_GATLING - MT_UNITS)
            mo->z.body_fire = mo->z.body_fire * scale / 100;
        if (mo->info && mo->info->speed > 0) mo->speed = Z_LevelSpeed(mo->z.body_speed / 10);
    }
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
bool Z_Enterable(const mobj_t *mo) {
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

static void store_crew(mobj_t *vehicle, mobj_t *robot) {
    if (vehicle->z.crew_count >= Z_CREW_MAX) return;
    vehicle->z.crew[vehicle->z.crew_count++] = (zcrew_t){
        .hp = robot->hp, .robot = robot->z.robot, .type = (uint8_t)Z_UnitType(robot),
        .grenades = robot->z.grenades, .home = robot->z.home, .flame_phase = robot->z.flame_phase,
        .manufactured = robot->z.manufactured,
    };
    Z_SquadLeave(robot);
    Z_DetourCancel(robot);
    P_MobjSetSelected(robot, false);
    P_RemoveMobj(robot);
}

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
    vehicle->z.object_slot = 0;
    vehicle->core.angle = t == MT_UNIT_APC - MT_UNITS ? P_PointToAngle(0, -FIXED_ONE)
                                                      : P_PointToAngle(-FIXED_ONE, 0);
    P_SetMobjState(vehicle, mobjinfo[vehicle->type_id].spawnstate);
    vehicle->traits |= vehicle->info->traits & (MF_SELECTABLE | MF_MOBILE | MF_ATTACK | MF_TURRET);
    vehicle->z.detour.slot = 0;
    vehicle->z.detour.claimant = 0;
    Z_SetTeam(vehicle, robot->team);
    vehicle->z.driver = (uint8_t)(Z_UnitType(robot) + 1);
    if (vehicle->z.body_tracked) vehicle->z.body_accuracy = (uint8_t)base_accuracy(robot);
    vehicle->z.squad = robot->z.squad;
    vehicle->z.home = robot->z.home;
    vehicle->z.robot = robot->z.robot;
    vehicle->z.manufactured = robot->z.manufactured;
    P_ClearMove(vehicle);
    vehicle->movement.goal = fixed3_xy(vehicle->core.position);
    vehicle->movement.order_arrived = true;
    P_MobjSetSelected(vehicle, P_MobjIsSelected(robot));
    store_crew(vehicle, robot);
    if (t == MT_UNIT_APC - MT_UNITS && vehicle->z.squad)
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *mate = (mobj_t *)th;
            if (th->function == P_MobjThinker && mate != robot && robot_type(mate) >= 0 &&
                mate->team == vehicle->team && mate->z.squad == vehicle->z.squad)
                store_crew(vehicle, mate);
        }
}

/* 0x7afbf..0x7aff4 compares native unit and object origins to 20px. */
bool Z_DetourBoard(mobj_t *unit, mobj_t *target) {
    const zmap_t *map = level.native_data;
    isize2_t size;
    if (!map || robot_type(unit) < 0 || !Z_Enterable(target) ||
        !Z_SpriteSize(map, states[target->core.state_id].frame, &size)) return false;
    ivec2_t centre = fixed2_cell(fixed2_scale(fixed3_xy(target->core.position), FIXED_FROM_INT(Z_TILE)));
    ivec2_t at = ivec2_sub(centre, (ivec2_t){size.w / 2, size.h / 2});
    ivec2_t delta = ivec2_sub(Z_AITopLeft(unit), at);
    if ((int64_t)delta.x * delta.x + (int64_t)delta.y * delta.y > REACH_ENTER * REACH_ENTER) return false;
    get_in(unit, target);
    return true;
}

/* Zod's eject command restores each crew member's health and leaves the
 * vehicle neutral. A gun on a fort wall cannot be abandoned. */
bool Z_EjectCrew(mobj_t *vehicle) {
    int t = Z_UnitType(vehicle);
    int empty = t == MT_UNIT_APC - MT_UNITS ? S_A_P_C :
                t == MT_UNIT_LIGHT_GUN - MT_UNITS ? S_GUN :
                t == MT_UNIT_GATLING - MT_UNITS ? S_GATTLING :
                t == MT_UNIT_HOWITZER - MT_UNITS ? S_HOWITZER :
                t == MT_UNIT_MISSILE_GUN - MT_UNITS ? S_MISSILE : -1;
    if (!live(vehicle) || vehicle->team >= Z_NEUTRAL || !vehicle->z.driver ||
        (t != MT_UNIT_APC - MT_UNITS && vehicle->core.position.z) || vehicle->z.service || empty < 0) return false;
    if (vehicle->z.crew_count > Z_CREW_MAX || !Z_IsRobot(vehicle->z.driver - 1)) return false;
    for (int i = 0; i < vehicle->z.crew_count; ++i)
        if (!Z_IsRobot(vehicle->z.crew[i].type) || vehicle->z.crew[i].hp <= 0) return false;
    int count = vehicle->z.crew_count ? vehicle->z.crew_count : 1;
    int health = vehicle->z.crew_count ? vehicle->z.crew[0].hp :
                 mobjinfo[MT_UNITS + vehicle->z.driver - 1].spawnhealth;
    if (vehicle->z.crew_hurt >= health) return false;
    int team = vehicle->team;
    mobj_t *spawned[Z_CREW_MAX];
    for (int i = 0; i < count; ++i) {
        zcrew_t crew = vehicle->z.crew_count ? vehicle->z.crew[i] : (zcrew_t){
            .type = (uint8_t)(vehicle->z.driver - 1), .robot = vehicle->z.robot,
            .hp = mobjinfo[MT_UNITS + vehicle->z.driver - 1].spawnhealth,
            .home = vehicle->z.home, .flame_phase = vehicle->z.flame_phase, .manufactured = vehicle->z.manufactured,
        };
        mobj_t *robot = P_SpawnMobj(vehicle->core.position, (uint16_t)(MT_UNITS + crew.type));
        if (!robot) {
            while (i) P_RemoveMobj(spawned[--i]);
            return false;
        }
        spawned[i] = robot;
        robot->hp = crew.hp - (i == 0 ? vehicle->z.crew_hurt : 0);
        robot->z.robot = crew.robot;
        robot->z.grenades = crew.grenades;
        robot->z.flame_phase = crew.flame_phase;
        robot->z.home = crew.home;
        robot->z.manufactured = crew.manufactured;
        Z_SetTeam(robot, team);
        robot->z.squad = vehicle->z.squad;
    }
    Z_SquadEjected(spawned, count);
    vehicle->z.crew_count = vehicle->z.driver = vehicle->z.squad = vehicle->z.crew_hurt = 0;
    vehicle->traits &= ~(MF_MOBILE | MF_ATTACK);
    vehicle->attack.target = NULL;
    P_ClearMove(vehicle);
    P_MobjSetSelected(vehicle, false);
    Z_SetTeam(vehicle, Z_NEUTRAL);
    P_SetMobjState(vehicle, empty);
    P_MobjSetSelected(spawned[0], team == consoleplayer);
    return true;
}

static void tick_boarding(mobj_t *robot) {
    mobj_t *vehicle = P_MobjById(robot->z.board);
    if (!Z_Enterable(vehicle)) {
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

/* 0x79bea divides among occupied group slots, giving the remainder to
 * slot zero (the leader), regardless of thinker allocation order. */
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
    const mobj_t *leader = Z_SquadLeader(robot);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *other = (mobj_t *)th;
        if (th->function != P_MobjThinker || !squad_mate(robot, other)) continue;
        int count = share + (other == leader ? total % members : 0);
        other->z.grenades = (uint8_t)(count < MAX_GRENADES ? count : MAX_GRENADES);
    }
    P_RemoveMobj(crate);
    return true;
}

static bool crate(const mobj_t *mo) {
    return live(mo) && (mo->type_id == MT_GRENADES || mo->type_id == MT_GRENADES_2);
}

/* 0x79b25 compares native top-left coordinates, strictly within 16 pixels
 * on each axis. The object mobj is stored at its sprite centre. */
static bool reaches_crate(const mobj_t *robot, const mobj_t *box) {
    const zmap_t *map = level.native_data;
    isize2_t size;
    if (!map || !Z_SpriteSize(map, states[mobjinfo[box->type_id].spawnstate].frame, &size)) return false;
    ivec2_t centre = fixed2_cell(fixed2_scale(fixed3_xy(box->core.position), FIXED_FROM_INT(Z_TILE)));
    ivec2_t origin = ivec2_sub(centre, (ivec2_t){size.w / 2, size.h / 2});
    ivec2_t delta = ivec2_sub(Z_AITopLeft(robot), origin);
    return delta.x > -Z_TILE && delta.x < Z_TILE && delta.y > -Z_TILE && delta.y < Z_TILE;
}

/* 0x75939: a robot with grenades throws them at vehicles and guns (a Laser
 * keeps its laser for those) and at buildings, which nothing else it
 * carries can hurt (0x753bb). */
static bool throws_at(const mobj_t *robot, const mobj_t *target) {
    int t = robot_type(robot), u = Z_UnitType(target);
    if (t < 0 || !robot->z.grenades || !live(target)) return false;
    if (structure(target)) return true;
    if (target->type_id == MT_CLIFF || target->type_id == MT_BRIDGE) return true;
    return u >= 0 && !Z_IsRobot(u) && t != MT_UNIT_LASER - MT_UNITS;
}

void A_PyroFlame(mobj_t *unit) {
    mobj_t *target = P_AttackTarget(unit);
    if (throws_at(unit, target)) {
        unit->attack.target = target;
        P_SetMobjState(unit, S_PYRO_THROW);
        return;
    }
    P_Attack(unit);
}

void A_PyroRecover(mobj_t *unit) {
    unit->attack.shots = (unit->attack.shots + 1) & 3;
    if (!unit->attack.shots)
        unit->core.tics = ((16 + 1) * RTS_TICRATE + Z_NATIVE_HZ - 1) / Z_NATIVE_HZ;
}

void A_ZToss(mobj_t *unit) {
    if (!throws_at(unit, unit->attack.target)) return;
    int grenades = unit->z.grenades;
    if (!P_Attack(unit) || unit->z.grenades == grenades) return;
    int steps = (unit->info->attack.cooldown_ms * Z_NATIVE_HZ + 999) / 1000;
    unit->attack.cooldown_left_ms = 2 * steps * 1000 / Z_NATIVE_HZ;
}

/* Forts are taken from inside, never shot (p_fort.c). */
bool Z_CanTarget(const mobj_t *attacker, const mobj_t *victim) {
    if (victim->type_id == MT_CLIFF || victim->type_id == MT_BRIDGE)
        return attacker->attack.target == victim && Z_CliffWeapon(attacker);
    if (victim->type_id == MT_FORT) return false;
    return !structure(victim) || robot_type(attacker) < 0 || attacker->z.grenades;
}

/* A robot about to fire that will throw instead plays the throw. */
void Z_MobjTicker(mobj_t *mo) {
    if (mo->type_id == MT_BUILDING_DEBRIS || mo->type_id == MT_BUILDING_REBUILD || mo->type_id == MT_WATER) return;
    Z_RouteTick(mo);
    Z_SquadTick(mo);
    terrain_speed(mo);
    if (mo->core.state_id == S_BRIDGE_REBUILD) {
        Z_BridgeEffectTick(mo);
        return;
    }
    if (mo->z.service) Z_ServiceTick(mo);
    int t = robot_type(mo);
    if (t < 0 || mo->core.state_id != (int)mobjinfo[mo->type_id].missilestate || mo->core.state_frame ||
        !throws_at(mo, mo->attack.target)) return;
    P_SetMobjState(mo, throw_states[t]);
}

/* The grenade leaves on the throw's last picture (0x636a0). */
/* 0x5ae44: a bullet loses 5 (0x103f49) against a vehicle or gun; one left
 * with nothing does 0 or 1 at random. Shells, rockets and grenades burst
 * at full strength (0x85ce4). Bullets come from the riflemen, the Laser,
 * the Jeep, the APC and the Gatling. */
static bool fires_bullets(int t) {
    switch (t) {
    case MT_UNIT_GRUNT - MT_UNITS: case MT_UNIT_SNIPER - MT_UNITS: case MT_UNIT_PSYCHO - MT_UNITS:
    case MT_UNIT_LASER - MT_UNITS: case MT_UNIT_JEEP - MT_UNITS: case MT_UNIT_APC - MT_UNITS:
    case MT_UNIT_GATLING - MT_UNITS: return true;
    default: return false;
    }
}
/* Deterministic stand-in for the native random stream, the same on every
 * peer: 0..127 from the tic and the two units. */
static int coin(const mobj_t *a, const mobj_t *b, uint32_t salt) {
    uint32_t h = (uint32_t)leveltime * 2654435761u ^ a->id * 2246822519u ^ b->id * 3266489917u ^ salt;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return (int)(h & 0x7f);
}
/* Native unit +2 begins with the driver's authored accuracy (0x5108c)
 * and scales on body damage at 0x86df0. */
static int accuracy(const mobj_t *mo) {
    return mo->z.body_tracked ? mo->z.body_accuracy : base_accuracy(mo);
}
static bool valid_crew(const mobj_t *vehicle) {
    return vehicle->z.crew_count <= Z_CREW_MAX && vehicle->z.driver && Z_IsRobot(vehicle->z.driver - 1);
}
/* 0x5ad72: a bullet that hits a Jeep or a class-2 gun may hit its driver
 * instead, when `rand & 0x7f` falls under the shooter's accuracy (halved
 * against a gun). The driver takes the whole shot; once he is dead the
 * vehicle stands empty for anyone to take (0x860fd). */
static bool hit_driver(const mobj_t *attacker, mobj_t *target, int damage, unsigned shot) {
    int t = Z_UnitType(target);
    bool gun = target->info && (target->info->traits & MF_TURRET);
    if (!valid_crew(target) || (t != MT_UNIT_JEEP - MT_UNITS && !gun)) return false;
    int skill = accuracy(attacker);
    if (gun) skill /= 2;
    if (coin(attacker, target, 0x5ad72 + shot) >= skill) return false;
    int driver_hp = target->z.crew_count ? target->z.crew[0].hp : mobjinfo[MT_UNITS + target->z.driver - 1].spawnhealth;
    int hurt = target->z.crew_hurt + damage;
    target->z.crew_hurt = (uint8_t)(hurt > 255 ? 255 : hurt);
    if (hurt <= driver_hp) return true;
    /* 0x61bb9..0x61be3 stores body health in 25-HP units on driver death. */
    target->hp = (target->hp / 25 ? target->hp / 25 : 1) * 25;
    target->max_hp = target->max_hp / 25 * 25;
    Z_SetTeam(target, Z_NEUTRAL);
    target->z.crew_count = target->z.driver = target->z.squad = target->z.crew_hurt = 0;
    target->traits &= ~(MF_MOBILE | MF_ATTACK);
    target->attack.target = NULL;
    P_ClearMove(target);
    P_MobjSetSelected(target, false);
    if (gun || t == MT_UNIT_JEEP - MT_UNITS) {
        int object = t == MT_UNIT_LIGHT_GUN - MT_UNITS ? MT_GUN :
                     t == MT_UNIT_GATLING - MT_UNITS ? MT_GATTLING :
                     t == MT_UNIT_HOWITZER - MT_UNITS ? MT_HOWITZER :
                     t == MT_UNIT_MISSILE_GUN - MT_UNITS ? MT_MISSILE : MT_JEEP;
        fixed2_t position;
        bool fits = gun ? empty_gun_position(target, object, &position) : empty_jeep_position(target, &position);
        int slot = empty_slot();
        if (!fits || !slot) P_RemoveMobj(target);
        else {
            target->core.position = fixed3_from_fixed2(position, target->core.position.z);
            target->z.object = object;
            target->z.object_slot = slot;
            target->z.object_hp = target->hp / 25;
            P_SetMobjState(target, mobjinfo[object].spawnstate);
        }
    }
    return true;
}
static int hit_damage(const mobj_t *attacker, mobj_t *target, int damage, int divisor, unsigned shot) {
    int firing = Z_UnitType(attacker);
    if (firing == MT_UNIT_APC - MT_UNITS) {
        if (!valid_crew(attacker)) return 0;
        firing = attacker->z.driver - 1;
        const mobjtype_t *driver = P_ActorType((uint16_t)(MT_UNITS + firing));
        if (driver) {
            damage = driver->attack.damage;
            if (!fires_bullets(firing)) damage *= attacker->z.crew_count ? attacker->z.crew_count : 1;
        }
    }
    damage /= divisor ? divisor : 1;
    int victim = Z_UnitType(target);
    if (!attacker || victim < 0 || Z_IsRobot(victim) || !fires_bullets(firing)) return damage;
    if (hit_driver(attacker, target, damage, shot)) return 0;
    damage -= 5;
    /* Deterministic stand-in for the native coin: same on every peer. */
    return damage > 0 ? damage : (int)((leveltime + attacker->id + target->id + shot) & 1);
}

int Z_HitDamage(const mobj_t *attacker, const weapondef_t *weapon, mobj_t *target, int damage, int divisor) {
    (void)weapon;
    return hit_damage(attacker, target, damage, divisor, 0);
}

/* Zod's missile-bearing weapons, whose retail projectiles enter the
 * explosion path. Grenades retain their existing separate projectile. */
static int shell_speed(int t) {
    switch (t) {
    /* ROBOTS.DAT weapon +0x23, pixels per 70 Hz step. */
    case MT_UNIT_TOUGH - MT_UNITS: case MT_UNIT_LIGHT_GUN - MT_UNITS:
    case MT_UNIT_MED_TANK - MT_UNITS: return 10;
    case MT_UNIT_LIGHT_TANK - MT_UNITS: return 12;
    case MT_UNIT_HEAVY_TANK - MT_UNITS: return 8;
    case MT_UNIT_M_MISSILE - MT_UNITS: return 4;
    case MT_UNIT_MISSILE_GUN - MT_UNITS: case MT_UNIT_HOWITZER - MT_UNITS: return 5;
    default: return 0;
    }
}

static void fire_shell(mobj_t *attacker, mobj_t *target, int speed, int damage, bool scatter, bool water) {
    mobj_t *shell = water ? P_SpawnMobj(attacker->core.position, MT_WATER) : P_SpawnMissile(attacker, target, MT_SHELL);
    if (!shell) return;
    ivec2_t aim = Z_BlastAnchor(target);
    if (water && Z_UnitType(target) >= 0)
        aim = ivec2_add(Z_AITopLeft(target), (ivec2_t){12, 8}); /* 0x76849/0x76aa3. */
    if (scatter)
        aim = ivec2_add(aim, (ivec2_t){(coin(attacker, shell, 0x1ebc1) & 15) - 8,
                                      (coin(attacker, shell, 0x1ebd2) & 15) - 8});
    fixed2_t impact = fixed2_scale(fixed2_from_cell(aim), FIXED_ONE / Z_TILE);
    if (water) {
        ivec2_t start = fixed2_cell(fixed2_scale(fixed3_xy(shell->core.position), FIXED_FROM_INT(Z_TILE)));
        ivec2_t delta = ivec2_sub(aim, start);
        int dx = abs(delta.x), dy = abs(delta.y);
        shell->z.water.direction = (ivec2_t){(delta.x > 0) - (delta.x < 0), (delta.y > 0) - (delta.y < 0)};
        shell->z.water.vertical = dy > dx;
        shell->z.water.remaining = shell->z.water.major = dy > dx ? dy : dx;
        shell->z.water.minor = dy > dx ? dx : dy;
        shell->z.water.error = shell->z.water.major / 2;
        const zmap_t *map = level.native_data;
        ivec2_t origin = Z_AITopLeft(attacker);
        shell->z.water.height = map && origin.x >= 0 && origin.y >= 0 ?
            Z_TerrainHeight(map, origin.x / Z_TILE, origin.y / Z_TILE) + 1 : 0;
        shell->core.position = fixed3_from_fixed2(fixed2_scale(fixed2_from_cell(start), FIXED_ONE / Z_TILE), shell->core.position.z);
        shell->target = attacker;
        shell->team = attacker->team; shell->owner = attacker->owner; shell->allegiance = attacker->allegiance;
        shell->movement.goal = impact;
        shell->attack.target = target;
        shell->missile.damage = damage;
        return;
    }
    fixed2_t delta = fixed2_sub(impact, fixed3_xy(shell->core.position));
    int64_t dx = delta.x < 0 ? -(int64_t)delta.x : delta.x;
    int64_t dy = delta.y < 0 ? -(int64_t)delta.y : delta.y;
    int64_t major = dx > dy ? dx : dy;
    int64_t step = (int64_t)speed * FIXED_ONE * Z_NATIVE_HZ / (Z_TILE * RTS_TICRATE);
    int duration = (int)((major + step - 1) / step);
    if (duration < 1) duration = 1;
    shell->movement.goal = impact;
    shell->attack.target = target;
    shell->missile.damage = damage;
    shell->missile.duration = duration;
    shell->core.momentum = fixed3_from_fixed2((fixed2_t){delta.x / duration, delta.y / duration}, 0);
}

bool Z_Attack(mobj_t *attacker, const weapondef_t *weapon, mobj_t *target) {
    if (throws_at(attacker, target) && P_SpawnMissile(attacker, target, MT_GRENADE)) {
        attacker->z.grenades--;
        return true;
    }
    int t = Z_UnitType(attacker);
    bool apc = t == MT_UNIT_APC - MT_UNITS;
    if (apc) {
        if (!valid_crew(attacker)) return true;
        t = attacker->z.driver - 1;
    }
    if (apc && target && weapon) {
        int count = attacker->z.crew_count ? attacker->z.crew_count : 1;
        int team = target->team;
        const mobjtype_t *driver = P_ActorType(MT_UNIT_PYRO);
        for (int i = 0; i < count; ++i) {
            uint8_t *phase = attacker->z.crew_count ? &attacker->z.crew[i].flame_phase : &attacker->z.flame_phase;
            *phase = (*phase + 1) & 15;
            if (t == MT_UNIT_PYRO - MT_UNITS && *phase < 4 && live(target) && target->team == team && driver)
                P_DamageMobj(target, attacker, driver->attack.damage);
        }
        if (t == MT_UNIT_PYRO - MT_UNITS) return true;
    }
    if (apc && fires_bullets(t) && target && weapon) {
        int count = attacker->z.crew_count ? attacker->z.crew_count : 1;
        int team = target->team;
        for (int i = 0; i < count && live(target) && target->team == team; ++i) {
            int damage = hit_damage(attacker, target, weapon->damage, 1, (unsigned)i);
            if (damage > 0) P_DamageMobj(target, attacker, damage);
            else P_ProvokeMobj(target, attacker);
        }
        return true;
    }
    int speed = shell_speed(t);
    bool water = !apc && t == MT_UNIT_FIRE_TRUCK - MT_UNITS;
    if (water) speed = 8; /* ROBOTS.DAT weapon 14 +0x23. */
    if (!speed || !weapon || !target) return false;
    int damage = weapon->damage, count = 1;
    if (apc) {
        const mobjtype_t *driver = P_ActorType((uint16_t)(MT_UNITS + t));
        if (driver) damage = driver->attack.damage;
        count = attacker->z.crew_count ? attacker->z.crew_count : 1;
    }
    for (int i = 0; i < count; ++i) fire_shell(attacker, target, speed, water ? damage / 2 : damage, apc, water);
    /* Suppress the shared hitscan fallback if allocation fails. */
    return true;
}

/* Shells use the native candidate box at the snapshotted impact point. */
static void blast_building(ivec2_t point, mobj_t *source, int damage) {
    mobj_t *building_hit = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *building = (mobj_t *)th;
        if (th->function != P_MobjThinker || !live(building) || building->type_id <= MT_FORT ||
            building->type_id >= MT_UNITS || !building->z.building || !building->info) continue;
        ivec2_t origin = Z_BuildingOrigin(building);
        ivec2_t d = ivec2_sub(point, origin);
        if (d.x < 0 || d.y < 0 || d.x > building->info->footprint.w * Z_TILE ||
            d.y > building->info->footprint.h * Z_TILE) continue;
        if (!building_hit || building->z.building < building_hit->z.building) building_hit = building;
    }
    if (building_hit) P_DamageMobj(building_hit, source, damage);
}

void A_ZBoxBlast(mobj_t *effect) {
    ivec2_t origin = fixed2_cell(fixed2_scale(fixed3_xy(effect->core.position), FIXED_FROM_INT(Z_TILE)));
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *victim = (mobj_t *)th;
        if (th->function != P_MobjThinker || !live(victim) || victim->team >= Z_NEUTRAL || Z_UnitType(victim) < 0) continue;
        ivec2_t d = ivec2_sub(Z_BlastAnchor(victim), origin);
        if (d.x >= -22 && d.x <= 22 && d.y >= -6 && d.y <= 26) P_DamageMobj(victim, NULL, 127);
    }
}

static void blast_object(ivec2_t impact, int damage) {
    static const ivec2_t probes[] = {{-8,-8}, {8,-8}, {-8,8}, {8,8}};
    const zmap_t *map = level.native_data;
    if (!map || damage <= 0) return;
    for (int i = 0; i < 4; ++i) {
        ivec2_t point = ivec2_add(impact, probes[i]);
        mobj_t *found = NULL;
        ivec2_t origin = {0};
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            mobj_t *box = (mobj_t *)th;
            bool empty = live(box) && box->z.object && box->z.object_slot && box->team == Z_NEUTRAL &&
                         !box->z.driver && Z_UnitType(box) >= 0;
            if (!crate(box) && !empty) continue;
            isize2_t size;
            if (!Z_SpriteSize(map, states[box->core.state_id].frame, &size)) continue;
            ivec2_t centre = fixed2_cell(fixed2_scale(fixed3_xy(box->core.position), FIXED_FROM_INT(Z_TILE)));
            ivec2_t at = ivec2_sub(centre, (ivec2_t){size.w / 2, size.h / 2});
            ivec2_t d = ivec2_sub(point, at);
            int extent = empty ? 24 : 16;
            if (d.x < 0 || d.y < 0 || d.x > extent || d.y > extent) continue;
            if (!found || (box->z.object_slot && (!found->z.object_slot || box->z.object_slot < found->z.object_slot))) {
                found = box; origin = at;
            }
        }
        if (!found) continue;
        if (damage < found->z.object_hp) {
            found->z.object_hp -= damage;
            if (!crate(found)) found->hp = found->z.object_hp * 25;
            return;
        }
        if (!crate(found)) { P_RemoveMobj(found); return; }
        P_RemoveMobj(found);
        blast_building(origin, NULL, 127);
        zmap_t *mutable_map = level.native_data;
        ivec2_t cell = {origin.x / Z_TILE, origin.y / Z_TILE};
        if (origin.x >= 0 && origin.y >= 0 && L_Contains(&level, cell.x, cell.y)) {
            int index = cell.y * Z_MAP_STRIDE + cell.x;
            mutable_map->ground_attr[index] = (mutable_map->ground_attr[index] & 0x80) | 0x16;
            Z_RebuildCliffs(&level);
        }
        P_SpawnMobj(fixed3_from_fixed2(fixed2_scale(fixed2_from_cell(origin), FIXED_ONE / Z_TILE), 0), MT_BOX_BLAST);
        return;
    }
}

/* 0x5a4c8 and 0x59b63: advance each native update with an accelerating
 * Bresenham major axis, then stop on terrain above the launch threshold. */
void A_ZWater(mobj_t *water) {
    const zmap_t *map = level.native_data;
    for (int steps = Z_NativeSteps(leveltime, Z_NATIVE_HZ); steps-- > 0;) {
        ivec2_t at = fixed2_cell(fixed2_scale(fixed3_xy(water->core.position), FIXED_FROM_INT(Z_TILE)));
        int speed = water->missile.age < 7 ? water->missile.age + 1 : 8;
        for (int i = 0; i < speed && water->z.water.remaining > 0; ++i) {
            water->z.water.error -= water->z.water.minor;
            if (water->z.water.vertical) at.y += water->z.water.direction.y;
            else at.x += water->z.water.direction.x;
            if (water->z.water.error < 0) {
                water->z.water.error += water->z.water.major;
                if (water->z.water.vertical) at.x += water->z.water.direction.x;
                else at.y += water->z.water.direction.y;
            }
            --water->z.water.remaining;
        }
        ++water->missile.age;
        water->core.position = fixed3_from_fixed2(fixed2_scale(fixed2_from_cell(at), FIXED_ONE / Z_TILE), water->core.position.z);
        if (!water->z.water.remaining || !map || at.x < 0 || at.y < 0 ||
            Z_TerrainHeight(map, at.x / Z_TILE, at.y / Z_TILE) > water->z.water.height) {
            water->movement.goal = fixed3_xy(water->core.position);
            P_ExplodeMissile(water);
            return;
        }
    }
    water->core.frame = 1622 + (water->missile.age & 3);
}

void A_ZShellBlast(mobj_t *missile) {
    ivec2_t impact = fixed2_cell(fixed2_scale(missile->movement.goal, FIXED_FROM_INT(Z_TILE)));
    /* Water's 0x59369 branch passes impact directly to 0x8476c, rather
     * than the shell branch's impact minus (12,24). */
    if (missile->type_id == MT_WATER) impact = ivec2_add(impact, (ivec2_t){12, 24});
    mobj_t *target = missile->attack.target;
    bool primary_hit = false;
    blast_object(impact, missile->missile.damage);
    blast_building(ivec2_add(impact, (ivec2_t){4, -8}), missile->target, missile->missile.damage);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *victim = (mobj_t *)th;
        if (th->function != P_MobjThinker || !live(victim) || victim == missile->target ||
            victim->team == missile->team || victim->team >= Z_NEUTRAL || Z_UnitType(victim) < 0) continue;
        ivec2_t delta = ivec2_sub(Z_BlastAnchor(victim), impact);
        if (delta.x >= -18 && delta.x <= 26 && delta.y >= -14 && delta.y <= 18) {
            P_DamageMobj(victim, missile->target, missile->missile.damage);
            primary_hit |= victim == target;
        }
    }
    /* Terrain has a separate native path. */
    if (!primary_hit && live(target) && !structure(target) && !crate(target) &&
        !(target->z.object_slot && target->team == Z_NEUTRAL) &&
        target != missile->target && target->team != missile->team) {
        ivec2_t delta = ivec2_sub(Z_BlastAnchor(target), impact);
        if (delta.x >= -18 && delta.x <= 26 && delta.y >= -14 && delta.y <= 18)
            P_DamageMobj(target, missile->target, missile->missile.damage);
    }
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

static void transfer_territory(mobj_t *flag, int team) {
    const zmap_t *map = level.native_data;
    const zterritory_t *territory = &map->territories[flag->z.territory - 1];
    Z_SetTeam(flag, team);
    flag->z.hold = REACH_FLAG_HOLD;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || mo->remove || !structure(mo) || mo->type_id == MT_FORT) continue;
        ivec2_t at = Z_BuildingOrigin(mo);
        ivec2_t cell = {at.x / Z_TILE, at.y / Z_TILE};
        if (cell.x >= territory->min.x && cell.x < territory->max.x &&
            cell.y >= territory->min.y && cell.y < territory->max.y) Z_SetTeam(mo, team);
    }
}

void Z_FortTerritory(mobj_t *fort, int team) {
    const zmap_t *map = level.native_data;
    int territory = map ? Z_AITerritory(map, fixed2_from_cell(fixed2_footprint_corner(fixed3_xy(fort->core.position), fort->info->footprint))) : -1;
    if (territory < 0) return;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *flag = (mobj_t *)th;
        if (flag->type_id == MT_FLAG && !flag->remove && flag->z.territory == territory + 1) {
            transfer_territory(flag, team);
            flag->z.hold = 0;
            return;
        }
    }
}

/* The territory and the buildings that stand in it change sides at once;
 * a robot that took it stops to cheer (0x62e3c). */
static void take_flag(mobj_t *flag, mobj_t *taker) {
    transfer_territory(flag, taker->team);
    Z_DetourCaptured(taker, flag);
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
    Z_RebuildTicker();
    /* Things taken out stay linked until the next tic (P_RemoveThinker). */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || mo->remove) continue;
        if (mo->type_id == MT_BRIDGE) {
            Z_BridgeTick(mo);
            continue;
        }
        if (mo->type_id == MT_FLAG) {
            tick_flag(mo);
            continue;
        }
        Z_Alert(mo);
        int t = robot_type(mo);
        if (t < 0) continue;
        if (carries_grenades(t)) {
            for (thinker_t *c = thinkercap.next; c != &thinkercap; c = c->next) {
                mobj_t *box = (mobj_t *)c;
                if (c->function == P_MobjThinker && crate(box) && reaches_crate(mo, box) &&
                    pick_up(mo, box)) break;
            }
        }
        if (mo->z.board) tick_boarding(mo);
    }
    Z_DetourTicker();
}

/* Robots told onto an empty vehicle or gun walk to it and the first there
 * gets in (0x61022); the rest stop beside it. */
bool Z_BoardOrder(mobj_t *const *units, int count, mobj_t *target) {
    if (!Z_Enterable(target)) return false;
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
