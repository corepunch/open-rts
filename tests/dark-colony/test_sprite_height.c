#include "game.h"
#include "engine.h"
#include "info.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 2, 128, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(r_renderer);
    app_t app = { .renderer = r_renderer, .win = {2, 128},
        .cell = {32, 32}, .cam = {0, 110} };
    level_t map = {0};
    spritesheet_t sheet = {0};
    assert(R_AllocSpriteCells(&sheet, 1));
    sheet.cells[0].rect = (irect_t){0, 0, 2, 64};
    uint32_t red[128], green[128];
    for (int i = 0; i < 128; ++i) {
        red[i] = 0xffff0000;
        green[i] = 0xff00ff00;
    }
    assert(R_CreateSpriteLumpTexture(r_renderer, &sheet.lumps[0], red, 2,
                                    sheet.cells[0].rect, true, -1));
    assert(R_CreateSpriteLumpTexture(r_renderer, &sheet.lumps[0], green, 2,
                                    sheet.cells[0].rect, true, 1));
    /* Two frames share a texture; only the elevated object's frame is green. */
    assert(R_InitSpriteDef(&sheet, 2, 1));
    assert(R_InstallSpriteLump(&sheet, 0, 0, 0, false));
    assert(R_InstallSpriteLump(&sheet, 1, 0, 0, false));
    sheet.spritedef.spriteframes[1].directions[0].layers[0].remap = 1;
    state_t states[3] = {{0}, {.tics = -1}, {.frame = 1, .tics = -1}};
    gameinfo_t game = {.states = states, .state_count = 3};
    gameinfo = &game;
    mobj_t ground = {.traits = MF_RENDERABLE};
    mobj_t ship = {.traits = MF_RENDERABLE};
    assert(P_SetMobjState(&ground, 1) && P_SetMobjState(&ship, 2));
    ship.core.position = fixed3_from_fvec2((fvec2_t){0, 0}, 50 * FIXED_ONE / g_cell_h);
    float sx, sy, ground_sx, ground_sy;
    R_MapPositionToScreen(&app, &map, ship.core.position, &sx, &sy);
    R_MapPositionToScreen(&app, &map, ground.core.position, &ground_sx, &ground_sy);
    assert(sx == ground_sx && ground_sy - sy == 50);
    app.cell = (isize2_t){64, 64};
    R_MapPositionToScreen(&app, &map, ship.core.position, &sx, &sy);
    assert(ground_sy - sy == 100);
    app.cell = (isize2_t){32, 32};
    /* Both ground-Y orders and both input orders must leave the ship on top. */
    for (int y = -1; y <= 1; y += 2) {
        ground.core.position = fixed3_from_fvec2((fvec2_t){0, y * 0.125f}, 0);
        for (int reverse = 0; reverse < 2; ++reverse) {
            mobj_t *units[2] = {reverse ? &ship : &ground, reverse ? &ground : &ship};
            SDL_SetRenderDrawColor(r_renderer, 0, 0, 0, 255);
            SDL_RenderClear(r_renderer);
            R_RenderPlayerView(&app, &map, NULL, units, 2, &sheet, NULL, &game, 0);
            uint32_t pixels[256];
            assert(SDL_RenderReadPixels(r_renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                        pixels, 2 * sizeof(*pixels)) == 0);
            assert(pixels[55 * 2] == 0xff00ff00);
            assert(pixels[90 * 2] == 0xffff0000);
        }
    }
    R_FreeSprite(&sheet);
    SDL_DestroyRenderer(r_renderer);
    r_renderer = NULL;
    SDL_FreeSurface(surface);
    puts("PASS: Z raises sprites with scale and sorts them above ground objects");
    return 0;
}
