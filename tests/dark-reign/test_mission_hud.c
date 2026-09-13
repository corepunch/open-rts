#include "../rts_model_test.h"
#include "game.h"
#include "info.h"
#include "dr_types.h"
#include "hud/dr_hud.h"

#define CHECK(c) RTS_CHECK(c, "dark-reign mission HUD", #c)

static int compare_buildings(app_t *app, spritecache_t *cache) {
    const char *bodies[] = {"nfhqt1l0.spr", "nclnc1l0.spr", "ncpow1l0.spr"};
    const char *tops[] = {"tfhqt1l0.spr", "tclnc1l0.spr", "tcpow1l0.spr"};
    mapdecoration_t decorations[3] = {0};
    for (int i = 0; i < 3; ++i) {
        snprintf(decorations[i].sprite_name, 32, "tileset|%s", bodies[i]);
        snprintf(decorations[i].sprite2_name, 32, "base|%s", bodies[i]);
        snprintf(decorations[i].sprite3_name, 32, "base|%s", tops[i]);
    }
    level_t fixture = level;
    fixture.decorations = decorations;
    fixture.decoration_count = 3;
    CHECK(R_InitSprites(app->renderer, g_game_default_root, &fixture, NULL, 0, cache));
    int pixels = 0;
    for (int i = 0; i < 3; ++i) {
        const spritesheet_t *result = R_CacheLookup(cache, bodies[i]);
        const spritesheet_t *layers[] = {
            R_CacheLookup(cache, decorations[i].sprite_name),
            R_CacheLookup(cache, decorations[i].sprite2_name),
            R_CacheLookup(cache, decorations[i].sprite3_name),
        };
        CHECK(result && layers[0] && layers[1] && layers[2]);
        for (int y = 0; y < result->frame_size.h; ++y)
            for (int x = 0; x < result->frame_size.w; ++x) {
                uint32_t expected = 0;
                for (int layer = 2; layer >= 0; --layer) {
                    const spritesheet_t *s = layers[layer];
                    if (x >= s->frame_size.w || y >= s->frame_size.h) continue;
                    int frame = s->numlumps > 1 ? 1 : 0;
                    int index = s->lumps[frame].indices[y*s->frame_size.w+x];
                    if (index) { expected = s->palette[index]; break; }
                }
                CHECK(result->palette[result->lumps[0].indices[y*result->frame_size.w+x]] == expected);
                ++pixels;
            }
    }
    printf("PASS: %d completed-building pixels match the native three-layer presentation\n", pixels);
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {0};
    CHECK(model && rts_game_model_load(model, &config));
    CHECK(!strcmp(g_game_default_map, "scenario/FIXED/M01F/M01F.SCN"));
    CHECK(level.width == 60 && level.height == 60);
    CHECK(level.player_resources[0][0] == 4000);
    CHECK(level.has_camera && level.camera.x == 225.0f/24 && level.camera.y == 1160.0f/24);
    CHECK(G_ModelHasActorType(model, 0, MT_FG_HQ1));
    CHECK(G_ModelHasActorType(model, 0, MT_FG_LIFE_PLANT));
    CHECK(G_ModelHasActorType(model, 0, MT_FG_POWER_PLANT));
    CHECK(!G_ModelHasActorType(model, 0, MT_FG_CONSTRUCTION_CREW));
    CHECK(DR_ProductInTech(11) && DR_ProductInTech(13) && DR_ProductInTech(9) && DR_ProductInTech(1));
    CHECK(!DR_ProductInTech(10) && !DR_ProductInTech(14));
    CHECK(gameui->command_rows == 5 && gameui->icon_size.w == 64 && gameui->icon_size.h == 50);
    irect_t radar = DR_MinimapRect(&level);
    CHECK(radar.x == 488 && radar.y == 381 && radar.w == 60 && radar.h == 60);

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640,480,32,SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    app_t app = {.renderer = SDL_CreateSoftwareRenderer(surface), .win = {640,480}, .cell = {24,24}};
    CHECK(app.renderer);
    sb_state_t *bar = G_InitCustomUI(&app, g_game_default_root);
    CHECK(bar && bar->ready);
    mobjlist_t objects = P_ListMobjs();
    spritecache_t *cache = calloc(1, sizeof(*cache));
    CHECK(cache && R_InitSprites(app.renderer, g_game_default_root, &level, objects.items, objects.count, cache));
    const spritesheet_t *hq = R_CacheLookup(cache, "nfhqt1l0.spr");
    CHECK(hq && hq->numlumps == 1 && hq->cells[0].ground_point.x == 0 && hq->cells[0].ground_point.y == 0);
    CHECK(compare_buildings(&app, cache) == 0);
    for (int i = 0; i < objects.count; ++i) P_MobjSetSelected(objects.items[i], false);
    G_CustomUIDrawer(bar, &app, &level, objects.items, objects.count, cache, NULL);
    CHECK(SDL_SaveBMP(surface, "/private/tmp/open-rts-mission01-hud.bmp") == 0);
    SDL_Event click = {.type = SDL_MOUSEBUTTONDOWN};
    click.button.button = SDL_BUTTON_LEFT;
    click.button.x = 458; click.button.y = 74;
    CHECK(G_CustomUIResponder(bar, &app, &level, objects.items, objects.count, &click));
    CHECK(level.player_resources[0][0] == 3700); /* First native slot builds a rig at the HQ. */
    click.button.x = 522; /* Freighter is unavailable without an assembly plant. */
    CHECK(G_CustomUIResponder(bar, &app, &level, objects.items, objects.count, &click));
    CHECK(level.player_resources[0][0] == 3700);
    click.button.x = 590; click.button.y = 10;
    CHECK(G_CustomUIResponder(bar, &app, &level, objects.items, objects.count, &click));
    CHECK(bar->options_visible);
    G_ShutdownCustomUI(bar);
    R_FreeSpriteCache(cache); free(cache);
    P_FreeMobjList(&objects);
    SDL_DestroyRenderer(app.renderer); SDL_FreeSurface(surface);
    rts_game_model_destroy(model);
    SDL_Quit();
    puts("PASS: Mission 01 start, native HUD assets, technology, production slots and MENU");
    return 0;
}
