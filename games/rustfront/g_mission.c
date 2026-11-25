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
