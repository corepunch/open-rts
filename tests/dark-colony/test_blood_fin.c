#include "game.h"
#include "engine.h"
#include "info.h"
#include "w_spr.h"
#include "p_blood.h"
#include "dc_types.h"
#include "rts_model_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

/* Draw decoded FIN commands directly as an independent pixel reference. */
static void draw_native_parts(app_t *app, const level_t *map,
                               const spritecache_t *cache, const spritelayer_t *parts, int team, fixed3_t position) {
    float sx, sy;
    R_MapPositionToScreen(app, map, position, &sx, &sy);
    for (const spritelayer_t *part = parts; part->sprite_name[0]; ++part) {
        const spritesheet_t *sprite = R_CacheLookup(cache,
            M_va("SPRITES/%s.SPR", M_Upper(M_va("%s", part->sprite_name))));
        CHECK(sprite && part->lump < sprite->numlumps);
        const spritecell_t *cell = &sprite->cells[part->lump];
        irect_t src = cell->rect;
        irect_t dst = { (int)lroundf(sx) + part->offset.x +
            ((part->flags & RTS_FRAME_FLIP_X) ? 0 : cell->displacement.x),
            (int)lroundf(sy) + part->offset.y - src.h, src.w, src.h };
        CHECK(sprite->shadowmap);
        if (part->layer == 1 || part->layer == 2) {
            irect_t ground = dst;
            ground.y += (int)lroundf(fixed_to_float(position.z) * app->cell.h);
            CHECK(R_RenderSpriteShadow(app, sprite, part->lump, ground, part->flags));
        }
        if (part->layer == 2) continue;
        if (R_RenderIndexedBlend(app, sprite, part->lump, dst, part->flags, part->layer)) continue;
        const spritelump_t *lump = &sprite->lumps[part->lump];
        SDL_Texture *texture = lump->texture;
        for (int i = 0; i < lump->translation_count; ++i)
            if (lump->translations[i].id == team) texture = lump->translations[i].texture;
        CHECK(texture);
        int intensity = part->intensity > 0 ? part->intensity : 16;
        int color = (intensity * 255 + 8) / 16;
        if (color > 255) color = 255;
        SDL_SetTextureColorMod(texture, color, color, color);
        SDL_SetTextureAlphaMod(texture, 255);
        SDL_RenderCopyEx(app->renderer, texture, &src, &dst, 0, NULL,
            (part->flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        SDL_SetTextureColorMod(texture, 255, 255, 255);
    }
}


static void lifecycle(void) {
    gameinfo = &game_info;
    P_FreeThinkers();
    level = (level_t){0};
    CHECK(P_DC_Random() == 5758 && P_DC_Random() == 10113);
    level.random_index = 255;
    CHECK(P_DC_Random() == 16838 && level.random_index == 0);
    level.random_index = 0;
    int variants[7];
    CHECK(P_DC_BloodStates(0, variants) == 7);
    CHECK(variants[0] == S_TRSCBLOODA0_313 && variants[6] == S_TRSCBLOODG0_366);
    CHECK(P_DC_BloodStates(8, variants) == 6); /* F absent; H outside A-G. */
    CHECK(variants[5] == S_GRAYBLOODG0_402);
    CHECK(P_DC_BloodStates(5, variants) == 0); /* SCGMBLOODA has no numeric suffix. */
    CHECK(P_DC_BloodStates(13, variants) == 0);
    CHECK(P_DC_BloodStates(3, variants) == 5);
    CHECK(P_DC_BloodStates(33, variants) == 6); /* MNDHIV2 labels live in BLOO.FIN. */
    CHECK(P_DC_BloodStates(91, variants) > 0); /* TONG labels live in WATC.FIN. */
    CHECK(P_DC_BloodStates(65535, variants) == 0);

    fixed3_t position = {FIXED_ONE, -FIXED_ONE, FIXED_ONE};
    mobj_t *owner = P_SpawnMobj(position, MT_TROOPER);
    CHECK(owner);
    owner->hp = 700;
    owner->team = 3;
    owner->core.angle = ANG90;
    A_DC_Damage(owner);
    mobj_t *blood = (mobj_t *)thinkercap.prev;
    CHECK(blood != owner && blood->type_id == MT_BLOOD);
    CHECK(blood->thinker.function == P_MobjThinker && !P_MobjIsHidden(blood));
    CHECK(blood->core.state_id == S_TRSCBLOODE0_348);
    CHECK(!memcmp(&blood->core.position, &position, sizeof(position)));
    CHECK(blood->team == 3 && blood->core.angle == ANG90);
    int state = blood->core.state_id, tics = blood->core.tics;
    A_DC_Damage(owner);
    mobj_t *second = (mobj_t *)thinkercap.prev;
    CHECK(second != blood && second->type_id == MT_BLOOD);
    CHECK(state == blood->core.state_id && tics == blood->core.tics);
    owner->core.position = fixed3_zero();
    owner->core.angle = 0;
    owner->team = 7;
    P_MobjThinker(blood);
    CHECK(!memcmp(&blood->core.position, &position, sizeof(position)));
    CHECK(blood->core.angle == ANG90 && blood->team == 3);
    P_RemoveMobj(owner);
    CHECK(!blood->remove && !second->remove);
    for (int i = 0; i < 200 && !blood->remove; ++i) P_MobjThinker(blood);
    CHECK(blood->remove && blood->core.state_id == S_NULL);
    P_FreeThinkers();

    owner = P_SpawnMobj(fixed3_zero(), MT_TROOPER);
    owner->hp = 0; /* Lethal hits also spawn-and-forget. */
    A_DC_Damage(owner);
    CHECK(thinkercap.prev != &owner->thinker);
    thinker_t *last = thinkercap.prev;
    P_MobjSetHidden(owner, true);
    A_DC_Damage(owner);
    CHECK(thinkercap.prev == last);
    P_MobjSetHidden(owner, false);
    owner->native_type_id = 5; /* Malformed SCGM label has no native match. */
    A_DC_Damage(owner);
    CHECK(thinkercap.prev == last);
    P_FreeThinkers();
    puts("PASS: native blood selection/RNG; ordinary mobjs retain spawn position/team/facing and outlive recipient");
}

static void pixels(void) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(r_renderer);
    app_t app = {.renderer = r_renderer, .win = {640,480}, .cam = {320,360}, .cell = {32,32}};
    spritecache_t *cache = calloc(1, sizeof(*cache));
    CHECK(load_render_tables("data/DCOLONY", "JUNGLE"));
    CHECK(cache && load_dark_colony_unit_sprites("data/DCOLONY", NULL, NULL, 0, cache));
    size_t bytes = surface->pitch * surface->h;
    void *expected = malloc(bytes);
    CHECK(expected);
    static const struct { const char *file, *label; int state; } cases[] = {
        {"TRSC", "TRSCBLOODA0", S_TRSCBLOODA0_313},
        {"GRAY", "GRAYBLOODA0", S_GRAYBLOODA0_357},
        {"BARR", "BARRBLOODA0", S_BARRBLOODA0_155},
        {"REAP", "REAPBLOODA0", S_REAPBLOODA0_207},
        {"HUBU", "EXCOPODBLOODA0", S_EXCOPODBLOODA0_87},
        {"BLOO", "MNDHIV2BLOODA0", S_MNDHIV2BLOODA0_0},
    };
    for (size_t c = 0; c < sizeof(cases) / sizeof(*cases); ++c) {
        dc_fin_t fin;
        CHECK(DC_LoadFIN(M_va("data/DCOLONY/ANIMATE/%s.FIN", cases[c].file), &fin));
        const dc_fin_label_t *label = DC_FINLabel(&fin, cases[c].label);
        CHECK(label);
        mobj_t body = {.traits = MF_RENDERABLE, .team = 2};
        mobj_t blood = {.traits = MF_RENDERABLE | MF_NOBLOCKMAP};
        CHECK(P_SetMobjState(&body, S_TRSC_STND));
        CHECK(P_SetMobjState(&blood, cases[c].state));
        unsigned native = 0, elapsed = 0;
        int first = SDL_SwapLE16(label->start), last = SDL_SwapLE16(label->end);
        for (int f = first; f <= last; ++f) {
            blood.core.position = fixed3_from_fvec2((fvec2_t){(f-first)*0.1f, (f-first)*0.05f}, FIXED_ONE);
            blood.core.angle = (angle_t)(f-first) * ANG45;
            blood.team = (f-first) % 8;
            spritedirection_t parts = {0};
            CHECK(DC_FINFrame(&fin, f, &parts));
            /* The same BLOO cells have different native shadow modes. */
            if (c < 2) {
                CHECK(parts.layers[0].sprite_name[0] && !parts.layers[1].sprite_name[0]);
                CHECK(parts.layers[0].layer == (c == 0 ? 1 : 0));
            }
            if (f == first + 1) native = elapsed = 0;
            unsigned raw = parts.ticks ? parts.ticks : 15;
            unsigned duration = (uint8_t)((raw + 3) * 15 / 100);
            native += duration ? duration : 256;
            unsigned boundary = (native * 66 * 30 + 500) / 1000;
            CHECK(blood.core.tics == (int)(boundary - elapsed));
            elapsed = boundary;
            SDL_SetRenderDrawColor(r_renderer, 70,80,90,255); SDL_RenderClear(r_renderer);
            R_DrawThings(&app, &(mobj_t *){&body}, 1, NULL, cache, &game_info, 0);
            draw_native_parts(&app, &level, cache, parts.layers, blood.team, blood.core.position);
            memcpy(expected, surface->pixels, bytes);
            SDL_SetRenderDrawColor(r_renderer, 70,80,90,255); SDL_RenderClear(r_renderer);
            /* Independent mobjs sort by their own spawn positions. */
            mobj_t *list[] = {&body, &blood};
            R_RenderPlayerView(&app, &level, NULL, list, 2, NULL, cache, &game_info, 0);
            CHECK(!memcmp(expected, surface->pixels, bytes));
            if (c == 0 && f == first + 4) SDL_SaveBMP(surface, "/private/tmp/dc-native-blood.bmp");
            free(parts.layers);
            bool alive = P_SetMobjState(&blood, gameinfo->states[blood.core.state_id].nextstate);
            CHECK(alive == (f < last));
        }
        CHECK(blood.core.state_id == S_NULL && blood.remove);
        DC_FreeFIN(&fin);
    }
    free(expected);
    R_FreeSpriteCache(cache); free(cache);
    SDL_DestroyRenderer(r_renderer); r_renderer = NULL;
    SDL_FreeSurface(surface);
    puts("PASS: complete organic, machine and cross-FIN blood layers/pixels at independent mobj anchors");
}

int main(void) { lifecycle(); pixels(); return 0; }
