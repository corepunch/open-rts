#include "game.h"
#include "engine.h"
#include "info.h"
#include "p_local.h"
#include "p_blood.h"
#include "dc_types.h"
#include "w_spr.h"
#include <assert.h>

static void screenshot(app_t *app, SDL_Surface *surface, tileset_t *tiles,
                       spritecache_t *cache, const char *name) {
    mobjlist_t actors = P_ListMobjs();
    R_DrawLevel(app, &level, tiles);
    R_RenderPlayerView(app, &level, tiles, actors.items, actors.count, NULL, cache, gameinfo, 0);
    P_FreeMobjList(&actors);
    SDL_RenderPresent(r_renderer);
    assert(!SDL_SaveBMP(surface, name));
}

static void hit(mobj_t *building, int remaining_hp) {
    mobj_t attacker = {0};
    P_ApplyActorTypeDefaults(&attacker, actor_type_by_id(MT_TROOPER));
    attacker.allegiance = ALLEGIANCE_ENEMY;
    attacker.attack.target = building;
    building->hp = remaining_hp + attacker.info->attack.damage;
    assert(P_Attack(&attacker));
    assert(building->hp == remaining_hp);
    mobj_t *sparks = (mobj_t *)thinkercap.prev;
    assert(sparks->type_id == MT_BLOOD);
    assert(!memcmp(&sparks->core.position, &building->core.position, sizeof(fixed3_t)));
    assert(!memcmp(&sparks->core.render_offset, &building->core.render_offset, sizeof(ivec2_t)));
}

static void check_fin_sequence(const char *name, int first, int last) {
    dc_fin_t fin;
    assert(DC_LoadFIN(M_va("data/DCOLONY/ANIMATE/%s.FIN", sprnames[states[first].sprite]), &fin));
    const dc_fin_label_t *label = DC_FINLabel(&fin, name);
    assert(label);
    int start = SDL_SwapLE16(label->start), end = SDL_SwapLE16(label->end);
    assert(last - first == end - start);
    int native = 0, elapsed = 0;
    for (int state = first; state <= last; ++state) {
        spritedirection_t frame = {0};
        assert(DC_FINFrame(&fin, start + state - first, &frame));
        int ticks = (uint8_t)(((frame.ticks ? frame.ticks : 15) + 3) * 15 / 100);
        bool death = states[state].group == 4;
        native += death && state == first ? 1 : ticks ? ticks : 256;
        int boundary = (native * 66 * 30 + 500) / 1000;
        assert(states[state].tics == boundary - elapsed);
        assert(states[state].frame == states[first].frame + state - first);
        assert(states[state].sprite == states[first].sprite);
        assert(states[state].nextstate == (state < last ? state + 1 : death ? S_NULL : first));
        elapsed = boundary;
        free(frame.layers);
    }
    DC_FreeFIN(&fin);
}

static void check_states(void) {
#define DC_BUILDING_LABEL(type, action, first, last) check_fin_sequence(#type #action "0", first, last);
#define DC_BUILDING_STATE(id, sprite, frame, tics, action, next, group)
#include "building_states.inc"
#undef DC_BUILDING_STATE
#undef DC_BUILDING_LABEL
    /* Boundaries come from DC.EXE comparisons, independently of state selection. */
    const int scratch[] = {S_EXCOPODSCRCH0_170, S_BRRKPODSCRCH0_301, S_ROBOPODSCRCH0_88,
        S_ROBOPOD2SCRCH0_0, S_SCNCPODSCRCH0_289, S_SCNCPOD2SCRCH0_408, S_RSCHPODSCRCH0_128};
    const int burn[] = {S_EXCOPODBURN0_186, S_BRRKPODBURN0_57, S_ROBOPODBURN0_386,
        S_ROBOPOD2BURN0_108, S_SCNCPODBURN0_72, S_SCNCPOD2BURN0_466, S_RSCHPODBURN0_149};
    for (int type = MT_EXCOPOD; type <= MT_RSCHPOD; ++type) {
        mobj_t *building = P_SpawnMobj(fixed3_zero(), type);
        assert(building);
        building->allegiance = ALLEGIANCE_PLAYER;
        building->core.render_offset = (ivec2_t){64, 47};
        int index = type - MT_EXCOPOD;
        hit(building, (building->max_hp * 11 >> 4) + 1);
        assert(building->core.state_id == mobjinfo[type].spawnstate);
        hit(building, building->max_hp * 11 >> 4);
        assert(building->core.state_id == burn[index]);
        int ticks = building->core.tics;
        P_MobjThinker(building);
        hit(building, (building->max_hp * 5 >> 4) + 1);
        assert(building->core.state_id == burn[index] && building->core.tics == ticks - 1);
        hit(building, building->max_hp * 5 >> 4);
        assert(building->core.state_id == scratch[index]);
        for (int tic = 0; tic < 200; ++tic) P_MobjThinker(building);
        assert(!building->remove && states[building->core.state_id].sprite == states[scratch[index]].sprite);
        building->hp = building->max_hp;
        A_DC_BuildingStand(building);
        assert(building->core.state_id == mobjinfo[type].spawnstate);
        if (type == MT_BRRKPOD) {
            assert(P_EnsureMobjProduction(building));
            building->production->release_active = true;
            P_SetMobjState(building, S_BRRKPOD_BUILD_TRSC1);
            hit(building, building->max_hp / 2);
            assert(building->core.state_id == S_BRRKPOD_BUILD_TRSC1);
            for (int tic = 0; tic < 200 && !building->production->release_ready; ++tic)
                P_MobjThinker(building);
            assert(building->production->release_ready && building->core.state_id == burn[index]);
        }
        hit(building, 0);
        assert(!building->remove && building->core.state_id == mobjinfo[type].deathstate);
        assert(!(building->traits & MF_SELECTABLE));
        for (int tic = 0; tic < 600 && !building->remove; ++tic) P_MobjThinker(building);
        assert(building->remove && building->core.state_id == S_NULL);
    }
    P_FreeLevel(&level);
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    check_states();
    const char *map = "data/DCOLONY/SCENARIO/HUMAN/HUMAN02.MAP";
    assert(G_DoLoadLevel(map, &level) && P_LoadThings(map) > 0);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(r_renderer);
    app_t app = {.renderer = r_renderer, .win = {640, 480}, .cell = {32, 32}};
    fvec2_t center;
    R_MapPositionToScreen(&app, &level, fixed3_from_fvec2((fvec2_t){56, 55}, 0), &center.x, &center.y);
    app.cam = fvec2_sub((fvec2_t){320, 370}, center);
    tileset_t tiles = {0};
    spritesheet_t fallback = {0};
    assert(W_LoadAssets(r_renderer, "data/DCOLONY", &level, "SPRITES/TROOPER1.SPR", &tiles, &fallback));
    spritecache_t *cache = calloc(1, sizeof(*cache));
    assert(cache && R_InitSprites(r_renderer, "data/DCOLONY", &level, NULL, 0, cache));
    mobj_t *building = NULL, *exco = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *actor = (mobj_t *)th;
        if (actor->type_id == MT_BRRKPOD && actor->team == 0) building = actor;
        if (actor->type_id == MT_EXCOPOD && actor->team == 0) exco = actor;
    }
    assert(building && exco);
    hit(building, building->max_hp - 100);
    screenshot(&app, surface, &tiles, cache, "/private/tmp/building-sparks.bmp");
    hit(building, building->max_hp / 2);
    hit(exco, exco->max_hp / 2);
    screenshot(&app, surface, &tiles, cache, "/private/tmp/building-burn.bmp");
    for (int tic = 0; tic < 100; ++tic) {
        P_MobjThinker(building);
        P_MobjThinker(exco);
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *actor = (mobj_t *)th;
            if (actor->type_id == MT_BLOOD && !actor->remove) P_MobjThinker(actor);
        }
    }
    screenshot(&app, surface, &tiles, cache, "/private/tmp/building-burn-idle.bmp");
    hit(building, building->max_hp / 4);
    hit(exco, exco->max_hp / 4);
    screenshot(&app, surface, &tiles, cache, "/private/tmp/building-scratch.bmp");
    hit(building, 0);
    hit(exco, 0);
    for (int tic = 0; tic < 30; ++tic) {
        P_MobjThinker(building);
        P_MobjThinker(exco);
    }
    screenshot(&app, surface, &tiles, cache, "/private/tmp/building-explosion.bmp");
    R_FreeSpriteCache(cache);
    free(cache);
    R_FreeSprite(&fallback);
    R_FreeTileset(&tiles);
    SDL_DestroyRenderer(r_renderer);
    r_renderer = NULL;
    SDL_FreeSurface(surface);
    P_FreeLevel(&level);
    puts("PASS: native building damage thresholds, persistent fire, production handoff, sparks and death");
    return 0;
}
