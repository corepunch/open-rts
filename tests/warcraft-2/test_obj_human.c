#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <stdlib.h>

#define LINE_STR2(x) #x
#define LINE_STR(x) LINE_STR2(x)
#define CHECK(c) RTS_CHECK(c, "Warcraft II objectives", #c " line " LINE_STR(__LINE__))

/* A fresh campaign mission for `me`. Player `me` is a person; the rest are computers. */
static void fixture(int campaign, bool orc, int me) {
    P_FreeLevel(&level); P_InitThinkers(); G_InitGame();
    consoleplayer = me; leveltime = 1;
    level.width = level.height = 32;
    level.tile_ids = calloc(1024, sizeof(*level.tile_ids));
    level.blocked = calloc(1024, 1); level.cell_solid = calloc(1024, 1); level.cell_terrain = calloc(1024, 1);
    level.speeds = calloc(1, sizeof(*level.speeds));
    level.speeds->class_count = 5;
    level.speeds->terrain[1][0] = level.speeds->terrain[2][0] = 100;
    for (int r = 0; r < 3; ++r) level.player_resources[0][r] = 10000;
    w2_pud_t *pud = calloc(1, sizeof(*pud));
    for (int p = 0; p < 8; ++p) pud->owners[p] = p == me ? 4 : 5;
    level.native_data = pud; level.destroy_native_data = free;
    level.outcome = OUTCOME_NONE;
    W2_SetCampaign(campaign, orc);
    w2_init_mission(&level);
}

static mobj_t *spawn(int type, fixed2_t at, int owner) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(at, 0), type);
    assert(unit);
    unit->owner = unit->team = owner;
    unit->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER;
    unit->traits |= MF_NOAUTOTARGET;
    return unit;
}

/* One objectives check, as G_OutcomeTicker makes it every 30 tics. */
static unsigned check(void) {
    static mobj_t *list[4096];
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap && n < 4096; th = th->next)
        if (th->function == P_MobjThinker) list[n++] = (mobj_t *)th;
    ((w2_mission_t *)level.mission)->countdown = 0;
    return G_LevelObjectives(list, n);
}

static void kill(mobj_t *unit) { unit->hp = 0; }

static int mission2(void) {
    fixture(2, false, 0);
    spawn(MT_RANGER, FIXED2_LIT(10.5, 10.5), 0);
    spawn(MT_CIRCLE_OF_POWER, FIXED2_LIT(11.5, 10.5), 15);
    /* A hostile archer keeps player 6 in the fight: no victory however long. */
    mobj_t *enemy = spawn(MT_ARCHER, FIXED2_LIT(20.5, 20.5), 6);
    CHECK(check() == ELIMINATE_DEFEAT);
    for (int i = 0; i < 6; ++i) check();
    CHECK(level.outcome == OUTCOME_NONE);
    kill(enemy);
    /* 120 cycles held: four checks. */
    for (int i = 0; i < 3; ++i) check();
    CHECK(level.outcome == OUTCOME_NONE);
    check();
    CHECK(level.outcome == OUTCOME_VICTORY);
    return 0;
}

static int mission2_needs_circle(void) {
    fixture(2, false, 0);
    spawn(MT_RANGER, FIXED2_LIT(10.5, 10.5), 0);
    for (int i = 0; i < 8; ++i) check();
    CHECK(level.outcome == OUTCOME_NONE);
    return 0;
}

static int mission3(void) {
    fixture(3, false, 0);
    spawn(MT_HUMAN_SHIPYARD, FIXED2_LIT(5.5, 5.5), 0);
    for (int i = 0; i < 3; ++i) spawn(MT_HUMAN_OIL_PLATFORM, FIXED2_LIT(8.5 + 3 * i, 5.5), 0);
    check();
    CHECK(level.outcome == OUTCOME_NONE);
    spawn(MT_HUMAN_OIL_PLATFORM, FIXED2_LIT(8.5, 9.5), 0);
    check();
    CHECK(level.outcome == OUTCOME_VICTORY);
    return 0;
}

/* Missions whose victory is GetNumOpponents == 0. */
static int no_opponents(void) {
    static const int missions[] = {4, 5, 6, 13};
    for (unsigned m = 0; m < sizeof(missions) / sizeof(missions[0]); ++m) {
        fixture(missions[m], false, 0);
        spawn(MT_FOOTMAN, FIXED2_LIT(5.5, 5.5), 0);
        mobj_t *foe = spawn(MT_FOOTMAN, FIXED2_LIT(20.5, 20.5), 2);
        spawn(MT_HUMAN_WALL, FIXED2_LIT(25.5, 25.5), 3);
        check();
        CHECK(level.outcome == OUTCOME_NONE);
        kill(foe);
        check();
        CHECK(level.outcome == OUTCOME_VICTORY); /* A wall alone is no opponent. */
    }
    return 0;
}

static int mission7(void) {
    fixture(7, false, 0);
    mobj_t *refinery = spawn(MT_ORC_REFINERY, FIXED2_LIT(20.5, 20.5), 2);
    check();
    CHECK(level.outcome == OUTCOME_NONE);
    kill(refinery);
    check();
    CHECK(level.outcome == OUTCOME_VICTORY);
    return 0;
}

static int mission8(void) {
    fixture(8, false, 0);
    spawn(MT_CASTLE, FIXED2_LIT(5.5, 5.5), 0);
    mobj_t *peasant = spawn(MT_PEASANT, FIXED2_LIT(20.5, 20.5), 2);
    check();
    CHECK(level.outcome == OUTCOME_NONE);
    kill(peasant);
    check();
    CHECK(level.outcome == OUTCOME_VICTORY);
    return 0;
}

static int mission9(void) {
    /* Exactly one Uther beside a circle of power wins. */
    fixture(9, false, 0);
    spawn(MT_UTHER_LIGHTBRINGER, FIXED2_LIT(10.5, 10.5), 0);
    spawn(MT_CIRCLE_OF_POWER, FIXED2_LIT(11.5, 10.5), 15);
    spawn(MT_FOOTMAN, FIXED2_LIT(3.5, 3.5), 0);
    check();
    CHECK(level.outcome == OUTCOME_VICTORY);

    /* Two beside it is not exactly one. */
    fixture(9, false, 0);
    spawn(MT_CIRCLE_OF_POWER, FIXED2_LIT(11.5, 10.5), 15);
    spawn(MT_UTHER_LIGHTBRINGER, FIXED2_LIT(10.5, 10.5), 0);
    spawn(MT_UTHER_LIGHTBRINGER, FIXED2_LIT(10.5, 9.5), 0);
    check();
    CHECK(level.outcome == OUTCOME_NONE);

    /* No Uther for this player or player 3 is defeat, while a footman lives. */
    fixture(9, false, 0);
    spawn(MT_FOOTMAN, FIXED2_LIT(3.5, 3.5), 0);
    mobj_t *keep = spawn(MT_UTHER_LIGHTBRINGER, FIXED2_LIT(9.5, 9.5), 3);
    check();
    CHECK(level.outcome == OUTCOME_NONE);
    kill(keep);
    check();
    CHECK(level.outcome == OUTCOME_DEFEAT);
    return 0;
}

static int mission10(void) {
    fixture(10, false, 0);
    spawn(MT_CIRCLE_OF_POWER, FIXED2_LIT(11.5, 10.5), 15);
    spawn(MT_ATTACK_PEASANT, FIXED2_LIT(10.5, 10.5), 0);
    spawn(MT_ATTACK_PEASANT, FIXED2_LIT(10.5, 9.5), 0);
    spawn(MT_ATTACK_PEASANT, FIXED2_LIT(12.5, 10.5), 0);
    spawn(MT_ATTACK_PEASANT, FIXED2_LIT(3.5, 3.5), 0); /* Four alive, only three rescued. */
    check();
    CHECK(level.outcome == OUTCOME_NONE);
    spawn(MT_ATTACK_PEASANT, FIXED2_LIT(12.5, 9.5), 0);
    check();
    CHECK(level.outcome == OUTCOME_VICTORY);

    /* Three attack peasants between this player and player 4, none rescued: defeat. */
    fixture(10, false, 0);
    spawn(MT_ATTACK_PEASANT, FIXED2_LIT(3.5, 3.5), 0);
    spawn(MT_ATTACK_PEASANT, FIXED2_LIT(3.5, 4.5), 0);
    spawn(MT_ATTACK_PEASANT, FIXED2_LIT(3.5, 5.5), 4);
    spawn(MT_FOOTMAN, FIXED2_LIT(9.5, 9.5), 0);
    check();
    CHECK(level.outcome == OUTCOME_DEFEAT);
    return 0;
}

static int mission11(void) {
    fixture(11, false, 0);
    spawn(MT_FOOTMAN, FIXED2_LIT(5.5, 5.5), 0);
    mobj_t *p3 = spawn(MT_FOOTMAN, FIXED2_LIT(20.5, 20.5), 3);
    check();
    CHECK(level.outcome == OUTCOME_NONE);
    kill(p3);
    check();
    CHECK(level.outcome == OUTCOME_VICTORY);
    return 0;
}

static int mission12(void) {
    /* Player 0 is the orc here, so this player is 1. */
    fixture(12, false, 1);
    spawn(MT_FOOTMAN, FIXED2_LIT(5.5, 5.5), 1);
    mobj_t *transport = spawn(MT_ORC_TRANSPORT, FIXED2_LIT(20.5, 20.5), 0);
    mobj_t *refinery = spawn(MT_ORC_REFINERY, FIXED2_LIT(22.5, 20.5), 0);
    mobj_t *shipyard = spawn(MT_ORC_SHIPYARD, FIXED2_LIT(24.5, 20.5), 0);
    check();
    CHECK(level.outcome == OUTCOME_NONE);
    kill(transport); kill(refinery);
    check();
    CHECK(level.outcome == OUTCOME_NONE);
    kill(shipyard);
    check();
    CHECK(level.outcome == OUTCOME_VICTORY);
    return 0;
}

static int mission14(void) {
    fixture(14, false, 0);
    spawn(MT_FOOTMAN, FIXED2_LIT(5.5, 5.5), 0);
    mobj_t *portal = spawn(MT_DARK_PORTAL, FIXED2_LIT(20.5, 20.5), 15);
    check();
    CHECK(level.outcome == OUTCOME_NONE);
    kill(portal);
    check();
    CHECK(level.outcome == OUTCOME_VICTORY);
    return 0;
}

/* Orc missions never take the human triggers, and mission 2 with no units only defeats by elimination. */
static int orc_and_defaults(void) {
    fixture(3, true, 0);
    spawn(MT_HUMAN_SHIPYARD, FIXED2_LIT(5.5, 5.5), 0);
    for (int i = 0; i < 4; ++i) spawn(MT_HUMAN_OIL_PLATFORM, FIXED2_LIT(8.5 + 3 * i, 5.5), 0);
    CHECK(check() == ELIMINATE_DEFEAT);
    CHECK(level.outcome == OUTCOME_NONE);

    fixture(2, false, 0);
    CHECK(check() == ELIMINATE_DEFEAT);
    CHECK(level.outcome == OUTCOME_NONE);
    return 0;
}

int main(void) {
    RTS_RUN(mission2()); RTS_RUN(mission2_needs_circle()); RTS_RUN(mission3());
    RTS_RUN(no_opponents()); RTS_RUN(mission7()); RTS_RUN(mission8()); RTS_RUN(mission9());
    RTS_RUN(mission10()); RTS_RUN(mission11()); RTS_RUN(mission12()); RTS_RUN(mission14());
    RTS_RUN(orc_and_defaults());
    P_FreeLevel(&level);
    puts("PASS: Human campaign objectives 2-14 (victory and defeat), orc campaigns unchanged");
    return 0;
}
