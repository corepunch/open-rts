#include "p_reinforce.h"
#include "dc_facing.h"
#include "info.h"
#include "dc_types.h"

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
    unit->core.position = fixed3_from_fvec2(
        fvec2_cell_center((ivec2_t){ spawn_x, spawn_y }), 0);
    unit->owner = team == 0 ? 0 : 1;
    if (unit->owner == 0) {
        bool has_selected_player = false;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *other = (mobj_t *)th;
            if (!other->remove && other->owner == 0 && P_MobjIsSelected(other)) {
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
