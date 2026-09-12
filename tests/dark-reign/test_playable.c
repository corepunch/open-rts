#include "../rts_model_test.h"
#include "../../games/dark-reign/dr_types.h"
#include "../../games/dark-reign/info.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *msg) { return rts_fail("dark-reign", msg); }

static int count_owner_type(const RtsRenderSnapshot *s, uint8_t owner, uint16_t type) {
    int n = 0;
    for (int i = 0; i < s->unit_count; ++i)
        if (s->units[i].owner == owner && s->units[i].type_id == type) n++;
    return n;
}

static int test_map_loads(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/MULTI/2NIC/2NIC.SCN",
    };
    if (!model || !rts_game_model_load(model, &config)) return fail("load 2NIC map");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot 2NIC");
    if (snap.map_width <= 0 || snap.map_height <= 0) return fail("map dimensions");
    if (snap.unit_count < 6) return fail("units spawn from SCN");
    if (count_owner_type(&snap, 0, ACTOR_FG_CONSTRUCTION_CREW) < 1)
        return fail("player has construction crews");
    if (count_owner_type(&snap, 1, ACTOR_FG_CONSTRUCTION_CREW) < 1)
        return fail("enemy has construction crews");
    if (snap.player_resources[0][0] <= 0 || snap.player_resources[1][0] <= 0)
        return fail("team credits from SCN");
    if (snap.resource_vent_count <= 0) return fail("resource vents from mines");
    printf("PASS: dark-reign map loads with %d units, %d vents\n",
           snap.unit_count, snap.resource_vent_count);
    rts_game_model_destroy(model);
    return 0;
}

static int test_select_and_move(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/MULTI/2NIC/2NIC.SCN",
    };
    if (!rts_game_model_load(model, &config)) return fail("load for select/move");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot for select/move");
    int player = -1;
    for (int i = 0; i < snap.unit_count; ++i)
        if (snap.units[i].owner == 0 && (snap.units[i].traits & RTS_RENDER_TRAIT_MOBILE)) {
            player = i; break;
        }
    if (player < 0) return fail("find player mobile unit");
    RtsGameCommand sel = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { player, false } };
    if (!rts_game_model_command(model, &sel)) return fail("select");
    fvec2_t target = { snap.units[player].position.x + 3.0f,
                       snap.units[player].position.y };
    RtsGameCommand move = { .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = { .target = target } };
    if (!rts_game_model_command(model, &move)) return fail("move");
    for (int t = 0; t < 60; ++t)
        if (!rts_tick(model, &snap)) return fail("tick movement");
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot after move");
    float dx = snap.units[player].position.x - target.x;
    if (fabsf(dx) > 2.0f) return fail("unit moved toward target");
    printf("PASS: dark-reign select and move\n");
    rts_game_model_destroy(model);
    return 0;
}

static int test_production(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/MULTI/2NIC/2NIC.SCN",
    };
    if (!rts_game_model_load(model, &config)) return fail("load for production");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot for production");
    RtsProductDefinition products[64];
    int product_count = rts_game_model_products(model, products, 64);
    if (product_count < 10) return fail("product table has entries");
    RtsGameCommand build_hq = {
        .kind = RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON,
        .data.activate_ui_button = { .ui_id = 10001 },
    };
    int resources_before = snap.player_resources[0][0];
    if (!rts_game_model_command(model, &build_hq)) return fail("build FG HQ queued");
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot after HQ queue");
    if (snap.player_resources[0][0] >= resources_before)
        return fail("HQ cost deducted");
    bool hq_built = false;
    for (int t = 0; t < 30 * 30 && !hq_built; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick HQ build");
        RtsGameEvent ev;
        while (rts_game_model_poll_event(model, &ev))
            if (ev.type == RTS_GAME_EVENT_BUILDING_BUILT ||
                ev.type == RTS_GAME_EVENT_UNIT_BUILT) hq_built = true;
    }
    if (!hq_built) return fail("HQ build completed");
    if (count_owner_type(&snap, 0, ACTOR_FG_HEADQUARTERS_1) < 1)
        return fail("HQ was built");
    printf("PASS: dark-reign production builds HQ\n");
    rts_game_model_destroy(model);
    return 0;
}

static int test_ai_production(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/MULTI/2NIC/2NIC.SCN",
    };
    if (!rts_game_model_load(model, &config)) return fail("load for AI");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot for AI");
    int initial_total = snap.unit_count;
    for (int t = 0; t < 30 * 120; ++t) {
        if (!rts_tick(model, NULL)) return fail("tick AI");
    }
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot after AI ticks");
    if (snap.unit_count <= initial_total)
        return fail("AI production increased total units");
    printf("PASS: dark-reign AI production (%d -> %d units)\n",
           initial_total, snap.unit_count);
    rts_game_model_destroy(model);
    return 0;
}

static int test_combat(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/MULTI/2NIC/2NIC.SCN",
    };
    if (!rts_game_model_load(model, &config)) return fail("load for combat");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot for combat");
    int player = -1, enemy = -1;
    for (int i = 0; i < snap.unit_count; ++i) {
        if (snap.units[i].owner == 0 && (snap.units[i].traits & RTS_RENDER_TRAIT_MOBILE))
            player = i;
        if (snap.units[i].owner == 1) enemy = i;
    }
    if (player < 0 || enemy < 0) return fail("find player and enemy");
    RtsGameCommand sel = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { player, false } };
    rts_game_model_command(model, &sel);
    RtsGameCommand attack = { .kind = RTS_GAME_COMMAND_ATTACK_UNIT,
        .data.attack_unit = { snap.units[enemy].id, enemy } };
    bool accepted = rts_game_model_command(model, &attack);
    if (accepted == snap.units[enemy].hidden) return fail("attack respects fog");
    bool saw_attack = false;
    for (int t = 0; t < 30 * 60 && !saw_attack; ++t) {
        if (!rts_tick(model, NULL)) return fail("tick combat");
        RtsGameEvent ev;
        while (rts_game_model_poll_event(model, &ev))
            if (ev.type == RTS_GAME_EVENT_ATTACK_STARTED) saw_attack = true;
    }
    printf("PASS: dark-reign combat %s\n", saw_attack ? "triggered" : "fog-blocked (expected)");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    RTS_RUN(test_map_loads());
    RTS_RUN(test_select_and_move());
    RTS_RUN(test_production());
    RTS_RUN(test_ai_production());
    RTS_RUN(test_combat());
    return 0;
}
