#include "p_drop.h"
#include "p_reinforce.h"
#include "dc_facing.h"
#include "info.h"
#include <math.h>
#include <string.h>

static bool dropship_cell_occupied(const mobj_t *units, int unit_count,
                                   ivec2_t cell) {
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].remove || units[i].hp <= 0 || (units[i].traits & MF_FLY)) continue;
        fvec2_t position = fixed3_xy_to_fvec2(units[i].core.position);
        if ((int)floorf(position.x) == cell.x &&
            (int)floorf(position.y) == cell.y) {
            return true;
        }
    }
    return false;
}

static fvec2_t dropship_drop_position(const level_t *map, const mobj_t *units,
                                      int unit_count, ivec2_t origin, int slot) {
    static const ivec2_t drop_formation[] = {
        { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 },
        { 0, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 },
        { 1, 1 }, { -2, 0 }, { 2, 0 }, { 0, -2 },
    };

    int formation_count = (int)(sizeof(drop_formation) /
                                sizeof(drop_formation[0]));
    for (int attempt = 0; attempt < formation_count; ++attempt) {
        ivec2_t offset = drop_formation[(slot + attempt) % formation_count];
        ivec2_t cell = ivec2_add(origin, offset);
        if ((!map || L_IsWalkable(map, cell.x, cell.y)) &&
            !dropship_cell_occupied(units, unit_count, cell)) {
            return fvec2_cell_center(cell);
        }
    }
    return fvec2_cell_center(origin);
}

void A_DC_Drop(mobj_t *ship) {
    statecontext_t *ctx = P_GetStateContext();
    if (!ctx || !ctx->mobjs || !ctx->mobj_count) return;
    dc_drop_t *drop = &ship->drop;
    DropshipPayload *payload = &drop->payload[drop->payload_index];
    fvec2_t position = fixed3_xy_to_fvec2(ship->core.position);
    int count = *ctx->mobj_count;
    DC_SpawnReinforcement(ctx->map, ctx->mobjs, ctx->mobj_count, ship->team,
                          (int)floorf(position.x), (int)floorf(position.y),
                          payload->type, ctx->game_info);
    if (*ctx->mobj_count == count) {
        P_SetMobjState(ctx, ship, S_DROP_UNLOAD1);
        return;
    }
    drop->released_count++;
    if (--payload->count == 0) drop->payload_index++;
    ship->movement.goal = drop->payload_index < drop->payload_count ?
        dropship_drop_position(ctx->map, ctx->mobjs, *ctx->mobj_count,
                               drop->origin, drop->released_count) :
        fvec2_cell_center(ivec2_add(drop->origin, (ivec2_t){ -1, -1 }));
    P_SetMobjState(ctx, ship, S_DROP_MOVE1);
}

void A_DC_Fly(mobj_t *ship) {
    statecontext_t *ctx = P_GetStateContext();
    if (P_MoveMobjToward(NULL, ship, ship->core.tics * FIXED_DT)) {
        P_SetMobjState(ctx, ship, ship->drop.payload_index < ship->drop.payload_count ?
                       S_DROP_UNLOAD1 : S_NULL);
    }
}

bool DC_StartDropship(mobj_t *units, int *unit_count,
                      int team, ivec2_t origin,
                      const DropshipPayload *payload, int payload_count) {
    if (!payload || payload_count <= 0 ||
        payload_count > DROPSHIP_MAX_PAYLOAD_TYPES) return false;
    for (int i = 0; i < payload_count; ++i)
        if (payload[i].count <= 0) return false;
    mobj_t *ship = P_AllocMobj(units, unit_count);
    if (!ship) return false;
    *ship = (mobj_t){ .team = team,
        .owner = team == 0 ? 0 : 1,
        .allegiance = team == 0 ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY,
        .drop = { .origin = origin, .payload_count = payload_count } };
    memcpy(ship->drop.payload, payload, (size_t)payload_count * sizeof(*payload));
    P_ApplyActorTypeDefaults(ship, actor_type_by_id(MT_DROPSHIP));
    ship->core.position = fixed3_from_fvec2(
        fvec2_cell_center(ivec2_add(origin, (ivec2_t){ -1, -1 })), 0);
    ship->core.angle = dc_direction_to_angle(6);
    ship->movement.goal = fvec2_cell_center(origin);
    P_SpawnMobj(&game_info, ship);
    return true;
}
