#include "p_drop.h"
#include "p_reinforce.h"
#include "p_local.h"
#include "dc_facing.h"
#include "info.h"
#include <math.h>
#include <string.h>

static bool dropship_cell_occupied(mobj_t *const *units, int unit_count,
                                   ivec2_t cell) {
    for (int i = 0; i < unit_count; ++i) {
        if (units[i]->remove || units[i]->hp <= 0 || (units[i]->traits & MF_FLY)) continue;
        fvec2_t position = fixed3_xy_to_fvec2(units[i]->core.position);
        if ((int)floorf(position.x) == cell.x &&
            (int)floorf(position.y) == cell.y) {
            return true;
        }
    }
    return false;
}

static fvec2_t dropship_drop_position(const level_t *map, mobj_t *const *units,
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
    dc_drop_t *drop = &ship->drop;
    if (drop->payload_index >= drop->payload_count) return;
    DropshipPayload *payload = &drop->payload[drop->payload_index];
    fvec2_t position = fixed3_xy_to_fvec2(ship->core.position);
    if (!DC_SpawnReinforcement(ship->team,
                              (int)floorf(position.x), (int)floorf(position.y),
                              payload->type)) {
        P_SetMobjState(ship, S_DROP_UNLOAD1);
        return;
    }
    drop->released_count++;
    if (--payload->count == 0) drop->payload_index++;
    mobjlist_t objects = P_ListMobjs();
    fvec2_t goal = drop->payload_index < drop->payload_count ?
        dropship_drop_position(&level, objects.items, objects.count,
                               drop->origin, drop->released_count) :
        fvec2_cell_center(ivec2_add(drop->origin, (ivec2_t){ -1, -1 }));
    P_FreeMobjList(&objects);
    P_MoveUnitTo(&level, ship, goal);
    P_SetMobjState(ship, S_DROP_MOVE1);
}

void A_DC_Arrive(mobj_t *ship) {
    if (ship->drop.payload_index >= ship->drop.payload_count)
        P_SetMobjState(ship, S_NULL);
}

bool DC_StartDropship(int team, ivec2_t origin,
                      const DropshipPayload *payload, int payload_count) {
    if (!payload || payload_count <= 0 ||
        payload_count > DROPSHIP_MAX_PAYLOAD_TYPES) return false;
    for (int i = 0; i < payload_count; ++i)
        if (payload[i].count <= 0) return false;
    mobj_t *ship = P_SpawnMobj(fixed3_from_fvec2(
        fvec2_cell_center(ivec2_add(origin, (ivec2_t){ -1, -1 })), 0), MT_DROPSHIP);
    if (!ship) return false;
    ship->team = team;
    ship->owner = team == 0 ? 0 : 1;
    ship->allegiance = team == 0 ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
    ship->drop = (dc_drop_t){ .origin = origin, .payload_count = payload_count };
    memcpy(ship->drop.payload, payload, (size_t)payload_count * sizeof(*payload));
    ship->core.angle = dc_direction_to_angle(6);
    P_MoveUnitTo(&level, ship, fvec2_cell_center(origin));
    P_SetMobjState(ship, S_DROP_MOVE1);
    return true;
}
