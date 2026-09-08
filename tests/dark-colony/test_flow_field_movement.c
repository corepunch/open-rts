#include "engine.h"
#include "p_local.h"
#include "rts_test.h"

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

int main(void) {
    RTS_RUN(shared_flow_field_moves_units());
    puts("All flow-field movement tests passed.");
    return 0;
}