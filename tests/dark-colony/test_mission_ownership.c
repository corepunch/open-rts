#include "game.h"
#include "../../games/dark-colony/p_mission.h"
#include <assert.h>
#include <stdlib.h>

/* No retail assets are needed: missing scripts/animations retain the existing
 * empty-script and minimum-unload-duration behavior. */
int main(void) {
    G_InitGame();
    level_t first = { .width = 16, .height = 16 };
    level_t second = { .width = 16, .height = 16 };
    first.mission = load_mission("missing-mission.map");
    second.mission = load_mission("missing-mission.map");
    assert(first.mission && second.mission);
    mobj_t *units = calloc(MAXMOBJS, sizeof(*units));
    effect_t *effects = calloc(64, sizeof(*effects));
    assert(units && effects);
    int count = 0;
    DropshipPayload payload = { .type = 0, .count = 2 };
    assert(DC_StartDropship(&first, effects, 64, 0, (ivec2_t){ 4, 4 }, &payload, 1));
    payload.count = 99; /* The ship must own a copy of the caller's payload. */
    for (int i = 0; i < 400; ++i)
        DC_UpdateDropships(&second, units, &count, effects, 64, gameinfo, 1.0f / 30.0f);
    assert(count == 0); /* Ticking another level cannot advance this delivery. */
    destroy_mission(second.mission);
    for (int i = 0; i < 400; ++i)
        DC_UpdateDropships(&first, units, &count, effects, 64, gameinfo, 1.0f / 30.0f);
    assert(count == 2);
    assert(mission_get_state(first.mission) == MISSION_ACTIVE);
    /* Finished ships release their bounded runtime slots. */
    payload.count = 1;
    for (int i = 0; i < 8; ++i)
        assert(DC_StartDropship(&first, effects, 64, 0, (ivec2_t){ 4, 4 }, &payload, 1));
    assert(!DC_StartDropship(&first, effects, 64, 0, (ivec2_t){ 4, 4 }, &payload, 1));
    destroy_mission(first.mission);
    free(effects);
    free(units);
    return 0;
}
