#include "game.h"
#include "engine.h"
#include "w_spr.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void clear(const spritesheet_t *sprite) {
    I_SetPalette(sprite->palette);
    V_AllocScreen(64, 64);
    memset(screens[0].pixels, 200, 64u * 64u);
}

static void read_screen(uint32_t *pixels) {
    V_ReadPixels(pixels, 64 * 4);
}

int main(void) {
    app_t app = { .win = {64, 64}, .cam = {24, 40}, .cell = {32, 32} };
    spritesheet_t sprite = {0};
    assert(R_AllocSpriteCells(&sprite, 1));
    sprite.cells[0].rect = (irect_t){0, 0, 4, 16};
    sprite.lumps[0].indices = calloc(64, 1);
    assert(sprite.lumps[0].indices);
    uint8_t shadowmap[256];
    for (int i = 0; i < 256; ++i) {
        sprite.palette[i] = 0xff000000u | (unsigned)i * 0x010101u;
        shadowmap[i] = i / 2;
    }
    sprite.shadowmap = shadowmap;
    sprite.source_palette[0] = 0;
    /* A different single pixel in each row exposes row duplication and flip. */
    for (int y = 0; y < 16; ++y) {
        sprite.lumps[0].indices[y * 4 + y % 4] = y + 1;
        sprite.source_palette[y + 1] = 0xffffffff;
    }
    assert(R_InitSpriteDef(&sprite, 1, 1) && R_InstallSpriteLump(&sprite, 0, 0, 0, false));
    /* Instruction-derived source rows: carry after source rows 6 and 12
     * repeats each once. A uniform scale has different rounding here. */
    const int rows[] = {0,1,2,3,4,5,6,6,7,8,9,10,11,12,12,13,14,15};
    uint32_t pixels[64 * 64], expected[64 * 64];
    for (int flip = 0; flip <= 1; ++flip) {
        clear(&sprite);
        assert(R_RenderSpriteShadow(&app, &sprite, 0, (irect_t){24,24,4,16}, flip));
        for (int i = 0; i < 64 * 64; ++i) expected[i] = 0xffc8c8c8;
        for (int y = 0; y < 18; ++y) {
            int x = 15 + y / 2 + (flip ? 4 - rows[y] % 4 : rows[y] % 4);
            expected[(22 + y) * 64 + x] = 0xff646464;
        }
        read_screen(pixels);
        assert(!memcmp(pixels, expected, sizeof(pixels)));
        /* Overlap remaps the destination a second time; source colors don't matter. */
        assert(R_RenderSpriteShadow(&app, &sprite, 0, (irect_t){24,24,4,16}, flip));
        read_screen(pixels);
        for (int i = 0; i < 64 * 64; ++i)
            assert(pixels[i] == (expected[i] == 0xff646464 ? 0xff323232 : expected[i]));
    }
    /* Top clipping restarts the native accumulators after skipping 8 rows.
     * Bottom=8, shadow top=-10, initial source row=floor(10*256/296)=8. */
    clear(&sprite);
    assert(R_RenderSpriteShadow(&app, &sprite, 0, (irect_t){4,-8,4,16}, 0));
    read_screen(pixels);
    const int clipped_rows[] = {8,9,10,11,12,13,14,14};
    for (int i = 0; i < 64 * 64; ++i) expected[i] = 0xffc8c8c8;
    for (int y = 0; y < 8; ++y) expected[y * 64 + y / 2 + clipped_rows[y] % 4] = 0xff646464;
    assert(!memcmp(pixels, expected, sizeof(pixels)));
    /* Side and bottom clipping retain the same projection and source rows. */
    const ivec2_t shifts[] = {{-18,0}, {45,0}, {0,35}};
    for (size_t edge = 0; edge < sizeof(shifts) / sizeof(*shifts); ++edge) {
        ivec2_t shift = shifts[edge];
        clear(&sprite);
        assert(R_RenderSpriteShadow(&app, &sprite, 0,
                                    (irect_t){24 + shift.x,24 + shift.y,4,16}, 0));
        for (int i = 0; i < 64 * 64; ++i) expected[i] = 0xffc8c8c8;
        for (int row = 0; row < 18; ++row) {
            ivec2_t point = ivec2_add((ivec2_t){15 + row / 2 + rows[row] % 4,22 + row}, shift);
            if (point.x >= 0 && point.x < 64 && point.y < 64)
                expected[point.y * 64 + point.x] = 0xff646464;
        }
        read_screen(pixels);
        assert(!memcmp(pixels, expected, sizeof(pixels)));
    }
    /* Palette index zero is an opaque destination color, even though it is
     * transparent when decoding sprite source images. */
    sprite.palette[0] = 0;
    I_SetPalette(sprite.palette);
    memset(screens[0].pixels, 0, 64u * 64u);
    assert(R_RenderSpriteShadow(&app, &sprite, 0, (irect_t){24,24,4,16}, 0));
    read_screen(pixels);
    for (int i = 0; i < 64 * 64; ++i) assert(pixels[i] == 0xff000000);
    /* The FIN dispatcher must suppress the body for layer 2, and keep
     * shadows at ground height when Z raises the body. */
    state_t states[2] = {{0}, {.tics = -1}};
    gameinfo_t game = {.states = states, .state_count = 2,
                      .state_coord_mode = RTS_STATE_COORDS_FIN_TOP_LEFT};
    gameinfo = &game;
    level_t map = {0};
    mobj_t unit = {.traits = MF_RENDERABLE};
    mobj_t *units[] = {&unit};
    assert(P_SetMobjState(&unit, 1));
    spritelayer_t *part = sprite.spritedef.spriteframes[0].directions[0].layers;
    for (int layer = 0; layer <= 3; ++layer) {
        part->layer = layer;
        for (int elevated = 0; elevated <= 1; ++elevated) {
            unit.core.position.z = elevated * FIXED_ONE;
            clear(&sprite);
            R_RenderPlayerView(&app, &map, NULL, units, 1, &sprite, NULL, &game, 0);
            read_screen(pixels);
            assert(pixels[22 * 64 + 15] == (layer == 1 || layer == 2 ? 0xff646464 : 0xffc8c8c8));
            if (!elevated && layer <= 2)
                assert(pixels[24 * 64 + 24] == (layer == 2 ? 0xffc8c8c8 : 0xffffffff));
        }
    }
    R_FreeSprite(&sprite);
    /* Native palette row, shared independently of source SPR palette/team. */
    const char *terrains[] = {"DESERT", "JUNGLE", "ATLANTIS", "HTRAIN"};
    for (size_t i = 0; i < sizeof(terrains) / sizeof(*terrains); ++i) {
        assert(load_render_tables("data/DCOLONY", terrains[i]));
        assert(load_dark_colony_sprite("data/DCOLONY/SPRITES/TRSC.SPR", &sprite, NULL));
        blob_t rmp;
        assert(W_ReadFile(M_va("data/DCOLONY/%s.RMP", terrains[i]), &rmp));
        assert(sprite.shadowmap && !memcmp(sprite.shadowmap, rmp.bytes + 0x4800, 256));
        if (i == 0) {
            dc_fin_t fin;
            assert(DC_LoadFIN("data/DCOLONY/ANIMATE/TRSC.FIN", &fin));
            const dc_fin_label_t *standing = DC_FINLabel(&fin, "TRSCSTAND0");
            assert(standing);
            unit.core.frame = sprite.numlumps + SDL_SwapLE16(standing->start);
            unit.core.position = (fixed3_t){0};
            app.cam = (fvec2_t){40, 56};
            const uint8_t *native_shadowmap = sprite.shadowmap;
            sprite.shadowmap = NULL;
            clear(&sprite);
            R_RenderPlayerView(&app, &map, NULL, units, 1, &sprite, NULL, &game, 0);
            read_screen(expected);
            sprite.shadowmap = native_shadowmap;
            clear(&sprite);
            R_RenderPlayerView(&app, &map, NULL, units, 1, &sprite, NULL, &game, 0);
            read_screen(pixels);
            int changed = 0;
            for (int pixel = 0; pixel < 64 * 64; ++pixel) changed += pixels[pixel] != expected[pixel];
            assert(changed > 0);
            const char *screenshot = getenv("OPEN_RTS_SHADOW_SCREENSHOT");
            if (screenshot) {
                SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
                    0, 64, 64, 32, SDL_PIXELFORMAT_ARGB8888);
                assert(surface);
                V_ReadPixels(surface->pixels, surface->pitch);
                assert(!SDL_SaveBMP(surface, screenshot));
                SDL_FreeSurface(surface);
            }
            DC_FreeFIN(&fin);
        }
        W_FreeFile(&rmp);
        R_FreeSprite(&sprite);
    }
    /* JUNGLE.RMP row 0x48 sends grass index 232 (2% of jungle tile pixels)
     * to team slot 143. The runtime palette shows that slot as dark cyan,
     * never the file placeholder green that would speckle every shadow. */
    assert(load_render_tables("data/DCOLONY", "JUNGLE"));
    assert(load_dark_colony_sprite("data/DCOLONY/SPRITES/TRSC.SPR", &sprite, NULL));
    assert(sprite.shadowmap[232] == 143 && sprite.palette[232] == 0xff1f4b33);
    assert(sprite.palette[143] == 0xff032727 && sprite.source_palette[143] == 0xff032727);
    I_SetPalette(sprite.palette);
    memset(screens[0].pixels, 232, 64u * 64u);
    irect_t cell = sprite.cells[0].rect;
    assert(R_RenderSpriteShadow(&app, &sprite, 0, (irect_t){24, 24, cell.w, cell.h}, 0));
    read_screen(pixels);
    int shaded = 0;
    for (int i = 0; i < 64 * 64; ++i) {
        assert(pixels[i] == 0xff1f4b33 || pixels[i] == 0xff032727);
        shaded += pixels[i] == 0xff032727;
    }
    assert(shaded > 0);
    R_FreeSprite(&sprite);
    R_FreeSpriteBuffer();
    V_FreeScreen();
    puts("PASS: native shadow projection, row carries, reflection, clipping, palette, overlap, FIN dispatch and Z");
    return 0;
}
