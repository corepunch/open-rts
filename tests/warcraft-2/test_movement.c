#include "t_local.h"
#include "info.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II movement", #c)

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
    P_FreeLevel(&level);
    puts("PASS: 17 Warcraft II movement cycles before and after native GRP loading");
    return 0;
}
