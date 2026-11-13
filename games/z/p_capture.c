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
int Z_FreeObjectSlot(void) {
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
        int fire_type = t == MT_UNIT_APC - MT_UNITS && mo->z.driver && Z_IsRobot(mo->z.driver - 1)
                      ? mo->z.driver - 1 : t;
        mo->z.body_fire = body_stats[fire_type].fire;
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
    vehicle->z.detour.claimant = 0;
    Z_SetTeam(vehicle, robot->team);
    vehicle->z.driver = (uint8_t)(Z_UnitType(robot) + 1);
    if (vehicle->z.body_tracked) {
        vehicle->z.body_accuracy = (uint8_t)base_accuracy(robot);
        if (t == MT_UNIT_APC - MT_UNITS) vehicle->z.body_fire = body_stats[Z_UnitType(robot)].fire;
    }
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
        robot->z.just_left_cannon = t != MT_UNIT_APC - MT_UNITS;
        Z_SetTeam(robot, team);
        robot->z.squad = vehicle->z.squad;
    }
    Z_SquadEjected(spawned, count);
    vehicle->z.crew_count = vehicle->z.driver = vehicle->z.squad = vehicle->z.crew_hurt = 0;
    vehicle->z.object = (uint8_t)(t == MT_UNIT_APC - MT_UNITS ? MT_A_P_C :
                        t == MT_UNIT_LIGHT_GUN - MT_UNITS ? MT_GUN :
                        t == MT_UNIT_GATLING - MT_UNITS ? MT_GATTLING :
                        t == MT_UNIT_HOWITZER - MT_UNITS ? MT_HOWITZER : MT_MISSILE);
    vehicle->z.object_slot = (uint8_t)Z_FreeObjectSlot();
    vehicle->z.object_hp = vehicle->hp / 25 ? vehicle->hp / 25 : 1;
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
    if (Z_DetourBoard(robot, vehicle)) return;
    if (!P_HasMoveOrder(robot) && leveltime % BOARD_RETRY == 0) walk_to(robot, vehicle);
}

/* ── grenades ─────────────────────────────────────────────────────────── */

static bool squad_mate(const mobj_t *robot, const mobj_t *other) {
    return other == robot || (robot->z.squad && other->z.squad == robot->z.squad &&
                              other->team == robot->team && robot_type(other) >= 0);
}

static ivec2_t object_origin(const mobj_t *object) {
    const zmap_t *map = level.native_data;
    isize2_t size = {0};
    Z_SpriteSize(map, states[mobjinfo[object->type_id].spawnstate].frame, &size);
    ivec2_t centre = fixed2_cell(fixed2_scale(fixed3_xy(object->core.position), FIXED_FROM_INT(Z_TILE)));
    return ivec2_sub(centre, (ivec2_t){size.w / 2, size.h / 2});
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
    Z_RestoreObjectTerrain(crate, object_origin(crate));
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

/* 0x75939: a robot with grenades throws them at vehicles and guns (Laser
 * and Tough retain their own weapons) and at buildings, which nothing else it
 * carries can hurt (0x753bb). */
static bool throws_at(const mobj_t *robot, const mobj_t *target) {
    int t = robot_type(robot), u = Z_UnitType(target);
    if (t < 0 || t == MT_UNIT_TOUGH - MT_UNITS || !robot->z.grenades || !live(target)) return false;
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
    int type = robot_type(attacker);
    return !structure(victim) || type < 0 || type == MT_UNIT_TOUGH - MT_UNITS || attacker->z.grenades;
}

/* A robot about to fire that will throw instead plays the throw. */
void Z_MobjTicker(mobj_t *mo) {
    if (mo->type_id == MT_BUILDING_DEBRIS || mo->type_id == MT_BUILDING_REBUILD || mo->type_id == MT_WATER || mo->type_id == MT_SHELL || mo->type_id == MT_GRENADE || mo->type_id == MT_BULLET) return;
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

/* 0x5117d: every newly created unit starts with a random +0xb0 phase. */
void Z_InitUnitPhase(mobj_t *unit) {
    zmap_t *map = level.native_data;
    if (map && unit && Z_UnitType(unit) >= 0)
        unit->z.flame_phase = Z_NextRandom(&map->combat_seed) & 15;
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
        int slot = Z_FreeObjectSlot();
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
static int bullet_hit_damage(const mobj_t *attacker, mobj_t *target, int damage, unsigned shot) {
    int victim = Z_UnitType(target);
    if (victim < 0 || Z_IsRobot(victim)) return damage;
    if (attacker && hit_driver(attacker, target, damage, shot)) return 0;
    damage -= 5;
    return damage > 0 ? damage : attacker ? (int)((leveltime + attacker->id + target->id + shot) & 1) : 0;
}

static int hit_damage(const mobj_t *attacker, mobj_t *target, int damage, int divisor, unsigned shot) {
    int firing = Z_UnitType(attacker);
    /* The ground-fire damage pass sets 0x14c05f; 0x86cd4 exempts
     * Fire Trucks. Pyro's current direct hit stands in for that fire. */
    if (firing == MT_UNIT_PYRO - MT_UNITS && target && target->type_id == MT_UNIT_FIRE_TRUCK) return 0;
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
    return attacker && fires_bullets(firing) ? bullet_hit_damage(attacker, target, damage, shot) : damage;
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

/* 0x5a648 uses integer half-axis boundaries, including its asymmetric
 * equality rules and arithmetic right shift for a negative y difference. */
static int water_direction(ivec2_t delta) {
    int x = delta.x, y = delta.y, ax = abs(x), ay = abs(y);
    if (ax >= ay) {
        if (x > 0) return ay <= ax / 2 ? 2 : y < 0 ? 1 : 3;
        return ax / 2 > ay ? 6 : y < 0 ? 7 : 5;
    }
    int half = y < 0 ? (ay + 1) / 2 : ay / 2;
    if (x > 0) return ax < half ? (y < 0 ? 0 : 4) : (y < 0 ? 1 : 3);
    return ax < half ? (y < 0 ? 0 : 4) : (y < 0 ? 7 : 5);
}

/* 0x64250: scatter the chosen target point using native distance and trig.
 * The combat stream is saved, but remains separate from retail's global RNG. */
static ivec2_t scatter_aim(const mobj_t *attacker, ivec2_t origin, ivec2_t aim, int divisor) {
    zmap_t *map = level.native_data;
    int dx = (origin.x - aim.x) >> 3, dy = (origin.y - aim.y) >> 3;
    int index = dx * dx + dy * dy;
    if (index > 1023) index = 1023;
    if (map && map->square_roots.bytes && map->trig.bytes && map->square_roots.bytes[index] > 6) {
        int skill = accuracy(attacker);
        skill += skill / 4;
        if (skill > 127) skill = 127;
        int radius = (128 - skill) / divisor;
        if (radius < 1) radius = 1;
        int scale = 65535 / radius;
        int distance = Z_NextRandom(&map->combat_seed) / scale;
        int angle = Z_NextRandom(&map->combat_seed) & 0x3fe;
        int sx = (int16_t)((map->trig.bytes[angle] << 8) | map->trig.bytes[angle + 1]);
        int sy = (int16_t)((map->trig.bytes[angle + 0x100] << 8) | map->trig.bytes[angle + 0x101]);
        aim = ivec2_add(aim, (ivec2_t){((sx >> 5) * distance) >> 10, ((sy >> 5) * distance) >> 10});
    }
    return aim;
}

/* Native Fire Truck muzzle: hull-facing byte pairs at 0x14b474 and
 * target-facing signed word pairs at 0x148966, added to unit top-left. */
static ivec2_t water_origin(const mobj_t *truck, ivec2_t aim) {
    static const ivec2_t hull[] = {{0,4},{-1,3},{-3,4},{0,1},{1,0},{4,1},{6,4},{4,4}};
    static const ivec2_t nozzle[] = {{-3,-19},{7,-16},{12,-11},{7,-5},{1,-3},{-7,-5},{-12,-9},{-10,-15}};
    ivec2_t origin = Z_AITopLeft(truck);
    int body = angle_to_direction(truck->core.angle, 8, ANG90, true);
    int facing = water_direction(ivec2_sub(aim, ivec2_add(origin, (ivec2_t){14,12})));
    return ivec2_add(origin, ivec2_add(hull[body], nozzle[facing]));
}

/* Facing offsets at 0x1492c8, used by 0x64227. */
static const ivec2_t gun_pivot[] = {{13,-2},{24,1},{29,9},{22,17},{13,20},{4,18},{-4,9},{2,0}};

/* 0x590c9/0x64227/0x5915f: tank turret pivot, mount and barrel. */
static ivec2_t tank_origin(const mobj_t *tank) {
    static const ivec2_t mount[] = {{0,0},{-2,0},{-3,-3},{-2,-3},{0,-3},{0,-3},{0,-3},{-1,0}};
    static const ivec2_t barrel[3][8] = {
        {{-11,-2},{0,1},{-2,6},{-2,12},{-10,11},{-14,9},{-16,5},{-14,-3}},
        {{-12,-4},{-7,-4},{-1,6},{-2,14},{-11,14},{-20,11},{-20,7},{-15,-3}},
        {{-16,5},{-13,6},{-10,12},{-13,11},{-16,11},{-16,8},{-16,10},{-13,4}}
    };
    int type = tank->type_id == MT_UNIT_MED_TANK ? 0 : tank->type_id == MT_UNIT_HEAVY_TANK ? 1 : 2;
    int facing = angle_to_direction(tank->core.angle + tank->attack.aim, 8, ANG90, true);
    return ivec2_add(Z_AITopLeft(tank), ivec2_add(gun_pivot[facing],
        ivec2_add(mount[facing], ivec2_add((ivec2_t){6,-9}, barrel[type][facing]))));
}

/* Jeep body and weapon offsets at 0x14b484 and 0x148b60 (0x59116). */
static ivec2_t jeep_origin(const mobj_t *jeep) {
    static const ivec2_t body[] = {{2,8},{-5,11},{-10,6},{-6,0},{0,0},{12,0},{14,5},{10,11}};
    static const ivec2_t gun[] = {{8,-2},{9,-1},{13,1},{10,4},{6,4},{3,2},{0,0},{4,-4}};
    int hull = angle_to_direction(jeep->core.angle, 8, ANG90, true);
    int facing = angle_to_direction(jeep->core.angle + jeep->attack.aim, 8, ANG90, true);
    return ivec2_add(Z_AITopLeft(jeep), ivec2_add(body[hull], gun[facing]));
}

/* 0x5a205: common arc trajectory for a grenade or a tank shell. */
static void init_arc(mobj_t *projectile, const mobj_t *attacker, ivec2_t start, ivec2_t aim, int speed) {
    ivec2_t pixels = ivec2_sub(aim, start);
    int x = abs(pixels.x), y = abs(pixels.y);
    zgrenade_t *flight = &projectile->z.grenade;
    flight->start = start;
    flight->direction = (ivec2_t){(pixels.x > 0) - (pixels.x < 0), (pixels.y > 0) - (pixels.y < 0)};
    flight->vertical = y > x;
    flight->remaining = flight->major = y > x ? y : x;
    flight->minor = y > x ? x : y;
    flight->error = flight->major / 2;
    flight->speed = speed;
    flight->range = (int)((int64_t)(attacker->info->attack.range +
        Z_AttackRangeBonus(attacker, &attacker->info->attack)) * Z_TILE / FIXED_ONE);
    const zmap_t *map = level.native_data;
    ivec2_t origin = Z_AITopLeft(attacker);
    flight->height = map && origin.x >= 0 && origin.y >= 0 ?
        Z_TerrainHeight(map, origin.x / Z_TILE, origin.y / Z_TILE) + 1 : 0;
    int ax = (start.x - aim.x) >> 7, ay = (start.y - aim.y) >> 7;
    int index = ax * ax + ay * ay;
    if (index > 1023) index = 1023;
    int root = map && map->square_roots.bytes ? map->square_roots.bytes[index] : 1;
    flight->arc = root > 5 ? 4 : root > 0 ? root - 1 : 0;
    flight->segment = flight->segment_length = flight->major / (2 * flight->arc + 1);
}

static void fire_shell(mobj_t *attacker, mobj_t *target, int speed, int damage, bool scatter, bool water, int port, const ivec2_t *shared_aim) {
    mobj_t *shell = P_SpawnMobj(attacker->core.position, water ? MT_WATER : MT_SHELL);
    if (!shell) return;
    ivec2_t aim = shared_aim ? *shared_aim : Z_BlastAnchor(target);
    if (water && Z_UnitType(target) >= 0)
        aim = ivec2_add(Z_AITopLeft(target), (ivec2_t){12, 8}); /* 0x76849/0x76aa3. */
    bool tank = attacker->type_id == MT_UNIT_LIGHT_TANK || attacker->type_id == MT_UNIT_MED_TANK ||
                attacker->type_id == MT_UNIT_HEAVY_TANK;
    if (tank && Z_UnitType(target) >= 0) {
        aim = ivec2_add(Z_AITopLeft(target), (ivec2_t){12, 8});
        aim = scatter_aim(attacker, tank_origin(attacker), aim, 4);
    }
    if (scatter)
        aim = ivec2_add(aim, (ivec2_t){(coin(attacker, shell, 0x1ebc1) & 15) - 8,
                                      (coin(attacker, shell, 0x1ebd2) & 15) - 8});
    fixed2_t impact = fixed2_scale(fixed2_from_cell(aim), FIXED_ONE / Z_TILE);
    {
        ivec2_t start = water ? water_origin(attacker, aim) :
            fixed2_cell(fixed2_scale(fixed3_xy(attacker->core.position), FIXED_FROM_INT(Z_TILE)));
        if (attacker->type_id == MT_UNIT_LIGHT_TANK || attacker->type_id == MT_UNIT_MED_TANK ||
            attacker->type_id == MT_UNIT_HEAVY_TANK) start = tank_origin(attacker);
        if (attacker->type_id == MT_UNIT_LIGHT_GUN || attacker->type_id == MT_UNIT_HOWITZER) {
            int facing = angle_to_direction(attacker->core.angle, 8, ANG90, true);
            start = ivec2_add(Z_AITopLeft(attacker), gun_pivot[facing]);
        } else if (attacker->type_id == MT_UNIT_TOUGH) {
            static const ivec2_t muzzle[] = {{3,-5},{-1,-6},{5,-5},{3,-1},{-2,3},{-4,0},{-7,-2},{-2,-5}};
            int facing = angle_to_direction(attacker->core.angle, 8, ANG90, true);
            start = ivec2_add(Z_AITopLeft(attacker), muzzle[facing]);
        }
        int sprite = 0;
        if (attacker->type_id == MT_UNIT_M_MISSILE) {
            static const ivec2_t body[] = {{2,-2},{2,-2},{4,0},{1,-3},{2,1},{1,-2},{3,-3},{1,-4}};
            static const ivec2_t nose[] = {{-10,-4},{-3,-3},{0,0},{1,0},{3,2},{4,4},{-5,6},{-3,3}};
            static const ivec2_t side[] = {{-6,0},{-6,-6},{0,-6},{3,-6},{6,0},{6,6},{0,6},{-6,6}};
            static const ivec2_t other[] = {{6,0},{6,6},{0,6},{-6,6},{-6,0},{-6,-6},{0,-6},{6,-6}};
            int hull = angle_to_direction(attacker->core.angle, 8, ANG90, true);
            int turret = angle_to_direction(attacker->core.angle + attacker->attack.aim, 8, ANG90, true);
            ivec2_t offset = port == 1 ? side[turret] : port == 2 ? other[turret] : (ivec2_t){0,0};
            start = ivec2_add(Z_AITopLeft(attacker), ivec2_add(body[hull], ivec2_add(nose[turret], (ivec2_t){0,-8})));
            start = ivec2_add(start, offset);
            aim = ivec2_add(aim, offset);
            sprite = 5425;
        } else if (attacker->type_id == MT_UNIT_MISSILE_GUN) {
            static const ivec2_t muzzle[2][8] = {
                {{4,-5},{11,-2},{25,6},{18,14},{10,16},{1,15},{-8,8},{-1,-2}},
                {{14,-5},{21,2},{25,13},{12,18},{-1,16},{-6,9},{-8,1},{2,-8}}
            };
            static const ivec2_t spread[2][8] = {
                {{-3,0},{-3,-3},{0,-3},{3,-3},{3,0},{3,3},{0,3},{-3,3}},
                {{3,0},{3,3},{0,3},{-3,3},{-3,0},{-3,-3},{0,-3},{3,-3}}
            };
            int facing = angle_to_direction(attacker->core.angle, 8, ANG90, true);
            start = ivec2_add(Z_AITopLeft(attacker), muzzle[port][facing]);
            aim = ivec2_add(aim, spread[port][facing]);
            sprite = 5345;
        }
        impact = fixed2_scale(fixed2_from_cell(aim), FIXED_ONE / Z_TILE);
        ivec2_t delta = ivec2_sub(aim, start);
        int dx = abs(delta.x), dy = abs(delta.y);
        if (!dx && !dy) { P_RemoveMobj(shell); return; } /* 0x59ffb rejects equal endpoints. */
        bool arc = !water && (attacker->type_id == MT_UNIT_LIGHT_TANK || attacker->type_id == MT_UNIT_MED_TANK ||
                              attacker->type_id == MT_UNIT_HEAVY_TANK || attacker->type_id == MT_UNIT_LIGHT_GUN ||
                              attacker->type_id == MT_UNIT_HOWITZER);
        if (arc) {
            init_arc(shell, attacker, start, aim, speed);
            shell->core.frame = 1634;
        } else {
            shell->z.water.direction = (ivec2_t){(delta.x > 0) - (delta.x < 0), (delta.y > 0) - (delta.y < 0)};
            shell->z.water.vertical = dy > dx;
            shell->z.water.remaining = shell->z.water.major = dy > dx ? dy : dx;
            shell->z.water.minor = dy > dx ? dx : dy;
            shell->z.water.error = shell->z.water.major / 2;
            shell->z.water.start = start;
            shell->z.water.speed = speed;
            shell->z.water.sprite = sprite;
            if (sprite) shell->core.frame = sprite;
            shell->z.water.range = water ? 0 : (int)((int64_t)(attacker->info->attack.range +
                Z_AttackRangeBonus(attacker, &attacker->info->attack)) * Z_TILE / FIXED_ONE);
            const zmap_t *map = level.native_data;
            ivec2_t origin = Z_AITopLeft(attacker);
            shell->z.water.height = map && origin.x >= 0 && origin.y >= 0 ?
                Z_TerrainHeight(map, origin.x / Z_TILE, origin.y / Z_TILE) + 1 : 0;
        }
        shell->core.position = fixed3_from_fixed2(fixed2_scale(fixed2_from_cell(start), FIXED_ONE / Z_TILE), shell->core.position.z);
        shell->target = attacker;
        shell->team = attacker->team; shell->owner = attacker->owner; shell->allegiance = attacker->allegiance;
        shell->movement.goal = impact;
        shell->attack.target = target;
        shell->missile.damage = damage;
        return;
    }
}

static void fire_bullet(mobj_t *attacker, const weapondef_t *weapon, mobj_t *target) {
    static const ivec2_t apc_port[] = {{5,-4},{14,-4},{21,3},{17,11},{11,14},{3,15},{-4,8},{-1,1}};
    ivec2_t origin = Z_AITopLeft(attacker);
    bool gatling = attacker->type_id == MT_UNIT_GATLING;
    bool jeep = attacker->type_id == MT_UNIT_JEEP;
    bool apc = attacker->type_id == MT_UNIT_APC;
    int facing = angle_to_direction(attacker->core.angle, 8, ANG90, true);
    ivec2_t start = jeep ? jeep_origin(attacker) : ivec2_add(origin, apc ? apc_port[facing] : gatling ? gun_pivot[facing] : (ivec2_t){2,2});
    if (apc && attacker->z.driver == MT_UNIT_LASER - MT_UNITS + 1) start = ivec2_sub(start, (ivec2_t){7,7});
    ivec2_t aim = Z_AITopLeft(target);
    if (gatling) aim = scatter_aim(attacker, start, ivec2_add(aim, (ivec2_t){0,12}), 4);
    else if (jeep) aim = scatter_aim(attacker, start, ivec2_add(aim, (ivec2_t){12,8}), 4);
    else {
        if (target->info && (target->info->traits & MF_TURRET)) aim.x += 8;
        aim = ivec2_add(scatter_aim(attacker, origin, aim, 4), (ivec2_t){8,8});
    }
    ivec2_t delta = ivec2_sub(aim, start);
    int dx = abs(delta.x), dy = abs(delta.y), count = 0;
    if (!dx && !dy) return;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && ((mobj_t *)th)->type_id == MT_BULLET && !((mobj_t *)th)->remove) ++count;
    if (count >= 200) return;
    mobj_t *bullet = P_SpawnMobj(fixed3_from_fixed2(fixed2_scale(fixed2_from_cell(start), FIXED_ONE / Z_TILE), 0), MT_BULLET);
    if (!bullet) return;
    bullet->target = attacker;
    bullet->team = attacker->team;
    bullet->owner = attacker->owner;
    if (attacker->type_id != MT_UNIT_LASER && !(apc && attacker->z.driver == MT_UNIT_LASER - MT_UNITS + 1))
        bullet->traits |= MF_RENDERABLE;
    bullet->missile.damage = weapon->damage;
    bullet->z.water.start = start;
    bullet->z.water.direction = (ivec2_t){(delta.x > 0) - (delta.x < 0), (delta.y > 0) - (delta.y < 0)};
    bullet->z.water.vertical = dy > dx;
    bullet->z.water.major = bullet->z.water.remaining = dy > dx ? dy : dx;
    bullet->z.water.minor = dy > dx ? dx : dy;
    bullet->z.water.error = bullet->z.water.major / 2;
    bullet->z.water.speed = 15; /* ROBOTS.DAT weapons 2, 4, 7, 11, 13 and 18 +0x23 */
    bullet->z.water.range = (int)((int64_t)weapon->range * Z_TILE / FIXED_ONE) + (jeep || apc ? 8 : 0);
    const zmap_t *map = level.native_data;
    ivec2_t terrain_at = ivec2_add(origin, (ivec2_t){8,8});
    bullet->z.water.height = map && terrain_at.x >= 0 && terrain_at.y >= 0 ?
        Z_TerrainHeight(map, terrain_at.x / Z_TILE, terrain_at.y / Z_TILE) + 1 : 0;
}

bool Z_Attack(mobj_t *attacker, const weapondef_t *weapon, mobj_t *target) {
    if (throws_at(attacker, target)) {
        /* Facing-indexed signed pairs at 0x149174 (0x636df). */
        static const ivec2_t muzzle[] = {{5,-4}, {7,-3}, {12,3}, {6,5}, {2,6}, {-2,6}, {-6,2}, {0,-2}};
        int facing = angle_to_direction(attacker->core.angle, 8, ANG90, true);
        ivec2_t start = ivec2_add(Z_AITopLeft(attacker), muzzle[facing]);
        fixed2_t source = fixed2_scale(fixed2_from_cell(start), FIXED_ONE / Z_TILE);
        int target_type = Z_UnitType(target);
        ivec2_t aim;
        if (target_type >= 0) {
            bool gun = target_type == MT_UNIT_LIGHT_GUN - MT_UNITS || target_type == MT_UNIT_MISSILE_GUN - MT_UNITS ||
                       target_type == MT_UNIT_GATLING - MT_UNITS || target_type == MT_UNIT_HOWITZER - MT_UNITS;
            aim = ivec2_add(Z_AITopLeft(target), (ivec2_t){gun ? 8 : 0, 0});
        } else aim = fixed2_cell(fixed2_scale(fixed3_xy(target->core.position), FIXED_FROM_INT(Z_TILE)));
        aim = scatter_aim(attacker, Z_AITopLeft(attacker), aim, target_type < 0 ? 2 : 4);
        if (target_type >= 0) aim = ivec2_add(aim, (ivec2_t){8, 8});
        fixed2_t destination = fixed2_scale(fixed2_from_cell(aim), FIXED_ONE / Z_TILE);
        if (fixed2_equal(source, destination)) return true; /* 0x5a205 rejects a zero-length throw. */
        mobj_t *grenade = P_SpawnMissile(attacker, target, MT_GRENADE);
        if (grenade) {
            grenade->core.position = fixed3_from_fixed2(source, 0);
            /* Native strength / 8, from the first shipped robot record for
             * each grenade-capable type (0x636d4 and 0x510fd). */
            static const uint8_t speeds[] = {0, 0, 3, 12, 5, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 6};
            int type = Z_UnitType(attacker);
            int speed = type >= 0 && (size_t)type < sizeof(speeds) ? speeds[type] : 0;
            if (!speed) speed = 1;
            ivec2_t aim = fixed2_cell(fixed2_scale(destination, FIXED_FROM_INT(Z_TILE)));
            init_arc(grenade, attacker, start, aim, speed);
            grenade->traits &= ~MF_MISSILE;
            grenade->traits |= MF_NOBLOCKMAP;
            grenade->core.momentum = fixed3_zero();
            grenade->attack.target = target;
            attacker->z.grenades--;
            return true;
        }
    }
    int t = Z_UnitType(attacker);
    if ((t == MT_UNIT_GRUNT - MT_UNITS || t == MT_UNIT_SNIPER - MT_UNITS ||
         t == MT_UNIT_PSYCHO - MT_UNITS || t == MT_UNIT_LASER - MT_UNITS ||
         t == MT_UNIT_GATLING - MT_UNITS || t == MT_UNIT_JEEP - MT_UNITS) &&
        weapon && target && Z_UnitType(target) >= 0) {
        fire_bullet(attacker, weapon, target);
        return true;
    }
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
            if (t == MT_UNIT_PYRO - MT_UNITS && *phase < 4 && live(target) && target->team == team && driver &&
                target->type_id != MT_UNIT_FIRE_TRUCK)
                P_DamageMobj(target, attacker, driver->attack.damage);
        }
        if (t == MT_UNIT_PYRO - MT_UNITS) return true;
    }
    if (apc && fires_bullets(t) && target && weapon && Z_UnitType(target) >= 0) {
        const mobjtype_t *driver = P_ActorType((uint16_t)(MT_UNITS + t));
        int count = attacker->z.crew_count ? attacker->z.crew_count : 1;
        if (driver) for (int i = 0; i < count; ++i) fire_bullet(attacker, &driver->attack, target);
        return true;
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
    if (!apc && attacker->type_id == MT_UNIT_M_MISSILE) count = 3;
    if (!apc && attacker->type_id == MT_UNIT_MISSILE_GUN) count = 2;
    bool gun = !apc && (attacker->type_id == MT_UNIT_LIGHT_GUN || attacker->type_id == MT_UNIT_HOWITZER ||
                        attacker->type_id == MT_UNIT_MISSILE_GUN);
    ivec2_t aim;
    const ivec2_t *shared_aim = NULL;
    if (gun && Z_UnitType(target) >= 0) {
        int facing = angle_to_direction(attacker->core.angle, 8, ANG90, true);
        ivec2_t origin = ivec2_add(Z_AITopLeft(attacker), gun_pivot[facing]);
        aim = ivec2_add(Z_AITopLeft(target), (ivec2_t){0, 12});
        aim = scatter_aim(attacker, origin, aim, 4);
        shared_aim = &aim;
    }
    for (int i = 0; i < count; ++i) fire_shell(attacker, target, speed, water ? damage / 2 : damage, apc, water, i, shared_aim);
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

static int blast_object_flags(const mobj_t *mo) {
    bool empty = mo->z.object && mo->team == Z_NEUTRAL && !mo->z.driver && Z_UnitType(mo) >= 0;
    int type = empty ? mo->z.object : mo->type_id;
    if (!live(mo) || type <= MT_NULL || type >= MT_FORT || (!mo->z.object_slot && !empty)) return 0;
    int flags = z_object_flags[type - 1];
    return flags & 0x10 ? flags : 0;
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
            int flags = blast_object_flags(box);
            if (!flags) continue;
            isize2_t size;
            if (!Z_SpriteSize(map, states[box->core.state_id].frame, &size)) continue;
            ivec2_t centre = fixed2_cell(fixed2_scale(fixed3_xy(box->core.position), FIXED_FROM_INT(Z_TILE)));
            ivec2_t at = ivec2_sub(centre, (ivec2_t){size.w / 2, size.h / 2});
            ivec2_t d = ivec2_sub(point, at);
            int extent = ((flags + 1) & 3) * 8;
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
        if (!crate(found)) {
            int type = found->z.object ? found->z.object : found->type_id;
            int flags = z_object_flags[type - 1];
            if (flags & 4) {
                ivec2_t cell = ivec2_add(origin, (ivec2_t){0, flags & 2 ? 16 : 0});
                cell = (ivec2_t){cell.x / Z_TILE, cell.y / Z_TILE};
                if (origin.x >= 0 && origin.y >= 0 && L_Contains(&level, cell.x, cell.y)) {
                    zmap_t *mutable_map = level.native_data;
                    int index = cell.y * Z_MAP_STRIDE + cell.x;
                    mutable_map->ground_attr[index] = (mutable_map->ground_attr[index] & 0x80) | 0x16;
                    Z_RebuildCliffs(&level);
                }
            }
            P_RemoveMobj(found);
            return;
        }
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

/* 0x5a4c8, 0x59440 and 0x594a8: travel, range and arc-height terrain. */
void A_ZGrenade(mobj_t *grenade) {
    static const uint8_t arcs[5][9] = {
        {16}, {16,12,16}, {16,12,8,12,16},
        {16,12,8,4,8,12,16}, {16,12,8,4,0,4,8,12,16}
    };
    zgrenade_t *flight = &grenade->z.grenade;
    const zmap_t *map = level.native_data;
    if (flight->arc < 0 || flight->arc >= 5 || flight->sample < 0 || flight->sample > 2 * flight->arc ||
        flight->speed <= 0 || flight->speed > 255 || flight->major <= 0 || flight->major > 65535 ||
        flight->minor < 0 || flight->minor > flight->major || flight->error < 0 || flight->error > flight->major ||
        flight->remaining <= 0 || flight->remaining > flight->major || flight->range <= 0 || flight->range > 2040 ||
        flight->segment < -255 || flight->segment > 65535 || flight->segment_length < 0 || flight->segment_length > 65535 ||
        flight->start.x < 0 || flight->start.x > 65535 || flight->start.y < 0 || flight->start.y > 65535 ||
        flight->direction.x < -1 || flight->direction.x > 1 || flight->direction.y < -1 || flight->direction.y > 1) {
        P_RemoveMobj(grenade);
        return;
    }
    for (int steps = Z_NativeSteps(leveltime, Z_NATIVE_HZ); steps-- > 0;) {
        ivec2_t at = fixed2_cell(fixed2_scale(fixed3_xy(grenade->core.position), FIXED_FROM_INT(Z_TILE)));
        int moved = 0;
        for (; moved < flight->speed && flight->remaining > 0; ++moved) {
            flight->error -= flight->minor;
            if (flight->vertical) at.y += flight->direction.y;
            else at.x += flight->direction.x;
            if (flight->error < 0) {
                flight->error += flight->major;
                if (flight->vertical) at.x += flight->direction.x;
                else at.y += flight->direction.y;
            }
            --flight->remaining;
        }
        ++grenade->missile.age;
        flight->segment -= moved;
        grenade->core.position = fixed3_from_fixed2(fixed2_scale(fixed2_from_cell(at), FIXED_ONE / Z_TILE), 0);
        if (!flight->remaining || ivec2_distance_squared64(at, flight->start) >= (int64_t)flight->range * flight->range) {
            if (grenade->type_id == MT_SHELL) grenade->movement.goal = fixed3_xy(grenade->core.position);
            P_ExplodeMissile(grenade);
            return;
        }
        if (flight->segment < 0) {
            flight->segment = flight->segment_length;
            if (flight->sample < 2 * flight->arc) ++flight->sample;
        }
        int height = 16 - arcs[flight->arc][flight->sample];
        grenade->core.position.z = (fixed_t)((int64_t)height * FIXED_ONE / Z_TILE);
        if (map && (at.x < 0 || at.y < 0 ||
            Z_TerrainHeight(map, at.x / Z_TILE, at.y / Z_TILE) > flight->height + height / 2)) {
            grenade->core.position.z = 0;
            if (grenade->type_id == MT_SHELL) grenade->movement.goal = fixed3_xy(grenade->core.position);
            P_ExplodeMissile(grenade);
            return;
        }
    }
    grenade->core.frame = grenade->type_id == MT_SHELL ? 1634 : Z_GRENADE_SPRITE + (grenade->missile.age & 3);
}

/* 0x5ac67 searches the current 128-pixel bin and its eight neighbors.
 * Thinker order approximates the original reusable unit-slot order. */
static bool bullet_hit(mobj_t *bullet, ivec2_t at) {
    static const int offsets[] = {0,-1,1,-17,-16,-15,15,16,17};
    /* 0x5abad: table 0x11662f marks impassable bullet terrain with 0xff. */
    static const uint64_t blocked[] = {UINT64_C(0x03014007330cf87e), UINT64_C(0x38be48fdfc6ff3f0)};
    const zmap_t *map = level.native_data;
    ivec2_t cell = {at.x / Z_TILE, at.y / Z_TILE};
    if (map && L_Contains(&level, cell.x, cell.y)) {
        unsigned terrain = map->terrain[cell.y * Z_MAP_STRIDE + cell.x] & 0x7f;
        bool stop = (blocked[terrain / 64] >> (terrain % 64)) & 1;
        if (stop && (at.y & 8) && L_Contains(&level, cell.x, cell.y + 1)) {
            terrain = map->terrain[(cell.y + 1) * Z_MAP_STRIDE + cell.x] & 0x7f;
            stop = (blocked[terrain / 64] >> (terrain % 64)) & 1;
        }
        if (stop) return true;
    }
    if (at.x < 8 || at.y < 8) return false;
    int bin = ((at.x - 8) >> 7) + 16 * ((at.y - 8) >> 7);
    for (int i = 0; i < 9; ++i) {
        int wanted = bin + offsets[i], count = 0;
        if (wanted < 0 || wanted >= 256) continue;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *victim = (mobj_t *)th;
            if (th->function != P_MobjThinker || !live(victim) || Z_UnitType(victim) < 0 || victim->team >= Z_NEUTRAL) continue;
            ivec2_t origin = Z_AITopLeft(victim);
            if (origin.x < 0 || origin.y < 0 || ((origin.x >> 7) + 16 * (origin.y >> 7)) != wanted) continue;
            if (++count > 32) break;
            if (victim->team == bullet->team) continue;
            ivec2_t d = ivec2_sub(origin, ivec2_sub(at, (ivec2_t){8,8}));
            int extent = !Z_IsRobot(Z_UnitType(victim)) && victim->info && (victim->info->traits & MF_MOBILE) ? 16 : 8;
            if (abs(d.x) > extent || abs(d.y) > extent) continue;
            mobj_t *source = bullet->target;
            unsigned shot = source && source->type_id == MT_UNIT_APC ? bullet->id : 0;
            int damage = bullet_hit_damage(source, victim, bullet->missile.damage, shot);
            if (damage > 0) P_DamageMobj(victim, source, damage);
            else if (source) P_ProvokeMobj(victim, source);
            return true;
        }
    }
    return false;
}

void A_ZBullet(mobj_t *bullet) {
    const zmap_t *map = level.native_data;
    if (bullet->z.water.speed <= 0 || bullet->z.water.speed > 255 || bullet->z.water.major <= 0 ||
        bullet->z.water.major > 65535 || bullet->z.water.minor < 0 || bullet->z.water.minor > bullet->z.water.major ||
        bullet->z.water.error < 0 || bullet->z.water.error > bullet->z.water.major ||
        bullet->z.water.remaining <= 0 || bullet->z.water.remaining > bullet->z.water.major ||
        bullet->z.water.range <= 0 || bullet->z.water.range > 2040 ||
        bullet->z.water.start.x < 0 || bullet->z.water.start.x > 65535 ||
        bullet->z.water.start.y < 0 || bullet->z.water.start.y > 65535 ||
        bullet->z.water.direction.x < -1 || bullet->z.water.direction.x > 1 ||
        bullet->z.water.direction.y < -1 || bullet->z.water.direction.y > 1) { P_RemoveMobj(bullet); return; }
    for (int steps = Z_NativeSteps(leveltime, Z_NATIVE_HZ); steps-- > 0;) {
        ivec2_t at = fixed2_cell(fixed2_scale(fixed3_xy(bullet->core.position), FIXED_FROM_INT(Z_TILE)));
        int limit = bullet->z.water.speed;
        bool finished = false;
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < limit && bullet->z.water.remaining > 0; ++i) {
                bullet->z.water.error -= bullet->z.water.minor;
                if (bullet->z.water.vertical) at.y += bullet->z.water.direction.y;
                else at.x += bullet->z.water.direction.x;
                if (bullet->z.water.error < 0) {
                    bullet->z.water.error += bullet->z.water.major;
                    if (bullet->z.water.vertical) at.x += bullet->z.water.direction.x;
                    else at.y += bullet->z.water.direction.y;
                }
                --bullet->z.water.remaining;
            }
            finished = bullet->z.water.remaining == 0;
            bullet->core.position = fixed3_from_fixed2(fixed2_scale(fixed2_from_cell(at), FIXED_ONE / Z_TILE), 0);
            if (at.x < 0 || at.y < 0 || !L_Contains(&level, at.x / Z_TILE, at.y / Z_TILE)) { P_RemoveMobj(bullet); return; }
            if (!pass && !finished && ivec2_distance_squared64(at, bullet->z.water.start) >=
                (int64_t)bullet->z.water.range * bullet->z.water.range) { P_RemoveMobj(bullet); return; }
            bool hit = bullet_hit(bullet, at);
            if (pass) { P_RemoveMobj(bullet); return; }
            if (!finished) {
                if (hit || (map && Z_TerrainHeight(map, at.x / Z_TILE, at.y / Z_TILE) > bullet->z.water.height)) { P_RemoveMobj(bullet); return; }
                break;
            }
            bullet->z.water.remaining = 16;
        }
    }
}

/* 0x5a4c8 and 0x59b63: advance each native update with an accelerating
 * Bresenham major axis, then stop on terrain above the launch threshold. */
void A_ZWater(mobj_t *water) {
    if (water->type_id == MT_SHELL && water->z.grenade.major) {
        A_ZGrenade(water);
        return;
    }
    const zmap_t *map = level.native_data;
    if (water->z.water.speed <= 0 || water->z.water.speed > 255 || water->missile.age < 0 || water->missile.age > 65535 ||
        water->z.water.major <= 0 || water->z.water.major > 65535 || water->z.water.minor < 0 ||
        water->z.water.minor > water->z.water.major || water->z.water.error < 0 || water->z.water.error > water->z.water.major ||
        water->z.water.remaining <= 0 || water->z.water.remaining > water->z.water.major ||
        water->z.water.range < 0 || water->z.water.range > 2040 ||
        (water->z.water.sprite && water->z.water.sprite != 5345 && water->z.water.sprite != 5425) ||
        water->z.water.start.x < 0 || water->z.water.start.x > 65535 || water->z.water.start.y < 0 || water->z.water.start.y > 65535 ||
        water->z.water.direction.x < -1 || water->z.water.direction.x > 1 || water->z.water.direction.y < -1 || water->z.water.direction.y > 1) {
        P_RemoveMobj(water);
        return;
    }
    for (int steps = Z_NativeSteps(leveltime, Z_NATIVE_HZ); steps-- > 0;) {
        ivec2_t at = fixed2_cell(fixed2_scale(fixed3_xy(water->core.position), FIXED_FROM_INT(Z_TILE)));
        int speed = water->missile.age < water->z.water.speed ? water->missile.age + 1 : water->z.water.speed;
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
        if (!water->z.water.remaining || (water->z.water.range &&
            ivec2_distance_squared64(at, water->z.water.start) >= (int64_t)water->z.water.range * water->z.water.range) ||
            (map && (at.x < 0 || at.y < 0 ||
            Z_TerrainHeight(map, at.x / Z_TILE, at.y / Z_TILE) > water->z.water.height))) {
            water->core.position.z = 0;
            water->movement.goal = fixed3_xy(water->core.position);
            P_ExplodeMissile(water);
            return;
        }
    }
    water->core.frame = water->z.water.sprite ? water->z.water.sprite : 1622 + (water->missile.age & 3);
}

static ivec2_t blast_impact(const mobj_t *missile) {
    fixed2_t position = missile->type_id == MT_GRENADE ? fixed3_xy(missile->core.position) : missile->movement.goal;
    ivec2_t impact = fixed2_cell(fixed2_scale(position, FIXED_FROM_INT(Z_TILE)));
    /* Water's 0x59369 branch passes impact directly to 0x8476c, rather
     * than the shell branch's impact minus (12,24). */
    if (missile->type_id == MT_WATER) impact = ivec2_add(impact, (ivec2_t){12, 24});
    return impact;
}

static void blast_units(mobj_t *missile, ivec2_t impact) {
    mobj_t *target = missile->attack.target;
    bool primary_hit = false;
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
    if (!primary_hit && live(target) && Z_UnitType(target) >= 0 && !structure(target) && !crate(target) &&
        !blast_object_flags(target) &&
        target != missile->target && target->team != missile->team) {
        ivec2_t delta = ivec2_sub(Z_BlastAnchor(target), impact);
        if (delta.x >= -18 && delta.x <= 26 && delta.y >= -14 && delta.y <= 18)
            P_DamageMobj(target, missile->target, missile->missile.damage);
    }
}

/* 0x8476c damages effects/buildings immediately, while the unit pass runs
 * at explosion frame 3 (0x85c24). Three 30 Hz tics approximate that delay. */
void A_ZBlastImpact(mobj_t *missile) {
    ivec2_t impact = blast_impact(missile);
    blast_object(impact, missile->missile.damage);
    blast_building(ivec2_add(impact, (ivec2_t){4, -8}), missile->target, missile->missile.damage);
    mobj_t *target = missile->attack.target;
    if (live(target) && Z_UnitType(target) < 0 && !structure(target) && !crate(target) &&
        !blast_object_flags(target) &&
        target != missile->target && target->team != missile->team) {
        ivec2_t delta = ivec2_sub(Z_BlastAnchor(target), impact);
        if (delta.x >= -18 && delta.x <= 26 && delta.y >= -14 && delta.y <= 18)
            P_DamageMobj(target, missile->target, missile->missile.damage);
    }
}

void A_ZBlastHit(mobj_t *missile) { blast_units(missile, blast_impact(missile)); }

/* ── flags ────────────────────────────────────────────────────────────── */

/* 0x796cb samples native origin +(8,8); 0x79936 also probes the cells
 * right and below it for a class-1 vehicle. */
static bool on_flag(const mobj_t *unit) {
    const zmap_t *map = level.native_data;
    int t = Z_UnitType(unit);
    if (!map || !live(unit) || t < 0 || unit->team >= Z_NEUTRAL ||
        (!(unit->traits & MF_MOBILE) && !(unit->info && (unit->info->traits & MF_TURRET)))) return false;
    ivec2_t pixel = ivec2_add(Z_AITopLeft(unit), (ivec2_t){8,8});
    if (pixel.x < 0 || pixel.y < 0) return false;
    ivec2_t cell = {pixel.x / Z_TILE, pixel.y / Z_TILE};
    int span = !Z_IsRobot(t) && unit->info && (unit->info->traits & MF_MOBILE) ? 2 : 1;
    for (int y = 0; y < span; ++y) for (int x = 0; x < span; ++x) {
        ivec2_t probe = ivec2_add(cell, (ivec2_t){x,y});
        if (L_Contains(&level, probe.x, probe.y) &&
            (map->terrain[probe.y * Z_MAP_STRIDE + probe.x] & 0x7f) == 0x23) return true;
    }
    return false;
}

static mobj_t *territory_flag(int territory) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *flag = (mobj_t *)th;
        if (th->function == P_MobjThinker && flag->type_id == MT_FLAG && !flag->remove &&
            flag->z.territory == territory + 1) return flag;
    }
    return NULL;
}

static void stamp_flags(void) {
    zmap_t *map = level.native_data;
    for (int i = 0; i < map->territory_count; ++i)
        if (Z_FlagAvailable(map, &map->territories[i])) Z_StampFlagTerrain(map->territories[i].flag);
}

static void transfer_territory(int index, int team) {
    zmap_t *map = level.native_data;
    zterritory_t *territory = &map->territories[index];
    territory->team = team;
    mobj_t *flag = territory_flag(index);
    if (flag) Z_SetTeam(flag, team);
    territory->capture_hold = REACH_FLAG_HOLD;
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
    zmap_t *map = level.native_data;
    int territory = map ? Z_AITerritory(map, fixed2_from_cell(fixed2_footprint_corner(fixed3_xy(fort->core.position), fort->info->footprint))) : -1;
    if (territory < 0) return;
    zterritory_t *t = &map->territories[territory];
    if (!territory_flag(territory) && L_Contains(&level, t->flag.x, t->flag.y)) {
        mobj_t *flag = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center(t->flag), 0), MT_FLAG);
        if (flag) {
            flag->z.territory = (uint8_t)(territory + 1);
            flag->z.object_slot = (uint8_t)Z_FreeObjectSlot();
            flag->core.position.y += 2 * FIXED_ONE;
            flag->core.position.z = 2 * FIXED_ONE;
        }
    }
    t->options &= 0xf0;
    transfer_territory(territory, team);
    t->capture_hold = 0;
    Z_StampFlagTerrain(t->flag);
}

/* The territory and the buildings that stand in it change sides at once;
 * a robot that took it stops to cheer (0x62e3c). */
static void take_flag(int territory, mobj_t *taker) {
    mobj_t *flag = territory_flag(territory);
    transfer_territory(territory, taker->team);
    stamp_flags();
    if (flag) Z_DetourCaptured(taker, flag);
    int t = robot_type(taker);
    if (t < 0) return;
    P_ClearMove(taker);
    taker->movement.goal = fixed3_xy(taker->core.position);
    taker->movement.order_arrived = true;
    taker->core.angle = P_PointToAngle(0, FIXED_ONE);
    P_SetMobjState(taker, cheer_states[t]);
}

static void tick_flag(mobj_t *unit, const bool *blocked) {
    const zmap_t *map = level.native_data;
    if (!map || !on_flag(unit)) return;
    ivec2_t pixel = Z_AITopLeft(unit);
    if (pixel.x < 0 || pixel.y < 0) return;
    int territory = Z_AITerritory(map, fixed2_scale(fixed2_from_cell(pixel), FIXED_ONE / Z_TILE));
    if (territory < 0 || blocked[territory] || map->territories[territory].capture_hold ||
        map->territories[territory].team == unit->team) return;
    take_flag(territory, unit);
}

/* ── each tic ─────────────────────────────────────────────────────────── */

void Z_Ticker(void) {
    if (!level.native_data || !thinkercap.next) return;
    zmap_t *map = level.native_data;
    bool blocked[Z_MAX_TERRITORIES] = {0};
    for (int i = 0; i < map->territory_count; ++i)
        if (map->territories[i].capture_hold) {
            blocked[i] = true;
            --map->territories[i].capture_hold;
        }
    Z_RebuildTicker();
    /* Things taken out stay linked until the next tic (P_RemoveThinker). */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || mo->remove) continue;
        if (mo->type_id == MT_BRIDGE) {
            Z_BridgeTick(mo);
            continue;
        }
        if (mo->type_id == MT_FLAG) continue;
        tick_flag(mo, blocked);
        Z_Alert(mo);
        int t = robot_type(mo);
        if (t < 0) continue;
        ivec2_t pos = ivec2_add(Z_AITopLeft(mo), (ivec2_t){8, 8});
        ivec2_t cell = {pos.x / Z_TILE, pos.y / Z_TILE};
        const zmap_t *map = level.native_data;
        if (Z_CanCarryGrenades(t) && pos.x >= 0 && pos.y >= 0 && L_Contains(&level, cell.x, cell.y) &&
            (map->terrain[cell.y * Z_MAP_STRIDE + cell.x] & 0x7f) == 0x24) {
            mobj_t *first = NULL;
            for (thinker_t *c = thinkercap.next; c != &thinkercap; c = c->next) {
                mobj_t *box = (mobj_t *)c;
                if (c->function != P_MobjThinker || !crate(box) || !reaches_crate(mo, box)) continue;
                if (!first || (box->z.object_slot && (!first->z.object_slot || box->z.object_slot < first->z.object_slot)))
                    first = box;
            }
            if (first) pick_up(mo, first);
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
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            mobj_t *member = (mobj_t *)th;
            if (member == units[i] || (units[i]->z.squad && member->z.squad == units[i]->z.squad &&
                member->team == units[i]->team && Z_IsRobot(Z_UnitType(member))))
                member->z.just_left_cannon = false;
        }
        units[i]->z.board = target->id;
        units[i]->waypoints = (waypoints_t){0};
        walk_to(units[i], target);
        any = true;
    }
    return any;
}
