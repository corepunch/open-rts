#include "engine_config.h"
#include "../rts_model_test.h"
#include "../../games/dark-colony/info.h"

static int troopers(const RtsRenderSnapshot *snapshot) {
    int count = 0;
    for (int i = 0; i < snapshot->unit_count; ++i)
        if (snapshot->units[i].owner == 0 && snapshot->units[i].type_id == MT_TROOPER)
            count++;
    return count;
}

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/DCOLONY", .map_path = "SCENARIO/HUMAN/HUMAN01.MAP",
    };
    RTS_CHECK(model && rts_game_model_load(model, &config), "dropship", "load Human01");
    RtsRenderSnapshot snapshot;
    RTS_CHECK(rts_game_model_snapshot(model, &snapshot), "dropship", "initial snapshot");
    int initial = troopers(&snapshot);
    bool seen = false, delivered = false, departed = false;
    for (int tick = 0; tick < 30 * 120; ++tick) {
        RTS_CHECK(rts_tick(model, &snapshot), "dropship", "tick reinforcement");
        bool visible = false;
        for (int i = 0; i < snapshot.unit_count; ++i)
            visible |= snapshot.units[i].type_id == MT_DROPSHIP;
        seen |= visible;
        delivered |= troopers(&snapshot) >= initial + 4;
        if (seen && !visible) { departed = true; break; }
    }
    fprintf(stderr, "dropship seen=%d delivered=%d departed=%d troopers=%d -> %d\n",
            seen, delivered, departed, initial, troopers(&snapshot));
    rts_game_model_destroy(model);
    RTS_CHECK(seen && delivered && departed, "dropship", "deliver all four Troopers and remove ship object");
    return 0;
}
