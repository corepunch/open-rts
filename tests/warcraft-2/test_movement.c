#include "t_local.h"
#include "info.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II movement", #c)

#include <stdlib.h>

/* Wargus Move frame operands divided by the five authored GRP facings.
 * Ships' other rows are sinking/attack art, not movement poses. */
static const struct { int type, first, count; } movement[] = {
    {MT_BALLISTA, 0, 2}, {MT_CATAPULT, 0, 2},
    {MT_HUMAN_OIL_TANKER, 0, 1}, {MT_ORC_OIL_TANKER, 0, 1},
    {MT_HUMAN_TRANSPORT, 0, 1}, {MT_ORC_TRANSPORT, 0, 1},
    {MT_HUMAN_DESTROYER, 0, 1}, {MT_ORC_DESTROYER, 0, 1},
    {MT_BATTLESHIP, 0, 1}, {MT_OGRE_JUGGERNAUGHT, 0, 1},
    {MT_GNOMISH_SUBMARINE, 0, 1}, {MT_GIANT_TURTLE, 0, 1},
    {MT_FLYING_MACHINE, 0, 2}, {MT_ZEPPELIN, 0, 1},
    {MT_EYE_OF_KILROGG, 0, 1},
    /* Preserve the existing infantry cycle. */
    {MT_FOOTMAN, 1, 4}, {MT_ARCHER, 1, 4},
};

static int check_cycles(void) {
    for (size_t i = 0; i < sizeof(movement) / sizeof(*movement); ++i) {
        const mobjinfo_t *info = &mobjinfo[movement[i].type];
        const state_t *walk = &states[info->seestate];
        CHECK(info->seestate != info->spawnstate);
        CHECK(walk->group == W2_GROUP_WALK && walk->action == A_Chase);
        CHECK(walk->frame == movement[i].first && walk->count == movement[i].count);
        mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(8, 8), 0), movement[i].type);
        CHECK(unit && P_SetMobjState(unit, info->seestate));
        for (int tic = 0; tic < 3 * walk->count * walk->tics; ++tic) {
            CHECK(unit->core.state_id == info->seestate);
            CHECK(unit->core.frame == movement[i].first + (tic / walk->tics) % movement[i].count);
            CHECK(P_TickMobjState(unit));
        }
        CHECK(P_SetMobjState(unit, info->spawnstate) && unit->core.frame == 0);
        P_RemoveMobj(unit);
        P_RunThinkers();
    }
    return 0;
}

/* Units walk the eight Warcraft II headings: a goal off at 45 degrees is
 * reached along the diagonal, not by an axis run and a turn. */
static int check_diagonal(void) {
    P_FreeLevel(&level); P_InitThinkers(); G_InitGame();
    level.width = level.height = 32;
    level.tile_ids = calloc(1024, sizeof(*level.tile_ids));
    level.blocked = calloc(1024, 1);
    level.cell_solid = calloc(1024, 1);
    level.cell_terrain = calloc(1024, 1);
    level.speeds = calloc(1, sizeof(*level.speeds));
    level.speeds->class_count = 2;
    level.speeds->terrain[1][0] = 100;
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(4.5, 4.5), 0), MT_FOOTMAN);
    CHECK(unit);
    unit->traits |= MF_NOAUTOTARGET;
    CHECK(P_MoveUnitTo(&level, unit, FIXED2_LIT(14.5, 14.5)));
    int steps = 0, diagonal = 0;
    for (int i = 0; i < 2000 && P_HasMoveOrder(unit); ++i) {
        fixed2_t before = fixed3_xy(unit->core.position);
        P_Ticker();
        fixed2_t step = fixed2_sub(fixed3_xy(unit->core.position), before);
        if (!step.x && !step.y) continue;
        ++steps; /* not a staircase of alternating axis steps */
        diagonal += step.x > 0 && step.y > 0;
    }
    CHECK(steps > 10 && diagonal >= steps - 2);
    CHECK(fixed2_near(fixed3_xy(unit->core.position), FIXED2_LIT(14.5, 14.5), FIXED_LIT(0.1)));
    P_FreeLevel(&level);
    return 0;
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    RTS_RUN(check_cycles());
    spritecache_t cache = {0};
    for (size_t i = 0; i < sizeof(movement) / sizeof(*movement); ++i) {
        int type = movement[i].type;
        CHECK(w2_cache_unit_sprite("data/WAR2", &cache, type - 1));
        const cachedsprite_t *sprite = R_CacheFind(&cache, mobjinfo[type].name);
        CHECK(sprite && sprite->sprite.spritedef.numframes >= movement[i].first + movement[i].count);
        for (int frame = movement[i].first; frame < movement[i].first + movement[i].count; ++frame)
            CHECK(sprite->sprite.spritedef.spriteframes[frame].rotations == 8);
    }
    RTS_RUN(check_cycles());
    R_FreeSpriteCache(&cache);
    RTS_RUN(check_diagonal());
    P_FreeLevel(&level);
    puts("PASS: 17 Warcraft II movement cycles before and after native GRP loading; diagonal headings");
    return 0;
}
