#include "t_local.h"
#include <stdio.h>
#include "warcraft-2.h"
#include "w2_local.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d\n", __LINE__); \
    return rts_fail("Warcraft II orc objectives", #c); } } while (0)

/* Owner slots as the PUD names them: 4 computer, 5 person, 6 rescue. */
static w2_pud_t pud = {.owners = {5, 4, 6, 4, 0, 0, 0, 4}};

static void fixture(int mission) {
    level.native_data = NULL;
    P_FreeLevel(&level); P_InitThinkers(); G_InitGame();
    consoleplayer = 0; leveltime = 1;
    level.width = level.height = 32;
    level.blocked = calloc(1024, 1);
    level.cell_solid = calloc(1024, 1);
    level.cell_terrain = calloc(1024, 1);
    level.tile_ids = calloc(1024, sizeof(*level.tile_ids));
    W2_SetCampaign(mission, true);
    assert(w2_init_mission(&level));
    W2_VictoryReset();
    level.native_data = &pud;
}

/* A unit of `type` whose footprint's top-left cell is (x, y). */
static fixed2_t corner_at(int type, int x, int y) {
    isize2_t f = mobjinfo[type].w2.footprint;
    return FIXED2_LIT(x + f.w * 0.5, y + f.h * 0.5);
}

static mobj_t *spawn_at(int type, fixed2_t at, int owner) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(at, 0), type);
    assert(unit);
    unit->owner = unit->team = owner;
    unit->allegiance = owner == consoleplayer ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
    unit->traits |= MF_NOAUTOTARGET;
    return unit;
}

static mobj_t *spawn(int type, int x, int y, int owner) {
    return spawn_at(type, corner_at(type, x, y), owner);
}

/* Runs one objectives check now and reports the outcome it reached. */
static outcome_t check_now(void) {
    mobjlist_t list = P_ListMobjs();
    ((w2_mission_t *)level.mission)->countdown = 0;
    G_LevelObjectives(list.items, list.count);
    P_FreeMobjList(&list);
    return G_LevelOutcome();
}

/* Mission 4: destroy every opponent. Walls do not keep an opponent alive. */
static int destroy_all(void) {
    fixture(4);
    spawn(MT_GRUNT, 4, 4, 0);
    mobj_t *enemy = spawn(MT_GRUNT, 10, 10, 1);
    spawn(MT_ORC_WALL, 12, 12, 1);
    CHECK(check_now() == OUTCOME_NONE);
    enemy->hp = 0;
    CHECK(check_now() == OUTCOME_VICTORY);

    fixture(4);
    spawn(MT_GRUNT, 4, 4, 0);
    spawn(MT_HUMAN_WALL, 10, 10, 1);
    CHECK(check_now() == OUTCOME_VICTORY);
    return 0;
}

/* Mission 3: one shipyard and four oil platforms, finished ones only. */
static int shipyard_platforms(void) {
    fixture(3);
    spawn(MT_ORC_SHIPYARD, 4, 4, 0);
    for (int i = 0; i < 3; ++i) spawn(MT_ORC_OIL_PLATFORM, 10 + i * 3, 10, 0);
    CHECK(check_now() == OUTCOME_NONE);
    mobj_t *foundation = spawn(MT_ORC_OIL_PLATFORM, 20, 10, 0);
    foundation->w2.build_left_tics = 5;
    CHECK(check_now() == OUTCOME_NONE);
    foundation->w2.build_left_tics = 0;
    CHECK(check_now() == OUTCOME_VICTORY);
    return 0;
}

/* Mission 6: Cho'gall near a circle of power wins; losing him for both sides loses. */
static int chogall(void) {
    fixture(6);
    mobj_t *hero = spawn(MT_CHOGALL, 20, 20, 0);
    spawn(MT_CIRCLE_OF_POWER, 10, 10, 0);
    CHECK(check_now() == OUTCOME_NONE);
    hero->core.position = fixed3_from_fixed2(FIXED2_LIT(12.5, 10.5), 0);
    CHECK(check_now() == OUTCOME_VICTORY);

    fixture(6);
    spawn(MT_GRUNT, 4, 4, 0);
    CHECK(check_now() == OUTCOME_DEFEAT);

    fixture(6);
    spawn(MT_GRUNT, 4, 4, 0);
    spawn(MT_CHOGALL, 20, 20, 3);
    CHECK(check_now() == OUTCOME_NONE);
    return 0;
}

/* Mission 2: Zuljin is held by player 2 until rescued near a circle; losing
 * him after holding him is a defeat. */
static int zuljin(void) {
    fixture(2);
    mobj_t *hero = spawn(MT_ZULJIN, 20, 20, 2);
    spawn(MT_CIRCLE_OF_POWER, 10, 10, 0);
    spawn(MT_GRUNT, 4, 4, 0);
    CHECK(check_now() == OUTCOME_NONE);
    hero->owner = hero->team = 0;
    hero->allegiance = ALLEGIANCE_PLAYER;
    CHECK(check_now() == OUTCOME_NONE);
    hero->core.position = fixed3_from_fixed2(FIXED2_LIT(12.5, 10.5), 0);
    CHECK(check_now() == OUTCOME_VICTORY);

    fixture(2);
    hero = spawn(MT_ZULJIN, 20, 20, 0);
    spawn(MT_GRUNT, 4, 4, 0);
    CHECK(check_now() == OUTCOME_NONE);
    hero->hp = 0;
    CHECK(check_now() == OUTCOME_DEFEAT);
    return 0;
}

/* Mission 8: player 7's runestone and player 1's castle must both be gone. */
static int runestone_castle(void) {
    fixture(8);
    mobj_t *runestone = spawn(MT_RUNESTONE, 10, 10, 7);
    mobj_t *castle = spawn(MT_CASTLE, 20, 20, 1);
    spawn(MT_GRUNT, 4, 4, 0);
    CHECK(check_now() == OUTCOME_NONE);
    castle->hp = 0;
    CHECK(check_now() == OUTCOME_NONE);
    runestone->hp = 0;
    CHECK(check_now() == OUTCOME_VICTORY);
    return 0;
}

/* Mission 9: a finished fortress in its zone and a shipyard in a shipyard zone. */
static int fortress_shipyard(void) {
    fixture(9);
    spawn(MT_FORTRESS, 55, 20, 0);
    spawn(MT_ORC_SHIPYARD, 60, 50, 0);
    CHECK(check_now() == OUTCOME_NONE);
    mobj_t *yard = spawn(MT_ORC_SHIPYARD, 50, 30, 0);
    yard->w2.build_left_tics = 5;
    CHECK(check_now() == OUTCOME_NONE);
    yard->w2.build_left_tics = 0;
    CHECK(check_now() == OUTCOME_VICTORY);
    return 0;
}

int main(void) {
    RTS_RUN(destroy_all());
    RTS_RUN(shipyard_platforms());
    RTS_RUN(chogall());
    RTS_RUN(zuljin());
    RTS_RUN(runestone_castle());
    RTS_RUN(fortress_shipyard());
    level.native_data = NULL;
    P_FreeLevel(&level);
    puts("PASS: Orc campaign missions 2-14 victory and defeat objectives");
    return 0;
}
