/* Rustfront in lockstep: two twins play four minutes of Ironbridge, Directorate against Scrap
 * Clans, both sides people whose orders arrive as the network delivers them, and must hash
 * the same every tic. They build, train, harvest until ore cells run dry, drive down the
 * ramps into the canyon and up the mesa, and fight across the fords. Two canaries prove the
 * comparison catches a changed unit and a changed cell of ground. See desync_twins.h;
 * test_desync_four.c plays four sides with computer players. */
#include "desync_twins.h"

#define SECOND 30
#define W 64

/* Ironbridge is mirrored west to east: the east side's orders are the west's, mirrored. */
static ivec2_t at(int owner, int x, int y, int width) {
    return owner ? (ivec2_t){W - x - width, y} : (ivec2_t){x, y};
}

static void side(int owner, int tic) {
    bool west = owner == 0;
    switch (tic) {
    case 1 * SECOND: twin_construct(owner, west ? MT_POWER : MT_CL_GENERATOR, at(owner, 4, 13, 2)); break;
    case 12 * SECOND: twin_construct(owner, west ? MT_REFINERY : MT_CL_SMELTER, at(owner, 2, 24, 3)); break;
    case 20 * SECOND: twin_order(owner, TC_MOVE, at(owner, 29, 20, 1), false); break;   /* down to the ford */
    case 30 * SECOND: twin_construct(owner, west ? MT_BARRACKS : MT_CL_CAMP, at(owner, 13, 22, 2)); break;
    case 42 * SECOND: twin_train(owner, west ? MT_BARRACKS : MT_CL_CAMP, west ? MT_RIFLEMAN : MT_CL_RAIDER,
                                 west ? 6 : 8); break;
    case 48 * SECOND: twin_order(owner, TC_MOVE, at(owner, 14, 33, 1), false); break;   /* up the mesa */
    case 55 * SECOND: twin_construct(owner, west ? MT_FACTORY : MT_CL_YARD, at(owner, 14, 25, 3)); break;
    case 60 * SECOND: twin_order(owner, TC_HARVEST, at(owner, 14, 13, 1), true); break;
    case 90 * SECOND: twin_train(owner, west ? MT_FACTORY : MT_CL_YARD, west ? MT_TANK : MT_CL_TRUCK, 2); break;
    case 100 * SECOND: case 150 * SECOND: case 200 * SECOND:
        twin_order(owner, TC_ORDER, at(1 - owner, 8, 23, 1), false); break;               /* attack */
    case 130 * SECOND: twin_train(owner, west ? MT_BARRACKS : MT_CL_CAMP, west ? MT_ROCKET : MT_CL_TORCHER, 4); break;
    }
}

static void two_people(int tic) {
    side(0, tic);
    side(1, tic);
}

static const twin_game_t IRONBRIDGE = {
    .map = "maps/ironbridge.txt", .tics = 240 * SECOND, .humans = 2,
    .plan = {.count = 2, .seat = {{0, RF_DIRECTORATE, 0}, {1, RF_CLANS, 0}}},
    .script = two_people,
};

static int test_ironbridge_in_lockstep(void) {
    uint32_t subsystems;
    twin_report_t r;
    int first = twin_compare(&IRONBRIDGE, &subsystems, &r);
    twin_explain(first, subsystems);
    CHECK(first == -1);
    twin_print("ironbridge, 4 minutes", &r);
    CHECK(r.structures_built >= 6 && r.owner0_refinery && r.owner0_barracks);   /* building */
    CHECK(r.units_trained > 100);                                                /* training, shots */
    CHECK(r.ore_taken > 5000 && r.mined_out > 0);                                /* harvesting, depletion */
    CHECK(r.kills > 0);                                                          /* combat */
    CHECK(r.on_ramp > 0 && r.low_level > 0 && r.high_level > 0);                 /* cliffs and ramps */
    puts("PASS: twins play four minutes of Ironbridge identically: building, harvesting, depletion, combat, ramps");
    return 0;
}

/* Canaries: a twin that changes one unit, or one cell of ground, is caught that tic, and
 * in that subsystem. */
static void hurt_a_unit(void) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->owner == 1 && u->hp > 1) { u->hp--; return; }
    }
}

static void turn_a_cell(void) {
    int i = L_Index(&level, 20, 2);
    level.cell_terrain[i] = (uint8_t)(level.cell_terrain[i] == RF_DefaultTerrain() ? RF_TerrainOfLetter(',')
                                                                                   : RF_DefaultTerrain());
}

static int test_canaries(void) {
    twin_game_t game = IRONBRIDGE;
    game.tics = 60;
    game.script = NULL;
    game.corrupt_tic = 40;
    game.corrupt = hurt_a_unit;
    uint32_t subsystems;
    twin_report_t r;
    int first = twin_compare(&game, &subsystems, &r);
    CHECK(first == 39 && subsystems == 1u << CONSISTENCY_THINKERS);
    game.corrupt = turn_a_cell;
    first = twin_compare(&game, &subsystems, &r);
    CHECK(first == 39 && subsystems == 1u << CONSISTENCY_GAME);
    puts("PASS: a changed unit or cell of ground shows in the twins' hashes the same tic");
    return 0;
}

int main(void) {
    RTS_RUN(test_canaries());
    RTS_RUN(test_ironbridge_in_lockstep());
    return 0;
}
