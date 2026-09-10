#include "game.h"
#include "info.h"
#include "r_selection.h"
#include "../rts_model_test.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 512, HEIGHT = 256, PIXELS = WIDTH * HEIGHT };
static uint32_t expected[PIXELS], actual[PIXELS];

static void clear(SDL_Renderer *renderer) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    for (int i = 0; i < PIXELS; ++i) expected[i] = 0xff000000;
}

static void reference(const spritesheet_t *client, int frame, ivec2_t at) {
    isize2_t size = {client->cells[frame].rect.w, client->cells[frame].rect.h};
    for (int y = 0; y < size.h; ++y)
        for (int x = 0; x < size.w; ++x) {
            unsigned index = client->lumps[frame].indices[y * size.w + x];
            if (index) expected[(at.y + y) * WIDTH + at.x + x] = client->palette[index];
        }
}

static void compare(SDL_Renderer *renderer) {
    assert(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                               actual, WIDTH * sizeof(*actual)) == 0);
    assert(!memcmp(actual, expected, sizeof(actual)));
}

static void check_ranks(app_t *app, spritecache_t *cache) {
    const spritesheet_t *client = R_CacheLookup(cache->ui, "INTRFACE/CLIENT.SPR");
    assert(client && client->numlumps == 51);
    mobj_t unit = {.type_id = MT_TROOPER, .ability_charge = 64};
    P_ApplyActorTypeDefaults(&unit, actor_type_by_id(MT_TROOPER));
    P_InitMobj(gameinfo, &unit);
    unitoverlaycontext_t ctx = {.app = app, .unit = &unit, .cache = cache,
                                .game_info = gameinfo, .anchor = {128,110}};
    /* TRSC STAND bounds top = -43; CLIENT cell 0 = 12x5. */
    static const ivec2_t badge[] = {{122,62}, {122,59}, {122,56}, {122,53}};
    static const ivec2_t star[] = {{122,54}, {122,51}, {122,48}, {122,45}};
    static const ivec2_t meter[] = {{122,50}, {122,47}, {122,44}, {122,41}};
    static const int health[] = {100,81,80,61,60,41,40,21,20,1};
    static const int color[] = {0,0,1,1,2,2,3,3,4,4};
    for (int type = 69; type <= 76; ++type) {
        int rank = (type - 69) % 4;
        int badge_frame = (type < 73 ? 35 : 47) + rank;
        /* GRAY standing extent is 34 pixels, nine shorter than TRSC. */
        ivec2_t shift = {0, type < 73 ? 0 : 9};
        unit.native_type_id = type;
        for (int selected = 0; selected < 2; ++selected) {
            P_MobjSetSelected(&unit, selected);
            for (int h = 0; h < 10; ++h) {
                unit.max_hp = 100; unit.hp = health[h];
                clear(app->renderer);
                DC_DrawUnitOverlays(&ctx);
                reference(client, badge_frame, ivec2_add(badge[rank], shift));
                if (selected) {
                    reference(client, 10 + color[h], ivec2_add(star[rank], shift));
                    reference(client, 24, ivec2_add(meter[rank], shift));
                }
                compare(app->renderer);
            }
        }
    }
    static const int charge[] = {0,3,4,31,32,64,255};
    static const int charge_frame[] = {15,15,16,23,24,24,24};
    unit.native_type_id = 69;
    unit.hp = unit.max_hp;
    for (int i = 0; i < 7; ++i) {
        unit.ability_charge = charge[i];
        clear(app->renderer);
        DC_DrawUnitOverlays(&ctx);
        reference(client, 35, badge[0]);
        reference(client, 10, star[0]);
        reference(client, charge_frame[i], meter[0]);
        compare(app->renderer);
    }
    unit.native_type_id = 0;
    for (int selected = 0; selected < 2; ++selected) {
        P_MobjSetSelected(&unit, selected);
        unit.hp = unit.max_hp;
        clear(app->renderer);
        DC_DrawUnitOverlays(&ctx);
        if (selected) reference(client, 0, (ivec2_t){122,60});
        compare(app->renderer);
    }
    unit.native_type_id = 69;
    unit.hp = 0;
    clear(app->renderer);
    DC_DrawUnitOverlays(&ctx);
    compare(app->renderer);
}

static void human01(app_t *app, SDL_Surface *surface, spritecache_t *cache) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = "SCENARIO/HUMAN/HUMAN01.MAP"};
    assert(model && rts_game_model_load(model, &config));
    RtsRenderSnapshot snapshot;
    mobj_t *sergeant = NULL;
    mobjlist_t objects = {0};
    for (int tick = 0; tick < 3600; ++tick) {
        assert(rts_tick(model, &snapshot));
        P_FreeMobjList(&objects);
        objects = P_ListMobjs();
        int troopers = 0;
        for (int i = 0; i < objects.count; ++i) {
            mobj_t *u = objects.items[i];
            if (u->owner == 0 && u->type_id == MT_TROOPER) ++troopers;
            if (u->owner == 0 && u->native_type_id == 69) sergeant = u;
        }
        if (sergeant && troopers == 4) break;
    }
    assert(sergeant && sergeant->ability_charge == 64);
    tileset_t tiles = {0};
    spritesheet_t fallback = {0};
    assert(W_LoadAssets(app->renderer, config.data_root, &level, g_game_default_sprite, &tiles, &fallback));
    /* Aim the actual level at the delivered HUMAN01 sergeant. */
    app->cam = (fvec2_t){0};
    fvec2_t position;
    R_MapPositionToScreen(app, &level, sergeant->core.position, &position.x, &position.y);
    app->cam = fvec2_sub((fvec2_t){WIDTH / 2, HEIGHT * 3 / 4}, position);
    for (int selected = 0; selected < 2; ++selected) {
        for (int i = 0; i < objects.count; ++i)
            P_MobjSetSelected(objects.items[i], selected && objects.items[i]->owner == 0);
        clear(app->renderer);
        R_DrawLevel(app, &level, &tiles);
        R_RenderPlayerView(app, &level, &tiles, objects.items, objects.count, &fallback, cache, gameinfo, 0);
        assert(SDL_SaveBMP(surface, selected ? "/private/tmp/dc-human01-selected.bmp" :
                                              "/private/tmp/dc-human01-unselected.bmp") == 0);
    }
    R_FreeSprite(&fallback);
    R_FreeTileset(&tiles);
    P_FreeMobjList(&objects);
    rts_game_model_destroy(model);
}

int main(void) {
    G_InitGame();
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, WIDTH, HEIGHT, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(r_renderer);
    app_t app = {.renderer = r_renderer, .win = {WIDTH,HEIGHT}, .cell = {32,32}, .cam = {128,110}};
    spritecache_t *cache = calloc(1, sizeof(*cache));
    assert(cache && load_dark_colony_unit_sprites("data/DCOLONY", NULL, NULL, 0, cache));
    check_ranks(&app, cache);
    human01(&app, surface, cache);
    R_FreeSpriteCache(cache);
    free(cache);
    R_FreeSpriteBuffer();
    SDL_DestroyRenderer(r_renderer);
    r_renderer = NULL;
    SDL_FreeSurface(surface);
    puts("PASS: persistent rank badges, native selection composition/health thresholds, HUMAN01 delivery");
}
