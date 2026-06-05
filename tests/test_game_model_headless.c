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

static bool snapshot_has_effect(const RtsRenderSnapshot *snapshot, const char *sprite_name) {
    if (!snapshot || !sprite_name) return false;
    for (int i = 0; i < snapshot->effect_count; ++i) {
        if (strcmp(snapshot->effects[i].sprite_name, sprite_name) == 0) return true;
    }
    return false;
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
    if (snapshot.map_width <= 0 || snapshot.map_height <= 0) {
        return fail("Human01 snapshot has map dimensions");
    }
    if (snapshot.unit_count <= 0) {
        return fail("Human01 snapshot has initial units");
    }
    if (snapshot.units[0].sprite_name[0] == '\0') {
        return fail("Human01 snapshot unit has render sprite reference");
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
    if (!snapshot_has_effect(&snapshot, "SPRITES/BEAC.SPR")) {
        return fail("Human01 scripted beacon effect is visible");
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
    return 0;
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
    if (snapshot.decoration_count <= 0) {
        return fail("Human02 loads map decorations");
    }
    if (snapshot.resource_vent_count <= 0) {
        return fail("Human02 loads Petra-7 vents");
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
