#include "v_video.h"
#include "rts_test.h"

#include <string.h>

#define CHECK(c) RTS_CHECK(c, "indexed video", #c)

static void fill_palette(void) {
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i)
        palette[i] = 0xff000000u | (unsigned)i * 0x010101u;
    palette[1] = 0xffff0000u;
    I_SetPalette(palette);
}

int main(void) {
    fill_palette();
    V_AllocScreen(64, 64);
    V_BeginFrame(0xff000000u);
    uint8_t block[4] = {0, 2, 0, 3};
    V_DrawBlock((ivec2_t){0, 0}, block, (isize2_t){2, 2}, 2, NULL, 0);
    CHECK(screens[0].pixels[0] == 0);
    CHECK(screens[0].pixels[1] == 2);
    CHECK(screens[0].pixels[64] == 0);
    CHECK(screens[0].pixels[65] == 3);

    V_BeginFrame(0xff000000u);
    V_DrawBlock((ivec2_t){0, 0}, block, (isize2_t){2, 2}, 2, NULL, V_OPAQUE);
    CHECK(screens[0].pixels[0] == 0);
    CHECK(screens[0].pixels[1] == 2);

    V_BeginFrame(0xff000000u);
    V_DrawBlock((ivec2_t){62, 62}, block, (isize2_t){2, 2}, 2, NULL, V_OPAQUE);
    CHECK(screens[0].pixels[62 * 64 + 62] == 0);
    CHECK(screens[0].pixels[62 * 64 + 63] == 2);
    CHECK(screens[0].pixels[63 * 64 + 62] == 0);
    CHECK(screens[0].pixels[63 * 64 + 63] == 3);
    CHECK(screens[0].pixels[0] == 0);

    V_BeginFrame(0xff000000u);
    V_DrawBlock((ivec2_t){0, 0}, block, (isize2_t){2, 2}, 2, NULL, V_FLIP_X | V_FLIP_Y);
    CHECK(screens[0].pixels[0] == 3);
    CHECK(screens[0].pixels[1] == 0);

    uint32_t same[256], other[256];
    for (int i = 0; i < 256; ++i) {
        same[i] = vpalette[i];
        other[i] = 0xff000000u | (unsigned)(255 - i) * 0x010101u;
    }
    const uint8_t *identity = V_RemapPalette(same);
    CHECK(identity[0] == 0 && identity[2] == 2 && identity[3] == 3);
    const uint8_t *mapped = V_RemapPalette(other);
    CHECK(mapped[0] == 0 && mapped != identity);

    uint8_t table[65536];
    memset(table, 0, sizeof(table));
    table[(2 << 8) | 4] = 9;
    screens[0].pixels[0] = 4;
    screens[0].pixels[1] = 4;
    uint8_t src[2] = {0, 2};
    V_DrawBlockTranslucent((ivec2_t){0, 0}, src, (isize2_t){2, 1}, 2, table, 0);
    CHECK(screens[0].pixels[0] == 4);
    CHECK(screens[0].pixels[1] == 9);

    V_FreeScreen();
    return 0;
}
