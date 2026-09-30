#include "p_nav.h"
#include "engine.h"

/* Costs are integers so plans are identical on every host (lockstep play). */
enum { COST_STRAIGHT = 10, COST_DIAGONAL = 14, COST_NEAR_WALL = 3 };

struct nav_s {
    int width, height;
    uint8_t *snapshot;  /* blocked[] as last seen; a mismatch rebuilds the cache */
    uint8_t *near_wall; /* 1 if any of the eight neighbours is blocked */
    uint16_t *region;   /* connected component id, 0 = blocked */
    int *g, *parent;
    uint32_t *stamp;    /* generation of g/parent/closed, avoids clearing per search */
    uint8_t *closed;
    uint32_t generation;
    uint64_t *heap;
    int heap_count, heap_capacity;
};

static navstats_t stats;

navstats_t P_NavStats(void) { return stats; }

static const int8_t neighbor[8][2] = {
    {0,-1}, {1,0}, {0,1}, {-1,0}, {1,-1}, {1,1}, {-1,1}, {-1,-1},
};

static bool walkable(const level_t *map, int x, int y) {
    return L_IsWalkable(map, x, y);
}

static bool step_allowed(const level_t *map, int x, int y, int dx, int dy) {
    if (!walkable(map, x + dx, y + dy)) return false;
    /* Never cut a blocked corner. */
    return !dx || !dy || (walkable(map, x + dx, y) && walkable(map, x, y + dy));
}

static void nav_release(struct nav_s *nav) {
    if (!nav) return;
    free(nav->snapshot); free(nav->near_wall); free(nav->region);
    free(nav->g); free(nav->parent); free(nav->stamp); free(nav->closed); free(nav->heap);
    free(nav);
}

void P_NavFree(level_t *map) {
    if (!map) return;
    nav_release(map->nav);
    map->nav = NULL;
}

static void build_regions(const level_t *map, struct nav_s *nav) {
    int w = nav->width, h = nav->height, total = w * h;
    memset(nav->region, 0, (size_t)total * sizeof(*nav->region));
    int *queue = malloc((size_t)total * sizeof(*queue));
    if (!queue) return;
    uint16_t next = 1;
    for (int start = 0; start < total; ++start) {
        if (nav->region[start] || !walkable(map, start % w, start / w)) continue;
        if (next == UINT16_MAX) { /* Merge overflow into the last region; still sound for rejection. */
            nav->region[start] = next;
            continue;
        }
        int head = 0, tail = 0;
        queue[tail++] = start;
        nav->region[start] = next;
        while (head < tail) {
            int cur = queue[head++], x = cur % w, y = cur / w;
            for (int d = 0; d < 8; ++d) {
                int dx = neighbor[d][0], dy = neighbor[d][1];
                if (!step_allowed(map, x, y, dx, dy)) continue;
                int ni = (y + dy) * w + x + dx;
                if (nav->region[ni]) continue;
                nav->region[ni] = next;
                queue[tail++] = ni;
            }
        }
        ++next;
    }
    free(queue);
}

static bool nav_matches(const level_t *map, const struct nav_s *nav) {
    size_t total = (size_t)map->width * map->height;
    if (!map->blocked) {
        for (size_t i = 0; i < total; ++i) if (nav->snapshot[i]) return false;
        return true;
    }
    return memcmp(nav->snapshot, map->blocked, total) == 0;
}

/* The cache validates itself against blocked[], so buildings, destruction and
 * scripted terrain changes never need to remember to invalidate it. */
static struct nav_s *nav_for(const level_t *map) {
    if (!map || map->width <= 0 || map->height <= 0) return NULL;
    struct nav_s *nav = map->nav;
    size_t total = (size_t)map->width * map->height;
    if (nav && nav->width == map->width && nav->height == map->height && nav_matches(map, nav))
        return nav;
    if (!nav || nav->width != map->width || nav->height != map->height) {
        nav_release(nav);
        nav = calloc(1, sizeof(*nav));
        if (!nav) { ((level_t *)map)->nav = NULL; return NULL; }
        nav->width = map->width; nav->height = map->height;
        nav->snapshot = calloc(total, 1);
        nav->near_wall = calloc(total, 1);
        nav->region = calloc(total, sizeof(*nav->region));
        nav->g = calloc(total, sizeof(*nav->g));
        nav->parent = calloc(total, sizeof(*nav->parent));
        nav->stamp = calloc(total, sizeof(*nav->stamp));
        nav->closed = calloc(total, 1);
        if (!nav->snapshot || !nav->near_wall || !nav->region || !nav->g || !nav->parent ||
            !nav->stamp || !nav->closed) {
            nav_release(nav);
            ((level_t *)map)->nav = NULL;
            return NULL;
        }
        ((level_t *)map)->nav = nav;
    }
    if (map->blocked) memcpy(nav->snapshot, map->blocked, total);
    else memset(nav->snapshot, 0, total);
    for (int y = 0; y < nav->height; ++y)
        for (int x = 0; x < nav->width; ++x) {
            bool near = false;
            for (int d = 0; d < 8 && !near; ++d)
                near = L_Contains(map, x + neighbor[d][0], y + neighbor[d][1]) &&
                       !walkable(map, x + neighbor[d][0], y + neighbor[d][1]);
            nav->near_wall[y * nav->width + x] = near;
        }
    build_regions(map, nav);
    return nav;
}

/* Binary min-heap of (f, -g, index). Larger g wins ties, which keeps routes
 * straight instead of fanning out across equal-cost plateaus. */
static uint64_t heap_key(int f, int g, int index) {
    uint64_t tie = 0xFFFFF - (uint64_t)(g > 0xFFFFF ? 0xFFFFF : g);
    return ((uint64_t)f << 44) | (tie << 24) | (uint64_t)index;
}

static bool heap_push(struct nav_s *nav, uint64_t key) {
    if (nav->heap_count == nav->heap_capacity) {
        int capacity = nav->heap_capacity ? nav->heap_capacity * 2 : 1024;
        uint64_t *grown = realloc(nav->heap, (size_t)capacity * sizeof(*grown));
        if (!grown) return false;
        nav->heap = grown;
        nav->heap_capacity = capacity;
    }
    int i = nav->heap_count++;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (nav->heap[parent] <= key) break;
        nav->heap[i] = nav->heap[parent];
        i = parent;
    }
    nav->heap[i] = key;
    return true;
}

static uint64_t heap_pop(struct nav_s *nav) {
    uint64_t top = nav->heap[0], last = nav->heap[--nav->heap_count];
    int i = 0;
    for (;;) {
        int child = i * 2 + 1;
        if (child >= nav->heap_count) break;
        if (child + 1 < nav->heap_count && nav->heap[child + 1] < nav->heap[child]) ++child;
        if (nav->heap[child] >= last) break;
        nav->heap[i] = nav->heap[child];
        i = child;
    }
    if (nav->heap_count) nav->heap[i] = last;
    return top;
}

static int octile(int dx, int dy) {
    dx = abs(dx); dy = abs(dy);
    int lo = dx < dy ? dx : dy, hi = dx < dy ? dy : dx;
    return COST_STRAIGHT * hi + (COST_DIAGONAL - COST_STRAIGHT) * lo;
}

bool P_NavReachable(const level_t *map, ivec2_t from, ivec2_t to) {
    struct nav_s *nav = nav_for(map);
    if (!nav || !L_Contains(map, from.x, from.y) || !L_Contains(map, to.x, to.y)) return false;
    uint16_t a = nav->region[L_Index(map, from.x, from.y)];
    return a && a == nav->region[L_Index(map, to.x, to.y)];
}

bool P_NavNearestReachable(const level_t *map, ivec2_t from, ivec2_t wanted,
                           int radius, ivec2_t *out) {
    struct nav_s *nav = nav_for(map);
    if (!nav || !out || !L_Contains(map, from.x, from.y)) return false;
    uint16_t region = nav->region[L_Index(map, from.x, from.y)];
    if (!region) return false;
    int best = INT32_MAX;
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            ivec2_t c = {wanted.x + dx, wanted.y + dy};
            if (!L_Contains(map, c.x, c.y) || nav->region[L_Index(map, c.x, c.y)] != region)
                continue;
            int d = dx * dx + dy * dy;
            if (d < best) { best = d; *out = c; }
        }
    return best != INT32_MAX;
}

bool P_NavLineClear(const level_t *map, fvec2_t a, fvec2_t b, float radius) {
    fvec2_t delta = fvec2_sub(b, a);
    float length = sqrtf(fvec2_length_squared(delta));
    int steps = (int)ceilf(length / 0.25f);
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; ++i) {
        fvec2_t at = fvec2_add(a, fvec2_scale(delta, (float)i / (float)steps));
        /* The origin may overlap terrain (authored spawns); leaving it is allowed. */
        if (!P_MapCircleWalkable(map, at.x, at.y, radius, &a)) return false;
    }
    return true;
}

static bool cell_usable(const level_t *map, int x, int y, float radius) {
    if (!walkable(map, x, y)) return false;
    if (radius <= 0.5f) return true; /* A cell-centred disc that small fits any open cell. */
    fvec2_t c = fvec2_cell_center((ivec2_t){x, y});
    return P_MapCircleWalkable(map, c.x, c.y, radius, NULL);
}

static bool search(const level_t *map, struct nav_s *nav, float radius, ivec2_t start,
                   ivec2_t goal, int *goal_index) {
    int w = nav->width;
    if (++nav->generation == 0) {
        memset(nav->stamp, 0, (size_t)w * nav->height * sizeof(*nav->stamp));
        nav->generation = 1;
    }
    nav->heap_count = 0;
    int s = start.y * w + start.x, t = goal.y * w + goal.x;
    nav->stamp[s] = nav->generation;
    nav->g[s] = 0;
    nav->parent[s] = -1;
    nav->closed[s] = 0;
    ++stats.searches;
    if (!heap_push(nav, heap_key(octile(start.x - goal.x, start.y - goal.y), 0, s))) return false;
    while (nav->heap_count) {
        int cur = (int)(heap_pop(nav) & 0xFFFFFF);
        if (nav->closed[cur]) continue;
        nav->closed[cur] = 1;
        ++stats.expansions;
        if (cur == t) { *goal_index = cur; return true; }
        int x = cur % w, y = cur / w;
        for (int d = 0; d < 8; ++d) {
            int dx = neighbor[d][0], dy = neighbor[d][1];
            if (!step_allowed(map, x, y, dx, dy)) continue;
            int nx = x + dx, ny = y + dy, ni = ny * w + nx;
            if ((nx != goal.x || ny != goal.y) && !cell_usable(map, nx, ny, radius)) continue;
            int cost = nav->g[cur] + (dx && dy ? COST_DIAGONAL : COST_STRAIGHT) +
                       (nav->near_wall[ni] ? COST_NEAR_WALL : 0);
            if (nav->stamp[ni] != nav->generation) {
                nav->stamp[ni] = nav->generation;
                nav->closed[ni] = 0;
            } else if (cost >= nav->g[ni]) continue;
            nav->g[ni] = cost;
            nav->parent[ni] = cur;
            if (!heap_push(nav, heap_key(cost + octile(nx - goal.x, ny - goal.y), cost, ni)))
                return false;
        }
    }
    return false;
}

bool P_NavPlan(const level_t *map, float radius, fvec2_t from, fvec2_t goal, navpath_t *out) {
    if (!out) return false;
    *out = (navpath_t){0};
    struct nav_s *nav = nav_for(map);
    if (!nav) return false;
    ivec2_t start = fvec2_cell(from), end = fvec2_cell(goal);
    if (!L_Contains(map, start.x, start.y)) return false;
    if (!walkable(map, start.x, start.y)) { /* Spawn overlapping terrain: begin at a neighbour. */
        bool found = false;
        int best = INT32_MAX;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                if (!walkable(map, start.x + dx, start.y + dy)) continue;
                fvec2_t c = fvec2_cell_center((ivec2_t){start.x + dx, start.y + dy});
                int d = (int)(fvec2_distance_squared(c, from) * 256.0f);
                if (d < best) { best = d; found = true; out->points[0] = c; }
            }
        if (!found) return false;
        start = fvec2_cell(out->points[0]);
    }
    bool exact_goal = L_Contains(map, end.x, end.y) && walkable(map, end.x, end.y) &&
                      P_NavReachable(map, start, end) &&
                      P_MapCircleWalkable(map, goal.x, goal.y, radius, NULL);
    if (!exact_goal) {
        ivec2_t want = {end.x < 0 ? 0 : end.x >= map->width ? map->width - 1 : end.x,
                        end.y < 0 ? 0 : end.y >= map->height ? map->height - 1 : end.y};
        if (!P_NavNearestReachable(map, start, want, 16, &end)) return false;
        goal = fvec2_cell_center(end);
    }
    if (ivec2_equal(start, end)) {
        out->points[0] = out->goal = goal;
        out->count = 1;
        out->complete = true;
        return true;
    }
    int goal_index;
    if (!search(map, nav, radius, start, end, &goal_index)) return false;

    /* Walk parents backwards into a cell-centre list, swap the last for the exact goal. */
    int length = 0, w = nav->width;
    for (int c = goal_index; c != -1; c = nav->parent[c]) ++length;
    fvec2_t *chain = malloc((size_t)length * sizeof(*chain));
    if (!chain) return false;
    int i = length;
    for (int c = goal_index; c != -1; c = nav->parent[c])
        chain[--i] = fvec2_cell_center((ivec2_t){c % w, c / w});
    chain[length - 1] = goal;
    out->goal = goal;

    /* Greedy string pull: extend the segment while the disc still fits. */
    fvec2_t anchor = from;
    int at = 0;
    out->complete = true;
    while (at < length) {
        int far = at;
        while (far + 1 < length && P_NavLineClear(map, anchor, chain[far + 1], radius)) ++far;
        if (out->count == NAV_MAX_WAYPOINTS) { out->complete = false; break; }
        out->points[out->count++] = chain[far];
        anchor = chain[far];
        at = far + 1;
    }
    free(chain);
    return out->count > 0;
}

int P_FindPath(const level_t *map, cell_t start, cell_t goal, cell_t *out_path, int max_path) {
    if (!out_path || max_path <= 0) return 0;
    struct nav_s *nav = nav_for(map);
    if (!nav || !walkable(map, start.x, start.y) || !L_Contains(map, goal.x, goal.y)) return 0;
    if (!walkable(map, goal.x, goal.y) && !P_NavNearestReachable(map, start, goal, 8, &goal)) return 0;
    if (!P_NavReachable(map, start, goal)) return 0;
    if (ivec2_equal(start, goal)) { out_path[0] = start; return 1; }
    int index;
    if (!search(map, nav, 0.0f, start, goal, &index)) return 0;
    int length = 0;
    for (int c = index; c != -1; c = nav->parent[c]) ++length;
    if (length > max_path) length = max_path;
    int total = 0;
    for (int c = index; c != -1; c = nav->parent[c]) ++total;
    int slot = total;
    for (int c = index; c != -1; c = nav->parent[c]) {
        --slot;
        if (slot < max_path) out_path[slot] = (cell_t){c % nav->width, c / nav->width};
    }
    return length;
}
