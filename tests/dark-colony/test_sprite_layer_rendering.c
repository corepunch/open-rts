#include "engine.h"
#include "info.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static void draw_pixels(app_t *app, const spritesheet_t *sheet, const gameinfo_t *game,
                         mobj_t *unit, uint32_t pixels[2]) {
    SDL_SetRenderDrawColor(app->renderer, 70, 80, 90, 255);
    SDL_RenderClear(app->renderer);
    const level_t map = {0};
    R_RenderPlayerView(app, &map, NULL, unit, 1, sheet, NULL, game, 0);
    CHECK(SDL_RenderReadPixels(app->renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                              pixels, 2 * sizeof(*pixels)) == 0);
}

int main(void) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 2, 1, 32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(r_renderer);
    app_t app = { .renderer = r_renderer, .win = { 2, 1 } };
    spritesheet_t sheet = {0};
    CHECK(R_AllocSpriteCells(&sheet, 1));
    sheet.cells[0].rect = (irect_t){ 0, 0, 2, 1 };
    const uint32_t colors[2] = { 0xff123456, 0xffabcdef };
    const uint32_t remapped[2] = { 0xff804020, 0xff204080 };
    CHECK(R_CreateSpriteLumpTexture(r_renderer, &sheet.lumps[0], colors, 2,
                                   sheet.cells[0].rect, true, -1));
    CHECK(R_CreateSpriteLumpTexture(r_renderer, &sheet.lumps[0], remapped, 2,
                                   sheet.cells[0].rect, true, 1));
    CHECK(R_InitSpriteDef(&sheet, 1, 1) && R_InstallSpriteLump(&sheet, 0, 0, 0, false));
    spritelayer_t *part = sheet.spritedef.spriteframes[0].directions[0].layers;
    part->offset = (ivec2_t){ 0, 1 };
    part->layer = 3;
    state_t states[2] = { {0}, { .tics = -1 } };
    gameinfo_t game = { .states = states, .state_count = 2 };
    statecontext_t ctx = { .game_info = &game };
    mobj_t unit = { .traits = MF_RENDERABLE };
    unit.core.render_flags = UINT32_MAX;
    CHECK(P_SetMobjState(&ctx, &unit, 1));
    CHECK(unit.core.render_flags == 0);

    uint32_t pixels[2], baseline[2];
    draw_pixels(&app, &sheet, &game, &unit, baseline);
    CHECK(!memcmp(baseline, colors, sizeof(colors)));
    /* Native layer rendering must ignore every actor-level flag, palette
     * override and intensity; those are not this command's metadata. */
    unit.core.render_flags = UINT32_MAX;
    unit.core.render_remap = 1;
    unit.core.render_intensity = 1;
    draw_pixels(&app, &sheet, &game, &unit, pixels);
    CHECK(!memcmp(pixels, baseline, sizeof(pixels)));

    part->flags = RTS_FRAME_FLIP_X;
    part->remap = 1;
    draw_pixels(&app, &sheet, &game, &unit, pixels);
    CHECK(pixels[0] == remapped[1] && pixels[1] == remapped[0]);
    part->intensity = 8;
    draw_pixels(&app, &sheet, &game, &unit, pixels);
    CHECK(pixels[0] == 0xff102040 && pixels[1] == 0xff402010);

    R_FreeSprite(&sheet);
    SDL_DestroyRenderer(r_renderer);
    r_renderer = NULL;
    SDL_FreeSurface(surface);
    puts("PASS: sprite layers own flags, remap and intensity; selector 3 gets no forced tint");
    return 0;
}
