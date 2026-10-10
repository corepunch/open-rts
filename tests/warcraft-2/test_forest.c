#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II forest", #c)

static void fixture(void) {
    P_FreeLevel(&level);
    G_InitGame();
    P_InitThinkers();
    level.width = level.height = 9;
    level.tile_ids = calloc(81, sizeof(*level.tile_ids));
    level.cell_terrain = calloc(81, 1);
    level.blocked = calloc(81, 1);
    level.cell_solid = calloc(81, 1);
    level.resource_vents = calloc(81, sizeof(*level.resource_vents));
    for (int i = 0; i < 81; ++i) level.tile_ids[i] = 0x50;
}

static void tree(ivec2_t cell, uint16_t tile) {
    int i = L_Index(&level, cell.x, cell.y);
    level.tile_ids[i] = tile;
    level.blocked[i] = 1;
    level.cell_terrain[i] = 2;
    level.resource_vents[level.resource_vent_count++] = (resourcevent_t){
        .cell = cell, .attachment = fixed2_cell_center(cell), .footprint = {1, 1},
        .resource_type = 1, .active = true, .amount = 100, .rate = 100,
    };
}

static int cut(ivec2_t cell) {
    mobj_t *peasant = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(1.5, 1.5), 0), MT_PEASANT);
    CHECK(peasant);
    for (int i = 0; i < level.resource_vent_count; ++i) {
        if (!ivec2_equal(level.resource_vents[i].cell, cell)) continue;
        peasant->harvest.target = i;
        peasant->harvest.phase = HARVEST_PHASE_MINING;
        peasant->w2.chops = 50;
        CHECK(W2_TickHarvest(peasant));
        CHECK(peasant->harvest.cargo == 100);
        CHECK(!level.resource_vents[i].active && !level.resource_vents[i].amount);
        P_RemoveMobj(peasant);
        P_RunThinkers();
        return 0;
    }
    CHECK(false);
    return 1;
}

static int test_hole(void) {
    fixture();
    for (int y = 2; y <= 6; ++y)
        for (int x = 2; x <= 6; ++x) tree((ivec2_t){x, y}, 0x70);
    RTS_RUN(cut((ivec2_t){4, 4}));
    /* NW/N/NE, W/stump/E, SW/S/SE: Stratagus corner intersections. */
    const uint16_t expected[3][3] = {
        {0x760, 0x720, 0x7a0}, {0x740, W2_TILE_LOOKUP, 0x790}, {0x7c0, 0x7b0, 0x7d0},
    };
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) {
            int i = L_Index(&level, x, y);
            uint16_t tile = x >= 2 && x <= 6 && y >= 2 && y <= 6 ? 0x70 : 0x50;
            if (x >= 3 && x <= 5 && y >= 3 && y <= 5) tile = expected[y - 3][x - 3];
            CHECK(level.tile_ids[i] == tile);
            CHECK((level.cell_terrain[i] == 2) == (tile != 0x50 && tile != W2_TILE_LOOKUP));
            CHECK(level.blocked[i] == (level.cell_terrain[i] == 2));
        }
    return 0;
}

static int test_columns(void) {
    fixture();
    tree((ivec2_t){3, 3}, 0x770);
    tree((ivec2_t){4, 3}, 0x730);
    tree((ivec2_t){3, 4}, 0x710);
    tree((ivec2_t){4, 4}, 0x700);
    RTS_RUN(cut((ivec2_t){3, 4}));
    CHECK(level.tile_ids[L_Index(&level, 3, 3)] == W2_TILE_REMOVED_TREE);
    CHECK(!level.resource_vents[0].active && !level.resource_vents[0].amount);
    CHECK(!level.blocked[L_Index(&level, 3, 3)] && !level.cell_terrain[L_Index(&level, 3, 3)]);
    CHECK(level.tile_ids[L_Index(&level, 4, 3)] == W2_TILE_TREE_TOP);
    CHECK(level.tile_ids[L_Index(&level, 4, 4)] == W2_TILE_TREE_BOTTOM);
    CHECK(level.resource_vents[1].active && level.resource_vents[1].amount == 100);
    CHECK(level.resource_vents[3].active && level.resource_vents[3].amount == 100);
    RTS_RUN(cut((ivec2_t){4, 4}));
    for (int i = 0; i < 4; ++i) {
        const resourcevent_t *vent = &level.resource_vents[i];
        int cell = L_Index(&level, vent->cell.x, vent->cell.y);
        CHECK(!vent->active && !vent->amount);
        CHECK(level.tile_ids[cell] == W2_TILE_REMOVED_TREE && !level.blocked[cell]);
    }

    fixture();
    tree((ivec2_t){4, 3}, W2_TILE_TREE_TOP);
    tree((ivec2_t){4, 4}, W2_TILE_TREE_MIDDLE);
    tree((ivec2_t){4, 5}, W2_TILE_TREE_BOTTOM);
    tree((ivec2_t){3, 4}, 0x70);
    RTS_RUN(cut((ivec2_t){3, 4}));
    CHECK(level.tile_ids[L_Index(&level, 4, 3)] == W2_TILE_TREE_TOP);
    CHECK(level.tile_ids[L_Index(&level, 4, 4)] == W2_TILE_TREE_MIDDLE);
    CHECK(level.tile_ids[L_Index(&level, 4, 5)] == W2_TILE_TREE_BOTTOM);
    return 0;
}

static int test_map_edges(void) {
    const ivec2_t corners[] = {{0, 0}, {8, 0}, {0, 8}, {8, 8}};
    for (size_t c = 0; c < sizeof(corners) / sizeof(*corners); ++c) {
        fixture();
        for (int y = 0; y < 9; ++y)
            for (int x = 0; x < 9; ++x) tree((ivec2_t){x, y}, 0x70);
        ivec2_t at = corners[c];
        RTS_RUN(cut(at));
        ivec2_t horizontal = ivec2_add(at, (ivec2_t){at.x ? -1 : 1, 0});
        ivec2_t vertical = ivec2_add(at, (ivec2_t){0, at.y ? -1 : 1});
        CHECK(level.tile_ids[L_Index(&level, horizontal.x, horizontal.y)] == (at.x ? 0x740 : 0x790));
        CHECK(level.tile_ids[L_Index(&level, vertical.x, vertical.y)] == (at.y ? 0x720 : 0x7b0));
        CHECK(level.tile_ids[L_Index(&level, 4, 4)] == 0x70);
    }
    return 0;
}

/* Optional winter old/new contact sheet, made directly from native
 * indexed megatiles so the border regression is inspectable without a UI. */
static int test_native_tiles(const char *bmp) {
    w2_archive_t archive;
    CHECK(w2_archive_open(&archive, "data/WAR2/DATA/MAINDAT.WAR"));
    for (int era = 0; era < 4; ++era) {
        tileset_t tiles = {0};
        CHECK(w2_decode_tileset(&archive, era, &tiles));
        CHECK(tiles.tile_lookup_count == W2_TILE_COUNT);
        CHECK(tiles.tile_lookup[W2_TILE_TREE_TOP] == 121);
        CHECK(tiles.tile_lookup[W2_TILE_TREE_MIDDLE] == 122);
        CHECK(tiles.tile_lookup[W2_TILE_TREE_BOTTOM] == 123);
        CHECK(tiles.tile_lookup[W2_TILE_REMOVED_TREE] == 126);
        for (int group = 0x700; group < 0x7e0; group += 16)
            CHECK(tiles.tile_lookup[group] > 0 && tiles.tile_lookup[group] < tiles.count);
        if (bmp && era == 1) {
            RTS_RUN(test_hole());
            SDL_Surface *image = SDL_CreateRGBSurfaceWithFormat(0, 320, 160, 32, SDL_PIXELFORMAT_ARGB8888);
            CHECK(image);
            for (int y = 0; y < 160; ++y)
                for (int x = 0; x < 320; ++x) {
                    int id = x < 160 ? 0x70 : level.tile_ids[L_Index(&level, 2 + (x - 160) / 32, 2 + y / 32)];
                    if (x / 32 == 2 && y / 32 == 2) id = W2_TILE_REMOVED_TREE;
                    int tile = tiles.tile_lookup[id];
                    uint8_t color = tiles.indices[tile * 1024 + (y % 32) * 32 + x % 32];
                    ((uint32_t *)((uint8_t *)image->pixels + y * image->pitch))[x] = tiles.palette[color];
                }
            SDL_Surface *rgb = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_RGB24, 0);
            CHECK(rgb && SDL_SaveBMP(rgb, bmp) == 0);
            SDL_FreeSurface(rgb);
            SDL_FreeSurface(image);
        }
        R_FreeTileset(&tiles);
    }
    w2_archive_close(&archive);
    return 0;
}

int main(int argc, char **argv) {
    RTS_RUN(test_hole());
    RTS_RUN(test_columns());
    RTS_RUN(test_map_edges());
    RTS_RUN(test_native_tiles(argc > 1 ? argv[1] : NULL));
    P_FreeLevel(&level);
    puts("PASS: Warcraft II cleared forest borders");
    return 0;
}
