#include "sc_local.h"
/* Where a passenger steps out of a Bunker or a mobile transport, after
 * OpenBW find_unit_placement: the container's own spot when it is free, else
 * square rings around it that grow by 16 pixels, each walked bottom edge
 * left to right, right edge upward, top edge right to left, then left edge
 * downward, stepping 8 pixels and jumping past whatever unit blocks a spot.
 * A spot must keep the passenger's box inside the map (the last 32 pixel
 * rows are not playable), clear of units of its own layer, on ground that
 * holds it, and connected to the container. OpenBW asks the static region
 * group of the container's tile; here that is the navigation component of the
 * passenger's movement class, read from a table built once per map, never
 * a path search, so a search costs a bounded number of box tests wherever
 * the map is large. */

sc_placement_stats_t sc_placement_stats;

typedef struct {
    mobj_t *unit;
    const mobj_t *container;
    int left, top, right, bottom; /* The passenger's collision box around its centre, inclusive. */
    int map_w, map_h;             /* Pixels; the playable height is map_h - 32. */
    int bx0, by0, bx1, by1;       /* Search bounds, 128 pixels about the origin. */
    const uint16_t *regions;
    int cls;
    uint16_t origin_regions[64];
    int origin_count;
} placement_t;

static fixed2_t where(const mobj_t *mo) { return fixed3_xy(mo->core.position); }
static ivec2_t pixel(fixed2_t at) {
    return (ivec2_t){(at.x + FIXED_ONE / 64) >> (FIXED_FRAC_BITS - 5), (at.y + FIXED_ONE / 64) >> (FIXED_FRAC_BITS - 5)};
}
static fixed2_t at_pixel(int x, int y) { return (fixed2_t){SC_PIXELS(x), SC_PIXELS(y)}; }
static bool live(const thinker_t *th) {
    const mobj_t *mo = (const mobj_t *)th;
    return th->function == P_MobjThinker && !mo->remove && mo->hp > 0;
}
static bool building(const mobj_t *mo) { return (sc_units[mo->type_id - 1].flags & SC_UNIT_BUILDING) != 0; }
/* Floor and ceiling to the 8 pixel grid, as OpenBW's unsigned arithmetic does. */
static int floor8(int v) { return v & ~7; }
static int ceil8(int v) { return (v + 7) & ~7; }

static bool terrain_fits(const mobj_t *unit, fixed2_t at) {
    irect_t bounds = sc_unit(unit)->bounds;
    int x0 = fixed_floor_int(at.x + SC_PIXELS(bounds.x));
    int y0 = fixed_floor_int(at.y + SC_PIXELS(bounds.y));
    int x1 = fixed_floor_int(at.x + SC_PIXELS(bounds.x + bounds.w));
    int y1 = fixed_floor_int(at.y + SC_PIXELS(bounds.y + bounds.h));
    int cls = P_MobjMoveClass(unit);
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        if (!L_Contains(&level, x, y)) return false;
        size_t index = L_Index(&level, x, y);
        if ((level.blocked && level.blocked[index]) ||
            (level.cell_solid && (level.cell_solid[index] & 1))) return false;
        if (level.speeds && cls > 0 && cls < level.speeds->class_count) {
            int effect = level.cell_effect ? level.cell_effect[index] : 255;
            int speed = effect < 8 ? level.speeds->overlay[cls][effect] :
                level.speeds->terrain[cls][(level.cell_terrain ? level.cell_terrain[index] : 15) & 15];
            if (speed <= 0) return false;
        }
    }
    return true;
}

/* A unit whose box overlaps the passenger's box centred on (x, y) and shares
 * its layer; a Bunker's footprint blocks its own door even while the
 * Bunker is dying. */
static const mobj_t *blocker_at(const placement_t *p, int x, int y) {
    sc_placement_stats.blocker_scans++;
    const mobj_t *unit = p->unit;
    bool flying = (unit->traits & MF_FLY) != 0;
    int from_x = x - p->left, to_x = x + p->right + 1, from_y = y - p->top, to_y = y + p->bottom + 1;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *other = (const mobj_t *)th;
        const sc_unit_t *type = sc_unit(other);
        if ((!live(th) && other != p->container) || other == unit || (other->traits & MF_NOBLOCKMAP) ||
            !type || ((other->traits & MF_FLY) != 0) != flying) continue;
        ivec2_t at = pixel(where(other));
        if (from_x < at.x + type->bounds.x + type->bounds.w && to_x > at.x + type->bounds.x &&
            from_y < at.y + type->bounds.y + type->bounds.h && to_y > at.y + type->bounds.y) return other;
    }
    return NULL;
}
/* Inclusive box inside the playable map. */
static bool in_valid_bounds(const placement_t *p, int x, int y) {
    return x - p->left >= 0 && x + p->right < p->map_w && y - p->top >= 0 && y + p->bottom < p->map_h - 32;
}
static bool origin_connects(const placement_t *p, fixed2_t at) {
    if (!p->regions) return true;
    ivec2_t node = P_NavNodeAt(&level, p->cls, at);
    if (!L_Contains(&level, node.x, node.y)) return false;
    uint16_t region = p->regions[L_Index(&level, node.x, node.y)];
    if (!region) return false;
    for (int i = 0; i < p->origin_count; i++) if (p->origin_regions[i] == region) return true;
    return false;
}
/* A spot the passenger may take: connected to the container and held by the
 * ground. */
static bool acceptable(const placement_t *p, int x, int y) {
    fixed2_t at = at_pixel(x, y);
    sc_placement_stats.reach_tests++;
    if (!origin_connects(p, at)) return false;
    sc_placement_stats.fit_tests++;
    return terrain_fits(p->unit, at);
}

/* The regions the container's spot belongs to. A building does not cut the
 * ground under it in OpenBW's static regions, so the regions around the
 * footprint stand for it. */
static void find_origin_regions(placement_t *p, ivec2_t origin, const mobj_t *blocker) {
    p->regions = P_NavRegions(&level, p->cls);
    if (!p->regions) return;
    ivec2_t clamped = {origin.x < p->left ? p->left : origin.x > p->map_w - 1 - p->right ? p->map_w - 1 - p->right : origin.x,
                       origin.y < p->top ? p->top : origin.y > p->map_h - 33 - p->bottom ? p->map_h - 33 - p->bottom : origin.y};
    ivec2_t node = P_NavNodeAt(&level, p->cls, at_pixel(clamped.x, clamped.y));
    uint16_t region = L_Contains(&level, node.x, node.y) ? p->regions[L_Index(&level, node.x, node.y)] : 0;
    if (region) { p->origin_regions[p->origin_count++] = region; return; }
    if (!blocker || !building(blocker)) return;
    irect_t cells = P_MobjCells(blocker);
    ivec2_t ring[128];
    int count = P_NavRing(&level, p->cls, (ivec2_t){cells.x, cells.y}, (isize2_t){cells.w, cells.h}, ring, 128);
    for (int i = 0; i < count; i++) {
        if (!L_Contains(&level, ring[i].x, ring[i].y)) continue;
        uint16_t r = p->regions[L_Index(&level, ring[i].x, ring[i].y)];
        bool known = false;
        for (int j = 0; j < p->origin_count; j++) known |= p->origin_regions[j] == r;
        if (r && !known && p->origin_count < 64) p->origin_regions[p->origin_count++] = r;
    }
}

/* One ring of OpenBW's find: the rectangle from (rx, ry) w by h, clamped to
 * the search bounds, walked edge by edge. */
static bool walk_ring(const placement_t *p, int rx, int ry, int w, int h, ivec2_t *out) {
    int fx = floor8(rx), fy = floor8(ry), tx = ceil8(rx + w), ty = ceil8(ry + h);
    if (fx < p->bx0) fx = p->bx0;
    if (fy < p->by0) fy = p->by0;
    if (tx > p->bx1) tx = p->bx1;
    if (ty > p->by1) ty = p->by1;
    int wide = (p->left + p->right + 1 + 7) / 8 * 8; /* Wide rings skip the left part of the bottom edge. */
    bool skip = w > (p->left + p->right + 1) * 2;
    if (skip) fx += wide;
    /* A blocker's far side, found as OpenBW does: past its box on the
     * walking side, then back onto the 8 pixel grid. */
#define TRY(X, Y, JUMP) { /* Not a do-while: JUMP's continue is the scan's. */ \
        if (in_valid_bounds(p, (X), (Y))) { \
            const mobj_t *n = blocker_at(p, (X), (Y)); \
            if (n) { \
                const irect_t nb = sc_unit(n)->bounds; ivec2_t np = pixel(where(n)); \
                JUMP; \
                continue; \
            } \
            sc_placement_stats.candidates++; \
            if (acceptable(p, (X), (Y))) { *out = (ivec2_t){(X), (Y)}; return true; } \
        } \
    }
    for (int x = fx; x <= tx;) {
        TRY(x, ty, { int inc = np.x + nb.x + nb.w - (x - p->left); inc += (8 - ((x + inc) & 7)) & 7; x += inc; });
        x += 8;
    }
    for (int y = ty; y >= fy;) {
        TRY(tx, y, { int dec = (y + p->bottom) - (np.y + nb.y - 1); dec += (8 - ((y - dec) & 7)) & 7; y -= dec; });
        y -= 8;
    }
    if (skip) fx -= wide;
    for (int x = tx; x >= fx;) {
        TRY(x, fy, { int dec = (x + p->right) - (np.x + nb.x - 1); dec += (8 - ((x - dec) & 7)) & 7; x -= dec; });
        x -= 8;
    }
    for (int y = fy; y <= ty;) {
        TRY(fx, y, { int inc = np.y + nb.y + nb.h - (y - p->top); inc += (8 - ((y + inc) & 7)) & 7; y += inc; });
        y += 8;
    }
#undef TRY
    return false;
}

bool sc_unload_position(mobj_t *unit, const mobj_t *container, fixed2_t at) {
    if (unit->traits & MF_FLY) {
        unit->core.position = fixed3_with_xy(unit->core.position, at);
        return true;
    }
    sc_placement_stats.searches++;
    irect_t b = sc_unit(unit)->bounds;
    ivec2_t pos = pixel(at);
    placement_t p = {.unit = unit, .container = container, .left = -b.x, .top = -b.y, .right = b.x + b.w - 1,
        .bottom = b.y + b.h - 1, .map_w = level.width * 32, .map_h = level.height * 32, .cls = P_MobjMoveClass(unit)};
    p.bx0 = pos.x - 128 < 0 ? 0 : pos.x - 128;
    p.by0 = pos.y - 128 < 0 ? 0 : pos.y - 128;
    p.bx1 = pos.x + 127 >= p.map_w ? p.map_w - 1 : pos.x + 127;
    p.by1 = pos.y + 127 >= p.map_h - 32 ? p.map_h - 32 - 1 : pos.y + 127;
    const mobj_t *blocker = blocker_at(&p, pos.x, pos.y);
    bool in_map = pos.x - p.left >= 0 && pos.x + p.right <= p.map_w && pos.y - p.top >= 0 && pos.y + p.bottom <= p.map_h - 32;
    if (!blocker && in_map) {
        /* Terrain at the container's own spot decides: no search from it. */
        if (!terrain_fits(unit, at)) return false;
        unit->core.position = fixed3_with_xy(unit->core.position, at);
        return true;
    }
    find_origin_regions(&p, pos, blocker);
    int width = 8, height = 8;
    if (blocker) {
        const irect_t nb = sc_unit(blocker)->bounds;
        int new_w = p.left + (nb.x + nb.w - 1) + 2, new_h = p.top + (nb.y + nb.h - 1) + 2;
        if (new_w > width) width = new_w;
        if (new_h > height) height = new_h;
    }
    for (;;) {
        int rx = pos.x - width, ry = pos.y - height;
        if (rx < p.bx0 && ry < p.by0 && pos.x + width > p.bx1 && pos.y + height > p.by1) return false;
        ivec2_t found;
        if (walk_ring(&p, rx, ry, width * 2, height * 2, &found)) {
            unit->core.position = fixed3_with_xy(unit->core.position, at_pixel(found.x, found.y));
            return true;
        }
        width += 16;
        height += 16;
    }
}
