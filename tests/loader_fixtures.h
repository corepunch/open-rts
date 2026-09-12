#ifndef __LOADER_FIXTURES__
#define __LOADER_FIXTURES__

#ifdef LOADER_FIXTURES
#include <unistd.h>

static void put16(uint8_t *p, unsigned value) { p[0] = value; p[1] = value >> 8; }
static void put32(uint8_t *p, unsigned value) { put16(p, value); put16(p + 2, value >> 16); }

#if defined(SL) || defined(DC)
static void write_fixture(char path[64], const uint8_t *data, size_t size) {
    snprintf(path, 64, "/private/tmp/loader-fixture-XXXXXX");
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    FILE *file = fdopen(fd, "wb");
    CHECK(file && fwrite(data, 1, size, file) == size);
    CHECK(fclose(file) == 0);
}
#endif

#if defined(DR) || defined(SL)
static bool fixture_sprite(SDL_Renderer *renderer, const uint8_t *data, size_t size,
                            const uint32_t *palette, spritesheet_t *sprite) {
#ifdef DR
    return load_dark_sprite(renderer, data, size, palette, sprite);
#else
    char path[64];
    write_fixture(path, data, size);
    bool ok = sl_load_bim_sprite(renderer, path, palette, sprite);
    CHECK(unlink(path) == 0);
    return ok;
#endif
}

static void reject_sprite(SDL_Renderer *renderer, const uint8_t *data, size_t size,
                           const uint32_t *palette) {
    spritesheet_t sprite;
    CHECK(!fixture_sprite(renderer, data, size, palette, &sprite));
    CHECK(!sprite.cells && !sprite.lumps && !sprite.spritedef.spriteframes);
    R_FreeSprite(&sprite);
}
#endif

static void test_loader_fixtures(SDL_Renderer *renderer) {
    uint32_t palette[256] = {0};
    palette[1] = 0xff123456u;
    palette[2] = 0xffabcdefu;
    uint8_t file[128] = {0};
#if defined(DR) || defined(SL)
#ifdef DR
    memcpy(file, "RSPR", 4);
    put32(file + 4, 0x210);
    put32(file + 8, 1); put32(file + 12, 1);
    put32(file + 16, 2); put32(file + 20, 1);
    put32(file + 24, 1); put32(file + 28, 1);
    put32(file + 64, 4);
    file[68] = 0; file[69] = 2; file[70] = 1; file[71] = 2;
    size_t size = 72;
#else
    put32(file, 4);
    put16(file + 4, 10); put16(file + 6, 1);
    put16(file + 8, 1); put16(file + 10, 0); put16(file + 12, 2);
    palette[0] = 0xff314159u; /* Span index zero is opaque; absent spans are transparent. */
    file[14] = 0; file[15] = 2;
    size_t size = 16;
#endif
    spritesheet_t sprite;
    CHECK(fixture_sprite(renderer, file, size, palette, &sprite));
    CHECK(sprite.numlumps == 1 && sprite.spritedef.numframes == 1);
    CHECK(sprite.cells[0].rect.w == 2 && sprite.cells[0].rect.h == 1);
    uint32_t actual[2];
    SDL_Rect area = { 0, 0, 2, 1 };
    CHECK(SDL_SetTextureBlendMode(sprite.lumps[0].texture, SDL_BLENDMODE_NONE) == 0);
    CHECK(SDL_RenderCopy(renderer, sprite.lumps[0].texture, NULL, &area) == 0);
    CHECK(SDL_RenderReadPixels(renderer, &area, SDL_PIXELFORMAT_ARGB8888, actual, 8) == 0);
#ifdef SL
    CHECK(actual[0] == palette[0] && actual[1] == palette[2]);
#else
    CHECK(actual[0] == palette[1] && actual[1] == palette[2]);
#endif
    R_FreeSprite(&sprite);
    for (size_t n = 0; n < size; ++n) reject_sprite(renderer, file, n, palette);
    reject_sprite(NULL, file, size, palette);
#ifdef DR
    file[69] = 3; /* A literal run cannot overrun the canvas. */
    reject_sprite(renderer, file, size, palette);
    file[69] = 2;
    put32(file + 8, INT32_MAX); /* Header arithmetic must not wrap. */
    reject_sprite(renderer, file, size, palette);
#else
    put16(file + 12, 3); /* Span extends past its source pixels. */
    reject_sprite(renderer, file, size, palette);
    memcpy(file, "VCLZ", 4); put32(file + 4, 2);
    file[8] = 3; file[9] = 0; file[10] = 0;
    reject_sprite(renderer, file, 11, palette); /* Expanded file shorter than its offset word. */
#endif
#elif defined(KK)
    /* Frame -> TRPS flags -> two raw pixels, with authored displacement. */
    put32(file, 3); put32(file + 4, 4); put32(file + 12, 28);
    memcpy(file + 28, "TRPS", 4); put32(file + 32, 1); put32(file + 36, 40);
    put32(file + 40, 2); put32(file + 44, 1);
    file[49] = 1; file[50] = 2;
    spritecell_t cell = {0};
    spritelump_t lump = {0};
    bool flip = false;
    CHECK(decode_mobd_image(renderer, file, 51, 0, palette, &cell, &lump, &flip));
    CHECK(ivec2_equal(cell.displacement, (ivec2_t){ -2, -4 }));
    uint32_t actual[2];
    SDL_Rect area = { 0, 0, 2, 1 };
    CHECK(SDL_SetTextureBlendMode(lump.texture, SDL_BLENDMODE_NONE) == 0);
    CHECK(SDL_RenderCopy(renderer, lump.texture, NULL, &area) == 0);
    CHECK(SDL_RenderReadPixels(renderer, &area, SDL_PIXELFORMAT_ARGB8888, actual, 8) == 0);
    CHECK(actual[0] == palette[1] && actual[1] == palette[2]);
    SDL_DestroyTexture(lump.texture);
    lump = (spritelump_t){0};
    for (size_t n = 0; n < 51; ++n)
        CHECK(!decode_mobd_image(renderer, file, n, 0, palette, &cell, &lump, &flip));
    CHECK(!decode_mobd_image(NULL, file, 51, 0, palette, &cell, &lump, &flip));
    file[48] = 2; file[49] = 4; file[50] = 0; file[51] = 3; file[52] = 1;
    CHECK(!decode_mobd_image(renderer, file, 53, 0, palette, &cell, &lump, &flip));
    file[49] = 2; file[50] = 3; /* Transparent skip past the two-pixel canvas. */
    CHECK(!decode_mobd_image(renderer, file, 51, 0, palette, &cell, &lump, &flip));
#else
    (void)renderer; (void)palette;
    extern bool load_dark_colony_map(const char *, level_t *);
    put32(file, 2); put32(file + 4, 2);
    for (int i = 0; i < 4; ++i) {
        put16(file + 8 + i * 4, i + 1);
        put16(file + 10 + i * 4, i + 11);
    }
    put16(file + 28, (1u << 9) | (1u << 5));
    put16(file + 30, 1u << 6);
    char path[64];
    write_fixture(path, file, 32);
    level_t map;
    CHECK(load_dark_colony_map(path, &map));
    CHECK(map.width == 2 && map.height == 2);
    CHECK(map.tile_ids[0] == 3 && map.tile_ids[3] == 2);
    CHECK(map.tile_overlays[0][0] == 13 && map.tile_overlays[0][3] == 12);
    CHECK(map.blocked[0] && !map.blocked[1]);
    CHECK(map.tile_transforms[0][0] == MAP_TILE_TRANSFORM_FLIP_X);
    CHECK(map.tile_transforms[1][1] == MAP_TILE_TRANSFORM_FLIP_X);
    P_FreeLevel(&map);
    CHECK(unlink(path) == 0);
    for (size_t n = 0; n < 32; ++n) {
        write_fixture(path, file, n);
        CHECK(!load_dark_colony_map(path, &map));
        CHECK(!map.tile_ids && !map.native_data);
        P_FreeLevel(&map);
        CHECK(unlink(path) == 0);
    }
    const uint8_t mtg[] = { 2, 2, 1, 2, 3, 4 };
    write_fixture(path, mtg, sizeof(mtg));
    CHECK(load_dark_colony_map(path, &map));
    CHECK(map.tile_ids[0] == 3 && map.tile_ids[3] == 2);
    P_FreeLevel(&map);
    CHECK(unlink(path) == 0);
#endif
    puts("PASS: loader pixels, metadata, truncated spans, and cleanup");
}
#else
static void test_loader_fixtures(SDL_Renderer *renderer) {
    (void)renderer;
    CHECK(!"Compile with LOADER_FIXTURES to run format fixtures");
}
#endif
#endif
