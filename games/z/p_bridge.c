#include "z_local.h"
#include "info.h"

int Z_BridgeTerritory(const zmap_t *map, const mobj_t *bridge) {
    irect_t r = map->bridges[bridge->z.bridge - 1].bounds;
    return Z_AITerritory(map, fixed2_scale(fixed2_from_cell((ivec2_t){r.x,r.y}), FIXED_ONE / Z_TILE));
}

/* 0x2a096 closes the two deck lanes. 0x2a16d opens them and stamps the
 * outside edges. Its horizontal callers pass x + 16 before it backs up. */
void Z_BridgeTerrain(const mobj_t *bridge, bool intact) {
    zmap_t *map = level.native_data;
    const zbridge_t *native = &map->bridges[bridge->z.bridge - 1];
    irect_t r = native->bounds;
    ivec2_t cell = {r.x / Z_TILE, r.y / Z_TILE};
    ivec2_t along = native->vertical ? (ivec2_t){0,1} : (ivec2_t){1,0};
    ivec2_t across = {along.y, along.x};
    int length = (native->vertical ? r.h : r.w) / Z_TILE;
    for (int i = 0; i < length; ++i) {
        bool end = i == 0 || i == length - 1;
        for (int lane = intact ? 0 : 1; lane < (intact ? 4 : 3); ++lane) {
            ivec2_t at = ivec2_add(cell, ivec2_add(ivec2_scale(along, i),
                                                  ivec2_scale(across, lane)));
            if (!L_Contains(&level, at.x, at.y)) continue;
            int terrain = intact ? (lane == 0 || lane == 3 ? 0x2b : end ? 7 : 0x17) : end ? 0x32 : 0x2f;
            uint8_t *attr = &map->ground_attr[at.y * Z_MAP_STRIDE + at.x];
            *attr = (uint8_t)((*attr & 0x80) | terrain);
        }
    }
    Z_RebuildCliffs(&level);

    if (intact) {
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *mo = (mobj_t *)th;
            if (th->function == P_MobjThinker && mo->type_id == MT_BRIDGE_DEBRIS && mo->z.bridge == bridge->z.bridge)
                P_RemoveMobj(mo);
        }
        return;
    }
    /* 0x29a65 lays one broken-deck sprite per step, starting one tile
     * along the span. The world-specific index is 0x117dd8. */
    int state = S_BRIDGE_DEBRIS + 2 * map->world + !native->vertical;
    isize2_t size;
    if (!Z_SpriteSize(map, states[state].frame, &size)) return;
    for (int i = 1; i < length - 1; ++i) {
        ivec2_t pixel = ivec2_add((ivec2_t){r.x + size.w / 2, r.y + size.h / 2}, ivec2_scale(along, i * Z_TILE));
        fixed2_t at = fixed2_scale(fixed2_from_cell(pixel), FIXED_ONE / Z_TILE);
        mobj_t *debris = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_BRIDGE_DEBRIS);
        if (!debris) break;
        Z_SetTeam(debris, Z_NEUTRAL);
        debris->z.bridge = bridge->z.bridge;
        P_SetMobjState(debris, state);
    }
}

void Z_BridgeDamage(mobj_t *bridge) {
    if (bridge->hp <= 0) Z_BridgeTerrain(bridge, false);
}

bool Z_BridgeOrder(const ticcmd_t *cmd, mobj_t *const *units, int count) {
    const zmap_t *map = level.native_data;
    if (!map) return false;
    ivec2_t at = fixed2_cell(fixed2_scale(fixed3_xy(cmd->position), FIXED_FROM_INT(Z_TILE)));
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *bridge = (mobj_t *)th;
        if (th->function != P_MobjThinker || bridge->type_id != MT_BRIDGE || bridge->remove ||
            bridge->hp <= 0 || !irect_contains(map->bridges[bridge->z.bridge - 1].bounds, at)) continue;
        bool ordered = false;
        for (int i = 0; i < count; ++i) {
            mobj_t *unit = units[i];
            if (!(unit->traits & MF_ATTACK) || !Z_CliffWeapon(unit)) continue;
            Z_CancelService(unit);
            P_ClearMove(unit);
            unit->waypoints = (waypoints_t){0};
            unit->z.board = 0;
            unit->attack.target = bridge;
            ordered = true;
        }
        return ordered;
    }
    return false;
}
