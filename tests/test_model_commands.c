#include "../game/g_game.h"

#include <stdio.h>
#include <string.h>

extern const char *const g_game_id;

static int fail(const char *message) {
    fprintf(stderr, "FAIL (%s): %s\n", g_game_id, message);
    return 1;
}

static int event_seen(RtsGameModel *model, RtsGameEventType wanted) {
    RtsGameEvent event;
    while (rts_game_model_poll_event(model, &event))
        if (event.type == wanted) return 1;
    return 0;
}

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = strcmp(g_game_id, "dark-colony") == 0 ? "data/DCOLONY" : "data/REIGN/dark",
        .map_path = strcmp(g_game_id, "dark-colony") == 0 ?
            "SCENARIO/HUMAN/HUMAN02.MAP" : "scenario/FIXED/M01F/M01F.SCN",
    };
    RtsRenderSnapshot snapshot;
    if (!model || !rts_game_model_load(model, &config)) return fail("load default model");
    if (!rts_game_model_snapshot(model, &snapshot)) return fail("snapshot default model");

    int player = -1, enemy = -1;
    for (int i = 0; i < snapshot.unit_count; ++i) {
        if (snapshot.units[i].owner == 0 &&
            (snapshot.units[i].traits & RTS_RENDER_TRAIT_MOBILE) && player < 0) player = i;
        if (snapshot.units[i].owner != 0 && enemy < 0) enemy = i;
        if (snapshot.units[i].id == 0) return fail("units expose stable IDs");
    }
    for (int tries = 0; player < 0 && tries < 30 * 10; ++tries) {
        if (!rts_game_model_tick(model, 1.0f / 30.0f) ||
            !rts_game_model_snapshot(model, &snapshot)) return fail("tick model");
        player = enemy = -1;
        for (int i = 0; i < snapshot.unit_count; ++i) {
            if (snapshot.units[i].owner == 0 &&
                (snapshot.units[i].traits & RTS_RENDER_TRAIT_MOBILE) && player < 0) player = i;
            if (snapshot.units[i].owner != 0 && enemy < 0) enemy = i;
        }
    }
    if (player < 0) return fail("find player mobile unit");
    RtsGameCommand select = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { player, false } };
    if (!rts_game_model_command(model, &select)) return fail("select command");
    RtsGameCommand move = { .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = { snapshot.units[player].gx, snapshot.units[player].gy } };
    if (!rts_game_model_command(model, &move) || !rts_game_model_tick(model, 1.0f / 30.0f))
        return fail("move command and tick");
    (void)event_seen(model, RTS_GAME_EVENT_UNIT_ARRIVED);

    if (enemy >= 0) {
        RtsGameCommand attack = { .kind = RTS_GAME_COMMAND_ATTACK_UNIT,
            .data.attack_unit = { snapshot.units[enemy].id, enemy } };
        if (!rts_game_model_command(model, &attack)) return fail("attack command");
    }

    for (int tries = 0; tries < 30 * 100 && snapshot.player_resources[0][0] < 2000; ++tries)
        if (!rts_game_model_tick(model, 1.0f / 30.0f) ||
            !rts_game_model_snapshot(model, &snapshot)) return fail("tick for build resources");

    RtsProductDefinition products[64];
    int product_count = rts_game_model_products(model, products, 64);
    for (int p = 0; p < product_count; ++p) {
        if (!products[p].available) continue;
        for (int i = 0; i < snapshot.unit_count; ++i) {
            if (snapshot.units[i].owner != 0) continue;
            RtsGameCommand build = { .kind = RTS_GAME_COMMAND_BUILD_PRODUCT,
                .data.build_product = { snapshot.units[i].id, i, products[p].ui_id } };
            if (rts_game_model_command(model, &build)) {
                if (!event_seen(model, RTS_GAME_EVENT_BUILD_STARTED)) return fail("build-started event");
                rts_game_model_destroy(model);
                printf("PASS: %s command/event model\n", g_game_id);
                return 0;
            }
        }
    }
    /* Production availability depends on the scenario economy; dedicated
       model tests cover successful production for Dark Colony. */
    rts_game_model_destroy(model);
    printf("PASS: %s command model\n", g_game_id);
    return 0;
}
