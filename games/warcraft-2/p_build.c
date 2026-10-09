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

bool W2_BuildCellClear(uint16_t type, ivec2_t cell, const mobj_t *builder) {
    if (!W2_Buildable(type) || !L_Contains(&level, cell.x, cell.y)) return false;
    /* Platforms are checked as a whole against their exact oil patch. */
    if (type == MT_HUMAN_OIL_PLATFORM || type == MT_ORC_OIL_PLATFORM) return true;
    int index = L_Index(&level, cell.x, cell.y);
    int terrain = level.cell_terrain ? level.cell_terrain[index] : 0;
    bool shore = (mobjinfo[type].w2.attributes & W2_SHORE_BUILDING) != 0;
    if (level.cell_solid && level.cell_solid[index]) return false;
    if (shore ? terrain != 1 && terrain != 3 : terrain != 0) return false;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *other = (const mobj_t *)th;
        if (other == builder || !alive(other) || (other->traits & (MF_MISSILE | MF_NOBLOCKMAP | MF_FLY))) continue;
        if (other->type_id >= NUMMOBJTYPES || (mobjinfo[other->type_id].w2.flags & W2_STRUCTURE)) continue;
        if (ivec2_equal(cell, fvec2_cell(fixed3_xy_to_fvec2(other->core.position)))) return false;
    }
    return true;
}

bool W2_CanPlace(uint16_t type, ivec2_t cell, const mobj_t *builder) {
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
            if (!W2_BuildCellClear(type, ivec2_add(cell, (ivec2_t){x, y}), builder)) return false;
            int index = L_Index(&level, cell.x + x, cell.y + y);
            int terrain = level.cell_terrain ? level.cell_terrain[index] : 0;
            shore |= terrain == 3;
        }
    if (shore_building && !shore) return false;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *other = (const mobj_t *)th;
        if (other == builder || !alive(other) || (other->traits & (MF_MISSILE | MF_NOBLOCKMAP))) continue;
        if (other->type_id >= NUMMOBJTYPES) continue;
        if (other->traits & MF_FLY) continue;
        if (mobjinfo[other->type_id].w2.flags & W2_STRUCTURE) {
            /* Wargus shipyards/refineries keep distance > 3 from oil. */
            if ((s->store_mask & 4) && (mobjinfo[other->type_id].w2.gives_mask & 4) &&
                footprint_gap(cell, foot, structure_cell(other), mobjinfo[other->type_id].w2.footprint) <= 3)
                return false;
            if ((s->flags & W2_HALL) && other->type_id == MT_GOLD_MINE &&
                footprint_gap(cell, foot, structure_cell(other), mobjinfo[other->type_id].w2.footprint) <= W2_MINE_GAP)
                return false;
            continue; /* Its cells are solid already. */
        }
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
    if (!P_ApproachFootprint(unit, unit->w2.build_cell, foot, &bay) || !P_MoveUnitTo(&level, unit, bay)) return false;
    unit->movement.order_id = 0;
    return true;
}

bool W2_ConstructOrder(mobj_t *builder, uint16_t type, ivec2_t cell) {
    if (!worker(builder) || builder->owner >= 8 || !W2_CanPlace(type, cell, builder)) return false;
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
    fvec2_t centre = { cell.x + foot.w * 0.5f, cell.y + foot.h * 0.5f };
    mobj_t *site = P_SpawnMobj(fixed3_from_fvec2(centre, 0), type);
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
                W2_HarvestOrder(builder, fixed3_xy_to_fvec2(site->core.position));
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

/* Whether `walker` can step beside the footprint at `cell`. */
bool w2_site_reachable(const mobj_t *walker, ivec2_t cell, isize2_t foot) {
    if (!walker) return true;
    ivec2_t from = fvec2_cell(fixed3_xy_to_fvec2(walker->core.position));
    for (int y = -1; y <= foot.h; ++y)
        for (int x = -1; x <= foot.w; ++x)
            if ((x < 0 || y < 0 || x >= foot.w || y >= foot.h) &&
                P_NavReachable(&level, P_MobjMoveClass(walker), from, ivec2_add(cell, (ivec2_t){x, y})))
                return true;
    return false;
}

/* Stratagus' AI keeps its buildings off the way between a hall and its
 * gold mine; a farm there jams the workers' trips. */
bool w2_blocks_mining(int owner, ivec2_t cell, isize2_t foot) {
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
            if (cell.x < x1 && cell.x + foot.w > x0 && cell.y < y1 && cell.y + foot.h > y0) return true;
        }
    }
    return false;
}

/* A clear ring keeps the base passable. A shipyard's ring is open land on
 * one side, for its builder, and open water on another, for its ships. */
static bool site_with_margin(int owner, uint16_t type, ivec2_t cell, const mobj_t *walker,
                             const uint8_t *crowd) {
    if (!W2_CanPlace(type, cell, NULL)) return false;
    isize2_t foot = mobjinfo[type].w2.footprint;
    if (!(mobjinfo[type].w2.flags & W2_HALL) && w2_blocks_mining(owner, cell, foot)) return false;
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
    return (!shore || (land && water)) && w2_site_reachable(walker, cell, foot);
}

/* The open oil patch nearest `from` that the tanker can sail to. */
static bool oil_site(uint16_t type, fvec2_t from, const mobj_t *walker, ivec2_t *out) {
    float best = 0;
    bool found = false;
    for (int i = 0; i < level.resource_vent_count; ++i) {
        const resourcevent_t *vent = &level.resource_vents[i];
        if (vent->resource_type != 2 || !W2_CanPlace(type, vent->cell, NULL)) continue;
        float d = fvec2_distance_squared(from, vent->attachment);
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
    fvec2_t centre = fixed3_xy_to_fvec2(anchor->core.position);
    if (platform) return oil_site(type, centre, walker, out);
    isize2_t foot = mobjinfo[type].w2.footprint;
    ivec2_t origin = { (int)floorf(centre.x) - foot.w / 2, (int)floorf(centre.y) - foot.h / 2 };
    /* The coast may lie well beyond the town. */
    int reach = (mobjinfo[type].w2.attributes & W2_SHORE_BUILDING) ? 40 : 28;
    uint8_t *crowd = P_IdleBlockers(&level, NULL, 0);
    bool found = false;
    for (int radius = 1; radius <= reach && !found; ++radius)
        for (int dy = -radius; dy <= radius && !found; ++dy)
            for (int dx = -radius; dx <= radius && !found; ++dx) {
                if (abs(dx) != radius && abs(dy) != radius) continue;
                ivec2_t cell = { origin.x + dx, origin.y + dy };
                if (site_with_margin(owner, type, cell, walker, crowd)) { *out = cell; found = true; }
            }
    free(crowd);
    return found;
}
