#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II walls and rocks", #c)

enum { N = 9 };

static void fixture(void) {
    P_FreeLevel(&level);
    G_InitGame();
    P_InitThinkers();
    level.width = level.height = N;
    level.tile_ids = calloc(N * N, sizeof(*level.tile_ids));
    level.cell_terrain = calloc(N * N, 1);
    level.blocked = calloc(N * N, 1);
    level.cell_solid = calloc(N * N, 1);
    level.resource_vents = calloc(N * N, sizeof(*level.resource_vents));
    for (int i = 0; i < N * N; ++i) level.tile_ids[i] = 0x50;
    level.speeds = calloc(1, sizeof(*level.speeds));
    level.speeds->class_count = 2;
    level.speeds->terrain[1][0] = 100;
    consoleplayer = 0;
}

static int idx(int x, int y) { return L_Index(&level, x, y); }

static void put_wall(int x, int y, uint16_t tile) {
    level.tile_ids[idx(x, y)] = tile;
    level.cell_solid[idx(x, y)] = level.blocked[idx(x, y)] = 1;
}

static fixed3_t at(int x, int y) { return fixed3_from_fixed2(fixed2_cell_center((ivec2_t){x, y}), 0); }

static int test_classify(void) {
    CHECK(w2_wall_tile(0x090) && w2_wall_tile(0x0b0) && w2_wall_tile(0x0a0) && w2_wall_tile(0x0c0));
    CHECK(w2_wall_tile(0x830) && w2_wall_tile(0x841) && !w2_wall_tile(0x842));
    CHECK(w2_wall_tile(0x832) /* broken */ && !w2_wall_tile(0x834) /* destroyed */);
    CHECK(!w2_wall_tile(0x590) && !w2_wall_tile(0x192) && !w2_wall_tile(0x50));
    CHECK(w2_rock_tile(0x80) && w2_rock_tile(0x470) && !w2_rock_tile(0x70) && !w2_rock_tile(0x590));
    return 0;
}

static int test_join_and_destroy(void) {
    fixture();
    put_wall(3, 4, 0x090); put_wall(4, 4, 0x090); put_wall(5, 4, 0x090);
    w2_walls_init(&level);
    CHECK(W2_WallAt((ivec2_t){4, 4}) && W2_WallHp((ivec2_t){4, 4}) == 40);
    /* Under half hit points the tile is the broken variant of its group. */
    W2_HitWall((ivec2_t){4, 4}, 25);
    CHECK(W2_WallHp((ivec2_t){4, 4}) == 15);
    CHECK((level.tile_ids[idx(4, 4)] & 0xfff0) == 0x890 && (level.tile_ids[idx(4, 4)] & 15) == 3);
    /* Destroyed: the cell opens and shows the destroyed variant. */
    W2_HitWall((ivec2_t){4, 4}, 15);
    CHECK(!W2_WallAt((ivec2_t){4, 4}));
    CHECK(level.cell_solid[idx(4, 4)] == 0 && level.blocked[idx(4, 4)] == 0);
    CHECK((level.tile_ids[idx(4, 4)] & 0xfff0) == 0x890 && (level.tile_ids[idx(4, 4)] & 15) == 6);
    /* The ends stay walls and, no longer joined, become isolated pieces. */
    CHECK(W2_WallAt((ivec2_t){3, 4}) && W2_WallAt((ivec2_t){5, 4}));
    CHECK(level.cell_solid[idx(3, 4)] == 1);
    CHECK((level.tile_ids[idx(3, 4)] & 0xfff0) == 0x090);
    return 0;
}

static int test_neighbor_rejoin(void) {
    fixture();
    put_wall(3, 4, 0x090); put_wall(4, 4, 0x090); put_wall(5, 4, 0x090);
    w2_walls_init(&level);
    /* Any hit re-fixes the tile from its surroundings: E+W neighbours is 0x890. */
    W2_HitWall((ivec2_t){4, 4}, 1);
    CHECK((level.tile_ids[idx(4, 4)] & 0xfff0) == 0x890);
    W2_HitWall((ivec2_t){3, 4}, 1);
    W2_HitWall((ivec2_t){5, 4}, 1);
    CHECK((level.tile_ids[idx(3, 4)] & 0xfff0) == 0x810); /* joins east */
    CHECK((level.tile_ids[idx(5, 4)] & 0xfff0) == 0x870); /* joins west */
    W2_ClearTile((ivec2_t){4, 4});
    CHECK((level.tile_ids[idx(3, 4)] & 0xfff0) == 0x090);
    CHECK((level.tile_ids[idx(5, 4)] & 0xfff0) == 0x090);
    return 0;
}

static int test_orc_and_damage(void) {
    fixture();
    put_wall(4, 4, 0x0a0);
    w2_walls_init(&level);
    CHECK(W2_WallRace((ivec2_t){4, 4}) == 2);
    CHECK(mobjinfo[MT_HUMAN_WALL].spawnhealth == 40 && mobjinfo[MT_ORC_WALL].w2.armor == 20);
    /* CalculateDamageStats: basic - armor, at least 1, plus piercing. */
    CHECK(W2_WallDamage(NULL, 8, 0, 0) == 1);
    CHECK(W2_WallDamage(NULL, 50, 3, 0) == 33);
    return 0;
}

static int test_melee_attack(void) {
    fixture();
    put_wall(4, 4, 0x0b0);
    w2_walls_init(&level);
    mobj_t *grunt = P_SpawnMobj(at(3, 4), MT_GRUNT);
    CHECK(grunt);
    grunt->owner = 1; grunt->team = 1;
    CHECK(W2_GroundOrder(grunt, at(4, 4)));
    for (int i = 0; i < 2000 && W2_WallAt((ivec2_t){4, 4}); ++i) P_Ticker();
    CHECK(!W2_WallAt((ivec2_t){4, 4}));
    CHECK(!level.cell_solid[idx(4, 4)]);
    return 0;
}

static int test_demolish_terrain(void) {
    fixture();
    for (int x = 2; x <= 6; ++x) put_wall(x, 4, 0x090);
    w2_walls_init(&level);
    level.tile_ids[idx(4, 6)] = 0x80;
    level.cell_terrain[idx(4, 6)] = W2_TERRAIN_ROCK;
    level.blocked[idx(4, 6)] = 1;
    w2_demolish_terrain((ivec2_t){4, 4}, 3);
    for (int x = 2; x <= 6; ++x) CHECK(!W2_WallAt((ivec2_t){x, 4}));
    CHECK(level.tile_ids[idx(4, 6)] == W2_TILE_REMOVED_ROCK);
    CHECK(level.cell_terrain[idx(4, 6)] == 0 && !level.blocked[idx(4, 6)]);
    return 0;
}

static int test_rock_borders(void) {
    fixture();
    for (int y = 2; y <= 6; ++y)
        for (int x = 2; x <= 6; ++x) {
            level.tile_ids[idx(x, y)] = 0x80;
            level.cell_terrain[idx(x, y)] = W2_TERRAIN_ROCK;
            level.blocked[idx(x, y)] = 1;
        }
    w2_remove_rock((ivec2_t){4, 4});
    /* The forest corner geometry in the rock groups 0x400.. */
    const uint16_t expected[3][3] = {
        {0x460, 0x420, 0x4a0}, {0x440, W2_TILE_REMOVED_ROCK, 0x490}, {0x4c0, 0x4b0, 0x4d0},
    };
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            CHECK(level.tile_ids[idx(3 + x, 3 + y)] == expected[y][x]);
    CHECK(!level.blocked[idx(4, 4)] && level.cell_terrain[idx(4, 4)] == 0);
    return 0;
}

static int test_catapult_shot(void) {
    fixture();
    put_wall(5, 4, 0x090);
    w2_walls_init(&level);
    mobj_t *catapult = P_SpawnMobj(at(1, 4), MT_CATAPULT);
    CHECK(catapult);
    catapult->owner = 1; catapult->team = 1;
    CHECK(W2_GroundOrder(catapult, at(5, 4)));
    for (int i = 0; i < 6000 && W2_WallAt((ivec2_t){5, 4}); ++i) P_Ticker();
    CHECK(!W2_WallAt((ivec2_t){5, 4}));
    return 0;
}

int main(void) {
    int failed = 0;
    failed += test_classify();
    failed += test_join_and_destroy();
    failed += test_neighbor_rejoin();
    failed += test_orc_and_damage();
    failed += test_melee_attack();
    failed += test_demolish_terrain();
    failed += test_rock_borders();
    failed += test_catapult_shot();
    if (!failed) printf("PASS: warcraft-2 tile walls and rocks\n");
    return failed;
}
