#include "game.h"
#include "engine.h"
#include "info.h"
#include "p_local.h"
#include "p_script.h"
#include "w_spr.h"

#include <assert.h>

static void tick(int count) {
    while (count-- > 0) P_Ticker();
}

static bool active(const mobj_t *actor) {
    return actor->core.state_id >= S_VENT_ACTIVE1 && actor->core.state_id <= S_VENT_ACTIVE20;
}

static void draw(app_t *app, const tileset_t *tiles, const spritecache_t *cache,
                 mobj_t *actor) {
    SDL_SetRenderDrawColor(app->renderer, 70, 80, 90, 255);
    SDL_RenderClear(app->renderer);
    R_DrawLevel(app, &level, tiles);
    R_RenderPlayerView(app, &level, tiles, &actor, actor ? 1 : 0, NULL, cache, gameinfo, 0);
    SDL_RenderPresent(app->renderer);
}

int main(void) {
    const char *path = "data/DCOLONY/SCENARIO/HUMAN/HUMAN02.MAP";
    G_InitGame();
    P_InitThinkers();
    assert(G_DoLoadLevel(path, &level) && P_LoadThings(path) > 0);
    int count = 0;
    mobj_t *vent = NULL, *dormant = NULL, *beacon = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *actor = (mobj_t *)th;
        if (actor->type_id == MT_BEACON) beacon = actor;
        if (actor->type_id != MT_VENT) continue;
        count++;
        const resourcevent_t *resource = &level.resource_vents[actor->resource_vent_index];
        assert(fvec2_near(fixed3_xy_to_fvec2(actor->core.position),
                          resource->attachment, 0.001f));
        assert(active(actor) == resource->active);
        assert(P_MobjIsHidden(actor) == !resource->active);
        assert(actor->thinker.function == P_MobjThinker);
        assert(!(actor->traits & (MF_SELECTABLE | MF_MOBILE | MF_ATTACK)));
        P_ApplyActorTypeDefaults(actor, actor->info);
        P_InitMobj(gameinfo, actor);
        assert(P_MobjIsHidden(actor) == !resource->active);
        if (ivec2_equal(resource->cell, (ivec2_t){69, 48})) vent = actor;
        if (ivec2_equal(resource->cell, (ivec2_t){11, 68})) dormant = actor;
    }
    assert(count == level.resource_vent_count && vent && dormant && beacon);
    assert(beacon->thinker.function == P_MobjThinker && !P_MobjIsHidden(beacon));
    assert(fvec2_near(fixed3_xy_to_fvec2(beacon->core.position), (fvec2_t){64.5f, 52.5f}, 0.001f));
    assert(beacon->hp == 800 && beacon->native_type_id == 84);
    resourcevent_t *resource = &level.resource_vents[vent->resource_vent_index];

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(r_renderer);
    app_t app = { .renderer = r_renderer, .win = {640, 480} };
    fvec2_t screen;
    R_MapPositionToScreen(&app, &level, vent->core.position, &screen.x, &screen.y);
    app.cam = fvec2_sub((fvec2_t){320, 240}, screen);
    tileset_t tiles = {0};
    spritesheet_t fallback = {0};
    assert(W_LoadAssets(r_renderer, "data/DCOLONY", &level, "SPRITES/TROOPER1.SPR", &tiles, &fallback));
    spritecache_t *cache = calloc(1, sizeof(*cache));
    assert(cache && R_InitSprites(r_renderer, "data/DCOLONY", &level, NULL, 0, cache));
    const spritesheet_t *sheet = R_StateSprite(cache, gameinfo, SPR_VENT, NULL);
    assert(sheet);
    dc_fin_t fin;
    assert(DC_LoadFIN("data/DCOLONY/ANIMATE/VENT.FIN", &fin));
    const dc_fin_label_t *label = DC_FINLabel(&fin, "VENTSTAND0");
    assert(label);
    int native = 0, elapsed = 0;
    size_t bytes = (size_t)surface->pitch * surface->h;
    void *first = malloc(bytes), *bare = malloc(bytes);
    assert(first && bare);
    draw(&app, &tiles, cache, NULL);
    memcpy(bare, surface->pixels, bytes);
    for (int frame = SDL_SwapLE16(label->start); frame <= SDL_SwapLE16(label->end); ++frame) {
        assert(vent->core.frame == sheet->numlumps + frame && !P_MobjIsHidden(vent));
        spritedirection_t expected = {0};
        assert(DC_FINFrame(&fin, frame, &expected));
        const spritelayer_t *layers = sheet->spritedef.spriteframes[vent->core.frame].directions[0].layers;
        int part = 0;
        for (; expected.layers[part].sprite_name[0]; ++part) {
            const spritelayer_t *a = &layers[part], *b = &expected.layers[part];
            assert(!strcasecmp(a->sprite_name, b->sprite_name) && a->lump == b->lump);
            assert(ivec2_equal(a->offset, b->offset) && a->flags == b->flags);
            assert(a->layer == b->layer && a->remap == b->remap && a->intensity == b->intensity);
        }
        assert(part >= 4 && !layers[part].sprite_name[0]);
        int raw = expected.ticks ? expected.ticks : 15;
        native += (raw + 3) * 15 / 100;
        int boundary = (native * 66 * 30 + 500) / 1000;
        assert(vent->core.tics == boundary - elapsed);
        draw(&app, &tiles, cache, vent);
        assert(memcmp(bare, surface->pixels, bytes));
        if (frame == SDL_SwapLE16(label->start)) memcpy(first, surface->pixels, bytes);
        if (frame == 29) {
            assert(memcmp(first, surface->pixels, bytes));
            assert(!SDL_SaveBMP(surface, "/private/tmp/vent-active.bmp"));
        }
        tick(boundary - elapsed);
        elapsed = boundary;
        free(expected.layers);
    }
    assert(vent->core.state_id == S_VENT_ACTIVE1);
    DC_FreeFIN(&fin);

    mobj_t *exploiter = P_SpawnMobj(fixed3_from_fvec2(resource->attachment, 0), MT_EXPLOITER);
    assert(exploiter);
    exploiter->harvest.target = vent->resource_vent_index;
    exploiter->harvest.phase = HARVEST_PHASE_MINING;
    tick(8);
    assert(vent->core.state_id == S_VENT_ATTACHED && P_MobjIsHidden(vent));
    draw(&app, &tiles, cache, vent);
    assert(!memcmp(bare, surface->pixels, bytes));
    exploiter->harvest.phase = HARVEST_PHASE_NONE;
    exploiter->harvest.target = -1;
    tick(1);
    assert(active(vent) && !P_MobjIsHidden(vent));
    exploiter->harvest.target = vent->resource_vent_index;
    exploiter->harvest.phase = HARVEST_PHASE_MINING;
    tick(8);
    assert(vent->core.state_id == S_VENT_ATTACHED);
    P_RemoveMobj(exploiter);
    tick(1);
    assert(active(vent) && !P_MobjIsHidden(vent));

    exploiter = P_SpawnMobj(fixed3_from_fvec2(resource->attachment, 0), MT_EXPLOITER);
    assert(exploiter);
    exploiter->harvest.target = vent->resource_vent_index;
    exploiter->harvest.phase = HARVEST_PHASE_MINING;
    exploiter->harvest.timer_ms = 999;
    resource->amount = resource->rate;
    tick(8);
    assert(resource->amount == 0 && !resource->active);
    assert(vent->core.state_id == S_VENT_EXHAUSTED && P_MobjIsHidden(vent));
    draw(&app, &tiles, cache, vent);
    assert(!memcmp(bare, surface->pixels, bytes));
    assert(!SDL_SaveBMP(surface, "/private/tmp/vent-exhausted.bmp"));
    tick(100);
    assert(!vent->remove && vent->core.state_id == S_VENT_EXHAUSTED);

    /* Human02's actual newrate command must find the dormant SCN coordinate. */
    const char *script_path = "/private/tmp/open-rts-vent-test.TRO";
    FILE *file = fopen(script_path, "w");
    assert(file);
    fputs("1 norm 1 (c>0)\nnewrate 12 11 68\nend\n", file);
    fclose(file);
    ScriptState *script = DC_LoadScript(script_path);
    assert(script);
    int units = 0;
    DC_UpdateScript(script, &level, NULL, &units, NULL, 1.0f);
    tick(1);
    assert(active(dormant) && !P_MobjIsHidden(dormant));
    assert(level.resource_vents[dormant->resource_vent_index].rate == 12);
    assert(!resource->active && resource->amount == 0);
    DC_FreeScript(script);
    remove(script_path);

    assert(P_SetMobjState(beacon, S_BEAC_STAND1));
    assert(DC_LoadFIN("data/DCOLONY/ANIMATE/BEAC.FIN", &fin));
    sheet = R_StateSprite(cache, gameinfo, SPR_BEAC, NULL);
    assert(sheet);
    R_MapPositionToScreen(&app, &level, beacon->core.position, &screen.x, &screen.y);
    app.cam = fvec2_add(app.cam, fvec2_sub((fvec2_t){320, 320}, screen));
    for (int frame = 0; frame < 2; ++frame) {
        assert(beacon->core.frame == sheet->numlumps + frame);
        spritedirection_t expected = {0};
        assert(DC_FINFrame(&fin, frame, &expected));
        const spritelayer_t *layers = sheet->spritedef.spriteframes[beacon->core.frame].directions[0].layers;
        for (int i = 0; i <= frame; ++i) {
            const spritelayer_t *a = &layers[i], *b = &expected.layers[i];
            assert(!strcmp(a->sprite_name, ".") && a->lump == b->lump);
            assert(ivec2_equal(a->offset, b->offset) && a->flags == b->flags);
            assert(a->layer == b->layer && a->remap == b->remap && a->intensity == b->intensity);
        }
        assert(!layers[frame + 1].sprite_name[0]);
        assert(beacon->core.tics == (frame ? 4 : 2));
        draw(&app, &tiles, cache, beacon);
        if (!frame) memcpy(first, surface->pixels, bytes);
        else {
            assert(memcmp(first, surface->pixels, bytes));
            assert(!SDL_SaveBMP(surface, "/private/tmp/beacon-lit.bmp"));
        }
        free(expected.layers);
        tick(beacon->core.tics);
    }
    assert(beacon->core.state_id == S_BEAC_STAND1);
    DC_FreeFIN(&fin);

    free(first);
    free(bare);
    R_FreeSpriteCache(cache);
    free(cache);
    R_FreeTileset(&tiles);
    R_FreeSprite(&fallback);
    R_FreeSpriteBuffer();
    SDL_DestroyRenderer(r_renderer);
    r_renderer = NULL;
    SDL_FreeSurface(surface);
    P_FreeLevel(&level);
    assert(thinkercap.next == &thinkercap);
    puts("PASS: vent/beacon FIN rendering and timing; vent attachment, depletion, reactivation and lifecycle");
    return 0;
}
