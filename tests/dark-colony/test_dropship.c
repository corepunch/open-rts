#include "engine_config.h"
#include "game.h"
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
    int previous_id = -1, previous_state = -1, smooth_ticks = 0;
    fvec2_t previous_position = {0};
    for (int tick = 0; tick < 30 * 120; ++tick) {
        RTS_CHECK(rts_tick(model, &snapshot), "dropship", "tick reinforcement");
        bool visible = false;
        for (int i = 0; i < snapshot.unit_count; ++i)
            visible |= snapshot.units[i].type_id == MT_DROPSHIP;
        mobjlist_t objects = P_ListMobjs();
        for (int i = 0; i < objects.count; ++i) {
            const mobj_t *unit = objects.items[i];
            if (unit->type_id == MT_DROPSHIP) {
                RTS_CHECK(unit->core.position.z * g_cell_h == 50 * FIXED_ONE,
                          "dropship", "retain 50 px altitude through every flight/unload state");
                fvec2_t position = fixed3_xy_to_fvec2(unit->core.position);
                if (previous_id == unit->id) {
                    float distance2 = fvec2_distance_squared(position, previous_position);
                    float step = unit->speed * RTS_FIXED_DT + 2.0f / FIXED_ONE;
                    RTS_CHECK(distance2 <= step * step, "dropship",
                              "movement never batches multiple tics into an animation step");
                    if (distance2 > 0 && previous_state == unit->core.state_id)
                        smooth_ticks++;
                }
                previous_id = unit->id;
                previous_state = unit->core.state_id;
                previous_position = position;
            }
            if (unit->type_id == MT_TROOPER)
                RTS_CHECK(unit->core.position.z == 0, "dropship", "cargo stays on ground");
        }
        P_FreeMobjList(&objects);
        seen |= visible;
        delivered |= troopers(&snapshot) >= initial + 4;
        if (seen && !visible) { departed = true; break; }
    }
    fprintf(stderr, "dropship seen=%d delivered=%d departed=%d troopers=%d -> %d\n",
            seen, delivered, departed, initial, troopers(&snapshot));
    RTS_CHECK(smooth_ticks > 0, "dropship", "move between animation frame changes");
    rts_game_model_destroy(model);
    RTS_CHECK(seen && delivered && departed, "dropship", "deliver all four Troopers and remove ship object");
    return 0;
}
