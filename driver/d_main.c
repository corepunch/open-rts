#define _DEFAULT_SOURCE
#include "engine.h"
#include "game.h"
#include "renderer.h"
#include "sb_bar.h"
#include "p_ai.h"
#include "d_net.h"
#include "m_menu.h"

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
    unit->core.position = fixed3_with_xy(unit->core.position,
        fvec2_cell_center((ivec2_t){ cell.x, cell.y }));
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
        if (units[i]->owner != consoleplayer || units[i]->remove || units[i]->hp <= 0) continue;
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
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) goto help;
    G_InitGame();
    if (!I_InitNetwork(&argc, argv)) {
        fprintf(stderr, "%s\n", neterror);
        return 1;
    }
    atexit(D_QuitNetGame);
    consoleplayer = doomcom->consoleplayer;
    bool check_only = false, screenshot_only = false;
    const char *screenshot_path = NULL;
    bool software_renderer = strcmp(g_game_id, "dark-colony") == 0;
    int check_tics = 0, positional = 0;
    const char *paths[3] = {NULL, NULL, NULL};
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--check")) check_only = true;
        else if (!strcmp(arg, "--software")) software_renderer = true;
        else if (!strncmp(arg, "-map=", 5) || !strncmp(arg, "--map=", 6)) {
            const char *value = strchr(arg, '=') + 1;
            if (!*value || paths[1]) goto usage;
            paths[1] = value;
        }
        else if (!strcmp(arg, "--map") || !strcmp(arg, "--data") || !strcmp(arg, "--sprite")) {
            int slot = !strcmp(arg, "--data") ? 0 : !strcmp(arg, "--map") ? 1 : 2;
            if (++i == argc || !argv[i][0] || paths[slot]) goto usage;
            paths[slot] = argv[i];
        } else if (!strncmp(arg, "-speed=", 7) || !strncmp(arg, "--speed=", 8)) {
            const char *value = strchr(arg, '=') + 1;
            char *end;
            long speed = strtol(value, &end, 10);
            if (!value[0] || *end || speed < 1 || speed > 9) goto usage;
            D_SetGameSpeed((int)speed);
        } else if (!strcmp(arg, "--speed") || !strcmp(arg, "-speed")) {
            if (++i == argc) goto usage;
            char *end;
            long speed = strtol(argv[i], &end, 10);
            if (!argv[i][0] || *end || speed < 1 || speed > 9) goto usage;
            D_SetGameSpeed((int)speed);
        } else if (!strcmp(arg, "--screenshot")) {
            if (++i == argc || !argv[i][0]) goto usage;
            screenshot_only = true; screenshot_path = argv[i];
        } else if (!strcmp(arg, "--net-check")) {
            if (++i == argc) goto usage;
            char *end;
            long count = strtol(argv[i], &end, 10);
            if (!argv[i][0] || *end || count < 1 || count > 1000000) goto usage;
            check_tics = (int)count;
        } else if (!strcmp(arg, "--game")) {
            if (++i == argc || strcmp(argv[i], g_game_id)) goto usage;
        } else if (!strncmp(arg, "--game=", 7)) {
            if (strcmp(arg + 7, g_game_id)) goto usage;
        } else if (arg[0] == '-') goto usage;
        else {
            if (positional == 3 || paths[positional]) goto usage;
            paths[positional++] = arg;
        }
    }
    if ((check_only && screenshot_only) || (check_tics && (check_only || screenshot_only)) ||
        (netgame && (check_only || screenshot_only)) || (I_NetJoining() && paths[1])) goto usage;
    const char *data_root = paths[0] ? paths[0] : g_game_default_root;
    const char *sprite_name = paths[2] ? paths[2] : g_game_default_sprite;
    const char *map_arg = paths[1] ? paths[1] : g_game_default_map;
    char map_name[1024], map_path[1024];
    if (strlen(map_arg) >= sizeof(map_name)) goto usage;
    strcpy(map_name, map_arg);

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
                             check_only || screenshot_only || check_tics,
                             check_only || screenshot_only || check_tics || software_renderer)) {
        return 1;
    }
    app.window = renderer.window;
    app.renderer = renderer.sdl;
    R_RefreshViewport(&app);

    if (!strcmp(g_game_id, "dark-colony") && !check_only && !check_tics) {
        if (!M_Init(&app, data_root)) {
            fprintf(stderr, "Could not load Dark Colony menu assets\n");
            M_Shutdown();
            renderer_destroy(&renderer);
            return 1;
        }
        if (!paths[1] && !netgame) M_StartControlPanel(&app);
    }
    /* No level, thinkers, mission or sidebar exists while choosing New Game. */
    while (app.running && menuactive) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            M_Responder(&app, &event, false);
            if (event.type == SDL_WINDOWEVENT) R_RefreshViewport(&app);
            if (!menuactive || !app.running) break;
        }
        if (!menuactive || !app.running) break;
        M_Ticker();
        renderer_begin_frame(&renderer, (SDL_Color){11, 14, 16, 255});
        M_Drawer(&app);
        if (screenshot_only) {
            bool saved = renderer_save_screenshot(&renderer, screenshot_path);
            M_Shutdown();
            renderer_destroy(&renderer);
            return saved ? 0 : 1;
        }
        renderer_end_frame(&renderer);
        SDL_Delay(1);
    }
    if (!app.running) {
        M_Shutdown();
        renderer_destroy(&renderer);
        return menuerror ? 1 : 0;
    }
load_level:
    if (menumap) {
        snprintf(map_name, sizeof(map_name), "%s", menumap);
        menumap = NULL;
    }

    if (!I_StartNetGame(g_game_id, map_name, sizeof(map_name))) {
        fprintf(stderr, "%s\n", neterror);
        M_Shutdown();
        renderer_destroy(&renderer);
        return 1;
    }
    consoleplayer = doomcom->consoleplayer;
    int path_length = map_name[0] == '/' ?
        snprintf(map_path, sizeof(map_path), "%s", map_name) :
        snprintf(map_path, sizeof(map_path), "%s/%s", data_root, map_name);
    if (path_length < 0 || (size_t)path_length >= sizeof(map_path)) {
        fprintf(stderr, "Map path is too long\n");
        M_Shutdown();
        renderer_destroy(&renderer);
        return 1;
    }
    P_InitThinkers();
    if (!G_DoLoadLevel(map_path, &level) || !P_InitSight()) {
        P_FreeLevel(&level);
        M_Shutdown();
        renderer_destroy(&renderer);
        return 1;
    }

    tileset_t tileset;
    spritesheet_t unit_sprite;
    memset(&tileset, 0, sizeof(tileset));
    memset(&unit_sprite, 0, sizeof(unit_sprite));
    if (!W_LoadAssets(app.renderer, data_root, &level, sprite_name, &tileset, &unit_sprite)) {
        P_FreeLevel(&level);
        M_Shutdown();
        renderer_destroy(&renderer);
        return 1;
    }
    app.cell.w = g_cell_w > 0 ? g_cell_w : (tileset.tile_w > 0 ? tileset.tile_w : CELL_W);
    app.cell.h = g_cell_h > 0 ? g_cell_h : (tileset.tile_h > 0 ? tileset.tile_h : CELL_H);

    P_LoadThings(map_path);
    mobjlist_t objects = P_ListMobjs();
    mobj_t **units = objects.items;
    int unit_count = objects.count;
    if (unit_count <= 0 && !netgame) {
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
    for (int i = 0; i < unit_count; ++i)
        if (units[i]->owner != consoleplayer) P_MobjSetSelected(units[i], false);
    P_UpdateSight();

    spritecache_t decoration_sprites = { 0 };
    if (!R_InitSprites(app.renderer, data_root, &level, units, unit_count,
                              &decoration_sprites)) {
        fprintf(stderr, "warning: some %s runtime sprites were not loaded\n", g_game_name);
    }

    if (!(netgame && focus_camera_on_first_player_unit(&app, &level, units, unit_count)) &&
        !focus_camera_on_map_start(&app, &level)) {
        fvec2_t position = unit_count > 0 ? fixed3_xy_to_fvec2(units[0]->core.position) :
            (fvec2_t){ (float)level.width * 0.5f, (float)level.height * 0.5f };
        float focus_gx = position.x;
        float focus_gy = position.y;
        focus_camera_on_grid(&app, &level, focus_gx, focus_gy);
    }
    R_ClampCamera(&app, &level, G_WorldViewportWidth(&app), app.win.h);

    printf("Loaded %s (%dx%d, tileset %s, %d units, %d level decorations, %d resource vents). Controls: left select/drag/order, right deselect, Alt+left spawn enemy, WASD/arrows pan, G grid, B blocked overlay, Ctrl+A select all, F10 +100 resources.\n",
           map_path, level.width, level.height, level.tileset_name, unit_count,
           level.decoration_count, level.resource_vent_count);

    void *custom_ui = G_InitCustomUI(&app, data_root);
    AiContext ai;
    P_AiInit(&ai);
    sb_state_t st = { 0 };
    if (!custom_ui && gameui && !SB_Init(&st, app.renderer, data_root, gameui))
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
            if (!custom_ui) SB_ProductionDrawer(&st, &app);
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
        M_Shutdown();
        renderer_destroy(&renderer);
        return 0;
    }

    uint64_t prev = SDL_GetPerformanceCounter();
    double freq = (double)SDL_GetPerformanceFrequency();
    int title_resources = -1;
    uint32_t signature;
    if (!G_NetSignature(map_path, &signature)) {
        fprintf(stderr, "Could not fingerprint map %s\n", map_path);
        app.running = false;
        snprintf(neterror, sizeof(neterror), "Map fingerprint failed");
    } else {
        D_CheckNetGame(signature);
        if (netgame) {
            for (int player = 0; player < doomcom->numplayers; ++player) {
                bool has_units = false;
                for (int i = 0; i < unit_count; ++i)
                    if (units[i]->owner == player && units[i]->hp > 0 &&
                        !units[i]->remove && (units[i]->traits & MF_SELECTABLE)) has_units = true;
                if (has_units) continue;
                snprintf(neterror, sizeof(neterror),
                         "Map has no starting units for player %d; choose a multiplayer map with --map", player + 1);
                fprintf(stderr, "%s\n", neterror);
                app.running = false;
                break;
            }
        }
    }
    uint64_t check_started = SDL_GetTicks64();

    while (app.running) {
        uint64_t now = SDL_GetPerformanceCounter();
        float frame_dt = (float)((double)(now - prev) / freq);
        if (frame_dt > 0.25f) frame_dt = 0.25f;
        prev = now;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (M_Responder(&app, &e, true)) {
                if (menumap || !app.running) break;
                continue;
            }
            if (e.type == SDL_KEYDOWN && !e.key.repeat &&
                e.key.keysym.sym == SDLK_F10) {
                if (netgame) continue;
                level.player_resources[consoleplayer][0] += 100;
                HU_PushMessage(&hud_text, "CHEAT: +100 RESOURCES", 2000);
                continue;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT &&
                (SDL_GetModState() & KMOD_ALT) != 0) {
                if (netgame) continue;
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
            /* Cancellation applies over the HUD too, before its responders
             * consume mouse buttons. Future games can retain right orders. */
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT &&
                !(gameinfo && gameinfo->right_click_orders)) {
                G_CustomUIResponder(custom_ui, &app, &level, units, unit_count, &e);
                G_Responder(&app, &level, units, unit_count, &unit_sprite,
                             &decoration_sprites, gameinfo, &e);
                continue;
            }
            if ((!custom_ui && SB_ProductionResponder(&st, &app, &e)) ||
                G_CustomUIResponder(custom_ui, &app, &level, units, unit_count, &e) ||
                SB_Responder(&st, &app, &e)) {
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    app.dragging_select = false;
                    app.selection_rect = (irect_t){0};
                }
                continue;
            }
            G_Responder(&app, &level, units, unit_count, &unit_sprite,
                         &decoration_sprites, gameinfo, &e);
        }
        if (menumap || !app.running) break;
        if (!menuactive) G_CameraMove(&app, frame_dt);
        M_Ticker();
        R_ClampCamera(&app, &level, G_WorldViewportWidth(&app), app.win.h);
        int runtics = TryRunTics();
        if (check_tics && runtics > check_tics - gametic) runtics = check_tics - gametic;
        while (runtics-- > 0) {
            if (!D_RunTiccmds()) break;
            /* Like Doom, menu pause stops the world, not the tic clock.
             * Multiplayer continues so opening a menu cannot stall peers. */
            if (menuactive && !netgame) {
                ++gametic;
                NetUpdate();
                continue;
            }
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
            bool production_spawned;
            if (custom_ui) {
                production_spawned = G_UpdateProduction(custom_ui, &level, units, &unit_count,
                                                        FIXED_DT);
            } else {
                P_AiTick(&ai, &level, units, unit_count, gameinfo, (int)(FIXED_DT * 1000));
                G_ModelAIProduction(NULL, (int)(FIXED_DT * 1000));
                production_spawned = G_ProductionTicker(FIXED_DT);
            }
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
            ++gametic;
            NetUpdate();
        }
        if (neterror[0]) {
            fprintf(stderr, "%s\n", neterror);
            app.running = false;
        }
        if (check_tics && gametic == check_tics) {
            printf("Network check: player=%d gametic=%d consistency=%08x\n",
                   consoleplayer + 1, gametic, G_Consistency());
            app.running = false;
        }
        if (check_tics && SDL_GetTicks64() - check_started > (uint64_t)check_tics * 1000 / RTS_TICRATE + 30000) {
            snprintf(neterror, sizeof(neterror), "Network check timed out at tic %d", gametic);
            fprintf(stderr, "%s\n", neterror);
            app.running = false;
        }
        if (check_tics) { SDL_Delay(1); continue; }
        if (level.player_resources[consoleplayer][0] != title_resources) {
            char title[128];
            title_resources = level.player_resources[consoleplayer][0];
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
            SDL_SetRenderDrawColor(app.renderer, 98, 224, 161, 255);
            SDL_RenderDrawRect(app.renderer, &app.selection_rect);
        }
        G_CustomUIDrawer(custom_ui, &app, &level, units, unit_count, &decoration_sprites, &hud_text);
        SB_Drawer(&st, &app, &level, units, unit_count, &decoration_sprites,
                  false, false);
        if (!custom_ui) SB_ProductionDrawer(&st, &app);
        M_Drawer(&app);
        renderer_end_frame(&renderer);
    }

    /* Let bounded-check clients consume the final commands and quit before
     * shutting down their relay. No further simulation tics run here. */
    if (check_tics && gametic == check_tics && netgame && !consoleplayer && !I_NetJoining()) {
        uint64_t until = SDL_GetTicks64() + 2000;
        bool waiting = true;
        while (waiting && !neterror[0] && SDL_GetTicks64() < until) {
            NetUpdate();
            waiting = false;
            for (int p = 1; p < doomcom->numplayers; ++p) waiting |= playeringame[p];
            SDL_Delay(1);
        }
    }
    int exit_code = neterror[0] || menuerror ? 1 : 0;
    D_QuitNetGame();
    SB_Shutdown(&st);
    G_ShutdownCustomUI(custom_ui);
    R_FreeSpriteCache(&decoration_sprites);
    R_FreeSprite(&unit_sprite);
    R_FreeTileset(&tileset);
    P_FreeMobjList(&objects);
    P_FreeLevel(&level);
    if (app.running && menumap && !exit_code) goto load_level;
    M_Shutdown();
    renderer_destroy(&renderer);
    return exit_code;
usage:
    fprintf(stderr, "Invalid arguments. Use --help for command-line options.\n");
    return 1;
help:
    printf("Usage: %s [options] [data-root [map [sprite]]]\n"
           "  --host                 Host a game (you are player 1)\n"
           "  --players <2..4>       Players to wait for; default 2\n"
           "  --join <host[:port]>   Join; receive the host's map and player slot\n"
           "  --port <1..65535>      Local UDP port; host 5029, client automatic\n"
           "  --map <path>           Map relative to data root; chosen by host\n"
           "  -map=<path>           Start directly in a map (also --map=<path>)\n"
           "  --speed <1..9>        Simulation speed multiplier; default 1\n"
           "  Dark Colony opens its main menu when no map is supplied.\n"
           "  --check and --net-check use the default map; screenshots show startup.\n"
           "  --data <directory>     Local game data directory\n"
           "  --sprite <path>        Default sprite asset\n"
           "  --software            Use the software renderer\n"
           "  --check | --screenshot <file.bmp>   Offline smoke check\n"
           "  --net-check <tics>     Run a bounded headless simulation\n"
           "  --net <1..4> <peers...>  Legacy manual peer setup\n"
           "  --dup <1..9> --extratic  Doom command timing/redundancy\n", argv[0]);
    return 0;
}
