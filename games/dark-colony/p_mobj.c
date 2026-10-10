#include "dark-colony.h"
#include "dc_local.h"
#include "engine.h"
#include "info.h"
#include <math.h>
#include <string.h>

ivec2_t DC_OccupiedPosition(const mobj_t *unit) {
    return fixed2_cell(fixed3_xy(unit->core.position));
}

mobj_t *DC_Occupant(ivec2_t cell, bool airborne, bool buried) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (unit->remove || unit->hp <= 0 || (unit->traits & (MF_NOBLOCKMAP | MF_MISSILE)) ||
            !!(unit->traits & MF_FLY) != airborne ||
            !!(unit->traits & MF_LANDMINE) != buried) continue;
        if (ivec2_equal(cell, DC_OccupiedPosition(unit))) return unit;
    }
    return NULL;
}

static uint16_t script_unit_type(int team, int type) {
    if (team != 0) {
        if (type == 0 || (type >= 69 && type <= 76)) return MT_GREY;
        return MT_GREY;
    }
    if (type == 0 || (type >= 69 && type <= 72)) return MT_TROOPER;
    switch (type) {
        case 2: return MT_REAPER;
        case 3: return MT_THUNDERBOLT;
        case 4: return MT_CYBORG;
        case 5: return MT_SCOUT;
        case 6: return MT_EXPLOITER;
        default: return MT_TROOPER;
    }
}

mobj_t *DC_SpawnReinforcement(int team, int gx, int gy, int type) {
    const level_t *map = &level;
    uint16_t type_id = script_unit_type(team, type);
    mobj_t *unit = P_SpawnMobj(fixed3_zero(), type_id);
    if (!unit) return NULL;
    int spawn_x = gx;
    int spawn_y = gy;
    if (map) {
        if (spawn_x < 0) spawn_x = 0;
        if (spawn_x >= map->width) spawn_x = map->width - 1;
        if (spawn_y < 0) spawn_y = 0;
        if (spawn_y >= map->height) spawn_y = map->height - 1;
    }
    unit->core.position = fixed3_with_xy(unit->core.position,
        fixed2_cell_center((ivec2_t){ spawn_x, spawn_y }));
    unit->owner = netgame ? team : team == 0 ? 0 : 1;
    if (unit->owner == consoleplayer) {
        bool has_selected_player = false;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *other = (mobj_t *)th;
            if (!other->remove && other->owner == consoleplayer && P_MobjIsSelected(other)) {
                has_selected_player = true;
                break;
            }
        }
        P_MobjSetSelected(unit, !has_selected_player);
    }
    unit->core.angle = dc_direction_to_angle(unit->owner == 0 ? 6 : 14);
    unit->native_type_id = (uint16_t)(type >= 0 ? type : 0);
    unit->ability_charge = 0x40;
    unit->team = team;
    unit->allegiance = team == 0 ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
    return unit;
}

static bool dropship_cell_occupied(mobj_t *const *units, int unit_count,
                                   ivec2_t cell) {
    for (int i = 0; i < unit_count; ++i) {
        if (units[i]->remove || units[i]->hp <= 0 || (units[i]->traits & MF_FLY)) continue;
        fixed2_t position = fixed3_xy(units[i]->core.position);
        if (fixed_floor_int(position.x) == cell.x &&
            fixed_floor_int(position.y) == cell.y) {
            return true;
        }
    }
    return false;
}

static fixed2_t dropship_drop_position(const level_t *map, mobj_t *const *units,
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
            return fixed2_cell_center(cell);
        }
    }
    return fixed2_cell_center(origin);
}

void A_DC_Drop(mobj_t *ship) {
    dc_drop_t *drop = &ship->drop;
    if (drop->payload_index >= drop->payload_count) return;
    DropshipPayload *payload = &drop->payload[drop->payload_index];
    fixed2_t position = fixed3_xy(ship->core.position);
    if (!DC_SpawnReinforcement(ship->team,
                              fixed_floor_int(position.x), fixed_floor_int(position.y),
                              payload->type)) {
        P_SetMobjState(ship, S_DROP_UNLOAD1);
        return;
    }
    drop->released_count++;
    if (--payload->count == 0) drop->payload_index++;
    mobjlist_t objects = P_ListMobjs();
    fixed2_t goal = drop->payload_index < drop->payload_count ?
        dropship_drop_position(&level, objects.items, objects.count,
                               drop->origin, drop->released_count) :
        fixed2_cell_center(ivec2_add(drop->origin, (ivec2_t){ -1, -1 }));
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
    mobj_t *ship = P_SpawnMobj(fixed3_from_fixed2(
        fixed2_cell_center(ivec2_add(origin, (ivec2_t){ -1, -1 })), 0), MT_DROPSHIP);
    if (!ship) return false;
    ship->team = team;
    ship->owner = netgame ? team : team == 0 ? 0 : 1;
    ship->allegiance = team == 0 ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
    ship->drop = (dc_drop_t){ .origin = origin, .payload_count = payload_count };
    memcpy(ship->drop.payload, payload, (size_t)payload_count * sizeof(*payload));
    ship->core.angle = dc_direction_to_angle(6);
    P_MoveUnitTo(&level, ship, fixed2_cell_center(origin));
    P_SetMobjState(ship, S_DROP_MOVE1);
    S_ActorSound(ship, SE_ACTIVE); /* 0x4177e7: the engine loop follows the ship. */
    return true;
}
