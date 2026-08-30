#define _DEFAULT_SOURCE
#include "p_local.h"


typedef struct {
    int g;
    int f;
    int parent;
    uint8_t state;
} AStarNode;

typedef struct {
    int cost;
    uint8_t closed;
    uint8_t queued;
} FlowCell;

static uint32_t g_next_move_order_id = 1;

static uint32_t next_move_order_id(void) {
    uint32_t id = g_next_move_order_id++;
    if (g_next_move_order_id == 0) g_next_move_order_id = 1;
    return id;
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

static bool map_circle_walkable(const level_t *map, float gx, float gy, float radius) {
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
            float closest_x = gx;
            float closest_y = gy;
            if (closest_x < (float)x) closest_x = (float)x;
            if (closest_x > (float)x + 1.0f) closest_x = (float)x + 1.0f;
            if (closest_y < (float)y) closest_y = (float)y;
            if (closest_y > (float)y + 1.0f) closest_y = (float)y + 1.0f;
            float dx = gx - closest_x;
            float dy = gy - closest_y;
            if (dx * dx + dy * dy < radius2) return false;
        }
    }
    return true;
}

bool P_CheckPosition(const level_t *map, const mobj_t *unit, float gx, float gy) {
    return map_circle_walkable(map, gx, gy, P_MobjRadius(unit));
}

static bool position_overlaps_reserved_goal(const mobj_t *units, int unit_count, int self_index,
                                            float gx, float gy, float radius,
                                            uint32_t order_id) {
    if (!units || order_id == 0) return false;
    for (int i = 0; i < unit_count; ++i) {
        if (i == self_index) continue;
        const mobj_t *other = &units[i];
        if (other->remove || other->hp <= 0 || other->move_order_id != order_id) continue;
        float min_dist = radius + P_MobjRadius(other);
        float dx = other->move_goal_gx - gx;
        float dy = other->move_goal_gy - gy;
        if (dx * dx + dy * dy < min_dist * min_dist) return true;
    }
    return false;
}

void P_ClampToLevel(const level_t *map, mobj_t *unit) {
    if (!map || !unit || map->width <= 0 || map->height <= 0) return;
    float r = P_MobjRadius(unit);
    float min_x = r;
    float min_y = r;
    float max_x = (float)map->width - r;
    float max_y = (float)map->height - r;
    if (max_x < min_x) max_x = min_x = (float)map->width * 0.5f;
    if (max_y < min_y) max_y = min_y = (float)map->height * 0.5f;
    if (unit->gx < min_x) unit->gx = min_x;
    if (unit->gy < min_y) unit->gy = min_y;
    if (unit->gx > max_x) unit->gx = max_x;
    if (unit->gy > max_y) unit->gy = max_y;
}

static int heuristic(cell_t a, cell_t b) {
    int dx = abs(a.x - b.x);
    int dy = abs(a.y - b.y);
    return 10 * (dx + dy);
}

int P_FindPath(const level_t *map, cell_t start, cell_t goal, cell_t *out_path, int max_path) {
    if (!L_IsWalkable(map, start.x, start.y) || !L_Contains(map, goal.x, goal.y) || max_path <= 0) return 0;
    if (!L_IsWalkable(map, goal.x, goal.y)) {
        const int radius = 8;
        bool found = false;
        cell_t best = goal;
        int best_h = 1000000;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                int x = goal.x + dx;
                int y = goal.y + dy;
                if (!L_IsWalkable(map, x, y)) continue;
                int h = abs(dx) + abs(dy);
                if (h < best_h) {
                    best_h = h;
                    best = (cell_t){ x, y };
                    found = true;
                }
            }
        }
        if (!found) return 0;
        goal = best;
    }

    int total = map->width * map->height;
    AStarNode *nodes = calloc((size_t)total, sizeof(AStarNode));
    int *open = malloc((size_t)total * sizeof(int));
    if (!nodes || !open) {
        free(nodes);
        free(open);
        return 0;
    }
    for (int i = 0; i < total; ++i) nodes[i].parent = -1;
    int open_count = 0;
    int start_idx = L_Index(map, start.x, start.y);
    int goal_idx = L_Index(map, goal.x, goal.y);
    nodes[start_idx].g = 0;
    nodes[start_idx].f = heuristic(start, goal);
    nodes[start_idx].state = 1;
    open[open_count++] = start_idx;

    const int dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    while (open_count > 0) {
        int best_open = 0;
        for (int i = 1; i < open_count; ++i) {
            if (nodes[open[i]].f < nodes[open[best_open]].f) best_open = i;
        }
        int current = open[best_open];
        open[best_open] = open[--open_count];
        nodes[current].state = 2;
        if (current == goal_idx) break;

        int cx = current % map->width;
        int cy = current / map->width;
        for (int d = 0; d < 4; ++d) {
            int nx = cx + dirs[d][0];
            int ny = cy + dirs[d][1];
            if (!L_IsWalkable(map, nx, ny)) continue;
            int ni = L_Index(map, nx, ny);
            if (nodes[ni].state == 2) continue;
            int ng = nodes[current].g + 10;
            if (nodes[ni].state != 1 || ng < nodes[ni].g) {
                nodes[ni].parent = current;
                nodes[ni].g = ng;
                nodes[ni].f = ng + heuristic((cell_t){ nx, ny }, goal);
                if (nodes[ni].state != 1) {
                    nodes[ni].state = 1;
                    open[open_count++] = ni;
                }
            }
        }
    }

    int length = 0;
    if (nodes[goal_idx].parent != -1 || goal_idx == start_idx) {
        int cursor = goal_idx;
        while (cursor != -1 && length < max_path) {
            out_path[length++] = (cell_t){ cursor % map->width, cursor / map->width };
            cursor = nodes[cursor].parent;
        }
        for (int i = 0; i < length / 2; ++i) {
            cell_t tmp = out_path[i];
            out_path[i] = out_path[length - 1 - i];
            out_path[length - 1 - i] = tmp;
        }
    }
    free(nodes);
    free(open);
    return length;
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
    if (map_circle_walkable(map, wanted_gx, wanted_gy, unit_radius)) {
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
            if (!map_circle_walkable(map, gx, gy, unit_radius)) continue;
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
                                                      const mobj_t *units, int unit_count,
                                                      int self_index, uint32_t order_id,
                                                      float wanted_gx, float wanted_gy,
                                                      float unit_radius, int search_radius,
                                                      float *gx_out, float *gy_out) {
    if (!gx_out || !gy_out) return false;
    if (map_circle_walkable(map, wanted_gx, wanted_gy, unit_radius) &&
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
            if (!map_circle_walkable(map, gx, gy, unit_radius)) continue;
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

static FlowCell *build_flow_field(const level_t *map, cell_t goal) {
    if (!map || !L_IsWalkable(map, goal.x, goal.y)) return NULL;
    int total = map->width * map->height;
    FlowCell *field = malloc((size_t)total * sizeof(*field));
    int *open = malloc((size_t)total * sizeof(*open));
    if (!field || !open) {
        free(field);
        free(open);
        return NULL;
    }
    for (int i = 0; i < total; ++i) {
        field[i].cost = 1000000000;
        field[i].closed = 0;
        field[i].queued = 0;
    }

    int open_count = 0;
    int goal_idx = L_Index(map, goal.x, goal.y);
    field[goal_idx].cost = 0;
    field[goal_idx].queued = 1;
    open[open_count++] = goal_idx;

    static const int dirs[8][3] = {
        { 1, 0, 10 }, { -1, 0, 10 }, { 0, 1, 10 }, { 0, -1, 10 },
        { 1, 1, 14 }, { -1, 1, 14 }, { 1, -1, 14 }, { -1, -1, 14 },
    };
    while (open_count > 0) {
        int best_open = 0;
        for (int i = 1; i < open_count; ++i) {
            if (field[open[i]].cost < field[open[best_open]].cost) best_open = i;
        }
        int current = open[best_open];
        open[best_open] = open[--open_count];
        field[current].queued = 0;
        if (field[current].closed) continue;
        field[current].closed = 1;

        int cx = current % map->width;
        int cy = current / map->width;
        for (int d = 0; d < 8; ++d) {
            int nx = cx + dirs[d][0];
            int ny = cy + dirs[d][1];
            if (!L_IsWalkable(map, nx, ny)) continue;
            if (dirs[d][0] != 0 && dirs[d][1] != 0 &&
                (!L_IsWalkable(map, cx + dirs[d][0], cy) ||
                 !L_IsWalkable(map, cx, cy + dirs[d][1]))) {
                continue;
            }
            int ni = L_Index(map, nx, ny);
            int next_cost = field[current].cost + dirs[d][2];
            if (next_cost >= field[ni].cost) continue;
            field[ni].cost = next_cost;
            if (!field[ni].queued && !field[ni].closed && open_count < total) {
                field[ni].queued = 1;
                open[open_count++] = ni;
            }
        }
    }

    free(open);
    return field;
}

static bool line_walkable(const level_t *map, cell_t a, cell_t b, float radius) {
    if (!map) return true;
    int dx = abs(b.x - a.x);
    int dy = abs(b.y - a.y);
    int steps = (dx > dy ? dx : dy) * 4;
    if (steps <= 0) return map_circle_walkable(map, (float)a.x + 0.5f, (float)a.y + 0.5f, radius);
    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        float x = (float)a.x + 0.5f + ((float)b.x - (float)a.x) * t;
        float y = (float)a.y + 0.5f + ((float)b.y - (float)a.y) * t;
        if (!map_circle_walkable(map, x, y, radius)) return false;
    }
    return true;
}

static int smooth_cell_path(const level_t *map, cell_t *path, int length, float radius) {
    if (!map || !path || length <= 2) return length;
    int write = 0;
    int anchor = 0;
    path[write++] = path[0];
    while (anchor < length - 1) {
        int next = length - 1;
        while (next > anchor + 1 && !line_walkable(map, path[anchor], path[next], radius)) next--;
        path[write++] = path[next];
        anchor = next;
    }
    return write;
}

static int flow_path_find(const level_t *map, const FlowCell *field, cell_t start,
                          cell_t *out_path, int max_path, float radius) {
    if (!map || !field || !out_path || max_path <= 0 ||
        !L_IsWalkable(map, start.x, start.y)) {
        return 0;
    }
    int current = L_Index(map, start.x, start.y);
    if (field[current].cost >= 1000000000) return 0;
    int length = 0;
    out_path[length++] = start;
    static const int dirs[8][2] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
        { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 },
    };
    int guard = map->width * map->height;
    while (field[current].cost > 0 && length < max_path && guard-- > 0) {
        int cx = current % map->width;
        int cy = current / map->width;
        int best = current;
        int best_cost = field[current].cost;
        for (int d = 0; d < 8; ++d) {
            int nx = cx + dirs[d][0];
            int ny = cy + dirs[d][1];
            if (!L_IsWalkable(map, nx, ny)) continue;
            if (dirs[d][0] != 0 && dirs[d][1] != 0 &&
                (!L_IsWalkable(map, cx + dirs[d][0], cy) ||
                 !L_IsWalkable(map, cx, cy + dirs[d][1]))) {
                continue;
            }
            int ni = L_Index(map, nx, ny);
            if (field[ni].cost < best_cost) {
                best = ni;
                best_cost = field[ni].cost;
            }
        }
        if (best == current) break;
        current = best;
        out_path[length++] = (cell_t){ current % map->width, current / map->width };
    }
    return smooth_cell_path(map, out_path, length, radius);
}



void P_MoveOrderAt(const level_t *map, mobj_t *units, int unit_count,
                                float goal_gx, float goal_gy) {
    int selected_count = 0;
    for (int i = 0; i < unit_count; ++i) {
        if (!units[i].selected) continue;
        if (units[i].hp <= 0) continue;
        if (units[i].owner != 0 || (units[i].traits & MF_MOBILE) == 0) continue;
        selected_count++;
    }
    if (selected_count <= 0) return;

    cell_t goal = { (int)floorf(goal_gx), (int)floorf(goal_gy) };
    if (!find_nearest_walkable_cell(map, goal, 8, &goal)) return;
    FlowCell *field = build_flow_field(map, goal);
    if (!field) return;

    int formation_columns = selected_count < 3 ? selected_count : 3;
    int formation_rows = (selected_count + formation_columns - 1) / formation_columns;
    int selected_index = 0;
    uint32_t order_id = next_move_order_id();
    for (int i = 0; i < unit_count; ++i) {
        if (!units[i].selected) continue;
        if (units[i].hp <= 0) continue;
        if (units[i].owner != 0 || (units[i].traits & MF_MOBILE) == 0) continue;
        units[i].move_order_id = order_id;
        units[i].move_order_arrived = false;
        units[i].harvest_target = -1;
        units[i].harvest_timer_ms = 0;
        cell_t start = { (int)floorf(units[i].gx), (int)floorf(units[i].gy) };
        int len = flow_path_find(map, field, start, units[i].path, MAX_PATH_CELLS,
                                 P_MobjRadius(&units[i]));
        int row = selected_index / formation_columns;
        int row_start = row * formation_columns;
        int row_count = selected_count - row_start;
        if (row_count > formation_columns) row_count = formation_columns;
        int col = selected_index - row_start;
        float spacing = P_MobjRadius(&units[i]) * 2.1f;
        float offset_x = ((float)col - ((float)row_count - 1.0f) * 0.5f) * spacing;
        float offset_y = ((float)row - ((float)formation_rows - 1.0f) * 0.5f) * spacing;
        units[i].move_goal_gx = goal_gx + offset_x;
        units[i].move_goal_gy = goal_gy + offset_y;
        if (!P_CheckPosition(map, &units[i], units[i].move_goal_gx, units[i].move_goal_gy) ||
            position_overlaps_reserved_goal(units, unit_count, i,
                                            units[i].move_goal_gx, units[i].move_goal_gy,
                                            P_MobjRadius(&units[i]), order_id)) {
            float adjusted_gx = (float)goal.x + 0.5f;
            float adjusted_gy = (float)goal.y + 0.5f;
            if (!find_nearest_unreserved_walkable_position(map, units, unit_count, i, order_id,
                                                           adjusted_gx, adjusted_gy,
                                                           P_MobjRadius(&units[i]), 8,
                                                           &adjusted_gx, &adjusted_gy)) {
                find_nearest_walkable_position(map, adjusted_gx, adjusted_gy,
                                               P_MobjRadius(&units[i]), 8,
                                               &adjusted_gx, &adjusted_gy);
            }
            units[i].move_goal_gx = adjusted_gx;
            units[i].move_goal_gy = adjusted_gy;
        }
        if (len == 1 && hypotf(units[i].move_goal_gx - units[i].gx,
                               units[i].move_goal_gy - units[i].gy) > 0.05f &&
            MAX_PATH_CELLS > 1) {
            units[i].path[1] = units[i].path[0];
            len = 2;
        }
        units[i].path_len = len;
        units[i].path_index = len > 1 ? 1 : 0;
        units[i].move_order_arrived = units[i].path_index == 0;
        selected_index++;
    }
    free(field);
}

static int find_resource_vent_at(const level_t *map, float gx, float gy) {
    if (!map || !map->resource_vents || map->resource_vent_count <= 0) return -1;
    int cell_x = (int)floorf(gx);
    int cell_y = (int)floorf(gy);
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!vent->active || vent->rate <= 0 || vent->amount <= 0) continue;
        if (vent->gx == cell_x && vent->gy == cell_y) return i;
    }

    int best = -1;
    float best_dist2 = 1.45f * 1.45f;
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!vent->active || vent->rate <= 0 || vent->amount <= 0) continue;
        float dx = vent->attach_gx - gx;
        float dy = vent->attach_gy - gy;
        float dist2 = dx * dx + dy * dy;
        if (dist2 < best_dist2) {
            best_dist2 = dist2;
            best = i;
        }
    }
    return best;
}

bool P_HarvestOrderAt(const level_t *map, mobj_t *units, int unit_count,
                                   float gx, float gy) {
    int vent_index = find_resource_vent_at(map, gx, gy);
    if (vent_index < 0) return false;

    bool has_harvester = false;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].selected && units[i].owner == 0 && units[i].hp > 0 &&
            (units[i].traits & (MF_MOBILE | MF_HARVESTER)) ==
                (MF_MOBILE | MF_HARVESTER)) {
            has_harvester = true;
            break;
        }
    }
    if (!has_harvester) return false;

    const resourcevent_t *vent = &map->resource_vents[vent_index];
    bool issued = false;
    uint32_t order_id = next_move_order_id();
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *unit = &units[i];
        if (!unit->selected || unit->owner != 0 || unit->hp <= 0) continue;
        if ((unit->traits & (MF_MOBILE | MF_HARVESTER)) !=
            (MF_MOBILE | MF_HARVESTER)) {
            continue;
        }

        float goal_gx = vent->attach_gx;
        float goal_gy = vent->attach_gy;
        if (!find_nearest_walkable_position(map, goal_gx, goal_gy,
                                            P_MobjRadius(unit), 8,
                                            &goal_gx, &goal_gy)) {
            cell_t fallback = { vent->gx, vent->gy };
            if (!find_nearest_walkable_cell(map, fallback, 8, &fallback)) continue;
            goal_gx = (float)fallback.x + 0.5f;
            goal_gy = (float)fallback.y + 0.5f;
        }

        cell_t goal = { (int)floorf(goal_gx), (int)floorf(goal_gy) };
        if (!find_nearest_walkable_cell(map, goal, 8, &goal)) continue;
        FlowCell *field = build_flow_field(map, goal);
        if (!field) continue;

        cell_t start = { (int)floorf(unit->gx), (int)floorf(unit->gy) };
        int len = flow_path_find(map, field, start, unit->path, MAX_PATH_CELLS,
                                 P_MobjRadius(unit));
        free(field);
        unit->move_goal_gx = goal_gx;
        unit->move_goal_gy = goal_gy;
        if (!P_CheckPosition(map, unit, unit->move_goal_gx, unit->move_goal_gy)) {
            float adjusted_gx = (float)goal.x + 0.5f;
            float adjusted_gy = (float)goal.y + 0.5f;
            find_nearest_walkable_position(map, adjusted_gx, adjusted_gy,
                                           P_MobjRadius(unit), 8,
                                           &adjusted_gx, &adjusted_gy);
            unit->move_goal_gx = adjusted_gx;
            unit->move_goal_gy = adjusted_gy;
        }
        if (len == 1 && hypotf(unit->move_goal_gx - unit->gx,
                               unit->move_goal_gy - unit->gy) > 0.05f &&
            MAX_PATH_CELLS > 1) {
            unit->path[1] = unit->path[0];
            len = 2;
        }
        unit->path_len = len;
        unit->path_index = len > 1 ? 1 : 0;
        unit->attack_target = -1;
        unit->harvest_target = vent_index;
        unit->harvest_timer_ms = 0;
        unit->move_order_id = order_id;
        unit->move_order_arrived = unit->path_index == 0;
        issued = true;
    }
    return issued;
}

void P_MoveOrder(const level_t *map, mobj_t *units, int unit_count, cell_t goal) {
    P_MoveOrderAt(map, units, unit_count, (float)goal.x + 0.5f, (float)goal.y + 0.5f);
}
