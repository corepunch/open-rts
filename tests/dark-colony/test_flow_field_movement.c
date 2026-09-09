#include "engine.h"
#include "p_local.h"
#include "rts_test.h"
#include "info.h"

#include <stdio.h>

static int shared_flow_field_moves_units(void) {
    const char *tag = "flow_field_movement";
    level_t map = { .width = 16, .height = 16 };
    mobj_t units[2] = { 0 };

    for (int i = 0; i < 2; ++i) {
        units[i].core.position = fixed3_from_fvec2(
            (fvec2_t){ 2.5f, 3.5f + (float)i }, fixed_from_float(2.0f));
        units[i].speed = 4.0f;
        units[i].hp = 1;
        units[i].radius = 0.25f;
        units[i].traits = MF_MOBILE;
        units[i].attack.target = -1;
        units[i].harvest.target = -1;
        RTS_CHECK(P_MoveUnitTo(&map, &units[i], (fvec2_t){ 10.5f, 8.5f }), tag,
                  "flow-field order created");
    }

    RTS_CHECK(units[0].movement.flow_field == units[1].movement.flow_field, tag,
              "units with one destination share a flow field");

    int unit_count = 2;
    for (int tic = 0; tic < 300 &&
         (!units[0].movement.order_arrived || !units[1].movement.order_arrived); ++tic) {
        P_Ticker(&map, units, &unit_count, NULL, 0, NULL, 1.0f / 30.0f);
    }

    RTS_CHECK(unit_count == 2, tag, "flow movement preserves both units");
    RTS_CHECK(units[0].movement.order_arrived && units[1].movement.order_arrived,
              tag,
              "both units arrive through the shared flow field");
    RTS_CHECK(units[0].core.position.z == fixed_from_float(2.0f) &&
              units[1].core.position.z == fixed_from_float(2.0f),
              tag,
              "planar flow movement preserves z");

    P_FreeFlowFields(&map);
    return 0;
}

static int exploiter_turns_before_moving(void) {
    const char *tag = "exploiter_turn";
    level_t map = { .width = 16, .height = 16 };
    mobj_t unit = {
        .type_id = MT_DC_EXPLOITER, .traits = MF_MOBILE,
        .core = { .angle = ANG90 },
        .attack.target = -1, .harvest.target = -1,
    };
    unit.core.position = fixed3_from_fvec2((fvec2_t){ 2.0f, 3.0f }, 0);
    P_SpawnMobj(&game_info, &unit);
    fixed3_t start = unit.core.position;
    RTS_CHECK(P_MoveUnitTo(&map, &unit, (fvec2_t){ 10.0f, 3.0f }), tag,
              "create eastbound movement order");
    bool saw_intermediate_pose = false, moved = false;
    int count = 1;
    for (int tic = 0; tic < 120; ++tic) {
        P_Ticker(&map, &unit, &count, NULL, 0, &game_info, 1.0f / 30.0f);
        if (memcmp(&unit.core.position, &start, sizeof(start)) != 0) {
            RTS_CHECK(unit.core.state_id == S_DC_EXPL_RUN1 ||
                      unit.core.state_id == S_DC_EXPL_RUN2, tag,
                      "translation selects the travel cycle");
            moved = true;
            break;
        }
        RTS_CHECK(unit.core.state_id == S_DC_EXPL_STND, tag,
                  "turning stays in the standing state without translating");
        saw_intermediate_pose |= angle_to_direction(unit.core.angle, 16, ANG90, false) & 1;
    }
    P_FreeFlowFields(&map);
    RTS_CHECK(saw_intermediate_pose && moved, tag,
              "turn passes through intermediate stationary facings before travel");
    return 0;
}

int main(void) {
    RTS_RUN(shared_flow_field_moves_units());
    RTS_RUN(exploiter_turns_before_moving());
    puts("All flow-field movement tests passed.");
    return 0;
}
