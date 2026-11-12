#include "z_local.h"
#include "info.h"

/* ZED.EXE 0x1047ca: nine sectors in each native search direction. */
static const int16_t sectors[8][9] = {
    {0,-1,1,-17,-16,-15,-32,-33,-31},
    {0,1,2,-16,-15,-14,-32,-31,-30},
    {0,-16,16,1,-15,17,2,-14,18},
    {0,1,2,16,17,18,32,33,34},
    {0,-1,1,17,16,15,32,33,31},
    {0,-1,-2,16,15,14,32,31,30},
    {0,-16,16,-1,-17,15,-2,-18,14},
    {0,-1,-2,-16,-17,-18,-32,-33,-34},
};

/* 0x11714b: native terrain heights. */
static const uint8_t heights[67] = {
    0,3,4,5,6,7,8,0,0,0,0,1,2,3,4,5,
    1,1,1,1,1,1,0,0,1,1,1,1,1,0,0,1,
    1,1,1,0,0,1,0,1,0,0,1,0,1,0,1,0,
    0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0
};

enum { OBJECT_SLOTS = 200 };

static bool object(const mobj_t *mo) {
    return mo && !mo->remove && (mo->type_id == MT_FLAG ||
           (mo->type_id > MT_NULL && mo->type_id < MT_FORT) ||
           Z_Enterable(mo));
}

static bool origin(const zmap_t *map, const mobj_t *mo, ivec2_t *out) {
    if (mo->type_id == MT_FLAG) {
        if (!mo->z.territory || mo->z.territory > map->territory_count) return false;
        *out = ivec2_scale(map->territories[mo->z.territory - 1].flag, Z_TILE);
        return true;
    }
    int type = mo->z.object ? mo->z.object : mo->type_id;
    if (!Z_Enterable(mo) && (type <= MT_NULL || type >= MT_FORT)) return false;
    isize2_t size;
    int sprite = Z_Enterable(mo) ? states[mo->core.state_id].frame : states[mobjinfo[type].spawnstate].frame;
    if (!Z_SpriteSize(map, sprite, &size)) return false;
    *out = ivec2_sub(fixed2_cell(fixed2_scale(fixed3_xy(mo->core.position), FIXED_FROM_INT(Z_TILE))),
                      (ivec2_t){size.w / 2, size.h / 2});
    return true;
}

static bool visible(const mobj_t *mo, int team) {
    if (team < 0 || team >= 4) return false;
    if (mo->type_id == MT_FLAG) return mo->team != team;
    int type = mo->z.object ? mo->z.object - 1 : mo->type_id - 1;
    if (type == 9 || type == 10 || Z_Enterable(mo)) return true;
    if (type == 11 || type == 12 || type == 17) return false;
    return team == 0;
}

static bool cone(int dir, ivec2_t d) {
    switch (dir) {
    case 0: return abs(d.x) <= -d.y;
    case 1: return d.x >= 0 && d.y < 0;
    case 2: return d.x >= abs(d.y);
    case 3: return d.x >= 0 && d.y >= 0;
    case 4: return abs(d.x) <= d.y;
    case 5: return d.x < 0 && d.y >= 0;
    case 6: return -d.x >= abs(d.y);
    default: return d.x < 0 && d.y < 0;
    }
}

static int height(const zmap_t *map, int x, int y) {
    if (x < 0 || y < 0 || x >= Z_MAP_STRIDE || y >= Z_MAP_STRIDE) return 255;
    unsigned terrain = map->terrain[y * Z_MAP_STRIDE + x] & 0x7f;
    return terrain < sizeof(heights) ? heights[terrain] : 255;
}

/* 0x6e491/0x6e64b: the target must be at height zero. Trace the same
 * three-axis integer line, swapping endpoints when x is not increasing. */
static bool line_clear(const zmap_t *map, const mobj_t *unit, ivec2_t target) {
    ivec2_t source = Z_AITopLeft(unit);
    int extra = Z_IsRobot(Z_UnitType(unit)) ? 0 : 8;
    int x0 = (source.x + 8 + extra) / Z_TILE, y0 = (source.y + 8 + extra) / Z_TILE;
    int x1 = (target.x + 8) / Z_TILE, y1 = (target.y + 8) / Z_TILE;
    int z0 = height(map, x0, y0), z1 = height(map, x1, y1);
    if (z0 == 255 || z1 != 0) return false;
    if (z0) ++z0;
    if (x1 <= x0) {
        int t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
        t = z0; z0 = z1; z1 = t;
    }
    int dx = x1 - x0, dy = abs(y1 - y0), dz = abs(z1 - z0);
    int sy = y1 < y0 ? -1 : 1, sz = z1 < z0 ? -1 : 1;
    int steps = dx > dy ? dx : dy;
    if (dz > steps) steps = dz;
    if (!steps) return true;
    int ex = steps / 2, ey = steps / 2, ez = steps / 2;
    for (int i = 0; i < steps; ++i) {
        ex -= dx; if (ex < 0) { ex += steps; ++x0; }
        ey -= dy; if (ey < 0) { ey += steps; y0 += sy; }
        ez -= dz; if (ez < 0) { ez += steps; z0 += sz; }
        if (height(map, x0, y0) > z0) return false;
    }
    return true;
}

/* The native sector lists are rebuilt by ascending reusable object slot.
 * This is a temporary borrowed view of the actual thinker-owned objects. */
static void objects(mobj_t **slots) {
    bool used[OBJECT_SLOTS] = {false};
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *mo = (mobj_t *)th;
        if (!object(mo) || !mo->z.detour.slot || mo->z.detour.slot > OBJECT_SLOTS) continue;
        used[mo->z.detour.slot - 1] = true;
        slots[mo->z.detour.slot - 1] = mo;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *mo = (mobj_t *)th;
        if (!object(mo) || mo->z.detour.slot) continue;
        int slot = 0;
        while (slot < OBJECT_SLOTS && used[slot]) ++slot;
        if (slot == OBJECT_SLOTS) break;
        used[slot] = true;
        slots[slot] = mo;
        mo->z.detour.slot = (uint8_t)(slot + 1);
    }
}

static mobj_t *candidate(const zmap_t *map, const mobj_t *unit, mobj_t *const *slots) {
    ivec2_t at = Z_AITopLeft(unit);
    if (at.x < 0 || at.y < 0) return NULL;
    int sector = (at.x >> 7) + 16 * (at.y >> 7);
    int dir = angle_to_direction(unit->core.angle, 8, ANG90, true);
    mobj_t *best = NULL;
    int distance = 255;
    for (int i = 0; i < 9; ++i) {
        int s = sector + sectors[dir][i], count = 0;
        if (s < 0 || s >= 256) continue;
        for (int j = 0; j < OBJECT_SLOTS; ++j) {
            mobj_t *mo = slots[j];
            ivec2_t pos;
            if (!mo || !origin(map, mo, &pos) || pos.x < 0 || pos.y < 0 ||
                (pos.x >> 7) + 16 * (pos.y >> 7) != s) continue;
            if (++count > 32) break;
            ivec2_t delta = ivec2_sub(pos, at);
            if (!visible(mo, unit->team) || !cone(dir, delta)) continue;
            int dx = (int8_t)(delta.x >> 3), dy = (int8_t)(delta.y >> 3);
            int index = dx * dx + dy * dy;
            int rank = index < 1024 ? map->square_roots.bytes[index] : 48;
            if (rank > distance || !line_clear(map, unit, pos)) continue;
            distance = rank;
            best = mo;
        }
    }
    if (Z_UnitType(unit) < 0 || !unit->info ||
        distance > (int)((int64_t)unit->info->attack.range * Z_TILE / (8 * FIXED_ONE))) return NULL;
    return best;
}

/* 0x78bfb / 0x64473: a leader with a flag as its explicit destination
 * does not interrupt that command for a different object. */
static bool flag_destination(const zmap_t *map, const mobj_t *unit) {
    if (unit->z.squad_slot != UINT8_MAX) return false;
    ivec2_t goal = fixed2_cell(fixed2_scale(unit->movement.goal, FIXED_FROM_INT(Z_TILE)));
    for (int i = 0; i < map->territory_count; ++i) {
        const zterritory_t *t = &map->territories[i];
        if (t->options & 3) continue;
        ivec2_t pos = ivec2_add(ivec2_scale(t->flag, Z_TILE), (ivec2_t){4,8});
        if (abs(goal.x - pos.x) <= 8 && abs(goal.y - pos.y) <= 8) return true;
    }
    return false;
}

bool Z_CrateEligible(const mobj_t *unit, const mobj_t *target) {
    int type = Z_UnitType(unit);
    if ((target->type_id != MT_GRENADES && target->type_id != MT_GRENADES_2) ||
        (type != MT_UNIT_GRUNT - MT_UNITS && type != MT_UNIT_PYRO - MT_UNITS &&
         type != MT_UNIT_LASER - MT_UNITS)) return false;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *mo = (const mobj_t *)th;
        if (mo == unit || (unit->z.squad && mo->z.squad == unit->z.squad && mo->team == unit->team))
            if (!mo->remove && mo->hp > 0 && mo->z.grenades) return false;
    }
    return true;
}

/* Object-coordinate offsets from 0x6110a..0x61232. The native path node
 * stores a centre, and 0x7bd1f subtracts (8,8) before moving. */
static bool destination(const zmap_t *map, const mobj_t *unit, const mobj_t *target, fixed2_t *goal) {
    ivec2_t at;
    if (!origin(map, target, &at)) return false;
    if (target->type_id == MT_FLAG) return Z_UnitCentre(map, Z_UnitType(unit), at, goal);
    ivec2_t offset;
    if (Z_CrateEligible(unit, target)) offset = (ivec2_t){0,0};
    else if (Z_IsRobot(Z_UnitType(unit)) && Z_Enterable(target)) {
        int sprite = states[target->core.state_id].frame;
        if (sprite == 0x1435) return false; /* APC requires an explicit target at 0x611a7. */
        offset = sprite == 0x669 || sprite == 0x679 || sprite == 0x1685 ?
                 (ivec2_t){12,12} : (ivec2_t){8,-4};
    } else return false;
    return Z_UnitCentre(map, Z_UnitType(unit), ivec2_add(at, ivec2_sub(offset, (ivec2_t){8,8})), goal);
}

static void resume(mobj_t *unit) {
    fixed2_t goal = unit->z.detour.resume;
    bool resume_order = unit->z.detour.phase != Z_DETOUR_CHEER || unit->z.squad_slot == UINT8_MAX;
    Z_DetourCancel(unit);
    if (resume_order) P_MoveUnitTo(&level, unit, goal);
    else {
        unit->z.route.active = 0;
        unit->z.route.target = 0;
        mobj_t *leader = Z_SquadLeader(unit);
        unit->z.formation_rejoin = unit->z.squad_slot && leader && leader != unit;
    }
}

void Z_DetourCancel(mobj_t *unit) {
    if (!unit || !unit->z.detour.phase) return;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *target = (mobj_t *)th;
        if (target->id == unit->z.detour.target && target->z.detour.claimant == unit->id) {
            target->z.detour.claimant = 0;
            break;
        }
    }
    unit->z.detour.target = 0;
    unit->z.detour.phase = 0;
}

void Z_DetourCaptured(mobj_t *unit, mobj_t *flag) {
    if (!unit || unit->z.detour.target != flag->id || unit->z.detour.phase != Z_DETOUR_TRAVEL) return;
    flag->z.detour.claimant = 0;
    flag->z.detour.cooldown = (85 * RTS_TICRATE + Z_NATIVE_HZ - 1) / Z_NATIVE_HZ;
    unit->z.detour.phase = Z_DETOUR_CHEER;
}

void Z_DetourTicker(void) {
    const zmap_t *map = level.native_data;
    if (!map || !map->square_roots.bytes || map->square_roots.size < 1024) return;
    mobj_t *slots[OBJECT_SLOTS] = {0};
    objects(slots);
    for (int i = 0; i < OBJECT_SLOTS; ++i) {
        mobj_t *mo = slots[i];
        if (!mo) continue;
        if (mo->z.detour.cooldown) --mo->z.detour.cooldown;
        mobj_t *claimant = P_MobjById(mo->z.detour.claimant);
        if (mo->z.detour.claimant && (!claimant || claimant->hp <= 0 || claimant->remove ||
            claimant->z.detour.target != mo->id)) mo->z.detour.claimant = 0;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        zdetour_t *d = &unit->z.detour;
        if (unit->hp <= 0 || unit->remove) {
            Z_DetourCancel(unit);
            continue;
        }
        if (d->phase == Z_DETOUR_CHEER) {
            int t = Z_UnitType(unit);
            if (t >= 0 && unit->core.state_id == mobjinfo[unit->type_id].spawnstate) {
                resume(unit);
            }
            continue;
        }
        if (d->phase) {
            mobj_t *target = P_MobjById(d->target);
            if (!target || target->remove || target->hp <= 0 || target->z.detour.claimant != unit->id ||
                (target->type_id == MT_FLAG && target->team == unit->team) ||
                (!P_HasMoveOrder(unit) && !unit->movement.plan_pending)) resume(unit);
            else if (Z_Enterable(target)) Z_DetourBoard(unit, target);
            continue;
        }
        mobj_t *leader = Z_SquadLeader(unit);
        const zroute_t *route = leader ? &leader->z.route : &unit->z.route;
        if (route->active != TC_ORDER || route->target ||
            !(unit->traits & MF_MOBILE) || unit->team >= Z_NEUTRAL || !P_HasMoveOrder(unit)) continue;
        mobj_t *target = candidate(map, unit, slots);
        if (!target || target->z.detour.claimant || target->z.detour.cooldown ||
            flag_destination(map, unit)) continue;
        fixed2_t goal;
        if (!destination(map, unit, target, &goal)) continue;
        d->resume = unit->movement.goal;
        if (!P_MoveUnitTo(&level, unit, goal)) continue;
        d->target = target->id;
        d->phase = Z_DETOUR_TRAVEL;
        target->z.detour.claimant = unit->id;
    }
}
