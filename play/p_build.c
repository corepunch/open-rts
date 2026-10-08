#include "engine.h"
#ifdef RTS_GAME_WARCRAFT_2
#include "warcraft-2.h"
#endif

const mobjtype_t *P_ActorType(uint16_t type) {
    for (int i = 0; i < num_actor_types; ++i)
        if (actor_types[i].id == type) return &actor_types[i];
    return NULL;
}

fvec2_t P_BuildingPosition(uint16_t type, ivec2_t cell) {
    const mobjtype_t *actor = P_ActorType(type);
    fvec2_t at = {(float)cell.x, (float)cell.y};
    if (actor && !actor->corner_anchor)
        at = fvec2_add(at, (fvec2_t){actor->footprint.w * 0.5f, actor->footprint.h * 0.5f});
    return at;
}

bool P_BuildingCellClear(uint16_t type, ivec2_t cell, const mobj_t *builder) {
#ifdef RTS_GAME_WARCRAFT_2
    return W2_BuildCellClear(type, cell, builder);
#else
    (void)type;
    if (!irect_contains((irect_t){0, 0, level.width, level.height}, cell)) return false;
#ifdef RTS_GAME_7LEGION
    int index = L_Index(&level, cell.x, cell.y);
    if (level.cell_terrain) {
        if (level.cell_terrain[index] != 1 && level.cell_terrain[index] != 2) return false;
    } else
#endif
    if (!L_IsWalkable(&level, cell.x, cell.y)) return false;
    if (level.cell_solid && level.cell_solid[L_Index(&level, cell.x, cell.y)]) return false;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *other = (const mobj_t *)th;
        const production_t *queue = other->production;
        if (other != builder && queue && queue->queue_count && queue->placed && !other->remove && other->hp > 0) {
            const mobjtype_t *planned = P_ActorType(queue->actor_id);
            if (planned) {
                ivec2_t local = ivec2_sub(cell, queue->cell);
                if (irect_contains((irect_t){0, 0, planned->footprint.w, planned->footprint.h}, local) &&
                    (!planned->foundation || planned->foundation[local.y * planned->footprint.w + local.x] != ' '))
                    return false;
            }
        }
        if (other->remove || other->hp <= 0 ||
            (other->traits & (MF_FLY | MF_MISSILE | MF_NOBLOCKMAP))) continue;
        irect_t bounds = P_MobjCells(other);
        if (!irect_contains(bounds, cell)) continue;
        const char *mask = other->info ? other->info->foundation : NULL;
        if (!mask || mask[(cell.y - bounds.y) * bounds.w + cell.x - bounds.x] != ' ') return false;
    }
    return true;
#endif
}

/* Bit zero belongs to authored terrain. Bit one is rebuilt from live mobjs,
 * so destruction releases a foundation without erasing native obstacles. */
void P_SyncBuildingBlocking(void) {
#ifdef RTS_GAME_7LEGION
    size_t cells = (size_t)level.width * level.height;
    if (!cells) return;
    if (!level.cell_solid) level.cell_solid = calloc(cells, 1);
    if (!level.cell_solid) return;
    for (size_t i = 0; i < cells; ++i) level.cell_solid[i] &= ~2u;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *unit = (const mobj_t *)th;
        if (unit->remove || unit->hp <= 0 || !unit->info ||
            (unit->traits & (MF_MOBILE | MF_FLY | MF_NOBLOCKMAP | MF_MISSILE)) ||
            unit->info->footprint.w <= 0) continue;
        irect_t rect = P_MobjCells(unit);
        for (int y = 0; y < rect.h; ++y)
            for (int x = 0; x < rect.w; ++x) {
                if (unit->info->foundation && unit->info->foundation[y * rect.w + x] != 'x') continue;
                ivec2_t cell = ivec2_add((ivec2_t){rect.x, rect.y}, (ivec2_t){x, y});
                if (L_Contains(&level, cell.x, cell.y)) level.cell_solid[L_Index(&level, cell.x, cell.y)] |= 2;
            }
    }
#endif
}

bool P_CanPlaceBuilding(uint16_t type, ivec2_t cell, const mobj_t *builder) {
#ifdef RTS_GAME_WARCRAFT_2
    return W2_CanPlace(type, cell, builder);
#else
    const mobjtype_t *actor = P_ActorType(type);
    if (!actor || actor->footprint.w <= 0 || actor->footprint.h <= 0) return false;
    isize2_t foot = actor->footprint;
    for (int y = 0; y < foot.h; ++y)
        for (int x = 0; x < foot.w; ++x) {
            if (actor->foundation && actor->foundation[y * foot.w + x] == ' ') continue;
            if (!P_BuildingCellClear(type, ivec2_add(cell, (ivec2_t){x, y}), builder)) return false;
        }
    return true;
#endif
}
