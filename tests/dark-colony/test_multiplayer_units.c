#include "engine.h"
#include "game.h"
#include "g_game.h"
#include "info.h"
#include "d_net.h"
#include "d_ticcmd.h"
#include "p_local.h"
#include "w_spr.h"
#include <assert.h>

static void button(app_t *app, const spritecache_t *cache, const mobjlist_t *objects,
                   Uint32 type, ivec2_t point) {
    SDL_Event event = {.button = {.type = type, .button = SDL_BUTTON_LEFT,
                                  .x = point.x, .y = point.y}};
    G_Responder(app, &level, objects->items, objects->count, NULL, cache, gameinfo, &event);
}

static void check_sarge(app_t *app, const spritecache_t *cache,
                        const mobjlist_t *objects, mobj_t *sarge) {
    fvec2_t start = fixed3_xy_to_fvec2(sarge->core.position);
    assert(start.x == 68.5f && start.y == 76.5f && sarge->owner == 0);
    assert(!L_IsWalkable(&level, 68, 76));
    blob_t path;
    assert(W_ReadFile("data/DCOLONY/SCENARIO/MPLAYER/D2PLAY01.PTH", &path));
    assert(path.size == 0x10000 + 96 * 84 && path.bytes[0x10000 + 76 * 96 + 68] == 0);
    W_FreeFile(&path);
    float sx, sy;
    R_MapPositionToScreen(app, &level, sarge->core.position, &sx, &sy);
    app->cam = fvec2_add(app->cam, (fvec2_t){256-sx, 240-sy});
    button(app, cache, objects, SDL_MOUSEBUTTONDOWN, (ivec2_t){236,190});
    button(app, cache, objects, SDL_MOUSEBUTTONUP, (ivec2_t){276,255});
    assert(P_MobjIsSelected(sarge));
    G_ClearTiccmds();
    netactive = true;
    fvec2_t goal = {73.5f, 75.5f};
    assert(G_SelectedTiccmd(TC_MOVE, objects->items, objects->count, goal, 0));
    ticcmd_t command;
    G_BuildTiccmd(&command);
    assert(command.order == TC_MOVE && command.count == 1 && command.units[0] == sarge->id);
    G_RunTiccmd(0, &command);
    netactive = false;
    for (int tic = 0; tic < 450 && !sarge->movement.order_arrived; ++tic) P_Ticker();
    assert(sarge->movement.order_arrived);
    assert(fvec2_distance_squared(fixed3_xy_to_fvec2(sarge->core.position), goal) < 0.01f);
    assert(!L_IsWalkable(&level, 68, 76)); /* Escape never edits terrain. */
    assert(!P_CheckPosition(&level, sarge, start.x, start.y));
    assert(!P_TryMove(sarge, fixed3_from_fvec2(start, 0))); /* Cannot re-enter. */
}

static void check_osprey(const spritecache_t *cache, mobj_t *osprey, SDL_Surface *surface) {
    assert(osprey->traits & MF_FLY);
    assert(actor_type_by_id(MT_ORTU)->traits & MF_FLY);
    assert(osprey->core.position.z == mobjinfo[MT_DROPSHIP].spawnz);
    mobj_t *ortu = P_SpawnMobj(fixed3_zero(), MT_ORTU);
    assert(ortu && ortu->core.position.z == mobjinfo[MT_DROPSHIP].spawnz);
    P_RemoveMobj(ortu);
    P_Ticker();
    const spritesheet_t *sprite = R_StateSprite(cache, gameinfo, SPR_SCGM, NULL);
    assert(sprite);
    dc_fin_t fin;
    assert(DC_LoadFIN("data/DCOLONY/ANIMATE/SCGM.FIN", &fin));
    const dc_fin_label_t *stand = DC_FINLabel(&fin, "SCGMSTAND0");
    assert(stand && SDL_SwapLE16(stand->start) == 32 && SDL_SwapLE16(stand->end) == 35);
    P_SetMobjState(osprey, S_SCGM_STND);
    fixed3_t start = osprey->core.position;
    app_t view = {.renderer = r_renderer, .win = {640,480}, .cell = {32,32}};
    fvec2_t anchor;
    R_MapPositionToScreen(&view, &level, start, &anchor.x, &anchor.y);
    view.cam = fvec2_sub((fvec2_t){320,240}, anchor);
    SDL_Surface *strip = SDL_CreateRGBSurfaceWithFormat(0, 512, 128, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(strip);
    for (int frame = 0; frame < 4; ++frame) {
        assert(osprey->core.frame == sprite->numlumps + 32 + frame);
        const spriteframe_t *pose = &sprite->spritedef.spriteframes[osprey->core.frame];
        assert(pose->rotations == 8);
        const spritelayer_t *body = pose->directions[4].layers; /* Native direction 0. */
        while (strcmp(body->sprite_name, ".")) { assert(body->sprite_name[0]); ++body; }
        static const int native_y[] = {23,24,23,22};
        assert(body->offset.y == native_y[frame]);
        SDL_SetRenderDrawColor(r_renderer, 40,40,40,255);
        SDL_RenderClear(r_renderer);
        R_RenderPlayerView(&view, &level, NULL, &osprey, 1, NULL, cache, gameinfo, 0);
        assert(SDL_BlitSurface(surface, &(SDL_Rect){256,176,128,128}, strip,
                               &(SDL_Rect){frame*128,0,128,128}) == 0);
        for (int tic = 0; tic < 4; ++tic) P_Ticker();
    }
    assert(SDL_SaveBMP(strip, "/private/tmp/dc-osprey-idle.bmp") == 0);
    SDL_FreeSurface(strip);
    assert(osprey->core.state_id == S_SCGM_STND);
    assert(!memcmp(&start, &osprey->core.position, sizeof(start)));
    DC_FreeFIN(&fin);

    /* A mixed selection keeps the aircraft's exact blocked destination. */
    mobj_t *ground = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){72.5f,76.5f},0), MT_TROOPER);
    assert(ground);
    mobj_t *group[] = {ground, osprey};
    fvec2_t goal = {68.5f,76.5f};
    P_MoveUnitsAt(&level, group, 2, goal);
    assert(!osprey->movement.flow_field && osprey->movement.order_id);
    assert(fvec2_distance_squared(osprey->movement.goal, goal) == 0);
    for (int tic = 0; tic < 180 && !osprey->movement.order_arrived; ++tic) P_Ticker();
    assert(osprey->movement.order_arrived);
    assert(fvec2_distance_squared(fixed3_xy_to_fvec2(osprey->core.position), goal) < 0.01f);
    assert(osprey->core.position.z == start.z);
    assert(states[osprey->core.state_id].group == 1);
    P_RemoveMobj(ground);

    /* Aircraft-only orders need no walkable goal anywhere on the map. */
    P_FreeFlowFields(&level);
    memset(level.blocked, 1, (size_t)level.width * level.height);
    goal = (fvec2_t){70.5f,76.5f};
    P_MoveUnitsAt(&level, &osprey, 1, goal);
    assert(!osprey->movement.flow_field && !level.flow_fields);
    for (int tic = 0; tic < 180 && !osprey->movement.order_arrived; ++tic) P_Ticker();
    assert(osprey->movement.order_arrived);
    assert(fvec2_distance_squared(fixed3_xy_to_fvec2(osprey->core.position), goal) < 0.01f);
}

int main(void) {
    netgame = true;
    doomcom->numplayers = 2;
    consoleplayer = 0;
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY",
        .map_path = "SCENARIO/MPLAYER/D2PLAY01.MAP"};
    assert(model && rts_game_model_load(model, &config));
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    app_t app = {.renderer = r_renderer, .win = {640,480}, .cell = {32,32}};
    spritecache_t *cache = calloc(1, sizeof(*cache));
    assert(R_InitSprites(r_renderer, "data/DCOLONY", &level, NULL, 0, cache));
    mobjlist_t objects = P_ListMobjs();
    assert(objects.count == 21);
    mobj_t *sarge = NULL, *osprey = NULL;
    for (int i = 0; i < objects.count; ++i) {
        mobj_t *u = objects.items[i];
        assert(u->type_id != MT_EXPLOITER && u->type_id != MT_SLUG);
        if (u->type_id == MT_CYBORG) sarge = u;
        if (u->type_id == MT_SCOUT) osprey = u;
        if (u->type_id == MT_EXCOPOD || u->type_id == MT_ALIEN_MINDHIVE) {
            fvec2_t at = fixed3_xy_to_fvec2(u->core.position);
            assert(at.x == (u->owner ? 23.0f : 65.0f));
            assert(at.y == (u->owner ? 5.46875f : 74.46875f));
        }
    }
    assert(sarge && osprey);
    check_sarge(&app, cache, &objects, sarge);
    check_osprey(cache, osprey, surface);
    P_FreeMobjList(&objects);
    R_FreeSpriteCache(cache); free(cache);
    rts_game_model_destroy(model);
    R_FreeSpriteBuffer(); SDL_DestroyRenderer(r_renderer); r_renderer = NULL;
    SDL_FreeSurface(surface);
    puts("PASS: authored multiplayer forces, Sarge drag/move, native Osprey hover and flight orders");
    return 0;
}
