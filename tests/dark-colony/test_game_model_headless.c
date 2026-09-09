#include "mobj_test.h"
#include "game.h"
#include "engine_config.h"
#include "../rts_model_test.h"
#include "../../games/dark-colony/dc_facing.h"
#include "../../games/dark-colony/info.h"
#include "../../games/dark-colony/dc_types.h"
#include "../../games/dark-reign/dr_types.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    return rts_fail("headless", message);
}

static int assert_dark_colony_direction_mapping(void) {
    if (dc_direction_to_angle(0) != ANG270 ||
        dc_direction_to_angle(4) != 0 ||
        dc_direction_to_angle(8) != ANG90 ||
        dc_direction_to_angle(12) != ANG180 ||
        dc_angle_to_direction(ANG270) != 0 ||
        dc_angle_to_direction(0) != 4 ||
        dc_angle_to_direction(ANG90) != 8 ||
        dc_angle_to_direction(ANG180) != 12 ||
        dc_fin_direction_to_angle(0) != ANG270 ||
        dc_fin_direction_to_angle(4) != ANG180 ||
        dc_fin_direction_to_angle(8) != ANG90 ||
        dc_fin_direction_to_angle(12) != 0) {
        return fail("Dark Colony direction codes use south-zero counterclockwise facings");
    }
    return 0;
}

static int assert_dark_colony_state_frames_are_direction_independent(void) {
    gameinfo = &game_info;
    mobj_t unit = { 0 };

    unit.core.angle = dc_direction_to_angle(1);
    if (!P_SetMobjState(&unit, S_TRSC_STND) || unit.core.frame != states[S_TRSC_STND].frame ||
        unit.core.render_flags != 0) {
        return fail("Trooper state preserves its logical frame independently of facing");
    }

    unit.core.angle = dc_direction_to_angle(15);
    if (!P_SetMobjState(&unit, S_EXPL_STND) || unit.core.frame != states[S_EXPL_STND].frame ||
        unit.core.render_flags != 0) {
        return fail("Exploiter state preserves its logical frame independently of facing");
    }
    return 0;
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

#ifdef RTS_GAME_DARK_REIGN
static bool snapshot_has_dark_reign_building(const RtsRenderSnapshot *snapshot,
                                             const char *underlay, const char *body,
                                             const char *top, int footprint_w,
                                             int footprint_h) {
    if (!snapshot || !underlay || !body || !top) return false;
    for (int i = 0; i < snapshot->decoration_count; ++i) {
        const RtsRenderDecoration *dec = &snapshot->decorations[i];
        if (strcmp(dec->sprite_name, underlay) != 0 ||
            strcmp(dec->sprite2_name, body) != 0 ||
            strcmp(dec->sprite3_name, top) != 0) continue;
        if (dec->frame_index == 1 && dec->frame2_index == 1 && dec->frame3_index == 1 &&
            dec->has_sprite_pivot &&
            ivec2_equal(dec->sprite_pivot, (ivec2_t){ 0, 0 }) &&
            dec->footprint.w == footprint_w && dec->footprint.h == footprint_h) return true;
    }
    return false;
}
#endif

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
    for (int i = 0; i < snapshot->unit_count; ++i) {
        const RtsRenderUnit *effect = &snapshot->units[i];
        if (effect->sprite_name[0] == '\0') continue;
        if (effect->render_remap != 0)
            return fail("effect render remap is exposed and neutral by default");
        if (effect->render_intensity != 16)
            return fail("effect render intensity is exposed and defaults to FIN neutral");
    }
    return 0;
}

static int assert_dark_colony_sprite_catalog(void) {
    for (int i = 0; i < NUMSPRITES; ++i)
        if (strchr(sprnames[i], '/') || strchr(sprnames[i], '.'))
            return fail("gameplay sprite registry contains only stems");
    if (strcmp(sprnames[SPR_GRAY], "GRAY"))
        return fail("gameplay sprite ID resolves its stem");
    if (strcmp(game_info.selection_marker.image, "INTRFACE/CLIENT.SPR"))
        return fail("selection marker uses a UI image path");
    return 0;
}

static int assert_dark_colony_exploiter_work_states(void) {
    enum { EXPECTED_WORK_STATE_COUNT = 2 };
    int deploy_count = S_EXPL_WORK1 - S_EXPL_DEPLOY1;
    if (deploy_count <= 0) {
        return fail("Dark Colony Exploiter deploy state count is positive");
    }
    int count = S_EXPL_DIE1 - S_EXPL_WORK1;
    if (count != EXPECTED_WORK_STATE_COUNT) {
        return fail("Dark Colony Exploiter mining work state count matches FIN cycle");
    }
    const state_t *lit = &states[S_EXPL_WORK1];
    const state_t *unlit = &states[S_EXPL_WORK2];
    if (lit->sprite != SPR_EXPL || lit->frame != 103 || lit->tics != 2 ||
        lit->nextstate != S_EXPL_WORK2) {
        return fail("Dark Colony Exploiter work uses the native deployed body frame");
    }
    if (unlit->sprite != SPR_EXPL || unlit->frame != 103 || unlit->tics != 4 ||
        unlit->nextstate != S_EXPL_WORK1) {
        return fail("Dark Colony Exploiter work loops through the native body frame");
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

static int snapshot_count_units_with_owner_and_type(const RtsRenderSnapshot *snapshot,
                                                    uint8_t owner, uint16_t type_id) {
    if (!snapshot) return 0;
    int count = 0;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        if (snapshot->units[i].owner == owner && snapshot->units[i].type_id == type_id)
            count++;
    }
    return count;
}

static bool snapshot_has_unit_at(const RtsRenderSnapshot *snapshot, const char *sprite_name,
                                 ivec2_t cell) {
    if (!snapshot || !sprite_name) return false;
    fvec2_t expected = fvec2_cell_center(cell);
    for (int i = 0; i < snapshot->unit_count; ++i) {
        const RtsRenderUnit *unit = &snapshot->units[i];
        if (strcmp(unit->sprite_name, sprite_name) == 0 &&
            fvec2_near(unit->position, expected, 0.05f)) {
            return true;
        }
    }
    return false;
}

static bool snapshot_has_owner_type_frame_at(const RtsRenderSnapshot *snapshot, uint8_t owner,
                                             uint16_t type_id, int frame, ivec2_t cell) {
    if (!snapshot) return false;
    fvec2_t expected = fvec2_cell_center(cell);
    for (int i = 0; i < snapshot->unit_count; ++i) {
        const RtsRenderUnit *unit = &snapshot->units[i];
        if (unit->owner == owner && unit->type_id == type_id && unit->frame == frame &&
            fvec2_near(unit->position, expected, 0.05f)) {
            return true;
        }
    }
    return false;
}

static bool snapshot_has_owner_type_pose(const RtsRenderSnapshot *snapshot,
                                         uint8_t owner, uint16_t type_id,
                                         int frame, int state_id,
                                         fvec2_t position,
                                         ivec2_t render_offset) {
    if (!snapshot) return false;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        const RtsRenderUnit *unit = &snapshot->units[i];
        if (unit->owner != owner || unit->type_id != type_id || unit->frame != frame ||
            unit->state_id != state_id || !fvec2_near(unit->position, position, 0.001f)) {
            continue;
        }
        if (ivec2_equal(unit->render_offset, render_offset)) {
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
        barracks->prerequisite_count != 1 || barracks->prerequisites[0] != 0 ||
        barracks->available) {
        return fail("Barracks requires DEPEND row 0 Exo Center");
    }

    const RtsProductDefinition *trooper = find_product(products, product_count, 89);
    if (!trooper) return fail("product table includes Trooper");
    if (trooper->cost != 350 || trooper->icon_frame != 6 ||
        trooper->product_class != RTS_PRODUCT_UNIT ||
        trooper->prerequisite_count != 1 || trooper->prerequisites[0] != 1 ||
        trooper->available) {
        return fail("Trooper requires DEPEND row 1 Barracks");
    }

    const RtsProductDefinition *reaper = find_product(products, product_count, 91);
    const RtsProductDefinition *barrager = find_product(products, product_count, 93);
    if (!reaper || !barrager) return fail("product table includes robot factory units");
    if (reaper->prerequisite_count != 2 || reaper->prerequisites[0] != 3 ||
        reaper->prerequisites[1] != 2 ||
        barrager->prerequisite_count != 2 || barrager->prerequisites[0] != 5 ||
        barrager->prerequisites[1] != 4) {
        return fail("Reaper and Barrager expose original DEPEND prerequisite rows");
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
    int initial_trooper_count = snapshot_count_units_with_owner_and_type(
        &snapshot, 0, MT_TROOPER);
    /* HUMAN01.SCN places 30 enemy Greys; HUMAN01.TRO delivers the player force. */
    if (initial_trooper_count != 0 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 1, MT_GREY) != 30)
        return fail("Human01 starts with 30 enemy Greys and awaits player reinforcements");
    if (snapshot.units[0].sprite_name[0] == '\0') {
        return fail("Human01 snapshot unit has render sprite reference");
    }
    for (int i = 0; i < snapshot.unit_count; ++i) {
        if (snapshot.units[i].id == 0) return fail("Human01 units have stable IDs");
        for (int j = 0; j < i; ++j)
            if (snapshot.units[i].id == snapshot.units[j].id)
                return fail("Human01 unit IDs are unique");
    }
    int metadata_result = assert_snapshot_render_command_metadata(&snapshot);
    if (metadata_result != 0) return metadata_result;
    if (snapshot_count_units_with_owner_and_type(&snapshot, 0, MT_BEACON) != 1)
        return fail("Human01 beacon is an ordinary FIN-animated mobj");

    bool saw_dropship = false;
    bool saw_delivery = false;
    for (int tick = 0; tick < 30 * 120 && !saw_delivery; ++tick) {
        if (!rts_tick(model, &snapshot)) return fail("tick Human01 Dropship reinforcement");
        if (snapshot_count_units_with_owner_and_type(&snapshot, 0, MT_DROPSHIP) > 0)
            saw_dropship = true;
        int troopers = snapshot_count_units_with_owner_and_type(
            &snapshot, 0, MT_TROOPER);
        if (troopers == initial_trooper_count + 5) saw_delivery = true;
    }
    if (!saw_dropship) return fail("Human01 reinforcement spawns a visible Dropship");
    if (!saw_delivery) return fail("Human01 opening reinforcement supplies four Troopers and the commander");

    int trooper = find_movable_player_unit(&snapshot);
    if (trooper < 0) return fail("find selectable Human01 Trooper");
    uint32_t trooper_id = snapshot.units[trooper].id;
    fvec2_t target = (fvec2_t){ 10.0f, 10.0f };
    RtsGameCommand select = {
        .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { .unit_index = trooper, .additive = false },
    };
    RtsGameCommand move = {
        .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = { .target = target },
    };
    if (!rts_game_model_command(model, &select) ||
        !rts_game_model_command(model, &move)) {
        return fail("Human01 selects Trooper and accepts move order");
    }
    if (!rts_game_model_snapshot(model, &snapshot))
        return fail("snapshot after Human01 move command");
    int selected_trooper = rts_find_unit_by_id(&snapshot, trooper_id);
    if (selected_trooper < 0 || !snapshot.units[selected_trooper].selected)
        return fail("Human01 selects Trooper");

    printf("PASS: Human01 starts with enemy Greys, delivers its player force, and accepts Trooper orders\n");
    return assert_dark_colony_products(model);
}

static int assert_human02(RtsGameModel *model) {
    RtsGameModelConfig config = {
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
    int grey_count = 0;
    for (int i = 0; i < snapshot.unit_count; ++i) {
        if (snapshot.units[i].type_id == MT_GREY) {
            if (snapshot.units[i].owner != 1 || snapshot.units[i].hidden ||
                snapshot.units[i].hp <= 0 || strcmp(snapshot.units[i].sprite_name, "GRAY"))
                return fail("Human02 Greys are active; negative SCN health selects defaults");
            grey_count++;
        }
    }
    if (grey_count != 10) {
        return fail("Human02 loads all ten starting enemy Greys");
    }
    if (snapshot_count_units_with_sprite(&snapshot, "DISH") != 3) {
        return fail("Human02 loads communication dish/base attachment objects");
    }
    if (snapshot_count_units_with_sprite(&snapshot, "HUBU") < 2 ||
        snapshot_count_units_with_sprite(&snapshot, "TOWR") < 1 ||
        snapshot_count_units_with_sprite(&snapshot, "ALIEN1") != 0) {
        return fail("Human02 loads active city slots from Dark Colony city data");
    }
    if (snapshot_count_units_with_owner_and_type(&snapshot, 0, MT_EXCOPOD) != 1 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 0, MT_BRRKPOD) != 1) {
        return fail("Human02 starting base buildings are Exo Center plus Barracks");
    }
    if (snapshot_count_units_with_owner_and_type(&snapshot, 0, MT_CITY_TOWER) != 1 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 1, MT_CITY_TOWER) != 0 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 1, MT_ALIEN_MINDHIVE) != 0) {
        return fail("Human02 only materializes cities with an explicit city anchor");
    }
    if (!snapshot_has_owner_type_pose(&snapshot, 0, MT_EXCOPOD, 19,
                                      S_EXCOPOD_STND, (fvec2_t){ 54.0f, 55.46875f },
                                      (ivec2_t){ 64, 32 + 15 }) ||
        !snapshot_has_owner_type_pose(&snapshot, 0, MT_BRRKPOD, 38,
                                      S_BRRKPOD_STND, (fvec2_t){ 56.0f, 55.0f },
                                      (ivec2_t){ 0, 32 }) ||
        !snapshot_has_owner_type_pose(&snapshot, 0, MT_CITY_TOWER, 1,
                                      S_TOWR_STND, (fvec2_t){ 56.0f, 56.0f },
                                      (ivec2_t){ 0, 32 + 32 })) {
        return fail("Human02 city buildings retain the native origin and render on the terrain row");
    }
    if (snapshot_has_owner_type_frame_at(&snapshot, 1, MT_EXCOPOD, 0,
                                         (ivec2_t){ 36, 26 }) ||
        snapshot_has_owner_type_frame_at(&snapshot, 1, MT_EXCOPOD, 0,
                                         (ivec2_t){ 50, 28 })) {
        return fail("Human02 non-player city slots stay non-materialized");
    }
    if (snapshot_count_units_with_owner_and_type(&snapshot, 0, MT_BEACON) != 1)
        return fail("Human02 beacon is an ordinary FIN-animated mobj");
        bool saw_dropship = false;
        for (int tick = 0; tick < 45 && !saw_dropship; ++tick) {
            if (!rts_tick(model, &snapshot)) return fail("tick Human02 Dropship reinforcement");
            for (int i = 0; i < snapshot.unit_count; ++i) {
                if (snapshot.units[i].type_id == MT_DROPSHIP) {
                    saw_dropship = true;
                    break;
                }
            }
        }
        if (!saw_dropship) return fail("Human02 reinforcement spawns a visible Dropship effect");
    RtsProductDefinition products[32];
    int product_count = rts_game_model_products(model, products, 32);
    const RtsProductDefinition *barracks = find_product(products, product_count, 80);
    const RtsProductDefinition *trooper = find_product(products, product_count, 89);
    const RtsProductDefinition *reaper = find_product(products, product_count, 91);
    if (!barracks || !trooper || !reaper) {
        return fail("Human02 exposes expected product availability entries");
    }
    if (!barracks->available || !trooper->available || reaper->available) {
        return fail("Human02 product availability follows DEPEND rows and starting buildings");
    }
    RtsGameCommand train_without_resources = {
        .kind = RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON,
        .data.activate_ui_button = { .ui_id = 89 },
    };
    if (rts_game_model_command(model, &train_without_resources)) {
        return fail("Human02 cannot train Trooper before enough Petra-7 is available");
    }
    if (!snapshot_has_unit_at(&snapshot, "DISH", (ivec2_t){ 33, 58 }) ||
        !snapshot_has_unit_at(&snapshot, "DISH", (ivec2_t){ 38, 58 }) ||
        !snapshot_has_unit_at(&snapshot, "DISH", (ivec2_t){ 35, 55 })) {
        return fail("Human02 satellite dish object rows use raw DC world Y coordinates");
    }
    if (snapshot.resource_vent_count <= 0) {
        return fail("Human02 loads Petra-7 vents");
    }
    for (int i = 0; i < snapshot.resource_vent_count; ++i) {
        const resourcevent_t *vent = &level.resource_vents[i];
        bool found = false;
        for (int j = 0; j < snapshot.unit_count; ++j) {
            const RtsRenderUnit *unit = &snapshot.units[j];
            if (unit->type_id != MT_VENT ||
                !fvec2_near(unit->position, vent->attachment, 0.001f)) continue;
            bool animated = unit->state_id >= S_VENT_ACTIVE1 && unit->state_id <= S_VENT_ACTIVE20;
            if (animated != vent->active)
                return fail("Human02 vent mobj state follows resource activity");
            found = true;
        }
        if (!found) return fail("Human02 resource vent has a persistent mobj");
    }

    printf("PASS: Human02 headless model loaded %dx%d with %d units and %d vents\n",
           snapshot.map_width, snapshot.map_height, snapshot.unit_count,
           snapshot.resource_vent_count);
    return 0;
}

static int assert_human03_city_slots(RtsGameModel *model) {
    RtsGameModelConfig config = {
        .data_root = "data/DCOLONY",
        .map_path = "SCENARIO/HUMAN/HUMAN03.MAP",
    };
    if (!rts_game_model_load(model, &config)) {
        fprintf(stderr, "model load error: %s\n", rts_game_model_last_error(model));
        return fail("load Dark Colony Human03");
    }

    RtsRenderSnapshot snapshot;
    if (!rts_game_model_snapshot(model, &snapshot)) {
        return fail("initial Human03 snapshot");
    }
    if (!snapshot_has_owner_type_pose(&snapshot, 0, MT_EXCOPOD, 19,
                                      S_EXCOPOD_STND, (fvec2_t){ 73.0f, 6.46875f },
                                      (ivec2_t){ 64, 32 + 15 }) ||
        !snapshot_has_owner_type_pose(&snapshot, 0, MT_BRRKPOD, 38,
                                      S_BRRKPOD_STND, (fvec2_t){ 75.0f, 6.0f },
                                      (ivec2_t){ 0, 32 }) ||
        !snapshot_has_owner_type_pose(&snapshot, 0, MT_SCNCPOD, 41,
                                      S_SCNCPOD_STND1, (fvec2_t){ 77.0f, 6.3125f },
                                      (ivec2_t){ -64, 32 + 10 }) ||
        !snapshot_has_owner_type_pose(&snapshot, 0, MT_CITY_TOWER, 1,
                                      S_TOWR_STND, (fvec2_t){ 75.0f, 7.0f },
                                      (ivec2_t){ 0, 32 + 32 })) {
        return fail("Human03 city slots retain the native origin and render on the terrain row");
    }
    if (!snapshot_has_owner_type_pose(&snapshot, 0, MT_EXCOPOD, 19,
                                      S_EXCOPOD_STND, (fvec2_t){ 73.0f, 6.46875f },
                                      (ivec2_t){ 64, 32 + 15 }) ||
        !snapshot_has_owner_type_pose(&snapshot, 0, MT_CITY_TOWER, 1,
                                      S_TOWR_STND, (fvec2_t){ 75.0f, 7.0f },
                                      (ivec2_t){ 0, 32 + 32 })) {
        return fail("Human03 DC city AISlot is the player city base");
    }
    if (snapshot_count_units_with_owner_and_type(&snapshot, 1, MT_ALIEN_MINDHIVE) != 1 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 1, MT_ALIEN_WARHIVE) != 1 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 1, MT_ALIEN_BRDRHIVE) != 1 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 1, MT_ALIEN_MINDHIVE2) != 0 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 1, MT_ALIEN_RSCHIVE) != 0 ||
        snapshot_count_units_with_sprite(&snapshot, "ALIEN1") != 0 ||
        snapshot_count_units_with_sprite(&snapshot, "ALBU") != 3) {
        return fail("Human03 alien city slots use native ALBU building art");
    }

    printf("PASS: Human03 city slots compose from Dark Colony fixed-point data with %d units\n",
           snapshot.unit_count);
    return 0;
}

#ifdef RTS_GAME_DARK_REIGN
static int assert_dark_reign_model_products(RtsGameModel *model) {
    RtsProductDefinition products[16];
    int product_count = rts_game_model_products(model, products, 16);
    if (product_count < 2) return fail("Dark Reign exposes product table");

    const RtsProductDefinition *hq = find_product(products, product_count, 10001);
    const RtsProductDefinition *rig = find_product(products, product_count, 11);
    if (!hq || !rig) return fail("Dark Reign exposes FG headquarters and construction rig products");
    if (strcmp(hq->label, "FG HQ 1") != 0 || hq->cost != 750 ||
        hq->product_class != RTS_PRODUCT_BUILDING || hq->product_type != 10001 ||
        hq->prerequisite_count != 0 || !hq->available) {
        return fail("Dark Reign FG HQ product follows BUILD.TXT SetType/SetCost/SetMaker");
    }
    if (strcmp(rig->label, "Construction Rig") != 0 || rig->cost != 300 ||
        rig->product_class != RTS_PRODUCT_UNIT || rig->product_type != 11 ||
        rig->prerequisite_count != 1 || rig->prerequisites[0] != 10001 ||
        rig->available) {
        return fail("Dark Reign Construction Rig product follows UNITS.TXT prerequisites");
    }
    return 0;
}

static int assert_dark_reign(RtsGameModel *model) {
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/MULTI/2NIC/2NIC.SCN",
    };
    if (!rts_game_model_load(model, &config)) {
        fprintf(stderr, "model load error: %s\n", rts_game_model_last_error(model));
        return fail("load Dark Reign 2NIC");
    }

    RtsRenderSnapshot snapshot;
    if (!rts_game_model_snapshot(model, &snapshot)) {
        return fail("initial Dark Reign snapshot");
    }
    if (snapshot.map_width <= 0 || snapshot.map_height <= 0 || snapshot.unit_count != 6) {
        return fail("Dark Reign snapshot has expected map and starting units");
    }
    if (snapshot.player_resources[0][0] != 12000 || snapshot.player_resources[1][0] != 12000) {
        return fail("Dark Reign team credits load into model resources");
    }
    if (snapshot_count_units_with_owner_and_type(&snapshot, 0, ACTOR_FG_CONSTRUCTION_CREW) != 3 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 1, ACTOR_FG_CONSTRUCTION_CREW) != 3) {
        return fail("Dark Reign starting construction crews preserve SCN team ownership");
    }
    if (!strstr(snapshot.ui_script, "ui dark-reign 1\n") ||
        !strstr(snapshot.ui_script, "btn 10001 enabled 1") ||
        !strstr(snapshot.ui_script, "btn 11 enabled 0")) {
        return fail("Dark Reign model UI script exposes product availability");
    }

    int product_result = assert_dark_reign_model_products(model);
    if (product_result != 0) return product_result;

    int units_before_hq = snapshot.unit_count;
    int resources_before_hq = snapshot.player_resources[0][0];
    RtsGameCommand build_hq = {
        .kind = RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON,
        .data.activate_ui_button = { .ui_id = 10001 },
    };
    if (!rts_game_model_command(model, &build_hq)) {
        return fail("Dark Reign builds FG HQ through model UI button command");
    }
    if (!rts_game_model_snapshot(model, &snapshot)) {
        return fail("Dark Reign snapshot after building HQ");
    }
    if (snapshot.unit_count != units_before_hq + 1 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 0, ACTOR_FG_HEADQUARTERS_1) != 1 ||
        snapshot.player_resources[0][0] != resources_before_hq - 750) {
        return fail("Dark Reign FG HQ production creates building and spends BUILD.TXT cost");
    }

    RtsProductDefinition products[16];
    int product_count = rts_game_model_products(model, products, 16);
    const RtsProductDefinition *rig = find_product(products, product_count, 11);
    if (!rig || !rig->available) {
        return fail("Dark Reign FG HQ unlocks Construction Rig production");
    }

    int rigs_before = snapshot_count_units_with_owner_and_type(
        &snapshot, 0, ACTOR_FG_CONSTRUCTION_CREW);
    int units_before_rig = snapshot.unit_count;
    int resources_before_rig = snapshot.player_resources[0][0];
    RtsGameCommand train_rig = {
        .kind = RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON,
        .data.activate_ui_button = { .ui_id = 11 },
    };
    if (!rts_game_model_command(model, &train_rig)) {
        return fail("Dark Reign trains Construction Rig through model UI button command");
    }
    if (!rts_game_model_snapshot(model, &snapshot)) {
        return fail("Dark Reign snapshot after training Construction Rig");
    }
    if (snapshot.unit_count != units_before_rig + 1 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 0, ACTOR_FG_CONSTRUCTION_CREW) != rigs_before + 1 ||
        snapshot.player_resources[0][0] != resources_before_rig - 300) {
        return fail("Dark Reign Construction Rig production creates unit and spends UNITS.TXT cost");
    }

    printf("PASS: Dark Reign headless model loaded %dx%d with %d units and %d decorations\n",
           snapshot.map_width, snapshot.map_height, snapshot.unit_count,
           snapshot.decoration_count);
    return 0;
}

static int assert_dark_reign_fixed_missions(RtsGameModel *model) {
    RtsGameModelConfig mission1 = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/FIXED/M01F/M01F.SCN",
    };
    if (!rts_game_model_load(model, &mission1))
        return fail("load Dark Reign Mission 1");

    RtsRenderSnapshot snapshot;
    if (!rts_game_model_snapshot(model, &snapshot))
        return fail("snapshot Dark Reign Mission 1");
    if (snapshot.map_width != 60 || snapshot.map_height != 60 ||
        snapshot.player_resources[0][0] != 4000 ||
        snapshot_count_units_with_owner_and_type(
            &snapshot, 0, ACTOR_FG_GROUND_TRANSPORTER) != 1 ||
        !snapshot_has_dark_reign_building(&snapshot,
            "tileset|nfhqt1l0.spr", "base|nfhqt1l0.spr", "base|tfhqt1l0.spr", 4, 4) ||
        !snapshot_has_dark_reign_building(&snapshot,
            "tileset|nclnc1l0.spr", "base|nclnc1l0.spr", "base|tclnc1l0.spr", 4, 3) ||
        !snapshot_has_dark_reign_building(&snapshot,
            "tileset|ncpow1l0.spr", "base|ncpow1l0.spr", "base|tcpow1l0.spr", 3, 4)) {
        return fail("Mission 1 loads its snow map, credits, three buildings, and freighter");
    }

    RtsGameModelConfig mission2 = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/FIXED/M02F/M02F.SCN",
    };
    if (!rts_game_model_load(model, &mission2))
        return fail("load Dark Reign Mission 2");
    if (!rts_game_model_snapshot(model, &snapshot))
        return fail("snapshot Dark Reign Mission 2");
    if (snapshot.map_width != 71 || snapshot.map_height != 71 ||
        snapshot.player_resources[0][0] != 12000 ||
        snapshot_count_units_with_owner_and_type(
            &snapshot, 0, ACTOR_FG_CONSTRUCTION_CREW) != 3) {
        return fail("Mission 2 loads its barren map, credits, and three construction crews");
    }

    int unit_index = find_movable_player_unit(&snapshot);
    if (unit_index < 0) return fail("Mission 2 has a movable player crew");
    RtsGameCommand select = {
        .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { .unit_index = unit_index, .additive = false },
    };
    RtsGameCommand move_east = {
        .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = {
            .target = fvec2_add(snapshot.units[unit_index].position,
                                (fvec2_t){ 4.0f, 0.0f }),
        },
    };
    if (!rts_game_model_command(model, &select) ||
        !rts_game_model_command(model, &move_east) ||
        !rts_game_model_tick(model, 1.0f / 30.0f) ||
        !rts_game_model_snapshot(model, &snapshot)) {
        return fail("move Mission 2 crew east");
    }
    if (snapshot.units[unit_index].facing_code != 4) {
        fprintf(stderr, "Mission 2 east-facing code was %d at %.2f,%.2f toward %.2f,%.2f\n",
                snapshot.units[unit_index].facing_code,
                snapshot.units[unit_index].position.x, snapshot.units[unit_index].position.y,
                snapshot.units[unit_index].move_goal.x, snapshot.units[unit_index].move_goal.y);
        return fail("Dark Reign eastward movement uses the east SPR facing");
    }

    printf("PASS: Dark Reign Missions 1 and 2 match their fixed scenario starts\n");
    return 0;
}
#endif

static int assert_fixed_momentum_semantics(void) {
    if (fixed_from_float(INFINITY) != INT32_MAX ||
        fixed_from_float(-INFINITY) != INT32_MIN ||
        fixed_from_float(NAN) != 0) {
        return fail("fixed conversion clamps non-finite values");
    }

    P_FreeLevel(&level);
    gameinfo = NULL;
    level = (level_t) { .width = 16, .height = 16 };
    mobj_t *unit = spawn_mobj_fixture((mobj_t){0});
    unit->core.position = fixed3_from_fvec2((fvec2_t){ 2.25f, 3.5f },
                                              fixed_from_float(7.0f));
    unit->speed = 3.0f;
    unit->hp = 1;
    unit->radius = 0.25f;
    unit->traits = MF_MOBILE;
    unit->attack.target = NULL;
    unit->harvest.target = -1;
    if (!P_MoveUnitTo(&level, unit, (fvec2_t){ 2.30f, 3.5f }))
        return fail("short final movement creates a flow-field order");

    fixed3_t before = unit->core.position;
    int unit_count = 1;
    P_Ticker();
    if (unit_count != 1 || unit->core.momentum.x == 0 || unit->core.momentum.y != 0 ||
        unit->core.momentum.z != 0 ||
        unit->core.position.x != fixed_add_saturated(before.x, unit->core.momentum.x) ||
        unit->core.position.y != fixed_add_saturated(before.y, unit->core.momentum.y) ||
        unit->core.position.z != before.z || unit->movement.flow_field != NULL) {
        return fail("short final movement applies position plus fixed momentum and preserves z");
    }

    P_Ticker();
    if (unit->core.momentum.x != 0 || unit->core.momentum.y != 0 ||
        unit->core.momentum.z != 0 || unit->core.position.z != before.z) {
        P_FreeFlowFields(&level);
        return fail("idle update clears fixed momentum and preserves z");
    }
    P_FreeFlowFields(&level);
    return 0;
}

int main(void) {
    RTS_RUN(assert_dark_colony_direction_mapping());
    RTS_RUN(assert_dark_colony_state_frames_are_direction_independent());
    RTS_RUN(assert_fixed_momentum_semantics());
    RTS_RUN(assert_dark_colony_sprite_catalog());
    RTS_RUN(assert_dark_colony_exploiter_work_states());

    RtsGameModel *model = rts_game_model_create();
    if (!model) return fail("create model");

    int result = assert_human01(model);
    /* Each check reloads its level; report later scenario failures too. */
    result |= assert_human02(model);
    result |= assert_human03_city_slots(model);
#ifdef RTS_GAME_DARK_REIGN
    if (result == 0) result = assert_dark_reign(model);
    if (result == 0) result = assert_dark_reign_fixed_missions(model);
#endif
    rts_game_model_destroy(model);
    return result;
}
