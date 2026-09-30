#define _DEFAULT_SOURCE
#include "engine.h"
#ifdef RTS_GAME_DARK_COLONY
#include "dark-colony.h"
#endif
#include "p_nav.h"

static uint32_t next_move_order_id(void) {
    if (++level.next_move_order_id == 0) ++level.next_move_order_id;
    return level.next_move_order_id;
}

int L_Index(const level_t *map, int x, int y) {
    return y * map->width + x;
}

bool L_Contains(const level_t *map, int x, int y) {
    return x >= 0 && y >= 0 && x < map->width && y < map->height;
}

bool L_IsWalkable(const level_t *map, int x, int y) {
    return L_Contains(map, x, y) && (!map->blocked || map->blocked[L_Index(map, x, y)] == 0);
}

float P_MobjRadius(const mobj_t *unit) {
    if (unit && unit->radius > 0.05f) return unit->radius;
    return 0.42f;
}

static float cell_distance_squared(fvec2_t position, ivec2_t cell) {
    fvec2_t closest = {fmaxf(cell.x, fminf(position.x, cell.x + 1)),
                       fmaxf(cell.y, fminf(position.y, cell.y + 1))};
    return fvec2_distance_squared(position, closest);
}

static bool map_circle_walkable(const level_t *map, float gx, float gy, float radius,
                                const fvec2_t *from) {
    if (!map) return true;
    if (radius < 0.01f) radius = 0.01f;
    if (gx - radius < 0.0f || gy - radius < 0.0f ||
        gx + radius > (float)map->width || gy + radius > (float)map->height) {
        return false;
    }

    int min_x = (int)floorf(gx - radius);
    int max_x = (int)floorf(gx + radius);
    int min_y = (int)floorf(gy - radius);
    int max_y = (int)floorf(gy + radius);
    float radius2 = radius * radius - 0.0001f;
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            if (L_IsWalkable(map, x, y)) continue;
            ivec2_t cell = {x, y};
            float distance = cell_distance_squared((fvec2_t){gx, gy}, cell);
            if (distance >= radius2) continue;
            /* An authored spawn can overlap terrain. Permit escape from
             * that overlap, but never enter or deepen another obstruction. */
            if (from) {
                float previous = cell_distance_squared(*from, cell);
                if (previous < radius2 && distance >= previous) continue;
            }
            return false;
        }
    }
    return true;
}

bool P_MapCircleWalkable(const level_t *map, float gx, float gy, float radius,
                         const fvec2_t *from) {
    return map_circle_walkable(map, gx, gy, radius, from);
}

bool P_CheckPosition(const level_t *map, const mobj_t *unit, float gx, float gy) {
    return map_circle_walkable(map, gx, gy, P_MobjRadius(unit), NULL);
}

bool P_TryMove(mobj_t *unit, fixed3_t position) {
    fvec2_t from = fixed3_xy_to_fvec2(unit->core.position);
    fvec2_t to = fixed3_xy_to_fvec2(position);
    if (!(unit->traits & MF_FLY) &&
        !map_circle_walkable(&level, to.x, to.y, P_MobjRadius(unit), &from)) return false;
    if (!(unit->traits & MF_FLY)) {
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            const mobj_t *other = (const mobj_t *)th;
            if (other == unit || other->remove || other->hp <= 0 || !P_HarvesterDocked(other)) continue;
            float radius = P_MobjRadius(unit) + P_MobjRadius(other);
            fvec2_t position = fixed3_xy_to_fvec2(other->core.position);
            if (fvec2_distance_squared(to, position) < radius * radius &&
                fvec2_distance_squared(to, position) < fvec2_distance_squared(from, position))
                return false;
        }
    }
    unit->core.momentum = fixed3_planar_displacement(unit->core.position, position);
    unit->core.position = position;
    return true;
}

static bool position_overlaps_reserved_goal(mobj_t *const *units, int unit_count, int self_index,
                                            float gx, float gy, float radius,
                                            uint32_t order_id) {
    if (!units || order_id == 0) return false;
    for (int i = 0; i < unit_count; ++i) {
        if (i == self_index) continue;
        const mobj_t *other = units[i];
        if (other->remove || other->hp <= 0 || other->movement.order_id != order_id) continue;
        float min_dist = radius + P_MobjRadius(other);
        float dx = other->movement.goal.x - gx;
        float dy = other->movement.goal.y - gy;
        if (dx * dx + dy * dy < min_dist * min_dist) return true;
    }
    return false;
}

void P_ClampToLevel(const level_t *map, mobj_t *unit) {
    if (!map || !unit || map->width <= 0 || map->height <= 0) return;
    fvec2_t position = fixed3_xy_to_fvec2(unit->core.position);
    float r = P_MobjRadius(unit);
    float min_x = r;
    float min_y = r;
    float max_x = (float)map->width - r;
    float max_y = (float)map->height - r;
    if (max_x < min_x) max_x = min_x = (float)map->width * 0.5f;
    if (max_y < min_y) max_y = min_y = (float)map->height * 0.5f;
    if (position.x < min_x) position.x = min_x;
    if (position.y < min_y) position.y = min_y;
    if (position.x > max_x) position.x = max_x;
    if (position.y > max_y) position.y = max_y;
    unit->core.position = fixed3_with_xy(unit->core.position, position);
}

static bool find_nearest_walkable_cell(const level_t *map, cell_t wanted, int radius, cell_t *out) {
    if (!map || !out) return false;
    if (L_IsWalkable(map, wanted.x, wanted.y)) {
        *out = wanted;
        return true;
    }
    int best_h = 1000000;
    bool found = false;
    cell_t best = wanted;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            cell_t c = { wanted.x + dx, wanted.y + dy };
            if (!L_IsWalkable(map, c.x, c.y)) continue;
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

static bool find_nearest_walkable_position(const level_t *map, float wanted_gx, float wanted_gy,
                                           float unit_radius, int search_radius,
                                           float *gx_out, float *gy_out) {
    if (!gx_out || !gy_out) return false;
    if (map_circle_walkable(map, wanted_gx, wanted_gy, unit_radius, NULL)) {
        *gx_out = wanted_gx;
        *gy_out = wanted_gy;
        return true;
    }
    cell_t wanted = { (int)floorf(wanted_gx), (int)floorf(wanted_gy) };
    float best_d2 = 1000000000.0f;
    bool found = false;
    float best_x = wanted_gx;
    float best_y = wanted_gy;
    for (int dy = -search_radius; dy <= search_radius; ++dy) {
        for (int dx = -search_radius; dx <= search_radius; ++dx) {
            float gx = (float)(wanted.x + dx) + 0.5f;
            float gy = (float)(wanted.y + dy) + 0.5f;
            if (!map_circle_walkable(map, gx, gy, unit_radius, NULL)) continue;
            float ddx = gx - wanted_gx;
            float ddy = gy - wanted_gy;
            float d2 = ddx * ddx + ddy * ddy;
            if (d2 < best_d2) {
                best_d2 = d2;
                best_x = gx;
                best_y = gy;
                found = true;
            }
        }
    }
    if (!found) return false;
    *gx_out = best_x;
    *gy_out = best_y;
    return true;
}

static bool find_nearest_unreserved_walkable_position(const level_t *map,
                                                      mobj_t *const *units, int unit_count,
                                                      int self_index, uint32_t order_id,
                                                      float wanted_gx, float wanted_gy,
                                                      float unit_radius, int search_radius,
                                                      float *gx_out, float *gy_out) {
    if (!gx_out || !gy_out) return false;
    if (map_circle_walkable(map, wanted_gx, wanted_gy, unit_radius, NULL) &&
        !position_overlaps_reserved_goal(units, unit_count, self_index,
                                         wanted_gx, wanted_gy, unit_radius, order_id)) {
        *gx_out = wanted_gx;
        *gy_out = wanted_gy;
        return true;
    }

    cell_t wanted = { (int)floorf(wanted_gx), (int)floorf(wanted_gy) };
    float best_d2 = 1000000000.0f;
    bool found = false;
    float best_x = wanted_gx;
    float best_y = wanted_gy;
    for (int dy = -search_radius; dy <= search_radius; ++dy) {
        for (int dx = -search_radius; dx <= search_radius; ++dx) {
            float gx = (float)(wanted.x + dx) + 0.5f;
            float gy = (float)(wanted.y + dy) + 0.5f;
            if (!map_circle_walkable(map, gx, gy, unit_radius, NULL)) continue;
            if (position_overlaps_reserved_goal(units, unit_count, self_index,
                                                gx, gy, unit_radius, order_id)) {
                continue;
            }
            float ddx = gx - wanted_gx;
            float ddy = gy - wanted_gy;
            float d2 = ddx * ddx + ddy * ddy;
            if (d2 < best_d2) {
                best_d2 = d2;
                best_x = gx;
                best_y = gy;
                found = true;
            }
        }
    }
    if (!found) return false;
    *gx_out = best_x;
    *gy_out = best_y;
    return true;
}

/* Plan a ground route; movement.goal becomes the resolved goal, which is
 * the request or the nearest reachable spot when the request is unusable. */
static bool assign_route(const level_t *map, mobj_t *unit, fvec2_t goal) {
    navpath_t path;
    fvec2_t position = fixed3_xy_to_fvec2(unit->core.position);
    if (!P_NavPlan(map, P_MobjRadius(unit), position, goal, &path)) return false;
    unit->movement.path = path;
    unit->movement.goal = path.goal;
    unit->movement.stuck_tics = unit->movement.replans = 0;
    unit->movement.order_arrived = false;
    unit->core.momentum = fixed3_zero();
    return true;
}

void P_MoveUnitsAt(const level_t *map, mobj_t *const *units, int unit_count,
                   fvec2_t goal_position) {
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

    cell_t goal = { (int)floorf(goal_position.x), (int)floorf(goal_position.y) };
    if (!find_nearest_walkable_cell(map, goal, 8, &goal)) return;

    int formation_columns = selected_count < 3 ? selected_count : 3;
    int formation_rows = (selected_count + formation_columns - 1) / formation_columns;
    int selected_index = 0;
    uint32_t order_id = next_move_order_id();
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *unit = units[i];
        if (unit->hp <= 0 || (unit->traits & MF_MOBILE) == 0 || (unit->traits & MF_FLY)) continue;
        unit->core.momentum = fixed3_zero();
        unit->movement.order_id = order_id;
        unit->movement.order_arrived = false;
        unit->harvest.target = -1;
        unit->harvest.timer_ms = 0;
        unit->harvest.phase = 0;
        fvec2_t position = fixed3_xy_to_fvec2(unit->core.position);
        int row = selected_index / formation_columns;
        int row_start = row * formation_columns;
        int row_count = selected_count - row_start;
        if (row_count > formation_columns) row_count = formation_columns;
        int col = selected_index - row_start;
        float spacing = P_MobjRadius(unit) * 2.1f;
        float offset_x = ((float)col - ((float)row_count - 1.0f) * 0.5f) * spacing;
        float offset_y = ((float)row - ((float)formation_rows - 1.0f) * 0.5f) * spacing;
        fvec2_t slot = fvec2_add(goal_position, (fvec2_t){ offset_x, offset_y });
        if (!P_CheckPosition(map, unit, slot.x, slot.y) ||
            position_overlaps_reserved_goal(units, unit_count, i, slot.x, slot.y,
                                            P_MobjRadius(unit), order_id)) {
            fvec2_t adjusted = fvec2_cell_center((ivec2_t){ goal.x, goal.y });
            if (!find_nearest_unreserved_walkable_position(map, units, unit_count, i, order_id,
                                                           adjusted.x, adjusted.y,
                                                           P_MobjRadius(unit), 8,
                                                           &adjusted.x, &adjusted.y))
                find_nearest_walkable_position(map, adjusted.x, adjusted.y, P_MobjRadius(unit), 8,
                                               &adjusted.x, &adjusted.y);
            slot = adjusted;
        }
        selected_index++;
        if (fvec2_distance_squared(slot, position) <= 0.05f * 0.05f) {
            unit->movement.goal = slot;
            P_ClearMove(unit);
            unit->movement.order_arrived = true;
        } else if (!assign_route(map, unit, slot)) {
            P_ClearMove(unit);
            unit->movement.order_id = 0;
        }
    }
}

bool P_MoveUnitTo(const level_t *map, mobj_t *unit, fvec2_t goal_position) {
    if (!map || !unit || unit->hp <= 0 || (unit->traits & MF_MOBILE) == 0) return false;
    unit->core.momentum = fixed3_zero();
    if (unit->traits & MF_FLY) {
        unit->movement.goal = goal_position;
        P_ClearMove(unit);
        unit->movement.order_id = next_move_order_id();
        unit->movement.order_arrived = false;
        return true;
    }
    return assign_route(map, unit, goal_position);
}

static int find_resource_vent_at(const level_t *map, fvec2_t position) {
    if (!map || !map->resource_vents || map->resource_vent_count <= 0) return -1;
    int cell_x = (int)floorf(position.x);
    int cell_y = (int)floorf(position.y);
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!vent->active || vent->rate <= 0 || vent->amount <= 0) continue;
        if (P_ResourceVentContainsCell(vent, (ivec2_t){ cell_x, cell_y })) return i;
    }

    int best = -1;
    float best_dist2 = 1e30f;
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!vent->active || vent->rate <= 0 || vent->amount <= 0) continue;
        float radius = P_ResourceVentRadius(vent);
        float dist2 = fvec2_distance_squared(vent->attachment, position);
        if (dist2 < radius * radius && dist2 < best_dist2) {
            best_dist2 = dist2;
            best = i;
        }
    }
    return best;
}

bool P_HarvestUnitsAt(const level_t *map, mobj_t *const *units, int unit_count,
                     fvec2_t position) {
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
        unit->core.momentum = fixed3_zero();

        if (!P_MoveUnitTo(map, unit, vent->attachment)) continue;
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
                      fvec2_t position) {
    mobj_t *selected[unit_count > 0 ? unit_count : 1];
    int count = 0;
    for (int i = 0; i < unit_count; ++i)
        if (P_MobjIsSelected(units[i]) && units[i]->owner == 0) {
            units[i]->waypoints = (waypoints_t){0};
            selected[count++] = units[i];
        }
    return P_HarvestUnitsAt(map, selected, count, position);
}

bool P_HarvestUnitTo(const level_t *map, mobj_t *unit, fvec2_t position) {
    if (!unit || unit->hp <= 0) return false;
    return P_HarvestUnitsAt(map, &unit, 1, position);
}

void P_MoveOrderAt(const level_t *map, mobj_t *const *units, int unit_count,
                   fvec2_t position) {
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
    P_MoveOrderAt(map, units, unit_count, fvec2_cell_center(goal));
}
