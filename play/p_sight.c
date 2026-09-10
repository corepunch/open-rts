#include "game.h"
#include "p_local.h"
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

void P_RevealSight(ivec2_t origin, int radius, uint32_t mask, bool airborne) {
    if (!level.sight.cells || radius < 1 || radius > 10) return;
    uint32_t explored = mask & level.sight.allies[0] ? SIGHT_EXPLORED : 0;
    /* DC.EXE 0x4458d0: a blocked branch ends after revealing its own cell.
     * The near-only flag suppresses current sight at depth >= 2, but still
     * allows the local player to discover terrain. Flying sight skips pruning. */
    for (size_t i = 0; i < sizeof(sightnodes) / sizeof(*sightnodes);) {
        ivec2_t offset = sightnodes[i].offset;
        if (offset.x * offset.x + offset.y * offset.y > radius * radius) {
            i = sightnodes[i].end;
            continue;
        }
        ivec2_t cell = ivec2_add(origin, offset);
        if (!L_Contains(&level, cell.x, cell.y)) { i = sightnodes[i].end; continue; }
        int index = L_Index(&level, cell.x, cell.y);
        uint16_t flags = level.tile_flags ? level.tile_flags[index] : MAP_SIGHT_PASS;
        level.sight.cells[index] |= explored;
        if (!(flags & MAP_SIGHT_NEAR) || sightnodes[i].depth < 2)
            level.sight.cells[index] |= mask;
        i = airborne || (flags & MAP_SIGHT_PASS) ? i + 1 : sightnodes[i].end;
    }
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
        /* Engine default for games whose sight stats have not yet been ported. */
        if (!actor->info->sight.day && !actor->info->sight.night &&
            (actor->traits & MF_SELECTABLE) && !(actor->traits & MF_NOBLOCKMAP)) radius = 7;
        ivec2_t origin = {actor->core.position.x >> FIXED_FRAC_BITS, actor->core.position.y >> FIXED_FRAC_BITS};
        P_RevealSight(origin, radius, UINT32_C(0x40000000) >> actor->team,
                      actor->info->sight.airborne || (actor->traits & MF_FLY));
    }
}

int P_SightBrightness(const level_t *map, ivec2_t cell) {
    if (!map->sight.cells) return 16;
    if (!L_Contains(map, cell.x, cell.y)) return 0;
    uint32_t bits = map->sight.cells[L_Index(map, cell.x, cell.y)];
    if (!(bits & SIGHT_EXPLORED)) return 0;
    return bits & map->sight.allies[0] ? 16 : 10;
}

static uint32_t object_sight(const mobj_t *mobj) {
    ivec2_t cell = {mobj->core.position.x >> FIXED_FRAC_BITS, mobj->core.position.y >> FIXED_FRAC_BITS};
    return L_Contains(&level, cell.x, cell.y) ?
        level.sight.cells[L_Index(&level, cell.x, cell.y)] : 0;
}

bool P_VisibleToPlayer(const mobj_t *mobj) {
    if (!mobj || mobj->remove || P_MobjIsHidden(mobj)) return false;
    if (!level.sight.cells) return true;
    return (object_sight(mobj) & level.sight.allies[0]) != 0;
}

bool P_VisibleTo(const mobj_t *observer, const mobj_t *target) {
    if (!target || target->remove) return false;
    if (!level.sight.cells) return true;
    return observer && observer->team < 8 &&
        (object_sight(target) & level.sight.allies[observer->team]) != 0;
}
