#include "game.h"
#include "g_game.h"
#include "info.h"
#include "p_ai.h"
#include "p_local.h"

#include <assert.h>

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/KKND"};
    assert(model && rts_game_model_load(model, &config));
    mobj_t *truck = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){8,6},0), MT_SURV_RIFLEMAN);
    if (truck) {
        truck->owner = 0;
        truck->team = 0;
        truck->allegiance = ALLEGIANCE_PLAYER;
    }
    assert(truck);
    fixed3_t start = truck->core.position;
    for (int t = 0; t < 1800; ++t) {
        rts_game_model_tick(model, 1.0f / 30.0f);
        assert(truck->core.position.x == start.x && truck->core.position.y == start.y);
        assert(truck->harvest.phase == HARVEST_PHASE_NONE);
        assert(truck->movement.order_id == 0);
    }
    fvec2_t goal = fvec2_add(fixed3_xy_to_fvec2(start), (fvec2_t){3, 0});
    assert(P_MoveUnitTo(&level, truck, goal));
    uint32_t order = truck->movement.order_id;
    for (int t = 0; t < 120; ++t) {
        rts_game_model_tick(model, 1.0f / 30.0f);
        assert(truck->movement.order_id == order);
        assert(truck->harvest.phase == HARVEST_PHASE_NONE);
    }
    assert(fvec2_near(fixed3_xy_to_fvec2(truck->core.position), goal, 0.1f));
    rts_game_model_destroy(model);
    P_InitThinkers();
    level.width = level.height = 64;
    level.blocked = calloc(64*64,1);
    level.resource_vents = calloc(2,sizeof(*level.resource_vents));
    assert(level.blocked && level.resource_vents);
    level.resource_vent_count = 2;
    level.resource_vents[0] = (resourcevent_t){.cell={2,2},.attachment={2,2},.amount=10000,.rate=1,.active=true};
    level.resource_vents[1] = (resourcevent_t){.cell={45,45},.attachment={45,45},.amount=10000,.rate=1,.active=true};
    mobj_t *base = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){40,40},0),MT_MUTE_DRILLRIG);
    truck = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){44,44},0),MT_MUTE_OIL_TANKER);
    assert(base && truck);
    base->owner = truck->owner = 1;
    base->allegiance = truck->allegiance = ALLEGIANCE_ENEMY;
    mobjlist_t objects = P_ListMobjs();
    AiContext ai;
    P_AiInit(&ai);
    P_AiTick(&ai,&level,objects.items,objects.count,gameinfo,33);
    assert(truck->harvest.target == 1); /* Nearby southeast vent, not the one closest to origin. */
    P_FreeMobjList(&objects);
    P_FreeLevel(&level);
    puts("PASS: native human unit stays idle and AI preserves explicit movement");
    return 0;
}
