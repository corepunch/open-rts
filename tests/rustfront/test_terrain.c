#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "rustfront terrain", #c)

static const char *const BADLANDS = "games/rustfront/maps/badlands.txt";
static tileset_t tiles;
static uint32_t pixels[640 * 480];

/* Every letter of the badlands map is a terrain: water, rocks and wrecks block, and keep
 * blocking when the structures' footprints are synced; every cell's tile is in the atlas. */
static int test_map(void) {
    P_FreeLevel(&level);
    P_InitThinkers();
    CHECK(G_DoLoadLevel(BADLANDS, &level));
    spritesheet_t fallback = {0};
    CHECK(W_LoadAssets(g_game_default_root, &level, NULL, &tiles, &fallback));
    CHECK(level.cell_terrain && level.render_transitions == RF_DrawTransitions);
    int seen[256] = {0}, water = RF_TerrainOfLetter('~'), rocks = RF_TerrainOfLetter('R');
    CHECK(water >= 0 && rocks >= 0 && RF_TerrainBlocks(water) && RF_TerrainBlocks(rocks));
    CHECK(!RF_TerrainBlocks(RF_TerrainOfLetter(',')) && RF_TerrainHoldsOre(RF_TerrainOfLetter('*')));
    CHECK(RF_TerrainOfLetter('?') < 0);
    for (int i = 0; i < level.width * level.height; ++i) {
        seen[level.cell_terrain[i]]++;
        CHECK(level.tile_ids[i] < tiles.count);
        CHECK(level.blocked[i] == RF_TerrainBlocks(level.cell_terrain[i]));
    }
    for (const char *l = "~.#,^*Rx="; *l; ++l) CHECK(seen[RF_TerrainOfLetter(*l)] > 0);
    CHECK(P_LoadThings(BADLANDS) > 0);
    RF_SyncBlocking(&level);
    for (int i = 0; i < level.width * level.height; ++i)
        if (RF_TerrainBlocks(level.cell_terrain[i])) CHECK(level.blocked[i]);
    P_FreeLevel(&level);
    puts("PASS: every badlands letter is a terrain; water, rocks and wrecks block");
    return 0;
}

/* A 3 x 3 field of water, with `sand` cells set to sand, drawn as the game draws it. */
static void draw_field(const ivec2_t *sand, int count) {
    P_FreeLevel(&level);
    level.width = level.height = 3;
    level.tile_ids = calloc(9, sizeof(*level.tile_ids));
    level.blocked = calloc(9, 1);
    level.cell_terrain = calloc(9, 1);
    level.render_capabilities = MAP_RENDER_CAP_TERRAIN_TRANSITIONS;
    level.render_transitions = RF_DrawTransitions;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) RF_SetCellTerrain(&level, x, y, RF_TerrainOfLetter('~'));
    for (int i = 0; i < count; ++i) RF_SetCellTerrain(&level, sand[i].x, sand[i].y, RF_TerrainOfLetter('.'));
    app_t app = {.win = {640, 480}, .cell = {48, 32}, .running = true};
    V_BeginFrame(0xff000000u);
    R_DrawLevel(&app, &level, &tiles);
    V_ReadPixels(pixels, 640 * 4);
}

/* How many pixels of the middle cell's quarter (qx, qy in 0..1) differ from `before`. */
static int changed(const uint32_t *before, int qx, int qy) {
    float sx, sy;
    app_t app = {.win = {640, 480}, .cell = {48, 32}};
    R_GridToScreen(&app, 1, 1, &sx, &sy);
    int n = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 24; ++x) {
            int px = (int)sx + qx * 24 + x, py = (int)sy + qy * 16 + y;
            n += before[py * 640 + px] != pixels[py * 640 + px];
        }
    return n;
}

/* Sand spills onto the water beside it: an edge from the north reaches into the cell's top
 * half only, and a lone diagonal neighbour reaches only into its corner. */
static int test_transitions(void) {
    static uint32_t water[640 * 480];
    V_AllocScreen(640, 480);
    R_SetLevelPalette(&tiles);
    draw_field(NULL, 0);
    memcpy(water, pixels, sizeof(water));
    draw_field((ivec2_t[]){{1, 0}}, 1);                    /* sand to the north */
    CHECK(changed(water, 0, 0) > 20 && changed(water, 1, 0) > 20);
    CHECK(changed(water, 0, 1) == 0 && changed(water, 1, 1) == 0);
    draw_field((ivec2_t[]){{2, 0}}, 1);                    /* sand to the north-east only */
    CHECK(changed(water, 1, 0) > 5);
    CHECK(changed(water, 0, 0) == 0 && changed(water, 0, 1) == 0 && changed(water, 1, 1) == 0);
    /* Sand never takes a spill from the water below it. */
    draw_field((ivec2_t[]){{1, 1}}, 1);
    memcpy(water, pixels, sizeof(water));
    draw_field((ivec2_t[]){{1, 1}, {0, 0}, {1, 0}, {2, 0}}, 4);
    CHECK(changed(water, 0, 0) == 0 && changed(water, 1, 0) == 0);
    P_FreeLevel(&level);
    puts("PASS: sand spills over the edges and corners of water, never the other way");
    return 0;
}

int main(void) {
    RTS_RUN(test_map());
    RTS_RUN(test_transitions());
    return 0;
}
