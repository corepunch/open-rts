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
        (mobjinfo[unit->type_id].w2.flags & W2_HARVEST);
}

/* Oil platforms replace an oil patch at exactly its authored footprint. */
static resourcevent_t *oil_patch(ivec2_t cell) {
    for (int i = 0; i < level.resource_vent_count; ++i) {
        resourcevent_t *vent = &level.resource_vents[i];
        mobj_t *source = P_MobjById(vent->source_id);
        if (vent->active && vent->amount > 0 && vent->resource_type == 2 &&
            vent->cell.x == cell.x && vent->cell.y == cell.y && source && source->type_id == MT_OIL_PATCH)
            return vent;
    }
    return NULL;
}

void W2_RestoreOilPatch(mobj_t *site) {
    if (site->type_id != MT_HUMAN_OIL_PLATFORM && site->type_id != MT_ORC_OIL_PLATFORM) return;
    for (int i = 0; i < level.resource_vent_count; ++i) {
        resourcevent_t *vent = &level.resource_vents[i];
        if (vent->source_id != site->id || vent->amount <= 0) continue;
        mobj_t *patch = P_SpawnMobj(site->core.position, MT_OIL_PATCH);
        if (patch) {
            patch->owner = patch->team = 15;
            patch->allegiance = ALLEGIANCE_NEUTRAL;
            vent->source_id = patch->id;
            w2_mark_footprint(vent->cell.x, vent->cell.y, vent->footprint);
            W2_EnsureUnitSprite(MT_OIL_PATCH - 1);
        }
        return;
    }
}

static bool alive(const mobj_t *unit) { return unit && !unit->remove && unit->hp > 0; }

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
    if (type == MT_HUMAN_OIL_PLATFORM || type == MT_ORC_OIL_PLATFORM) return true;
    if (s->gives_mask || (s->attributes & W2_NEUTRAL)) return false;
    /* Upgraded halls grow out of a hall; platforms are a tanker's job;
     * the portal, runestone and circle are map furniture. */
    if ((s->flags & W2_HALL) && type != MT_TOWN_HALL && type != MT_GREAT_HALL) return false;
    if (type == MT_DARK_PORTAL || type == MT_RUNESTONE || type == MT_CIRCLE_OF_POWER) return false;
    return true;
}

bool W2_UnderConstruction(const mobj_t *unit) {
    return alive(unit) && unit->type_id < NUMMOBJTYPES &&
        (mobjinfo[unit->type_id].w2.flags & W2_STRUCTURE) && unit->w2.build_left_tics > 0;
}

int W2_BuildProgress(const mobj_t *site) {
    if (!site || site->w2.build_tics <= 0) return 100;
    int left = site->w2.build_left_tics < 0 ? 0 : site->w2.build_left_tics;
    return (site->w2.build_tics - left) * 100 / site->w2.build_tics;
}

static ivec2_t structure_cell(const mobj_t *unit) {
    isize2_t foot = mobjinfo[unit->type_id].w2.footprint;
    return fixed2_footprint_corner(fixed3_xy(unit->core.position), foot);
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

/* Terrain alone: the cell is land (or coast for a shore building) and free. */
static bool cell_ground_clear(uint16_t type, ivec2_t cell) {
    if (!W2_Buildable(type) || !L_Contains(&level, cell.x, cell.y)) return false;
    /* Platforms are checked as a whole against their exact oil patch. */
    if (type == MT_HUMAN_OIL_PLATFORM || type == MT_ORC_OIL_PLATFORM) return true;
    int index = L_Index(&level, cell.x, cell.y);
    int terrain = level.cell_terrain ? level.cell_terrain[index] : 0;
    bool shore = (mobjinfo[type].w2.attributes & W2_SHORE_BUILDING) != 0;
    if (level.cell_solid && level.cell_solid[index]) return false;
    return shore ? terrain == 1 || terrain == 3 : terrain == 0;
}

/* A ground unit other than the builder stands on the cell. */
static bool blocks_cell(const mobj_t *other, const mobj_t *builder) {
    return other != builder && alive(other) && !(other->traits & (MF_MISSILE | MF_NOBLOCKMAP | MF_FLY)) &&
        other->type_id < NUMMOBJTYPES && !(mobjinfo[other->type_id].w2.flags & W2_STRUCTURE);
}

static bool cell_has_unit(ivec2_t cell, const mobj_t *builder) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *other = (const mobj_t *)th;
        if (blocks_cell(other, builder) && ivec2_equal(cell, fixed2_cell(fixed3_xy(other->core.position))))
            return true;
    }
    return false;
}

bool W2_BuildCellClear(uint16_t type, ivec2_t cell, const mobj_t *builder) {
    if (!cell_ground_clear(type, cell)) return false;
    if (type == MT_HUMAN_OIL_PLATFORM || type == MT_ORC_OIL_PLATFORM) return true;
    return !cell_has_unit(cell, builder);
}

/* A site search tests hundreds of footprints against the same world, so it
 * takes the units, the structures the spacing rules name and the mining
 * lanes once instead of walking every object for every cell. */
typedef struct {
    uint8_t *units;          /* Cells a ground unit stands on. */
    const mobj_t **spaced;   /* Structures a spacing rule measures from. */
    int spaced_count;
    irect_t *lanes;          /* Hall-to-gold-mine rectangles of the owner. */
    int lane_count;
    const uint16_t *regions; /* The walker's nav components, when there is a walker. */
    uint16_t region;
} sitescan_t;

/* Wargus spacing: shipyards and refineries keep distance > 3 from oil,
 * halls from a gold mine. */
static bool spacing_blocks(const w2_stats_t *s, ivec2_t cell, const mobj_t *other) {
    const w2_stats_t *o = &mobjinfo[other->type_id].w2;
    if ((s->store_mask & 4) && (o->gives_mask & 4) &&
        footprint_gap(cell, s->footprint, structure_cell(other), o->footprint) <= 3) return true;
    return (s->flags & W2_HALL) && other->type_id == MT_GOLD_MINE &&
        footprint_gap(cell, s->footprint, structure_cell(other), o->footprint) <= W2_MINE_GAP;
}

static bool spaced_structure(const mobj_t *other, const mobj_t *builder) {
    return other != builder && alive(other) && !(other->traits & (MF_MISSILE | MF_NOBLOCKMAP | MF_FLY)) &&
        other->type_id < NUMMOBJTYPES && (mobjinfo[other->type_id].w2.flags & W2_STRUCTURE);
}

static bool place(uint16_t type, ivec2_t cell, const mobj_t *builder, const sitescan_t *scan) {
    if (!W2_Buildable(type)) return false;
    bool platform = type == MT_HUMAN_OIL_PLATFORM || type == MT_ORC_OIL_PLATFORM;
    if (builder) {
        uint16_t maker = type == MT_HUMAN_OIL_PLATFORM ? MT_HUMAN_OIL_TANKER : MT_ORC_OIL_TANKER;
        if (platform ? builder->type_id != maker :
            builder->type_id != MT_PEASANT && builder->type_id != MT_PEON) return false;
    }
    if (platform) return oil_patch(cell) != NULL;
    const w2_stats_t *s = &mobjinfo[type].w2;
    isize2_t foot = s->footprint;
    if (foot.w <= 0 || foot.h <= 0) return false;
    bool shore_building = (s->attributes & W2_SHORE_BUILDING) != 0;
    bool shore = false;
    for (int y = 0; y < foot.h; ++y)
        for (int x = 0; x < foot.w; ++x) {
            ivec2_t at = ivec2_add(cell, (ivec2_t){x, y});
            if (!cell_ground_clear(type, at)) return false;
            int index = L_Index(&level, at.x, at.y);
            if (scan ? scan->units[index] : cell_has_unit(at, builder)) return false;
            int terrain = level.cell_terrain ? level.cell_terrain[index] : 0;
            shore |= terrain == 3;
        }
    if (shore_building && !shore) return false;
    if (scan) {
        for (int i = 0; i < scan->spaced_count; ++i)
            if (spacing_blocks(s, cell, scan->spaced[i])) return false;
        return true;
    }
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *other = (const mobj_t *)th;
        if (th->function == P_MobjThinker && spaced_structure(other, builder) && spacing_blocks(s, cell, other))
            return false;
    }
    return true;
}

bool W2_CanPlace(uint16_t type, ivec2_t cell, const mobj_t *builder) {
    return place(type, cell, builder, NULL);
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
    unit->movement.goal = fixed3_xy(unit->core.position);
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
    fixed2_t bay;
    if (!P_ApproachFootprint(unit, unit->w2.build_cell, foot, &bay) || !P_MoveUnitTo(&level, unit, bay)) return false;
    unit->movement.order_id = 0;
    return true;
}

bool W2_ConstructOrder(mobj_t *builder, uint16_t type, ivec2_t cell) {
    if (!worker(builder) || builder->owner >= 8 || !W2_CanPlace(type, cell, builder) ||
        !W2_SiteAllowed(builder->owner, builder->type_id, type)) return false;
    const int *price = mobjinfo[type].w2.costs.resources, *stock = level.player_resources[builder->owner];
    for (int r = 0; r < 3; ++r) if (stock[r] < price[r]) return false;
    W2_InterruptRepair(builder);
    W2_InterruptHarvest(builder);
    builder->w2.carrier = 0;
    builder->w2.stand_ground = false;
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
    fixed2_t centre = { FIXED_FROM_INT(cell.x) + foot.w * (FIXED_ONE / 2), FIXED_FROM_INT(cell.y) + foot.h * (FIXED_ONE / 2) };
    mobj_t *site = P_SpawnMobj(fixed3_from_fixed2(centre, 0), type);
    if (!site) return false;
    if (type == MT_HUMAN_OIL_PLATFORM || type == MT_ORC_OIL_PLATFORM) {
        resourcevent_t *vent = oil_patch(cell);
        P_RemoveMobj(P_MobjById(vent->source_id));
        vent->source_id = site->id;
    }
    for (int r = 0; r < 3; ++r) stock[r] -= price[r];
    site->owner = unit->owner;
    site->team = unit->team;
    site->allegiance = unit->allegiance;
    site->core.angle = ANG270;
    site->harvest.target = -1;
    site->hp = 1;
    site->w2.build_tics = mobjinfo[type].w2.costs.time * 6;
    site->w2.build_left_tics = site->w2.build_tics;
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
static int hp_at(const mobj_t *site, int left) {
    int total = site->w2.build_tics;
    if (total <= 0) return site->max_hp;
    if (left < 0) left = 0;
    return 1 + (int)((int64_t)(site->max_hp - 1) * (total - left) / total);
}

void w2_advance_build(mobj_t *site, int ticks) {
    int before = hp_at(site, site->w2.build_left_tics);
    site->w2.build_left_tics -= ticks;
    site->hp += hp_at(site, site->w2.build_left_tics) - before;
    if (site->hp > site->max_hp) site->hp = site->max_hp;
    if (site->w2.build_left_tics <= 0) {
        mobj_t *builder = P_MobjById(site->w2.builder);
        site->w2.build_left_tics = 0;
        site->w2.builder = 0;
        P_SetMobjState(site, mobjinfo[site->type_id].spawnstate);
        if (builder) {
            S_Bark(&builder, 1, SE_WORK_COMPLETE, false);
            release(builder);
            if (mobjinfo[site->type_id].w2.gives_mask)
                W2_HarvestOrder(builder, fixed3_xy(site->core.position));
        }
        return;
    }
    int percent = W2_BuildProgress(site);
    int stage = percent < 25 ? 0 : percent < 50 ? 1 : 2;
    int state = W2_BUILD_STATE(site->type_id - 1) + stage;
    if (site->core.state_id != state) P_SetMobjState(site, state);
}

bool W2_TickBuild(mobj_t *unit) {
    if (!worker(unit) || unit->w2.build_phase == W2_BUILD_NONE) return false;
    if (unit->w2.build_phase == W2_BUILD_TO_SITE) {
        if (!W2_Buildable(unit->w2.build_type)) { release(unit); return false; }
        if (P_HasMoveOrder(unit)) return false;
        if (!unit->movement.order_arrived ||
            !fixed2_near(fixed3_xy(unit->core.position), unit->movement.goal, FIXED_LIT(0.001))) {
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
    w2_advance_build(site, 1);
    return true;
}

/* Cancelling returns the whole price (retail refunds a cancelled site). */
bool W2_CancelConstruction(mobj_t *site) {
    if (!W2_UnderConstruction(site) || site->owner >= 8) return false;
    const int *price = mobjinfo[site->type_id].w2.costs.resources;
    for (int r = 0; r < 3; ++r) level.player_resources[site->owner][r] += price[r];
    mobj_t *builder = P_MobjById(site->w2.builder);
    site->w2.build_left_tics = 0;
    site->w2.builder = 0;
    if (builder && builder->w2.site == site->id) release(builder);
    w2_clear_footprint(structure_cell(site).x, structure_cell(site).y, mobjinfo[site->type_id].w2.footprint);
    W2_RestoreOilPatch(site);
    P_RemoveMobj(site);
    return true;
}

/* Some node of the ring around a footprint lies in `region`. */
static bool ring_in_region(const mobj_t *walker, const uint16_t *regions, uint16_t region, ivec2_t cell,
                           isize2_t foot) {
    ivec2_t ring[256];
    int count = region ? P_NavRing(&level, P_MobjMoveClass(walker), cell, foot, ring, 256) : 0;
    for (int i = 0; i < count; ++i)
        if (L_Contains(&level, ring[i].x, ring[i].y) && regions[L_Index(&level, ring[i].x, ring[i].y)] == region)
            return true;
    return false;
}

/* The walker's component, 0 when it has none. */
static uint16_t walker_region(const mobj_t *walker, const uint16_t **regions) {
    int cls = P_MobjMoveClass(walker);
    ivec2_t from = P_NavNodeAt(&level, cls, fixed3_xy(walker->core.position));
    *regions = P_NavRegions(&level, cls);
    return *regions && L_Contains(&level, from.x, from.y) ? (*regions)[L_Index(&level, from.x, from.y)] : 0;
}

bool w2_site_reachable(const mobj_t *walker, ivec2_t cell, isize2_t foot) {
    if (!walker) return true;
    const uint16_t *regions;
    uint16_t region = walker_region(walker, &regions);
    return region && ring_in_region(walker, regions, region, cell, foot);
}

/* Stratagus' AI keeps its buildings off the way between a hall and its
 * gold mine; a farm there jams the workers' trips. Calls `lane` with each
 * rectangle until it returns true. */
static bool mining_lanes(int owner, bool (*lane)(irect_t, void *), void *data) {
    for (thinker_t *a = thinkercap.next; a && a != &thinkercap; a = a->next) {
        const mobj_t *hall = (const mobj_t *)a;
        if (a->function != P_MobjThinker || !alive(hall) || hall->owner != owner ||
            hall->type_id >= NUMMOBJTYPES || !(mobjinfo[hall->type_id].w2.store_mask & 1)) continue;
        for (thinker_t *b = thinkercap.next; b && b != &thinkercap; b = b->next) {
            const mobj_t *mine = (const mobj_t *)b;
            if (b->function != P_MobjThinker || !alive(mine) || mine->type_id != MT_GOLD_MINE ||
                W2_Distance(hall, mine) > 12) continue;
            irect_t h = P_MobjCells(hall), m = P_MobjCells(mine);
            int x0 = (h.x < m.x ? h.x : m.x) - 1, y0 = (h.y < m.y ? h.y : m.y) - 1;
            int x1 = (h.x + h.w > m.x + m.w ? h.x + h.w : m.x + m.w) + 1;
            int y1 = (h.y + h.h > m.y + m.h ? h.y + h.h : m.y + m.h) + 1;
            if (lane((irect_t){x0, y0, x1 - x0, y1 - y0}, data)) return true;
        }
    }
    return false;
}

static bool lane_overlaps(irect_t lane, ivec2_t cell, isize2_t foot) {
    return cell.x < lane.x + lane.w && cell.x + foot.w > lane.x &&
        cell.y < lane.y + lane.h && cell.y + foot.h > lane.y;
}

typedef struct { ivec2_t cell; isize2_t foot; } footprint_t;
static bool footprint_in_lane(irect_t lane, void *data) {
    const footprint_t *f = data;
    return lane_overlaps(lane, f->cell, f->foot);
}

bool w2_blocks_mining(int owner, ivec2_t cell, isize2_t foot) {
    footprint_t f = {cell, foot};
    return mining_lanes(owner, footprint_in_lane, &f);
}

static bool collect_lane(irect_t lane, void *data) {
    sitescan_t *scan = data;
    irect_t *lanes = realloc(scan->lanes, (size_t)(scan->lane_count + 1) * sizeof(*lanes));
    if (!lanes) return false;
    scan->lanes = lanes;
    scan->lanes[scan->lane_count++] = lane;
    return false;
}

static bool scan_init(sitescan_t *scan, int owner, uint16_t type, const mobj_t *walker) {
    *scan = (sitescan_t){0};
    if (walker) scan->region = walker_region(walker, &scan->regions);
    size_t cells = (size_t)level.width * level.height;
    if (!cells || !(scan->units = calloc(cells, 1))) return false;
    const w2_stats_t *s = &mobjinfo[type].w2;
    int count = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) count++;
    scan->spaced = malloc((size_t)(count ? count : 1) * sizeof(*scan->spaced));
    if (!scan->spaced) { free(scan->units); return false; }
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *other = (const mobj_t *)th;
        if (blocks_cell(other, NULL)) {
            ivec2_t at = fixed2_cell(fixed3_xy(other->core.position));
            if (L_Contains(&level, at.x, at.y)) scan->units[L_Index(&level, at.x, at.y)] = 1;
        } else if (spaced_structure(other, NULL) &&
                   (((s->store_mask & 4) && (mobjinfo[other->type_id].w2.gives_mask & 4)) ||
                    ((s->flags & W2_HALL) && other->type_id == MT_GOLD_MINE))) {
            scan->spaced[scan->spaced_count++] = other;
        }
    }
    if (!(s->flags & W2_HALL)) mining_lanes(owner, collect_lane, scan);
    return true;
}

static void scan_free(sitescan_t *scan) {
    free(scan->units);
    free(scan->spaced);
    free(scan->lanes);
}

/* A clear ring keeps the base passable. A shipyard's ring is open land on
 * one side, for its builder, and open water on another, for its ships. */
static bool site_with_margin(const sitescan_t *scan, uint16_t type, ivec2_t cell, const mobj_t *walker,
                             const uint8_t *crowd) {
    if (!place(type, cell, NULL, scan)) return false;
    isize2_t foot = mobjinfo[type].w2.footprint;
    for (int i = 0; i < scan->lane_count; ++i)
        if (lane_overlaps(scan->lanes[i], cell, foot)) return false;
    bool shore = (mobjinfo[type].w2.attributes & W2_SHORE_BUILDING) != 0, land = false, water = false;
    for (int y = -2; y <= foot.h + 1; ++y)
        for (int x = -2; x <= foot.w + 1; ++x) {
            if (x >= 0 && y >= 0 && x < foot.w && y < foot.h) continue;
            int index = L_Index(&level, cell.x + x, cell.y + y);
            /* Two cells to the next building: units jam in one-cell lanes. */
            if (x < -1 || y < -1 || x > foot.w || y > foot.h) {
                if (L_Contains(&level, cell.x + x, cell.y + y) && level.cell_solid[index] &&
                    level.cell_terrain[index] == 0) return false;
                continue;
            }
            bool dry = cell_is_land(cell.x + x, cell.y + y);
            if (dry && crowd && crowd[index]) return false; /* Idle troops wall the site off. */
            if (!shore && !dry) return false;
            if (!shore) continue;
            if (!L_Contains(&level, cell.x + x, cell.y + y) || level.cell_solid[index]) return false;
            land |= dry;
            water |= level.cell_terrain[index] == 1;
        }
    return (!shore || (land && water)) &&
        (!walker || (scan->region && ring_in_region(walker, scan->regions, scan->region, cell, foot)));
}

/* The open oil patch nearest `from` that the tanker can sail to. */
static bool oil_site(uint16_t type, fixed2_t from, const mobj_t *walker, ivec2_t *out) {
    int64_t best = 0;
    bool found = false;
    for (int i = 0; i < level.resource_vent_count; ++i) {
        const resourcevent_t *vent = &level.resource_vents[i];
        if (vent->resource_type != 2 || !W2_CanPlace(type, vent->cell, NULL)) continue;
        int64_t d = fixed2_distance_squared64(from, vent->attachment);
        if ((found && d >= best) || !w2_site_reachable(walker, vent->cell, vent->footprint)) continue;
        found = true; best = d; *out = vent->cell;
    }
    return found;
}

/* A footprint with a free ring of land around it, so the base stays
 * passable, nearest to the owner's hall (or any structure, or a worker);
 * a platform goes on the nearest open oil patch. */
bool W2_FindBuildSite(int owner, uint16_t type, ivec2_t *out) {
    if (!out || !W2_Buildable(type)) return false;
    const mobj_t *anchor = NULL, *walker = NULL;
    int best = 0;
    bool platform = type == MT_HUMAN_OIL_PLATFORM || type == MT_ORC_OIL_PLATFORM;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *unit = (const mobj_t *)th;
        if (!alive(unit) || unit->owner != owner || unit->type_id >= NUMMOBJTYPES) continue;
        const w2_stats_t *s = &mobjinfo[unit->type_id].w2;
        int rank = (s->flags & W2_HALL) ? 3 : (s->flags & W2_STRUCTURE) ? 2 : worker(unit) ? 1 : 0;
        if (rank > best) { best = rank; anchor = unit; }
        /* A worker of the kind that builds it must be able to get there. */
        if (!walker && (unit->traits & MF_MOBILE) && worker(unit) && ((s->flags & W2_SEA) != 0) == platform)
            walker = unit;
    }
    if (!anchor) return false;
    fixed2_t centre_at = fixed3_xy(anchor->core.position);
    ivec2_t centre = fixed2_cell(centre_at);
    if (platform) return oil_site(type, centre_at, walker, out);
    isize2_t foot = mobjinfo[type].w2.footprint;
    ivec2_t origin = { centre.x - foot.w / 2, centre.y - foot.h / 2 };
    /* The coast may lie well beyond the town. */
    int reach = (mobjinfo[type].w2.attributes & W2_SHORE_BUILDING) ? 40 : 28;
    sitescan_t scan;
    if (!scan_init(&scan, owner, type, walker)) return false;
    uint8_t *crowd = P_IdleBlockers(&level, NULL, 0);
    bool found = false;
    for (int radius = 1; radius <= reach && !found; ++radius)
        for (int dy = -radius; dy <= radius && !found; ++dy)
            for (int dx = -radius; dx <= radius && !found; ++dx) {
                if (abs(dx) != radius && abs(dy) != radius) continue;
                ivec2_t cell = { origin.x + dx, origin.y + dy };
                if (site_with_margin(&scan, type, cell, walker, crowd)) { *out = cell; found = true; }
            }
    free(crowd);
    scan_free(&scan);
    return found;
}
