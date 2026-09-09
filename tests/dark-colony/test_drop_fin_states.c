#include "engine.h"
#include "info.h"
#include "w_spr.h"
#include "p_drop.h"
#include "rts_model_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static void clear(app_t *app) {
    SDL_SetRenderDrawColor(app->renderer, 70, 80, 90, 255);
    SDL_RenderClear(app->renderer);
}

static void check_sequence(app_t *app, SDL_Surface *surface, spritecache_t *cache,
                           const char *file, const char *label_name,
                           int first_state, int exit_state) {
    level_t map = {0};
    dc_fin_t fin;
    CHECK(DC_LoadFIN(M_va("data/DCOLONY/ANIMATE/%s.FIN", file), &fin));
    const dc_fin_label_t *label = DC_FINLabel(&fin, label_name);
    CHECK(label);
    const spritesheet_t *sheet = R_StateSprite(cache, &game_info, states[first_state].sprite, NULL);
    CHECK(sheet);
    int start = SDL_SwapLE16(label->start), end = SDL_SwapLE16(label->end);
    mobj_t unit = { .traits = MF_RENDERABLE, .team = 7,
        .movement.goal = {16, 16} };
    statecontext_t ctx = { .game_info = &game_info };
    CHECK(P_SetMobjState(&ctx, &unit, first_state));
    size_t bytes = (size_t)surface->pitch * surface->h;
    void *expected_pixels = malloc(bytes);
    CHECK(expected_pixels);
    int elapsed = 0, native = 0;
    for (int f = start; f <= end; ++f) {
        CHECK(unit.core.frame == sheet->numlumps + f);
        spritedirection_t expected = {0};
        CHECK(DC_FINFrame(&fin, f, &expected));
        const spritelayer_t *actual = sheet->spritedef.spriteframes[unit.core.frame].directions[0].layers;
        effect_t parts[100] = {0};
        int p = 0;
        for (; expected.layers[p].sprite_name[0]; ++p) {
            CHECK(p < 100);
            const spritelayer_t *part = &expected.layers[p];
            CHECK(!strcasecmp(actual[p].sprite_name, !strcasecmp(part->sprite_name, file) ? "." : part->sprite_name));
            CHECK(actual[p].lump == part->lump && ivec2_equal(actual[p].offset, part->offset));
            CHECK(actual[p].layer == part->layer && actual[p].flags == part->flags);
            CHECK(actual[p].remap == part->remap && actual[p].intensity == part->intensity);
            parts[p] = (effect_t){ .active = true, .fin_placement = true,
                .render_selector = part->layer,
                .core = { .frame = part->lump, .render_offset = part->offset,
                    .render_flags = part->flags, .render_remap = part->remap,
                    .render_intensity = part->intensity } };
            snprintf(parts[p].core.sprite_name, sizeof(parts[p].core.sprite_name),
                     "SPRITES/%s.SPR", M_Upper(M_va("%s", part->sprite_name)));
        }
        CHECK(!actual[p].sprite_name[0]);
        clear(app);
        R_DrawEffects(app, &map, parts, p, cache, &game_info);
        memcpy(expected_pixels, surface->pixels, bytes);
        clear(app);
        R_RenderPlayerView(app, &map, NULL, &unit, 1, NULL, cache, &game_info, 0);
        CHECK(!memcmp(expected_pixels, surface->pixels, bytes));
        if (f == (start + end) / 2)
            CHECK(SDL_SaveBMP(surface, M_va("/private/tmp/%s.bmp", label_name)) == 0);
        int raw = expected.ticks ? expected.ticks : 15;
        native += ((raw + 3) * 19) / 100;
        int boundary = (native * 30 + 9) / 19;
        CHECK(unit.core.tics == boundary - elapsed);
        int state = unit.core.state_id;
        for (int t = elapsed; t < boundary; ++t) {
            CHECK(unit.core.state_id == state);
            P_TickMobjState(&ctx, &unit);
        }
        elapsed = boundary;
        free(expected.layers);
    }
    CHECK(!unit.remove && unit.core.state_id == exit_state);
    fprintf(stderr, "%s: frames=%d tics=%d native layers/pixels match\n", label_name, end - start + 1, elapsed);
    free(expected_pixels);
    DC_FreeFIN(&fin);
}

int main(void) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(r_renderer);
    app_t app = { .renderer = r_renderer, .win = {640, 480}, .cam = {320, 360} };
    spritecache_t *cache = calloc(1, sizeof(*cache));
    CHECK(cache && load_dark_colony_unit_sprites("data/DCOLONY", NULL, NULL, 0, cache));
    check_sequence(&app, surface, cache, "DROP", "DROPMOVE0", S_DROP_MOVE1, S_DROP_MOVE1);
    /* With no object context the release action leaves nextstate to advance. */
    check_sequence(&app, surface, cache, "DROP", "DROPTWO", S_DROP_UNLOAD1, S_DROP_MOVE1);
    static const struct { int product, first, last; const char *file, *label; } builds[] = {
        {20, S_SCNCPOD_BUILD1, S_SCNCPOD_STND1, "DROP", "SCNCPODBUILD0"},
        {21, S_SCNCPOD2_BUILD1, S_SCNCPOD2_STND1, "DROP3", "SCNCPOD2BUILD0"},
        {19, S_ROBOPOD2_BUILD1, S_ROBOPOD2_STND1, "DROP4", "ROBOPOD2BUILD0"},
    };
    for (size_t i = 0; i < sizeof(builds) / sizeof(builds[0]); ++i) {
        const StaticProductDefinition *product = G_ModelProductByClassType(NULL, RTS_PRODUCT_BUILDING, builds[i].product);
        CHECK(product && G_ModelBuildingStateForProduct(&game_info, product) == builds[i].first);
        check_sequence(&app, surface, cache, builds[i].file, builds[i].label, builds[i].first, builds[i].last);
    }
    R_FreeSpriteCache(cache);
    free(cache);
    SDL_DestroyRenderer(r_renderer);
    r_renderer = NULL;
    SDL_FreeSurface(surface);
    puts("PASS: native dropship/construction frame chains and pixels");
    return 0;
}
