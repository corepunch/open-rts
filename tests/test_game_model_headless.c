#include "engine_config.h"
#include "../game/g_game.h"
#include "../games/dark-colony/info.h"
#include "../games/dark-colony/dc_types.h"
#include "../games/dark-reign/dr_types.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
            dec->has_sprite_pivot && dec->sprite_pivot_x == 0 &&
            dec->sprite_pivot_y == 0 && dec->footprint_w == footprint_w &&
            dec->footprint_h == footprint_h) return true;
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

static bool poll_event_type(RtsGameModel *model, RtsGameEventType wanted,
                            uint32_t subject_id, int product_type) {
    RtsGameEvent event;
    while (rts_game_model_poll_event(model, &event)) {
        if (event.type == wanted &&
            (subject_id == 0 || event.subject_id == subject_id) &&
            (product_type < 0 || event.product_type == product_type)) return true;
    }
    return false;
}

static int assert_dark_colony_sprite_catalog(void) {
    FILE *f = fopen("games/dark-colony/info.c", "rb");
    if (!f) return fail("open generated Dark Colony info.c");
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return fail("seek generated Dark Colony info.c");
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return fail("read generated Dark Colony info.c size");
    }
    char *text = malloc((size_t)size + 1);
    if (!text) {
        fclose(f);
        return fail("allocate generated Dark Colony info.c text");
    }
    if (size > 0 && fread(text, 1, (size_t)size, f) != (size_t)size) {
        free(text);
        fclose(f);
        return fail("read generated Dark Colony info.c");
    }
    fclose(f);
    text[size] = '\0';

    bool has_gameplay = strstr(text, "\"SPRITES/GRAY.SPR\"") != NULL;
    bool has_interface = strstr(text, "\"INTRFACE/FONT.SPR\"") != NULL;
    bool has_encyclopedia = strstr(text, "\"ENCYCLO/REAP.SPR\"") != NULL;
    bool has_cursor = strstr(text, "\"CURSOR/CURS.SPR\"") != NULL;
    free(text);

    if (!has_gameplay)
        return fail("Dark Colony sprite catalog includes gameplay sprites");
    if (!has_interface)
        return fail("Dark Colony sprite catalog includes interface sprites");
    if (!has_encyclopedia)
        return fail("Dark Colony sprite catalog includes encyclopedia sprites");
    if (!has_cursor)
        return fail("Dark Colony sprite catalog includes cursor sprites");
    return 0;
}

static int assert_dark_colony_exploiter_work_states(void) {
    enum { EXPECTED_WORK_STATE_COUNT = 15 };
    int deploy_count = S_DC_EXPL_WORK1 - S_DC_EXPL_DEPLOY1;
    if (deploy_count <= 0) {
        return fail("Dark Colony Exploiter deploy state count is positive");
    }
    int count = S_DC_EXPL_DIE1 - S_DC_EXPL_WORK1;
    if (count != EXPECTED_WORK_STATE_COUNT) {
        return fail("Dark Colony Exploiter mining work state count matches FIN cycle");
    }
    FILE *f = fopen("games/dark-colony/info.c", "rb");
    if (!f) return fail("open generated Dark Colony info.c for pulse states");
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return fail("seek generated Dark Colony info.c for pulse states");
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return fail("read generated Dark Colony info.c pulse size");
    }
    char *text = malloc((size_t)size + 1);
    if (!text) {
        fclose(f);
        return fail("allocate generated Dark Colony info.c pulse text");
    }
    if (size > 0 && fread(text, 1, (size_t)size, f) != (size_t)size) {
        free(text);
        fclose(f);
        return fail("read generated Dark Colony info.c pulse text");
    }
    fclose(f);
    text[size] = '\0';

    for (int i = 0; i < deploy_count; ++i) {
        char prefix[96];
        int body_frame = i >= deploy_count - 2 ? 34 : 14;
        if (i + 1 < deploy_count) {
            snprintf(prefix, sizeof(prefix),
                     "{ SPR_DC_EXPL, %d, 3, A_None, S_DC_EXPL_DEPLOY%d,",
                     body_frame, i + 2);
        } else {
            snprintf(prefix, sizeof(prefix),
                     "{ SPR_DC_EXPL, %d, 3, A_None, S_DC_EXPL_WORK1,",
                     body_frame);
        }
        char *line = strstr(text, prefix);
        if (!line) {
            free(text);
            return fail("Dark Colony Exploiter deploy uses deployed body frames");
        }
        char *overlay = strstr(line, "}, SPR_DC_EXPL, ");
        char *end = strchr(line, '\n');
        if (!overlay || (end && overlay > end)) {
            free(text);
            return fail("Dark Colony Exploiter deploy states add FIN layer-0 overlay");
        }
    }

    for (int i = 0; i < count; ++i) {
        int next = i + 2;
        if (next > count) next = 1;
        char prefix[96];
        snprintf(prefix, sizeof(prefix),
                 "{ SPR_DC_EXPL, 34, 5, A_None, S_DC_EXPL_WORK%d,", next);
        char *line = strstr(text, prefix);
        if (!line) {
            free(text);
            return fail("Dark Colony Exploiter mining work uses deployed body frames");
        }
        char *no_overlay = strstr(line, "DC_NO_OVERLAY");
        char *end = strchr(line, '\n');
        if (!no_overlay || (end && no_overlay > end)) {
            free(text);
            return fail("Dark Colony Exploiter mining work uses no probe-arm overlay");
        }
    }
    free(text);
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

static int snapshot_count_units_with_type(const RtsRenderSnapshot *snapshot, uint16_t type_id) {
    if (!snapshot) return 0;
    int count = 0;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        if (snapshot->units[i].type_id == type_id) count++;
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

static int snapshot_find_unit_with_owner_and_type(const RtsRenderSnapshot *snapshot,
                                                  uint8_t owner, uint16_t type_id) {
    if (!snapshot) return -1;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        if (snapshot->units[i].owner == owner && snapshot->units[i].type_id == type_id)
            return i;
    }
    return -1;
}

static bool near_cell_center(float value, int cell) {
    float expected = (float)cell + 0.5f;
    float delta = value - expected;
    return delta > -0.05f && delta < 0.05f;
}

static bool near_float(float value, float expected) {
    float delta = value - expected;
    return delta > -0.001f && delta < 0.001f;
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

static bool snapshot_has_owner_type_frame_at(const RtsRenderSnapshot *snapshot, uint8_t owner,
                                             uint16_t type_id, int frame, int gx, int gy) {
    if (!snapshot) return false;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        const RtsRenderUnit *unit = &snapshot->units[i];
        if (unit->owner == owner && unit->type_id == type_id && unit->frame == frame &&
            near_cell_center(unit->gx, gx) && near_cell_center(unit->gy, gy)) {
            return true;
        }
    }
    return false;
}

static bool snapshot_has_owner_type_pose(const RtsRenderSnapshot *snapshot,
                                         uint8_t owner, uint16_t type_id,
                                         int frame, int state_id,
                                         float gx, float gy,
                                         int render_offset_x,
                                         int render_offset_y) {
    if (!snapshot) return false;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        const RtsRenderUnit *unit = &snapshot->units[i];
        if (unit->owner != owner || unit->type_id != type_id || unit->frame != frame ||
            unit->state_id != state_id || !near_float(unit->gx, gx) ||
            !near_float(unit->gy, gy)) {
            continue;
        }
        if (unit->render_offset_x == render_offset_x &&
            unit->render_offset_y == render_offset_y) {
            return true;
        }
    }
    return false;
}

static bool snapshot_has_animated_decoration_at(const RtsRenderSnapshot *snapshot,
                                                const char *sprite_name, int gx, int gy,
                                                uint32_t required_flags,
                                                int pivot_x, int pivot_y) {
    if (!snapshot || !sprite_name) return false;
    for (int i = 0; i < snapshot->decoration_count; ++i) {
        const RtsRenderDecoration *dec = &snapshot->decorations[i];
        if (strcmp(dec->sprite_name, sprite_name) == 0 &&
            dec->gx == gx && dec->gy == gy && dec->frame_index < 0 &&
            dec->center_anchor && dec->has_sprite_pivot &&
            dec->sprite_pivot_x == pivot_x && dec->sprite_pivot_y == pivot_y &&
            (dec->render_flags & required_flags) == required_flags) {
            return true;
        }
    }
    return false;
}

static bool snapshot_has_blinking_decoration_at(const RtsRenderSnapshot *snapshot,
                                                const char *sprite_name,
                                                const char *sprite2_name,
                                                int gx, int gy,
                                                uint32_t required_render2_flags) {
    if (!snapshot || !sprite_name || !sprite2_name) return false;
    for (int i = 0; i < snapshot->decoration_count; ++i) {
        const RtsRenderDecoration *dec = &snapshot->decorations[i];
        if (strcmp(dec->sprite_name, sprite_name) == 0 &&
            strcmp(dec->sprite2_name, sprite2_name) == 0 &&
            dec->gx == gx && dec->gy == gy &&
            (dec->render2_flags & required_render2_flags) == required_render2_flags) {
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
    if (snapshot_count_units_with_sprite(&snapshot, "SPRITES/GRAY.SPR") != 24) {
        return fail("Human01 loads initially active Grey scenario objects");
    }
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
    if (snapshot_count_units_with_sprite(&snapshot, "SPRITES/HUBU.SPR") != 2 ||
        snapshot_count_units_with_sprite(&snapshot, "SPRITES/TOWR.SPR") != 1 ||
        snapshot_count_units_with_sprite(&snapshot, "SPRITES/ALIEN1.SPR") != 0) {
        return fail("Human02 loads active city slots from Dark Colony city data");
    }
    if (snapshot_count_units_with_owner_and_type(&snapshot, 0, MT_DC_EXCOPOD) != 1 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 0, MT_DC_BRRKPOD) != 1) {
        return fail("Human02 starting base buildings are Exo Center plus Barracks");
    }
    if (snapshot_count_units_with_owner_and_type(&snapshot, 0, MT_DC_CITY_TOWER) != 1 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 1, MT_DC_EXCOPOD) != 0 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 1, MT_DC_CITY_TOWER) != 0 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 1, MT_DC_ALIEN_MINDHIVE) != 0) {
        return fail("Human02 city slots with zero DC city anchors are not materialized");
    }
    if (!snapshot_has_owner_type_pose(&snapshot, 0, MT_DC_EXCOPOD, 0,
                                      S_DC_EXCOPOD_STND, 56.0f, 53.5f, 0, 0) ||
        !snapshot_has_owner_type_pose(&snapshot, 0, MT_DC_BRRKPOD, 4,
                                      S_DC_BRRKPOD_STND, 56.0f, 53.5f, 0, 0) ||
        !snapshot_has_owner_type_pose(&snapshot, 0, MT_DC_CITY_TOWER, 0,
                                      S_DC_TOWR_STND, 56.0f, 53.5f, 0, 0)) {
        return fail("Human02 city bases use Dark Colony city anchor and slot offsets");
    }
    if (snapshot_has_owner_type_frame_at(&snapshot, 1, MT_DC_EXCOPOD, 0, 36, 26) ||
        snapshot_has_owner_type_frame_at(&snapshot, 1, MT_DC_EXCOPOD, 0, 50, 28)) {
        return fail("Human02 non-player city slots stay non-materialized");
    }
    if (!snapshot_has_blinking_decoration_at(&snapshot,
                                             "SPRITES/BEAC.SPR", "SPRITES/BEAC.SPR",
                                             64, 52,
                                             RTS_FRAME_ADDITIVE | RTS_FRAME_BLINK)) {
        return fail("Human02 dropship beacon stays anchored beside the starting base");
    }
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
    if (!snapshot_has_unit_at(&snapshot, "SPRITES/DISH.SPR", 33, 58) ||
        !snapshot_has_unit_at(&snapshot, "SPRITES/DISH.SPR", 38, 58) ||
        !snapshot_has_unit_at(&snapshot, "SPRITES/DISH.SPR", 35, 55)) {
        return fail("Human02 satellite dish object rows use raw DC world Y coordinates");
    }
    if (snapshot.decoration_count <= 0) {
        return fail("Human02 loads map decorations");
    }
    if (snapshot.resource_vent_count <= 0) {
        return fail("Human02 loads Petra-7 vents");
    }
    if (!snapshot_has_animated_decoration_at(&snapshot, "SPRITES/VENT2.SPR", 69, 48,
                                             RTS_FRAME_ADDITIVE, 9, -25)) {
        return fail("Human02 active Petra-7 vent glow uses VENT.FIN placement");
    }
    if (!snapshot_has_animated_decoration_at(&snapshot, "SPRITES/VENT2.SPR", 53, 27,
                                             RTS_FRAME_ADDITIVE, 9, -25)) {
        return fail("Human02 Petra-7 vent attributes keep SCN coordinates and authored pivot");
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
    float exploiter_start_gx = snapshot.units[exploiter].gx;
    float exploiter_start_gy = snapshot.units[exploiter].gy;

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

    int initial_resources = snapshot.player_resources[0][0];
    RtsGameCommand harvest = {
        .kind = RTS_GAME_COMMAND_HARVEST_SELECTED,
        .data.harvest_selected = {
            .gx = 69.5f,
            .gy = 48.5f,
        },
    };
    if (!rts_game_model_command(model, &harvest)) {
        return fail("Human02 harvest command targets active Petra-7 vent");
    }
    if (!rts_game_model_snapshot(model, &snapshot)) {
        return fail("Human02 snapshot immediately after harvest command");
    }
    exploiter = find_unit_with_sprite(&snapshot, "SPRITES/EXPL.SPR");
    if (exploiter < 0 ||
        !near_float(snapshot.units[exploiter].gx, exploiter_start_gx) ||
        !near_float(snapshot.units[exploiter].gy, exploiter_start_gy)) {
        return fail("Human02 Exploiter starts driving instead of snapping to the vent");
    }

    bool saw_deploy_body_frame = false;
    bool saw_mining_body = false;
    bool saw_exploiter_move = false;
    bool saw_mining_work_states[16] = {0};
    int mining_work_state_count = 0;
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
            float move_dx = snapshot.units[exploiter].gx - exploiter_start_gx;
            float move_dy = snapshot.units[exploiter].gy - exploiter_start_gy;
            float move_dist = hypotf(move_dx, move_dy);
            if (!saw_exploiter_move && move_dist > 0.25f) {
                float facing_angle = snapshot.units[exploiter].facing_code *
                    0.19634954084936207f;
                float alignment = (move_dx * sinf(facing_angle) +
                                   -move_dy * cosf(facing_angle)) / move_dist;
                if (alignment < 0.8f) {
                    fprintf(stderr,
                            "Exploiter movement delta=%.3f,%.3f facing=%d alignment=%.3f\n",
                            move_dx, move_dy, snapshot.units[exploiter].facing_code,
                            alignment);
                    return fail("Human02 Exploiter faces along its movement direction");
                }
                saw_exploiter_move = true;
            }
            int frame = snapshot.units[exploiter].frame;
            if (frame >= 15 && frame <= 25) {
                return fail("Human02 Exploiter turret frames do not replace body frames");
            }
            if (frame == 14 || frame == 34) {
                saw_deploy_body_frame = true;
            }
            if (snapshot.player_resources[0][0] > initial_resources) {
                int work_state = snapshot.units[exploiter].state_id - S_DC_EXPL_WORK1;
                if (snapshot.units[exploiter].state_id >= S_DC_EXPL_WORK1 &&
                    snapshot.units[exploiter].state_id < S_DC_EXPL_DIE1 &&
                    work_state >= 0 && work_state < (int)(sizeof(saw_mining_work_states) / sizeof(saw_mining_work_states[0])) &&
                    !saw_mining_work_states[work_state]) {
                    saw_mining_work_states[work_state] = true;
                    mining_work_state_count++;
                }
                saw_mining_body = true;
            }
        }
        if (saw_deploy_body_frame && saw_mining_body && mining_work_state_count >= 4 &&
            snapshot.player_resources[0][0] > initial_resources) break;
    }
    if (!saw_deploy_body_frame) {
        exploiter = find_unit_with_sprite(&snapshot, "SPRITES/EXPL.SPR");
        if (exploiter >= 0) {
            fprintf(stderr,
                    "Exploiter start=%.2f,%.2f current=%.2f,%.2f goal=%.2f,%.2f facing=%d moving=%d\n",
                    exploiter_start_gx, exploiter_start_gy,
                    snapshot.units[exploiter].gx, snapshot.units[exploiter].gy,
                    snapshot.units[exploiter].move_goal_gx,
                    snapshot.units[exploiter].move_goal_gy,
                    snapshot.units[exploiter].facing_code,
                    snapshot.units[exploiter].has_move_order ? 1 : 0);
        }
        return fail("Human02 Exploiter keeps deployed body frame while harvesting");
    }
    if (!saw_exploiter_move) {
        return fail("Human02 Exploiter drives to the Petra-7 vent");
    }
    if (snapshot.player_resources[0][0] <= initial_resources) {
        return fail("Human02 Exploiter mining adds player resources");
    }
    if (!saw_mining_body) {
        return fail("Human02 Exploiter mining uses the deployed body with FIN-authored flags");
    }
    if (mining_work_state_count < 4) {
        return fail("Human02 Exploiter mining plays the deployed beacon work cycle");
    }
    exploiter = find_unit_with_sprite(&snapshot, "SPRITES/EXPL.SPR");
    if (exploiter < 0 || (int)floorf(snapshot.units[exploiter].gx) != 69 ||
        (int)floorf(snapshot.units[exploiter].gy) != 48) {
        return fail("Human02 Exploiter deploys when it occupies the Petra-7 tile");
    }
    if (near_float(snapshot.units[exploiter].gx, 69.5f) &&
        near_float(snapshot.units[exploiter].gy, 48.5f)) {
        return fail("Human02 Exploiter preserves its sub-cell approach position");
    }
    for (int i = 0; i < 30 * 20; ++i) {
        if (!rts_game_model_tick(model, 1.0f / 30.0f)) {
            return fail("tick Human02 while checking mining persistence");
        }
    }
    if (!rts_game_model_snapshot(model, &snapshot)) {
        return fail("Human02 snapshot after sustained mining");
    }
    exploiter = find_unit_with_sprite(&snapshot, "SPRITES/EXPL.SPR");
    if (exploiter < 0 || snapshot.units[exploiter].hp <= 0 ||
        snapshot.units[exploiter].harvest_target < 0) {
        return fail("Human02 scripted enemy waves do not interrupt early Exploiter mining");
    }
    for (int i = 0; snapshot.player_resources[0][0] < 350 && i < 30 * 90; ++i) {
        if (!rts_game_model_tick(model, 1.0f / 30.0f)) {
            return fail("tick Human02 while mining enough Petra-7 for production");
        }
        if (!rts_game_model_snapshot(model, &snapshot)) {
            return fail("Human02 snapshot while mining enough Petra-7 for production");
        }
    }
    if (snapshot.player_resources[0][0] < 350) {
        return fail("Human02 mining produces enough Petra-7 to test unit production");
    }

    int units_before_production = snapshot.unit_count;
    int troopers_before_production = snapshot_count_units_with_type(&snapshot, MT_DC_TROOPER);
    int resources_before_production = snapshot.player_resources[0][0];
    int barracks_index = snapshot_find_unit_with_owner_and_type(&snapshot, 0, MT_DC_BRRKPOD);
    if (barracks_index < 0) return fail("Human02 has a player Barracks for Trooper production");
    RtsGameCommand train_trooper = {
        .kind = RTS_GAME_COMMAND_BUILD_PRODUCT,
        .data.build_product = {
            .producer_id = snapshot.units[barracks_index].id,
            .producer_index = barracks_index,
            .ui_id = 89,
        },
    };
    if (!rts_game_model_command(model, &train_trooper)) {
        return fail("Human02 trains Trooper through model UI button command");
    }
    if (!rts_game_model_snapshot(model, &snapshot)) {
        return fail("Human02 snapshot after training Trooper");
    }
    if (!poll_event_type(model, RTS_GAME_EVENT_BUILD_QUEUED,
                         snapshot.units[barracks_index].id, 0))
        return fail("Human02 emits build-queued event for explicit build command");
    if (!poll_event_type(model, RTS_GAME_EVENT_BUILD_STARTED,
                         snapshot.units[barracks_index].id, 0))
        return fail("Human02 emits build-started event for explicit build command");
    float barracks_gx = snapshot.units[barracks_index].gx;
    float barracks_gy = snapshot.units[barracks_index].gy;
    if (snapshot.unit_count != units_before_production ||
        snapshot_count_units_with_type(&snapshot, MT_DC_TROOPER) != troopers_before_production) {
        return fail("Human02 Trooper production queues instead of spawning instantly");
    }
    if (snapshot.player_resources[0][0] != resources_before_production - 350) {
        return fail("Human02 queued Trooper production spends the DEPEND cost immediately");
    }
    for (int i = 0; i < 30 * 4; ++i) {
        if (!rts_game_model_tick(model, 1.0f / 30.0f)) {
            return fail("tick Human02 while training Trooper");
        }
    }
    if (!rts_game_model_snapshot(model, &snapshot)) {
        return fail("Human02 snapshot during queued Trooper release");
    }
    if (snapshot.unit_count != units_before_production ||
        snapshot_count_units_with_type(&snapshot, MT_DC_TROOPER) != troopers_before_production) {
        return fail("Human02 Trooper release animation delays the spawned unit");
    }
    if (!snapshot_has_effect(&snapshot, "SPRITES/HUBU.SPR")) {
        return fail("Human02 Trooper production plays HUBU.FIN TRSCBUILD0 hatch effect");
    }
    bool production_spawned = false;
    for (int i = 0; i < 30 * 5 && !production_spawned; ++i) {
        if (!rts_game_model_tick(model, 1.0f / 30.0f)) {
            return fail("tick Human02 while releasing Trooper");
        }
        if (!rts_game_model_snapshot(model, &snapshot)) {
            return fail("Human02 snapshot while releasing Trooper");
        }
        production_spawned =
            snapshot_count_units_with_type(&snapshot, MT_DC_TROOPER) ==
            troopers_before_production + 1;
    }
    if (!production_spawned || snapshot.unit_count != units_before_production + 1 ||
        snapshot_count_units_with_type(&snapshot, MT_DC_TROOPER) != troopers_before_production + 1) {
        return fail("Human02 queued Trooper production completes after model ticks");
    }
    bool saw_unit_built = false;
    bool saw_build_finished = false;
    RtsGameEvent completion_event;
    while (rts_game_model_poll_event(model, &completion_event)) {
        if (completion_event.type == RTS_GAME_EVENT_UNIT_BUILT) saw_unit_built = true;
        if (completion_event.type == RTS_GAME_EVENT_BUILD_FINISHED) saw_build_finished = true;
    }
    if (!saw_unit_built)
        return fail("Human02 emits typed unit-built event after Trooper release");
    if (!saw_build_finished)
        return fail("Human02 emits build-finished event after Trooper release");
    float expected_exit_gx = barracks_gx + 16.0f / (float)CELL_W;
    float expected_exit_gy = barracks_gy - 75.0f / (float)CELL_H;
    int produced_trooper = -1;
    for (int i = 0; i < snapshot.unit_count; ++i) {
        const RtsRenderUnit *unit = &snapshot.units[i];
        if (unit->owner != 0 || unit->type_id != MT_DC_TROOPER) continue;
        float dx = unit->gx - expected_exit_gx;
        float dy = unit->gy - expected_exit_gy;
        if (dx * dx + dy * dy < 0.01f) {
            produced_trooper = i;
            break;
        }
    }
    if (produced_trooper < 0) {
        return fail("Human02 completed Trooper spawns at FIN-authored Barracks release point");
    }
    if (!snapshot.units[produced_trooper].has_move_order ||
        snapshot.units[produced_trooper].move_goal_gy >= snapshot.units[produced_trooper].gy) {
        return fail("Human02 completed Trooper receives a doorway-clearing move order");
    }

    printf("PASS: Human02 headless model loaded %dx%d with %d units, %d decorations, %d vents\n",
           snapshot.map_width, snapshot.map_height, snapshot.unit_count,
           snapshot.decoration_count, snapshot.resource_vent_count);
    return 0;
}

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
    if (!snapshot_has_owner_type_pose(&snapshot, 0, MT_DC_EXCOPOD, 0,
                                      S_DC_EXCOPOD_STND, 75.0f, 9.5f, 0, 0) ||
        !snapshot_has_owner_type_pose(&snapshot, 0, MT_DC_BRRKPOD, 4,
                                      S_DC_BRRKPOD_STND, 75.0f, 9.5f, 0, 0) ||
        !snapshot_has_owner_type_pose(&snapshot, 0, MT_DC_SCNCPOD, 2,
                                      S_NULL, 75.0f, 9.5f, 0, 0) ||
        !snapshot_has_owner_type_pose(&snapshot, 0, MT_DC_CITY_TOWER, 0,
                                      S_DC_TOWR_STND, 75.0f, 9.5f, 0, 0)) {
        return fail("Human03 city slots compose with Dark Colony fixed-point offsets");
    }
    if (!snapshot_has_owner_type_pose(&snapshot, 0, MT_DC_EXCOPOD, 0,
                                      S_DC_EXCOPOD_STND, 75.0f, 9.5f, 0, 0) ||
        !snapshot_has_owner_type_pose(&snapshot, 0, MT_DC_CITY_TOWER, 0,
                                      S_DC_TOWR_STND, 75.0f, 9.5f, 0, 0)) {
        return fail("Human03 DC city AISlot is the player city base");
    }

    printf("PASS: Human03 city slots compose from Dark Colony fixed-point data with %d units\n",
           snapshot.unit_count);
    return 0;
}

#if 0 /* Dark Reign uses the dedicated dual-plugin command harness below. */
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
    if (snapshot_count_units_with_owner_and_type(&snapshot, 0, DR_ACTOR_FG_CONSTRUCTION_CREW) != 3 ||
        snapshot_count_units_with_owner_and_type(&snapshot, 1, DR_ACTOR_FG_CONSTRUCTION_CREW) != 3) {
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
        snapshot_count_units_with_owner_and_type(&snapshot, 0, DR_ACTOR_FG_HEADQUARTERS_1) != 1 ||
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
        &snapshot, 0, DR_ACTOR_FG_CONSTRUCTION_CREW);
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
        snapshot_count_units_with_owner_and_type(&snapshot, 0, DR_ACTOR_FG_CONSTRUCTION_CREW) != rigs_before + 1 ||
        snapshot.player_resources[0][0] != resources_before_rig - 300) {
        return fail("Dark Reign Construction Rig production creates unit and spends UNITS.TXT cost");
    }

    printf("PASS: Dark Reign headless model loaded %dx%d with %d units and %d decorations\n",
           snapshot.map_width, snapshot.map_height, snapshot.unit_count,
           snapshot.decoration_count);
    return 0;
}
#endif

#if 0 /* Dark Reign uses the dedicated dual-plugin command harness below. */
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
            &snapshot, 0, DR_ACTOR_FG_GROUND_TRANSPORTER) != 1 ||
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
            &snapshot, 0, DR_ACTOR_FG_CONSTRUCTION_CREW) != 3) {
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
            .gx = snapshot.units[unit_index].gx + 4.0f,
            .gy = snapshot.units[unit_index].gy,
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
                snapshot.units[unit_index].gx, snapshot.units[unit_index].gy,
                snapshot.units[unit_index].move_goal_gx, snapshot.units[unit_index].move_goal_gy);
        return fail("Dark Reign eastward movement uses the east SPR facing");
    }

    printf("PASS: Dark Reign Missions 1 and 2 match their fixed scenario starts\n");
    return 0;
}
#endif

int main(void) {
    int result = assert_dark_colony_sprite_catalog();
    if (result != 0) return result;
    result = assert_dark_colony_exploiter_work_states();
    if (result != 0) return result;

    RtsGameModel *model = rts_game_model_create();
    if (!model) return fail("create model");

    result = assert_human01(model);
    if (result == 0) result = assert_human02(model);
    if (result == 0) result = assert_human03_city_slots(model);
    rts_game_model_destroy(model);
    return result;
}
