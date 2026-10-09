#include "engine.h"
#ifdef RTS_GAME_WARCRAFT_2
#include "warcraft-2.h"
#endif
#include "p_sight_data.h"
#include <stdlib.h>

bool P_InitSight(void) {
    if (level.width <= 0 || level.height <= 0 ||
        (size_t)level.width > SIZE_MAX / sizeof(uint32_t) / (size_t)level.height)
        return false;
    if (!level.sight.cells)
        level.sight.cells = calloc((size_t)level.width * level.height, sizeof(uint32_t));
    for (int team = 0; team < 8; ++team)
        level.sight.allies[team] |= UINT32_C(0x40000000) >> team;
    return level.sight.cells != NULL;
}

static void reveal_sight(ivec2_t origin, int radius, uint32_t mask, bool airborne, bool detector) {
    if (!level.sight.cells || radius < 1) return;
    uint32_t explored = mask & level.sight.allies[consoleplayer] ? SIGHT_EXPLORED : 0;
    /* Low eight bits hold this pass's detector teams. They share the cell
     * footprint, including near-only cells, without scanning every object
     * from every visited tile. Current sight remains in bits 23..30. */
    if (detector) explored |= mask >> 23;
    if (gameinfo && gameinfo->radial_sight) {
        /* Stratagus ProceedSimpleRadial: a cell is inside when
         * dx^2 + dy^2 < (radius + 1)^2. The Dark Colony ray table stops at
         * radius^2 and keeps only (0, ±radius) and (±radius, 0) on each axis.
         * All four of that cell's shroud corners are hidden, TiledFogTable[15]
         * is the empty mask, and the cell stays fully lit. */
        int limit = (radius + 1) * (radius + 1);
        for (int dy = -radius; dy <= radius; ++dy) {
            int dy2 = dy * dy;
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy2 >= limit) continue;
                ivec2_t cell = {origin.x + dx, origin.y + dy};
                if (!L_Contains(&level, cell.x, cell.y)) continue;
                level.sight.cells[L_Index(&level, cell.x, cell.y)] |= explored | mask;
            }
        }
        return;
    }
    if (radius > SIGHT_MAX_RADIUS) return;
    int distance = radius * radius;
    bool clipped = origin.x < radius || origin.y < radius ||
        origin.x >= level.width - radius || origin.y >= level.height - radius;
    int base = L_Index(&level, origin.x, origin.y);
    /* DC.EXE 0x4458d0: a blocked branch ends after revealing its own cell.
     * The near-only flag suppresses current sight at depth >= 2, but still
     * allows the local player to discover terrain. Flying sight skips pruning. */
    for (size_t i = 0; i < sizeof(sightnodes) / sizeof(*sightnodes);) {
        ivec2_t offset = sightnodes[i].offset;
        if (sightnodes[i].distance > distance) {
            i = sightnodes[i].end;
            continue;
        }
        if (clipped) {
            ivec2_t cell = ivec2_add(origin, offset);
            if (!L_Contains(&level, cell.x, cell.y)) { i = sightnodes[i].end; continue; }
        }
        int index = base + L_Index(&level, offset.x, offset.y);
        uint16_t flags = level.tile_flags ? level.tile_flags[index] : MAP_SIGHT_PASS;
        level.sight.cells[index] |= explored;
        if (!(flags & MAP_SIGHT_NEAR) || sightnodes[i].depth < 2)
            level.sight.cells[index] |= mask;
        i = airborne || (flags & MAP_SIGHT_PASS) ? i + 1 : sightnodes[i].end;
    }
}

void P_RevealSight(ivec2_t origin, int radius, uint32_t mask, bool airborne) {
    reveal_sight(origin, radius, mask, airborne, false);
}

void P_UpdateSight(void) {
    if (!level.sight.cells) return;
    size_t count = (size_t)level.width * level.height;
    for (size_t i = 0; i < count; ++i) level.sight.cells[i] &= SIGHT_EXPLORED;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *actor = (mobj_t *)th;
        if (actor->remove || actor->hp <= 0 || !actor->info || actor->team >= 8) continue;
        int radius = (level.daylight.weight * actor->info->sight.night +
                      (256 - level.daylight.weight) * actor->info->sight.day) >> 8;
#ifdef RTS_GAME_WARCRAFT_2
        if (actor->w2.boarded) continue;
        radius = W2_SightRange(actor);
#endif
        /* Engine default for games whose sight stats have not yet been ported. */
        if (!actor->info->sight.day && !actor->info->sight.night &&
            (actor->traits & MF_SELECTABLE) && !(actor->traits & MF_NOBLOCKMAP)) radius = 7;
        ivec2_t origin = {actor->core.position.x >> FIXED_FRAC_BITS, actor->core.position.y >> FIXED_FRAC_BITS};
        uint32_t mask = UINT32_C(0x40000000) >> actor->team;
        bool airborne = actor->info->sight.airborne || (actor->traits & MF_FLY);
        isize2_t foot = actor->info->footprint;
        if (actor->info->sight_from_footprint && foot.w > 0 && foot.h > 0) {
            /* Warcraft measures sight from the building's edge. */
            fvec2_t centre = fixed3_xy_to_fvec2(actor->core.position);
            ivec2_t corner = {(int)floorf(centre.x - foot.w * 0.5f + 0.001f),
                              (int)floorf(centre.y - foot.h * 0.5f + 0.001f)};
            for (int y = 0; y < foot.h; ++y)
                for (int x = 0; x < foot.w; ++x) {
                    ivec2_t cell = {corner.x + x, corner.y + y};
                    if (L_Contains(&level, cell.x, cell.y))
                        reveal_sight(cell, radius, mask, airborne, actor->traits & MF_DETECTOR);
                }
            continue;
        }
        reveal_sight(origin, radius, mask, airborne, actor->traits & MF_DETECTOR);
    }
}

int P_SightBrightness(const level_t *map, ivec2_t cell) {
    if (!map->sight.cells) return 16;
    if (!L_Contains(map, cell.x, cell.y)) return 0;
    uint32_t bits = map->sight.cells[L_Index(map, cell.x, cell.y)];
    if (!(bits & SIGHT_EXPLORED)) return 0;
    return bits & map->sight.allies[consoleplayer] ? 16 : 10;
}

static uint32_t object_sight(const mobj_t *mobj) {
    ivec2_t cell = {mobj->core.position.x >> FIXED_FRAC_BITS, mobj->core.position.y >> FIXED_FRAC_BITS};
    return L_Contains(&level, cell.x, cell.y) ?
        level.sight.cells[L_Index(&level, cell.x, cell.y)] : 0;
}

uint32_t P_Detectors(const mobj_t *mobj) {
    return level.sight.cells ? (object_sight(mobj) & 255) << 23 : 0;
}

/* A cloaked mobj shows to its owner, to teams sharing its owner's sight
 * and to teams whose detectors cover it (Dark Colony mines, StarCraft cloaks). */
static bool cloaked_from(const mobj_t *mobj, int owner, int team) {
    if (!(mobj->traits & MF_CLOAKED) || mobj->owner == owner) return false;
    if (team < 0 || team >= 8) return true;
    if (mobj->team < 8 && (level.sight.allies[team] & (UINT32_C(0x40000000) >> mobj->team))) return false;
    return !(P_Detectors(mobj) & level.sight.allies[team]);
}

bool P_VisibleToPlayer(const mobj_t *mobj) {
    if (!mobj || mobj->remove || P_MobjIsHidden(mobj)) return false;
#ifdef RTS_GAME_WARCRAFT_2
    if (!W2_VisibleTo(mobj, consoleplayer)) return false;
#endif
    if (cloaked_from(mobj, consoleplayer, consoleplayer)) return false;
    if (!level.sight.cells) return true;
    return (object_sight(mobj) & level.sight.allies[consoleplayer]) != 0;
}

bool P_VisibleTo(const mobj_t *observer, const mobj_t *target) {
    if (!target || target->remove) return false;
#ifdef RTS_GAME_WARCRAFT_2
    if (!observer || !W2_VisibleTo(target, observer->owner)) return false;
#endif
    if (observer && cloaked_from(target, observer->owner, observer->team)) return false;
    if (!level.sight.cells) return true;
    return observer && observer->team < 8 &&
        (object_sight(target) & level.sight.allies[observer->team]) != 0;
}
