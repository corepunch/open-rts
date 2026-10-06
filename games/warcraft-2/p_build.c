#define _DEFAULT_SOURCE
#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <math.h>
#include <stdlib.h>

/* Construction. A worker is sent to a clear footprint; on arrival it pays
 * the price (retail: "not enough gold" is reported when the peasant gets
 * there, not when it is sent), steps inside, and the structure rises
 * through the Wargus construction-land stages while its hit points grow
 * from one to full. The builder steps out beside the finished building.
 * Hall upgrades are not built this way; they transform in place. */

#define W2_BUILD_RETRIES 4
#define W2_MINE_GAP 3 /* Wargus BuildingRules: halls keep "distance > 3" from a gold mine. */

static bool worker(const mobj_t *unit) {
    return unit && !unit->remove && unit->hp > 0 &&
        (unit->type_id == MT_PEASANT || unit->type_id == MT_PEON);
}

static bool alive(const mobj_t *unit) { return unit && !unit->remove && unit->hp > 0; }

static int build_ms(uint16_t type) { return mobjinfo[type].w2.costs.time * 1000; }

/* Keeps and castles stand in for the town hall they grew from. */
bool W2_CountsAs(uint16_t type, uint16_t wanted) {
    if (type == wanted) return true;
    switch (wanted) {
    case MT_TOWN_HALL: return type == MT_KEEP || type == MT_CASTLE;
    case MT_KEEP: return type == MT_CASTLE;
    case MT_GREAT_HALL: return type == MT_STRONGHOLD || type == MT_FORTRESS;
    case MT_STRONGHOLD: return type == MT_FORTRESS;
    default: return false;
    }
}

bool W2_Buildable(uint16_t type) {
    if (type == 0 || type >= NUMMOBJTYPES || !mobjinfo[type].name) return false;
    const w2_stats_t *s = &mobjinfo[type].w2;
    if ((s->flags & (W2_STRUCTURE | W2_SKIP)) != W2_STRUCTURE || s->costs.time <= 0) return false;
    if (s->gives_mask || (s->attributes & W2_NEUTRAL)) return false;
    /* Upgraded halls grow out of a hall; platforms are a tanker's job;
     * the portal, runestone and circle are map furniture. */
    if ((s->flags & W2_HALL) && type != MT_TOWN_HALL && type != MT_GREAT_HALL) return false;
    if (type == MT_HUMAN_OIL_PLATFORM || type == MT_ORC_OIL_PLATFORM) return false;
    if (type == MT_DARK_PORTAL || type == MT_RUNESTONE || type == MT_CIRCLE_OF_POWER) return false;
    return true;
}

bool W2_UnderConstruction(const mobj_t *unit) {
    return alive(unit) && unit->type_id < NUMMOBJTYPES &&
        (mobjinfo[unit->type_id].w2.flags & W2_STRUCTURE) && unit->w2.build_left_ms > 0;
}

int W2_BuildProgress(const mobj_t *site) {
    if (!site || site->w2.build_time_ms <= 0) return 100;
    int left = site->w2.build_left_ms < 0 ? 0 : site->w2.build_left_ms;
    return (site->w2.build_time_ms - left) * 100 / site->w2.build_time_ms;
}

static ivec2_t structure_cell(const mobj_t *unit) {
    isize2_t foot = mobjinfo[unit->type_id].w2.footprint;
    fvec2_t centre = fixed3_xy_to_fvec2(unit->core.position);
    return (ivec2_t){ (int)floorf(centre.x - foot.w * 0.5f + 0.001f),
                      (int)floorf(centre.y - foot.h * 0.5f + 0.001f) };
}

/* Chebyshev distance between two footprints, Stratagus MapDistanceBetweenTypes
 * style: touching rectangles are one apart. */
static int footprint_gap(ivec2_t a, isize2_t sa, ivec2_t b, isize2_t sb) {
    int dx = 0, dy = 0;
    if (b.x >= a.x + sa.w) dx = b.x - (a.x + sa.w) + 1;
    else if (a.x >= b.x + sb.w) dx = a.x - (b.x + sb.w) + 1;
    if (b.y >= a.y + sa.h) dy = b.y - (a.y + sa.h) + 1;
    else if (a.y >= b.y + sb.h) dy = a.y - (b.y + sb.h) + 1;
    return dx > dy ? dx : dy;
}

static bool cell_is_land(int x, int y) {
    if (!L_Contains(&level, x, y)) return false;
    int index = L_Index(&level, x, y);
    if (level.cell_terrain && level.cell_terrain[index] != 0) return false;
    if (level.cell_solid && level.cell_solid[index]) return false;
    return true;
}

static bool cell_is_water(int x, int y) {
    return L_Contains(&level, x, y) && level.cell_terrain &&
        level.cell_terrain[L_Index(&level, x, y)] == 1;
}

bool W2_CanPlace(uint16_t type, ivec2_t cell, const mobj_t *builder) {
    if (!W2_Buildable(type)) return false;
    const w2_stats_t *s = &mobjinfo[type].w2;
    isize2_t foot = s->footprint;
    if (foot.w <= 0 || foot.h <= 0) return false;
    bool shore = false;
    for (int y = 0; y < foot.h; ++y)
        for (int x = 0; x < foot.w; ++x) {
            if (!cell_is_land(cell.x + x, cell.y + y)) return false;
            shore |= cell_is_water(cell.x + x - 1, cell.y + y) || cell_is_water(cell.x + x + 1, cell.y + y) ||
                     cell_is_water(cell.x + x, cell.y + y - 1) || cell_is_water(cell.x + x, cell.y + y + 1);
        }
    if ((s->attributes & W2_SHORE_BUILDING) && !shore) return false;
    irect_t rect = { cell.x, cell.y, foot.w, foot.h };
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *other = (const mobj_t *)th;
        if (other == builder || !alive(other) || (other->traits & (MF_MISSILE | MF_NOBLOCKMAP))) continue;
        if (other->type_id >= NUMMOBJTYPES) continue;
        if (other->traits & MF_FLY) continue;
        if (mobjinfo[other->type_id].w2.flags & W2_STRUCTURE) {
            if ((s->flags & W2_HALL) && other->type_id == MT_GOLD_MINE &&
                footprint_gap(cell, foot, structure_cell(other), mobjinfo[other->type_id].w2.footprint) <= W2_MINE_GAP)
                return false;
            continue; /* Its cells are solid already. */
        }
        ivec2_t at = fvec2_cell(fixed3_xy_to_fvec2(other->core.position));
        if (irect_contains(rect, at)) return false; /* Someone stands on the site. */
    }
    return true;
}

static void show(mobj_t *unit, bool visible) {
    if (visible) {
        unit->traits |= MF_RENDERABLE | MF_SELECTABLE | MF_MOBILE;
        unit->traits &= ~MF_NOBLOCKMAP;
    } else {
        unit->traits &= ~(MF_RENDERABLE | MF_SELECTABLE | MF_MOBILE);
        unit->traits |= MF_NOBLOCKMAP;
    }
    P_MobjSetHidden(unit, !visible);
}

/* The builder is free again, standing where it entered. */
static void release(mobj_t *unit) {
    unit->w2.build_phase = W2_BUILD_NONE;
    unit->w2.build_type = 0;
    unit->w2.site = 0;
    unit->w2.build_tries = 0;
    if (!alive(unit)) return;
    show(unit, true);
    P_ClearMove(unit);
    unit->movement.goal = fixed3_xy_to_fvec2(unit->core.position);
    unit->movement.order_arrived = true;
    P_SetMobjState(unit, gameinfo->mobjinfo[unit->type_id].spawnstate);
}

void W2_InterruptBuild(mobj_t *unit) {
    if (!worker(unit) || unit->w2.build_phase != W2_BUILD_TO_SITE) return;
    unit->w2.build_phase = W2_BUILD_NONE;
    unit->w2.build_type = 0;
    unit->w2.build_tries = 0;
}

static bool walk_to_site(mobj_t *unit) {
    isize2_t foot = mobjinfo[unit->w2.build_type].w2.footprint;
    fvec2_t bay;
    if (!w2_approach(unit, unit->w2.build_cell, foot, &bay) || !P_MoveUnitTo(&level, unit, bay)) return false;
    unit->movement.order_id = 0;
    return true;
}

bool W2_ConstructOrder(mobj_t *builder, uint16_t type, ivec2_t cell) {
    if (!worker(builder) || builder->owner >= 8 || !W2_CanPlace(type, cell, builder)) return false;
    const int *price = mobjinfo[type].w2.costs.resources, *stock = level.player_resources[builder->owner];
    for (int r = 0; r < 3; ++r) if (stock[r] < price[r]) return false;
    W2_InterruptHarvest(builder);
    builder->attack.target = NULL;
    builder->harvest.target = -1;
    builder->harvest.base = NULL;
    builder->harvest.phase = HARVEST_PHASE_NONE;
    builder->waypoints = (waypoints_t){0};
    builder->w2.build_type = type;
    builder->w2.build_cell = cell;
    builder->w2.build_phase = W2_BUILD_TO_SITE;
    builder->w2.build_tries = 0;
    if (!walk_to_site(builder)) {
        builder->w2.build_phase = W2_BUILD_NONE;
        builder->w2.build_type = 0;
        return false;
    }
    return true;
}

/* The worker is beside the footprint: pay, raise the site, step inside. */
static bool start_site(mobj_t *unit) {
    uint16_t type = unit->w2.build_type;
    ivec2_t cell = unit->w2.build_cell;
    if (!W2_CanPlace(type, cell, unit) || unit->owner >= 8) return false;
    const int *price = mobjinfo[type].w2.costs.resources;
    int *stock = level.player_resources[unit->owner];
    for (int r = 0; r < 3; ++r) if (stock[r] < price[r]) return false;
    isize2_t foot = mobjinfo[type].w2.footprint;
    fvec2_t centre = { cell.x + foot.w * 0.5f, cell.y + foot.h * 0.5f };
    mobj_t *site = P_SpawnMobj(fixed3_from_fvec2(centre, 0), type);
    if (!site) return false;
    for (int r = 0; r < 3; ++r) stock[r] -= price[r];
    site->owner = unit->owner;
    site->team = unit->team;
    site->allegiance = unit->allegiance;
    site->core.angle = ANG270;
    site->harvest.target = -1;
    site->hp = 1;
    site->w2.build_time_ms = build_ms(type);
    site->w2.build_left_ms = site->w2.build_time_ms;
    site->w2.builder = unit->id;
    w2_mark_footprint(cell.x, cell.y, foot);
    P_SetMobjState(site, W2_BUILD_STATE(type - 1));
    W2_EnsureUnitSprite(type - 1);
    unit->w2.site = site->id;
    unit->w2.build_phase = W2_BUILD_WORKING;
    P_ClearMove(unit);
    unit->core.momentum = fixed3_zero();
    P_MobjSetSelected(unit, false); /* Out of sight, out of the selection. */
    show(unit, false);
    P_SetMobjState(unit, gameinfo->mobjinfo[unit->type_id].spawnstate);
    return true;
}

/* Hit points track the time spent: one at the start, full at the end. */
static int hp_at(const mobj_t *site, int left_ms) {
    int total = site->w2.build_time_ms;
    if (total <= 0) return site->max_hp;
    if (left_ms < 0) left_ms = 0;
    return 1 + (int)((int64_t)(site->max_hp - 1) * (total - left_ms) / total);
}

static void finish_site(mobj_t *site) {
    site->w2.build_left_ms = 0;
    site->w2.builder = 0;
    P_SetMobjState(site, mobjinfo[site->type_id].spawnstate);
}

bool W2_TickBuild(mobj_t *unit) {
    if (!worker(unit) || unit->w2.build_phase == W2_BUILD_NONE) return false;
    if (unit->w2.build_phase == W2_BUILD_TO_SITE) {
        if (!W2_Buildable(unit->w2.build_type)) { release(unit); return false; }
        if (P_HasMoveOrder(unit)) return false;
        if (!unit->movement.order_arrived ||
            !fvec2_near(fixed3_xy_to_fvec2(unit->core.position), unit->movement.goal, 0.001f)) {
            /* Pushed off the bay or the way was blocked: try again a few times. */
            if (++unit->w2.build_tries > W2_BUILD_RETRIES || !walk_to_site(unit)) release(unit);
            return false;
        }
        if (!start_site(unit)) release(unit);
        return true;
    }
    mobj_t *site = P_MobjById(unit->w2.site);
    if (!site || !W2_UnderConstruction(site) || site->w2.builder != unit->id) {
        release(unit);
        return true;
    }
    int dt_ms = (int)lroundf(FIXED_DT * 1000.0f);
    int before = hp_at(site, site->w2.build_left_ms);
    site->w2.build_left_ms -= dt_ms;
    int after = hp_at(site, site->w2.build_left_ms);
    site->hp += after - before;
    if (site->hp > site->max_hp) site->hp = site->max_hp;
    if (site->w2.build_left_ms <= 0) {
        finish_site(site);
        release(unit);
        return true;
    }
    int percent = W2_BuildProgress(site);
    int stage = percent < 25 ? 0 : percent < 50 ? 1 : 2;
    int state = W2_BUILD_STATE(site->type_id - 1) + stage;
    if (site->core.state_id != state) P_SetMobjState(site, state);
    return true;
}

/* Cancelling returns the whole price (retail refunds a cancelled site). */
bool W2_CancelConstruction(mobj_t *site) {
    if (!W2_UnderConstruction(site) || site->owner >= 8) return false;
    const int *price = mobjinfo[site->type_id].w2.costs.resources;
    for (int r = 0; r < 3; ++r) level.player_resources[site->owner][r] += price[r];
    mobj_t *builder = P_MobjById(site->w2.builder);
    site->w2.build_left_ms = 0;
    site->w2.builder = 0;
    if (builder && builder->w2.site == site->id) release(builder);
    w2_clear_footprint(structure_cell(site).x, structure_cell(site).y, mobjinfo[site->type_id].w2.footprint);
    P_RemoveMobj(site);
    return true;
}

/* A footprint with a free ring of land around it, so the base stays
 * passable, nearest to the owner's hall (or any structure, or a worker). */
static bool site_with_margin(uint16_t type, ivec2_t cell) {
    if (!W2_CanPlace(type, cell, NULL)) return false;
    isize2_t foot = mobjinfo[type].w2.footprint;
    for (int y = -1; y <= foot.h; ++y)
        for (int x = -1; x <= foot.w; ++x) {
            if (x >= 0 && y >= 0 && x < foot.w && y < foot.h) continue;
            if (!cell_is_land(cell.x + x, cell.y + y)) return false;
        }
    return true;
}

bool W2_FindBuildSite(int owner, uint16_t type, ivec2_t *out) {
    if (!out || !W2_Buildable(type)) return false;
    const mobj_t *anchor = NULL;
    int best = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *unit = (const mobj_t *)th;
        if (!alive(unit) || unit->owner != owner || unit->type_id >= NUMMOBJTYPES) continue;
        const w2_stats_t *s = &mobjinfo[unit->type_id].w2;
        int rank = (s->flags & W2_HALL) ? 3 : (s->flags & W2_STRUCTURE) ? 2 : worker(unit) ? 1 : 0;
        if (rank > best) { best = rank; anchor = unit; }
    }
    if (!anchor) return false;
    fvec2_t centre = fixed3_xy_to_fvec2(anchor->core.position);
    isize2_t foot = mobjinfo[type].w2.footprint;
    ivec2_t origin = { (int)floorf(centre.x) - foot.w / 2, (int)floorf(centre.y) - foot.h / 2 };
    for (int radius = 1; radius <= 20; ++radius)
        for (int dy = -radius; dy <= radius; ++dy)
            for (int dx = -radius; dx <= radius; ++dx) {
                if (abs(dx) != radius && abs(dy) != radius) continue;
                ivec2_t cell = { origin.x + dx, origin.y + dy };
                if (site_with_margin(type, cell)) { *out = cell; return true; }
            }
    return false;
}
