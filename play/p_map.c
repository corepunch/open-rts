#define _DEFAULT_SOURCE
#include "engine.h"
#include "p_nav.h"

static uint32_t next_move_order_id(void) {
    if (++level.next_move_order_id == 0) ++level.next_move_order_id;
    return level.next_move_order_id;
}

int L_Index(const level_t *map, int x, int y) {
#ifdef RTS_LEVEL_COLUMN_MAJOR
    return x * map->height + y;
#else
    return y * map->width + x;
#endif
}

ivec2_t L_Cell(const level_t *map, int index) {
#ifdef RTS_LEVEL_COLUMN_MAJOR
    return (ivec2_t){index / map->height, index % map->height};
#else
    return (ivec2_t){index % map->width, index / map->width};
#endif
}

bool L_Contains(const level_t *map, int x, int y) {
    return x >= 0 && y >= 0 && x < map->width && y < map->height;
}

bool L_IsWalkable(const level_t *map, int x, int y) {
    return L_Contains(map, x, y) && (!map->blocked || map->blocked[L_Index(map, x, y)] == 0) &&
        (!map->cell_solid || !map->cell_solid[L_Index(map, x, y)]);
}

/* Speed percentage for a movement class at a cell (0 = impassable). Class 0 is the
 * plain blocked[] grid; other classes read the game's terrain speed tables. */
int L_MoveSpeed(const level_t *map, int move_class, int x, int y) {
    if (!L_Contains(map, x, y)) return 0;
    int index = L_Index(map, x, y);
    if (move_class <= 0 || !map->speeds || move_class >= map->speeds->class_count)
        return L_IsWalkable(map, x, y) ? 100 : 0;
    if (map->cell_solid && map->cell_solid[index]) return 0;
    int effect = map->cell_effect ? map->cell_effect[index] : 255;
    if (effect < 8) return map->speeds->overlay[move_class][effect];
    return map->speeds->terrain[move_class][(map->cell_terrain ? map->cell_terrain[index] : 15) & 15];
}

fixed_t P_MobjRadius(const mobj_t *unit) {
    if (unit && unit->radius > FIXED_LIT(0.05)) return unit->radius;
    return FIXED_LIT(0.42);
}

/* Squared distance (32.32) from a point to the closed unit square of a cell. */
static int64_t cell_distance_squared(fixed2_t position, ivec2_t cell) {
    fixed_t x0 = FIXED_FROM_INT(cell.x), y0 = FIXED_FROM_INT(cell.y);
    fixed2_t closest = { fixed_max(x0, fixed_min(position.x, x0 + FIXED_ONE)),
                         fixed_max(y0, fixed_min(position.y, y0 + FIXED_ONE)) };
    return fixed2_distance_squared64(position, closest);
}

static bool map_circle_walkable(const level_t *map, int cls, fixed2_t at, fixed_t radius,
                                const fixed2_t *from) {
    if (!map) return true;
    if (radius < FIXED_LIT(0.01)) radius = FIXED_LIT(0.01);
    if (at.x - radius < 0 || at.y - radius < 0 ||
        at.x + radius > FIXED_FROM_INT(map->width) || at.y + radius > FIXED_FROM_INT(map->height)) {
        return false;
    }

    int min_x = fixed_floor_int(at.x - radius);
    int max_x = fixed_floor_int(at.x + radius);
    int min_y = fixed_floor_int(at.y - radius);
    int max_y = fixed_floor_int(at.y + radius);
    int64_t radius2 = fixed_sq64(radius) - FIXED_LIT_64(0.0001); /* 32.32 */
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            if (L_MoveSpeed(map, cls, x, y) > 0) continue;
            ivec2_t cell = {x, y};
            int64_t distance = cell_distance_squared(at, cell);
            if (distance >= radius2) continue;
            /* An authored spawn can overlap terrain. Permit escape from
             * that overlap, but never enter or deepen another obstruction. */
            if (from) {
                int64_t previous = cell_distance_squared(*from, cell);
                if (previous < radius2 && distance >= previous) continue;
            }
            return false;
        }
    }
    return true;
}

bool P_MapCircleWalkable(const level_t *map, int move_class, fixed2_t at, fixed_t radius,
                         const fixed2_t *from) {
    return map_circle_walkable(map, move_class, at, radius, from);
}

bool P_CheckPosition(const level_t *map, const mobj_t *unit, fixed2_t at) {
    return map_circle_walkable(map, P_MobjMoveClass(unit), at, P_MobjRadius(unit), NULL);
}

bool P_TryMove(mobj_t *unit, fixed3_t position) {
    fixed2_t from = fixed3_xy(unit->core.position);
    fixed2_t to = fixed3_xy(position);
    if (!(unit->traits & MF_FLY) &&
        !map_circle_walkable(&level, P_MobjMoveClass(unit), to, P_MobjRadius(unit), &from)) return false;
    if (!(unit->traits & MF_FLY)) {
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            const mobj_t *other = (const mobj_t *)th;
            if (other == unit || other->remove || other->hp <= 0 || !P_HarvesterDocked(other)) continue;
            fixed_t radius = P_MobjRadius(unit) + P_MobjRadius(other);
            fixed2_t position = fixed3_xy(other->core.position);
            if (fixed2_distance_squared64(to, position) < fixed_sq64(radius) &&
                fixed2_distance_squared64(to, position) < fixed2_distance_squared64(from, position))
                return false;
        }
    }
    unit->core.momentum = fixed3_planar_displacement(unit->core.position, position);
    unit->core.position = position;
    return true;
}

static bool position_overlaps_reserved_goal(mobj_t *const *units, int unit_count, int self_index,
                                            fixed2_t at, fixed_t radius, uint32_t order_id) {
    if (!units || order_id == 0) return false;
    for (int i = 0; i < unit_count; ++i) {
        if (i == self_index) continue;
        const mobj_t *other = units[i];
        if (other->remove || other->hp <= 0 || other->movement.order_id != order_id) continue;
        fixed_t min_dist = radius + P_MobjRadius(other);
        if (fixed2_distance_squared64(other->movement.goal, at) < fixed_sq64(min_dist)) return true;
    }
    return false;
}

void P_ClampToLevel(const level_t *map, mobj_t *unit) {
    if (!map || !unit || map->width <= 0 || map->height <= 0) return;
    fixed2_t position = fixed3_xy(unit->core.position);
    fixed_t r = P_MobjRadius(unit);
    fixed_t min_x = r;
    fixed_t min_y = r;
    fixed_t max_x = FIXED_FROM_INT(map->width) - r;
    fixed_t max_y = FIXED_FROM_INT(map->height) - r;
    if (max_x < min_x) max_x = min_x = FIXED_FROM_INT(map->width) / 2;
    if (max_y < min_y) max_y = min_y = FIXED_FROM_INT(map->height) / 2;
    if (position.x < min_x) position.x = min_x;
    if (position.y < min_y) position.y = min_y;
    if (position.x > max_x) position.x = max_x;
    if (position.y > max_y) position.y = max_y;
    unit->core.position = fixed3_with_xy(unit->core.position, position);
}

static bool find_nearest_walkable_cell(const level_t *map, int cls, cell_t wanted, int radius, cell_t *out) {
    if (!map || !out) return false;
    if (L_MoveSpeed(map, cls, wanted.x, wanted.y) > 0) {
        *out = wanted;
        return true;
    }
    int best_h = 1000000;
    bool found = false;
    cell_t best = wanted;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            cell_t c = { wanted.x + dx, wanted.y + dy };
            if (L_MoveSpeed(map, cls, c.x, c.y) <= 0) continue;
            int h = dx * dx + dy * dy;
            if (h < best_h) {
                best_h = h;
                best = c;
                found = true;
            }
        }
    }
    if (!found) return false;
    *out = best;
    return true;
}

/* Walkable spot nearest `wanted`: the point itself, else the closest cell
 * centre within search_radius cells. With units != NULL, spots inside another
 * member's reserved goal (same order) are rejected too. */
static bool find_nearest_position(const level_t *map, int cls, mobj_t *const *units, int unit_count,
                                  int self_index, uint32_t order_id, fixed2_t wanted,
                                  fixed_t unit_radius, int search_radius, fixed2_t *out) {
    if (!out) return false;
    if (map_circle_walkable(map, cls, wanted, unit_radius, NULL) &&
        !position_overlaps_reserved_goal(units, unit_count, self_index, wanted, unit_radius, order_id)) {
        *out = wanted;
        return true;
    }
    cell_t cell = fixed2_cell(wanted);
    int64_t best_d2 = INT64_MAX;
    bool found = false;
    fixed2_t best = wanted;
    for (int dy = -search_radius; dy <= search_radius; ++dy) {
        for (int dx = -search_radius; dx <= search_radius; ++dx) {
            fixed2_t at = fixed2_cell_center((ivec2_t){ cell.x + dx, cell.y + dy });
            if (!map_circle_walkable(map, cls, at, unit_radius, NULL)) continue;
            if (position_overlaps_reserved_goal(units, unit_count, self_index, at, unit_radius, order_id))
                continue;
            int64_t d2 = fixed2_distance_squared64(at, wanted);
            if (d2 < best_d2) {
                best_d2 = d2;
                best = at;
                found = true;
            }
        }
    }
    if (!found) return false;
    *out = best;
    return true;
}

/* ---- Planning pipeline -------------------------------------------------
 * Orders are accepted immediately but planned under a per-tic work budget
 * (measured in A* expansions, so every peer slices identically). A unit whose
 * plan does not fit waits in a FIFO and keeps its order; it starts moving on the
 * tic its plan completes. Group orders search once and derive every follower's
 * route from the leader's, falling back to their own search only when they
 * cannot see the shared route. */
enum { PLAN_BUDGET_EXPANSIONS = 40000, SHARE_MIN_GROUP = 3 };

static uint64_t budget_base;
static uint32_t budget_tic = UINT32_MAX;
static uint32_t plan_sequence;

void P_NavBeginTick(void) {
    budget_base = P_NavStats().expansions;
    budget_tic = (uint32_t)leveltime;
}

static bool over_budget(void) {
    if (budget_tic != (uint32_t)leveltime) P_NavBeginTick(); /* Orders issued between tics. */
    return P_NavStats().expansions - budget_base > PLAN_BUDGET_EXPANSIONS;
}

uint8_t *P_IdleBlockers(const level_t *map, const mobj_t *exclude, uint32_t order_id) {
    uint8_t *cells = NULL;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *other = (const mobj_t *)th;
        if (other == exclude || other->remove || other->hp <= 0 ||
            (other->traits & (MF_MOBILE | MF_FLY)) != MF_MOBILE || P_HasMoveOrder(other) ||
            P_HarvesterSharingVent(other) ||
            (order_id && other->movement.order_id == order_id)) continue;
        ivec2_t cell = fixed2_cell(fixed3_xy(other->core.position));
        if (!L_Contains(map, cell.x, cell.y)) continue;
        if (!cells && !(cells = calloc((size_t)map->width * map->height, 1))) return NULL;
        cells[L_Index(map, cell.x, cell.y)] = 1;
    }
    return cells;
}

static void adopt_route(mobj_t *unit, const navpath_t *path) {
    unit->movement.path = *path;
    unit->movement.goal = path->goal;
    unit->movement.stuck_tics = unit->movement.replans = 0;
    unit->movement.plan_pending = false;
    unit->movement.order_arrived = false;
    unit->core.momentum = fixed3_zero();
}

static void defer_route(mobj_t *unit, fixed2_t goal) {
    unit->movement.path = (navpath_t){0};
    unit->movement.goal = goal;
    unit->movement.stuck_tics = unit->movement.replans = 0;
    unit->movement.plan_pending = true;
    unit->movement.plan_seq = ++plan_sequence;
    unit->movement.order_arrived = false;
    unit->core.momentum = fixed3_zero();
}

/* Plan now if the tic's budget allows, otherwise queue. False: no route exists. */
static bool assign_route(const level_t *map, mobj_t *unit, fixed2_t goal, const uint8_t *soft) {
    if (over_budget()) { defer_route(unit, goal); return true; }
    navpath_t path;
    fixed2_t position = fixed3_xy(unit->core.position);
    if (!P_NavPlan(map, P_MobjMoveClass(unit), P_MobjRadius(unit), position, goal, soft, &path))
        return false;
    adopt_route(unit, &path);
    return true;
}

/* Complete queued plans, oldest order first, until the tic's budget is spent. */
void P_NavRunPlans(const level_t *map) {
    P_NavBeginTick();
    for (;;) {
        mobj_t *next = NULL;
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            mobj_t *unit = (mobj_t *)th;
            if (!unit->movement.plan_pending) continue;
            if (unit->remove || unit->hp <= 0) { unit->movement.plan_pending = false; continue; }
            if (!next || unit->movement.plan_seq < next->movement.plan_seq) next = unit;
        }
        if (!next || over_budget()) return;
        navpath_t path;
        uint8_t *soft = P_IdleBlockers(map, next, 0);
        bool planned = P_NavPlan(map, P_MobjMoveClass(next), P_MobjRadius(next),
                                 fixed3_xy(next->core.position),
                                 next->movement.goal, soft, &path);
        free(soft);
        if (planned) adopt_route(next, &path);
        else { P_ClearMove(next); next->movement.order_id = 0; }
    }
}

/* Derive a follower's route from the leader's: join the shared polyline at the
 * farthest point already in sight, and finish at the follower's own slot. */
static bool share_route(const level_t *map, const mobj_t *leader, mobj_t *unit, fixed2_t slot) {
    const navpath_t *lead = &leader->movement.path;
    if (!lead->count || P_MobjMoveClass(unit) != P_MobjMoveClass(leader) ||
        P_MobjRadius(unit) > P_MobjRadius(leader) + FIXED_LIT(0.001)) return false;
    fixed2_t position = fixed3_xy(unit->core.position);
    int cls = P_MobjMoveClass(unit), join = -1;
    for (int i = 0; i < lead->count; ++i)
        if (P_NavLineClear(map, cls, position, lead->points[i], P_MobjRadius(unit))) join = i;
    if (join < 0) return false;
    navpath_t path = {.goal = slot, .complete = lead->complete};
    for (int i = join; i < lead->count; ++i) path.points[path.count++] = lead->points[i];
    if (lead->complete) {
        fixed2_t before = path.count > 1 ? path.points[path.count - 2] : position;
        if (P_NavLineClear(map, cls, before, slot, P_MobjRadius(unit))) path.points[path.count - 1] = slot;
        else if (path.count < NAV_MAX_WAYPOINTS) path.points[path.count++] = slot;
        else return false;
    } else path.goal = unit->movement.goal;
    adopt_route(unit, &path);
    unit->movement.goal = slot;
    return true;
}

void P_MoveUnitsAt(const level_t *map, mobj_t *const *units, int unit_count,
                   fixed2_t goal_position) {
    int selected_count = 0;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i]->hp <= 0 || (units[i]->traits & MF_MOBILE) == 0) continue;
        if (units[i]->traits & MF_FLY) {
            units[i]->harvest.target = -1;
            units[i]->harvest.timer_ms = 0;
            units[i]->harvest.phase = 0;
            P_MoveUnitTo(map, units[i], goal_position);
            continue;
        }
        selected_count++;
    }
    if (selected_count <= 0) return;

    cell_t goal = fixed2_cell(goal_position);
    int lead_class = 0;
    for (int i = 0; i < unit_count; ++i)
        if (units[i]->hp > 0 && (units[i]->traits & (MF_MOBILE | MF_FLY)) == MF_MOBILE) {
            lead_class = P_MobjMoveClass(units[i]);
            break;
        }
    if (!find_nearest_walkable_cell(map, lead_class, goal, 8, &goal)) return;

    int formation_columns = selected_count < 3 ? selected_count : 3;
    int formation_rows = (selected_count + formation_columns - 1) / formation_columns;
    int selected_index = 0;
    uint32_t order_id = next_move_order_id();
    fixed2_t slots[unit_count > 0 ? unit_count : 1];
    bool ground[unit_count > 0 ? unit_count : 1];
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *unit = units[i];
        ground[i] = unit->hp > 0 && (unit->traits & MF_MOBILE) && !(unit->traits & MF_FLY);
        if (!ground[i]) continue;
        unit->core.momentum = fixed3_zero();
        unit->movement.order_id = order_id;
        unit->movement.order_arrived = false;
        unit->harvest.target = -1;
        unit->harvest.timer_ms = 0;
        unit->harvest.phase = 0;
        int cls = P_MobjMoveClass(unit);
        int row = selected_index / formation_columns;
        int row_start = row * formation_columns;
        int row_count = selected_count - row_start;
        if (row_count > formation_columns) row_count = formation_columns;
        int col = selected_index - row_start;
        fixed_t spacing = fixed_mul32(P_MobjRadius(unit), FIXED_LIT(2.1));
        fixed_t offset_x = fixed_mul32((2 * col - (row_count - 1)) * (FIXED_ONE / 2), spacing);
        fixed_t offset_y = fixed_mul32((2 * row - (formation_rows - 1)) * (FIXED_ONE / 2), spacing);
        fixed2_t slot = fixed2_add(goal_position, (fixed2_t){ offset_x, offset_y });
        if (!P_CheckPosition(map, unit, slot) ||
            position_overlaps_reserved_goal(units, unit_count, i, slot, P_MobjRadius(unit), order_id)) {
            fixed2_t adjusted = fixed2_cell_center((ivec2_t){ goal.x, goal.y });
            if (!find_nearest_position(map, cls, units, unit_count, i, order_id, adjusted,
                                       P_MobjRadius(unit), 8, &adjusted))
                find_nearest_position(map, cls, NULL, 0, -1, 0, adjusted, P_MobjRadius(unit), 8, &adjusted);
            slot = adjusted;
        }
        slots[i] = slot;
        unit->movement.goal = slot; /* Reserve before the next slot is chosen. */
        selected_index++;
    }

    /* The leader is the unit nearest the goal; its route is the group's. */
    int leader = -1;
    int64_t nearest = 0;
    for (int i = 0; i < unit_count; ++i) {
        if (!ground[i]) continue;
        int64_t d = fixed2_distance_squared64(fixed3_xy(units[i]->core.position), goal_position);
        if (leader < 0 || d < nearest) { leader = i; nearest = d; }
    }
    uint8_t *soft = P_IdleBlockers(map, NULL, order_id);
    bool leader_planned = false;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < unit_count; ++i) {
            if (!ground[i] || (pass == 0) != (i == leader)) continue;
            mobj_t *unit = units[i];
            fixed2_t position = fixed3_xy(unit->core.position);
            if (fixed2_distance_squared64(slots[i], position) <= FIXED_LIT_64(0.05 * 0.05)) {
                unit->movement.goal = slots[i];
                P_ClearMove(unit);
                unit->movement.order_arrived = true;
                continue;
            }
            bool done = false;
            if (pass == 1 && leader_planned && selected_count >= SHARE_MIN_GROUP &&
                !units[leader]->movement.plan_pending)
                done = share_route(map, units[leader], unit, slots[i]);
            if (!done) done = assign_route(map, unit, slots[i], soft);
            if (!done) { P_ClearMove(unit); unit->movement.order_id = 0; }
            else if (pass == 0) leader_planned = unit->movement.path.count > 0;
        }
    free(soft);
}

bool P_MoveUnitTo(const level_t *map, mobj_t *unit, fixed2_t goal_position) {
    if (!map || !unit || unit->hp <= 0 || (unit->traits & MF_MOBILE) == 0) return false;
    unit->core.momentum = fixed3_zero();
    if (unit->traits & MF_FLY) {
        unit->movement.goal = goal_position;
        P_ClearMove(unit);
        unit->movement.order_id = next_move_order_id();
        unit->movement.order_arrived = false;
        return true;
    }
    uint8_t *soft = P_IdleBlockers(map, unit, 0);
    bool ok = assign_route(map, unit, goal_position, soft);
    free(soft);
    return ok;
}

static int find_resource_vent_at(const level_t *map, fixed2_t position) {
    if (!map || !map->resource_vents || map->resource_vent_count <= 0) return -1;
    int cell_x = fixed_floor_int(position.x);
    int cell_y = fixed_floor_int(position.y);
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!vent->active || vent->rate <= 0 || vent->amount <= 0) continue;
        if (P_ResourceVentContainsCell(vent, (ivec2_t){ cell_x, cell_y })) return i;
    }

    int best = -1;
    int64_t best_dist2 = INT64_MAX;
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!vent->active || vent->rate <= 0 || vent->amount <= 0) continue;
        fixed_t radius = P_ResourceVentRadius(vent);
        int64_t dist2 = fixed2_distance_squared64(vent->attachment, position);
        if (dist2 < fixed_sq64(radius) && dist2 < best_dist2) {
            best_dist2 = dist2;
            best = i;
        }
    }
    return best;
}

bool P_HarvestUnitsAt(const level_t *map, mobj_t *const *units, int unit_count,
                     fixed2_t position) {
    bool handled_issue = false;
    if (G_GameHarvestAt(units, unit_count, position, &handled_issue)) return handled_issue;
    int vent_index = find_resource_vent_at(map, position);
    if (vent_index < 0) return false;

    bool has_harvester = false;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i]->hp > 0 &&
            (units[i]->traits & (MF_MOBILE | MF_HARVESTER)) ==
                (MF_MOBILE | MF_HARVESTER)) {
            has_harvester = true;
            break;
        }
    }
    if (!has_harvester) return false;

    const resourcevent_t *vent = &map->resource_vents[vent_index];
    if (vent->resource_type < 0 || vent->resource_type >= RTS_MAX_RESOURCES) return false;
    bool issued = false;
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *unit = units[i];
        if (unit->hp <= 0) continue;
        if ((unit->traits & (MF_MOBILE | MF_HARVESTER)) !=
            (MF_MOBILE | MF_HARVESTER)) {
            continue;
        }
        if (!P_VentOpenTo(map, vent, unit)) continue;
        if (!(unit->traits & MF_FLY)) {
            /* The planner falls back to the nearest reachable spot, which must
             * still be at the vent: a walled-off vent is not harvestable. */
            ivec2_t near;
            if (!P_NavNearestReachable(map, P_MobjMoveClass(unit), fixed2_cell(fixed3_xy(unit->core.position)),
                                       fixed2_cell(vent->attachment), fixed_ceil_int(P_ResourceVentRadius(vent)) + 1, &near))
                continue;
        }
        unit->core.momentum = fixed3_zero();

        fixed2_t bay = vent->attachment;
        if (!P_CheckPosition(map, unit, bay) &&
            !P_ApproachFootprint(unit, vent->cell, vent->footprint, &bay)) continue;
        if (!P_MoveUnitTo(map, unit, bay)) continue;
        unit->attack.target = NULL;
        unit->harvest.target = vent_index;
        unit->harvest.timer_ms = 0;
        if (!unit->harvest.cargo) unit->harvest.resource_type = vent->resource_type;
        unit->harvest.base = NULL;
        unit->harvest.phase = HARVEST_PHASE_TO_MINE;
        unit->movement.order_id = 0; /* A shared resource bay requires exact arrival. */
        unit->movement.order_arrived = false;
        issued = true;
    }
    return issued;
}

bool P_HarvestOrderAt(const level_t *map, mobj_t *const *units, int unit_count,
                      fixed2_t position) {
    mobj_t *selected[unit_count > 0 ? unit_count : 1];
    int count = 0;
    for (int i = 0; i < unit_count; ++i)
        if (P_MobjIsSelected(units[i]) && units[i]->owner == 0) {
            units[i]->waypoints = (waypoints_t){0};
            selected[count++] = units[i];
        }
    return P_HarvestUnitsAt(map, selected, count, position);
}

bool P_HarvestUnitTo(const level_t *map, mobj_t *unit, fixed2_t position) {
    if (!unit || unit->hp <= 0) return false;
    return P_HarvestUnitsAt(map, &unit, 1, position);
}

void P_MoveOrderAt(const level_t *map, mobj_t *const *units, int unit_count,
                   fixed2_t position) {
    mobj_t *selected[unit_count > 0 ? unit_count : 1];
    int count = 0;
    for (int i = 0; i < unit_count; ++i)
        if (P_MobjIsSelected(units[i]) && units[i]->owner == 0) {
            units[i]->waypoints = (waypoints_t){0};
            selected[count++] = units[i];
        }
    P_MoveUnitsAt(map, selected, count, position);
}

void P_MoveOrder(const level_t *map, mobj_t *const *units, int unit_count, cell_t goal) {
    P_MoveOrderAt(map, units, unit_count, fixed2_cell_center(goal));
}
