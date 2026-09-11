#include "game.h"
#include "g_game.h"
#include "info.h"
#include "rts_model_test.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int row_id;
    int ui_id;
    RtsProductClass product_class;
    int product_type;
    int prerequisite_count;
    int prerequisites[3];
} ExpectedProduct;

static const ExpectedProduct expected_products[] = {
    {  0, 206, RTS_PRODUCT_BUILDING, 16, 0, { 0 } },
    {  1,  80, RTS_PRODUCT_BUILDING, 17, 1, { 0 } },
    {  2,  81, RTS_PRODUCT_BUILDING, 20, 1, { 0 } },
    {  3,  82, RTS_PRODUCT_BUILDING, 18, 2, { 2, 1 } },
    {  6,  83, RTS_PRODUCT_BUILDING, 22, 1, { 4 } },
    {  4,  85, RTS_PRODUCT_BUILDING, 21, 1, { 2 } },
    {  5,  86, RTS_PRODUCT_BUILDING, 19, 2, { 3, 2 } },
    {  7,  87, RTS_PRODUCT_UNIT, 6, 1, { 0 } },
    {  9,  89, RTS_PRODUCT_UNIT, 0, 1, { 1 } },
    { 29,  90, RTS_PRODUCT_UNIT, 43, 2, { 1, 2 } },
    { 13,  94, RTS_PRODUCT_UNIT, 4, 2, { 1, 6 } },
    { 11,  91, RTS_PRODUCT_UNIT, 2, 2, { 3, 2 } },
    { 12,  93, RTS_PRODUCT_UNIT, 3, 2, { 5, 4 } },
    { 10,  92, RTS_PRODUCT_UNIT, 5, 3, { 0, 3, 4 } },
    {  8,  88, RTS_PRODUCT_UNIT, 1, 1, { 5 } },
    { 83, 135, RTS_PRODUCT_UNIT, 49, 3, { 4, 3, 6 } },
};

static const int expected_makers[] = {
    MT_EXCOPOD, MT_EXCOPOD, MT_EXCOPOD, MT_EXCOPOD, MT_EXCOPOD, MT_EXCOPOD,
    MT_EXCOPOD, MT_EXCOPOD, MT_BRRKPOD, MT_BRRKPOD, MT_BRRKPOD, MT_ROBOPOD,
    MT_ROBOPOD2, MT_ROBOPOD, MT_ROBOPOD2, MT_ROBOPOD,
};

static const StaticProductDefinition *find_product(const StaticProductDefinition *products,
                                                    int count, int row_id) {
    for (int i = 0; i < count; ++i)
        if (products[i].row_id == row_id) return &products[i];
    return NULL;
}

static int expected_index(int row_id) {
    for (size_t i = 0; i < sizeof(expected_products) / sizeof(expected_products[0]); ++i)
        if (expected_products[i].row_id == row_id) return (int)i;
    return -1;
}

static mobj_t ready_building(uint16_t actor_id) {
    mobj_t building = { 0 };
    building.type_id = actor_id;
    building.owner = 0;
    building.hp = 100;
    building.max_hp = 100;
    building.core.state_id = mobjinfo[actor_id].spawnstate;
    return building;
}

static int verify_table(const StaticProductDefinition *products, int count) {
    if (count != (int)(sizeof(expected_products) / sizeof(expected_products[0])))
        return rts_fail("production", "retail human product count changed");

    for (size_t i = 0; i < sizeof(expected_products) / sizeof(expected_products[0]); ++i) {
        const ExpectedProduct *expected = &expected_products[i];
        const StaticProductDefinition *product = find_product(products, count, expected->row_id);
        if (!product) return rts_fail("production", "DEPEND row missing from product table");
        if (product->ui_id != expected->ui_id || product->product_class != expected->product_class ||
            product->product_type != expected->product_type ||
            product->prerequisite_count != expected->prerequisite_count ||
            product->maker_count != 1 || product->makers[0] != expected_makers[i] ||
            memcmp(product->prerequisites, expected->prerequisites,
                   (size_t)expected->prerequisite_count * sizeof(int)) != 0)
            return rts_fail("production", "product prerequisites do not match DEPEND.TXT");
        if (G_ModelActorIdForProduct(product) == 0)
            return rts_fail("production", "product has no runtime actor mapping");
    }
    return 0;
}

static int verify_prerequisites(const StaticProductDefinition *products, int count) {
    mobj_t buildings[7];
    mobj_t *units[7];

    for (size_t i = 0; i < sizeof(expected_products) / sizeof(expected_products[0]); ++i) {
        const ExpectedProduct *expected = &expected_products[i];
        const StaticProductDefinition *product = find_product(products, count, expected->row_id);
        if (expected->prerequisite_count == 0) continue;

        for (int j = 0; j < expected->prerequisite_count; ++j) {
            int prerequisite_index = expected_index(expected->prerequisites[j]);
            if (prerequisite_index < 0)
                return rts_fail("production", "product references an unknown prerequisite row");
            buildings[j] = ready_building(
                G_ModelActorIdForProduct(find_product(products, count,
                                                      expected_products[prerequisite_index].row_id)));
            units[j] = &buildings[j];
        }
        if (!G_ModelProductAvailableForUnits(units, expected->prerequisite_count, product))
            return rts_fail("production", "product is unavailable with all prerequisites built");

        for (int missing = 0; missing < expected->prerequisite_count; ++missing) {
            int present_count = 0;
            for (int j = 0; j < expected->prerequisite_count; ++j) {
                if (j == missing) continue;
                buildings[present_count] = ready_building(
                    G_ModelActorIdForProduct(find_product(products, count,
                                                          expected_products[expected_index(
                                                              expected->prerequisites[j])].row_id)));
                units[present_count] = &buildings[present_count];
                present_count++;
            }
            if (G_ModelProductAvailableForUnits(units, present_count, product))
                return rts_fail("production", "product ignores a missing prerequisite");
        }
    }

    mobj_t exco = ready_building(MT_EXCOPOD);
    mobj_t sci_pod = ready_building(MT_SCNCPOD);
    mobj_t robo = ready_building(MT_ROBOPOD);
    mobj_t *partial[] = { &exco, &sci_pod, &robo };
    sci_pod.core.state_id = S_SCNCPOD_BUILD1;
    const StaticProductDefinition *sci_upgrade = find_product(products, count, 4);
    const StaticProductDefinition *reaper = find_product(products, count, 11);
    if (!sci_upgrade || !reaper ||
        G_ModelProductAvailableForUnits(partial, 3, sci_upgrade) ||
        G_ModelProductAvailableForUnits(partial, 3, reaper))
        return rts_fail("production", "construction-state buildings unlock production early");
    return 0;
}

static int verify_all_unit_spawns(RtsGameModel *model, const StaticProductDefinition *products,
                                  int count) {
    (void)model;
    for (int i = 0; i < count; ++i) {
        const StaticProductDefinition *product = &products[i];
        if (product->product_class != RTS_PRODUCT_UNIT) continue;
        uint16_t actor_id = G_ModelActorIdForProduct(product);
        mobj_t *unit = P_SpawnMobj(fixed3_zero(), actor_id);
        if (!unit || unit->remove || unit->core.state_id <= 0 ||
            unit->core.sprite_id < 0 || unit->core.sprite_id >= gameinfo->sprite_count)
            return rts_fail("production", "a listed unit cannot be spawned after training");
        P_RemoveMobj(unit);
    }
    return 0;
}

static int verify_model_construction_gate(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/DCOLONY",
        .map_path = "SCENARIO/HUMAN/HUMAN02.MAP",
    };
    if (!model || !rts_game_model_load(model, &config))
        return rts_fail("production", "load Human02 for construction gate test");

    if (level.destroy_mission) level.destroy_mission(level.mission);
    level.mission = NULL;
    level.destroy_mission = NULL;
    level.player_resources[0][0] = 10000;

    const StaticProductDefinition *science = G_ModelProductByUIId(model, 81);
    const StaticProductDefinition *science_upgrade = G_ModelProductByUIId(model, 85);
    if (!science || !science_upgrade || !G_ModelProductAvailable(model, 0, science))
        return rts_fail("production", "Sci-Pod is not available from the starting Exo Center");

    RtsGameCommand build = {
        .kind = RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON,
        .data.activate_ui_button = { .ui_id = science->ui_id },
    };
    if (!rts_game_model_command(model, &build))
        return rts_fail("production", "start Sci-Pod construction");
    RtsRenderSnapshot construction_snapshot;
    if (!rts_game_model_snapshot(model, &construction_snapshot))
        return rts_fail("production", "snapshot immediately after Sci-Pod construction starts");
    int construction_index = rts_find_unit(&construction_snapshot, 0, MT_SCNCPOD);
    if (construction_index < 0)
        return rts_fail("production", "Sci-Pod construction object missing from snapshot");
    if (G_ModelProductAvailable(model, 0, science_upgrade))
        return rts_fail("production", "Sci-Pod upgrade unlocked during construction");

    RtsRenderSnapshot snapshot;
    bool finished = false;
    for (int tick = 0; tick < 300; ++tick) {
        if (!rts_game_model_snapshot(model, &snapshot))
            return rts_fail("production", "snapshot during Sci-Pod construction");
        int index = rts_find_unit(&snapshot, 0, MT_SCNCPOD);
        if (index >= 0 && gameinfo->states[snapshot.units[index].state_id].group != 6) {
            finished = true;
            break;
        }
        if (!rts_tick(model, NULL))
            return rts_fail("production", "tick Sci-Pod construction");
    }
    if (!finished || !G_ModelProductAvailable(model, 0, science_upgrade))
        return rts_fail("production", "finished Sci-Pod did not unlock its upgrade");

    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/DCOLONY",
        .map_path = "SCENARIO/HUMAN/HUMAN02.MAP",
    };
    if (!model || !rts_game_model_load(model, &config))
        return rts_fail("production", "load Dark Colony product table");

    StaticProductDefinition products[32];
    int count = G_ModelGetProducts(model, 0, products, 32);
    RTS_RUN(verify_table(products, count));
    RTS_RUN(verify_prerequisites(products, count));
    RTS_RUN(verify_all_unit_spawns(model, products, count));
    rts_game_model_destroy(model);
    RTS_RUN(verify_model_construction_gate());
    puts("PASS: Dark Colony DEPEND graph gates every product and every human unit spawns");
    return 0;
}
