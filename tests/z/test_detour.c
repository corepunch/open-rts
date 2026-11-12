#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"
#include <stdio.h>

#define CHECK(c) RTS_CHECK(c, "Z native detour", #c)

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/Z"};
    CHECK(model && rts_game_model_load(model, &config));
    zmap_t *map = level.native_data;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker) P_RemoveMobj((mobj_t *)th);
    P_RunThinkers();
    memset(map->terrain, 0, sizeof(map->terrain));
    memset(level.blocked, 0, level.width * level.height);
    map->territory_count = 1;
    map->territories[0].flag = (ivec2_t){20,20};
    map->territories[0].options = 0;
    mobj_t *flag = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(20.5,22.5), FIXED_FROM_INT(2)), MT_FLAG);
    fixed2_t start;
    CHECK(Z_UnitCentre(map, MT_UNIT_GRUNT - MT_UNITS, (ivec2_t){320,368}, &start));
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(start, 0), MT_UNIT_GRUNT);
    CHECK(flag && unit);
    flag->z.territory = 1;
    Z_SetTeam(flag, 1);
    Z_SetTeam(unit, 0);
    unit->z.squad = 200;
    unit->z.squad_slot = UINT8_MAX;
    unit->z.route = (zroute_t){.active = TC_ORDER, .owner = 0};
    unit->core.angle = P_PointToAngle(0, -FIXED_ONE);
    CHECK(P_MoveUnitTo(&level, unit, FIXED2_LIT(20.5,10.5)));
    fixed2_t old = unit->movement.goal;
    unit->z.route.active = TC_MOVE; /* Zod force-move policy. */
    Z_DetourTicker();
    CHECK(!unit->z.detour.phase);
    unit->z.route.active = TC_ORDER;
    fixed2_t at;
    CHECK(Z_UnitCentre(map, Z_UnitType(unit), (ivec2_t){320,448}, &at));
    unit->core.position = fixed3_from_fixed2(at, 0);
    Z_DetourTicker();
    CHECK(!unit->z.detour.phase); /* Grunt's authored 120px reach ends before 128px. */
    CHECK(Z_UnitCentre(map, Z_UnitType(unit), (ivec2_t){320,440}, &at));
    unit->core.position = fixed3_from_fixed2(at, 0);
    Z_DetourTicker();
    CHECK(unit->z.detour.target == flag->id);
    Z_DetourCancel(unit);
    unit->core.position = fixed3_from_fixed2(start, 0);
    CHECK(P_MoveUnitTo(&level, unit, old));
    unit->movement.goal = fixed2_scale(fixed2_from_cell((ivec2_t){332,336}), FIXED_ONE / Z_TILE);
    Z_DetourTicker();
    CHECK(!unit->z.detour.phase); /* Flag destination guard includes both high edges. */
    unit->movement.goal = fixed2_scale(fixed2_from_cell((ivec2_t){333,336}), FIXED_ONE / Z_TILE);
    Z_DetourTicker();
    CHECK(unit->z.detour.target == flag->id);
    Z_DetourCancel(unit);
    CHECK(P_MoveUnitTo(&level, unit, old));
    Z_SetTeam(flag, 0);
    Z_DetourTicker();
    CHECK(!unit->z.detour.phase); /* Own flag marker is invisible. */
    Z_SetTeam(flag, 1);
    unit->core.angle = P_PointToAngle(FIXED_ONE, 0);
    Z_DetourTicker();
    CHECK(!unit->z.detour.phase); /* Flag is outside the direction cone. */
    unit->core.angle = P_PointToAngle(0, -FIXED_ONE);
    map->terrain[22 * Z_MAP_STRIDE + 20] = 1;
    Z_DetourTicker();
    CHECK(!unit->z.detour.phase); /* Raised intervening terrain blocks sight. */
    map->terrain[22 * Z_MAP_STRIDE + 20] = 0;
    mobj_t *blocker = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(20.5,22.5), 0), MT_BOX);
    CHECK(blocker);
    Z_DetourTicker();
    CHECK(!unit->z.detour.phase); /* Nearest visible object is selected before dispatch. */
    P_RemoveMobj(blocker);
    map->territory_count = 2;
    map->territories[1].flag = map->territories[0].flag;
    mobj_t *tie = P_SpawnMobj(flag->core.position, MT_FLAG);
    CHECK(tie);
    tie->z.territory = 2;
    Z_SetTeam(tie, 1);
    Z_DetourTicker();
    CHECK(unit->z.detour.target == tie->id); /* Equal rank replaces with later native slot. */
    Z_DetourCancel(unit);
    P_RemoveMobj(tie);
    map->territory_count = 1;
    CHECK(P_MoveUnitTo(&level, unit, old));
    Z_DetourTicker();
    CHECK(unit->z.detour.target == flag->id && flag->z.detour.claimant == unit->id);
    CHECK(unit->z.detour.resume.x == old.x && unit->z.detour.resume.y == old.y);
    unit->z.route.steps[0] = (zroute_step_t){fixed3_from_fixed2(FIXED2_LIT(15.5,10.5), 0), 0, TC_MOVE};
    unit->z.route.count = 1;
    uint32_t unit_id = unit->id, flag_id = flag->id;
    void *save = NULL; size_t save_size = 0;
    app_t app = {0}; hudtext_t hud = {0};
    CHECK(G_SaveGameBlob(&save, &save_size, "Detour", &app, rts_game_model_ai(model), &hud));
    CHECK(G_LoadGameBlob(save, save_size, &app, rts_game_model_ai(model), &hud));
    free(save);
    unit = P_MobjById(unit_id); flag = P_MobjById(flag_id);
    CHECK(unit && flag && unit->z.detour.target == flag_id && flag->z.detour.claimant == unit_id);
    CHECK(unit->z.detour.resume.x == old.x && unit->z.detour.resume.y == old.y);
    CHECK(unit->z.route.count == 1 && unit->z.route.steps[0].order == TC_MOVE);
    Z_SetTeam(flag, 0); /* Another unit captured the target first. */
    Z_DetourTicker();
    CHECK(!unit->z.detour.phase && !flag->z.detour.claimant && unit->movement.goal.y == old.y);
    Z_SetTeam(flag, 1);
    unit->core.position = fixed3_from_fixed2(start, 0);
    unit->core.angle = P_PointToAngle(0, -FIXED_ONE);
    CHECK(P_MoveUnitTo(&level, unit, old));
    Z_DetourTicker();
    CHECK(unit->z.detour.phase && flag->z.detour.claimant == unit->id);
    Z_SetTeam(unit, 1);
    CHECK(!unit->z.detour.phase && !flag->z.detour.claimant && !P_HasMoveOrder(unit));
    Z_SetTeam(unit, 0);
    unit->z.squad = 200; unit->z.squad_slot = UINT8_MAX;
    unit->z.route = (zroute_t){.active = TC_ORDER, .owner = 0};
    CHECK(P_MoveUnitTo(&level, unit, old));
    Z_DetourTicker();
    CHECK(unit->z.detour.target == flag->id);
    ticcmd_t stop = {.order = TC_STOP, .count = 1, .units = {unit->id}};
    G_RunTiccmd(0, &stop);
    CHECK(!unit->z.detour.phase && !flag->z.detour.claimant);
    unit->z.route = (zroute_t){.active = TC_ORDER, .owner = 0};
    CHECK(P_MoveUnitTo(&level, unit, old));
    Z_DetourTicker();
    CHECK(unit->z.detour.target == flag->id);
    unit->z.route.steps[0] = (zroute_step_t){fixed3_from_fixed2(FIXED2_LIT(15.5,10.5), 0), 0, TC_MOVE};
    unit->z.route.count = 1;
    RtsRenderSnapshot snap;
    bool resumed = false, cheered = false;
    for (int i = 0; i < 600 && !resumed; ++i) {
        CHECK(rts_tick(model, &snap));
        if (unit->z.detour.phase == Z_DETOUR_CHEER) {
            cheered = true;
            CHECK(unit->z.route.count == 1 && unit->z.route.active == TC_ORDER);
        }
        resumed = flag->team == 0 && !unit->z.detour.phase;
    }
    CHECK(resumed && cheered && !flag->z.detour.claimant);
    CHECK(unit->movement.goal.x == old.x && unit->movement.goal.y == old.y);
    CHECK(unit->z.route.count == 1 && unit->z.route.active == TC_ORDER);
    for (int i = 0; i < 1000 && unit->z.route.active != TC_MOVE; ++i) CHECK(rts_tick(model, &snap));
    CHECK(unit->z.route.active == TC_MOVE && unit->z.route.count == 0);
    P_ClearMove(unit);
    unit->core.position = fixed3_from_fixed2(FIXED2_LIT(10.5,10.5), 0);
    mobj_t *solo = P_SpawnMobj(fixed3_from_fixed2(start, 0), MT_UNIT_GRUNT);
    CHECK(solo);
    Z_SetTeam(solo, 0); Z_SetTeam(flag, 1);
    flag->z.hold = 0; flag->z.detour.cooldown = 0;
    solo->z.route = (zroute_t){.active = TC_ORDER, .owner = 0};
    solo->core.angle = P_PointToAngle(0, -FIXED_ONE);
    CHECK(P_MoveUnitTo(&level, solo, old));
    Z_DetourTicker();
    CHECK(solo->z.detour.target == flag->id);
    resumed = false;
    for (int i = 0; i < 600 && !resumed; ++i) {
        CHECK(rts_tick(model, &snap));
        resumed = flag->team == 0 && !solo->z.detour.phase;
    }
    CHECK(resumed && !P_HasMoveOrder(solo) && !solo->z.route.active);
    solo->z.detour.phase = Z_DETOUR_CHEER;
    solo->hp = 0;
    Z_DetourTicker();
    CHECK(!solo->z.detour.phase); /* Dead units cannot retain cheer state. */
    P_RemoveMobj(solo);
    unit->core.position = fixed3_from_fixed2(start, 0);
    unit->core.angle = P_PointToAngle(0, -FIXED_ONE);
    unit->z.route = (zroute_t){.active = TC_ORDER, .owner = 0};
    CHECK(P_MoveUnitTo(&level, unit, old));
    mobj_t *box = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(20.5,20.5), 0), MT_GRENADES_2);
    CHECK(box);
    uint32_t box_id = box->id;
    Z_DetourTicker();
    CHECK(unit->z.detour.target == box_id && box->z.detour.claimant == unit->id);
    resumed = false;
    for (int i = 0; i < 600 && !resumed; ++i) {
        CHECK(rts_tick(model, &snap));
        resumed = !P_MobjById(box_id) && !unit->z.detour.phase;
    }
    CHECK(resumed && unit->z.grenades == 20);
    CHECK(unit->movement.goal.x == old.x && unit->movement.goal.y == old.y);

    unit->core.position = fixed3_from_fixed2(start, 0);
    unit->core.angle = P_PointToAngle(0, -FIXED_ONE);
    unit->z.route = (zroute_t){.active = TC_ORDER, .owner = 0};
    CHECK(P_MoveUnitTo(&level, unit, old));
    isize2_t size;
    CHECK(Z_SpriteSize(map, states[S_JEEP].frame, &size));
    fixed2_t vehicle_pos = fixed2_scale(fixed2_from_cell((ivec2_t){320 + size.w / 2,320 + size.h / 2}),
                                        FIXED_ONE / Z_TILE);
    mobj_t *vehicle = P_SpawnMobj(fixed3_from_fixed2(vehicle_pos, 0), MT_UNIT_JEEP);
    CHECK(vehicle);
    Z_SetTeam(vehicle, Z_NEUTRAL);
    vehicle->z.object = MT_JEEP;
    vehicle->traits &= ~(MF_MOBILE | MF_ATTACK);
    P_SetMobjState(vehicle, S_JEEP);
    Z_DetourTicker();
    CHECK(unit->z.detour.target == vehicle->id && vehicle->z.detour.claimant == unit->id);
    vehicle->hp = 0;
    Z_DetourTicker();
    CHECK(!unit->z.detour.phase && !vehicle->z.detour.claimant && unit->movement.goal.y == old.y);
    vehicle->hp = vehicle->max_hp;
    unit->core.angle = P_PointToAngle(0, -FIXED_ONE);
    Z_DetourTicker();
    CHECK(unit->z.detour.target == vehicle->id && vehicle->z.detour.claimant == unit->id);
    for (int i = 0; i < 600 && !vehicle->z.driver; ++i) CHECK(rts_tick(model, &snap));
    CHECK(vehicle->z.driver && vehicle->team == 0 && !vehicle->z.route.active && !P_HasMoveOrder(vehicle));
    rts_game_model_destroy(model);
    puts("PASS: Z moving units divert to flags, crates and empty vehicles");
    return 0;
}
