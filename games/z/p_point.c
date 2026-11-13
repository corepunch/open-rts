#include "z_local.h"
#include "info.h"

/* The robot coordinate attack: ZED.EXE command 5 with target sentinel 0x0d80
 * (the CPU's task 6, 0x9a9ae). A squad leader receives it at 0x7c817 ->
 * 0x7cb27. A grenadier squad with no grenades at all turns it into a move
 * that stops once the aim point is within weapon range (0x7cb93, 0x7bf4e);
 * everything else runs robot action 8 (0x75046, point branch 0x751d0).
 * The order lives on the leader: `point_mode`, `point`, `point_phase`,
 * `point_shots`; a unit about to throw or fire at the point carries
 * `point_throw` with its own copy of the point (0x753d4..0x753f8). */

enum { MODE_ATTACK = 1, MODE_MOVE = 2, MODE_VEHICLE = 3, PHASE_THROWING = 1, PHASE_APPROACH = 2, TOUGH_SHOTS = 2 };

static const int throw_states[] = {
#define Z_UNIT(t, type, ...) S_##type##_THROW,
#include "units.inc"
#undef Z_UNIT
};
static const int toss_states[] = {
#define Z_UNIT(t, type, ...) S_##type##_TOSS,
#include "units.inc"
#undef Z_UNIT
};
static const int stand_states[] = {
#define Z_UNIT(t, type, ...) S_##type##_STND,
#include "units.inc"
#undef Z_UNIT
};

static bool live(const mobj_t *mo) { return mo && !mo->remove && mo->hp > 0; }

/* Classes 0 and 0x10 (Grunt, Sniper, Psycho, Laser) take the grenade path
 * at 0x753bb; Tough (class 3) the launcher action 0x13 at 0x752bc. The Pyro
 * (0x752ff) and vehicles (action ids 0x35..0x3c) are not ported. */
static bool grenadier(int type) {
    return type == MT_UNIT_GRUNT - MT_UNITS || type == MT_UNIT_SNIPER - MT_UNITS ||
           type == MT_UNIT_PSYCHO - MT_UNITS || type == MT_UNIT_LASER - MT_UNITS;
}
static bool tough(const mobj_t *mo) { return mo->type_id == MT_UNIT_TOUGH; }

/* Light, Medium, Heavy tank and Mobile Missile: the vehicle types 0x9a801
 * lets through (+0x30 = 0x1e, 6, 0x19, 0x18). Their order is action 9. */
static bool tank(int type) {
    return type == MT_UNIT_LIGHT_TANK - MT_UNITS || type == MT_UNIT_MED_TANK - MT_UNITS ||
           type == MT_UNIT_HEAVY_TANK - MT_UNITS || type == MT_UNIT_M_MISSILE - MT_UNITS;
}

bool Z_PointCanAttack(const mobj_t *unit) {
    int type = Z_UnitType(unit);
    return live(unit) && type >= 0 && (grenadier(type) || type == MT_UNIT_TOUGH - MT_UNITS || tank(type));
}


static bool same_squad(const mobj_t *a, const mobj_t *b) {
    return a == b || (a->z.squad && a->z.squad == b->z.squad && a->team == b->team &&
                      Z_IsRobot(Z_UnitType(a)) && Z_IsRobot(Z_UnitType(b)));
}

/* 0x7a776: the grenade bytes of every occupied slot of the group. */
int Z_SquadGrenades(const mobj_t *unit) {
    int sum = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function == P_MobjThinker && live(mo) && same_squad(unit, mo)) sum += mo->z.grenades;
    }
    return sum;
}

/* Table 0x1171d1, entries for terrain 0..66: what a robot may attack. */
static const uint8_t robot_terrain[67] = {
    0,1,1,1,1,1,1,0,0,0,0,1,1,1,1,1, 1,0,1,0,0,0,0,0,1,1,1,1,1,0,0,1,
    1,1,1,0,0,1,0,0,0,0,0,1,1,0,1,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0
};

/* 0x29fd8 (an intact bridge rectangle), then 0x193f0 (the terrain under the
 * point). 0x4a03b, which also accepts a point on a destructible map object,
 * is not ported. */
bool Z_PointAttackable(ivec2_t point) {
    const zmap_t *map = level.native_data;
    if (!map || point.x < 0 || point.y < 0 || point.x >= Z_MAP_STRIDE * Z_TILE || point.y >= Z_MAP_STRIDE * Z_TILE)
        return false;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *bridge = (const mobj_t *)th;
        if (th->function == P_MobjThinker && bridge->type_id == MT_BRIDGE && live(bridge) &&
            bridge->z.bridge && irect_contains(map->bridges[bridge->z.bridge - 1].bounds, point)) return true;
    }
    unsigned terrain = map->terrain[(point.y / Z_TILE) * Z_MAP_STRIDE + point.x / Z_TILE] & 0x7f;
    return terrain < sizeof(robot_terrain) && robot_terrain[terrain];
}

/* 0x281da, then 0x75215: the CPU does not shoot at its own BUILD records. */
static bool own_building_at(ivec2_t point) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *b = (const mobj_t *)th;
        if (th->function != P_MobjThinker || !b->z.building || !live(b) || b->team != 1 || !b->info) continue;
        ivec2_t o = Z_BuildingOrigin(b);
        if (point.x >= o.x && point.x <= o.x + b->info->footprint.w * Z_TILE &&
            point.y >= o.y && point.y <= o.y + b->info->footprint.h * Z_TILE) return true;
    }
    return false;
}

static int range_pixels(const mobj_t *unit) {
    return (int)((int64_t)(unit->info->attack.range + Z_AttackRangeBonus(unit, &unit->info->attack)) *
                 Z_TILE / FIXED_ONE);
}

/* 0x60bb2 and 0x75285: the throw starts when four times the root of the
 * squared 16-pixel cell offsets (8-bit squares, SQROOT.BIN) is below unit
 * +4, which is twice the range byte (u16 range >> 3). */
bool Z_PointInRange(const mobj_t *unit, ivec2_t point) {
    const zmap_t *map = level.native_data;
    if (!map || !map->square_roots.bytes) return false;
    ivec2_t at = Z_AITopLeft(unit);
    int dx = (int16_t)(at.x - point.x) >> 4, dy = (int16_t)(at.y - point.y) >> 4;
    int sum = (int16_t)((int8_t)dx * (int8_t)dx) + (int16_t)((int8_t)dy * (int8_t)dy);
    if (sum >= 0x400) return false;
    if (sum < 0) return true;
    return (uint8_t)(4 * map->square_roots.bytes[sum]) < 2 * (range_pixels(unit) >> 3);
}

/* 0x7bf85: the ranged move stops at squared distance <= (range byte * 8)^2. */
bool Z_PointMoveDone(const mobj_t *unit, ivec2_t point) {
    ivec2_t at = Z_AITopLeft(unit);
    int64_t dx = at.x - point.x, dy = at.y - point.y, reach = (range_pixels(unit) >> 3) * 8;
    return reach * reach >= dx * dx + dy * dy;
}

/* 0x591ca, the reach test 0x764e8 runs on a vehicle every step: squared
 * distance from the unit's top-left + (12,8) to the point, inclusive, against
 * (range byte * 8)^2 (body +5, the weapon range in 8-pixel units). */
bool Z_PointReach(const mobj_t *unit, ivec2_t point) {
    ivec2_t at = Z_AITopLeft(unit);
    int64_t dx = at.x + 12 - point.x, dy = at.y + 8 - point.y, reach = (range_pixels(unit) >> 3) * 8;
    return reach * reach >= dx * dx + dy * dy;
}

/* 0x7c906..0x7c933: a vehicle plans a path to the point unless five times
 * the root of the 16-pixel-cell distance (8-bit, 0x60bb2) is below +4. */
static bool vehicle_needs_path(const mobj_t *unit, ivec2_t point) {
    const zmap_t *map = level.native_data;
    ivec2_t at = Z_AITopLeft(unit);
    int dx = (int16_t)(at.x - point.x) >> 4, dy = (int16_t)(at.y - point.y) >> 4;
    int sum = (int16_t)((int8_t)dx * (int8_t)dx) + (int16_t)((int8_t)dy * (int8_t)dy);
    if (sum >= 0x400 || sum < 0 || !map || !map->square_roots.bytes) return true;
    return (uint8_t)(5 * map->square_roots.bytes[sum]) >= 2 * (range_pixels(unit) >> 3);
}

/* 0x5a648: eight directions with half-axis boundaries. */
static angle_t facing(ivec2_t delta) {
    int x = delta.x, y = delta.y, ax = abs(x), ay = abs(y), d;
    if (ax >= ay) d = ay <= ax / 2 ? (x > 0 ? 2 : 6) : x > 0 ? (y < 0 ? 1 : 3) : (y < 0 ? 7 : 5);
    else {
        int half = y < 0 ? (ay + 1) / 2 : ay / 2;
        d = ax < half ? (y < 0 ? 0 : 4) : x > 0 ? (y < 0 ? 1 : 3) : (y < 0 ? 7 : 5);
    }
    return direction_to_angle(d, 8, ANG90, true);
}

static fixed2_t pixel_goal(ivec2_t pixel) {
    return (fixed2_t){pixel.x * (FIXED_ONE / Z_TILE), pixel.y * (FIXED_ONE / Z_TILE)};
}

bool Z_PointActive(const mobj_t *unit) { return unit->z.point_mode || unit->z.point_throw; }

void Z_PointCancel(mobj_t *unit) {
    unit->z.point_mode = unit->z.point_phase = unit->z.point_shots = 0;
}

/* 0x75423 / 0x79049 / 0x7cdaf: the order ends, done or not. */
static void finish(mobj_t *unit) {
    Z_PointCancel(unit);
    P_ClearMove(unit);
}

static bool throwing(const mobj_t *unit) {
    int type = Z_UnitType(unit);
    return type >= 0 && (unit->core.state_id == throw_states[type] || unit->core.state_id == toss_states[type]);
}

bool Z_PointPhaseValid(const mobj_t *unit) {
    int type = Z_UnitType(unit);
    uint8_t flag;
    memcpy(&flag, &unit->z.point_throw, 1);
    if (flag > 1) return false;
    if (!unit->z.point_mode && !unit->z.point_phase && !unit->z.point_shots && !flag) return true;
    return unit->z.point_mode <= MODE_VEHICLE && unit->z.point_phase <= PHASE_APPROACH &&
           unit->z.point_shots <= TOUGH_SHOTS &&
           (unit->z.point_mode == MODE_VEHICLE ? tank(type) : Z_IsRobot(type)) && unit->z.point.x >= 0 && unit->z.point.y >= 0 &&
           unit->z.point.x < Z_MAP_STRIDE * Z_TILE && unit->z.point.y < Z_MAP_STRIDE * Z_TILE;
}

/* 0x75432: action 0x31 for a grenade carrier, facing the point. */
static void start_throw(mobj_t *carrier, ivec2_t point) {
    int type = Z_UnitType(carrier);
    carrier->z.point = point;
    carrier->z.point_throw = true;
    carrier->core.angle = facing(ivec2_sub(point, Z_AITopLeft(carrier)));
    P_ClearMove(carrier);
    P_SetMobjState(carrier, throw_states[type]);
}

/* 0x7545c: the first occupied slot holding grenades whose action is 1 or 7
 * (standing). The leader was already tried at 0x753bb. */
static mobj_t *pick_carrier(const mobj_t *leader) {
    mobj_t *best = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        int type = Z_UnitType(mo);
        if (th->function != P_MobjThinker || mo == leader || !live(mo) || type < 0 || !mo->z.grenades ||
            !same_squad(leader, mo) || mo->z.point_throw || P_HasMoveOrder(mo) ||
            mo->core.state_id != stand_states[type]) continue;
        if (!best || mo->z.squad_slot < best->z.squad_slot) best = mo;
    }
    return best;
}

bool Z_PointAttackOrder(mobj_t *leader, ivec2_t point) {
    if (!Z_PointCanAttack(leader) || !Z_PointAttackable(point)) return false;
    Z_SquadOrder(leader, true);
    P_ClearMove(leader);
    leader->waypoints = (waypoints_t){0};
    leader->attack.target = NULL;
    leader->move_only = false;
    leader->z.board = 0;
    leader->z.route = (zroute_t){0};
    leader->z.point = point;
    Z_PointCancel(leader);
    if (tank(Z_UnitType(leader))) {
        /* 0x7c872: path to the point unless already well inside the range. */
        /* A failed path plan (0x74eb7) does not refuse the order; it ends at the first step. */
        if (vehicle_needs_path(leader, point)) P_MoveUnitTo(&level, leader, pixel_goal(point));
        leader->z.point_mode = MODE_VEHICLE;
        return true;
    }
    if (grenadier(Z_UnitType(leader)) && !Z_SquadGrenades(leader)) {
        /* 0x7cb93: no grenade anywhere in the group, so go to weapon range. */
        if (!P_MoveUnitTo(&level, leader, pixel_goal(point))) return false;
        leader->z.point_mode = MODE_MOVE;
        return true;
    }
    leader->z.point_mode = MODE_ATTACK;
    return true;
}

void Z_PointToss(mobj_t *unit) {
    int grenades = unit->z.grenades;
    unit->z.point_throw = false;
    if (!Z_ThrowAtPoint(unit, unit->z.point) || unit->z.grenades == grenades) return;
    int steps = (unit->info->attack.cooldown_ms * Z_NATIVE_HZ + 999) / 1000;
    unit->attack.cooldown_left_ms = 2 * steps * 1000 / Z_NATIVE_HZ;
}

/* Action 0x13 (0x60c2a): two launcher shells at the point, the second after
 * the reload, then the group looks for a target. */
static void tough_tick(mobj_t *unit) {
    int type = Z_UnitType(unit);
    if (unit->z.point_throw) {
        if (unit->core.state_id == S_TOUGH_SHOT) {
            unit->z.point_throw = false;
            ++unit->z.point_shots;
            if (Z_ShellAtPoint(unit, unit->z.point)) unit->attack.cooldown_left_ms = unit->info->attack.cooldown_ms;
        }
        return;
    }
    if (unit->z.point_shots >= TOUGH_SHOTS) { finish(unit); return; }
    if (unit->attack.cooldown_left_ms > 0 || unit->core.state_id != stand_states[type]) return;
    unit->core.angle = facing(ivec2_sub(unit->z.point, Z_AITopLeft(unit)));
    unit->z.point_throw = true;
    P_SetMobjState(unit, S_TOUGH_FIRE);
}

static void attack_tick(mobj_t *unit) {
    ivec2_t point = unit->z.point;
    if (!tough(unit) && unit->z.point_phase == PHASE_THROWING) {
        if (!unit->z.point_throw && !throwing(unit)) finish(unit);
        return;
    }
    if (!tough(unit) || !unit->z.point_throw) {
        if (!Z_PointAttackable(point) || (unit->team == 1 && own_building_at(point))) { finish(unit); return; }
        if (!Z_PointInRange(unit, point)) {
            if (P_HasMoveOrder(unit)) return;
            if (unit->z.point_phase == PHASE_APPROACH || !P_MoveUnitTo(&level, unit, pixel_goal(point))) {
                finish(unit); /* The path ended short of range, or none exists. */
                return;
            }
            unit->z.point_phase = PHASE_APPROACH;
            return;
        }
        if (P_HasMoveOrder(unit)) P_ClearMove(unit);
    }
    if (tough(unit)) { tough_tick(unit); return; }
    if (unit->z.grenades) { /* 0x753bb: the leader throws first, and then the order is over. */
        unit->z.point_phase = PHASE_THROWING;
        start_throw(unit, point);
        return;
    }
    if (!Z_SquadGrenades(unit)) { finish(unit); return; }
    mobj_t *carrier = pick_carrier(unit);
    if (carrier) start_throw(carrier, point);
}

void Z_PointTick(mobj_t *unit) {
    if (!Z_PointActive(unit)) return;
    if (!live(unit)) { Z_PointCancel(unit); unit->z.point_throw = false; return; }
    int type = Z_UnitType(unit);
    if (unit->z.point_throw && !throwing(unit) && !(tough(unit) && (unit->core.state_id == S_TOUGH_FIRE ||
                                                                    unit->core.state_id == S_TOUGH_SHOT)))
        unit->z.point_throw = false;
    if (type < 0 || live(unit->attack.target)) return;
    if (unit->z.point_mode == MODE_VEHICLE) {
        /* 0x764e8: in reach the vehicle stops and fires nothing; a path that
         * ends short of the reach ends the order. */
        if (Z_PointReach(unit, unit->z.point) || !P_HasMoveOrder(unit)) finish(unit);
    } else if (unit->z.point_mode == MODE_MOVE) {
        if (Z_PointMoveDone(unit, unit->z.point) || !P_HasMoveOrder(unit)) finish(unit);
    } else if (unit->z.point_mode == MODE_ATTACK) attack_tick(unit);
}
