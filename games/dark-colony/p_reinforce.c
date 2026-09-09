#include "p_reinforce.h"
#include "dc_facing.h"
#include "info.h"
#include "dc_types.h"
#include <string.h>

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

void DC_SpawnReinforcement(const level_t *map, mobj_t *units, int *unit_count, int team,
                                          int gx, int gy, int type,
                                          const gameinfo_t *game_info) {
    if (!units || !unit_count || *unit_count >= MAXMOBJS) return;
    mobj_t *unit = &units[*unit_count];
    memset(unit, 0, sizeof(*unit));
    int spawn_x = gx;
    int spawn_y = gy;
    if (map) {
        if (spawn_x < 0) spawn_x = 0;
        if (spawn_x >= map->width) spawn_x = map->width - 1;
        if (spawn_y < 0) spawn_y = 0;
        if (spawn_y >= map->height) spawn_y = map->height - 1;
    }
    unit->core.position = fixed3_from_fvec2(
        fvec2_cell_center((ivec2_t){ spawn_x, spawn_y }), 0);
    unit->owner = team == 0 ? 0 : 1;
    if (unit->owner == 0) {
        bool has_selected_player = false;
        for (int i = 0; i < *unit_count; ++i) {
            if (units[i].owner == 0 && P_MobjIsSelected(&units[i])) {
                has_selected_player = true;
                break;
            }
        }
        P_MobjSetSelected(unit, !has_selected_player);
    }
    unit->core.angle = dc_direction_to_angle(unit->owner == 0 ? 6 : 14);
    uint16_t type_id = script_unit_type(team, type);
    const mobjtype_t *actor = actor_type_by_id(type_id);
    unit->native_type_id = (uint16_t)(type >= 0 ? type : 0);
    P_ApplyActorTypeDefaults(unit, actor);
    P_SpawnMobj(game_info, unit);
    (*unit_count)++;
}

