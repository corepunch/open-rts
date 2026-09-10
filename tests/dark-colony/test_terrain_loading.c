#include "engine.h"
#include <assert.h>
#include <inttypes.h>

extern bool load_dark_colony_tileset(const char *path, tileset_t *out);

/* Captured from 86ef7e8's ARGB terrain path, including existing water selection,
 * all seven 180-ms phases and all four map flips. These preserve engine output;
 * they do not establish the retail game's palette-cycling rules. */
static const struct {
    const char *path;
    uint64_t hash;
} catalog[] = {
    {"data/DCOLONY/SCENARIO/ATLANTIS.BTS", UINT64_C(0xee50712fbf3656f5)},
    {"data/DCOLONY/SCENARIO/DESERT.BTS", UINT64_C(0x312747fd33317745)},
    {"data/DCOLONY/SCENARIO/HTRAIN.BTS", UINT64_C(0x4bc6a3bf88668145)},
    {"data/DCOLONY/SCENARIO/JUNGLE.BTS", UINT64_C(0xa10a942c4125835d)},
};

static void clear(SDL_Renderer *renderer) {
    SDL_SetRenderDrawColor(renderer, 23, 42, 61, 255);
    SDL_RenderClear(renderer);
}

static void check_live_palette(app_t *app) {
    uint8_t indices[] = {1, 0, 2, 3};
    tileset_t tileset = {.indices = indices, .tile_w = 2, .tile_h = 2, .count = 1};
    tileset.palette[1] = 0xffff0000;
    tileset.palette[2] = 0xff00ff00;
    tileset.palette[3] = 0xff0000ff;
    irect_t src = {0,0,2,2}, dst = {0,0,2,2};
    uint32_t pixels[4];
    clear(app->renderer);
    R_DrawTile(app, &tileset, 0, src, dst);
    assert(!SDL_RenderReadPixels(app->renderer, &dst, SDL_PIXELFORMAT_ARGB8888, pixels, 8));
    assert(pixels[0] == 0xffff0000 && pixels[1] == 0xff172a3d);
    assert(pixels[2] == 0xff00ff00 && pixels[3] == 0xff0000ff);
    tileset.palette[1] = 0xff123456;
    R_DrawTile(app, &tileset, 0, src, dst);
    assert(!SDL_RenderReadPixels(app->renderer, &dst, SDL_PIXELFORMAT_ARGB8888, pixels, 8));
    assert(pixels[0] == 0xff123456 && !tileset.texture);
    assert(!memcmp(indices, (uint8_t[]){1,0,2,3}, sizeof(indices)));
    /* Draw a cropped pixel at a different size, retaining destination clipping. */
    src = (irect_t){0,1,1,1};
    dst = (irect_t){0,0,4,4};
    irect_t clip = {1,1,2,2};
    clear(app->renderer);
    SDL_RenderSetClipRect(app->renderer, &clip);
    R_DrawTile(app, &tileset, 0, src, dst);
    SDL_RenderSetClipRect(app->renderer, NULL);
    uint32_t scaled[16];
    assert(!SDL_RenderReadPixels(app->renderer, &dst, SDL_PIXELFORMAT_ARGB8888, scaled, 16));
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            assert(scaled[y * 4 + x] == (x >= 1 && x < 3 && y >= 1 && y < 3 ?
                                       0xff00ff00 : 0xff172a3d));
}

int main(void) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 32, 32, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *renderer = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
    assert(renderer);
    app_t app = {.renderer = renderer, .win = {32,32}, .cell = {32,32}, .cam = {0,-32}};
    check_live_palette(&app);
    for (size_t f = 0; f < sizeof(catalog) / sizeof(*catalog); ++f) {
        blob_t file;
        tileset_t tileset;
        assert(W_ReadFile(catalog[f].path, &file));
        assert(load_dark_colony_tileset(catalog[f].path, &tileset));
        int count = read_u32_le(file.bytes + 4);
        assert(tileset.count == count && tileset.indices && !tileset.texture);
        assert(!tileset.animations && !tileset.animation_count);
        for (int i = 0; i < count; ++i)
            assert(!memcmp(tileset.indices + (size_t)i * 1024,
                           file.bytes + 776 + (size_t)i * 1028 + 4, 1024));
        uint64_t hash = UINT64_C(14695981039346656037);
        for (int phase = 0; phase < 7; ++phase) {
            app.ticks_ms = phase * 180;
            for (int i = 0; i < count; ++i) {
                uint16_t key = read_u32_le(file.bytes + 776 + (size_t)i * 1028);
                uint8_t flip = 0;
                level_t map = {.width = 1, .height = 1, .tile_ids = &key,
                    .render_capabilities = MAP_RENDER_CAP_TILE_TRANSFORMS,
                    .tile_transforms = {&flip}};
                for (flip = 0; flip < 4; ++flip) {
                    uint32_t pixels[1024];
                    clear(renderer);
                    R_DrawLevel(&app, &map, &tileset);
                    assert(!SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, 128));
                    for (size_t b = 0; b < sizeof(pixels); ++b) {
                        hash ^= ((uint8_t *)pixels)[b];
                        hash *= UINT64_C(1099511628211);
                    }
                }
            }
        }
        printf("%016" PRIx64 " %s\n", hash, catalog[f].path);
        assert(hash == catalog[f].hash);
        R_FreeTileset(&tileset);
        assert(!tileset.indices && !tileset.palette_cycle.tiles && !tileset.count);
        W_FreeFile(&file);
    }
    R_FreeSpriteBuffer();
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    puts("PASS: indexed BTS pixels, live palettes, water phases, flips, crops and clipping");
    return 0;
}
