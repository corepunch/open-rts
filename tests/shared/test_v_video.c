#include "v_video.h"
#include "rts_test.h"

#include <string.h>

#define CHECK(c) RTS_CHECK(c, "indexed video", #c)

enum { W = 64, MARKER = 77 };

/* A gray ramp, so a brightness change lands on a predictable index. */
static void gray_palette(uint32_t palette[256]) {
    for (int i = 0; i < 256; ++i)
        palette[i] = 0xff000000u | (unsigned)i * 0x010101u;
}

static void mark_screen(void) {
    memset(screens[0].pixels, MARKER, (size_t)W * W);
}

static int blocks(void) {
    uint8_t block[4] = {0, 2, 0, 3};
    V_BeginFrame(0xff000000u);
    V_DrawBlock((ivec2_t){0, 0}, block, (isize2_t){2, 2}, 2, NULL, 0);
    CHECK(screens[0].pixels[0] == 0);
    CHECK(screens[0].pixels[1] == 2);
    CHECK(screens[0].pixels[W] == 0);
    CHECK(screens[0].pixels[W + 1] == 3);

    /* Source index 0 is skipped by a sprite draw and written by an opaque one. */
    mark_screen();
    V_DrawBlock((ivec2_t){0, 0}, block, (isize2_t){2, 2}, 2, NULL, 0);
    CHECK(screens[0].pixels[0] == MARKER && screens[0].pixels[1] == 2);
    V_DrawBlock((ivec2_t){0, 0}, block, (isize2_t){2, 2}, 2, NULL, V_OPAQUE);
    CHECK(screens[0].pixels[0] == 0 && screens[0].pixels[1] == 2);

    /* The right and bottom edges clip without touching other rows. */
    mark_screen();
    V_DrawBlock((ivec2_t){W - 1, W - 1}, block, (isize2_t){2, 2}, 2, NULL, V_OPAQUE);
    CHECK(screens[0].pixels[W * W - 1] == 0);
    CHECK(screens[0].pixels[W * W - 2] == MARKER && screens[0].pixels[0] == MARKER);
    V_DrawBlock((ivec2_t){-1, -1}, block, (isize2_t){2, 2}, 2, NULL, V_OPAQUE);
    CHECK(screens[0].pixels[0] == 3 && screens[0].pixels[1] == MARKER);

    mark_screen();
    V_DrawBlock((ivec2_t){0, 0}, block, (isize2_t){2, 2}, 2, NULL, V_FLIP_X | V_FLIP_Y);
    CHECK(screens[0].pixels[0] == 3 && screens[0].pixels[1] == MARKER);
    CHECK(screens[0].pixels[W] == 2 && screens[0].pixels[W + 1] == MARKER);

    V_SetClip((irect_t){1, 0, 1, 2});
    mark_screen();
    V_DrawBlock((ivec2_t){0, 0}, block, (isize2_t){2, 2}, 2, NULL, V_OPAQUE);
    V_SetClip((irect_t){0});
    CHECK(screens[0].pixels[0] == MARKER && screens[0].pixels[1] == 2);
    CHECK(screens[0].pixels[W] == MARKER && screens[0].pixels[W + 1] == 3);

    /* Transparency is decided before the remap: a remap may land on index 0. */
    uint8_t to_zero[256] = {0};
    to_zero[0] = 9;
    mark_screen();
    V_DrawBlock((ivec2_t){0, 0}, block, (isize2_t){2, 2}, 2, to_zero, 0);
    CHECK(screens[0].pixels[0] == MARKER && screens[0].pixels[1] == 0);

    /* Nearest-neighbour scaling, flipped: {0,2} becomes 2,2,-,-. */
    mark_screen();
    V_DrawBlockScaled((irect_t){0, 0, 4, 1}, block, (isize2_t){2, 1}, 2, NULL, V_FLIP_X);
    CHECK(screens[0].pixels[0] == 2 && screens[0].pixels[1] == 2);
    CHECK(screens[0].pixels[2] == MARKER && screens[0].pixels[3] == MARKER);

    uint8_t table[65536];
    memset(table, 0, sizeof(table));
    table[(2 << 8) | 4] = 9;
    screens[0].pixels[0] = 4;
    screens[0].pixels[1] = 4;
    uint8_t src[2] = {0, 2};
    V_DrawBlockTranslucent((ivec2_t){0, 0}, src, (isize2_t){2, 1}, 2, table, 0);
    CHECK(screens[0].pixels[0] == 4);
    CHECK(screens[0].pixels[1] == 9);
    return 0;
}

static int remaps(void) {
    uint32_t same[256], other[256];
    for (int i = 0; i < 256; ++i) {
        same[i] = vpalette[i];
        other[i] = 0xff000000u | (unsigned)(255 - i) * 0x010101u;
    }
    const uint8_t *identity = V_RemapPalette(same);
    CHECK(identity[0] == 0 && identity[2] == 2 && identity[3] == 3);
    const uint8_t *mapped = V_RemapPalette(other);
    CHECK(mapped != identity && mapped[0] == 255 && mapped[255] == 0 && mapped[10] == 245);

    /* The nearest-colour cache follows the screen palette. */
    CHECK(V_NearestIndex(0xff0000ffu) != 5);
    uint32_t edited[256];
    memcpy(edited, vpalette, sizeof(edited));
    edited[5] = 0xff0000ffu;
    I_SetPalette(edited);
    CHECK(V_NearestIndex(0xff0000ffu) == 5);
    return 0;
}

static int sprites(void) {
    uint8_t indices[4] = {0, 1, 40, 200};
    spritecell_t cell = {.rect = {0, 0, 4, 1}};
    spritelump_t lump = {.indices = indices};
    spritepalettemap_t team = {.id = 3};
    for (int i = 0; i < 256; ++i) team.indices[i] = (uint8_t)i;
    team.indices[200] = 100;
    spritesheet_t sprite = {.cells = &cell, .lumps = &lump, .numlumps = 1,
                            .palette_maps = &team, .palette_map_count = 1};
    gray_palette(sprite.source_palette);
    I_SetPalette(sprite.source_palette);
    irect_t dst = {0, 0, 4, 1};

    mark_screen();
    CHECK(R_DrawSprite(&sprite, 0, -1, NULL, &dst, 0, 16));
    CHECK(screens[0].pixels[0] == MARKER && screens[0].pixels[1] == 1);
    CHECK(screens[0].pixels[2] == 40 && screens[0].pixels[3] == 200);

    /* A team map is an index translation; an unknown id draws source colours. */
    mark_screen();
    CHECK(R_DrawSprite(&sprite, 0, 3, NULL, &dst, 0, 16));
    CHECK(screens[0].pixels[2] == 40 && screens[0].pixels[3] == 100);
    CHECK(R_DrawSprite(&sprite, 0, 9, NULL, &dst, 0, 16));
    CHECK(screens[0].pixels[3] == 200);

    /* Intensity 8 of 16 scales by 128/255. A pixel that darkens to black is
     * still drawn: only source index 0 is transparent. */
    mark_screen();
    CHECK(R_DrawSprite(&sprite, 0, -1, NULL, &dst, 0, 8));
    CHECK(screens[0].pixels[0] == MARKER && screens[0].pixels[1] == 0);
    CHECK(screens[0].pixels[2] == 40 * 128 / 255 && screens[0].pixels[3] == 200 * 128 / 255);

    /* A source palette the screen lacks is nearest-matched. */
    for (int i = 1; i < 256; ++i)
        sprite.source_palette[i] = 0xff000000u | (unsigned)(255 - i) * 0x010101u;
    mark_screen();
    CHECK(R_DrawSprite(&sprite, 0, -1, NULL, &dst, 0, 16));
    CHECK(screens[0].pixels[1] == 254 && screens[0].pixels[2] == 215 && screens[0].pixels[3] == 55);

    /* Palette alpha: 0 skips the pixel even on an opaque draw, a partial value
     * blends over the destination, and an opaque entry 0 is a real colour. */
    gray_palette(sprite.source_palette);
    sprite.source_palette[1] = 0x00ffffffu;
    sprite.source_palette[40] = 0x80000000u;
    mark_screen();
    CHECK(R_DrawSprite(&sprite, 0, -1, NULL, &dst, V_OPAQUE, 16));
    CHECK(screens[0].pixels[0] == 0 && screens[0].pixels[1] == MARKER);
    CHECK(screens[0].pixels[2] == MARKER * 127 / 255 && screens[0].pixels[3] == 200);
    sprite.source_palette[0] = 0x00000000u;
    mark_screen();
    CHECK(R_DrawSprite(&sprite, 0, -1, NULL, &dst, V_OPAQUE, 16));
    CHECK(screens[0].pixels[0] == MARKER && screens[0].pixels[3] == 200);

    /* Out-of-range frames and crops are rejected. */
    irect_t crop = {2, 0, 3, 1};
    CHECK(!R_DrawSprite(&sprite, 1, -1, NULL, &dst, 0, 16));
    CHECK(!R_DrawSprite(&sprite, 0, -1, &crop, &dst, 0, 16));
    CHECK(!R_DrawSprite(NULL, 0, -1, NULL, &dst, 0, 16));
    return 0;
}

int main(void) {
    uint32_t palette[256];
    gray_palette(palette);
    I_SetPalette(palette);
    V_AllocScreen(W, W);
    CHECK(screens[0].pixels);
    RTS_RUN(blocks());
    RTS_RUN(remaps());
    RTS_RUN(sprites());

    /* Reading back expands indices through the palette with opaque alpha. */
    static uint32_t argb[W * W];
    screens[0].pixels[0] = 9;
    V_ReadPixels(argb, W * 4);
    CHECK(argb[0] == (vpalette[9] | 0xff000000u));

    V_FreeScreen();
    puts("PASS: indexed blits, clipping, remaps, sprite tint/team/alpha and readback");
    return 0;
}
