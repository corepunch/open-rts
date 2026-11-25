#include "engine.h"
#include "info.h"
#include "rustfront.h"

#include <string.h>

/* Footprints block movement: level.blocked is rebuilt from the living structures (the
 * skirmish maps author no terrain obstacles), so a new building blocks and a destroyed one
 * frees its ground. */
void RF_SyncBlocking(level_t *map) {
    size_t cells = (size_t)map->width * map->height;
    if (!map->blocked || !cells) return;
    memset(map->blocked, 0, cells);
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *unit = (const mobj_t *)th;
        if (unit->remove || unit->hp <= 0 || !unit->info || (unit->traits & MF_MOBILE) ||
            unit->info->footprint.w <= 0) continue;
        irect_t rect = P_MobjCells(unit);
        for (int y = rect.y; y < rect.y + rect.h; ++y)
            for (int x = rect.x; x < rect.x + rect.w; ++x)
                if (L_Contains(map, x, y)) map->blocked[L_Index(map, x, y)] = 1;
    }
}

/* The nearest field cell that still holds ore, or NULL. */
static const resourcevent_t *nearest_ore(const level_t *map, fixed2_t from) {
    const resourcevent_t *best = NULL;
    int64_t best_d = 0;
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!P_VentYields(vent)) continue;
        int64_t d = fixed2_length_squared64(fixed2_sub(vent->attachment, from));
        if (!best || d < best_d) { best = vent; best_d = d; }
    }
    return best;
}

/* A harvester with nothing to do (just built, or its cell ran dry) goes to the nearest ore,
 * as long as its side has a refinery to bring it to. */
static void send_idle_harvesters(level_t *map, mobj_t *const *mobjs, int count) {
    for (int i = 0; i < count; ++i) {
        mobj_t *unit = mobjs[i];
        if (!unit || unit->remove || unit->hp <= 0 || !(unit->traits & MF_HARVESTER)) continue;
        if (unit->harvest.phase != HARVEST_PHASE_NONE || unit->harvest.cargo > 0 ||
            P_HasMoveOrder(unit) || !G_ModelHasActorType(NULL, unit->owner, MT_REFINERY)) continue;
        const resourcevent_t *ore = nearest_ore(map, fixed3_xy(unit->core.position));
        if (ore) P_HarvestUnitTo(map, unit, ore->attachment);
    }
}

void RF_MissionTicker(level_t *map, mobj_t *const *mobjs, int count) {
    if (leveltime % (RTS_TICRATE / 2) == 0) RF_SyncBlocking(map);
    if (leveltime % 2 == 0) RF_TrailRockets(mobjs, count);
    if (leveltime % RTS_TICRATE == 0) {
        send_idle_harvesters(map, mobjs, count);
        RF_ClearMinedOre(map);
    }
}

/* Like C&C's, a refinery arrives with its first harvester, parked below its dock. */
void G_UnitProduced(mobj_t *unit, const mobj_t *producer) {
    (void)producer;
    if (!unit || unit->type_id != MT_REFINERY) return;
    irect_t cells = P_MobjCells(unit);
    mobj_t *harvester = P_SpawnMobj(fixed3_zero(), MT_HARVESTER);
    if (!harvester) return;
    harvester->core.position = fixed3_from_fixed2(
        fixed2_cell_center((ivec2_t){cells.x + 1, cells.y + cells.h}), 0);
    harvester->core.angle = ANG270;
    harvester->owner = unit->owner;
    harvester->team = unit->team;
    harvester->allegiance = unit->allegiance;
    RF_SyncBlocking(&level);
}

/* ── effects ───────────────────────────────────────────────────────────────
 * Pictures only: they block nothing, belong to nobody and remove themselves when their run
 * ends. Their scatter comes from the tic and the victim, so every peer draws the same. */

static uint32_t scatter(const mobj_t *victim, uint32_t salt) {
    uint32_t h = victim->id * 2654435761u ^ (uint32_t)leveltime * 40503u ^ salt * 2246822519u;
    h ^= h >> 15;
    h *= 2246822519u;
    return h ^ (h >> 13);
}

/* An effect in the victim's box: within `reach` cells of its centre, drawn just in front. */
static void spawn_effect(uint16_t type, const mobj_t *victim, fixed_t reach_x, fixed_t reach_y, uint32_t salt) {
    uint32_t h = scatter(victim, salt);
    fixed_t dx = reach_x ? (fixed_t)(h % (uint32_t)(2 * reach_x)) - reach_x : 0;
    fixed_t dy = reach_y ? (fixed_t)((h >> 12) % (uint32_t)(2 * reach_y)) - reach_y : 0;
    fixed3_t at = victim->core.position;
    at.x += dx;
    at.y += dy + FIXED_ONE / 16;
    mobj_t *fx = P_SpawnMobj(at, type);
    if (!fx) return;
    fx->owner = fx->team = 0;
    fx->allegiance = ALLEGIANCE_NEUTRAL;
}

static bool is_structure(const mobj_t *unit) {
    return unit->info && unit->info->footprint.w > 0 && !(unit->traits & MF_MOBILE);
}

/* A shot landing: a puff on the victim, anywhere on a structure's face. */
void G_UnitDamaged(mobj_t *victim, const mobj_t *source, int old_hp) {
    (void)source;
    /* A shell or rocket brings its own blast; the puff is for bullets. */
    if (!victim || victim->hp >= old_hp || victim->type_id >= MT_FX_HIT || RF_ImpactInProgress()) return;
    if (is_structure(victim))
        spawn_effect(MT_FX_HIT, victim, victim->info->footprint.w * (FIXED_ONE / 3),
                     victim->info->footprint.h * (FIXED_ONE / 3), 1);
    else
        spawn_effect(MT_FX_HIT, victim, FIXED_ONE / 5, FIXED_ONE / 6, 1);
}

/* Vehicles go up in one explosion; a structure in several across its footprint. Soldiers
 * fall instead (their death run). */
void G_UnitKilled(mobj_t *victim, mobj_t *killer) {
    (void)killer;
    if (!victim || victim->type_id >= MT_FX_HIT) return;
    if (victim->type_id == MT_RIFLEMAN || victim->type_id == MT_ROCKET) return;
    if (!is_structure(victim)) {
        spawn_effect(MT_FX_BOOM, victim, 0, 0, 2);
        return;
    }
    int blasts = victim->info->footprint.w * victim->info->footprint.h;
    if (blasts > 6) blasts = 6;
    for (int i = 0; i < blasts; ++i)
        spawn_effect(i % 2 ? MT_FX_BOOM : MT_FX_BIGBOOM, victim, victim->info->footprint.w * (FIXED_ONE * 2 / 5),
                     victim->info->footprint.h * (FIXED_ONE * 2 / 5), 10 + (uint32_t)i);
}
