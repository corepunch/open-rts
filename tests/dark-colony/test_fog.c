#include "mobj_test.h"
#include "engine.h"
#include "p_local.h"
#include "info.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PLAYER UINT32_C(0x40000000)
#define ENEMY UINT32_C(0x20000000)

static void clear_sight(void) {
    memset(level.sight.cells, 0, (size_t)level.width * level.height * sizeof(uint32_t));
}

static uint32_t sight(int x, int y) {
    return level.sight.cells[L_Index(&level, x, y)];
}

static void clock_tick(void) {
    int before = level.daylight.tics;
    while (level.daylight.tics == before) P_Ticker();
}

static void check_render(void) {
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 128, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    assert(renderer);
    app_t app = {.renderer = renderer, .win = {640, 128}, .cell = {32, 32}};
    clear_sight();
    for (int y = 0; y < level.height; ++y)
        level.sight.cells[L_Index(&level, 0, y)] = PLAYER | SIGHT_EXPLORED;
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
    R_DrawFog(&app, &level);
    uint32_t pixels[640 * 128];
    assert(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, 640 * 4) == 0);
    assert((pixels[32 * 640] & 255) == 255);
    assert((pixels[32 * 640 + 31] & 255) == 127);
    assert((pixels[32 * 640 + 32] & 255) == 127);
    assert((pixels[32 * 640 + 63] & 255) == 0);
    assert((pixels[32 * 640 + 600] & 255) == 255); /* HUD is outside fog. */
    app.cam = (fvec2_t){-16, 0};
    SDL_RenderClear(renderer);
    R_DrawFog(&app, &level);
    assert(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, 640 * 4) == 0);
    assert((pixels[32 * 640 + 15] & 255) == 127);
    assert((pixels[32 * 640 + 47] & 255) == 0);
    /* Scene rows use the map's bottom-up coordinates; a bright bottom row
     * must not reveal the top of the map after camera movement. */
    clear_sight();
    for (int x = 0; x < level.width; ++x)
        level.sight.cells[L_Index(&level, x, 0)] = PLAYER | SIGHT_EXPLORED;
    app.cam = (fvec2_t){0, -30 * 32};
    SDL_RenderClear(renderer);
    R_DrawFog(&app, &level);
    assert(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, 640 * 4) == 0);
    assert((pixels[0] & 255) == 0);
    assert((pixels[63 * 640] & 255) == 255);
    /* A cell rectangle starts at its top edge, whereas world positions use
     * the bottom-up point transform. Terrain and fog must cover the same row. */
    uint32_t colors[] = {0xffff0000, 0xff00ff00};
    level_t rows = {.width = 1, .height = 2, .cell_colors = colors,
                    .render_capabilities = MAP_RENDER_CAP_CELL_COLORS};
    tileset_t tiles = {0};
    app.cam = (fvec2_t){0, 0};
    SDL_RenderClear(renderer);
    R_DrawLevel(&app, &rows, &tiles);
    assert(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, 640 * 4) == 0);
    assert(pixels[0] == 0xff00ff00 && pixels[32 * 640] == 0xffff0000);
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    SDL_Quit();
}

int main(void) {
    P_InitThinkers();
    level.width = level.height = 32;
    level.blocked = calloc(32 * 32, 1);
    level.tile_flags = malloc(32 * 32 * sizeof(uint16_t));
    assert(level.blocked && level.tile_flags && P_InitSight());
    for (int i = 0; i < 32 * 32; ++i) level.tile_flags[i] = MAP_SIGHT_PASS;
    /* Native root table 0x483fbc: exact full-circle counts, not a square or
     * Manhattan-distance approximation. */
    const int counts[] = {5, 13, 29, 49, 81, 113, 149, 197, 253, 317};
    for (int radius = 1; radius <= 10; ++radius) {
        clear_sight();
        P_RevealSight((ivec2_t){16, 16}, radius, PLAYER, false);
        int count = 0;
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                bool expected = (x - 16) * (x - 16) + (y - 16) * (y - 16) <= radius * radius;
                assert((sight(x, y) != 0) == expected);
                count += sight(x, y) != 0;
            }
        }
        assert(count == counts[radius - 1]);
    }
    clear_sight();
    level.tile_flags[L_Index(&level, 17, 16)] = 0;
    P_RevealSight((ivec2_t){16, 16}, 4, PLAYER, false);
    assert(sight(17, 16) == (PLAYER | SIGHT_EXPLORED));
    assert(sight(18, 16) == 0);
    assert(sight(16, 20) == (PLAYER | SIGHT_EXPLORED));
    P_RevealSight((ivec2_t){16, 16}, 4, PLAYER, true);
    assert(sight(18, 16) == (PLAYER | SIGHT_EXPLORED));
    level.tile_flags[L_Index(&level, 17, 16)] = MAP_SIGHT_PASS;
    level.tile_flags[L_Index(&level, 18, 16)] = MAP_SIGHT_PASS | MAP_SIGHT_NEAR;
    clear_sight();
    P_RevealSight((ivec2_t){16, 16}, 4, PLAYER, false);
    assert(sight(18, 16) == SIGHT_EXPLORED);
    assert(P_SightBrightness(&level, (ivec2_t){18, 16}) == 10);
    assert(sight(19, 16) == (PLAYER | SIGHT_EXPLORED));
    P_UpdateSight();
    assert(sight(19, 16) == SIGHT_EXPLORED);
    clear_sight();
    P_RevealSight((ivec2_t){0, 0}, 2, PLAYER, false);
    assert(sight(0, 2) == (PLAYER | SIGHT_EXPLORED));
    assert(sight(2, 0) == (PLAYER | SIGHT_EXPLORED));
    assert(!sight(31, 31));
    clear_sight();
    P_RevealSight((ivec2_t){16, 16}, 2, ENEMY, false);
    assert(sight(16, 16) == ENEMY);
    assert(P_SightBrightness(&level, (ivec2_t){16, 16}) == 0);
    level.sight.allies[0] |= ENEMY;
    P_RevealSight((ivec2_t){16, 16}, 2, ENEMY, false);
    assert(P_SightBrightness(&level, (ivec2_t){16, 16}) == 16);
    level.sight.allies[0] = PLAYER;

    mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){8.5f, 8.5f}, 0), MT_TROOPER);
    mobj_t *enemy = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){14.5f, 8.5f}, 0), MT_GREY);
    assert(unit && enemy);
    enemy->team = enemy->owner = 1;
    enemy->allegiance = ALLEGIANCE_ENEMY;
    clear_sight();
    level.daylight.weight = 0;
    P_UpdateSight();
    assert(P_VisibleToPlayer(enemy));
    assert(P_VisibleTo(unit, enemy));
    assert(!P_VisibleTo(enemy, unit)); /* Grey's daytime radius is four. */
    level.daylight.weight = 256;
    P_UpdateSight();
    assert(!P_VisibleToPlayer(enemy));
    assert(!P_VisibleTo(unit, enemy));
    assert(P_VisibleTo(enemy, unit)); /* Its nighttime radius is seven. */
    assert(P_SightBrightness(&level, (ivec2_t){14, 8}) == 10);
    enemy->hp = 0;
    unit->hp = 0;
    P_UpdateSight();
    assert(P_SightBrightness(&level, (ivec2_t){8, 8}) == 10);
    P_FreeThinkers();
    level.daylight = (daylight_t){.phase = 1, .duration = 4, .transition = 2};
    P_Ticker(); assert(level.daylight.tics == 0); /* 33 ms is not a native tic. */
    clock_tick(); assert(level.daylight.weight == 128);
    clock_tick(); assert(level.daylight.weight == 256);
    clock_tick(); clock_tick(); clock_tick();
    assert(level.daylight.phase == 0 && level.daylight.weight == 256);
    clock_tick(); assert(level.daylight.weight == 128);
    clock_tick(); assert(level.daylight.weight == 0);

    const int edge[4] = {0, 16, 0, 16};
    assert(R_FogSample(edge, (ivec2_t){0, 0}) == 0);
    assert(R_FogSample(edge, (ivec2_t){15, 12}) == 7);
    assert(R_FogSample(edge, (ivec2_t){16, 12}) == 8);
    assert(R_FogSample(edge, (ivec2_t){31, 31}) == 16);
    const int diagonal[4] = {0, 0, 0, 16};
    assert(R_FogSample(diagonal, (ivec2_t){16, 16}) == 4);
    assert(R_FogSample(diagonal, (ivec2_t){30, 30}) == 14);
    check_render();
    P_FreeLevel(&level);
    assert(!level.sight.cells && !level.tile_flags);
    assert(G_DoLoadLevel("data/DCOLONY/SCENARIO/HUMAN/HUMAN01.MAP", &level));
    assert(level.daylight.phase == 0 && level.daylight.weight == 0);
    assert(level.daylight.duration == 6750 && level.daylight.tics == 1500 &&
           level.daylight.transition == 75);
    blob_t map;
    assert(W_ReadFile("data/DCOLONY/SCENARIO/HUMAN/HUMAN01.MAP", &map));
    size_t count = (size_t)level.width * level.height;
    assert(map.size >= 8 + count * 6);
    for (int y = 0; y < level.height; ++y) {
        for (int x = 0; x < level.width; ++x) {
            size_t src = (size_t)L_ScreenY(&level, y) * level.width + x;
            int dst = L_Index(&level, x, y);
            uint16_t flags = read_u16_le(map.bytes + 8 + count * 4 + src * 2);
            assert(level.tile_flags[dst] == (flags | ((flags & 512) ? 0 : MAP_SIGHT_PASS)));
            assert(level.blocked[dst] == ((flags & 512) != 0));
            assert(level.tile_ids[dst] == read_u16_le(map.bytes + 8 + src * 4));
        }
    }
    W_FreeFile(&map);
    P_FreeLevel(&level);
    puts("fog: native circles, pruning, near-only terrain, flying sight, teams, exploration, day/night and interpolation OK");
    return 0;
}
