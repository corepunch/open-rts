#include "sc_local.h"
#include <stdlib.h>
#include <string.h>
/* StarCraft creep as per-map tile state, reimplemented from the pinned OpenBW
 * source (bwgame.h function names, no line numbers):
 *   spread_creep / spread_creep_completely  growth and the game-start state
 *   secondary_order_SpreadCreep             one spread every 16 frames
 *   set_creep_building_tiles                footprint (and the Hatchery ring)
 *   add_creep_provider / remove_creep_provider / creep_is_being_sustained
 *   recede_creep, set_creep_receding, set_tile_creep (the nine neighbour lists)
 * Randomness is lcg_rand(26) for a spread and lcg_rand(27, 0, n - 1) for a
 * recession step; both draw from the one LCG, which is sc_random here.
 *
 * Differences from OpenBW that cannot be removed without a retail trace:
 * - A building is noticed once per engine tic (creep_ticker syncs the tracked
 *   Zerg buildings with the world) instead of in the unit's own update, so a
 *   spread or a recession starts at the first native frame of the tic after
 *   the completion or the death.
 * - OpenBW stops with an error when remove_creep_provider runs out of its
 *   1024 recession entries (status_flag_4000 in update_dead_unit, and
 *   check_dead_unit_timer is only ever written). There is no retry path to
 *   copy: here the tiles that did not fit simply stay.
 * - The random draws share the stream with the other local users of
 *   sc_random, whose call order differs from OpenBW's unit order. */
enum {
    POOL = 1024, TRACKED = 1024, BUCKET = 288, SPREAD_PERIOD = 15,
    TILE_CREEP = 1, TILE_RECEDING = 2,
    TRACK_DONE = 1, TRACK_MORPH = 2,
    ORDER_NONE = 0, ORDER_SPREAD = 1, ORDER_DONE = 2,
    FLAG_REQUIRES_CREEP = 0x20000
};
#define FLAG_PRODUCES 0x80000000u
/* A live Zerg building the creep follows: its id, position in pixels, and the
 * SpreadCreep secondary order (ORDER_*, timer counting 16 frames down). */
typedef struct { uint32_t id; uint16_t type, px, py; uint8_t flags, order, timer, pad0; uint16_t pad1; } track_t;
struct sc_creep {
    uint32_t w, h;
    int32_t recede_timer;
    uint32_t count[9];
    uint32_t tracked;
    uint32_t list[9][POOL]; /* index 0 is the front (OpenBW push_front) */
    track_t track[TRACKED];
    uint8_t tile[];
};
typedef struct { uint32_t id; uint16_t type; int px, py, tw, th; bool zerg, completed, morph; } snap_t;
typedef struct { snap_t *a; int n; } snaps_t;
typedef struct { int x0, y0, x1, y1; } area_t;

static bool spreads_type(int type, bool completed) {
    return type == MT_LAIR || type == MT_HIVE || type == MT_SPORE_COLONY || type == MT_SUNKEN_COLONY ||
        (completed && (type == MT_HATCHERY || type == MT_CREEP_COLONY));
}
/* order_InitCreepGrowth is the idle order of exactly these four. */
static bool starts_growth(int type) {
    return type == MT_HATCHERY || type == MT_LAIR || type == MT_HIVE || type == MT_CREEP_COLONY;
}
static bool valid_type(int type) { return type >= 1 && type <= SC_TYPES; }
static bool zerg_building(int type) {
    return valid_type(type) && (sc_units[type - 1].flags & 1) && (sc_units[type - 1].race & 1);
}
static bool requires_creep(int type) { return (sc_units[type - 1].flags & FLAG_REQUIRES_CREEP) != 0; }

void sc_creep_free(sc_creep_t *c) { free(c); }
void sc_creep_reset(sc_mission_t *m) {
    if (!m) return;
    free(m->creep);
    m->creep = NULL;
}

static bool strip_tile(const sc_creep_t *c, int x, int y) {
    return y == (int)c->h - 1 || (y == (int)c->h - 2 && (x < 5 || x >= (int)c->w - 5));
}
static const sc_terrain_t *terrain_at(const sc_creep_t *c, int x, int y) {
    const sc_mission_t *m = level.mission;
    return m && m->terrain ? &m->terrain[(size_t)y * c->w + (size_t)x] : NULL;
}
/* The tile flag_unbuildable: the CV5 bit, or the map's bottom strips. */
static bool unbuildable(const sc_creep_t *c, int x, int y) {
    const sc_terrain_t *t = terrain_at(c, x, y);
    return strip_tile(c, x, y) || (t && (t->cv5 & SC_TERRAIN_UNBUILDABLE));
}
static bool partially_walkable(const sc_creep_t *c, int x, int y) {
    const sc_terrain_t *t = terrain_at(c, x, y);
    if (!t || strip_tile(c, x, y)) return false;
    int walkable = 0;
    for (int i = 0; i < 16; i++) walkable += t->vf4[i] & 1;
    return walkable && walkable < 16;
}
/* tile_can_have_creep. */
static bool can_have_creep(const sc_creep_t *c, int x, int y) {
    if (unbuildable(c, x, y) || partially_walkable(c, x, y)) return false;
    if (y == (int)c->h - 1) return true;
    return !unbuildable(c, x, y + 1);
}

static sc_creep_t *make(void) {
    int w = level.width, h = level.height;
    if (w <= 0 || h <= 0) return NULL;
    sc_creep_t *c = calloc(1, sizeof(*c) + (size_t)w * h);
    if (!c) return NULL;
    c->w = (uint32_t)w;
    c->h = (uint32_t)h;
    /* MTXM: a tile whose CV5 group carries the creep flag starts with creep
     * (the map's own creep; it recedes like any other tile). */
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        const sc_terrain_t *t = terrain_at(c, x, y);
        if (t && (t->cv5 & SC_TERRAIN_CREEP) && !strip_tile(c, x, y)) c->tile[(size_t)y * c->w + x] |= TILE_CREEP;
    }
    return c;
}
static sc_creep_t *state(void) {
    sc_mission_t *m = level.mission;
    if (!m || level.width <= 0 || level.height <= 0) return NULL;
    if (m->creep && (m->creep->w != (uint32_t)level.width || m->creep->h != (uint32_t)level.height)) sc_creep_reset(m);
    if (!m->creep) m->creep = make();
    return m->creep;
}

bool sc_creep_at(ivec2_t cell) {
    const sc_creep_t *c = state();
    return c && cell.x >= 0 && cell.y >= 0 && cell.x < (int)c->w && cell.y < (int)c->h &&
        (c->tile[(size_t)cell.y * c->w + cell.x] & TILE_CREEP);
}
bool sc_creep_receding(ivec2_t cell) {
    const sc_creep_t *c = state();
    return c && cell.x >= 0 && cell.y >= 0 && cell.x < (int)c->w && cell.y < (int)c->h &&
        (c->tile[(size_t)cell.y * c->w + cell.x] & TILE_RECEDING);
}
int sc_creep_receding_count(void) {
    const sc_creep_t *c = state();
    int n = 0;
    for (int i = 0; c && i < 9; i++) n += (int)c->count[i];
    return n;
}

/* ---- Neighbour lists (creep_life_t::lists) ---- */
static uint8_t *at(sc_creep_t *c, int x, int y) { return &c->tile[(size_t)y * c->w + x]; }
static bool has(const sc_creep_t *c, int x, int y) { return c->tile[(size_t)y * c->w + x] & TILE_CREEP; }
/* count_neighboring_creep_tiles */
static int neighbors(const sc_creep_t *c, int x, int y) {
    int n = 0;
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        int nx = x + dx, ny = y + dy;
        if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= (int)c->w || ny >= (int)c->h) continue;
        n += has(c, nx, ny);
    }
    return n;
}
static bool list_find(const sc_creep_t *c, uint32_t tile, int *which, int *where) {
    for (int n = 0; n < 9; n++) for (uint32_t i = 0; i < c->count[n]; i++)
        if (c->list[n][i] == tile) { *which = n; *where = (int)i; return true; }
    return false;
}
static void list_remove(sc_creep_t *c, int n, int i) {
    memmove(&c->list[n][i], &c->list[n][i + 1], (c->count[n] - (uint32_t)i - 1) * sizeof(uint32_t));
    c->count[n]--;
}
static void list_push_front(sc_creep_t *c, int n, uint32_t tile) {
    memmove(&c->list[n][1], &c->list[n][0], c->count[n] * sizeof(uint32_t));
    c->list[n][0] = tile;
    c->count[n]++;
}
static uint32_t listed(const sc_creep_t *c) {
    uint32_t n = 0;
    for (int i = 0; i < 9; i++) n += c->count[i];
    return n;
}
/* set_tile_creep: a receding neighbour whose creep-neighbour count changed
 * moves to the front of its new list. */
static void set_tile_creep(sc_creep_t *c, int x, int y, bool on) {
    if (on) *at(c, x, y) |= TILE_CREEP; else *at(c, x, y) &= (uint8_t)~TILE_CREEP;
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        int nx = x + dx, ny = y + dy;
        if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= (int)c->w || ny >= (int)c->h) continue;
        if ((*at(c, nx, ny) & (TILE_CREEP | TILE_RECEDING)) != (TILE_CREEP | TILE_RECEDING)) continue;
        uint32_t tile = (uint32_t)ny * c->w + (uint32_t)nx;
        int which, where, now = neighbors(c, nx, ny);
        if (!list_find(c, tile, &which, &where) || which == now) continue;
        list_remove(c, which, where);
        list_push_front(c, now, tile);
    }
}
/* set_creep_receding: false when the 1024 entries are all in use. */
static bool set_receding(sc_creep_t *c, int x, int y) {
    if (listed(c) >= POOL) return false;
    list_push_front(c, neighbors(c, x, y), (uint32_t)y * c->w + (uint32_t)x);
    *at(c, x, y) |= TILE_RECEDING;
    return true;
}

/* ---- The world seen by the creep ---- */
static void snapshot(snaps_t *s) {
    s->a = NULL;
    s->n = 0;
    int cap = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function != P_MobjThinker || mo->remove || mo->hp <= 0 || !valid_type(mo->type_id) ||
            !(sc_units[mo->type_id - 1].flags & 1) || (mo->traits & MF_FLY)) continue;
        if (s->n == cap) {
            cap = cap ? cap * 2 : 64;
            snap_t *grown = realloc(s->a, (size_t)cap * sizeof(*grown));
            if (!grown) return;
            s->a = grown;
        }
        const sc_unit_t *u = &sc_units[mo->type_id - 1];
        bool ready = G_ModelUnitReady(mo);
        bool morph = ready && mo->production && mo->production->product_class == RTS_PRODUCT_BUILDING;
        s->a[s->n++] = (snap_t){.id = mo->id, .type = mo->type_id,
            .px = (int)(mo->core.position.x / (FIXED_ONE / 32)), .py = (int)(mo->core.position.y / (FIXED_ONE / 32)),
            .tw = u->placement.w / 32, .th = u->placement.h / 32, .zerg = (u->race & 1) != 0,
            .completed = ready && !morph, .morph = morph};
    }
}
static bool occupied(const snaps_t *s, int x, int y, uint32_t skip) {
    for (int i = 0; i < s->n; i++) {
        const snap_t *b = &s->a[i];
        if (b->id == skip) continue;
        int x0 = b->px / 32 - b->tw / 2, y0 = b->py / 32 - b->th / 2;
        if (x >= x0 && x < x0 + b->tw && y >= y0 && y < y0 + b->th) return true;
    }
    return false;
}
/* get_max_creep_bb: the 20 by 12.5 tile box of a spreader, else the footprint. */
static area_t max_bb(const sc_creep_t *c, bool spreads, int px, int py, int tw, int th) {
    int fx, fy, tx, ty;
    if (spreads) {
        fx = px - 320; fy = py - 200; tx = px + 320; ty = py + 200;
    } else {
        fx = (px / 32 - tw / 2) * 32; fy = (py / 32 - th / 2) * 32;
        tx = (fx / 32 + tw - 1) * 32; ty = (fy / 32 + th - 1) * 32;
    }
    area_t a;
    a.x0 = fx <= 0 ? 0 : fx / 32;
    a.y0 = fy <= 0 ? 0 : fy / 32;
    a.x1 = tx >= (int)c->w * 32 ? (int)c->w - 1 : tx / 32;
    a.y1 = ty >= (int)c->h * 32 ? (int)c->h - 1 : ty / 32;
    return a;
}
/* The ellipse of the creep a provider covers, in pixels from its centre. */
static bool outside_ellipse(int tile_x, int tile_y, int px, int py) {
    int dx = tile_x * 32 + 16 - px, dy = tile_y * 32 + 16 - py;
    return dx * dx * 25 + dy * dy * 64 > 320 * 320 * 25;
}

/* creep_is_being_sustained: any live Zerg building covering the tile. */
static bool sustained(const sc_creep_t *c, const snaps_t *s, int x, int y) {
    for (int i = 0; i < s->n; i++) {
        const snap_t *b = &s->a[i];
        if (!b->zerg) continue;
        area_t a = max_bb(c, spreads_type(b->type, b->completed), b->px, b->py, b->tw, b->th);
        if (x < a.x0 || y < a.y0 || x > a.x1 || y > a.y1) continue;
        if (outside_ellipse(x, y, b->px, b->py)) continue;
        return true;
    }
    return false;
}

/* ---- Growth ---- */
/* spread_creep: adds the creep-free tile touching the most creep tiles. */
static bool spread(sc_creep_t *c, const snaps_t *s, bool ellipse, int px, int py, int tw, int th,
                   uint32_t skip, bool *any_occupied) {
    uint32_t bucket[8][BUCKET];
    int used[8] = {0};
    area_t a = max_bb(c, ellipse, px, py, tw, th);
    bool blocked = false;
    for (int y = a.y0; y <= a.y1; y++) for (int x = a.x0; x <= a.x1; x++) {
        if (has(c, x, y) || !can_have_creep(c, x, y)) continue;
        if (occupied(s, x, y, skip)) { blocked = true; continue; }
        if (ellipse && outside_ellipse(x, y, px, py)) continue;
        int n = neighbors(c, x, y);
        if (n && used[n - 1] < BUCKET) bucket[n - 1][used[n - 1]++] = (uint32_t)y * c->w + (uint32_t)x;
    }
    if (any_occupied) *any_occupied = blocked;
    for (int n = 7; n >= 0; n--) {
        if (!used[n]) continue;
        uint32_t tile = bucket[n][(sc_random() >> 4) % (uint32_t)used[n]];
        set_tile_creep(c, (int)(tile % c->w), (int)(tile / c->w), true);
        return true;
    }
    return false;
}
/* spread_creep_completely: the footprint, then growth until it stops. */
static void spread_completely(sc_creep_t *c, const snaps_t *s, int type, int px, int py, int tw, int th, uint32_t skip) {
    int x0 = px / 32 - tw / 2, y0 = py / 32 - th / 2;
    for (int y = y0; y < y0 + th; y++) for (int x = x0; x < x0 + tw; x++)
        if (x >= 0 && y >= 0 && x < (int)c->w && y < (int)c->h && can_have_creep(c, x, y)) set_tile_creep(c, x, y, true);
    while (spread(c, s, spreads_type(type, true), px, py, tw, th, skip, NULL)) {}
}
/* set_creep_building_tiles: the footprint, widened by the Hatchery ring. */
static void set_building_tiles(sc_creep_t *c, const snaps_t *s, int type, uint32_t id, int px, int py, int tw, int th) {
    if (!spreads_type(type, true) && requires_creep(type)) return;
    int x0 = px / 32 - tw / 2, y0 = py / 32 - th / 2, x1 = x0 + tw, y1 = y0 + th;
    bool rounded = (sc_units[type - 1].flags & FLAG_PRODUCES) != 0;
    if (rounded) { x0 -= 2; y0 -= 1; x1 += 2; y1 += 1; }
    for (int y = y0; y < y1; y++) {
        if (y < 0 || y >= (int)c->h) continue;
        for (int x = x0; x < x1; x++) {
            if (x < 0 || x >= (int)c->w) continue;
            if (rounded && (x == x0 || x == x1 - 1) && (y == y0 || y == y1 - 1)) continue;
            if (!can_have_creep(c, x, y) || occupied(s, x, y, id)) continue;
            set_tile_creep(c, x, y, true);
        }
    }
}
/* add_creep_provider: receding tiles under the provider stop receding. */
static void add_provider(sc_creep_t *c, bool spreads, int px, int py, int tw, int th) {
    area_t a = max_bb(c, spreads, px, py, tw, th);
    for (int y = a.y0; y <= a.y1; y++) for (int x = a.x0; x <= a.x1; x++) {
        if (!(*at(c, x, y) & TILE_RECEDING)) continue;
        if (spreads && outside_ellipse(x, y, px, py)) continue;
        int which, where;
        if (!list_find(c, (uint32_t)y * c->w + (uint32_t)x, &which, &where)) continue;
        list_remove(c, which, where);
        *at(c, x, y) &= (uint8_t)~TILE_RECEDING;
    }
}
/* remove_creep_provider and, for an Extractor, remove_extractor_creep. */
static void remove_provider(sc_creep_t *c, const snaps_t *s, const track_t *e) {
    const sc_unit_t *u = &sc_units[e->type - 1];
    int tw = u->placement.w / 32, th = u->placement.h / 32;
    bool extractor = e->type == MT_EXTRACTOR;
    bool spreads = spreads_type(e->type, (e->flags & (TRACK_DONE | TRACK_MORPH)) != 0);
    area_t a = max_bb(c, spreads, e->px, e->py, tw, th);
    for (int y = a.y0; y <= a.y1; y++) for (int x = a.x0; x <= a.x1; x++) {
        if (!has(c, x, y)) continue;
        if (!extractor && ((*at(c, x, y) & TILE_RECEDING) || occupied(s, x, y, 0))) continue;
        if (spreads && outside_ellipse(x, y, e->px, e->py)) continue;
        if (extractor) set_tile_creep(c, x, y, false);
        else if (!sustained(c, s, x, y) && !set_receding(c, x, y)) return;
    }
}
/* recede_creep: once per frame, one tile from the first non-empty list. */
static void recede(sc_creep_t *c) {
    static const int period[9] = {1, 3, 5, 6, 7, 8, 9, 9, 9};
    if (c->recede_timer) { c->recede_timer--; return; }
    c->recede_timer = period[(POOL - listed(c)) >> 7];
    for (int n = 0; n < 9; n++) {
        if (!c->count[n]) continue;
        int pick = (int)(((uint64_t)sc_random() * c->count[n]) >> 15);
        uint32_t tile = c->list[n][pick];
        list_remove(c, n, pick);
        *at(c, (int)(tile % c->w), (int)(tile / c->w)) &= (uint8_t)~TILE_RECEDING;
        set_tile_creep(c, (int)(tile % c->w), (int)(tile / c->w), false);
        break;
    }
}

/* ---- Tracked buildings ---- */
static track_t *track_of(sc_creep_t *c, uint32_t id) {
    for (uint32_t i = 0; i < c->tracked; i++) if (c->track[i].id == id) return &c->track[i];
    return NULL;
}
static track_t *track_add(sc_creep_t *c, const snap_t *b) {
    if (c->tracked >= TRACKED) return NULL;
    track_t *e = &c->track[c->tracked++];
    *e = (track_t){.id = b->id, .type = b->type, .px = (uint16_t)b->px, .py = (uint16_t)b->py};
    return e;
}
/* A building that appears complete (a trigger or script creates it): the
 * trigger_create_unit branch. */
static void appear(sc_creep_t *c, const snaps_t *s, const snap_t *b) {
    if (spreads_type(b->type, true)) {
        set_building_tiles(c, s, b->type, b->id, b->px, b->py, b->tw, b->th);
        add_provider(c, true, b->px, b->py, b->tw, b->th);
    } else if (requires_creep(b->type)) {
        spread_completely(c, s, b->type, b->px, b->py, b->tw, b->th, b->id);
        add_provider(c, false, b->px, b->py, b->tw, b->th);
    }
}
static void sync(sc_creep_t *c, const snaps_t *s) {
    for (uint32_t i = 0; i < c->tracked;) {
        bool alive = false;
        for (int k = 0; k < s->n && !alive; k++) alive = s->a[k].id == c->track[i].id && s->a[k].zerg;
        if (alive) { i++; continue; }
        track_t gone = c->track[i];
        remove_provider(c, s, &gone);
        memmove(&c->track[i], &c->track[i + 1], (c->tracked - i - 1) * sizeof(track_t));
        memset(&c->track[--c->tracked], 0, sizeof(track_t));
    }
    for (int k = 0; k < s->n; k++) {
        const snap_t *b = &s->a[k];
        if (!b->zerg) continue;
        track_t *e = track_of(c, b->id);
        if (!e) {
            if (b->completed || b->morph) {
                appear(c, s, b);
                if (!(e = track_add(c, b))) continue;
                e->flags = b->completed ? TRACK_DONE : TRACK_MORPH;
                e->order = starts_growth(b->type) ? ORDER_SPREAD : ORDER_NONE;
            } else {
                add_provider(c, false, b->px, b->py, b->tw, b->th);
                track_add(c, b);
            }
            continue;
        }
        e->type = b->type;
        if (b->completed && !(e->flags & TRACK_DONE)) {
            /* order_IncompleteMorphing's last state: complete_unit, then
             * add_creep_provider and set_creep_building_tiles; the morph
             * starts the creep timer over and the idle order InitCreepGrowth
             * (Hatchery, Lair, Hive, Creep Colony) starts SpreadCreep. */
            set_building_tiles(c, s, b->type, b->id, b->px, b->py, b->tw, b->th);
            add_provider(c, spreads_type(b->type, true), b->px, b->py, b->tw, b->th);
            e->timer = 0;
            if (starts_growth(b->type)) e->order = ORDER_SPREAD;
            e->flags = TRACK_DONE;
        } else if (b->morph) {
            e->flags = TRACK_MORPH;
        }
    }
}

void sc_creep_initial(mobj_t *mo) {
    sc_creep_t *c = state();
    if (!c || !mo || !zerg_building(mo->type_id) || track_of(c, mo->id)) return;
    snaps_t s;
    snapshot(&s);
    const sc_unit_t *u = &sc_units[mo->type_id - 1];
    int px = (int)(mo->core.position.x / (FIXED_ONE / 32)), py = (int)(mo->core.position.y / (FIXED_ONE / 32));
    /* create_initial_unit: complete growth before the unit is placed, so the
     * units created before it stand in the way and later ones do not. */
    if (spreads_type(mo->type_id, true) || requires_creep(mo->type_id))
        spread_completely(c, &s, mo->type_id, px, py, u->placement.w / 32, u->placement.h / 32, mo->id);
    free(s.a);
    track_t *e = track_add(c, &(snap_t){.id = mo->id, .type = mo->type_id, .px = px, .py = py});
    if (!e) return;
    e->flags = TRACK_DONE;
    e->order = starts_growth(mo->type_id) ? ORDER_SPREAD : ORDER_NONE;
}

void sc_creep_ticker(void) {
    int frames = leveltime * 24 / RTS_TICRATE - (leveltime - 1) * 24 / RTS_TICRATE;
    sc_creep_t *c = state();
    if (!c) return;
    snaps_t s;
    snapshot(&s);
    sync(c, &s);
    for (int f = 0; f < frames; f++) {
        recede(c);
        for (uint32_t i = 0; i < c->tracked; i++) {
            track_t *e = &c->track[i];
            if (e->order != ORDER_SPREAD) continue;
            if (e->timer) { e->timer--; continue; }
            e->timer = SPREAD_PERIOD;
            bool blocked = false;
            /* secondary_order_SpreadCreep always spreads with the Hive's box. */
            if (!spread(c, &s, true, e->px, e->py, 4, 3, 0, &blocked) && !blocked) e->order = ORDER_DONE;
        }
    }
    free(s.a);
}

/* ---- Save, validation and hash ---- */
size_t sc_creep_save_size(void) {
    const sc_creep_t *c = state();
    return c ? 4 + 4 + 4 + sizeof(c->count) + 4 + sizeof(uint32_t) * POOL + sizeof(c->track) + (size_t)c->w * c->h : 0;
}
void sc_creep_save(void *out) {
    const sc_creep_t *c = state();
    if (!c) return;
    uint8_t *p = out;
    memcpy(p, &c->w, 4); p += 4;
    memcpy(p, &c->h, 4); p += 4;
    memcpy(p, &c->recede_timer, 4); p += 4;
    memcpy(p, c->count, sizeof(c->count)); p += sizeof(c->count);
    memcpy(p, &c->tracked, 4); p += 4;
    uint32_t packed[POOL] = {0}, next = 0;
    for (int n = 0; n < 9; n++) for (uint32_t i = 0; i < c->count[n]; i++) packed[next++] = c->list[n][i];
    memcpy(p, packed, sizeof(packed)); p += sizeof(packed);
    memcpy(p, c->track, sizeof(c->track)); p += sizeof(c->track);
    memcpy(p, c->tile, (size_t)c->w * c->h);
}
/* A saved creep state, or NULL when it does not describe this level. */
sc_creep_t *sc_creep_parse(const void *data, size_t size) {
    if (level.width <= 0 || level.height <= 0) return NULL;
    size_t cells = (size_t)level.width * level.height;
    size_t fixed = 4 + 4 + 4 + sizeof(uint32_t) * 9 + 4 + sizeof(uint32_t) * POOL + sizeof(track_t) * TRACKED;
    if (size != fixed + cells) return NULL;
    sc_creep_t *c = calloc(1, sizeof(*c) + cells);
    if (!c) return NULL;
    const uint8_t *p = data;
    memcpy(&c->w, p, 4); p += 4;
    memcpy(&c->h, p, 4); p += 4;
    memcpy(&c->recede_timer, p, 4); p += 4;
    memcpy(c->count, p, sizeof(c->count)); p += sizeof(c->count);
    memcpy(&c->tracked, p, 4); p += 4;
    uint32_t packed[POOL];
    memcpy(packed, p, sizeof(packed)); p += sizeof(packed);
    memcpy(c->track, p, sizeof(c->track)); p += sizeof(c->track);
    memcpy(c->tile, p, cells);
    uint32_t total = 0;
    bool ok = c->w == (uint32_t)level.width && c->h == (uint32_t)level.height && c->recede_timer >= 0 &&
        c->recede_timer <= 9 && c->tracked <= TRACKED;
    for (int n = 0; ok && n < 9; n++) {
        ok = c->count[n] <= POOL - total;
        total += ok ? c->count[n] : 0;
    }
    uint32_t receding = 0;
    for (size_t i = 0; ok && i < cells; i++) {
        if (c->tile[i] & ~(TILE_CREEP | TILE_RECEDING)) ok = false;
        else if ((c->tile[i] & TILE_RECEDING) && !(c->tile[i] & TILE_CREEP)) ok = false;
        receding += (c->tile[i] & TILE_RECEDING) != 0;
    }
    ok = ok && receding == total;
    uint32_t next = 0;
    for (int n = 0; ok && n < 9; n++) for (uint32_t i = 0; i < c->count[n]; i++) c->list[n][i] = packed[next++];
    /* A listed tile is receding creep filed under its creep-neighbour count,
     * and listed once. */
    for (int n = 0; ok && n < 9; n++) for (uint32_t i = 0; ok && i < c->count[n]; i++) {
        uint32_t tile = c->list[n][i];
        int which, where;
        ok = tile < cells && (c->tile[tile] & TILE_RECEDING) && neighbors(c, (int)(tile % c->w), (int)(tile / c->w)) == n &&
            list_find(c, tile, &which, &where) && which == n && where == (int)i;
    }
    for (uint32_t i = 0; ok && i < TRACKED; i++) {
        const track_t *e = &c->track[i];
        if (i >= c->tracked) {
            track_t zero;
            memset(&zero, 0, sizeof(zero));
            ok = !memcmp(e, &zero, sizeof(zero));
            continue;
        }
        ok = e->id && zerg_building(e->type) && e->px < c->w * 32 && e->py < c->h * 32 && e->order <= ORDER_DONE &&
            e->timer <= SPREAD_PERIOD && (e->flags & ~(TRACK_DONE | TRACK_MORPH)) == 0 &&
            !((e->flags & TRACK_DONE) && (e->flags & TRACK_MORPH)) && !e->pad0 && !e->pad1 &&
            (e->order == ORDER_NONE || starts_growth(e->type) || (e->flags & (TRACK_DONE | TRACK_MORPH)));
        for (uint32_t k = 0; ok && k < i; k++) ok = c->track[k].id != e->id;
    }
    if (!ok) { free(c); return NULL; }
    return c;
}
uint32_t sc_creep_hash(uint32_t hash) {
    const sc_creep_t *c = state();
    if (!c) return hash;
    hash = G_HashValue(hash, c->w);
    hash = G_HashValue(hash, c->h);
    hash = G_HashValue(hash, (uint32_t)c->recede_timer);
    for (int n = 0; n < 9; n++) {
        hash = G_HashValue(hash, c->count[n]);
        for (uint32_t i = 0; i < c->count[n]; i++) hash = G_HashValue(hash, c->list[n][i]);
    }
    hash = G_HashValue(hash, c->tracked);
    for (uint32_t i = 0; i < c->tracked; i++) {
        const track_t *e = &c->track[i];
        hash = G_HashValue(hash, e->id);
        hash = G_HashValue(hash, (uint32_t)e->type | (uint32_t)e->px << 16);
        hash = G_HashValue(hash, (uint32_t)e->py | (uint32_t)e->flags << 16 | (uint32_t)e->order << 24);
        hash = G_HashValue(hash, e->timer);
    }
    size_t cells = (size_t)c->w * c->h;
    for (size_t i = 0; i < cells; i += 4) {
        uint32_t word = 0;
        for (size_t k = 0; k < 4 && i + k < cells; k++) word |= (uint32_t)c->tile[i + k] << (8 * k);
        hash = G_HashValue(hash, word);
    }
    return hash;
}
