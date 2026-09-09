#include "game.h"
#include "info.h"
#include "p_mission.h"
#include "p_drop.h"
#include <assert.h>
#include <stdlib.h>

/* Missions own scripts; dropships and their copied payloads live in the level's
 * ordinary object array, including across compaction. No effects are needed. */
int main(void) {
    G_InitGame();
    level_t first = { .width = 16, .height = 16 };
    level_t second = { .width = 16, .height = 16 };
    first.mission = load_mission("missing-mission.map");
    second.mission = load_mission("missing-mission.map");
    assert(first.mission && second.mission);
    mobj_t *units = calloc(MAXMOBJS, sizeof(*units));
    assert(units);
    int count = 1;
    units[0].remove = true; /* Forces the ship to move within the array. */
    DropshipPayload payload[] = { { .type = 0, .count = 2 }, { .type = 2, .count = 1 } };
    assert(DC_StartDropship(units, &count, 0, (ivec2_t){4, 4}, payload, 2));
    payload[0].count = 99;
    int tics = units[1].core.tics;
    update_mission(&second, units, &count, NULL, 0, gameinfo, NULL, FIXED_DT);
    assert(units[1].core.tics == tics); /* Mission code has no dropship ticker. */
    destroy_mission(second.mission);
    P_Ticker(&first, units, &count, NULL, 0, gameinfo, FIXED_DT);
    assert(count == 1 && units[0].type_id == MT_DROPSHIP);
    assert(units[0].drop.payload[0].count == 2);
    for (int i = 0; i < 1000; ++i)
        P_Ticker(&first, units, &count, NULL, 0, gameinfo, FIXED_DT);
    assert(count == 3);
    assert(units[0].type_id == MT_TROOPER && units[1].type_id == MT_TROOPER);
    assert(units[2].type_id == MT_REAPER);
    assert(mission_get_state(first.mission) == MISSION_ACTIVE);

    /* Capacity belongs to the object array. A blocked release keeps its cargo. */
    count = 0;
    payload[0].count = 1;
    assert(DC_StartDropship(units, &count, 0, (ivec2_t){4, 4}, payload, 1));
    count = MAXMOBJS;
    assert(!DC_StartDropship(units, &count, 0, (ivec2_t){4, 4}, payload, 1));
    statecontext_t ctx = { .map = &first, .mobjs = units, .mobj_count = &count,
        .game_info = gameinfo };
    assert(P_SetMobjState(&ctx, &units[0], S_DROP_RELEASE));
    assert(count == MAXMOBJS && units[0].drop.payload[0].count == 1);
    assert(units[0].core.state_id == S_DROP_UNLOAD1);
    count = 1;
    for (int i = 0; i < 400; ++i)
        P_Ticker(&first, units, &count, NULL, 0, gameinfo, FIXED_DT);
    assert(count == 1 && units[0].type_id == MT_TROOPER);
    destroy_mission(first.mission);
    free(units);
    return 0;
}
