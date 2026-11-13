#include "z_local.h"
#include "info.h"

enum { FACILITY_HZ = 17, CRANE_RATE = 120 >> 4, HOOK_CYCLE = 12,
       CRANE_STOW = Z_CRANE_POSES + HOOK_CYCLE };

static fixed2_t pixel_point(ivec2_t pixel) {
    return fixed2_scale(fixed2_from_cell(pixel), FIXED_ONE / Z_TILE);
}

static bool building(const mobj_t *mo) {
    return mo->type_id > MT_FORT && mo->type_id < MT_UNITS;
}

bool Z_RepairVehicle(const mobj_t *unit) {
    int type = Z_UnitType(unit);
    return type >= 0 && !Z_IsRobot(type) && unit->hp > 0 && !unit->remove && (unit->traits & MF_MOBILE);
}

/* A repair order keeps a destroyed object addressable by its stable ID. */
mobj_t *Z_ServiceTarget(uint32_t id) {
    if (!id) return NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->id == id && !mo->remove) return mo;
    }
    return NULL;
}

static fixed2_t service_goal(const mobj_t *target) {
    const zmap_t *map = level.native_data;
    if (target->type_id == MT_BRIDGE) {
        const zbridge_t *bridge = &map->bridges[target->z.bridge - 1];
        irect_t r = bridge->bounds;
        /* 0x9c9ee, then 0x9ca1d checks these four points for deck edge 0x2b. */
        ivec2_t centre = {r.x + r.w / 2, r.y + r.h / 2};
        ivec2_t origin = ivec2_add(centre, bridge->vertical ? (ivec2_t){-1,-8} : (ivec2_t){-8,-1});
        static const ivec2_t offsets[] = {{0,0}, {16,0}, {0,16}, {16,16}};
        for (int i = 0; i < 4; ++i) {
            ivec2_t at = ivec2_add(origin, offsets[i]);
            ivec2_t cell = {at.x / Z_TILE, at.y / Z_TILE};
            if (L_Contains(&level, cell.x, cell.y) && (map->terrain[cell.y * Z_MAP_STRIDE + cell.x] & 0x7f) == 0x2b)
                return pixel_point(at);
        }
        return pixel_point(origin);
    }
    /* ZED.EXE building-type bytes +0x34/+0x35, read by 0x9c4a9/0x9c962. */
    ivec2_t offset;
    switch (target->type_id) {
    case MT_TANK_FACTORY: case MT_ROBOT_FACTORY: offset = (ivec2_t){31,32}; break;
    case MT_RADAR: offset = (ivec2_t){31,16}; break;
    default: offset = (ivec2_t){31,28}; break;
    }
    return pixel_point(ivec2_add(Z_BuildingOrigin(target), offset));
}

void Z_CancelService(mobj_t *unit) {
    if (!unit->z.service) return;
    if (unit->z.service_work < 0 && unit->hp > 0) {
        unit->traits |= unit->info->traits & (MF_MOBILE | MF_ATTACK | MF_RENDERABLE | MF_SELECTABLE);
        unit->traits &= ~MF_NOBLOCKMAP;
    }
    unit->z.service = 0;
    unit->z.service_work = 0;
    unit->z.service_ticks = 0;
    unit->move_only = false;
    if (unit->core.state_id == S_CRANE_WORK && unit->hp > 0)
        P_SetMobjState(unit, S_CRANE_STND);
}

bool Z_ServiceApproach(const mobj_t *unit, const mobj_t *target, fixed2_t *goal) {
    *goal = service_goal(target);
    int cls = P_MobjMoveClass(unit);
    fixed2_t position = fixed3_xy(unit->core.position);
    ivec2_t from = P_NavNodeAt(&level, cls, position);
    ivec2_t wanted = P_NavNodeAt(&level, cls, *goal), near;
    if (P_NavReachable(&level, cls, from, wanted) && P_CheckPosition(&level, unit, *goal)) return true;
    if (target->type_id != MT_BRIDGE) {
        if (!P_NavNearestReachable(&level, cls, from, wanted, target->info->footprint.w, &near)) return false;
        *goal = P_NavNodePoint(&level, cls, near);
    } else {
        /* A collapsed deck can split the route. Use the nearer reachable
         * end when the native centre/edge approach lies on blocked terrain. */
        const zmap_t *map = level.native_data;
        const zbridge_t *bridge = &map->bridges[target->z.bridge - 1];
        irect_t r = bridge->bounds;
        ivec2_t bank = bridge->vertical ? (ivec2_t){r.x + r.w / 2,r.y} : (ivec2_t){r.x,r.y + r.h / 2};
        ivec2_t span = bridge->vertical ? (ivec2_t){0,r.h} : (ivec2_t){r.w,0};
        int64_t best = INT64_MAX;
        for (int side = 0; side < 2; ++side) {
            fixed2_t end = pixel_point(ivec2_add(bank, ivec2_scale(span, side)));
            ivec2_t node = P_NavNodeAt(&level, cls, end);
            if (!P_NavNearestReachable(&level, cls, from, node, 2, &near)) continue;
            fixed2_t at = P_NavNodePoint(&level, cls, near);
            int64_t d = fixed2_distance_squared64(position, at);
            if (d < best) { *goal = at; best = d; }
        }
        if (best == INT64_MAX) return false;
    }
    return true;
}

bool Z_ServiceOrder(mobj_t *unit, const mobj_t *target, fixed2_t goal) {
    if (!Z_RepairVehicle(unit) || !target || target->remove) return false;
    Z_CancelService(unit);
    if (!P_MoveUnitTo(&level, unit, goal)) return false;
    unit->waypoints = (waypoints_t){0};
    unit->attack.target = NULL;
    unit->z.service = target->id;
    unit->move_only = true;
    return true;
}

static bool bay_busy(const mobj_t *unit, const mobj_t *facility) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *other = (const mobj_t *)th;
        if (th->function == P_MobjThinker && other != unit && !other->remove && other->hp > 0 &&
            other->z.service == facility->id && other->z.service_work < 0) return true;
    }
    return false;
}

static void leave_bay(mobj_t *unit, const mobj_t *facility) {
    Z_CancelService(unit);
    if (!facility) return;
    /* 0x3feda sends the vehicle south of its bay, adding (8,66) to
     * its native position at the entrance (+31,+28). */
    fixed2_t exit = pixel_point(ivec2_add(Z_BuildingOrigin(facility), (ivec2_t){39,94}));
    P_MoveUnitTo(&level, unit, exit);
}

static void service_vehicle(mobj_t *unit, const mobj_t *facility) {
    const zmap_t *map = level.native_data;
    if (!unit->z.service_work) {
        if (P_HasMoveOrder(unit) || !unit->movement.order_arrived || bay_busy(unit, facility)) return;
        /* 0x7a065 and 0x41ae1 initialize the remaining repair stock. */
        int stock = (unit->max_hp - unit->hp) / 20;
        unit->z.service_work = -((1 + (stock > 0 ? stock : 1)) * 880 / map->production_speed);
        Z_RepairBody(unit);
        unit->traits &= ~(MF_MOBILE | MF_ATTACK | MF_RENDERABLE | MF_SELECTABLE);
        unit->traits |= MF_NOBLOCKMAP;
        P_MobjSetSelected(unit, false);
        return;
    }
    int total;
    int held = Z_TerritoriesHeld(facility->team, &total);
    int rate = Z_ProductionRate(facility, held, total);
    for (int steps = Z_NativeSteps((int64_t)leveltime - 1, FACILITY_HZ); rate && steps-- > 0;) {
        if (++unit->z.service_ticks == 17) {
            unit->z.service_ticks = 0;
            int seconds = -unit->z.service_work / rate / 17;
            if (seconds && unit->hp < unit->max_hp) {
                int hp = unit->hp + 1 + (unit->max_hp - unit->hp) / seconds;
                unit->hp = hp < unit->max_hp ? hp : unit->max_hp;
            }
        }
        if (-unit->z.service_work <= rate) {
            unit->hp = unit->max_hp;
            leave_bay(unit, facility);
            break;
        }
        unit->z.service_work += rate;
    }
}

static int crane_site(const mobj_t *unit, bool bridge) {
    int dir = angle_to_direction(unit->core.angle, 8, ANG90, true);
    return bridge ? Z_CRANE_SOUTH + dir / 2 : Z_CRANE_BUILDING;
}

static void finish_crane(mobj_t *unit, mobj_t *target, int site) {
    ivec2_t exit = ivec2_add(Z_AITopLeft(unit), (ivec2_t){8,8});
    if (site != Z_CRANE_BUILDING) {
        const zmap_t *map = level.native_data;
        irect_t bounds = map->bridges[target->z.bridge - 1].bounds;
        switch (site) {
        case Z_CRANE_SOUTH: exit.y = bounds.y + bounds.h + 16; break;
        case Z_CRANE_WEST: exit.x = bounds.x - 48; break;
        case Z_CRANE_NORTH: exit.y = bounds.y - 48; break;
        case Z_CRANE_EAST: exit.x = bounds.x + bounds.w + 48; break;
        }
        Z_RebuildBridge(target);
    } else {
        target->z.service_work = 0;
        if (target->hp <= 0 && target->z.building) {
            Z_StartBuildingRebuild(target);
        } else {
            target->hp = target->max_hp;
            target->traits |= MF_SELECTABLE;
            while (target->production && target->production->queue_count)
                G_RemoveProductionAt(target, 0);
            P_SetMobjState(target, mobjinfo[target->type_id].spawnstate);
        }
        exit.y = Z_BuildingOrigin(target).y + (target->info->footprint.h + 1) * Z_TILE;
    }
    Z_CancelService(unit);
    P_MoveUnitTo(&level, unit, pixel_point(exit));
}

/* 0x24556: flicker the two deployed sprite runs. Use a deterministic
 * cosmetic seed so the work animation does not advance the AI's RNG. */
static void crane_variant(mobj_t *unit, int64_t frame) {
    int high = unit->z.service_ticks >> 4, low = unit->z.service_ticks & 0xf;
    uint16_t seed = (uint16_t)((uint64_t)frame * 0x9e37 + unit->id);
    if ((Z_NextRandom(&seed) & 0x3f) >= 4) high = (high ^ 1) << 4;
    if (!(frame & 7)) {
        low = low >= 4 ? 0 : low + 1;
        if (low == 3 && (Z_NextRandom(&seed) & 7)) low = 1;
        if (low == 4 && (Z_NextRandom(&seed) & 1)) low = 5;
    }
    unit->z.service_ticks = (uint8_t)(high | low);
}

static void crane_work(mobj_t *unit, mobj_t *target, bool bridge) {
    int site = crane_site(unit, bridge);
    int *step = &unit->z.service_work;
    int64_t last = ((int64_t)leveltime + 1) * Z_NATIVE_HZ / RTS_TICRATE;
    int picture = 0;
    for (int64_t frame = last - Z_NativeSteps(leveltime, Z_NATIVE_HZ) + 1; frame <= last; ++frame) {
        if (*step < Z_CRANE_POSES) {
            picture = (*step)++;
        } else if (*step < CRANE_STOW) {
            /* 0x24615 rocks the hook through 1..7..2. Work starts at
             * the deck/ruin, with a collapsed bridge's native HP at -1. */
            int phase = *step - Z_CRANE_POSES;
            int hook = 1 + (phase <= HOOK_CYCLE / 2 ? phase : HOOK_CYCLE - phase);
            int flicker = unit->z.service_ticks;
            int variant = 6 * (flicker >> 4) + (flicker & 0xf);
            picture = Z_CRANE_WORK_FRAME + hook * Z_CRANE_VARIANTS + variant;
            crane_variant(unit, frame);
            if (!target->z.service_work && bridge) target->z.service_work = -1;
            target->z.service_work += CRANE_RATE;
            *step = target->z.service_work >= target->max_hp ?
                CRANE_STOW : Z_CRANE_POSES + (phase + 1) % HOOK_CYCLE;
        } else {
            /* 0x2452c retracts the work layers every other native frame. */
            int pose = Z_CRANE_POSES - 1 - (*step - CRANE_STOW);
            if (pose < 0) {
                finish_crane(unit, target, site);
                return;
            }
            picture = Z_CRANE_POSES + pose;
            if (!(frame & 1)) ++*step;
        }
    }
    P_SetMobjStateFrame(unit, S_CRANE_WORK, site * Z_CRANE_SITE_FRAMES + picture);
}

void Z_ServiceTick(mobj_t *unit) {
    mobj_t *target = Z_ServiceTarget(unit->z.service);
    bool bridge = target && target->type_id == MT_BRIDGE;
    bool rebuild = target && unit->type_id == MT_UNIT_CRANE && (bridge || building(target)) && target->hp == 0;
    bool facility = target && target->type_id == MT_REPAIR && target->hp > 0;
    if (bridge) {
        const zmap_t *map = level.native_data;
        int t = Z_BridgeTerritory(map, target);
        rebuild = rebuild && !target->z.service_ticks && t >= 0 && map->ai.territories[t].owner == unit->team;
    }
    if ((!rebuild && !facility) || (!bridge && target->team != unit->team) ||
        (!P_HasMoveOrder(unit) && !unit->movement.order_arrived)) {
        P_ClearMove(unit);
        if (unit->z.service_work < 0) leave_bay(unit, target);
        else Z_CancelService(unit);
        return;
    }
    unit->attack.target = NULL;
    if (facility) {
        service_vehicle(unit, target);
        return;
    }
    if (P_HasMoveOrder(unit)) return;
    crane_work(unit, target, bridge);
}
