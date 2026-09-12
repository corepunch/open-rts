#include "game.h"
#include "info.h"
#include "../rts_test.h"

#define CHECK(c) RTS_CHECK(c, "kknd sprites", #c)

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
    CHECK(rifleman && rig && rifleman != rig && rifleman->lumps[0].texture != rig->lumps[0].texture);
    SDL_Texture *retained = rifleman->lumps[0].texture;
    P_FreeMobjList(&units);
    for (int i = 0; i < num_actor_types; ++i)
        CHECK(P_SpawnMobj(fixed3_zero(), actor_types[i].id));
    units = P_ListMobjs();
    CHECK(R_InitSprites(renderer, config.data_root, &level, units.items, units.count, cache));
    CHECK(cache->count == NUMSPRITES);
    CHECK(R_StateSprite(cache, gameinfo, SPR_SURV_RIFLEMAN, NULL)->lumps[0].texture == retained);
    const spritesheet_t *derrick = R_StateSprite(cache, gameinfo, SPR_SURV_MOBILE_DERRICK, NULL);
    const spritesheet_t *wolf = R_StateSprite(cache, gameinfo, SPR_MUTE_DIRE_WOLF, NULL);
    CHECK(derrick->spritedef.numframes == 4 && wolf->spritedef.numframes == 53);
    CHECK(states[S_SURV_MOBILE_DERRICK_WALK1].frame == 2);
    CHECK(states[S_MUTE_DIRE_WOLF_STND].frame == 40);
    CHECK(states[S_MUTE_DIRE_WOLF_ATCK1].frame == 41);
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
    puts("PASS: KKND runtime catalog loads distinct sprites, retains textures, and resolves all state frames");
    return 0;
}
