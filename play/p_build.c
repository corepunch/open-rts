#include "engine.h"
#include "info.h"
#ifdef RTS_GAME_WARCRAFT_2
#include "warcraft-2.h"
#endif
#ifdef RTS_GAME_STARCRAFT
#include "starcraft.h"
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
    const mobjtype_t *actor = P_ActorType(type);
    uint16_t replace = actor ? actor->build_on_type : 0;
    if (!irect_contains((irect_t){0, 0, level.width, level.height}, cell)) return false;
#ifdef RTS_GAME_7LEGION
    int index = L_Index(&level, cell.x, cell.y);
    if (level.cell_terrain) {
        if (level.cell_terrain[index] != 1 && level.cell_terrain[index] != 2) return false;
    } else
#endif
    if (replace ? (level.blocked && level.blocked[L_Index(&level, cell.x, cell.y)]) :
                  !L_IsWalkable(&level, cell.x, cell.y)) return false;
    if (!replace && level.cell_solid && level.cell_solid[L_Index(&level, cell.x, cell.y)]) return false;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *other = (const mobj_t *)th;
        if (replace && other->type_id == replace) continue;
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
#if defined(RTS_GAME_7LEGION) || defined(RTS_GAME_STARCRAFT)
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
    if (actor->build_on_type) {
        bool found = false;
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            const mobj_t *source = (const mobj_t *)th;
            if (source->remove || source->hp <= 0 || source->type_id != actor->build_on_type) continue;
            irect_t bounds = P_MobjCells(source);
            if (ivec2_equal((ivec2_t){bounds.x, bounds.y}, cell) &&
                bounds.w == actor->footprint.w && bounds.h == actor->footprint.h) found = true;
        }
        if (!found) return false;
    }
#ifdef RTS_GAME_STARCRAFT
    if (!sc_ground_ok(type, cell, builder)) return false;
#endif
    isize2_t foot = actor->footprint;
    for (int y = 0; y < foot.h; ++y)
        for (int x = 0; x < foot.w; ++x) {
            if (actor->foundation && actor->foundation[y * foot.w + x] == ' ') continue;
            if (!P_BuildingCellClear(type, ivec2_add(cell, (ivec2_t){x, y}), builder)) return false;
        }
    return true;
#endif
}

bool P_MorphMobj(mobj_t *mo, uint16_t type) {
    const mobjtype_t *to = P_ActorType(type);
    if (!mo || mo->remove || mo->hp <= 0 || !to) return false;
    int old_max = mo->max_hp > 0 ? mo->max_hp : 1;
    unsigned selected = mo->traits & MF_SELECTED;
    mo->hp = (int)((int64_t)mo->hp * to->max_hp / old_max);
    if (mo->hp < 1) mo->hp = 1;
    mo->max_hp = to->max_hp;
    mo->speed = to->speed;
    mo->core.sprite_name[0] = '\0';
    mo->info = NULL;
    P_ApplyActorTypeDefaults(mo, to);
    mo->traits |= selected;
    mo->attack.target = NULL;
    P_ClearMove(mo);
    if (gameinfo && type < gameinfo->mobj_type_count) P_SetMobjState(mo, gameinfo->mobjinfo[type].spawnstate);
    return true;
}

/* Approach the actual footprint, never the blocked building centre. The
 * nav component check excludes banks and trees behind an enclosing wall. */
bool P_ApproachFootprint(mobj_t *unit, ivec2_t cell, isize2_t size, fvec2_t *bay) {
    fvec2_t from = fixed3_xy_to_fvec2(unit->core.position);
    bool found = false;
    float distance = 0;
    for (int y = -1; y <= size.h; ++y)
        for (int x = -1; x <= size.w; ++x) {
            if (x >= 0 && y >= 0 && x < size.w && y < size.h) continue;
            ivec2_t candidate = ivec2_add(cell, (ivec2_t){x, y});
            fvec2_t at = fvec2_cell_center(candidate);
            float d = fvec2_distance_squared(from, at);
            if ((found && d >= distance) || !P_CheckPosition(&level, unit, at.x, at.y)) continue;
            bool occupied = false;
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                if (th->function != P_MobjThinker) continue;
                const mobj_t *other = (const mobj_t *)th;
                if (other == unit || other->remove || other->hp <= 0 || !(other->traits & MF_MOBILE)) continue;
                float radius = P_MobjRadius(unit) + P_MobjRadius(other);
                if (fvec2_distance_squared(at, fixed3_xy_to_fvec2(other->core.position)) < radius * radius) {
                    occupied = true;
                    break;
                }
            }
            if (occupied) continue;
            if (!P_NavReachable(&level, P_MobjMoveClass(unit), fvec2_cell(from), candidate)) continue;
            *bay = at;
            distance = d;
            found = true;
        }
    return found;
}
