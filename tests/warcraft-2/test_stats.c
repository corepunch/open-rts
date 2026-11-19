#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <string.h>

#define CHECK(c) RTS_CHECK(c, "Warcraft II result statistics", #c)

static mobj_t *spawn(int type, int owner) {
    mobj_t *unit = P_SpawnMobj(fixed3_zero(), type);
    if (unit) unit->owner = (uint8_t)owner;
    return unit;
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level));
    CHECK(P_LoadThings(NULL) > 0);
    memset(level.w2_tally, 0, sizeof(level.w2_tally));
    CHECK(consoleplayer >= 0 && consoleplayer < 8);
    int me = consoleplayer, foe = (me + 1) % 8;

    /* Produced units and buildings count; walls and neutral owners do not. */
    mobj_t *footman = spawn(MT_FOOTMAN, me), *grunt = spawn(MT_GRUNT, foe);
    mobj_t *farm = spawn(MT_FARM, foe), *wall = spawn(MT_HUMAN_WALL, me);
    CHECK(footman && grunt && farm && wall);
    G_UnitProduced(footman, NULL);
    G_UnitProduced(grunt, NULL);
    G_UnitProduced(farm, NULL);
    G_UnitProduced(wall, NULL);
    CHECK(W2_Tally(me)->units == 1 && W2_Tally(me)->buildings == 0);
    CHECK(W2_Tally(foe)->units == 1 && W2_Tally(foe)->buildings == 1);

    /* A kill earns the victim type's Points; a razing is a structure kill. */
    int unit_points = mobjinfo[MT_GRUNT].w2.points, farm_points = mobjinfo[MT_FARM].w2.points;
    CHECK(unit_points > 0 && farm_points > 0);
    P_DamageMobj(grunt, footman, 100000);
    CHECK(W2_Tally(me)->kills == 1 && W2_Tally(me)->razings == 0 && W2_Tally(me)->score == unit_points);
    P_DamageMobj(farm, footman, 100000);
    CHECK(W2_Tally(me)->kills == 1 && W2_Tally(me)->razings == 1 &&
          W2_Tally(me)->score == unit_points + farm_points);
    CHECK(W2_Tally(foe)->kills == 0 && W2_Tally(foe)->score == 0);
    /* No killer, or killing one's own, scores nothing. */
    mobj_t *own = spawn(MT_FOOTMAN, me), *orphan = spawn(MT_GRUNT, foe);
    CHECK(own && orphan);
    P_DamageMobj(own, footman, 100000);
    P_DamageMobj(orphan, NULL, 100000);
    CHECK(W2_Tally(me)->kills == 1 && W2_Tally(me)->score == unit_points + farm_points);

    /* Banked resources accumulate per type. */
    W2_TallyResource(me, 0, 100);
    W2_TallyResource(me, 0, 100);
    W2_TallyResource(me, 1, 100);
    W2_TallyResource(me, 2, 50);
    W2_TallyResource(me, 3, 50); /* not a resource */
    W2_TallyResource(8, 0, 50);  /* not a player */
    CHECK(W2_Tally(me)->resources[0] == 200 && W2_Tally(me)->resources[1] == 100 &&
          W2_Tally(me)->resources[2] == 50);

    /* The counters are part of the lockstep checksum. */
    uint32_t before = G_Consistency();
    level.w2_tally[me].kills++;
    CHECK(G_Consistency() != before);
    level.w2_tally[me].kills--;
    CHECK(G_Consistency() == before);

    /* Wargus rank tables: a rank starts above its floor. */
    CHECK(!strcmp(W2_RankName(false, 0), "Servant") && !strcmp(W2_RankName(true, 0), "Slave"));
    CHECK(!strcmp(W2_RankName(false, 2000), "Servant") && !strcmp(W2_RankName(false, 2001), "Peasant"));
    CHECK(!strcmp(W2_RankName(true, 8001), "Grunt") && !strcmp(W2_RankName(false, 85001), "Knight"));
    CHECK(!strcmp(W2_RankName(true, 99999999), "Designer") && W2_RankIndex(280001) == 18);

    P_FreeLevel(&level);
    puts("PASS: Warcraft II tracks per-player counters, score and rank");
    return 0;
}
