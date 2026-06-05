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

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    if (!model) return fail("create model");

    RtsGameModelConfig config = {
        .game_id = "dark-colony",
        .data_root = "data/DCOLONY",
        .map_path = "SCENARIO/HUMAN/HUMAN01.MAP",
    };
    if (!rts_game_model_load(model, &config)) {
        fprintf(stderr, "model load error: %s\n", rts_game_model_last_error(model));
        rts_game_model_destroy(model);
        return fail("load Dark Colony Human01");
    }

    RtsRenderSnapshot snapshot;
    if (!rts_game_model_snapshot(model, &snapshot)) {
        rts_game_model_destroy(model);
        return fail("initial snapshot");
    }
    if (snapshot.map_width <= 0 || snapshot.map_height <= 0) {
        rts_game_model_destroy(model);
        return fail("snapshot has map dimensions");
    }
    if (snapshot.unit_count <= 0) {
        rts_game_model_destroy(model);
        return fail("snapshot has initial units");
    }
    if (snapshot.units[0].sprite_name[0] == '\0') {
        rts_game_model_destroy(model);
        return fail("snapshot unit has render sprite reference");
    }

    int movable_player_unit = find_movable_player_unit(&snapshot);
    for (int i = 0; movable_player_unit < 0 && i < 30 * 20; ++i) {
        if (!rts_game_model_tick(model, 1.0f / 30.0f)) {
            rts_game_model_destroy(model);
            return fail("tick model while waiting for player units");
        }
        if (!rts_game_model_snapshot(model, &snapshot)) {
            rts_game_model_destroy(model);
            return fail("snapshot while waiting for player units");
        }
        movable_player_unit = find_movable_player_unit(&snapshot);
    }
    if (movable_player_unit < 0) {
        rts_game_model_destroy(model);
        return fail("snapshot has a player-owned mobile selectable unit");
    }

    RtsGameCommand select_first = {
        .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = {
            .unit_index = movable_player_unit,
            .additive = false,
        },
    };
    if (!rts_game_model_command(model, &select_first)) {
        rts_game_model_destroy(model);
        return fail("select command");
    }
    if (!rts_game_model_snapshot(model, &snapshot)) {
        rts_game_model_destroy(model);
        return fail("snapshot after selection");
    }
    int selected = 0;
    int first_selected = -1;
    for (int i = 0; i < snapshot.unit_count; ++i) {
        if (!snapshot.units[i].selected) continue;
        selected++;
        if (first_selected < 0) first_selected = i;
    }
    if (selected != 1 || first_selected != movable_player_unit) {
        rts_game_model_destroy(model);
        return fail("select command updates snapshot");
    }

    RtsGameCommand move = {
        .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = {
            .gx = snapshot.units[first_selected].gx,
            .gy = snapshot.units[first_selected].gy,
        },
    };
    if (!rts_game_model_command(model, &move)) {
        rts_game_model_destroy(model);
        return fail("move command");
    }
    if (!rts_game_model_tick(model, 1.0f / 30.0f)) {
        rts_game_model_destroy(model);
        return fail("tick model");
    }
    if (!rts_game_model_snapshot(model, &snapshot)) {
        rts_game_model_destroy(model);
        return fail("snapshot after move");
    }

    if (!snapshot.units[movable_player_unit].selected ||
        !snapshot.units[movable_player_unit].has_move_order) {
        rts_game_model_destroy(model);
        return fail("move command is visible through render snapshot after tick");
    }

    printf("PASS: headless game model loaded %dx%d with %d units and %d effects\n",
           snapshot.map_width, snapshot.map_height, snapshot.unit_count,
           snapshot.effect_count);
    rts_game_model_destroy(model);
    return 0;
}
