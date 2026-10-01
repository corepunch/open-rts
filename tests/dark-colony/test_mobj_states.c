#include "engine.h"
#include "info.h"
#include "t_local.h"

static int calls;
static void count_action(mobj_t *unit) {
    (void)unit;
    calls++;
}
static void redirect_action(mobj_t *unit) {
    calls++;
    P_SetMobjState(unit, 3);
}
static void run_redirect_action(mobj_t *unit) {
    calls++;
    if (unit->core.state_frame == 1) P_SetMobjState(unit, 3);
}

int main(void) {
    const state_t states[] = {
        {0},
        { .tics = 1, .action = count_action, .nextstate = 2 },
        { .tics = 0, .action = redirect_action, .nextstate = 0 },
        { .sprite = 7, .frame = 9, .tics = 2, .action = count_action, .nextstate = 4 },
        { .tics = -1, .action = count_action, .nextstate = 0 },
        { .tics = 0, .action = count_action, .nextstate = 3 },
        { .sprite = 7, .frame = 20, .count = 3, .action = count_action, .nextstate = 4,
          .frame_tics = TICS(2, 1, 3) },
        { .frame = 30, .count = 3, .tics = 1, .action = run_redirect_action, .nextstate = 4 },
        { .frame = 40, .count = 3, .tics = 0, .action = count_action, .nextstate = 4 },
    };
    const mobjinfo_t info[] = { {0}, { .spawnstate = 1, .spawnhealth = 100 } };
    const gameinfo_t game = { .states = states, .state_count = 9,
        .mobjinfo = info, .mobj_type_count = 2, .null_state = 0 };
    gameinfo = &game;
    mobj_t city = { .type_id = 1, .core = { .state_id = 3, .tics = 1, .sprite_id = -1 } };
    P_InitMobj(&game, &city);
    RTS_CHECK(calls == 0 && city.core.sprite_id == 7 && city.core.frame == 9 &&
                  city.core.state_id == 3 && city.core.tics == 1,
              "states", "authored spawn state initializes visuals without actions or resetting tics");
    mobj_t unit = { .type_id = 1 };
    P_InitMobj(&game, &unit);
    RTS_CHECK(calls == 0 && unit.core.state_id == 1 && unit.core.tics == 1,
              "states", "spawn initializes state without invoking an unlinked actor action");
    RTS_CHECK(P_TickMobjState(&unit) && calls == 2 && unit.core.state_id == 3 && unit.core.tics == 2,
              "states", "zero-tic entry action redirects without following stale S_NULL");
    RTS_CHECK(P_TickMobjState(&unit) && unit.core.tics == 1 && calls == 2,
              "states", "actions run on entry, not every tick");
    RTS_CHECK(P_TickMobjState(&unit) && unit.core.state_id == 4 && calls == 3,
              "states", "normal transition enters persistent state");
    for (int i = 0; i < 10; ++i)
        RTS_CHECK(P_TickMobjState(&unit) && unit.core.tics == -1 && calls == 3,
                  "states", "minus-one tics persist");
    RTS_CHECK(P_SetMobjState(&unit, 5) && unit.core.state_id == 3 &&
                  unit.core.tics == 2 && calls == 5,
              "states", "setter immediately consumes zero-tic chains");
    RTS_CHECK(P_TickMobjState(&unit) && unit.core.state_id == 3 &&
                  unit.core.tics == 1 && calls == 5,
              "states", "thinker advances the installed nonzero state");
    RTS_CHECK(P_SetMobjState(&unit, 6) && unit.core.state_frame == 0 && unit.core.frame == 20 &&
                  unit.core.tics == 2 && calls == 6,
              "states", "a run starts at its first frame");
    RTS_CHECK(P_TickMobjState(&unit) && unit.core.state_frame == 0 && calls == 6 &&
                  P_TickMobjState(&unit) && unit.core.state_id == 6 &&
                  unit.core.state_frame == 1 && unit.core.frame == 21 &&
                  unit.core.tics == 1 && calls == 7,
              "states", "a run advances its frame, runs its action again and keeps its state");
    RTS_CHECK(P_TickMobjState(&unit) && unit.core.state_frame == 2 && unit.core.frame == 22 &&
                  unit.core.tics == 3 && calls == 8,
              "states", "each frame of a run has its own duration");
    for (int i = 0; i < 3; ++i) RTS_CHECK(P_TickMobjState(&unit), "states", "run ticks");
    RTS_CHECK(unit.core.state_id == 4 && unit.core.state_frame == 0 && unit.core.tics == -1 &&
                  calls == 9,
              "states", "the next state follows the last frame of a run");
    RTS_CHECK(P_SetMobjStateFrame(&unit, 6, 2) && unit.core.frame == 22 &&
                  unit.core.tics == 3 && calls == 10,
              "states", "a run can be entered at a later frame");
    RTS_CHECK(P_SetMobjStateFrame(&unit, 6, 3) && unit.core.state_id == 4 && calls == 11,
              "states", "a frame past the run enters the next state");
    RTS_CHECK(P_SetMobjState(&unit, 8) && unit.core.state_id == 4 && calls == 15,
              "states", "zero-tic frames chain through the run into the next state");
    RTS_CHECK(P_SetMobjState(&unit, 7) && unit.core.state_id == 7 && calls == 16 &&
                  P_TickMobjState(&unit) && unit.core.state_id == 3 &&
                  unit.core.state_frame == 0 && unit.core.frame == 9 &&
                  unit.core.tics == 2 && calls == 18,
              "states", "an action redirect inside a run leaves the run");
    RTS_CHECK(!P_SetMobjState(&unit, 0) && unit.remove,
              "states", "S_NULL removes actor");
    RTS_CHECK(!P_SetMobjState(&unit, 3) && !P_TickMobjState(&unit) && calls == 18,
              "states", "removed actor cannot execute more actions");
    return 0;
}
