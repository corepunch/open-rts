#define _DEFAULT_SOURCE
#include "engine.h"
#include "game.h"
#include "renderer.h"
#include "sb_bar.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const mobjtype_t *actor_type_by_id(uint16_t type_id) {
    const mobjtype_t *types = (const mobjtype_t *)actor_types;
    if (!types) return NULL;
    for (int i = 0; i < num_actor_types; ++i) {
        if (types[i].id == type_id) return &types[i];
    }
    return NULL;
}

static const mobjtype_t *actor_type_for_unit(const mobj_t *unit) {
    const mobjtype_t *type = actor_type_by_id(unit ? unit->type_id : 0);
    if (type) return type;
    const mobjtype_t *types = (const mobjtype_t *)actor_types;
    if (!types || !unit) return NULL;
    for (int i = 0; i < num_actor_types; ++i) {
        const char *sprite = types[i].sprite_name;
        if (sprite && sprite[0] != '\0' && strcasecmp(sprite, unit->core.sprite_name) == 0) {
            return &types[i];
        }
    }
    return num_actor_types > 0 ? &types[0] : NULL;
}

static void apply_actor_defaults(mobj_t *const *units, int count) {
    for (int i = 0; i < count; ++i) {
        P_ApplyActorTypeDefaults(units[i], actor_type_for_unit(units[i]));
        P_InitMobj(gameinfo, units[i]);
    }
}

static bool spawn_debug_enemy_unit(const level_t *map, const app_t *app,
                                   int sx, int sy) {
    if (!map || !app) return false;
    const mobjtype_t *type = actor_type_by_id(g_debug_enemy_type);
    const mobjtype_t *types = (const mobjtype_t *)actor_types;
    if (!type && num_actor_types > 0) type = &types[0];
    if (!type) return false;
    cell_t cell = R_ScreenToMapGrid(app, map, sx, sy);
    if (!L_Contains(map, cell.x, cell.y)) return false;
    mobj_t *unit = P_SpawnMobj(fixed3_zero(), type->id);
    if (!unit) return false;
    unit->core.position = fixed3_from_fvec2(
        fvec2_cell_center((ivec2_t){ cell.x, cell.y }), 0);
    unit->owner = 1;
    unit->team = 1;
    unit->core.angle = direction_to_angle(12, 32, ANG90, true);
    P_ApplyActorTypeDefaults(unit, type);
    P_InitMobj(gameinfo, unit);
    return true;
}

static bool focus_camera_on_first_player_unit(app_t *app, const level_t *map,
                                              mobj_t *const *units, int unit_count) {
    if (!app || !map || !units) return false;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i]->owner != 0 || units[i]->remove || units[i]->hp <= 0) continue;
        float sx = 0.0f, sy = 0.0f;
        fvec2_t position = fixed3_xy_to_fvec2(units[i]->core.position);
        R_MapToScreen(app, map, position.x, position.y, &sx, &sy);
        app->cam.x += (float)app->win.w * 0.5f - sx;
        app->cam.y += (float)app->win.h * 0.5f - sy;
        return true;
    }
    return false;
}

static void focus_camera_on_grid(app_t *app, const level_t *map,
                                 float gx, float gy) {
    if (!app || !map) return;
    float sx = 0.0f, sy = 0.0f;
    R_MapToScreen(app, map, gx, gy, &sx, &sy);
    irect_t viewport = { 0, 0, app->win.w, app->win.h };
    if (gameui) {
        viewport.x = gameui->world_viewport.x * app->win.w / gameui->logical_width;
        viewport.y = gameui->world_viewport.y * app->win.h / gameui->logical_height;
        viewport.w = gameui->world_viewport.w * app->win.w / gameui->logical_width;
        viewport.h = gameui->world_viewport.h * app->win.h / gameui->logical_height;
    }
    app->cam.x += (float)(viewport.x + viewport.w / 2) - sx;
    app->cam.y += (float)(viewport.y + viewport.h / 2) - sy;
}

static bool focus_camera_on_map_start(app_t *app, const level_t *map) {
    if (!app || !map || !map->has_camera) return false;
    focus_camera_on_grid(app, map, map->camera.x, map->camera.y);
    return true;
}

int main(int argc, char **argv) {
    G_InitGame();
    bool check_only = argc > 1 && strcmp(argv[1], "--check") == 0;
    bool screenshot_only = argc > 1 && strcmp(argv[1], "--screenshot") == 0;
    const char *screenshot_path = screenshot_only && argc > 2 ? argv[2] : NULL;
    int arg_base = check_only ? 2 : (screenshot_only ? 3 : 1);
    bool software_renderer = strcmp(g_game_id, "dark-colony") == 0;
    while (argc > arg_base) {
        if (strcmp(argv[arg_base], "--software") == 0) {
            software_renderer = true;
            arg_base += 1;
        } else if (argc > arg_base + 1 && strcmp(argv[arg_base], "--game") == 0) {
            /* --game is ignored: the binary IS the game */
            arg_base += 2;
        } else if (strncmp(argv[arg_base], "--game=", 7) == 0) {
            /* --game=xxx is ignored */
            arg_base += 1;
        } else {
            break;
        }
    }

    const char *data_root = argc > arg_base ? argv[arg_base] : g_game_default_root;
    const char *map_rel_or_abs = argc > arg_base + 1 ? argv[arg_base + 1] : g_game_default_map;
    const char *sprite_name = argc > arg_base + 2 ? argv[arg_base + 2] : g_game_default_sprite;
    char map_path[1024];
    if (map_rel_or_abs[0] == '/') {
        snprintf(map_path, sizeof(map_path), "%s", map_rel_or_abs);
    } else {
        M_PathJoin(map_path, sizeof(map_path), data_root, map_rel_or_abs);
    }

    renderer_t renderer;
    app_t app = { 0 };
    if (gameui) {
        app.win.w = gameui->logical_width;
        app.win.h = gameui->logical_height;
    } else {
        app.win.w = 640;
        app.win.h = 480;
    }
    app.show_grid = false;
    app.running = true;
    if (!renderer_create(&renderer, sdl_renderer_backend(), "open-rts - paletted RTS base",
                             app.win.w, app.win.h,
                             check_only || screenshot_only,
                             check_only || screenshot_only || software_renderer)) {
        return 1;
    }
    app.window = renderer.window;
    app.renderer = renderer.sdl;
    R_RefreshViewport(&app);

    P_InitThinkers();
    if (!G_DoLoadLevel(map_path, &level) || !P_InitSight()) {
        P_FreeLevel(&level);
        renderer_destroy(&renderer);
        return 1;
    }

    tileset_t tileset;
    spritesheet_t unit_sprite;
    memset(&tileset, 0, sizeof(tileset));
    memset(&unit_sprite, 0, sizeof(unit_sprite));
    if (!W_LoadAssets(app.renderer, data_root, &level, sprite_name, &tileset, &unit_sprite)) {
        P_FreeLevel(&level);
        renderer_destroy(&renderer);
        return 1;
    }
    app.cell.w = g_cell_w > 0 ? g_cell_w : (tileset.tile_w > 0 ? tileset.tile_w : CELL_W);
    app.cell.h = g_cell_h > 0 ? g_cell_h : (tileset.tile_h > 0 ? tileset.tile_h : CELL_H);

    P_LoadThings(map_path);
    mobjlist_t objects = P_ListMobjs();
    mobj_t **units = objects.items;
    int unit_count = objects.count;
    if (unit_count <= 0) {
        int cx = level.width / 2;
        int cy = level.height / 2;
        const mobjtype_t *fallback_type = num_actor_types > 0 ? actor_types : NULL;
        for (int i = 0; i < 6; ++i) {
            mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2(fvec2_cell_center(
                (ivec2_t){ cx + i % 3, cy + i / 3 }), 0),
                fallback_type ? fallback_type->id : 0);
            if (!unit) break;
            unit->owner = 0;
            P_MobjSetSelected(unit, i == 0);
            if (!fallback_type) {
                unit->traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE;
                snprintf(unit->core.sprite_name, sizeof(unit->core.sprite_name), "%s", sprite_name);
            }
        }
        P_FreeMobjList(&objects);
        objects = P_ListMobjs();
        units = objects.items;
        unit_count = objects.count;
    }
    apply_actor_defaults(units, unit_count);
    P_UpdateSight();

    spritecache_t decoration_sprites = { 0 };
    if (!R_InitSprites(app.renderer, data_root, &level, units, unit_count,
                              &decoration_sprites)) {
        fprintf(stderr, "warning: some %s runtime sprites were not loaded\n", g_game_name);
    }

    if (!focus_camera_on_map_start(&app, &level)) {
        fvec2_t position = unit_count > 0 ? fixed3_xy_to_fvec2(units[0]->core.position) :
            (fvec2_t){ (float)level.width * 0.5f, (float)level.height * 0.5f };
        float focus_gx = position.x;
        float focus_gy = position.y;
        focus_camera_on_grid(&app, &level, focus_gx, focus_gy);
    }
    R_ClampCamera(&app, &level, G_WorldViewportWidth(&app), app.win.h);

    printf("Loaded %s (%dx%d, tileset %s, %d units, %d level decorations, %d resource vents). Controls: left select/drag, right move/harvest, Alt+left spawn enemy, WASD/arrows pan, G grid, B blocked overlay, Ctrl+A select all, F10 +100 resources.\n",
           map_path, level.width, level.height, level.tileset_name, unit_count,
           level.decoration_count, level.resource_vent_count);

    void *custom_ui = G_InitCustomUI(&app, data_root);
    sb_state_t st = { 0 };
    if (gameui && !SB_Init(&st, app.renderer, data_root, gameui))
        fprintf(stderr, "warning: SB_Init failed for %s\n", g_game_name);
    hudtext_t hud_text = { 0 };
    if (check_only || screenshot_only) {
        if (screenshot_only) {
            app.ticks_ms = SDL_GetTicks();
            if (level.mission) {
                int before_count = unit_count;
                G_MissionTicker(&level, units, &unit_count,
                                &hud_text, FIXED_DT);
                P_FreeMobjList(&objects);
                objects = P_ListMobjs();
                units = objects.items;
                unit_count = objects.count;
                if (unit_count != before_count && !level.has_camera)
                    focus_camera_on_first_player_unit(&app, &level, units, unit_count);
                R_ClampCamera(&app, &level, G_WorldViewportWidth(&app), app.win.h);
            }
            P_UpdateSight();
            renderer_begin_frame(&renderer, (SDL_Color){ 11, 14, 16, 255 });
            R_DrawLevel(&app, &level, &tileset);
            R_RenderPlayerView(&app, &level, &tileset, units,
                                 unit_count, &unit_sprite,
                                 &decoration_sprites, gameinfo, SDL_GetTicks());

            R_DrawGridOverlay(&app, &level);
            R_DrawFog(&app, &level);
            G_CustomUIDrawer(custom_ui, &app, &level, units, unit_count, &decoration_sprites, &hud_text);
            SB_Drawer(&st, &app, &level, units, unit_count, &decoration_sprites,
                      false, true);
            if (renderer_save_screenshot(&renderer, screenshot_path)) {
                printf("Saved screenshot %s.\n", screenshot_path);
            }
        }
        printf("Smoke check OK: %d terrain tiles, %d unit frames from %s, %d resource vents.\n",
               tileset.count, unit_sprite.numlumps, sprite_name, level.resource_vent_count);
        SB_Shutdown(&st);
        G_ShutdownCustomUI(custom_ui);
        R_FreeSpriteCache(&decoration_sprites);
        R_FreeSprite(&unit_sprite);
        R_FreeTileset(&tileset);
        P_FreeMobjList(&objects);
        P_FreeLevel(&level);
        renderer_destroy(&renderer);
        return 0;
    }

    uint64_t prev = SDL_GetPerformanceCounter();
    double freq = (double)SDL_GetPerformanceFrequency();
    float accumulator = 0.0f;
    int title_resources = -1;

    while (app.running) {
        uint64_t now = SDL_GetPerformanceCounter();
        float frame_dt = (float)((double)(now - prev) / freq);
        if (frame_dt > 0.25f) frame_dt = 0.25f;
        prev = now;
        accumulator += frame_dt;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_KEYDOWN && !e.key.repeat &&
                e.key.keysym.sym == SDLK_F10) {
                level.player_resources[0][0] += 100;
                HU_PushMessage(&hud_text, "CHEAT: +100 RESOURCES", 2000);
                continue;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                (SDL_GetModState() & KMOD_ALT) != 0) {
                if (spawn_debug_enemy_unit(&level, &app, e.button.x, e.button.y)) {
                    P_FreeMobjList(&objects);
                    objects = P_ListMobjs();
                    units = objects.items;
                    unit_count = objects.count;
                    if (!R_InitSprites(app.renderer, data_root, &level,
                                             units, unit_count,
                                             &decoration_sprites)) {
                        fprintf(stderr, "warning: failed to load debug enemy sprite\n");
                    }
                }
                continue;
            }
            if (G_CustomUIResponder(custom_ui, &app, &level, units, unit_count, &e) ||
                SB_Responder(&st, &app, &e)) {
                continue;
            }
            G_Responder(&app, &level, units, unit_count, &unit_sprite,
                         &decoration_sprites, gameinfo, &e);
        }
        G_CameraMove(&app, frame_dt);
        R_ClampCamera(&app, &level, G_WorldViewportWidth(&app), app.win.h);
        while (accumulator >= FIXED_DT) {
            P_Ticker();
            P_FreeMobjList(&objects);
            objects = P_ListMobjs();
            units = objects.items;
            unit_count = objects.count;
            if (level.mission) {
                int before_count = unit_count;
                G_MissionTicker(&level, units, &unit_count,
                                &hud_text, FIXED_DT);
                P_FreeMobjList(&objects);
                objects = P_ListMobjs();
                units = objects.items;
                unit_count = objects.count;
                if (unit_count != before_count) {
                    if (!level.has_camera) focus_camera_on_first_player_unit(&app, &level, units, unit_count);
                    R_ClampCamera(&app, &level, G_WorldViewportWidth(&app), app.win.h);
                    if (!R_InitSprites(app.renderer, data_root, &level,
                                             units, unit_count,
                                             &decoration_sprites)) {
                        fprintf(stderr, "warning: failed to load scripted runtime sprites\n");
                    }
                }
            }
            int before_production_count = unit_count;
            bool production_spawned = G_UpdateProduction(custom_ui, &level, units, &unit_count,
                                                         FIXED_DT);
            P_FreeMobjList(&objects);
            objects = P_ListMobjs();
            units = objects.items;
            unit_count = objects.count;
            if (production_spawned || unit_count != before_production_count) {
                if (!R_InitSprites(app.renderer, data_root, &level,
                                         units, unit_count,
                                         &decoration_sprites)) {
                    fprintf(stderr, "warning: failed to load produced unit sprite\n");
                }
            }

            HU_Ticker(&hud_text, FIXED_DT);
            SB_Ticker(&st);
            G_CustomUITicker(custom_ui);
            accumulator -= FIXED_DT;
        }
        if (level.player_resources[0][0] != title_resources) {
            char title[128];
            title_resources = level.player_resources[0][0];
            snprintf(title, sizeof(title), "open-rts - %s - Resources %d", g_game_name, title_resources);
            SDL_SetWindowTitle(app.window, title);
        }

        app.ticks_ms = SDL_GetTicks();
        renderer_begin_frame(&renderer, (SDL_Color){ 11, 14, 16, 255 });
        R_DrawLevel(&app, &level, &tileset);
        R_RenderPlayerView(&app, &level, &tileset, units, unit_count, &unit_sprite,
                             &decoration_sprites, gameinfo, SDL_GetTicks());

        R_DrawGridOverlay(&app, &level);
        R_DrawFog(&app, &level);
        if (app.dragging_select) {
            SDL_SetRenderDrawColor(app.renderer, 98, 224, 161, 70);
            SDL_RenderFillRect(app.renderer, &app.selection_rect);
            SDL_SetRenderDrawColor(app.renderer, 98, 224, 161, 220);
            SDL_RenderDrawRect(app.renderer, &app.selection_rect);
        }
        G_CustomUIDrawer(custom_ui, &app, &level, units, unit_count, &decoration_sprites, &hud_text);
        SB_Drawer(&st, &app, &level, units, unit_count, &decoration_sprites,
                  false, false);
        renderer_end_frame(&renderer);
    }

    SB_Shutdown(&st);
    G_ShutdownCustomUI(custom_ui);
    R_FreeSpriteCache(&decoration_sprites);
    R_FreeSprite(&unit_sprite);
    R_FreeTileset(&tileset);
    P_FreeMobjList(&objects);
    P_FreeLevel(&level);
    renderer_destroy(&renderer);
    return 0;
}
