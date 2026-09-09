#include "game.h"
#include "engine.h"
#include "info.h"
#include "w_spr.h"
#include "dc_facing.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { WIDTH = 256, HEIGHT = 192, PIXELS = WIDTH * HEIGHT };

static void clear(SDL_Renderer *renderer) {
    SDL_SetRenderDrawColor(renderer, 70, 80, 90, 255);
    SDL_RenderClear(renderer);
}

static void read_pixels(SDL_Renderer *renderer, uint32_t *pixels) {
    assert(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                               pixels, WIDTH * sizeof(*pixels)) == 0);
}

static void check_selection(app_t *app, spritecache_t *cache, mobj_t *unit) {
    uint32_t plain[PIXELS], selected[PIXELS];
    uint8_t baseline[PIXELS];
    const level_t map = {0};
    bool first = true;
    int changed = 0;
    for (int direction = 0; direction < 16; ++direction) {
        unit->core.angle = dc_direction_to_angle(direction);
        for (int step = 0; step < 8; ++step) {
            assert(P_SetMobjState(unit, S_TRSC_RUN1 + step));
            P_MobjSetSelected(unit, false);
            clear(app->renderer);
            R_RenderPlayerView(app, &map, NULL, &unit, 1, NULL, cache, &game_info, 0);
            read_pixels(app->renderer, plain);
            P_MobjSetSelected(unit, true);
            clear(app->renderer);
            R_RenderPlayerView(app, &map, NULL, &unit, 1, NULL, cache, &game_info, 0);
            read_pixels(app->renderer, selected);
            for (int i = 0; i < PIXELS; ++i) {
                bool differs = selected[i] != plain[i];
                if (first) { baseline[i] = differs; changed += differs; }
                else assert(baseline[i] == differs);
            }
            first = false;
        }
    }
    assert(changed > 0);
    /* Object XY, Z and camera movement must move the marker with the anchor. */
    unit->core.position = fixed3_from_fvec2((fvec2_t){1, -1}, FIXED_ONE / 4);
    app->cam = fvec2_add(app->cam, (fvec2_t){7, 9});
    P_MobjSetSelected(unit, false);
    clear(app->renderer);
    R_RenderPlayerView(app, &map, NULL, &unit, 1, NULL, cache, &game_info, 0);
    read_pixels(app->renderer, plain);
    P_MobjSetSelected(unit, true);
    clear(app->renderer);
    R_RenderPlayerView(app, &map, NULL, &unit, 1, NULL, cache, &game_info, 0);
    read_pixels(app->renderer, selected);
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x) {
            bool expected = x >= 39 && y >= 33 && baseline[(y - 33) * WIDTH + x - 39];
            assert((selected[y * WIDTH + x] != plain[y * WIDTH + x]) == expected);
        }
    app->cam = fvec2_sub(app->cam, (fvec2_t){7, 9});
    unit->core.position = fixed3_zero();
}

static void check_death(app_t *app, SDL_Surface *surface, spritecache_t *cache, mobj_t *unit) {
    dc_fin_t fin;
    assert(DC_LoadFIN("data/DCOLONY/ANIMATE/TRSC.FIN", &fin));
    const dc_fin_label_t *label = DC_FINLabel(&fin, "TRSCDIEA14");
    assert(label);
    const spritesheet_t *sheet = R_StateSprite(cache, &game_info, SPR_TRSC, NULL);
    assert(sheet);
    mobj_t attacker = {0};
    P_ApplyActorTypeDefaults(&attacker, actor_type_by_id(MT_TROOPER));
    attacker.allegiance = ALLEGIANCE_ENEMY;
    attacker.attack.target = unit;
    unit->hp = 1;
    assert(P_Attack(&attacker));
    assert(!P_MobjIsSelected(unit) && unit->core.state_id == S_TRSC_DIE1);
    uint32_t actual[PIXELS], expected[PIXELS];
    int elapsed = 0, native = 0;
    level_t map = {0};
    for (int f = SDL_SwapLE16(label->start); f <= SDL_SwapLE16(label->end); ++f) {
        assert(unit->core.frame == sheet->numlumps + f);
        spritedirection_t frame = {0};
        assert(DC_FINFrame(&fin, f, &frame));
        const spritelayer_t *part = frame.layers;
        assert(part[0].sprite_name[0] && !part[1].sprite_name[0]);
        const spritecell_t *cell = &sheet->cells[part->lump];
        const spritelump_t *lump = &sheet->lumps[part->lump];
        SDL_Texture *texture = lump->texture;
        for (int i = 0; i < lump->translation_count; ++i)
            if (lump->translations[i].id == part->remap) texture = lump->translations[i].texture;
        assert(texture && part->flags == 0 && part->intensity == 16);
        irect_t dst = {(int)app->cam.x + part->offset.x + cell->displacement.x,
                      (int)app->cam.y + part->offset.y - cell->rect.h,
                      cell->rect.w, cell->rect.h};
        clear(app->renderer);
        SDL_RenderCopy(app->renderer, texture, &cell->rect, &dst);
        read_pixels(app->renderer, expected);
        clear(app->renderer);
        R_RenderPlayerView(app, &map, NULL, &unit, 1, NULL, cache, &game_info, 0);
        read_pixels(app->renderer, actual);
        assert(!memcmp(actual, expected, sizeof(actual)));
        assert(SDL_SaveBMP(surface, M_va("/private/tmp/trsc-death-%d.bmp", f)) == 0);
        native += (((frame.ticks ? frame.ticks : 15) + 3) * 19) / 100;
        int boundary = (native * 30 + 9) / 19;
        assert(unit->core.tics == boundary - elapsed);
        for (int t = elapsed; t < boundary; ++t) P_TickMobjState(unit);
        elapsed = boundary;
        free(frame.layers);
    }
    assert(elapsed == 308 && unit->core.state_id == S_TRSC_CORPSE && unit->core.tics == -1);
    clear(app->renderer);
    R_RenderPlayerView(app, &map, NULL, &unit, 1, NULL, cache, &game_info, 0);
    read_pixels(app->renderer, actual);
    assert(!memcmp(actual, expected, sizeof(actual)));
    assert(unit->core.position.x == 0 && unit->core.position.y == 0 && unit->core.position.z == 0);
    DC_FreeFIN(&fin);
}

int main(void) {
    G_InitGame();
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, WIDTH, HEIGHT, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(r_renderer);
    app_t app = {.renderer = r_renderer, .win = {WIDTH, HEIGHT}, .cell = {32,32}, .cam = {128,110}};
    spritecache_t *cache = calloc(1, sizeof(*cache));
    assert(cache && load_dark_colony_unit_sprites("data/DCOLONY", NULL, NULL, 0, cache));
    mobj_t unit = {.type_id = MT_TROOPER};
    P_ApplyActorTypeDefaults(&unit, actor_type_by_id(MT_TROOPER));
    P_InitMobj(&game_info, &unit);
    check_selection(&app, cache, &unit);
    check_death(&app, surface, cache, &unit);
    R_FreeSpriteCache(cache);
    free(cache);
    SDL_DestroyRenderer(r_renderer);
    r_renderer = NULL;
    SDL_FreeSurface(surface);
    puts("PASS: Trooper selection anchor is stable; complete death/decay FIN pixels and timing match");
}
