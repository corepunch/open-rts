#include "game.h"
#include "info.h"
#include "p_mission.h"
#include "p_drop.h"
#include <assert.h>

int main(void) {
    G_InitGame();
    P_InitThinkers();
    level = (level_t){ .width = 16, .height = 16 };
    level.mission = load_mission("missing-mission.map");
    level.destroy_mission = destroy_mission;
    assert(level.mission);
    mobj_t *removed = P_SpawnMobj(fixed3_zero(), 0);
    assert(removed);
    P_RemoveMobj(removed);
    DropshipPayload payload[] = { { .type = 0, .count = 2 }, { .type = 2, .count = 1 } };
    assert(DC_StartDropship(0, (ivec2_t){4, 4}, payload, 2));
    mobj_t *ship = (mobj_t *)thinkercap.prev;
    payload[0].count = 99;
    P_Ticker();
    assert(thinkercap.next == &ship->thinker);
    assert(ship->drop.payload[0].count == 2);
    for (int i = 0; i < 1000; ++i) P_Ticker();
    mobjlist_t objects = P_ListMobjs();
    assert(objects.count == 3);
    assert(objects.items[0]->type_id == MT_TROOPER && objects.items[1]->type_id == MT_TROOPER);
    assert(objects.items[2]->type_id == MT_REAPER);
    assert(mission_get_state(level.mission) == MISSION_ACTIVE);
    P_FreeMobjList(&objects);
    P_FreeLevel(&level);
    assert(!level.mission && thinkercap.next == &thinkercap);
    return 0;
}
