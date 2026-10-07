#include "t_local.h"
#include "warcraft-2.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II fog", #c)

static int mask_at(const tileset_t *tiles, int tile, int x, int y) {
    return tiles->indices[((size_t)tile * 32 + (size_t)y) * 32 + (size_t)x] != 0;
}

static uint8_t pixel(int x, int y) {
    return screens[0].pixels[(size_t)y * (size_t)screens[0].w + (size_t)x];
}

static int test_fog(const char *bmp) {
    G_InitGame();
    P_FreeLevel(&level);
    level.width = level.height = 5;
    CHECK(P_InitSight());
    uint32_t see = SIGHT_EXPLORED | level.sight.allies[consoleplayer];
    for (int i = 0; i < 25; ++i) level.sight.cells[i] = see;
    level.sight.cells[1 + 2 * 5] = 0;
    level.sight.cells[3 + 2 * 5] = SIGHT_EXPLORED;

    w2_archive_t archive;
    CHECK(w2_archive_open(&archive, "data/WAR2/DATA/MAINDAT.WAR"));
    tileset_t tiles = {0};
    CHECK(w2_decode_tileset(&archive, 1, &tiles));
    w2_archive_close(&archive);
    I_SetPalette(tiles.palette);

    uint8_t terrain = 0;
    for (int i = 1; i < 256; ++i)
        if (tiles.palette[i] & 0x00ffffffu) { terrain = (uint8_t)i; break; }
    CHECK(terrain);
    uint8_t black = V_NearestIndex(0xff000000u);
    CHECK(black != terrain);

    app_t app = {.win = {640, 480}, .cell = {32, 32}, .cam = {176, 16}};
    V_AllocScreen(app.win.w, app.win.h);
    memset(screens[0].pixels, terrain, (size_t)app.win.w * (size_t)app.win.h);
    CHECK(gameinfo->draw_fog);
    gameinfo->draw_fog(&app, &level, &tiles);

    int unseen_x = 176 + 32, seen_y = 16 + 64;
    CHECK(pixel(unseen_x + 4, seen_y + 7) == black);
    CHECK(pixel(unseen_x + 21, seen_y + 18) == black);

    int fog_x = 176 + 96;
    int covered = 0, open = 0;
    for (int py = 0; py < 32; ++py)
        for (int px = 0; px < 32; ++px) {
            uint8_t sample = pixel(fog_x + px, seen_y + py);
            if (((px + py) & 1) == 0) {
                CHECK(sample == black);
                ++covered;
            } else {
                CHECK(sample == terrain);
                ++open;
            }
        }
    CHECK(covered == 512 && open == 512);

    int view_x = 176 + 64;
    int shroud = 0, stipple = 0, clear = 0;
    for (int py = 0; py < 32; ++py)
        for (int px = 0; px < 32; ++px) {
            int left = mask_at(&tiles, 4, px, py);
            int right = mask_at(&tiles, 6, px, py);
            uint8_t sample = pixel(view_x + px, seen_y + py);
            if (left && !right) {
                CHECK(sample == black);
                ++shroud;
            } else if (right && !left) {
                if (((px + py) & 1) == 0) {
                    CHECK(sample == black);
                    ++stipple;
                } else {
                    CHECK(sample == terrain);
                    ++clear;
                }
            } else if (!left && !right) {
                CHECK(sample == terrain);
            }
        }
    CHECK(shroud > 50 && stipple > 50 && clear > 20);

    if (bmp) {
        SDL_Surface *image = SDL_CreateRGBSurfaceWithFormat(0, 160, 96, 32, SDL_PIXELFORMAT_ARGB8888);
        CHECK(image);
        for (int y = 0; y < 96; ++y)
            for (int x = 0; x < 160; ++x) {
                uint8_t index = pixel(176 + x, 16 + y);
                ((uint32_t *)((uint8_t *)image->pixels + y * image->pitch))[x] =
                    vpalette[index] | 0xff000000u;
            }
        SDL_Surface *rgb = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_RGB24, 0);
        CHECK(rgb && SDL_SaveBMP(rgb, bmp) == 0);
        SDL_FreeSurface(rgb);
        SDL_FreeSurface(image);
    }

    R_FreeTileset(&tiles);
    P_FreeLevel(&level);
    return 0;
}

int main(int argc, char **argv) {
    RTS_RUN(test_fog(argc > 1 ? argv[1] : NULL));
    puts("PASS: Warcraft II fog masks");
    return 0;
}
