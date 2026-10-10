#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include <assert.h>

static void button(app_t *app, const spritecache_t *cache, const mobjlist_t *objects,
                   Uint32 type, ivec2_t point) {
    SDL_Event event = {.button = {.type = type, .button = SDL_BUTTON_LEFT,
                                  .x = point.x, .y = point.y}};
    G_Responder(app, &level, objects->items, objects->count, NULL, cache, gameinfo, &event);
}

static void check_sarge(app_t *app, const spritecache_t *cache,
                        const mobjlist_t *objects, mobj_t *sarge) {
    fixed2_t start = fixed3_xy(sarge->core.position);
    assert(start.x == FIXED_LIT(68.5) && start.y == FIXED_LIT(76.5) && sarge->owner == 0);
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
    fixed2_t goal = FIXED2_LIT(73.5, 75.5);
    assert(G_SelectedTiccmd(TC_MOVE, objects->items, objects->count, goal, 0));
    ticcmd_t command;
    G_BuildTiccmd(&command);
    assert(command.order == TC_MOVE && command.count == 1 && command.units[0] == sarge->id);
    G_RunTiccmd(0, &command);
    netactive = false;
    for (int tic = 0; tic < 450 && !sarge->movement.order_arrived; ++tic) P_Ticker();
    assert(sarge->movement.order_arrived);
    assert(fixed2_distance_squared64(fixed3_xy(sarge->core.position), goal) < FIXED_LIT_64(0.01));
    assert(!L_IsWalkable(&level, 68, 76)); /* Escape never edits terrain. */
    assert(!P_CheckPosition(&level, sarge, start));
    assert(!P_TryMove(sarge, fixed3_from_fixed2(start, 0))); /* Cannot re-enter. */
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
    app_t view = {.win = {640,480}, .cell = {32,32}};
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
        V_BeginFrame(0xff282828u);
        R_RenderPlayerView(&view, &level, NULL, &osprey, 1, NULL, cache, gameinfo, 0);
        V_ReadPixels(surface->pixels, surface->pitch);
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
    mobj_t *ground = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(72.5, 76.5),0), MT_TROOPER);
    assert(ground);
    mobj_t *group[] = {ground, osprey};
    fixed2_t goal = FIXED2_LIT(68.5, 76.5);
    P_MoveUnitsAt(&level, group, 2, goal);
    assert(!osprey->movement.path.count && osprey->movement.order_id);
    assert(fixed2_distance_squared64(osprey->movement.goal, goal) == 0);
    for (int tic = 0; tic < 180 && !osprey->movement.order_arrived; ++tic) P_Ticker();
    assert(osprey->movement.order_arrived);
    assert(fixed2_distance_squared64(fixed3_xy(osprey->core.position), goal) < FIXED_LIT_64(0.01));
    assert(osprey->core.position.z == start.z);
    assert(states[osprey->core.state_id].group == 1);
    P_RemoveMobj(ground);

    /* Aircraft-only orders need no walkable goal anywhere on the map. */
    P_NavFree(&level);
    memset(level.blocked, 1, (size_t)level.width * level.height);
    goal = FIXED2_LIT(70.5, 76.5);
    P_MoveUnitsAt(&level, &osprey, 1, goal);
    assert(!osprey->movement.path.count && !level.nav);
    for (int tic = 0; tic < 180 && !osprey->movement.order_arrived; ++tic) P_Ticker();
    assert(osprey->movement.order_arrived);
    assert(fixed2_distance_squared64(fixed3_xy(osprey->core.position), goal) < FIXED_LIT_64(0.01));
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
    V_AllocScreen(surface->w, surface->h);
    app_t app = {.win = {640,480}, .cell = {32,32}};
    spritecache_t *cache = calloc(1, sizeof(*cache));
    assert(R_InitSprites("data/DCOLONY", &level, NULL, 0, cache));
    mobjlist_t objects = P_ListMobjs();
    assert(objects.count == 21);
    mobj_t *sarge = NULL, *osprey = NULL;
    for (int i = 0; i < objects.count; ++i) {
        mobj_t *u = objects.items[i];
        assert(u->type_id != MT_EXPLOITER && u->type_id != MT_SLUG);
        if (u->type_id == MT_CYBORG) sarge = u;
        if (u->type_id == MT_SCOUT) osprey = u;
        if (u->type_id == MT_EXCOPOD || u->type_id == MT_ALIEN_MINDHIVE) {
            fixed2_t at = fixed3_xy(u->core.position);
            assert(at.x == (u->owner ? 23 : 65) * FIXED_ONE);
            assert(at.y == (u->owner ? FIXED_LIT(5.46875) : FIXED_LIT(74.46875)));
        }
    }
    assert(sarge && osprey);
    check_sarge(&app, cache, &objects, sarge);
    check_osprey(cache, osprey, surface);
    P_FreeMobjList(&objects);
    R_FreeSpriteCache(cache); free(cache);
    rts_game_model_destroy(model);
    V_FreeScreen();
    SDL_FreeSurface(surface);
    puts("PASS: authored multiplayer forces, Sarge drag/move, native Osprey hover and flight orders");
    return 0;
}
