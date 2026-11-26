/* Rustfront in lockstep with computer players: twins play Crossroads, two people (scripted)
 * and two computer players, everyone for themselves, and must hash the same every tic. The
 * people's twins are different seats, so the computer must not play by who is watching.
 * Two minutes by default (the test runner's budget), --long four. See desync_twins.h. */
#include "desync_twins.h"

#define SECOND 30

/* Crossroads' starts: 0 north-west, 1 north-east (the map is mirrored both ways). */
static void two_people(int tic) {
    switch (tic) {
    case 1 * SECOND: twin_construct(0, MT_POWER, (ivec2_t){12, 4});
                     twin_construct(1, MT_CL_GENERATOR, (ivec2_t){58, 4}); break;
    case 9 * SECOND: twin_construct(1, MT_CL_SMELTER, (ivec2_t){67, 12}); break;
    case 12 * SECOND: twin_construct(0, MT_REFINERY, (ivec2_t){2, 12}); break;
    case 22 * SECOND: twin_construct(1, MT_CL_CAMP, (ivec2_t){68, 3}); break;
    case 25 * SECOND: twin_order(0, TC_MOVE, (ivec2_t){12, 26}, false); break;      /* down the ramp to the ford */
    case 32 * SECOND: twin_construct(0, MT_BARRACKS, (ivec2_t){2, 3});
                      twin_train(1, MT_CL_CAMP, MT_CL_RAIDER, 8); break;
    case 40 * SECOND: twin_order(1, TC_MOVE, (ivec2_t){37, 25}, false); break;      /* to the plaza */
    case 48 * SECOND: twin_train(0, MT_BARRACKS, MT_RIFLEMAN, 6);
                      twin_order(0, TC_MOVE, (ivec2_t){23, 6}, false); break;       /* up the mesa */
    case 60 * SECOND: twin_order(0, TC_HARVEST, (ivec2_t){15, 14}, true);
                      twin_order(1, TC_ORDER, (ivec2_t){60, 44}, false); break;     /* raid the south-east */
    case 80 * SECOND: twin_order(0, TC_ORDER, (ivec2_t){35, 26}, false); break;
    case 100 * SECOND: twin_order(1, TC_ORDER, (ivec2_t){9, 12}, false); break;     /* raid the west */
    case 130 * SECOND: twin_order(0, TC_ORDER, (ivec2_t){63, 11}, false); break;    /* and back east */
    case 170 * SECOND: twin_train(1, MT_CL_CAMP, MT_CL_TORCHER, 4);
                       twin_order(1, TC_ORDER, (ivec2_t){9, 12}, false); break;
    }
}

int main(int argc, char **argv) {
    bool long_game = argc > 1 && !strcmp(argv[1], "--long");
    twin_game_t game = {
        .map = "maps/crossroads.txt", .tics = (long_game ? 240 : 120) * SECOND, .humans = 2,
        .plan = {.count = 4, .seat = {{0, RF_DIRECTORATE, 0}, {1, RF_CLANS, 0}, {2, RF_CONCORD, 0}, {3, RF_CLANS, 0}}},
        .script = two_people,
    };
    uint32_t subsystems;
    twin_report_t r;
    int first = twin_compare(&game, &subsystems, &r);
    twin_explain(first, subsystems);
    CHECK(first == -1);
    twin_print(long_game ? "crossroads, 4 minutes" : "crossroads, 2 minutes", &r);
    CHECK(r.structures_built >= 8 && r.owner0_refinery && r.owner0_barracks);
    CHECK(r.ore_taken > 5000 && r.mined_out > 0 && r.kills > 0);
    CHECK(r.on_ramp > 0 && r.low_level > 0 && r.high_level > 0);
    printf("PASS: twins play %s of Crossroads with two computer players identically\n",
           long_game ? "four minutes" : "two minutes");
    return 0;
}
