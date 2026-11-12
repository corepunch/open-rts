#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"
#include <stdio.h>
#include <unistd.h>

#define CHECK(c) RTS_CHECK(c, "Z native detour", #c)

static int test_sparse_objects(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/Z"};
    CHECK(model && rts_game_model_load(model, &config));
    zmap_t *map = level.native_data;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker) P_RemoveMobj((mobj_t *)th);
    P_RunThinkers();
    map->fort_count = 0; map->territory_count = 1;
    map->territories[0].options = 0;
    memset(map->bridges, 0, sizeof(map->bridges));
    memset(map->cliffs, 0, sizeof(map->cliffs));
    snprintf(map->dir, sizeof(map->dir), "/private/tmp");
    snprintf(map->objects, sizeof(map->objects), "O%ld.DAT", (long)getpid());
    snprintf(map->buildings, sizeof(map->buildings), "B%ld.DAT", (long)getpid());
    char path[128], building_path[128];
    snprintf(path, sizeof(path), "/private/tmp/%s", map->objects);
    snprintf(building_path, sizeof(building_path), "/private/tmp/%s", map->buildings);
    uint8_t records[7][10];
    memset(records, 0xff, sizeof(records));
    records[1][0] = 64; records[1][1] = 1; records[1][2] = 64; records[1][3] = 1;
    records[1][4] = 9; records[1][5] = 0;
    records[2][0] = 80; records[2][1] = 1; records[2][2] = 80; records[2][3] = 1;
    records[2][4] = 0x25; records[2][5] = MT_UNIT_FIRE_TRUCK - MT_UNITS;
    records[4][0] = 96; records[4][1] = 1; records[4][2] = 96; records[4][3] = 1;
    records[4][4] = 33; records[4][5] = MT_UNIT_JEEP - MT_UNITS;
    records[5][0] = 112; records[5][1] = 1; records[5][2] = 112; records[5][3] = 1;
    records[5][4] = 64; records[5][5] = MT_UNIT_JEEP - MT_UNITS;
    records[6][0] = 128; records[6][1] = 1; records[6][2] = 128; records[6][3] = 1;
    records[6][4] = 59;
    FILE *f = fopen(path, "wb");
    CHECK(f && fwrite(records, sizeof(records), 1, f) == 1 && fclose(f) == 0);
    f = fopen(building_path, "wb");
    CHECK(f && fclose(f) == 0);
    Z_LoadThings();
    remove(path);
    remove(building_path);
    mobj_t *flag = NULL, *crate = NULL, *jeep = NULL, *wreck = NULL, *truck_wreck = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_FLAG) flag = mo;
        if (mo->type_id == MT_GRENADES) crate = mo;
        if (mo->type_id == MT_UNIT_JEEP) jeep = mo;
        if (mo->type_id == MT_DEAD_GATTLING) wreck = mo;
        if (mo->type_id == MT_EX_DEAD_TRUCK) truck_wreck = mo;
        CHECK(mo->type_id != MT_UNIT_FIRE_TRUCK);
    }
    CHECK(flag && crate && jeep && wreck && truck_wreck && flag->z.object_slot == 1 &&
          crate->z.object_slot == 2 && jeep->z.object_slot == 3 && wreck->z.object_slot == 4 &&
          truck_wreck->z.object_slot == 5 && wreck->z.object_hp == 2 && !truck_wreck->z.object_hp &&
          Z_FreeObjectSlot() == 6);
    rts_game_model_destroy(model);
    return 0;
}

static int test_ejected_cannon(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/Z"};
    CHECK(model && rts_game_model_load(model, &config));
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker) P_RemoveMobj((mobj_t *)th);
    P_RunThinkers();
    zmap_t *map = level.native_data;
    memset(map->terrain, 0, sizeof(map->terrain));
    memset(level.blocked, 0, level.width * level.height);
    map->territory_count = 0;
    fixed2_t cannon_at = FIXED2_LIT(20.5,20.5);
    mobj_t *gun = P_SpawnMobj(fixed3_from_fixed2(cannon_at, 0), MT_UNIT_LIGHT_GUN);
    CHECK(gun);
    Z_SetTeam(gun, 0);
    gun->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    gun->z.squad = 200;
    CHECK(Z_EjectCrew(gun));
    mobj_t *robot = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == MT_UNIT_GRUNT && mo->team == 0) robot = mo;
    }
    CHECK(robot && robot->z.just_left_cannon);
    isize2_t size;
    CHECK(Z_SpriteSize(map, states[gun->core.state_id].frame, &size));
    ivec2_t centre = fixed2_cell(fixed2_scale(cannon_at, FIXED_FROM_INT(Z_TILE)));
    ivec2_t origin = ivec2_sub(centre, (ivec2_t){size.w / 2, size.h / 2});
    fixed2_t start;
    CHECK(Z_UnitCentre(map, Z_UnitType(robot), ivec2_add(origin, (ivec2_t){0,48}), &start));
    robot->core.position = fixed3_from_fixed2(start, 0);
    robot->core.angle = P_PointToAngle(0, -FIXED_ONE);
    ticcmd_t move = {.order = TC_ORDER, .position = fixed3_from_fixed2(FIXED2_LIT(20.5,10.5), 0),
                     .count = 1, .units = {robot->id}};
    G_RunTiccmd(0, &move);
    Z_DetourTicker();
    CHECK(robot->z.just_left_cannon && !robot->z.detour.phase);
    uint32_t robot_id = robot->id, gun_id = gun->id, hash = G_Consistency();
    robot->z.just_left_cannon = false;
    CHECK(G_Consistency() != hash);
    robot->z.just_left_cannon = true;
    void *save = NULL; size_t bytes = 0; app_t app = {0}; hudtext_t hud = {0};
    CHECK(G_SaveGameBlob(&save, &bytes, "Ejected cannon", &app, rts_game_model_ai(model), &hud));
    CHECK(G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud));
    free(save);
    robot = P_MobjById(robot_id); gun = P_MobjById(gun_id); map = level.native_data;
    CHECK(robot && gun && robot->z.just_left_cannon);
    memset(map->terrain, 0, sizeof(map->terrain));
    memset(level.blocked, 0, level.width * level.height);
    Z_DetourTicker();
    CHECK(robot->z.just_left_cannon && !robot->z.detour.phase);
    mobj_t *follower = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(5.5,5.5), 0), MT_UNIT_GRUNT);
    mobj_t *outsider = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(6.5,5.5), 0), MT_UNIT_GRUNT);
    CHECK(follower && outsider);
    Z_SetTeam(follower, 0); Z_SetTeam(outsider, 0);
    follower->z.squad = robot->z.squad; follower->z.squad_slot = 1;
    outsider->z.squad = 201; outsider->z.squad_slot = UINT8_MAX;
    follower->z.just_left_cannon = outsider->z.just_left_cannon = true;
    ticcmd_t board = {.order = TC_BOARD, .target = gun->id, .position = gun->core.position,
                      .count = 1, .units = {robot->id}};
    G_RunTiccmd(0, &board);
    CHECK(!robot->z.just_left_cannon && !follower->z.just_left_cannon &&
          outsider->z.just_left_cannon && robot->z.board == gun->id);
    P_RemoveMobj(follower); P_RemoveMobj(outsider);
    ticcmd_t stop = {.order = TC_STOP, .count = 1, .units = {robot->id}};
    G_RunTiccmd(0, &stop);
    G_RunTiccmd(0, &move);
    Z_DetourTicker();
    CHECK(robot->z.detour.target == gun->id);
    Z_DetourCancel(robot);
    robot->core.position = gun->core.position;
    CHECK(Z_DetourBoard(robot, gun));
    CHECK(Z_EjectCrew(gun));
    robot = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == MT_UNIT_GRUNT && !mo->remove && mo->team == 0) robot = mo;
    }
    CHECK(robot && robot->z.just_left_cannon);
    robot->core.position = fixed3_from_fixed2(start, 0);
    robot->core.angle = P_PointToAngle(0, -FIXED_ONE);
    move.units[0] = robot->id;
    G_RunTiccmd(0, &move);
    board.units[0] = robot->id; board.queued = true;
    G_RunTiccmd(0, &board);
    CHECK(robot->z.just_left_cannon && robot->z.route.count == 1);
    Z_DetourTicker();
    CHECK(robot->z.just_left_cannon && !robot->z.detour.phase);
    P_ClearMove(robot); robot->movement.order_arrived = true;
    Z_RouteTick(robot);
    CHECK(!robot->z.just_left_cannon && robot->z.board == gun->id);
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    if (test_sparse_objects()) return 1;
    if (test_ejected_cannon()) return 1;
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/Z"};
    CHECK(model && rts_game_model_load(model, &config));
    zmap_t *map = level.native_data;
    /* Native flag markers precede loaded effects in the same first-free
     * array. Sparse OBJECT record indices must not become slot numbers. */
    int expected_slot = 1, flag_count = 0, object_count = 0;
    mobj_t *first_object = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *mo = (mobj_t *)th;
        if (!mo->z.object_slot) continue;
        CHECK(mo->z.object_slot == expected_slot++);
        if (mo->type_id == MT_FLAG) { CHECK(!object_count); ++flag_count; }
        else { if (!first_object) first_object = mo; ++object_count; }
    }
    CHECK(flag_count && object_count && first_object && Z_FreeObjectSlot() == expected_slot);
    uint8_t freed_slot = first_object->z.object_slot;
    P_RemoveMobj(first_object);
    CHECK(Z_FreeObjectSlot() == freed_slot);
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
