#include "sc_local.h"
#include <limits.h>
/* Larvae: the hatchery's spawn timer and a larva's own wandering, reimplemented
 * from the pinned OpenBW functions (names only):
 *   secondary_order_SpawningLarva  the spawn timer and the cap of three
 *   secondary_order_SpreadCreep    runs the same spawn code for a hatchery
 *   get_spawn_larva_position, spawn_larva
 *   order_Larva                    the wander, and death off creep
 * Both orders run only on the frame their unit's order_process_timer is 0, which
 * is once every nine frames (the main order sets it to 8 and counts it down),
 * so the 37-count timer is 38 runs of 9 frames: a larva every 342 frames.
 *
 * State, saved in the mobj: a hatchery's larva_ms is INT_MIN (no order) or
 * timer (bits 0-5, 0 to 37), run phase (bits 6-9, 0 to 8) and a morph mark
 * (bit 10, set while a building morph runs, so its end resets the timer as
 * reinitialize_unit_type does). A larva keeps its run phase (bits 0-3), the
 * side of the hatchery it lives on (bits 4-5, OpenBW's order_state), and its
 * move target (sc.order.at, valid with bit 6; bit 7 marks a trip back to the
 * hatchery) in sc.order, which a larva never otherwise uses.
 *
 * Differences from OpenBW that no trace here settles (see
 * docs/SC_CONSTRUCTION_READINESS.md):
 * - The run phase is 0 for every unit; OpenBW restarts every unit's phase from
 *   its place in the unit list every 150 frames, which cannot be reproduced
 *   without that list, so only the nine-frame period is exact.
 * - larva_spawn_side_values (worker returns bias the spawn side) is not kept:
 *   no hook tells this engine where a worker delivered, so every value is 0.
 * - A larva walks a straight line at its walking-script speed and stops exactly
 *   on its target; OpenBW's flingy movement, collision and the position a
 *   larva stops at beside its hatchery are not traced. */
enum { RUN_PERIOD = 9, TIMER_SPAWNED = 37, TIMER_BLOCKED = 3, CAP = 3, COUNT_REACH = 256, HATCHERY_RANGE = 10, WANDER = 10 };
enum { H_TIMER = 63, H_PHASE_SHIFT = 6, H_PHASE = 15 << 6, H_MORPH = 1 << 10 };
enum { L_PHASE = 15, L_SIDE_SHIFT = 4, L_SIDE = 3 << 4, L_TARGET = 1 << 6, L_HOME = 1 << 7 };
typedef struct { int x, y; } pt;
typedef struct { int left, top, right, bottom; } dims_t; /* units.dat box around the centre, inclusive */

static bool hatchery(uint16_t type) { return type == MT_HATCHERY || type == MT_LAIR || type == MT_HIVE; }
static bool live(const thinker_t *th) {
    const mobj_t *mo = (const mobj_t *)th;
    return th->function == P_MobjThinker && !mo->remove && mo->hp > 0;
}
bool sc_larva_state_valid(const mobj_t *mo) {
    int v = mo->sc.larva_ms;
    if (v != INT_MIN && (v < 0 || (v & ~(H_TIMER | H_PHASE | H_MORPH)) || (v & H_TIMER) > TIMER_SPAWNED ||
                         ((v & H_PHASE) >> H_PHASE_SHIFT) >= RUN_PERIOD)) return false;
    if (mo->type_id != MT_LARVA) return true;
    unsigned s = (unsigned)mo->sc.order.time;
    return !(s & ~0xffu) && (s & L_PHASE) < RUN_PERIOD;
}
static pt where(const mobj_t *mo) {
    return (pt){mo->core.position.x / (FIXED_ONE / 32), mo->core.position.y / (FIXED_ONE / 32)};
}
static void put(mobj_t *mo, pt p) {
    mo->core.position = fixed3_with_xy(mo->core.position, (fixed2_t){p.x * (FIXED_ONE / 32), p.y * (FIXED_ONE / 32)});
}
static dims_t dims(uint16_t type) {
    irect_t b = sc_units[type - 1].bounds;
    return (dims_t){-b.x, -b.y, b.x + b.w - 1, b.y + b.h - 1};
}
/* unit_sprite_inner_bounding_box and unit_sprite_bounding_box (one past). */
static void inner_box(pt p, dims_t d, pt *from, pt *to) { *from = (pt){p.x - d.left, p.y - d.top}; *to = (pt){p.x + d.right, p.y + d.bottom}; }
/* xy_length: OpenBW's integer approximation of a vector's length. */
static int length(int x, int y) {
    unsigned a = (unsigned)abs(x), b = (unsigned)abs(y);
    if (a < b) { unsigned t = a; a = b; b = t; }
    if (a / 4 < b) a = a - a / 16 + b * 3 / 8 - a / 64 + b * 3 / 256;
    return (int)a;
}
static int gap(int from_a, int to_a, int from_b, int to_b) {
    return from_a > to_b ? from_a - to_b : from_b > to_a ? from_b - to_a : 0;
}
/* units_distance between a larva and its hatchery. */
static int distance(const mobj_t *larva, const mobj_t *h) {
    pt a0, a1, b0, b1;
    inner_box(where(larva), dims(larva->type_id), &a0, &a1);
    inner_box(where(h), dims(h->type_id), &b0, &b1);
    return length(gap(a0.x, a1.x + 1, b0.x, b1.x + 1), gap(a0.y, a1.y + 1, b0.y, b1.y + 1));
}
static bool creep_at(pt p) {
    return p.x >= 0 && p.y >= 0 && sc_creep_at((ivec2_t){p.x / 32, p.y / 32});
}
static bool in_map(pt p) { return p.x >= 0 && p.y >= 0 && p.x < level.width * 32 && p.y < level.height * 32; }
/* restrict_move_target_to_valid_bounds leaves the position alone: the box
 * stays inside the map, whose bottom 32 pixels are not usable. */
static bool inside(pt p, dims_t d) {
    return p.x - d.left >= 0 && p.x + d.right + 1 < level.width * 32 && p.y - d.top >= 0 &&
        p.y + d.bottom + 1 < level.height * 32 - 32;
}
static bool tile_walkable(ivec2_t c) {
    const sc_mission_t *m = level.mission;
    if (!m || !m->terrain || !L_Contains(&level, c.x, c.y)) return true;
    const sc_terrain_t *t = &m->terrain[L_Index(&level, c.x, c.y)];
    for (int i = 0; i < 16; i++) if (!(t->vf4[i] & 1)) return false;
    return true;
}
static bool tile_occupied(ivec2_t c) { return L_Contains(&level, c.x, c.y) && (level.cell_solid[L_Index(&level, c.x, c.y)] & 3); }

typedef struct { int best; pt at; bool found; dims_t larva; } spawn_t;
/* The test lambda of get_spawn_larva_position. val is the side's
 * larva_spawn_side_values entry, always 0 here. */
static bool test_side(spawn_t *s, pt pos, pt neighbor) {
    int val = 0;
    if (val >= s->best || !inside(pos, s->larva) || !creep_at(pos)) return false;
    pt op = {pos.x + neighbor.x, pos.y + neighbor.y};
    if (!inside(op, s->larva) || !in_map(op)) return false;
    ivec2_t cell = {op.x / 32, op.y / 32};
    if (tile_occupied(cell)) return false;
    if (!tile_walkable(cell) && !sc_creep_at(cell)) return false;
    s->best = val;
    s->at = pos;
    s->found = true;
    return true;
}
static bool spawn_position(const mobj_t *h, pt *out) {
    spawn_t s = {.best = 101, .larva = dims(MT_LARVA)};
    dims_t d = dims(h->type_id);
    pt c = where(h), from, to;
    inner_box(c, d, &from, &to);
    test_side(&s, (pt){c.x, to.y + 10}, (pt){0, 22});
    test_side(&s, (pt){from.x - 10, c.y}, (pt){-22, 0});
    test_side(&s, (pt){to.x + 10, c.y}, (pt){22, 0});
    test_side(&s, (pt){c.x, from.y - 10}, (pt){0, -22});
    if (!s.found) {
        int w = (d.left + d.right + 1 + 1) / 2, hh = (d.top + d.bottom + 1 + 1) / 2;
        s.best = 0x10000;
        if (!test_side(&s, (pt){c.x - w, to.y + 10}, (pt){0, 22}) && !test_side(&s, (pt){c.x + w, to.y + 10}, (pt){0, 22})) s.best = 101;
        if (!test_side(&s, (pt){from.x - 10, c.y - hh}, (pt){-22, 0})) test_side(&s, (pt){from.x - 10, c.y + hh}, (pt){-22, 0});
        if (!test_side(&s, (pt){to.x + 10, c.y - hh}, (pt){22, 0})) test_side(&s, (pt){to.x + 10, c.y + hh}, (pt){22, 0});
        if (!test_side(&s, (pt){c.x - w, from.y - 10}, (pt){0, -22})) test_side(&s, (pt){c.x + w, from.y - 10}, (pt){0, -22});
    }
    *out = s.at;
    return s.found;
}
/* spawn_larva: the larva belongs to its hatchery and lives on the side it was
 * created on. */
mobj_t *sc_spawn_larva(mobj_t *h) {
    pt at;
    if (!spawn_position(h, &at)) return NULL;
    mobj_t *larva = sc_spawn_actor(MT_LARVA - 1, (ivec2_t){at.x, at.y}, (uint8_t)h->owner);
    if (!larva) return NULL;
    larva->team = h->team;
    larva->allegiance = h->allegiance;
    larva->sc.parent = h->id;
    dims_t d = dims(h->type_id);
    pt c = where(h), p = where(larva);
    int side = p.x < c.x - d.left ? 0 : p.y < c.y - d.top ? 1 : p.x > c.x + d.right ? 2 : 3;
    larva->sc.order = (sc_order_t){.time = side << L_SIDE_SHIFT};
    return larva;
}

static int larvae_near(const mobj_t *h) {
    pt c = where(h);
    int count = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (!live(th) || mo->type_id != MT_LARVA || mo->sc.parent != h->id) continue;
        pt p = where(mo);
        count += abs(p.x - c.x) <= COUNT_REACH && abs(p.y - c.y) <= COUNT_REACH;
    }
    return count;
}
/* secondary_order_SpawningLarva; v is the hatchery's packed state. */
static void spawn_run(mobj_t *h, int *v) {
    if (h->owner == 11 || larvae_near(h) >= CAP) return;
    if (*v & H_TIMER) { --*v; return; }
    *v |= sc_spawn_larva(h) ? TIMER_SPAWNED : TIMER_BLOCKED;
}
static void hatchery_frame(mobj_t *h) {
    int v = h->sc.larva_ms;
    if (v == INT_MIN || !G_ModelUnitReady(h)) return;
    bool morphing = h->production && h->production->product_class == RTS_PRODUCT_BUILDING;
    if (morphing) v |= H_MORPH;
    else if (v & H_MORPH) v &= ~(H_MORPH | H_TIMER); /* reinitialize_unit_type: larva_timer = 0 */
    int phase = (v & H_PHASE) >> H_PHASE_SHIFT;
    if (!phase) spawn_run(h, &v);
    phase = (phase + 1) % RUN_PERIOD;
    h->sc.larva_ms = (v & ~H_PHASE) | phase << H_PHASE_SHIFT;
}

/* The nearest position on the hatchery's box grown by the larva's own extent:
 * where a larva walking to its hatchery is stopped. */
static pt home_target(const mobj_t *larva, const mobj_t *h) {
    dims_t d = dims(h->type_id), l = dims(MT_LARVA);
    pt c = where(h), p = where(larva), from, to;
    inner_box(c, d, &from, &to);
    from.x -= l.right + 1; from.y -= l.bottom + 1;
    to.x += l.left + 1; to.y += l.top + 1;
    if (p.x < from.x) p.x = from.x; else if (p.x > to.x) p.x = to.x;
    if (p.y < from.y) p.y = from.y; else if (p.y > to.y) p.y = to.y;
    return p;
}
static bool on_side(int side, pt p, pt from, pt to) {
    return side == 0 ? p.x <= from.x : side == 1 ? p.y <= from.y : side == 2 ? p.x >= to.x : p.y >= to.y;
}
/* order_Larva. */
static void larva_order(mobj_t *l) {
    unsigned s = (unsigned)l->sc.order.time;
    pt pos = where(l), target = (s & L_TARGET) ? (pt){l->sc.order.at.x, l->sc.order.at.y} : pos;
    mobj_t *h = P_MobjById(l->sc.parent);
    if (h && (h->remove || h->hp <= 0 || !hatchery(h->type_id))) h = NULL;
    if (h && distance(l, h) > HATCHERY_RANGE && !(s & L_HOME)) {
        target = home_target(l, h);
        s |= L_TARGET | L_HOME;
    }
    bool arrived = pos.x == target.x && pos.y == target.y;
    pt hc = h ? where(h) : (pt){0, 0};
    if (arrived || ((s & L_HOME) && h && !creep_at(target) && (target.x != hc.x || target.y != hc.y))) {
        if ((!h || !(s & L_HOME)) && !creep_at(pos)) { P_DamageMobj(l, NULL, l->hp); return; }
        unsigned rv = sc_random();
        pt next = {pos.x + ((rv & 8) ? 10 : -10), pos.y + ((rv & 0x80) ? 10 : -10)};
        if (in_map(next) && creep_at(next)) {
            bool go = true;
            if (h) {
                pt from, to;
                inner_box(hc, dims(h->type_id), &from, &to);
                int side = (int)((s & L_SIDE) >> L_SIDE_SHIFT);
                if (!on_side(side, next, from, to)) {
                    if (on_side(side, pos, from, to)) go = false;
                    else next = side == 0 ? (pt){from.x - 10, hc.y} : side == 1 ? (pt){hc.x, from.y - 10} :
                                side == 2 ? (pt){to.x + 10, hc.y} : (pt){hc.x, to.y + 10};
                }
            }
            if (go) { target = next; s = (s | L_TARGET) & ~(unsigned)L_HOME; }
        }
    }
    l->sc.order.at = (ivec2_t){target.x, target.y};
    l->sc.order.time = (int)s;
}
static void larva_step(mobj_t *l) {
    unsigned s = (unsigned)l->sc.order.time;
    if (!(s & L_TARGET)) return;
    pt p = where(l), t = {l->sc.order.at.x, l->sc.order.at.y};
    int dx = t.x - p.x, dy = t.y - p.y, step = sc_units[MT_LARVA - 1].speed / 256, dist = length(dx, dy);
    if (dist <= step) p = t;
    else { p.x += dx * step / dist; p.y += dy * step / dist; }
    put(l, p);
}
static void larva_frame(mobj_t *l) {
    if (l->production) return;
    larva_step(l);
    int phase = l->sc.order.time & L_PHASE;
    if (!phase) larva_order(l);
    l->sc.order.time = (l->sc.order.time & ~L_PHASE) | (phase + 1) % RUN_PERIOD;
}

void sc_larva_ticker(void) {
    int frames = leveltime * 24 / RTS_TICRATE - (leveltime - 1) * 24 / RTS_TICRATE;
    for (int f = 0; f < frames; f++)
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
            if (!live(th)) continue;
            mobj_t *mo = (mobj_t *)th;
            if (hatchery(mo->type_id)) hatchery_frame(mo);
            else if (mo->type_id == MT_LARVA) larva_frame(mo);
        }
}
