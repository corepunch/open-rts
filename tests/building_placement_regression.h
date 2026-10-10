#ifndef __BUILDING_PLACEMENT_REGRESSION__
#define __BUILDING_PLACEMENT_REGRESSION__
#include "t_local.h"
#include "info.h"
#define CHECK(c) RTS_CHECK(c, "building placement", #c)

int main(void) {
    G_InitGame();
    P_InitThinkers();
    level.width = level.height = 64;
    level.blocked = calloc(64 * 64, 1);
    CHECK(level.blocked);
    consoleplayer = 0;
    level.player_resources[0][0] = 100000;
    mobj_t *builder = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(5, 5), 0), BUILDER);
    mobj_t *second = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(6, 5), 0), BUILDER);
    CHECK(builder && second);
    P_MobjSetSelected(builder, true);
    const StaticProductDefinition *product = G_ModelProductByClassType(NULL, RTS_PRODUCT_BUILDING, PRODUCT);
    CHECK(product);
    uint16_t type = G_ModelActorIdForProduct(product);
    const mobjtype_t *actor = P_ActorType(type);
    CHECK(actor && actor->footprint.w > 0 && actor->footprint.h > 0);
    for (int i = 0; i < num_actor_types; ++i)
        CHECK(!actor_types[i].foundation || strlen(actor_types[i].foundation) ==
              (size_t)actor_types[i].footprint.w * actor_types[i].footprint.h);
    ivec2_t site = {20, 20};
    CHECK(P_CanPlaceBuilding(type, site, builder));
    CHECK(!P_CanPlaceBuilding(type, (ivec2_t){63, 63}, builder));
    /* Find a required foundation cell (DR canvases have empty upper rows). */
    ivec2_t occupied = site;
    for (int y = 0; y < actor->footprint.h; ++y)
        for (int x = 0; x < actor->footprint.w; ++x)
            if (!actor->foundation || actor->foundation[y * actor->footprint.w + x] == 'x')
                occupied = ivec2_add(site, (ivec2_t){x, y});
    level.blocked[L_Index(&level, occupied.x, occupied.y)] = 1;
    CHECK(!P_CanPlaceBuilding(type, site, builder));
    level.blocked[L_Index(&level, occupied.x, occupied.y)] = 0;
    second->core.position = fixed3_from_fixed2(fixed2_cell_center(occupied), 0);
    CHECK(!P_CanPlaceBuilding(type, site, builder));
    second->traits |= MF_FLY;
    CHECK(P_CanPlaceBuilding(type, site, builder));
    second->traits &= ~MF_FLY;
    second->core.position = fixed3_from_fixed2(FIXED2_LIT(6, 5), 0);

    app_t app = {.win = {640, 480}, .cell = {8, 8}};
    menuitem_t item = {.visible = true, .enabled = true, .routine = HU_PlaceProduct};
    menu_t menu = {.items = &item, .numitems = 1, .app = &app};
    HU_InitProducts(&menu, g_game_default_root);
    int cash = level.player_resources[0][0];
    HU_BuildProduct(&menu, &item, builder, product);
    CHECK(menu.target == &item && !builder->production);
    SDL_Event escape = {.type = SDL_KEYDOWN};
    escape.key.keysym.sym = SDLK_ESCAPE;
    M_MenuResponder(&menu, &app, &escape);
    CHECK(!menu.target && level.player_resources[0][0] == cash);

    HU_BuildProduct(&menu, &item, builder, product);
    fvec2_t screen;
    R_MapToScreen(&app, &level, site.x + .5f, site.y + .5f, &screen.x, &screen.y);
    menu.cursor = (ivec2_t){(int)screen.x, (int)screen.y};
    menu.target = NULL; /* The target responder clears this before calling. */
    level.blocked[L_Index(&level, occupied.x, occupied.y)] = 1;
    HU_PlaceProduct(&menu, &item, MA_TARGET);
    CHECK(menu.target == &item && !builder->production && level.player_resources[0][0] == cash);
    level.blocked[L_Index(&level, occupied.x, occupied.y)] = 0;
    menu.target = NULL;
    netactive = true;
    HU_PlaceProduct(&menu, &item, MA_TARGET);
    CHECK(!menu.target && !builder->production && level.player_resources[0][0] == cash);
    ticcmd_t cmd;
    G_BuildTiccmd(&cmd);
    CHECK(cmd.order == TC_CONSTRUCT);
    G_RunTiccmd(0, &cmd);
    netactive = false;
    CHECK(builder->production && builder->production->placed);
    CHECK(ivec2_equal(builder->production->cell, site));
    CHECK(level.player_resources[0][0] == cash - product->cost);
    CHECK(!G_PlaceProduct(second, product, site)); /* Reserved by first builder. */
    CHECK(G_ProductionTicker(G_ModelProductTrainingTimeMs(product) + 1000));
    mobj_t *building = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && ((mobj_t *)th)->type_id == type) building = (mobj_t *)th;
    CHECK(building && !builder->production);
    irect_t cells = P_MobjCells(building);
    CHECK(cells.x == site.x && cells.y == site.y);
    CHECK(!P_CanPlaceBuilding(type, site, builder));
#ifdef RTS_GAME_7LEGION
    CHECK(!L_IsWalkable(&level, occupied.x, occupied.y));
#endif
    P_RemoveMobj(building);
    P_SyncBuildingBlocking();
    CHECK(P_CanPlaceBuilding(type, site, builder));
    CHECK(L_IsWalkable(&level, occupied.x, occupied.y));
    P_FreeLevel(&level);
    puts("PASS: building terrain, units, reservation, cancel, lockstep, completion and destruction");
    return 0;
}
#endif
