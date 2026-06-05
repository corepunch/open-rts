#include "engine_config.h"
#include "game_model.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int find_movable_player_unit(const RtsRenderSnapshot *snapshot) {
    if (!snapshot) return -1;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        if (snapshot->units[i].owner == 0 &&
            (snapshot->units[i].traits & RTS_RENDER_TRAIT_SELECTABLE) != 0 &&
            (snapshot->units[i].traits & RTS_RENDER_TRAIT_MOBILE) != 0) {
            return i;
        }
    }
    return -1;
}

static int find_unit_with_sprite(const RtsRenderSnapshot *snapshot, const char *sprite_name) {
    if (!snapshot || !sprite_name) return -1;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        if (strcmp(snapshot->units[i].sprite_name, sprite_name) == 0) return i;
    }
    return -1;
}

static bool snapshot_has_effect(const RtsRenderSnapshot *snapshot, const char *sprite_name) {
    if (!snapshot || !sprite_name) return false;
    for (int i = 0; i < snapshot->effect_count; ++i) {
        if (strcmp(snapshot->effects[i].sprite_name, sprite_name) == 0) return true;
    }
    return false;
}

static bool snapshot_has_blinking_beacon_decoration(const RtsRenderSnapshot *snapshot) {
    if (!snapshot) return false;
    for (int i = 0; i < snapshot->decoration_count; ++i) {
        const RtsRenderDecoration *dec = &snapshot->decorations[i];
        if (strcmp(dec->sprite_name, "SPRITES/BEAC.SPR") != 0) continue;
        if (strcmp(dec->sprite2_name, "SPRITES/BEAC.SPR") != 0) continue;
        if (dec->frame_index == 0 && dec->frame2_index == 1 &&
            (dec->render2_flags & RTS_FRAME_BLINK) != 0) {
            return true;
        }
    }
    return false;
}

static int assert_snapshot_render_command_metadata(const RtsRenderSnapshot *snapshot) {
    if (!snapshot || snapshot->unit_count <= 0)
        return fail("snapshot has units with render command metadata");
    for (int i = 0; i < snapshot->unit_count; ++i) {
        const RtsRenderUnit *unit = &snapshot->units[i];
        if (unit->sprite_name[0] == '\0') continue;
        if (unit->render_remap != 0)
            return fail("unit render remap is exposed and neutral by default");
        if (unit->render_intensity != 16)
            return fail("unit render intensity is exposed and defaults to FIN neutral");
    }
    for (int i = 0; i < snapshot->effect_count; ++i) {
        const RtsRenderEffect *effect = &snapshot->effects[i];
        if (!effect->active || effect->sprite_name[0] == '\0') continue;
        if (effect->render_remap != 0)
            return fail("effect render remap is exposed and neutral by default");
        if (effect->render_intensity != 16)
            return fail("effect render intensity is exposed and defaults to FIN neutral");
    }
    return 0;
}

static int snapshot_count_units_with_sprite(const RtsRenderSnapshot *snapshot,
                                            const char *sprite_name) {
    if (!snapshot || !sprite_name) return 0;
    int count = 0;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        if (strcmp(snapshot->units[i].sprite_name, sprite_name) == 0) count++;
    }
    return count;
}

static bool near_cell_center(float value, int cell) {
    float expected = (float)cell + 0.5f;
    float delta = value - expected;
    return delta > -0.05f && delta < 0.05f;
}

static bool snapshot_has_unit_at(const RtsRenderSnapshot *snapshot, const char *sprite_name,
                                 int gx, int gy) {
    if (!snapshot || !sprite_name) return false;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        const RtsRenderUnit *unit = &snapshot->units[i];
        if (strcmp(unit->sprite_name, sprite_name) == 0 &&
            near_cell_center(unit->gx, gx) && near_cell_center(unit->gy, gy)) {
            return true;
        }
    }
    return false;
}

static bool snapshot_has_decoration_at(const RtsRenderSnapshot *snapshot,
                                       const char *sprite_name, int gx, int gy) {
    if (!snapshot || !sprite_name) return false;
    for (int i = 0; i < snapshot->decoration_count; ++i) {
        const RtsRenderDecoration *dec = &snapshot->decorations[i];
        if (strcmp(dec->sprite_name, sprite_name) == 0 &&
            dec->gx == gx && dec->gy == gy) {
            return true;
        }
    }
    return false;
}

static bool snapshot_has_animated_decoration_at(const RtsRenderSnapshot *snapshot,
                                                const char *sprite_name, int gx, int gy,
                                                uint32_t required_flags) {
    if (!snapshot || !sprite_name) return false;
    for (int i = 0; i < snapshot->decoration_count; ++i) {
        const RtsRenderDecoration *dec = &snapshot->decorations[i];
        if (strcmp(dec->sprite_name, sprite_name) == 0 &&
            dec->gx == gx && dec->gy == gy && dec->frame_index < 0 &&
            (dec->render_flags & required_flags) == required_flags) {
            return true;
        }
    }
    return false;
}

static const RtsProductDefinition *find_product(const RtsProductDefinition *products,
                                                int product_count, int ui_id) {
    for (int i = 0; i < product_count; ++i) {
        if (products[i].ui_id == ui_id) return &products[i];
    }
    return NULL;
}

static int assert_dark_colony_products(RtsGameModel *model) {
    RtsProductDefinition products[32];
    int product_count = rts_game_model_products(model, products, 32);
    if (product_count < 16) return fail("Dark Colony exposes human product table");

    const RtsProductDefinition *exo = find_product(products, product_count, 206);
    if (!exo) return fail("product table includes Exo Center");
    if (strcmp(exo->label, "Exo-Ctr") != 0 || exo->cost != 2000 ||
        exo->icon_frame != 129 || exo->product_class != RTS_PRODUCT_BUILDING ||
        exo->product_type != 16 || !exo->available) {
        return fail("Exo Center metadata and availability match original data");
    }

    const RtsProductDefinition *barracks = find_product(products, product_count, 80);
    if (!barracks) return fail("product table includes Barracks");
    if (barracks->cost != 1000 || barracks->icon_frame != 20 ||
        barracks->prerequisite_count != 1 || barracks->prerequisites[0] != 16 ||
        barracks->available) {
        return fail("Barracks requires Exo Center");
    }

    const RtsProductDefinition *trooper = find_product(products, product_count, 89);
    if (!trooper) return fail("product table includes Trooper");
    if (trooper->cost != 350 || trooper->icon_frame != 6 ||
        trooper->product_class != RTS_PRODUCT_UNIT ||
        trooper->prerequisite_count != 1 || trooper->prerequisites[0] != 17 ||
        trooper->available) {
        return fail("Trooper requires Barracks");
    }

    const RtsProductDefinition *reaper = find_product(products, product_count, 91);
    const RtsProductDefinition *barrager = find_product(products, product_count, 93);
    if (!reaper || !barrager) return fail("product table includes robot factory units");
    if (reaper->prerequisite_count != 1 || reaper->prerequisites[0] != 18 ||
        barrager->prerequisite_count != 1 || barrager->prerequisites[0] != 18) {
        return fail("Reaper and Barrager require Robot Factory");
    }

    return 0;
}

static int assert_dark_colony_ui_script(const RtsRenderSnapshot *snapshot) {
    if (!snapshot || snapshot->ui_script[0] == '\0')
        return fail("snapshot includes model-generated UI script");
    if (!strstr(snapshot->ui_script, "ui dark-colony 1\n"))
        return fail("UI script has version header");
    if (!strstr(snapshot->ui_script, "x 516 y 92 btn 206 enabled 1 pic 129\n"))
        return fail("UI script includes enabled Exo Center button callback");
    if (!strstr(snapshot->ui_script, "x 524 y 126 text \"Exo-Ctr 2000\"\n"))
        return fail("UI script includes Exo Center text");
    if (!strstr(snapshot->ui_script, "x 552 y 92 btn 80 enabled 0 pic 20\n"))
        return fail("UI script includes disabled Barracks button callback");
    if (!strstr(snapshot->ui_script, "x 560 y 126 text \"Barracks 1000\"\n"))
        return fail("UI script includes Barracks text");
    return 0;
}

static int assert_human01(RtsGameModel *model) {
    RtsGameModelConfig config = {
        .game_id = "dark-colony",
        .data_root = "data/DCOLONY",
        .map_path = "SCENARIO/HUMAN/HUMAN01.MAP",
    };
    if (!rts_game_model_load(model, &config)) {
        fprintf(stderr, "model load error: %s\n", rts_game_model_last_error(model));
        return fail("load Dark Colony Human01");
    }

    RtsRenderSnapshot snapshot;
    if (!rts_game_model_snapshot(model, &snapshot)) {
        return fail("initial Human01 snapshot");
    }
    int ui_result = assert_dark_colony_ui_script(&snapshot);
    if (ui_result != 0) return ui_result;
    if (snapshot.map_width <= 0 || snapshot.map_height <= 0) {
        return fail("Human01 snapshot has map dimensions");
    }
    if (snapshot.unit_count <= 0) {
        return fail("Human01 snapshot has initial units");
    }
    if (snapshot.units[0].sprite_name[0] == '\0') {
        return fail("Human01 snapshot unit has render sprite reference");
    }
    int metadata_result = assert_snapshot_render_command_metadata(&snapshot);
    if (metadata_result != 0) return metadata_result;
    if (!snapshot_has_blinking_beacon_decoration(&snapshot)) {
        return fail("Human01 beacon is rendered as stable base plus blinking sprite2 glow");
    }

    int initial_unit_count = snapshot.unit_count;
    int movable_player_unit = find_movable_player_unit(&snapshot);
    for (int i = 0; movable_player_unit < 0 && i < 30 * 20; ++i) {
        if (!rts_game_model_tick(model, 1.0f / 30.0f)) {
            return fail("tick Human01 while waiting for player units");
        }
        if (!rts_game_model_snapshot(model, &snapshot)) {
            return fail("Human01 snapshot while waiting for player units");
        }
        movable_player_unit = find_movable_player_unit(&snapshot);
    }
    if (movable_player_unit < 0) {
        return fail("Human01 snapshot has a player-owned mobile selectable unit");
    }
    if (snapshot.unit_count <= initial_unit_count) {
        return fail("Human01 scripted reinforcements add units");
    }
    if (!snapshot_has_effect(&snapshot, "SPRITES/DROP.SPR")) {
        return fail("Human01 scripted dropship effect is visible");
    }
    if (snapshot_has_effect(&snapshot, "SPRITES/BEAC.SPR")) {
        return fail("Human01 beacon glow is not spawned as a frame-flipping visual effect");
    }

    RtsGameCommand select_first = {
        .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = {
            .unit_index = movable_player_unit,
            .additive = false,
        },
    };
    if (!rts_game_model_command(model, &select_first)) {
        return fail("Human01 select command");
    }
    if (!rts_game_model_snapshot(model, &snapshot)) {
        return fail("Human01 snapshot after selection");
    }
    int selected = 0;
    int first_selected = -1;
    for (int i = 0; i < snapshot.unit_count; ++i) {
        if (!snapshot.units[i].selected) continue;
        selected++;
        if (first_selected < 0) first_selected = i;
    }
    if (selected != 1 || first_selected != movable_player_unit) {
        return fail("Human01 select command updates snapshot");
    }

    RtsGameCommand move = {
        .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = {
            .gx = snapshot.units[first_selected].gx,
            .gy = snapshot.units[first_selected].gy,
        },
    };
    if (!rts_game_model_command(model, &move)) {
        return fail("Human01 move command");
    }
    if (!rts_game_model_tick(model, 1.0f / 30.0f)) {
        return fail("tick Human01 after move command");
    }
    if (!rts_game_model_snapshot(model, &snapshot)) {
        return fail("Human01 snapshot after move");
    }

    if (!snapshot.units[movable_player_unit].selected ||
        !snapshot.units[movable_player_unit].has_move_order) {
        return fail("Human01 move command is visible through render snapshot after tick");
    }

    printf("PASS: Human01 headless model loaded %dx%d with %d units and %d effects\n",
           snapshot.map_width, snapshot.map_height, snapshot.unit_count,
           snapshot.effect_count);
    return assert_dark_colony_products(model);
}

static int assert_human02(RtsGameModel *model) {
    RtsGameModelConfig config = {
        .game_id = "dark-colony",
        .data_root = "data/DCOLONY",
        .map_path = "SCENARIO/HUMAN/HUMAN02.MAP",
    };
    if (!rts_game_model_load(model, &config)) {
        fprintf(stderr, "model load error: %s\n", rts_game_model_last_error(model));
        return fail("load Dark Colony Human02");
    }

    RtsRenderSnapshot snapshot;
    if (!rts_game_model_snapshot(model, &snapshot)) {
        return fail("initial Human02 snapshot");
    }
    if (snapshot.map_width <= 0 || snapshot.map_height <= 0) {
        return fail("Human02 snapshot has map dimensions");
    }
    if (snapshot.unit_count <= 0) {
        return fail("Human02 loads expected starting units");
    }
    int metadata_result = assert_snapshot_render_command_metadata(&snapshot);
    if (metadata_result != 0) return metadata_result;
    if (snapshot_count_units_with_sprite(&snapshot, "SPRITES/GRAY.SPR") != 0) {
        return fail("Human02 hidden Grey placeholders are not loaded as starting units");
    }
    if (snapshot_count_units_with_sprite(&snapshot, "SPRITES/DISH.SPR") != 3) {
        return fail("Human02 loads communication dish/base attachment objects");
    }
    if (snapshot_count_units_with_sprite(&snapshot, "SPRITES/BUILDNG.SPR") != 2) {
        return fail("Human02 loads two starting base buildings from team city slots");
    }
    if (!snapshot_has_unit_at(&snapshot, "SPRITES/BUILDNG.SPR", 61, 53) ||
        !snapshot_has_unit_at(&snapshot, "SPRITES/BUILDNG.SPR", 56, 55)) {
        return fail("Human02 starting base buildings use team AISlot coordinates");
    }
    if (!snapshot_has_unit_at(&snapshot, "SPRITES/DISH.SPR", 33, 25) ||
        !snapshot_has_unit_at(&snapshot, "SPRITES/DISH.SPR", 38, 25) ||
        !snapshot_has_unit_at(&snapshot, "SPRITES/DISH.SPR", 35, 28)) {
        return fail("Human02 satellite dish object rows use flipped SCN Y coordinates");
    }
    if (snapshot.decoration_count <= 0) {
        return fail("Human02 loads map decorations");
    }
    if (snapshot.resource_vent_count <= 0) {
        return fail("Human02 loads Petra-7 vents");
    }
    if (!snapshot_has_animated_decoration_at(&snapshot, "SPRITES/VENT2.SPR", 69, 35,
                                             RTS_FRAME_ADDITIVE)) {
        return fail("Human02 active Petra-7 vent glow animates at flipped map-object coordinates");
    }
    if (snapshot_has_decoration_at(&snapshot, "SPRITES/VENT.SPR", 53, 27) ||
        snapshot_has_decoration_at(&snapshot, "SPRITES/VENT2.SPR", 53, 27)) {
        return fail("Human02 Petra-7 vent attributes are not drawn at direct SCN Y");
    }

    int exploiter = find_unit_with_sprite(&snapshot, "SPRITES/EXPL.SPR");
    for (int i = 0; exploiter < 0 && i < 30 * 5; ++i) {
        if (!rts_game_model_tick(model, 1.0f / 30.0f)) {
            return fail("tick Human02 while waiting for exploiter");
        }
        if (!rts_game_model_snapshot(model, &snapshot)) {
            return fail("Human02 snapshot while waiting for exploiter");
        }
        exploiter = find_unit_with_sprite(&snapshot, "SPRITES/EXPL.SPR");
    }
    if (exploiter < 0) {
        return fail("Human02 scripted drop spawns an Exploiter");
    }

    RtsGameCommand select_exploiter = {
        .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = {
            .unit_index = exploiter,
            .additive = false,
        },
    };
    if (!rts_game_model_command(model, &select_exploiter)) {
        return fail("Human02 select exploiter");
    }

    int initial_resources = snapshot.player_resources[0];
    RtsGameCommand harvest = {
        .kind = RTS_GAME_COMMAND_HARVEST_SELECTED,
        .data.harvest_selected = {
            .gx = 69.5f,
            .gy = 35.5f,
        },
    };
    if (!rts_game_model_command(model, &harvest)) {
        return fail("Human02 harvest command targets active Petra-7 vent");
    }

    bool saw_deploy_body_frame = false;
    for (int i = 0; i < 30 * 45; ++i) {
        if (!rts_game_model_tick(model, 1.0f / 30.0f)) {
            return fail("tick Human02 while mining");
        }
        if (!rts_game_model_snapshot(model, &snapshot)) {
            return fail("Human02 snapshot while mining");
        }
        exploiter = find_unit_with_sprite(&snapshot, "SPRITES/EXPL.SPR");
        if (exploiter >= 0 &&
            snapshot.units[exploiter].harvest_target >= 0) {
            int frame = snapshot.units[exploiter].frame;
            if (frame >= 15 && frame <= 25) {
                return fail("Human02 Exploiter turret frames do not replace body frames");
            }
            if (frame == 14 || frame == 34) {
                saw_deploy_body_frame = true;
            }
        }
        if (saw_deploy_body_frame && snapshot.player_resources[0] > initial_resources) break;
    }
    if (!saw_deploy_body_frame) {
        return fail("Human02 Exploiter keeps body frame while deploy/harvest overlay animates");
    }
    if (snapshot.player_resources[0] <= initial_resources) {
        return fail("Human02 Exploiter mining adds player resources");
    }
    exploiter = find_unit_with_sprite(&snapshot, "SPRITES/EXPL.SPR");
    if (exploiter < 0 || !near_cell_center(snapshot.units[exploiter].gx, 69) ||
        !near_cell_center(snapshot.units[exploiter].gy, 35)) {
        return fail("Human02 Exploiter deploys at the target Petra-7 vent");
    }

    printf("PASS: Human02 headless model loaded %dx%d with %d units, %d decorations, %d vents\n",
           snapshot.map_width, snapshot.map_height, snapshot.unit_count,
           snapshot.decoration_count, snapshot.resource_vent_count);
    return 0;
}

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    if (!model) return fail("create model");

    int result = assert_human01(model);
    if (result == 0) result = assert_human02(model);
    rts_game_model_destroy(model);
    return result;
}
