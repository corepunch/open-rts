#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Soak: the computer plays campaign levels against an idle player.
 * It must take territory from its start or take the player's fort, keep a
 * live army, and play the same game twice from the same start. AI_TRACE=1
 * prints each level's course: territories per side, task states, units. */

static int fail(const char *msg) { return rts_fail("z computer soak", msg); }

static int held(int team) {
    int total = 0;
    return Z_TerritoriesHeld(team, &total);
}
static int units(int team) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function == P_MobjThinker && !mo->remove && mo->hp > 0 && mo->team == team &&
            Z_UnitType(mo) >= 0) n++;
    }
    return n;
}
static void trace(int minute) {
    if (!getenv("AI_TRACE")) return;
    const zmap_t *map = level.native_data;
    int states[16] = {0}, mobile = 0;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        const zaitask_t *t = &map->ai.tasks[i];
        if (!t->unit) continue;
        states[t->task < 0 ? 15 : t->task & 15]++;
        mobile += (t->flags & 16) != 0;
    }
    printf("  %2d min: territories human %d cpu %d; units human %d cpu %d; tasks idle %d march %d flag %d "
           "staged %d assault %d garrison %d vehicle %d frontier %d mobile %d\n",
           minute, held(0), held(1), units(0), units(1), states[15], states[0], states[1], states[2],
           states[3], states[4], states[5], states[10], mobile);
}

typedef struct { uint32_t hash; int start, end, army, minutes; outcome_t outcome; } game_t;
static bool play(const char *map_path, int minutes, game_t *out) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/Z", .map_path = map_path};
    if (!model || !rts_game_model_load(model, &config)) { rts_game_model_destroy(model); return false; }
    *out = (game_t){.start = held(1)};
    RtsRenderSnapshot snap;
    for (out->minutes = 1; out->minutes <= minutes && G_LevelOutcome() == OUTCOME_NONE; ++out->minutes) {
        for (int t = 0; t < 60 * RTS_TICRATE && G_LevelOutcome() == OUTCOME_NONE; ++t)
            if (!rts_tick(model, &snap)) { rts_game_model_destroy(model); return false; }
        trace(out->minutes);
    }
    out->outcome = G_LevelOutcome();
    out->end = held(1);
    out->army = units(1);
    out->hash = G_ConsistencyExtra(0);
    rts_game_model_destroy(model);
    return true;
}

int main(void) {
    static const char *levels[] = {"LEVEL01/LEVEL01.MAP", "LEVEL02/LEVEL02.MAP", "LEVEL05/LEVEL05.MAP"};
    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        game_t first, second;
        if (getenv("AI_TRACE")) printf("%s\n", levels[i]);
        if (!play(levels[i], 10, &first)) return fail("level loads and plays");
        bool won = first.outcome == OUTCOME_DEFEAT;
        if (first.end <= first.start && !won) return fail("computer takes territory or the fort from an idle player");
        if (!first.army) return fail("computer keeps an army");
        if (!play(levels[i], 10, &second) || memcmp(&first, &second, sizeof(first)))
            return fail("same start plays the same game");
        if (won) printf("PASS: z %s computer takes the player's fort in %d minutes\n", levels[i], first.minutes - 1);
        else printf("PASS: z %s computer goes from %d to %d territories with %d units\n",
                    levels[i], first.start, first.end, first.army);
    }
    return 0;
}
