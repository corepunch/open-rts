#ifndef __PALETTE_REGRESSION__
#define __PALETTE_REGRESSION__
#include "game.h"
#include "sb_bar.h"
#include "info.h"
#include "rts_test.h"
#include <assert.h>

static mobj_t *spawn(uint16_t type, int owner) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){20,20},0), type);
    assert(u);
    u->owner = u->team = owner;
    return u;
}
static void click(sb_state_t *bar, app_t *app, ivec2_t pos) {
    SDL_Event e = {.type = SDL_MOUSEBUTTONDOWN};
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = pos.x * app->win.w / gameui->logical_width;
    e.button.y = pos.y * app->win.h / gameui->logical_height;
    assert(SB_ProductionResponder(bar, app, &e));
}
int main(void) {
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    level.width = level.height = 96;
    level.blocked = calloc(96*96,1);
    assert(level.blocked);
    P_InitThinkers();
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0,960,720,32,SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    app_t app = {.renderer = SDL_CreateSoftwareRenderer(surface), .win = {960,720}, .cell = {32,32}};
    assert(app.renderer);
    sb_state_t bar;
    assert(SB_Init(&bar,app.renderer,g_game_default_root,gameui));
    level.player_resources[0][0] = 10000;
    const StaticProductDefinition *p = G_ModelProductByUIId(NULL, TEST_PRODUCT);
    assert(p);
    int category = -1, slot = 0;
    for (int i = 0; i < gameui->product_count; ++i)
        if (gameui->products[i].id == TEST_PRODUCT) { category = gameui->products[i].category; break; }
    assert(category >= 0);
    for (int i = 0; i < gameui->product_count; ++i) {
        if (gameui->products[i].id == TEST_PRODUCT) break;
        slot += gameui->products[i].category == category;
    }
    const irect_t cat = gameui->categories[category].rect;
    bar.production_category = -1;
    click(&bar,&app,(ivec2_t){cat.x+4,cat.y+4});
    assert(bar.production_category == category);
    ivec2_t item = {gameui->command_grid.x+slot%gameui->command_columns*gameui->icon_size.w+8,
        gameui->command_grid.y+slot/gameui->command_columns*gameui->icon_size.h+8};
    click(&bar,&app,item);
    assert(level.player_resources[0][0] == 10000); /* No producer. */
    spawn(TEST_MAKER,1);
    assert(!G_ModelProductAvailable(NULL,0,p)); /* Enemy producer cannot unlock it. */
    mobj_t *maker = spawn(TEST_MAKER,0);
#ifdef TEST_PREREQUISITE
    assert(!G_ModelProductAvailable(NULL,0,p));
    mobj_t *prerequisite = spawn(TEST_PREREQUISITE,0);
    assert(G_ModelProductAvailable(NULL,0,p));
    prerequisite->hp = 0;
    assert(!G_ModelProductAvailable(NULL,0,p));
    prerequisite->hp = prerequisite->max_hp;
#else
    StaticProductDefinition restricted = *p;
    restricted.prerequisites[0] = MT_SURV_RESEARCH_LAB;
    restricted.prerequisite_count = 1;
    assert(!G_ModelProductAvailable(NULL,0,&restricted));
    mobj_t *prerequisite = spawn(MT_SURV_RESEARCH_LAB,0);
    assert(G_ModelProductAvailable(NULL,0,&restricted));
    prerequisite->hp = 0;
    assert(!G_ModelProductAvailable(NULL,0,&restricted));
#endif
    assert(G_ModelProductAvailable(NULL,0,p));
    click(&bar,&app,item); /* Category queue works without selecting the maker. */
    assert(maker->production && maker->production->queue_count == 1);
    assert(level.player_resources[0][0] == 10000-p->cost);
    level.player_resources[0][0] = 0;
    click(&bar,&app,item);
    assert(maker->production->queue_count == 1);
    assert(G_ProductionTicker(0.1f) == false);
    SB_Drawer(&bar,&app,&level,NULL,0,NULL,false,true);
    SB_ProductionDrawer(&bar,&app);
    char path[128];
    snprintf(path,sizeof(path),"/private/tmp/open-rts-palette-%s.bmp",g_game_id);
    assert(SDL_SaveBMP(surface,path) == 0);
    /* Every declared icon loads, including locked products and later pages. */
    for (int i = 0; i < gameui->product_count; ++i) assert(bar.product_icons[i].numlumps > 0);
    SB_Shutdown(&bar);
    SDL_DestroyRenderer(app.renderer);
    SDL_FreeSurface(surface);
    P_FreeLevel(&level);
    SDL_Quit();
    puts("PASS: palette assets, categories, prerequisites, ownership, money and queue dispatch");
    return 0;
}
#endif
