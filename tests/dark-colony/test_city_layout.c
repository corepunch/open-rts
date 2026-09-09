#include "game.h"
#include "engine.h"
#include "info.h"
#include "dc_types.h"
#include "p_local.h"
#include "w_spr.h"

#include <assert.h>

static void check_city(const char *path, fvec2_t anchor, const char *screenshot) {
    assert(G_DoLoadLevel(path, &level) && P_LoadThings(path) > 0);
    /* DC.EXE 0x475b64, in pixels; the constructor multiplies by 8 for 8.8. */
    const ivec2_t offsets[] = {{-64, 15}, {0, 0}, {32, 64}, {64, 10}, {-32, 65}, {0, 32}};
    const int types[] = {MT_EXCOPOD, MT_BRRKPOD, MT_ROBOPOD, MT_SCNCPOD, MT_RSCHPOD, MT_CITY_TOWER};
    const int poses[] = {S_EXCOPOD_STND, S_BRRKPOD_STND, S_ROBOPOD_STND1,
                         S_SCNCPOD_STND1, S_RSCHPOD_STND1, S_TOWR_STND};
    int counts[6] = {0}, count = 0;
    mobj_t *actors[MAX_OBJECTS];
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(r_renderer);
    app_t app = {.renderer = r_renderer, .win = {640, 480}, .cell = {32, 32}};
    fvec2_t city_screen;
    R_MapPositionToScreen(&app, &level, fixed3_from_fvec2(anchor, 0), &city_screen.x, &city_screen.y);
    app.cam = fvec2_sub((fvec2_t){320, 370}, city_screen);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *actor = (mobj_t *)th;
        assert(count < MAX_OBJECTS);
        actors[count++] = actor;
        if (actor->type_id < MT_EXCOPOD || actor->type_id > MT_CITY_TOWER) continue;
        /* Initial render must already use FIN, including persistent TOWR. */
        assert(actor->type_id < NUMMOBJTYPES);
        assert(actor->core.state_id == mobjinfo[actor->type_id].spawnstate);
        assert(actor->core.state_id != S_NULL);
        const state_t *state = &gameinfo->states[actor->core.state_id];
        assert(actor->core.sprite_id == state->sprite && actor->core.frame == state->frame);
        assert(actor->core.tics == state->tics && actor->thinker.function == P_MobjThinker);
        if (actor->team != 0) continue;
        for (int slot = 0; slot < 6; ++slot) {
            if (actor->type_id != types[slot]) continue;
            counts[slot]++;
            assert(actor->core.state_id == poses[slot]);
            fvec2_t expected = fvec2_add(anchor, (fvec2_t){offsets[slot].x / 32.0f,
                                                                         offsets[slot].y / 32.0f});
            assert(fvec2_near(fixed3_xy_to_fvec2(actor->core.position), expected, 0.0001f));
            fvec2_t screen;
            R_MapPositionToScreen(&app, &level, actor->core.position, &screen.x, &screen.y);
            screen = fvec2_add(screen, (fvec2_t){actor->core.render_offset.x, actor->core.render_offset.y});
            assert(fvec2_near(screen, (fvec2_t){320, 370 + 32}, 0.0001f));
        }
    }
    assert(counts[0] == 1 && counts[1] == 1 && counts[5] == 1);
    tileset_t tiles = {0};
    spritesheet_t fallback = {0};
    assert(W_LoadAssets(r_renderer, "data/DCOLONY", &level, "SPRITES/TROOPER1.SPR", &tiles, &fallback));
    spritecache_t *cache = calloc(1, sizeof(*cache));
    assert(cache && R_InitSprites(r_renderer, "data/DCOLONY", &level, actors, count, cache));
    const spritesheet_t *sheet = R_StateSprite(cache, gameinfo, SPR_HUBU, NULL);
    dc_fin_t fin;
    assert(sheet && DC_LoadFIN("data/DCOLONY/ANIMATE/HUBU.FIN", &fin));
    const char *labels[] = {"ROBOPODSTAND0", "RSCHPODSTAND0"};
    const int starts[] = {S_ROBOPOD_STND1, S_RSCHPOD_STND1};
    for (int sequence = 0; sequence < 2; ++sequence) {
        const dc_fin_label_t *label = DC_FINLabel(&fin, labels[sequence]);
        assert(label);
        mobj_t pose = {.type_id = types[sequence ? 4 : 2],
                       .core = {.state_id = starts[sequence], .tics = states[starts[sequence]].tics}};
        P_InitMobj(gameinfo, &pose);
        int native = 0, elapsed = 0;
        for (int frame = SDL_SwapLE16(label->start); frame <= SDL_SwapLE16(label->end); ++frame) {
            assert(pose.core.sprite_id == SPR_HUBU && pose.core.frame == sheet->numlumps + frame);
            spritedirection_t expected = {0};
            assert(DC_FINFrame(&fin, frame, &expected));
            native += ((expected.ticks ? expected.ticks : 15) + 3) * 15 / 100;
            int boundary = (native * 66 * 30 + 500) / 1000;
            assert(pose.core.tics == boundary - elapsed);
            for (int tic = elapsed; tic < boundary; ++tic) assert(P_TickMobjState(&pose));
            elapsed = boundary;
            free(expected.layers);
        }
        assert(pose.core.state_id == starts[sequence]);
    }
    DC_FreeFIN(&fin);
    R_DrawLevel(&app, &level, &tiles);
    R_RenderPlayerView(&app, &level, &tiles, actors, count, NULL, cache, gameinfo, 0);
    SDL_RenderPresent(r_renderer);
    assert(!SDL_SaveBMP(surface, screenshot));
    R_FreeSpriteCache(cache);
    free(cache);
    R_FreeSprite(&fallback);
    R_FreeTileset(&tiles);
    SDL_DestroyRenderer(r_renderer);
    r_renderer = NULL;
    SDL_FreeSurface(surface);
    P_FreeLevel(&level);
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    for (int type = MT_EXCOPOD; type <= MT_CITY_TOWER; ++type) {
        mobj_t *building = P_SpawnMobj(fixed3_zero(), type);
        assert(building && building->core.state_id == mobjinfo[type].spawnstate);
        assert(building->hp == mobjinfo[type].spawnhealth && building->hp > 0);
        assert(building->traits == (uint32_t)mobjinfo[type].flags);
        assert(building->core.sprite_id == states[building->core.state_id].sprite);
        assert(building->core.frame == states[building->core.state_id].frame);
    }
    P_FreeLevel(&level);
    check_city("data/DCOLONY/SCENARIO/HUMAN/HUMAN02.MAP", (fvec2_t){56, 55}, "/private/tmp/city-human02.bmp");
    check_city("data/DCOLONY/SCENARIO/HUMAN/HUMAN03.MAP", (fvec2_t){75, 6}, "/private/tmp/city-human03.bmp");
    puts("PASS: cities use native slot positions, shared FIN origins, and initialized building states");
    return 0;
}
