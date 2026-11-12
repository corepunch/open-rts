#include "z_local.h"
#include "info.h"

/* Zod zobject.h:82 and :162: a waypoint has an action, target reference
 * and position. zserver.cpp:1889 copies the factory list to new units. */
static bool queueable(int order) {
    return order == TC_ORDER || order == TC_MOVE || order == TC_ATTACK || order == TC_REPAIR || order == TC_BOARD;
}

static bool fort_entry(const mobj_t *unit, const mobj_t *target, fixed2_t *entrance) {
    if (!target || target->type_id != MT_FORT || target->hp <= 0 || target->team == unit->team ||
        !(unit->traits & MF_MOBILE) || !Z_FortEntrance(target, entrance)) return false;
    if (unit->traits & MF_FLY) return true;
    int cls = P_MobjMoveClass(unit);
    return P_NavReachable(&level, cls, P_NavNodeAt(&level, cls, fixed3_xy(unit->core.position)),
                          P_NavNodeAt(&level, cls, *entrance));
}

static bool service_allowed(const mobj_t *unit, const mobj_t *target) {
    if (!target || target->remove || !Z_RepairVehicle(unit)) return false;
    bool wreck = unit->type_id == MT_UNIT_CRANE && target->hp <= 0 &&
                 (target->z.building || target->type_id == MT_BRIDGE);
    bool bay = target->type_id == MT_REPAIR && target->hp > 0 && unit->hp < unit->max_hp;
    if ((!wreck && !bay) || (target->type_id != MT_BRIDGE && target->team != unit->team)) return false;
    if (target->type_id == MT_BRIDGE) {
        const zmap_t *map = level.native_data;
        if (!map || !target->z.bridge || target->z.bridge > Z_BRIDGES) return false;
        int territory = Z_BridgeTerritory(map, target);
        if (territory < 0 || map->ai.territories[territory].owner != unit->team) return false;
    }
    return true;
}

static bool service(mobj_t *unit, mobj_t *target) {
    if (!service_allowed(unit, target)) return false;
    fixed2_t goal;
    return Z_ServiceApproach(unit, target, &goal) && Z_ServiceOrder(unit, target, goal);
}

static bool done(const mobj_t *unit) {
    const zroute_t *route = &unit->z.route;
    if (!route->active) return true;
    if (unit->z.board || unit->z.service || unit->waypoints.count) return false;
    if ((route->active == TC_ATTACK || route->active == TC_ORDER) && route->target) {
        const mobj_t *target = Z_ServiceTarget(route->target);
        if (target && target->hp > 0 && P_CanTarget(unit, target)) return false;
    }
    return !P_HasMoveOrder(unit) && !unit->movement.plan_pending;
}

static void append(zroute_t *route, int order, fixed3_t position, uint32_t target) {
    if (route->count < MAXWAYPOINTS)
        route->steps[route->count++] = (zroute_step_t){position, target, order};
}

static void issue(mobj_t *unit, const zroute_step_t *step) {
    ticcmd_t cmd = {.order = step->order, .position = step->position, .target = step->target,
                    .internal = true, .count = 1, .units = {unit->id}};
    const mobj_t *target = Z_ServiceTarget(step->target);
    if (cmd.order == TC_ORDER && target && P_CanTarget(unit, target)) cmd.order = TC_ATTACK;
    G_RunTiccmd(unit->owner, &cmd);
}

/* Wrecks are not selectable. A crane's ground click resolves a wreck from
 * its native footprint and retains its stable ID for queued execution. */
static mobj_t *clicked_wreck(const mobj_t *unit, fixed3_t position) {
    if (unit->type_id != MT_UNIT_CRANE) return NULL;
    const zmap_t *map = level.native_data;
    ivec2_t at = fixed2_cell(fixed2_scale(fixed3_xy(position), FIXED_FROM_INT(Z_TILE)));
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *target = (mobj_t *)th;
        if (target->remove || target->hp > 0) continue;
        irect_t bounds;
        if (target->type_id == MT_BRIDGE && target->z.bridge && target->z.bridge <= Z_BRIDGES && map)
            bounds = map->bridges[target->z.bridge - 1].bounds;
        else if (target->z.building && target->team == unit->team) {
            ivec2_t origin = Z_BuildingOrigin(target);
            bounds = (irect_t){origin.x, origin.y, target->info->footprint.w * Z_TILE, target->info->footprint.h * Z_TILE};
        } else continue;
        if (irect_contains(bounds, at)) return target;
    }
    return NULL;
}

void Z_RouteTick(mobj_t *unit) {
    zroute_t *route = &unit->z.route;
    if (!route->active && !route->count) return;
    if (route->owner != unit->owner || unit->hp <= 0 || unit->remove || route->count > MAXWAYPOINTS) {
        *route = (zroute_t){0};
        return;
    }
    if (Z_Producer(unit)) return;
    if ((route->active == TC_ATTACK || route->active == TC_ORDER) && route->target &&
        !unit->z.board && !unit->z.service) {
        mobj_t *target = Z_ServiceTarget(route->target);
        fixed2_t entrance;
        if (!target || target->hp <= 0 ||
            ((route->active == TC_ATTACK || target->type_id == MT_FORT) &&
             !P_CanTarget(unit, target) && !fort_entry(unit, target, &entrance))) {
            P_ClearMove(unit);
            unit->movement.order_arrived = true;
            unit->attack.target = NULL;
        }
    }
    if (!done(unit)) return;
    route->active = 0;
    route->target = 0;
    while (route->count) {
        zroute_step_t step = route->steps[0];
        memmove(route->steps, route->steps + 1, (--route->count) * sizeof(*route->steps));
        route->steps[route->count] = (zroute_step_t){0};
        mobj_t *target = Z_ServiceTarget(step.target);
        if (!queueable(step.order) || (step.target && !target) ||
            ((step.order == TC_ATTACK || step.order == TC_BOARD || step.order == TC_REPAIR) && !target) ||
            ((step.order == TC_ORDER || step.order == TC_MOVE) &&
             !L_Contains(&level, fixed_floor_int(step.position.x), fixed_floor_int(step.position.y))) ||
            (step.order == TC_ATTACK && target->hp <= 0) ||
            (step.order == TC_BOARD && !Z_Enterable(target)) ||
            (step.order == TC_REPAIR && !service_allowed(unit, target))) continue;
        fixed2_t goal;
        if ((step.order == TC_REPAIR && !Z_ServiceApproach(unit, target, &goal)) ||
            (step.order == TC_ATTACK && target->type_id == MT_FORT && !fort_entry(unit, target, &goal))) continue;
        if (step.order == TC_ORDER && target && P_CanTarget(unit, target)) step.order = TC_ATTACK;
        route->active = step.order;
        route->target = step.target;
        issue(unit, &step);
        return;
    }
}

void Z_RouteProduced(mobj_t *unit, const mobj_t *producer) {
    unit->z.route = producer->z.route;
    unit->z.route.active = 0;
    unit->z.route.target = 0;
    unit->z.route.owner = unit->owner;
    Z_RouteTick(unit);
}

bool Z_RouteCommand(int player, const ticcmd_t *cmd, mobj_t *const *units, int count) {
    if (cmd->internal) {
        mobj_t *target = Z_ServiceTarget(cmd->target);
        if ((cmd->order == TC_ATTACK || cmd->order == TC_ORDER) && target) {
            bool handled = false;
            for (int i = 0; i < count; ++i) {
                fixed2_t entrance;
                if (!fort_entry(units[i], target, &entrance)) continue;
                Z_CancelService(units[i]);
                units[i]->z.board = 0;
                units[i]->waypoints = (waypoints_t){0};
                units[i]->attack.target = NULL;
                P_MoveUnitTo(&level, units[i], entrance);
                handled = true;
            }
            if (handled) return true;
        }
        if (cmd->order == TC_REPAIR || cmd->order == TC_BOARD ||
            ((cmd->order == TC_ORDER || cmd->order == TC_ATTACK) && target)) {
            bool handled = false;
            for (int i = 0; i < count; ++i) handled |= service(units[i], target);
            if (handled || cmd->order == TC_REPAIR) return true;
            if (Z_Enterable(target)) {
                for (int i = 0; i < count; ++i) Z_CancelService(units[i]);
                if (Z_BoardOrder(units, count, target)) return true;
            }
            if (cmd->order == TC_BOARD) return true;
        }
        return false;
    }
    bool producers = false, mobiles = false;
    for (int i = 0; i < count; ++i) {
        producers |= Z_Producer(units[i]);
        mobiles |= (units[i]->traits & MF_MOBILE) != 0;
    }
    if (cmd->order == TC_STOP || cmd->order == TC_UNLOAD) {
        for (int i = 0; i < count; ++i) units[i]->z.route = (zroute_t){0};
        return false;
    }
    if (cmd->order == TC_ORDER && cmd->target && !Z_ServiceTarget(cmd->target)) return true;
    if (producers && (queueable(cmd->order) || cmd->order == TC_SET_RALLY ||
                      cmd->order == TC_PATH || cmd->order == TC_WAYPOINT)) {
        if (cmd->order != TC_PATH && !L_Contains(&level, fixed_floor_int(cmd->position.x), fixed_floor_int(cmd->position.y))) return true;
        if ((cmd->order == TC_ATTACK || cmd->order == TC_BOARD || cmd->order == TC_REPAIR) &&
            !Z_ServiceTarget(cmd->target)) return true;
        if (cmd->queued && cmd->order == TC_PATH && cmd->path.mode != WP_ONCE) return true;
        for (int i = 0; i < count; ++i) {
            if (!Z_Producer(units[i])) continue;
            zroute_t *route = &units[i]->z.route;
            bool add = cmd->queued || (cmd->order == TC_WAYPOINT && cmd->target);
            if (route->count > MAXWAYPOINTS || route->owner != player)
                *route = (zroute_t){.owner = player};
            if (add && cmd->order == TC_PATH && cmd->path.count > MAXWAYPOINTS - route->count) continue;
            if (!add) *route = (zroute_t){.owner = player};
            if (cmd->order == TC_PATH) {
                for (int p = 0; p < cmd->path.count; ++p)
                    append(route, TC_ORDER, fixed3_from_fixed2(fixed2_cell_center(cmd->path.points[p]), 0), 0);
            } else {
                int order = cmd->order == TC_SET_RALLY || cmd->order == TC_WAYPOINT ? TC_ORDER : cmd->order;
                append(route, order, cmd->position, cmd->order == TC_WAYPOINT ? 0 : cmd->target);
            }
        }
        if (!mobiles || cmd->order == TC_SET_RALLY) return true;
    }
    if (!queueable(cmd->order) && cmd->order != TC_PATH && cmd->order != TC_WAYPOINT) return false;
    if ((cmd->order == TC_MOVE || cmd->order == TC_ORDER || cmd->order == TC_WAYPOINT) &&
        !L_Contains(&level, fixed_floor_int(cmd->position.x), fixed_floor_int(cmd->position.y))) return true;
    if ((cmd->order == TC_ATTACK || cmd->order == TC_BOARD || cmd->order == TC_REPAIR) &&
        !Z_ServiceTarget(cmd->target)) return true;
    if (cmd->queued && cmd->order == TC_PATH && cmd->path.mode != WP_ONCE) return true;
    ticcmd_t batch = *cmd;
    batch.internal = true;
    batch.queued = false;
    batch.count = 0;
    for (int i = 0; i < count; ++i) {
        mobj_t *unit = units[i];
        if (!(unit->traits & MF_MOBILE)) continue;
        mobj_t *target = Z_ServiceTarget(cmd->target);
        fixed2_t entrance;
        if (cmd->order == TC_REPAIR && !service_allowed(unit, target)) continue;
        if (cmd->order == TC_BOARD && !(Z_Enterable(target) && Z_IsRobot(Z_UnitType(unit)))) continue;
        if (cmd->order == TC_ATTACK &&
            !P_CanTarget(unit, target) && !(Z_Enterable(target) && Z_IsRobot(Z_UnitType(unit))) &&
            !service_allowed(unit, target) && !fort_entry(unit, target, &entrance)) continue;
        zroute_t *route = &unit->z.route;
        if (route->count > MAXWAYPOINTS || route->owner != player) *route = (zroute_t){.owner = player};
        zroute_step_t step = {cmd->position, cmd->target, cmd->order};
        if (step.order == TC_ORDER && !step.target) {
            mobj_t *wreck = clicked_wreck(unit, step.position);
            if (wreck) { step.order = TC_REPAIR; step.target = wreck->id; }
        }
        if (step.order == TC_ORDER && target && P_CanTarget(unit, target)) step.order = TC_ATTACK;
        if ((step.order == TC_ORDER || step.order == TC_ATTACK) && target && service_allowed(unit, target))
            step.order = TC_REPAIR;
        if (step.order == TC_REPAIR && !service_allowed(unit, Z_ServiceTarget(step.target))) continue;
        bool busy = route->active || P_HasMoveOrder(unit) || unit->z.board || unit->z.service ||
                    P_CanTarget(unit, unit->attack.target);
        if (!(cmd->queued && busy) && step.order == TC_REPAIR) {
            fixed2_t goal;
            if (!Z_ServiceApproach(unit, Z_ServiceTarget(step.target), &goal)) continue;
        }
        if (cmd->queued && cmd->order == TC_PATH &&
            (route->active || P_HasMoveOrder(unit) || unit->z.board || unit->z.service ||
             P_CanTarget(unit, unit->attack.target))) {
            if (cmd->path.count > MAXWAYPOINTS - route->count) continue;
            if (!route->active) route->active = TC_ORDER;
            for (int p = 0; p < cmd->path.count; ++p)
                append(route, TC_ORDER, fixed3_from_fixed2(fixed2_cell_center(cmd->path.points[p]), 0), 0);
            continue;
        }
        if (cmd->queued && queueable(cmd->order) && busy) {
            if (!route->active) route->active = TC_ORDER;
            append(route, step.order, step.position, step.target);
            continue;
        }
        *route = (zroute_t){.active = step.order, .target = step.target, .owner = player};
        if ((cmd->order == TC_ORDER && !step.target) || cmd->order == TC_MOVE ||
            cmd->order == TC_PATH || cmd->order == TC_WAYPOINT) {
            batch.units[batch.count++] = unit->id;
            continue;
        }
        ticcmd_t immediate = *cmd;
        immediate.order = step.order;
        immediate.target = step.target;
        if (step.order == TC_ORDER && step.target && P_CanTarget(unit, Z_ServiceTarget(step.target))) immediate.order = TC_ATTACK;
        immediate.internal = true;
        immediate.queued = false;
        immediate.count = 1;
        immediate.units[0] = unit->id;
        G_RunTiccmd(player, &immediate);
    }
    if (batch.count) G_RunTiccmd(player, &batch);
    return true;
}
