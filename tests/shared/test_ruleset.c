#include "engine.h"
#include "t_local.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) RTS_CHECK(c, g_game_id, #c)

/* Every faction of g_ruleset names real catalog products, and a plan built
 * from it carries its opening, wave sizes and doctrine. */
static int test_factions(void) {
    const ruleset_t *rules = &g_ruleset;
    CHECK(rules->game && !strcmp(rules->game, g_game_id));
    CHECK(rules->factions && rules->faction_count >= 1);
    for (int f = 0; f < rules->faction_count; ++f) {
        const faction_t *faction = &rules->factions[f];
        CHECK(faction->name && faction->opening_count > 0 && faction->opening_count <= AI_MAX_GOALS);
        for (int i = 0; i < faction->opening_count; ++i) {
            CHECK(faction->opening[i].count > 0);
            CHECK(G_ModelProductByUIId(NULL, faction->opening[i].product));
        }
        const AiDoctrine *d = &faction->doctrine;
        CHECK(d->roster_count > 0 && d->roster_count <= AI_MAX_ROSTER);
        int weighted = 0;
        for (int i = 0; i < d->roster_count; ++i) {
            CHECK(G_ModelProductByUIId(NULL, d->roster[i].product));
            weighted += d->roster[i].weight > 0;
        }
        CHECK(weighted > 0);
        AiPlan plan;
        memset(&plan, 0, sizeof(plan));
        CHECK(R_FactionPlan(faction, AI_LEVEL_NORMAL, &plan));
        CHECK(plan.goal_count == faction->opening_count && plan.doctrine.roster_count == d->roster_count);
        CHECK(plan.wave_interval_ms == faction->wave_interval_ms && plan.wave_max_size == faction->wave_max_size);
        CHECK(plan.goals[0].product == faction->opening[0].product);
    }
    CHECK(!R_FactionPlan(NULL, AI_LEVEL_NORMAL, &(AiPlan){0}));
    return 0;
}

/* A faction may carry its own doctrine for a harder AI level. */
static int test_levels(void) {
    static const AiStep opening[] = { {1, 2}, {2, 1} };
    static const AiDoctrine hard = { .workers = 30, .attack_ratio = 50, .roster = { {1, 10} }, .roster_count = 1 };
    faction_t faction = { .name = "Synthetic", .opening = opening, .opening_count = 2,
        .wave_interval_ms = 1234, .wave_min_size = 3, .wave_max_size = 9,
        .doctrine = { .workers = 10, .attack_ratio = 120, .roster = { {2, 5} }, .roster_count = 1 },
        .level_doctrine = { [AI_LEVEL_PLUS] = &hard } };
    AiPlan normal, plus;
    memset(&normal, 0, sizeof(normal));
    memset(&plus, 0, sizeof(plus));
    CHECK(R_FactionPlan(&faction, AI_LEVEL_NORMAL, &normal) && R_FactionPlan(&faction, AI_LEVEL_PLUS, &plus));
    CHECK(normal.doctrine.workers == 10 && normal.doctrine.attack_ratio == 120);
    CHECK(plus.doctrine.workers == 30 && plus.doctrine.attack_ratio == 50 && plus.doctrine.roster[0].product == 1);
    CHECK(plus.goal_count == 2 && plus.wave_interval_ms == 1234 && plus.wave_min_size == 3);
    return 0;
}

/* Requirement vectors: catalog prerequisites as buildings, then the ruleset's rows. */
static int test_requirements(void) {
    int buildings = 0, rows = 0;
    for (int f = 0; f < g_ruleset.faction_count; ++f)
        for (int i = 0; i < g_ruleset.factions[f].opening_count; ++i) {
            const StaticProductDefinition *product =
                G_ModelProductByUIId(NULL, g_ruleset.factions[f].opening[i].product);
            requirement_t reqs[16];
            int count = R_ProductRequirements(product, reqs, 16);
            CHECK(count >= product->prerequisite_count);
            for (int r = 0; r < product->prerequisite_count; ++r)
                CHECK(reqs[r].kind == REQ_BUILDING && reqs[r].id == product->prerequisites[r]);
            buildings += product->prerequisite_count;
            rows += count - product->prerequisite_count;
        }
    CHECK(R_ProductRequirements(NULL, &(requirement_t){0}, 1) == 0);
    CHECK(rows == 0 || g_ruleset.requirement_count > 0);
    (void)buildings;
    return 0;
}

/* A map's patch is part of the consistency hash, and applying none clears it.
 * The entries name no game table, so every game's apply hook must ignore them. */
static int test_patch_set(void) {
    uint32_t clean = R_PatchHash(UINT32_C(2166136261));
    rulepatchset_t set;
    memset(&set, 0, sizeof(set));
    CHECK(R_PatchAdd(&set, 0xFFFF, 2, 3, 4));
    CHECK(R_PatchAdd(&set, 0, 0, 0, 0));
    set.count = RULEPATCH_MAX;
    CHECK(!R_PatchAdd(&set, 1, 1, 1, 1));
    set.count = 1;
    R_PatchApply(&set);
    uint32_t patched = R_PatchHash(UINT32_C(2166136261));
    CHECK(patched != clean);
    set.entries[0].value = 5;
    R_PatchApply(&set);
    CHECK(R_PatchHash(UINT32_C(2166136261)) != patched);
    R_PatchApply(NULL);
    CHECK(g_rulepatch.count == 0 && R_PatchHash(UINT32_C(2166136261)) == clean);
    return 0;
}

/* Start units expand around a start location: the first at the offset, the
 * rest a step apart; every authored one is a real actor type. */
static int test_start_units(void) {
    static const startunit_t units[] = { { 5, 1, {0, 0}, {0, 0} }, { 6, 3, {10, 20}, {2, -1} } };
    faction_t faction = { .name = "Synthetic", .start_units = units, .start_unit_count = 2 };
    startplace_t places[8];
    CHECK(R_StartPlacements(&faction, (ivec2_t){100, 200}, places, 8) == 4);
    CHECK(places[0].type == 5 && places[0].at.x == 100 && places[0].at.y == 200);
    CHECK(places[1].type == 6 && places[1].at.x == 110 && places[1].at.y == 220);
    CHECK(places[3].at.x == 114 && places[3].at.y == 218);
    CHECK(R_StartPlacements(&faction, (ivec2_t){0, 0}, places, 2) == 2);
    CHECK(R_StartPlacements(NULL, (ivec2_t){0, 0}, places, 2) == 0);
    for (int f = 0; f < g_ruleset.faction_count; ++f)
        for (int i = 0; i < g_ruleset.factions[f].start_unit_count; ++i) {
            const startunit_t *unit = &g_ruleset.factions[f].start_units[i];
            CHECK(unit->count > 0 && unit->type > 0 && unit->type < gameinfo->mobj_type_count);
        }
    return 0;
}

/* The ruleset's product view agrees with the catalog it reads, product by product. */
static int test_products(void) {
    int count = R_ProductCount(1);
    StaticProductDefinition list[512];
    CHECK(count > 0 && count == G_ModelGetProducts(NULL, 1, list, 512));
    for (int i = 0; i < count; ++i) {
        product_t product, by_id;
        CHECK(R_ProductAt(1, i, &product) && R_ProductByUiId(list[i].ui_id, &by_id));
        CHECK(product.ui_id == list[i].ui_id && by_id.def == product.def && by_id.time_ms == product.time_ms);
        CHECK(product.kind == list[i].product_class && product.time_ms == G_ModelProductTrainingTimeMs(&list[i]));
        CHECK(product.producer_count == list[i].maker_count && product.prerequisite_count == list[i].prerequisite_count);
        for (int m = 0; m < product.producer_count; ++m) CHECK(product.producers[m] == list[i].makers[m]);
        CHECK(product.cost[0] == list[i].cost);
    }
    CHECK(!R_ProductAt(1, count, &(product_t){0}) && !R_ProductAt(1, -1, &(product_t){0}));
    CHECK(!R_ProductByUiId(-98765, &(product_t){0}));
    /* Affordability reads every resource of the price. */
    product_t first;
    CHECK(R_ProductAt(1, 0, &first));
    int saved[RTS_MAX_RESOURCES];
    memcpy(saved, level.player_resources[1], sizeof(saved));
    for (int r = 0; r < RTS_MAX_RESOURCES; ++r) level.player_resources[1][r] = first.cost[r];
    CHECK(R_CanAfford(1, first.def));
    for (int r = 0; r < RTS_MAX_RESOURCES; ++r) {
        if (first.cost[r] <= 0) continue;
        --level.player_resources[1][r];
        CHECK(!R_CanAfford(1, first.def));
        ++level.player_resources[1][r];
    }
    memcpy(level.player_resources[1], saved, sizeof(saved));
    return 0;
}

int main(void) {
    /* Loading a model initialises the game's catalog. */
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = g_game_default_root };
    CHECK(model && rts_game_model_load(model, &config));
    RTS_RUN(test_factions());
    RTS_RUN(test_levels());
    RTS_RUN(test_requirements());
    RTS_RUN(test_patch_set());
    RTS_RUN(test_start_units());
    RTS_RUN(test_products());
    rts_game_model_destroy(model);
    puts("PASS: ruleset factions, per-level doctrines, requirement vectors and patch hashing");
    return 0;
}
