#include "../rts_model_test.h"
#include "game.h"
#include "../../games/7legion/info.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *msg) { return rts_fail("7legion", msg); }

static int count_owner(const RtsRenderSnapshot *s, uint8_t owner) {
    int n = 0;
    for (int i = 0; i < s->unit_count; ++i)
        if (s->units[i].owner == owner) n++;
    return n;
}

static int count_owner_type(const RtsRenderSnapshot *s, uint8_t owner, uint16_t type) {
    int n = 0;
    for (int i = 0; i < s->unit_count; ++i)
        if (s->units[i].owner == owner && s->units[i].type_id == type) n++;
    return n;
}

static int test_map_loads(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/7LEGION" };
    if (!model || !rts_game_model_load(model, &config)) return fail("load default map");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");
    if (snap.map_width != 128 || snap.map_height != 128) return fail("128x128 map");
    if (snap.unit_count < 8) return fail("units spawn");
    if (count_owner(&snap, 0) < 2) return fail("player has units");
    if (count_owner(&snap, 1) < 2) return fail("enemy has units");
    if (snap.resource_vent_count < 2) return fail("resource vents placed");
    if (snap.player_resources[0][0] <= 0) return fail("starting cash");
    printf("PASS: 7legion map loads with %d units, %d vents, %d credits\n",
           snap.unit_count, snap.resource_vent_count, snap.player_resources[0][0]);
    rts_game_model_destroy(model);
    return 0;
}

static int test_select_and_move(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/7LEGION" };
    if (!rts_game_model_load(model, &config)) return fail("load for move");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");
    int player = -1;
    for (int i = 0; i < snap.unit_count; ++i)
        if (snap.units[i].owner == 0 && (snap.units[i].traits & RTS_RENDER_TRAIT_MOBILE)) {
            player = i; break;
        }
    if (player < 0) return fail("find player mobile unit");
    fvec2_t start = snap.units[player].position;
    RtsGameCommand sel = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { player, false } };
    if (!rts_game_model_command(model, &sel)) return fail("select");
    fvec2_t target = { start.x + 4.0f, start.y };
    RtsGameCommand move = { .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = { .target = target } };
    if (!rts_game_model_command(model, &move)) return fail("move");
    fvec2_t before = snap.units[player].position;
    for (int t = 0; t < 120; ++t)
        if (!rts_tick(model, &snap)) return fail("tick");
    float dist = fabsf(snap.units[player].position.x - before.x) +
                 fabsf(snap.units[player].position.y - before.y);
    if (dist < 0.1f) return fail("unit moved");
    printf("PASS: 7legion select and move\n");
    rts_game_model_destroy(model);
    return 0;
}

static int test_production(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/7LEGION" };
    if (!rts_game_model_load(model, &config)) return fail("load for production");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");
    RtsProductDefinition products[32];
    int product_count = rts_game_model_products(model, products, 32);
    if (product_count < 4) return fail("product table has entries");
    int mobile_bases_before = count_owner_type(&snap, 0, MT_MOBILE_BASE);
    if (mobile_bases_before < 1) return fail("player has mobile base");
    int troopers_before = count_owner_type(&snap, 0, MT_TROOPER);
    RtsGameCommand build = {
        .kind = RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON,
        .data.activate_ui_button = { .ui_id = 1 },
    };
    if (!rts_game_model_command(model, &build)) return fail("queue trooper");
    bool built = false;
    for (int t = 0; t < 30 * 60 && !built; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick production");
        RtsGameEvent ev;
        while (rts_game_model_poll_event(model, &ev))
            if (ev.type == RTS_GAME_EVENT_UNIT_BUILT) built = true;
    }
    if (!built) return fail("trooper was built");
    if (count_owner_type(&snap, 0, MT_TROOPER) <= troopers_before)
        return fail("trooper count increased");
    printf("PASS: 7legion production builds troopers\n");
    rts_game_model_destroy(model);
    return 0;
}

static int test_ai_production(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/7LEGION" };
    if (!rts_game_model_load(model, &config)) return fail("load for AI");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot for AI");
    int initial_enemy = count_owner(&snap, 1);
    int initial_player = count_owner(&snap, 0);
    int initial_resources = snap.player_resources[0][0];
    for (int t = 0; t < 30 * 60; ++t)
        if (!rts_tick(model, NULL)) return fail("tick AI");
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot after AI");
    if (count_owner(&snap, 1) <= initial_enemy) return fail("enemy AI produced units");
    if (count_owner(&snap, 0) > initial_player || snap.player_resources[0][0] < initial_resources)
        return fail("AI leaves human production and resources alone");
    printf("PASS: 7legion enemy AI produced units\n");
    rts_game_model_destroy(model);
    return 0;
}

static int test_harvesting(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/7LEGION" };
    if (!rts_game_model_load(model, &config)) return fail("load for harvest");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");
    int harvester = -1;
    for (int i = 0; i < snap.unit_count; ++i)
        if (snap.units[i].owner == 0 &&
            (snap.units[i].traits & RTS_RENDER_TRAIT_HARVESTER)) {
            harvester = i; break;
        }
    if (harvester < 0) return fail("find player harvester");
    RtsGameCommand sel = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { harvester, false } };
    if (!rts_game_model_command(model, &sel)) return fail("select harvester");
    if (snap.resource_vent_count <= 0) return fail("vents exist");
    RtsGameCommand harvest = { .kind = RTS_GAME_COMMAND_HARVEST_SELECTED,
        .data.harvest_selected = { .target = level.resource_vents[0].attachment } };
    if (!rts_game_model_command(model, &harvest)) return fail("order harvester to native vent");
    int initial_resources = snap.player_resources[0][0];
    for (int t = 0; t < 30 * 60; ++t)
        if (!rts_tick(model, &snap)) return fail("tick harvest");
    if (snap.player_resources[0][0] <= initial_resources) return fail("harvester delivered resources");
    printf("PASS: 7legion harvesting (resources %d -> %d)\n",
           initial_resources, snap.player_resources[0][0]);
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    RTS_RUN(test_map_loads());
    RTS_RUN(test_select_and_move());
    RTS_RUN(test_production());
    RTS_RUN(test_ai_production());
    RTS_RUN(test_harvesting());
    return 0;
}
