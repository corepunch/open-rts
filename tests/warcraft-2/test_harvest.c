#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II harvesting", #c)

static mobj_t *worker_unit;

static void fixture(bool orc) {
    P_FreeLevel(&level);
    P_InitThinkers();
    G_InitGame();
    consoleplayer = 0;
    level.width = 24;
    level.height = 20;
    int cells = level.width * level.height;
    level.tile_ids = calloc((size_t)cells, sizeof(*level.tile_ids));
    level.blocked = calloc((size_t)cells, 1);
    level.cell_solid = calloc((size_t)cells, 1);
    level.cell_terrain = calloc((size_t)cells, 1);
    level.resource_vents = calloc(3, sizeof(*level.resource_vents));
    level.speeds = calloc(1, sizeof(*level.speeds));
    level.speeds->class_count = 2;
    level.speeds->terrain[1][0] = 100;
    mobj_t *hall = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){5, 5}, 0), orc ? 76 : 75);
    hall->owner = 0;
    w2_mark_footprint(3, 3, (isize2_t){4, 4});
    worker_unit = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){9.5f, 7.5f}, 0), orc ? 4 : 3);
    worker_unit->owner = 0;
}

static void mine(int amount) {
    mobj_t *mine = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){13.5f, 6.5f}, 0), 93);
    mine->owner = 15;
    mine->allegiance = ALLEGIANCE_NEUTRAL;
    w2_mark_footprint(12, 5, (isize2_t){3, 3});
    level.resource_vents[level.resource_vent_count++] = (resourcevent_t){
        .cell = {12, 5}, .attachment = {13.5f, 6.5f}, .footprint = {3, 3},
        .amount = amount, .rate = 100, .active = true, .source_id = mine->id,
    };
}

static void tree(ivec2_t cell) {
    int i = L_Index(&level, cell.x, cell.y);
    level.cell_terrain[i] = 2;
    level.blocked[i] = 1;
    level.tile_ids[i] = 0x70;
    level.resource_vents[level.resource_vent_count++] = (resourcevent_t){
        .cell = cell, .attachment = fvec2_cell_center(cell), .footprint = {1, 1},
        .amount = 100, .rate = 100, .active = true, .resource_type = 1,
    };
}

static void tick(int count) { while (count-- > 0) P_Ticker(); }

static int test_gold(bool orc) {
    fixture(orc);
    mine(125);
    CHECK(W2_HarvestOrder(worker_unit, (fvec2_t){13.5f, 6.5f}));
    bool carried = false, hidden = false;
    for (int i = 0; i < 2000 && level.player_resources[0][0] < 125; ++i) {
        tick(1);
        hidden |= !(worker_unit->traits & MF_RENDERABLE);
        carried |= worker_unit->harvest.cargo && worker_unit->core.sprite_id >= W2_TYPE_COUNT;
    }
    CHECK(hidden && carried);
    CHECK(level.player_resources[0][0] == 125);
    CHECK(!level.resource_vents[0].active && level.resource_vents[0].amount == 0);
    CHECK(worker_unit->harvest.cargo == 0);
    CHECK(!level.cell_solid[L_Index(&level, 12, 5)]);
    CHECK(worker_unit->traits & MF_RENDERABLE);
    CHECK(worker_unit->harvest.phase == HARVEST_PHASE_NONE);
    return 0;
}

static int test_lumber(void) {
    fixture(false);
    tree((ivec2_t){12, 10});
    tree((ivec2_t){13, 10});
    CHECK(W2_HarvestOrder(worker_unit, (fvec2_t){12.5f, 10.5f}));
    for (int i = 0; i < 2000 && worker_unit->w2.chops < 50; ++i) tick(1);
    CHECK(worker_unit->w2.chops == 50);
    CHECK(worker_unit->harvest.cargo == 0 && level.player_resources[0][1] == 0);
    CHECK(level.resource_vents[0].amount == 100);
    for (int i = 0; i < 5000 && level.player_resources[0][1] < 200; ++i) tick(1);
    CHECK(level.player_resources[0][1] == 200);
    CHECK(!level.cell_terrain[L_Index(&level, 12, 10)]);
    CHECK(!level.blocked[L_Index(&level, 12, 10)]);
    CHECK(level.tile_ids[L_Index(&level, 12, 10)] == W2_TILE_LOOKUP);
    return 0;
}

static int test_orders(void) {
    fixture(false);
    mine(1000);
    tree((ivec2_t){12, 10});
    worker_unit->harvest.cargo = 40;
    worker_unit->harvest.resource_type = 1;
    CHECK(W2_HarvestOrder(worker_unit, (fvec2_t){13.5f, 6.5f}));
    CHECK(worker_unit->harvest.resource_type == 1);
    for (int i = 0; i < 1500 && !level.player_resources[0][1]; ++i) tick(1);
    CHECK(level.player_resources[0][1] == 40 && !level.player_resources[0][0]);
    for (int i = 0; i < 1500 && worker_unit->harvest.phase != HARVEST_PHASE_MINING; ++i) tick(1);
    CHECK(worker_unit->harvest.phase == HARVEST_PHASE_MINING);
    ticcmd_t invalid = {.order = TC_MOVE, .count = 1, .units = {worker_unit->id},
                       .position = {-FIXED_ONE, -FIXED_ONE, 0}};
    G_RunTiccmd(0, &invalid);
    CHECK(!(worker_unit->traits & MF_RENDERABLE));
    ticcmd_t stop = {.order = TC_STOP, .count = 1, .units = {worker_unit->id}};
    G_RunTiccmd(0, &stop);
    CHECK(worker_unit->traits & MF_RENDERABLE);
    CHECK(worker_unit->harvest.phase == HARVEST_PHASE_NONE);
    CHECK(W2_HarvestOrder(worker_unit, (fvec2_t){12.5f, 10.5f}));
    tick(200);
    G_RunTiccmd(0, &stop);
    CHECK(worker_unit->w2.chops == 0);
    worker_unit->harvest.cargo = 20;
    worker_unit->harvest.resource_type = 0;
    ticcmd_t goods = {.order = TC_RETURN_GOODS, .count = 1, .units = {worker_unit->id}};
    G_RunTiccmd(0, &goods);
    for (int i = 0; i < 1500 && !level.player_resources[0][0]; ++i) tick(1);
    CHECK(level.player_resources[0][0] == 20);
    return 0;
}

static int test_recovery(void) {
    fixture(false);
    mine(200);
    mobj_t *hall = (mobj_t *)thinkercap.next;
    hall->owner = 1; /* Another player's hall must not accept our cargo. */
    CHECK(W2_HarvestOrder(worker_unit, (fvec2_t){13.5f, 6.5f}));
    tick(1000);
    CHECK(worker_unit->harvest.cargo == 100);
    CHECK(level.player_resources[0][0] == 0 && level.player_resources[1][0] == 0);
    CHECK(worker_unit->traits & MF_RENDERABLE);
    hall->owner = 0;
    for (int i = 0; i < 1500 && !level.player_resources[0][0]; ++i) tick(1);
    CHECK(level.player_resources[0][0] == 100);
    /* Losing the depot during its entry wait preserves cargo and unhides us. */
    for (int i = 0; i < 1500 && worker_unit->harvest.phase != HARVEST_PHASE_UNLOADING; ++i) tick(1);
    CHECK(worker_unit->harvest.phase == HARVEST_PHASE_UNLOADING);
    P_RemoveMobj(hall);
    tick(200);
    CHECK(worker_unit->harvest.cargo == 100 && worker_unit->harvest.base == NULL);
    CHECK(worker_unit->traits & MF_RENDERABLE);
    hall = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){5, 5}, 0), 75);
    hall->owner = 0;
    for (int i = 0; i < 1500 && level.player_resources[0][0] < 200; ++i) tick(1);
    CHECK(level.player_resources[0][0] == 200);
    return 0;
}

static int test_competition(void) {
    fixture(false);
    tree((ivec2_t){12, 10});
    tree((ivec2_t){13, 10});
    mobj_t *second = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){10.5f, 11.5f}, 0), 3);
    second->owner = 0;
    CHECK(W2_HarvestOrder(worker_unit, (fvec2_t){12.5f, 10.5f}));
    CHECK(W2_HarvestOrder(second, (fvec2_t){12.5f, 10.5f}));
    P_SetMobjState(worker_unit, gameinfo->mobjinfo[3].spawnstate);
    P_SetMobjState(second, gameinfo->mobjinfo[3].spawnstate);
    worker_unit->harvest.phase = second->harvest.phase = HARVEST_PHASE_MINING;
    worker_unit->w2.chops = 50;
    second->w2.chops = 49;
    CHECK(W2_TickHarvest(worker_unit));
    W2_TickHarvest(second);
    CHECK(worker_unit->harvest.cargo == 100 && second->harvest.cargo == 0);
    CHECK(second->w2.chops == 0 && second->harvest.target == 1);
    return 0;
}

static int test_mining_group(void) {
    fixture(false);
    mine(1000);
    mobj_t *units[3] = {worker_unit};
    for (int i = 1; i < 3; ++i) {
        units[i] = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){9.5f, 7.5f + i}, 0), 3);
        units[i]->owner = 0;
    }
    CHECK(P_HarvestUnitsAt(&level, units, 3, (fvec2_t){13.5f, 6.5f}));
    for (int i = 0; i < 6000 && level.player_resources[0][0] < 1000; ++i) tick(1);
    CHECK(level.player_resources[0][0] == 1000);
    for (int i = 0; i < 3; ++i)
        CHECK(units[i]->harvest.cargo == 0 && (units[i]->traits & MF_RENDERABLE));
    fixture(false);
    mine(1000);
    for (int y = 0; y < level.height; ++y)
        level.cell_solid[L_Index(&level, 11, y)] = level.blocked[L_Index(&level, 11, y)] = 1;
    CHECK(!W2_HarvestOrder(worker_unit, (fvec2_t){13.5f, 6.5f}));
    CHECK(worker_unit->harvest.phase == HARVEST_PHASE_NONE);
    return 0;
}

static int test_income_and_training(void) {
    fixture(false);
    mobj_t *keep = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){20, 15}, 0), 89);
    keep->owner = 0;
    mobj_t *mill = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){20, 10}, 0), 77);
    mill->owner = 0;
    CHECK(W2_ResourceIncome(0, 0) == 110 && W2_ResourceIncome(0, 1) == 125);
    worker_unit->harvest.cargo = 100;
    worker_unit->harvest.resource_type = 1;
    CHECK(W2_ReturnGoods(worker_unit));
    CHECK(worker_unit->harvest.base->type_id == 75); /* Bonus applies at the nearer hall. */
    for (int i = 0; i < 1500 && !level.player_resources[0][1]; ++i) tick(1);
    CHECK(level.player_resources[0][1] == 125);
    P_RemoveMobj(keep);
    P_RemoveMobj(mill);
    CHECK(W2_ResourceIncome(0, 0) == 100 && W2_ResourceIncome(0, 1) == 100);
    mobj_t *barracks = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){20, 5}, 0), 61);
    barracks->owner = 0;
    const StaticProductDefinition *archer = G_ModelProductByUIId(NULL, 5);
    level.player_resources[0][0] = 1000;
    level.player_resources[0][1] = 49;
    CHECK(!G_PlayerBuildProduct(barracks, archer));
    CHECK(level.player_resources[0][0] == 1000 && level.player_resources[0][1] == 49);
    level.player_resources[0][0] = 400;
    level.player_resources[0][1] = 100;
    CHECK(!G_PlayerBuildProduct(barracks, archer));
    CHECK(level.player_resources[0][0] == 400 && level.player_resources[0][1] == 100);
    level.player_resources[0][0] = 500;
    ticcmd_t train = {.order = TC_BUILD, .product = 5, .count = 1, .units = {barracks->id}};
    G_RunTiccmd(0, &train);
    CHECK(level.player_resources[0][0] == 0 && level.player_resources[0][1] == 50);
    CHECK(barracks->production && barracks->production->queue_count == 1);
    return 0;
}

int main(void) {
    RTS_RUN(test_gold(false));
    RTS_RUN(test_gold(true));
    RTS_RUN(test_lumber());
    RTS_RUN(test_orders());
    RTS_RUN(test_recovery());
    RTS_RUN(test_competition());
    RTS_RUN(test_mining_group());
    RTS_RUN(test_income_and_training());
    P_FreeLevel(&level);
    puts("PASS: gold/lumber, depletion, orders, depot recovery, tree competition, income and training");
    return 0;
}
