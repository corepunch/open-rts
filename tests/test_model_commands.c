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

static int build_event_seen(RtsGameModel *model, RtsGameEventType wanted,
                            uint32_t producer_id, int product_class,
                            int product_type) {
    RtsGameEvent event;
    while (rts_game_model_poll_event(model, &event)) {
        if (event.type == wanted && event.subject_id == producer_id &&
            event.product_class == product_class && event.product_type == product_type)
            return 1;
    }
    return 0;
}

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = strcmp(g_game_id, "dark-colony") == 0 ? "data/DCOLONY" : "data/REIGN/dark",
        .map_path = strcmp(g_game_id, "dark-colony") == 0 ?
            "SCENARIO/HUMAN/HUMAN02.MAP" : "scenario/MULTI/2NIC/2NIC.SCN",
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
        .data.move_selected = { .target = snapshot.units[player].position } };
    if (!rts_game_model_command(model, &move) || !rts_game_model_tick(model, 1.0f / 30.0f))
        return fail("move command and tick");
    (void)event_seen(model, RTS_GAME_EVENT_UNIT_ARRIVED);

    int build_cost_floor = strcmp(g_game_id, "dark-colony") == 0 ? 350 : 750;
    if (strcmp(g_game_id, "dark-colony") == 0) {
        int exploiter = -1;
        for (int tries = 0; exploiter < 0 && tries < 30 * 5; ++tries) {
            for (int i = 0; i < snapshot.unit_count; ++i)
                if (strstr(snapshot.units[i].sprite_name, "EXPL.SPR") != NULL) {
                    exploiter = i;
                    break;
                }
            if (exploiter < 0) {
                if (!rts_game_model_tick(model, 1.0f / 30.0f) ||
                    !rts_game_model_snapshot(model, &snapshot))
                    return fail("tick for scripted exploiter");
            }
        }
        if (exploiter >= 0) {
            RtsGameCommand select_exploiter = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
                .data.select_unit_index = { exploiter, false } };
            RtsGameCommand harvest = { .kind = RTS_GAME_COMMAND_HARVEST_SELECTED,
                .data.harvest_selected = { .target = { 69.5f, 48.5f } } };
            if (!rts_game_model_command(model, &select_exploiter) ||
                !rts_game_model_command(model, &harvest)) return fail("harvest command");
        }
    }
    for (int tries = 0; tries < 30 * 100 &&
         snapshot.player_resources[0][0] < build_cost_floor; ++tries) {
        if (!rts_game_model_tick(model, 1.0f / 30.0f) ||
            !rts_game_model_snapshot(model, &snapshot)) return fail("tick for build resources");
        RtsGameEvent discarded;
        while (rts_game_model_poll_event(model, &discarded)) {}
    }

    RtsProductDefinition products[64];
    int product_count = rts_game_model_products(model, products, 64);
    bool build_accepted = false;
    uint32_t producer_id = 0;
    int product_class = 0;
    int product_type = 0;
    for (int p = 0; p < product_count && !build_accepted; ++p) {
        if (!products[p].available) continue;
        if (strcmp(g_game_id, "dark-colony") == 0 && products[p].ui_id != 89) continue;
        for (int i = 0; i < snapshot.unit_count; ++i) {
            if (snapshot.units[i].owner != 0) continue;
            RtsGameCommand build = { .kind = RTS_GAME_COMMAND_BUILD_PRODUCT,
                .data.build_product = { snapshot.units[i].id, i, products[p].ui_id } };
            if (rts_game_model_command(model, &build)) {
                build_accepted = true;
                producer_id = snapshot.units[i].id;
                product_class = products[p].product_class;
                product_type = products[p].product_type;
                break;
            }
        }
    }
    if (!build_accepted) {
        fprintf(stderr, "resources=%d products=%d units=%d\n", snapshot.player_resources[0][0], product_count, snapshot.unit_count);
        for (int i = 0; i < snapshot.unit_count; ++i)
            fprintf(stderr, "unit %d id=%u owner=%u type=%u traits=%u\n", i, snapshot.units[i].id,
                    snapshot.units[i].owner, snapshot.units[i].type_id, snapshot.units[i].traits);
        return fail("accept an available build command");
    }
    if (!build_event_seen(model, RTS_GAME_EVENT_BUILD_QUEUED, producer_id,
                          product_class, product_type)) return fail("build-queued event");
    if (product_class == RTS_PRODUCT_UNIT || strcmp(g_game_id, "dark-reign") == 0) {
        if (!build_event_seen(model, RTS_GAME_EVENT_BUILD_STARTED, producer_id,
                              product_class, product_type)) return fail("build-started event");
    }

    if (enemy >= 0) {
        RtsGameCommand attack = { .kind = RTS_GAME_COMMAND_ATTACK_UNIT,
            .data.attack_unit = { snapshot.units[enemy].id, enemy } };
        if (!rts_game_model_command(model, &attack)) return fail("attack command");
    }

    RtsGameEvent event;
    bool completed = false;
    RtsGameEventType completed_type = product_class == RTS_PRODUCT_UNIT ?
        RTS_GAME_EVENT_UNIT_BUILT : RTS_GAME_EVENT_BUILDING_BUILT;
    for (int tries = 0; tries < 30 * 50 && !completed; ++tries) {
        if (!rts_game_model_tick(model, 1.0f / 30.0f)) return fail("tick production");
        while (rts_game_model_poll_event(model, &event)) {
            if (event.type == completed_type && event.target_id == producer_id &&
                event.product_class == product_class && event.product_type == product_type)
                completed = true;
        }
    }
    if (!completed) return fail("typed build completion event");
    rts_game_model_destroy(model);
    printf("PASS: %s command/event lifecycle model\n", g_game_id);
    return 0;
}
