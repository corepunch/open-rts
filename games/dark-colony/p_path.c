#include "dark-colony.h"
#include "engine.h"
#include <stddef.h>

/* DC.EXE path.c: +00 stamp, +04 score (0 closed, -1 unseen), +08 cell,
 * +0a parent, +0c family, +0d adjustment, +10/+14 bucket links.
 * Indices replace 32-bit pointers so the record stays 0x18 bytes on this host. */
typedef struct {
    uint32_t stamp;
    int32_t cost;
    uint8_t x, y, parent_x, parent_y, family, adjustment;
    uint16_t padding;
    int32_t next, prev;
} dc_pathcell_t;
_Static_assert(sizeof(dc_pathcell_t) == 0x18, "DC path record");
_Static_assert(offsetof(dc_pathcell_t, family) == 0x0c, "DC path family");

struct dc_pathmap_s {
    blob_t file;
    dc_pathcell_t *cells;
    uint32_t stamp;
};

/* 0x4759a8: row = sign(target-current) in a 3x3 grid; column = neighbor. */
static const int costs[9][9] = {
    { 1,10,20,10,90,50,20,50,70},
    {10, 1,10,20,90,20,70,50,70},
    {20,10, 1,50,90,10,70,50,20},
    {10,20,50, 1,90,70,10,20,50},
    {80,80,80,80,90,80,80,80,80},
    {50,20,10,70,90, 1,50,20,10},
    {20,50,70,10,90,50, 1,10,20},
    {70,50,70,20,90,20,10, 1,10},
    {70,50,20,50,90,10,20,10, 1},
};
static const ivec2_t neighbors[9] = {
    {-1,-1}, {0,-1}, {1,-1}, {-1,0}, {0,0}, {1,0}, {-1,1}, {0,1}, {1,1}
};
/* Preserve insertion order and LIFO ties from 0x43fcbf..0x440841. */
static const int expansion[8] = {5,8,2,3,0,6,7,1};

static bool init_paths(level_t *map) {
    if (map->paths) return true;
    if (map->width <= 0 || map->height <= 0 || map->width > 256 || map->height > 256)
        return false;
    struct dc_pathmap_s *paths = calloc(1, sizeof(*paths));
    if (!paths) return false;
    paths->cells = calloc((size_t)map->width * map->height, sizeof(*paths->cells));
    if (!paths->cells) { free(paths); return false; }
    map->paths = paths;
    return true;
}

bool DC_LoadPaths(level_t *map, const char *path) {
    if (!init_paths(map)) return false;
    blob_t file = {0};
    if (!W_ReadFile(path, &file)) return false;
    if (file.size != 0x10000 + (size_t)map->width * map->height) {
        fprintf(stderr, "%s: invalid Dark Colony PTH size\n", path);
        W_FreeFile(&file);
        return false;
    }
    W_FreeFile(&map->paths->file);
    map->paths->file = file;
    return true;
}

void DC_FreePaths(level_t *map) {
    if (!map->paths) return;
    W_FreeFile(&map->paths->file);
    free(map->paths->cells);
    free(map->paths);
    map->paths = NULL;
}

static uint8_t family(const level_t *map, ivec2_t cell) {
    if (!L_Contains(map, cell.x, cell.y)) return 255;
    if (map->paths && map->paths->file.bytes)
        return map->paths->file.bytes[0x10000 + L_Index(map, cell.x, cell.y)];
    /* Procedural/test levels have no native PTH; one family is sufficient. */
    return L_IsWalkable(map, cell.x, cell.y) ? 1 : 0;
}

ivec2_t DC_OccupiedPosition(const mobj_t *unit) {
    /* 0x414f8b claims the destination; 0x4117fc releases the origin before
     * displacement. Occupancy follows the step, not the drawn position. */
    return unit->route.traveling ? unit->route.cells[unit->route.current] :
           fvec2_cell(fixed3_xy_to_fvec2(unit->core.position));
}

mobj_t *DC_Occupant(ivec2_t cell, bool airborne, bool buried) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (unit->remove || unit->hp <= 0 || (unit->traits & (MF_NOBLOCKMAP | MF_MISSILE)) ||
            !!(unit->traits & MF_FLY) != airborne ||
            !!(unit->traits & MF_LANDMINE) != buried) continue;
        if (ivec2_equal(cell, DC_OccupiedPosition(unit))) return unit;
    }
    return NULL;
}

static bool occupied_cell(const mobj_t *unit, ivec2_t cell) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *other = (const mobj_t *)th;
        if (other == unit || other->remove || other->hp <= 0 ||
            (other->traits & (MF_NOBLOCKMAP | MF_MISSILE)) ||
            ((other->traits ^ (unit ? unit->traits : 0)) & MF_FLY)) continue;
        if (ivec2_equal(cell, DC_OccupiedPosition(other))) return true;
    }
    return false;
}

int DC_FindPath(const level_t *map, ivec2_t start, ivec2_t goal,
                const mobj_t *mover, bool occupied, ivec2_t *route, int capacity) {
    if (!map || !route || capacity <= 0 || !L_Contains(map, start.x, start.y) ||
        !L_Contains(map, goal.x, goal.y) || !init_paths((level_t *)map)) return 0;
    struct dc_pathmap_s *paths = map->paths;
    bool flying = mover && (mover->traits & MF_FLY);
    if (flying && !occupied) {
        /* 0x440ac0 builds axial then diagonal parents backwards: forward
         * flight is diagonal first, followed by the remaining major axis. */
        int count = 0;
        while (count < capacity && !ivec2_equal(start, goal)) {
            ivec2_t step = {(goal.x > start.x) - (goal.x < start.x),
                            (goal.y > start.y) - (goal.y < start.y)};
            start = ivec2_add(start, step);
            route[count++] = start;
        }
        return count;
    }
    uint8_t allowed[256] = {0};
    int from_family = family(map, goal), to_family = family(map, start);
    if ((!from_family && !flying) || from_family == 255) return 0;
    /* 0x440dc4 follows PTH[current_family][destination_family], backwards
     * from the order's destination. 0x4409d8 local detours allow all families. */
    if (occupied || !paths->file.bytes || !to_family) {
        memset(allowed + 1, 1, 254);
        allowed[0] = flying;
    } else {
        for (int i = 0; i < 256; ++i) {
            if (!from_family || from_family == 255 || allowed[from_family]) return 0;
            allowed[from_family] = 1;
            if (from_family == to_family) break;
            from_family = paths->file.bytes[from_family * 256 + to_family];
        }
        if (!allowed[to_family]) return 0;
    }
    int total = map->width * map->height;
    if (++paths->stamp == 0) {
        memset(paths->cells, 0, (size_t)total * sizeof(*paths->cells));
        ++paths->stamp;
    }
    uint8_t *blocked = NULL;
    if (occupied) {
        blocked = calloc((size_t)total, 1);
        if (!blocked) return 0;
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            const mobj_t *other = (const mobj_t *)th;
            if (other == mover || other->remove || other->hp <= 0 ||
                (other->traits & (MF_NOBLOCKMAP | MF_MISSILE)) ||
                ((other->traits ^ (mover ? mover->traits : 0)) & MF_FLY)) continue;
            ivec2_t cell = DC_OccupiedPosition(other);
            if (L_Contains(map, cell.x, cell.y)) blocked[L_Index(map, cell.x, cell.y)] = 1;
        }
        if (blocked[L_Index(map, goal.x, goal.y)]) { free(blocked); return 0; }
    }
    int heads[256];
    for (int i = 0; i < 256; ++i) heads[i] = -1;
    int current = L_Index(map, goal.x, goal.y), bucket = 0, distance = 0;
    paths->cells[current] = (dc_pathcell_t){.stamp = paths->stamp, .cost = 0,
        .x = goal.x, .y = goal.y, .parent_x = goal.x, .parent_y = goal.y,
        .next = -1, .prev = -1};
    int expanded = 0;
    for (;;) {
        dc_pathcell_t *node = &paths->cells[current];
        ivec2_t cell = {node->x, node->y};
        int row = (start.x > cell.x) - (start.x < cell.x) + 1 +
                  3 * ((start.y > cell.y) - (start.y < cell.y) + 1);
        bool pass[9] = {0};
        for (int d = 0; d < 9; ++d) {
            ivec2_t next = ivec2_add(cell, neighbors[d]);
            if (!L_Contains(map, next.x, next.y)) continue;
            pass[d] = allowed[family(map, next)] &&
                      (!blocked || !blocked[L_Index(map, next.x, next.y)]);
            /* Preserve the engine's authored blocked-spawn escape. Retail's
             * main search asserts nonzero families at both endpoints. */
            if (!to_family && ivec2_equal(next, start)) pass[d] = true;
        }
        for (int i = 0; i < 8; ++i) {
            int d = expansion[i];
            ivec2_t step = neighbors[d];
            if (!pass[d] || (step.x && step.y &&
                !pass[4 + step.x] && !pass[4 + step.y * 3])) continue;
            ivec2_t next = ivec2_add(cell, step);
            int index = L_Index(map, next.x, next.y);
            dc_pathcell_t *dest = &paths->cells[index];
            int cost = node->cost + costs[row][d];
            if (dest->stamp != paths->stamp) {
                *dest = (dc_pathcell_t){.stamp = paths->stamp, .cost = -1,
                    .x = next.x, .y = next.y, .family = family(map, next), .next = -1, .prev = -1};
            } else if (dest->cost == 0 || (dest->cost != -1 && dest->cost < cost)) continue;
            if (dest->cost != -1) {
                if (dest->prev == -1) heads[(dest->cost - distance + bucket) & 255] = dest->next;
                else paths->cells[dest->prev].next = dest->next;
                if (dest->next != -1) paths->cells[dest->next].prev = dest->prev;
            }
            int slot = (cost - distance + bucket) & 255;
            dest->next = heads[slot];
            dest->prev = -1;
            if (dest->next != -1) paths->cells[dest->next].prev = index;
            heads[slot] = index;
            dest->cost = cost;
            dest->parent_x = cell.x;
            dest->parent_y = cell.y;
        }
        node->cost = 0;
        if (node->next != -1) paths->cells[node->next].prev = -1;
        if (heads[bucket] == current) heads[bucket] = node->next;
        if (ivec2_equal(cell, start)) break;
        /* The seed expansion precedes the native local search's 256 pops. */
        if (occupied && expanded++ == 256) break;
        int scanned = 0;
        while (heads[bucket] == -1 && scanned++ < 256) {
            bucket = (bucket + 1) & 255;
            ++distance;
        }
        if (heads[bucket] == -1) break;
        current = heads[bucket];
    }
    free(blocked);
    int count = 0;
    dc_pathcell_t *node = &paths->cells[L_Index(map, start.x, start.y)];
    if (node->stamp != paths->stamp) return 0;
    while (count < capacity && !ivec2_equal(start, goal)) {
        start = (ivec2_t){node->parent_x, node->parent_y};
        route[count++] = start;
        node = &paths->cells[L_Index(map, start.x, start.y)];
    }
    return count;
}

bool DC_CheckStep(const level_t *map, const mobj_t *unit, fvec2_t from, fvec2_t to) {
    ivec2_t a = fvec2_cell(from), b = fvec2_cell(to), delta = ivec2_sub(b, a);
    if (!L_Contains(map, b.x, b.y)) return false;
    if (ivec2_equal(a, b)) return true;
    if (abs(delta.x) > 1 || abs(delta.y) > 1 || occupied_cell(unit, b)) return false;
    if (unit->traits & MF_FLY) return true;
    if (!family(map, b) || family(map, b) == 255) return false;
    return !delta.x || !delta.y ||
        (family(map, ivec2_add(a, (ivec2_t){delta.x,0})) != 0) ||
        (family(map, ivec2_add(a, (ivec2_t){0,delta.y})) != 0);
}

bool DC_MoveUnitTo(const level_t *map, mobj_t *unit, fvec2_t goal) {
    bool flying = unit->traits & MF_FLY;
    ivec2_t wanted = fvec2_cell(goal), chosen = wanted;
    if (wanted.x < 0) wanted.x = 0;
    if (wanted.y < 0) wanted.y = 0;
    if (wanted.x >= map->width) wanted.x = map->width - 1;
    if (wanted.y >= map->height) wanted.y = map->height - 1;
    if (flying && map->height >= 3 && wanted.y > map->height - 3)
        wanted.y = map->height - 3;
    chosen = wanted;
    bool found = false;
    /* 0x414133..0x4141e3: expanding squares, X outer / Y inner. */
    for (int radius = 0; radius < 256 && !found; ++radius) {
        for (int x = wanted.x - radius; x <= wanted.x + radius && !found; ++x) {
            for (int y = wanted.y - radius; y <= wanted.y + radius; ++y) {
                ivec2_t cell = {x,y};
                if (!L_Contains(map, x, y) ||
                    (!flying && (!family(map, cell) || family(map, cell) == 255))) continue;
                chosen = cell;
                found = true;
                break;
            }
        }
    }
    if (!found) return false;
    dc_route_t route = {0};
    fvec2_t position = fixed3_xy_to_fvec2(unit->core.position);
    ivec2_t start = fvec2_cell(position);
    int prefix = !ivec2_equal(start, chosen) &&
                 !fvec2_near(position, fvec2_cell_center(start), 1.0f / FIXED_ONE);
    route.count = DC_FindPath(map, start, chosen, unit, false, route.cells + prefix, 32 - prefix);
    if (!route.count && !ivec2_equal(start, chosen)) return false;
    if (prefix) { route.cells[0] = start; ++route.count; }
    if (!route.count) route.cells[route.count++] = chosen;
    P_ClearMove(unit);
    unit->route = route;
    unit->movement.goal = ivec2_equal(chosen, fvec2_cell(goal)) ? goal : fvec2_cell_center(chosen);
    unit->movement.order_arrived = false;
    return true;
}

bool DC_MoveTarget(const level_t *map, mobj_t *unit, fvec2_t *target, bool *final) {
    dc_route_t *route = &unit->route;
    fvec2_t position = fixed3_xy_to_fvec2(unit->core.position);
    ivec2_t goal = fvec2_cell(unit->movement.goal);
    while (route->current < route->count) {
        ivec2_t cell = route->cells[route->current];
        *final = ivec2_equal(cell, goal);
        *target = *final ? unit->movement.goal : fvec2_cell_center(cell);
        if (*final || !fvec2_near(position, *target, 1.0f / FIXED_ONE)) break;
        route->traveling = false;
        ++route->current;
    }
    if (route->current == route->count) {
        route->current = 0;
        route->count = DC_FindPath(map, fvec2_cell(position), goal, unit, false, route->cells, 32);
        if (!route->count) return false;
        *final = ivec2_equal(route->cells[0], goal);
        *target = *final ? unit->movement.goal : fvec2_cell_center(route->cells[0]);
    }
    if (!ivec2_equal(fvec2_cell(position), fvec2_cell(*target)) &&
        occupied_cell(unit, fvec2_cell(*target))) {
        route->traveling = false;
        /* 0x414680 skips occupied route cells; 0x414418 splices a bounded
         * occupancy-aware detour to the first free cell ahead. */
        int resume = route->current;
        while (resume < route->count && occupied_cell(unit, route->cells[resume])) ++resume;
        if (resume == route->count) {
            /* 0x414809..0x414913: when every remaining cell is occupied,
             * perturb each destination axis by (random % 3 - 1) cells. */
            int shift_x = (int)(P_DC_Random() % 3) - 1;
            ivec2_t offset = {shift_x, (int)(P_DC_Random() % 3) - 1};
            ivec2_t shifted = ivec2_add(goal, offset);
            if (shifted.x < 0) shifted.x = 0;
            if (shifted.y < 0) shifted.y = 0;
            if (shifted.x >= map->width) shifted.x = map->width - 1;
            if (shifted.y >= map->height) shifted.y = map->height - 1;
            if (!DC_MoveUnitTo(map, unit, fvec2_cell_center(shifted))) return false;
            *target = position;
            *final = false;
            return true;
        }
        dc_route_t detour = {0};
        ivec2_t start = fvec2_cell(position);
        int prefix = !fvec2_near(position, fvec2_cell_center(start), 1.0f / FIXED_ONE);
        detour.count = DC_FindPath(map, start, route->cells[resume],
                                   unit, true, detour.cells + prefix, 32 - prefix);
        if (!detour.count) {
            *target = position; /* Retry next tic while another unit occupies the route. */
            *final = false;
            return true;
        }
        if (prefix) { detour.cells[0] = start; ++detour.count; }
        int remaining = route->count - resume - 1;
        if (detour.count + remaining < 32) {
            memcpy(detour.cells + detour.count, route->cells + resume + 1,
                   (size_t)remaining * sizeof(*route->cells));
            detour.count += remaining;
        }
        *route = detour;
        *final = ivec2_equal(route->cells[0], goal);
        *target = *final ? unit->movement.goal : fvec2_cell_center(route->cells[0]);
    }
    route->traveling = true;
    return true;
}
