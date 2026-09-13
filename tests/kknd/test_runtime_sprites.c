#include "game.h"
#include "info.h"
#include "../rts_test.h"

#define CHECK(c) RTS_CHECK(c, "kknd sprites", #c)

/* Compare the world renderer to OpenKrush's origin = position - MOBD offset.
 * These offsets are from SPRITES.LVL member 73, records 4405007 + 28*n. */
static int check_anchors(SDL_Renderer *renderer, const spritecache_t *cache) {
    static const ivec2_t tanker_offsets[16] = {
        {14,26}, {21,26}, {27,23}, {31,20}, {34,18}, {31,22}, {27,26}, {22,26},
        {13,26}, {15,26}, {24,26}, {31,22}, {33,18}, {32,20}, {24,23}, {16,26},
    };
    static const struct { int type; ivec2_t anchor; } buildings[] = {
        {MT_SURV_BARRACKS, {65,48}}, /* 121x96 centered sheet, Offset: -5,0 */
        {MT_MUTE_WARRIOR_HALL, {65,55}}, /* 130x110 centered sheet */
    };
    app_t app = {.renderer = renderer, .win = {640,480}, .cell = {32,32}, .cam = {-960,-720}};
    size_t bytes = (size_t)app.win.w * app.win.h * sizeof(uint32_t);
    void *expected = malloc(bytes), *actual = malloc(bytes);
    CHECK(expected && actual);
    mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){40,30},0), MT_SURV_OIL_TANKER);
    CHECK(unit);
    for (int pose = 0; pose < 18; ++pose) {
        ivec2_t anchor;
        int rotation = 0;
        if (pose < 16) {
            anchor = tanker_offsets[pose];
            rotation = (16 - pose) % 16;
            unit->core.angle = direction_to_angle(rotation, 16, ANG90, false);
        } else {
            P_RemoveMobj(unit);
            unit = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){40,30},0), buildings[pose-16].type);
            CHECK(unit);
            anchor = buildings[pose-16].anchor;
        }
        const spritesheet_t *sprite = R_StateSprite(cache, gameinfo, unit->core.sprite_id, NULL);
        CHECK(P_VisibleToPlayer(unit));
        CHECK(sprite);
        const spritelayer_t *layer = sprite->spritedef.spriteframes[unit->core.frame].directions[rotation].layers;
        CHECK(layer);
        bool flip = (layer->flags & RTS_FRAME_FLIP_X) != 0;
        if (pose < 16) CHECK(flip == (pose > 0 && pose < 8));
        const spritecell_t *cell = &sprite->cells[layer->lump];
        float sx, sy;
        R_MapPositionToScreen(&app, &level, unit->core.position, &sx, &sy);
        irect_t dst = {(int)sx-anchor.x, (int)sy-anchor.y, cell->rect.w, cell->rect.h};
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        CHECK(R_DrawSprite(renderer, sprite, layer->lump, unit->team, NULL, &dst,
                          flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE,
                          (SDL_Color){255,255,255,255}, SDL_BLENDMODE_BLEND));
        CHECK(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, expected, app.win.w*4) == 0);
        SDL_RenderClear(renderer);
        R_DrawThings(&app, &unit, 1, NULL, cache, gameinfo, 0);
        CHECK(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, actual, app.win.w*4) == 0);
        if (memcmp(expected, actual, bytes) != 0) {
            fprintf(stderr, "pose=%d native anchor=(%d,%d) loaded ground=(%d,%d) flip=%d\n",
                    pose, anchor.x, anchor.y, cell->ground_point.x, cell->ground_point.y, flip);
            CHECK(false);
        }
    }
    P_RemoveMobj(unit);
    free(expected);
    free(actual);
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(renderer);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/KKND"};
    CHECK(rts_game_model_load(model, &config));
    spritecache_t *cache = calloc(1, sizeof(*cache));
    CHECK(cache);
    mobjlist_t units = P_ListMobjs();
    CHECK(R_InitSprites(renderer, config.data_root, &level, units.items, units.count, cache));
    const spritesheet_t *rifleman = R_StateSprite(cache, gameinfo, SPR_SURV_RIFLEMAN, NULL);
    const spritesheet_t *rig = R_StateSprite(cache, gameinfo, SPR_SURV_DRILLRIG, NULL);
    CHECK(rifleman && !rig);
    SDL_Texture *retained = rifleman->lumps[0].texture;
    P_FreeMobjList(&units);
    for (int i = 0; i < num_actor_types; ++i)
        CHECK(P_SpawnMobj(fixed3_zero(), actor_types[i].id));
    units = P_ListMobjs();
    CHECK(R_InitSprites(renderer, config.data_root, &level, units.items, units.count, cache));
    CHECK(cache->count == NUMSPRITES);
    CHECK(check_anchors(renderer, cache) == 0);
    const spritesheet_t *barracks = R_StateSprite(cache,gameinfo,SPR_SURV_BARRACKS,NULL);
    const spritesheet_t *warriors = R_StateSprite(cache,gameinfo,SPR_MUTE_WARRIOR_HALL,NULL);
    CHECK(barracks && barracks->spritedef.numframes == 13);
    CHECK(warriors && warriors->spritedef.numframes == 6);
    CHECK(ivec2_equal(barracks->cells[3].ground_point, (ivec2_t){65,48}));
    CHECK(ivec2_equal(warriors->cells[3].ground_point, (ivec2_t){65,55}));
    CHECK(R_StateSprite(cache, gameinfo, SPR_SURV_RIFLEMAN, NULL)->lumps[0].texture == retained);
    const spritesheet_t *derrick = R_StateSprite(cache, gameinfo, SPR_SURV_MOBILE_DERRICK, NULL);
    const spritesheet_t *wolf = R_StateSprite(cache, gameinfo, SPR_MUTE_DIRE_WOLF, NULL);
    CHECK(derrick->spritedef.numframes == 4 && wolf->spritedef.numframes == 55);
    CHECK(states[S_SURV_MOBILE_DERRICK_WALK1].frame == 2);
    CHECK(states[S_MUTE_DIRE_WOLF_STND].frame == 40);
    CHECK(states[S_MUTE_DIRE_WOLF_ATCK1].frame == 53);
    CHECK(states[S_MUTE_DIRE_WOLF_WALK1].frame == 46);
    CHECK(states[S_SURV_DRILLRIG_STND].frame == 7);
    CHECK(states[S_SURV_OUTPOST_STND].frame == 170);
    CHECK(states[S_MUTE_CLANHALL_STND].frame == 132);
    CHECK(states[S_SURV_REPAIR_BAY_STND].frame == 20);
    int failures = 0;
    for (int i = 1; i < NUMSTATES; ++i) {
        const state_t *state = &states[i];
        const spritesheet_t *sprite = R_StateSprite(cache, gameinfo, state->sprite, NULL);
        if (!sprite || state->frame < 0 || state->frame >= sprite->spritedef.numframes) {
            fprintf(stderr, "state=%d sprite=%s frame=%d loaded_frames=%d\n", i,
                    sprnames[state->sprite], state->frame, sprite ? sprite->spritedef.numframes : 0);
            failures++;
        }
    }
    CHECK(failures == 0);
    CHECK(R_InitSprites(renderer, config.data_root, &level, units.items, units.count, cache));
    CHECK(cache->count == NUMSPRITES);
    R_FreeSpriteCache(cache);
    free(cache);
    P_FreeMobjList(&units);
    rts_game_model_destroy(model);
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    SDL_Quit();
    puts("PASS: KKND runtime catalog retains textures, resolves all state frames, and renders native anchors");
    return 0;
}
