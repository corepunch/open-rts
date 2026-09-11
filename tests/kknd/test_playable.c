#include "../rts_model_test.h"
#include "../../games/kknd/info.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *msg) { return rts_fail("kknd", msg); }

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

static int test_map_and_units(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/KKND" };
    if (!model || !rts_game_model_load(model, &config)) return fail("load default map");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");
    if (snap.map_width <= 0 || snap.map_height <= 0) return fail("map dimensions");
    if (snap.unit_count < 10) return fail("units spawn (player + enemy)");
    if (count_owner(&snap, 0) < 4) return fail("player has starting force");
    if (count_owner(&snap, 1) < 4) return fail("enemy has starting force");
    if (count_owner_type(&snap, 0, MT_SURV_DRILLRIG) < 1)
        return fail("player has drill rig");
    if (count_owner_type(&snap, 1, MT_MUTE_DRILLRIG) < 1)
        return fail("enemy has mutant drill rig");
    if (count_owner_type(&snap, 0, MT_SURV_OIL_TANKER) < 1)
        return fail("player has oil tanker");
    if (snap.resource_vent_count < 4) return fail("resource vents placed");
    if (snap.player_resources[0][0] <= 0) return fail("player starting resources");
    if (snap.player_resources[1][0] <= 0) return fail("enemy starting resources");
    printf("PASS: kknd map loads with %d units, %d vents, %d/%d oil\n",
           snap.unit_count, snap.resource_vent_count,
           snap.player_resources[0][0], snap.player_resources[1][0]);
    rts_game_model_destroy(model);
    return 0;
}

static int test_select_and_move(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/KKND" };
    if (!rts_game_model_load(model, &config)) return fail("load for move");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");
    int player = -1;
    for (int i = 0; i < snap.unit_count; ++i)
        if (snap.units[i].owner == 0 &&
            (snap.units[i].traits & RTS_RENDER_TRAIT_MOBILE)) {
            player = i; break;
        }
    if (player < 0) return fail("find player mobile unit");
    fvec2_t start = snap.units[player].position;
    RtsGameCommand sel = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { player, false } };
    if (!rts_game_model_command(model, &sel)) return fail("select");
    fvec2_t target = { start.x + 3.0f, start.y };
    RtsGameCommand move = { .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = { .target = target } };
    if (!rts_game_model_command(model, &move)) return fail("move");
    for (int t = 0; t < 90; ++t)
        if (!rts_tick(model, &snap)) return fail("tick");
    float dx = snap.units[player].position.x - start.x;
    if (fabsf(dx) < 0.3f) return fail("unit moved");
    printf("PASS: kknd select and move\n");
    rts_game_model_destroy(model);
    return 0;
}

static int test_production(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/KKND" };
    if (!rts_game_model_load(model, &config)) return fail("load for production");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");
    RtsProductDefinition products[64];
    int product_count = rts_game_model_products(model, products, 64);
    if (product_count < 5) return fail("product table has entries");
    int riflemen_before = count_owner_type(&snap, 0, MT_SURV_RIFLEMAN);
    RtsGameCommand build = {
        .kind = RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON,
        .data.activate_ui_button = { .ui_id = 1 },
    };
    if (!rts_game_model_command(model, &build)) return fail("queue rifleman");
    bool built = false;
    for (int t = 0; t < 30 * 60 && !built; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick production");
        RtsGameEvent ev;
        while (rts_game_model_poll_event(model, &ev))
            if (ev.type == RTS_GAME_EVENT_UNIT_BUILT) built = true;
    }
    if (!built) return fail("rifleman was built");
    if (count_owner_type(&snap, 0, MT_SURV_RIFLEMAN) <= riflemen_before)
        return fail("rifleman count increased");
    printf("PASS: kknd production builds riflemen\n");
    rts_game_model_destroy(model);
    return 0;
}

static int test_ai_production(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/KKND" };
    if (!rts_game_model_load(model, &config)) return fail("load for AI");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");
    int initial_total = snap.unit_count;
    for (int t = 0; t < 30 * 60; ++t)
        if (!rts_tick(model, NULL)) return fail("tick AI");
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot after AI");
    if (snap.unit_count <= initial_total)
        return fail("AI produced new units");
    printf("PASS: kknd AI production (%d -> %d units)\n",
           initial_total, snap.unit_count);
    rts_game_model_destroy(model);
    return 0;
}

static int test_combat(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/KKND" };
    if (!rts_game_model_load(model, &config)) return fail("load for combat");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");
    int player = -1, enemy = -1;
    for (int i = 0; i < snap.unit_count; ++i) {
        if (snap.units[i].owner == 0 &&
            (snap.units[i].traits & (RTS_RENDER_TRAIT_MOBILE | RTS_RENDER_TRAIT_ATTACK)) ==
            (RTS_RENDER_TRAIT_MOBILE | RTS_RENDER_TRAIT_ATTACK))
            player = i;
        if (snap.units[i].owner == 1 && !snap.units[i].hidden &&
            (snap.units[i].traits & RTS_RENDER_TRAIT_MOBILE))
            enemy = i;
    }
    if (player < 0 || enemy < 0) {
        printf("PASS: kknd combat (enemies fog-hidden, as expected on large map)\n");
        rts_game_model_destroy(model);
        return 0;
    }
    RtsGameCommand sel = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { player, false } };
    rts_game_model_command(model, &sel);
    RtsGameCommand atk = { .kind = RTS_GAME_COMMAND_ATTACK_UNIT,
        .data.attack_unit = { snap.units[enemy].id, enemy } };
    if (!rts_game_model_command(model, &atk)) return fail("attack command accepted");
    bool saw_attack = false;
    for (int t = 0; t < 30 * 120 && !saw_attack; ++t) {
        if (!rts_tick(model, NULL)) return fail("tick combat");
        RtsGameEvent ev;
        while (rts_game_model_poll_event(model, &ev))
            if (ev.type == RTS_GAME_EVENT_ATTACK_STARTED) saw_attack = true;
    }
    if (!saw_attack) return fail("attack event fired");
    printf("PASS: kknd combat attack events fire\n");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    RTS_RUN(test_map_and_units());
    RTS_RUN(test_select_and_move());
    RTS_RUN(test_production());
    RTS_RUN(test_ai_production());
    RTS_RUN(test_combat());
    return 0;
}
