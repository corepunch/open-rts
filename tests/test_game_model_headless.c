#include "game_model.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
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

    RtsGameCommand select_first = {
        .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = {
            .unit_index = 0,
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
    if (selected != 1 || first_selected != 0) {
        rts_game_model_destroy(model);
        return fail("select command updates snapshot");
    }

    if (!rts_game_model_tick(model, 1.0f / 30.0f)) {
        rts_game_model_destroy(model);
        return fail("tick model");
    }
    if (!rts_game_model_snapshot(model, &snapshot)) {
        rts_game_model_destroy(model);
        return fail("snapshot after move");
    }

    if (!snapshot.units[0].selected) {
        rts_game_model_destroy(model);
        return fail("selection remains visible through render snapshot after tick");
    }

    printf("PASS: headless game model loaded %dx%d with %d units and %d effects\n",
           snapshot.map_width, snapshot.map_height, snapshot.unit_count,
           snapshot.effect_count);
    rts_game_model_destroy(model);
    return 0;
}
