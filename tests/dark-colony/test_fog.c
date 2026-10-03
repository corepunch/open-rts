#include "t_local.h"
#include "engine.h"
#include "info.h"
#include "gamestat.h"
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

static void idle(mobj_t *actor) { (void)actor; }

static void check_refresh(void) {
    P_InitThinkers();
    level.daylight = (daylight_t){0};
    clear_sight();
    mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){24.5f, 24.5f}, 0), MT_TROOPER);
    mobj_t *base = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){4.5f, 4.5f}, 0), MT_EXCOPOD);
    assert(unit && base);
    unit->thinker.function = base->thinker.function = idle;
    level.exo_income[0] = 17;
    level.player_resources[0][0] = 100;
    P_Ticker();
    assert(sight(38, 24) & PLAYER);
    unit->core.position = fixed3_from_fvec2((fvec2_t){25.5f, 24.5f}, 0);
    P_Ticker();
    assert(sight(39, 24) & PLAYER);
    assert(sight(10, 24) == SIGHT_EXPLORED);
    while (leveltime < 64) {
        P_Ticker();
        int clock = leveltime * 1000 / (WORLD_CLOCK_MS * RTS_TICRATE);
        assert(level.player_resources[0][0] == 100 + clock / 16 * 17);
    }
    P_FreeThinkers();
}

static int reference_corner(ivec2_t corner) {
    int sum = 0;
    for (int y = corner.y - 1; y <= corner.y; ++y)
        for (int x = corner.x - 1; x <= corner.x; ++x) {
            ivec2_t cell = {x < 0 ? 0 : x >= level.width ? level.width - 1 : x,
                           y < 0 ? 0 : y >= level.height ? level.height - 1 : y};
            cell.y = L_ScreenY(&level, cell.y);
            sum += P_SightBrightness(&level, cell);
        }
    return sum >> 2;
}

static void check_render_equivalence(void) {
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i) palette[i] = 0xff000000u | (unsigned)i * 0x010101u;
    I_SetPalette(palette);
    V_AllocScreen(640, 128);
    for (int y = 0; y < level.height; ++y)
        for (int x = 0; x < level.width; ++x)
            level.sight.cells[L_Index(&level, x, y)] = (x + 2 * y) % 3 == 0 ? 0 :
                (x + 2 * y) % 3 == 1 ? SIGHT_EXPLORED : PLAYER | SIGHT_EXPLORED;
    const isize2_t sizes[] = {{16, 16}, {23, 37}, {32, 32}, {48, 48}};
    const fvec2_t cameras[] = {{0, 0}, {-15.75f, -51.25f}, {7.5f, 3.75f}, {-1100, -1100}};
    for (unsigned s = 0; s < sizeof(sizes) / sizeof(*sizes); ++s)
        for (unsigned c = 0; c < sizeof(cameras) / sizeof(*cameras); ++c) {
            app_t app = {.win = {640, 128}, .cell = sizes[s], .cam = cameras[c]};
            ivec2_t first = {(int)floorf(-app.cam.x / app.cell.w), (int)floorf(-app.cam.y / app.cell.h)};
            ivec2_t start = {(int)(first.x * app.cell.w + app.cam.x), (int)(first.y * app.cell.h + app.cam.y)};
            for (int y = 0; y < 128; ++y)
                for (int x = 0; x < 640; ++x) screens[0].pixels[y * 640 + x] = (uint8_t)(x * 17 + y * 31);
            R_DrawFog(&app, &level);
            for (int y = 0; y < 128; ++y)
                for (int x = 0; x < 640; ++x) {
                    int expected = (uint8_t)(x * 17 + y * 31);
                    if (x < G_WorldViewportWidth(&app) && x >= start.x && y >= start.y) {
                        ivec2_t source = {(x - start.x) * 32 / app.cell.w, (y - start.y) * 32 / app.cell.h};
                        ivec2_t cell = ivec2_add(first, (ivec2_t){source.x / 32, source.y / 32});
                        int corners[4] = {0};
                        if (L_Contains(&level, cell.x, cell.y)) {
                            corners[0] = reference_corner(cell);
                            corners[1] = reference_corner(ivec2_add(cell, (ivec2_t){1, 0}));
                            corners[2] = reference_corner(ivec2_add(cell, (ivec2_t){0, 1}));
                            corners[3] = reference_corner(ivec2_add(cell, (ivec2_t){1, 1}));
                        }
                        int light = R_FogSample(corners, (ivec2_t){source.x % 32, source.y % 32}) * 255 / 16;
                        expected = expected * light / 255;
                    }
                    assert(screens[0].pixels[y * 640 + x] == expected);
                }
        }
    clear_sight();
    palette[0] = 0xffff0000u;
    palette[17] = 0xff000000u;
    I_SetPalette(palette);
    app_t app = {.win = {640, 128}, .cell = {32, 32}};
    V_BeginFrame(0xffffffffu);
    R_DrawFog(&app, &level);
    assert(screens[0].pixels[0] == 17); /* Black need not be palette index zero. */
    V_FreeScreen();
}

static void check_detection(void) {
    clear_sight();
    mobj_t *detector = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){24.5f, 24.5f}, 0), MT_TROOPER);
    mobj_t *mine = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){26.5f, 24.5f}, 0), MT_HUMAN_MINE);
    assert(detector && mine);
    detector->traits |= MF_DETECTOR;
    mine->team = mine->owner = 1;
    int near = L_Index(&level, 26, 24);
    level.tile_flags[near] = MAP_SIGHT_PASS | MAP_SIGHT_NEAR;
    P_UpdateSight();
    assert(mine->detected_by == PLAYER); /* Detection still visits near-only terrain. */
    assert(!P_VisibleToPlayer(mine));
    level.tile_flags[near] = MAP_SIGHT_PASS;
    P_UpdateSight();
    assert(P_VisibleToPlayer(mine));
    level.tile_flags[L_Index(&level, 25, 24)] = 0;
    P_UpdateSight();
    assert(!mine->detected_by && !P_VisibleToPlayer(mine));
    detector->traits |= MF_FLY;
    P_UpdateSight();
    assert(mine->detected_by == PLAYER && P_VisibleToPlayer(mine));
    detector->hp = 0;
    P_UpdateSight();
    assert(!mine->detected_by);
    level.tile_flags[L_Index(&level, 25, 24)] = MAP_SIGHT_PASS;
    P_FreeThinkers();
}

static void check_human02(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = "SCENARIO/HUMAN/HUMAN02.MAP"};
    assert(model && rts_game_model_load(model, &config));
    int visible = 0;
    for (int i = 0; i < level.width * level.height; ++i)
        visible += !!(level.sight.cells[i] & level.sight.allies[consoleplayer]);
    assert(visible == 1283);
    assert(P_SightBrightness(&level, (ivec2_t){54, 55}) == 16); /* Exo-Ctr. */
    assert(P_SightBrightness(&level, (ivec2_t){64, 52}) == 16); /* Landing beacon. */
    int credits = level.player_resources[0][0], day_tics = level.daylight.tics;
    assert(level.exo_income[0] == 3);
    for (int tick = 1; tick <= 120; ++tick) {
        assert(rts_game_model_tick(model, FIXED_DT));
        int clock = tick * 1000 / (WORLD_CLOCK_MS * RTS_TICRATE);
        assert(leveltime == tick);
        assert(level.player_resources[0][0] == credits + (clock / 16) * 3);
        assert(level.daylight.tics == day_tics + clock);
        assert(P_SightBrightness(&level, (ivec2_t){54, 55}) == 16);
    }
    rts_game_model_destroy(model);
}

static void benchmark(void) {
    if (!getenv("OPEN_RTS_BENCH_FOG")) return;
    P_InitThinkers();
    level.width = level.height = 128;
    assert(P_InitSight());
    mobjtype_t observer = {.sight = {10, 10, false}};
    for (int i = 0; i < 800; ++i) {
        mobj_t *actor = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){20 + i % 80, 20 + i / 80}, 0), MT_TROOPER);
        assert(actor);
        actor->info = &observer;
    }
    uint64_t start = SDL_GetPerformanceCounter();
    for (int i = 0; i < 200; ++i) P_UpdateSight();
    double sight_ms = (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency() / 200;
    observer.sight.day = observer.sight.night = 5;
    start = SDL_GetPerformanceCounter();
    for (int i = 0; i < 200; ++i) P_UpdateSight();
    double same_area_ms = (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency() / 200;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        ((mobj_t *)th)->traits |= MF_DETECTOR;
    start = SDL_GetPerformanceCounter();
    for (int i = 0; i < 200; ++i) P_UpdateSight();
    double detector_ms = (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency() / 200;
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i) palette[i] = 0xff000000u | (unsigned)i * 0x010101u;
    I_SetPalette(palette);
    V_AllocScreen(1920, 1080);
    app_t app = {.win = {1920, 1080}, .cell = {32, 32}};
    for (int y = 0; y < level.height; ++y)
        for (int x = 0; x < level.width; ++x)
            level.sight.cells[L_Index(&level, x, y)] = x < 12 ? PLAYER | SIGHT_EXPLORED : x < 24 ? SIGHT_EXPLORED : 0;
    V_BeginFrame(0xffffffffu);
    R_DrawFog(&app, &level); /* Warm palette lookup tables. */
    start = SDL_GetPerformanceCounter();
    for (int i = 0; i < 200; ++i) {
        memset(screens[0].pixels, 255, (size_t)screens[0].w * screens[0].h);
        R_DrawFog(&app, &level);
    }
    double draw_ms = (SDL_GetPerformanceCounter() - start) * 1000.0 / SDL_GetPerformanceFrequency() / 200;
    printf("fog benchmark: 800 radius-20 observers %.3f ms/pass; radius-10 %.3f ms/pass; radius-10 detectors %.3f ms/pass; 1920x1080 draw %.3f ms/frame\n",
           sight_ms, same_area_ms, detector_ms, draw_ms);
    V_FreeScreen();
    P_FreeLevel(&level);
}

static void check_render(void) {
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i)
        palette[i] = 0xff000000u | (unsigned)i * 0x010101u;
    palette[1] = 0xff00ff00u;
    palette[2] = 0xffff0000u;
    I_SetPalette(palette);
    V_AllocScreen(640, 128);
    app_t app = {.win = {640, 128}, .cell = {32, 32}};
    uint32_t pixels[640 * 128];
    clear_sight();
    for (int y = 0; y < level.height; ++y)
        level.sight.cells[L_Index(&level, 0, y)] = PLAYER | SIGHT_EXPLORED;
    V_BeginFrame(0xffffffffu);
    R_DrawFog(&app, &level);
    V_ReadPixels(pixels, 640 * 4);
    assert((pixels[32 * 640] & 255) == 255);
    assert((pixels[32 * 640 + 31] & 255) == 127);
    assert((pixels[32 * 640 + 32] & 255) == 127);
    assert((pixels[32 * 640 + 63] & 255) == 0);
    assert((pixels[32 * 640 + 600] & 255) == 255); /* HUD is outside fog. */
    app.cam = (fvec2_t){-16, 0};
    V_BeginFrame(0xffffffffu);
    R_DrawFog(&app, &level);
    V_ReadPixels(pixels, 640 * 4);
    assert((pixels[32 * 640 + 15] & 255) == 127);
    assert((pixels[32 * 640 + 47] & 255) == 0);
    /* Scene rows use the map's bottom-up coordinates; a bright bottom row
     * must not reveal the top of the map after camera movement. */
    clear_sight();
    for (int x = 0; x < level.width; ++x)
        level.sight.cells[L_Index(&level, x, 0)] = PLAYER | SIGHT_EXPLORED;
    app.cam = (fvec2_t){0, -(level.height - 2) * 32};
    V_BeginFrame(0xffffffffu);
    R_DrawFog(&app, &level);
    V_ReadPixels(pixels, 640 * 4);
    assert((pixels[0] & 255) == 0);
    assert((pixels[63 * 640] & 255) == 255);
    /* A cell rectangle starts at its top edge, whereas world positions use
     * the bottom-up point transform. Terrain and fog must cover the same row. */
    uint32_t colors[] = {0xffff0000, 0xff00ff00};
    level_t rows = {.width = 1, .height = 2, .cell_colors = colors,
                    .render_capabilities = MAP_RENDER_CAP_CELL_COLORS};
    tileset_t tiles = {0};
    app.cam = (fvec2_t){0, 0};
    V_BeginFrame(0xff000000u);
    R_DrawLevel(&app, &rows, &tiles);
    V_ReadPixels(pixels, 640 * 4);
    assert(pixels[0] == 0xff00ff00 && pixels[32 * 640] == 0xffff0000);
    /* DC.EXE 0x40a7b3: terrain reads light-selector row night_weight*7>>8. */
    uint8_t texels[32 * 32];
    memset(texels, 5, sizeof(texels));
    uint16_t ids[] = {0};
    level_t ground = {.width = 1, .height = 1, .tile_ids = ids};
    tileset_t night = {.indices = texels, .count = 1, .tile_w = 32, .tile_h = 32,
                       .light_row_count = 8};
    memcpy(night.palette, palette, sizeof(palette));
    for (int row = 0; row < 8; ++row) {
        for (int i = 0; i < 256; ++i) night.light_rows[row][i] = (uint8_t)i;
        night.light_rows[row][5] = (uint8_t)(10 + row);
    }
    const struct { int weight, index; } shades[] = {{0, 10}, {36, 10}, {37, 11}, {128, 13}, {256, 17}};
    for (unsigned i = 0; i < sizeof(shades) / sizeof(*shades); ++i) {
        ground.daylight.weight = shades[i].weight;
        V_BeginFrame(0xff000000u);
        R_DrawLevel(&app, &ground, &night);
        assert(screens[0].pixels[0] == shades[i].index);
        assert(screens[0].pixels[31 * screens[0].w + 31] == shades[i].index);
    }
    V_FreeScreen();
}

int main(void) {
    for (int i = 0; i < num_actor_types; ++i) {
        const mobjtype_t *type = &actor_types[i];
        if (!type->sight.day && !type->sight.night) continue;
        assert(type->native_type_id < GAMESTAT_UNIT_COUNT);
        const int *native = dc_gamestat_units[type->native_type_id].values;
        assert(type->sight.day == native[GAMESTAT_UNIT_OBS_DAY]);
        assert(type->sight.night == native[GAMESTAT_UNIT_OBS_NIGHT]);
    }
    P_InitThinkers();
    level.width = level.height = 48;
    level.blocked = calloc(48 * 48, 1);
    level.tile_flags = malloc(48 * 48 * sizeof(uint16_t));
    assert(level.blocked && level.tile_flags && P_InitSight());
    for (int i = 0; i < 48 * 48; ++i) level.tile_flags[i] = MAP_SIGHT_PASS;
    /* Native root table 0x483fbc: exact full-circle counts, not a square or
     * Manhattan-distance approximation. */
    const int counts[] = {5, 13, 29, 49, 81, 113, 149, 197, 253, 317};
    for (int radius = 1; radius <= 20; ++radius) {
        clear_sight();
        P_RevealSight((ivec2_t){24, 24}, radius, PLAYER, false);
        int count = 0;
        for (int y = 0; y < 48; ++y) {
            for (int x = 0; x < 48; ++x) {
                bool expected = (x - 24) * (x - 24) + (y - 24) * (y - 24) <= radius * radius;
                assert((sight(x, y) != 0) == expected);
                count += sight(x, y) != 0;
            }
        }
        if (radius <= 10) assert(count == counts[radius - 1]);
        if (radius == 20) assert(count == 1257);
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
    mobj_t *enemy = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){20.5f, 8.5f}, 0), MT_GREY);
    assert(unit && enemy);
    enemy->team = enemy->owner = 1;
    enemy->allegiance = ALLEGIANCE_ENEMY;
    clear_sight();
    level.daylight.weight = 0;
    P_UpdateSight();
    assert(P_VisibleToPlayer(enemy));
    assert(P_VisibleTo(unit, enemy));
    assert(unit->info->sight.day == 7 && unit->info->sight.night == 4);
    assert(enemy->info->sight.day == 4 && enemy->info->sight.night == 7);
    assert(!P_VisibleTo(enemy, unit)); /* Engine doubles Grey's four to eight. */
    level.daylight.weight = 256;
    P_UpdateSight();
    assert(!P_VisibleToPlayer(enemy));
    assert(!P_VisibleTo(unit, enemy));
    assert(P_VisibleTo(enemy, unit)); /* Engine doubles seven to fourteen. */
    assert(P_SightBrightness(&level, (ivec2_t){20, 8}) == 10);
    enemy->hp = 0;
    unit->hp = 0;
    P_UpdateSight();
    assert(P_SightBrightness(&level, (ivec2_t){8, 8}) == 10);
    P_FreeThinkers();
    check_refresh();
    check_detection();
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
    check_render_equivalence();
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
            /* The PTH family byte may block further cells; it never frees an obstacle. */
            assert(!(flags & 512) || level.blocked[dst]);
            assert(level.tile_ids[dst] == read_u16_le(map.bytes + 8 + src * 4));
        }
    }
    W_FreeFile(&map);
    P_FreeLevel(&level);
    check_human02();
    benchmark();
    puts("fog: native circles, pruning, near-only terrain, flying sight, teams, exploration, day/night and interpolation OK");
    return 0;
}
