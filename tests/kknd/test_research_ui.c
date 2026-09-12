#include "game.h"
#include "kknd.h"
#include "info.h"
#include "sb_bar.h"
#include "d_net.h"
#include <assert.h>

static mobj_t *spawn(int type, fvec2_t position) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fvec2(position,0),type);
    assert(u);
    return u;
}
static bool click(sb_state_t *st, app_t *app, int button, ivec2_t p) {
    SDL_Event event = {.type = SDL_MOUSEBUTTONDOWN};
    event.button.button = button;
    event.button.x = p.x;
    event.button.y = p.y;
    return SB_ProductionResponder(st,app,&event);
}
int main(void) {
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    P_InitThinkers();
    level.width = level.height = 32;
    level.blocked = calloc(32*32,1);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0,960,720,32,SDL_PIXELFORMAT_RGBA32);
    app_t app = {.renderer=SDL_CreateSoftwareRenderer(surface),.win={960,720},.cell={32,32}};
    assert(app.renderer);
    sb_state_t st;
    assert(SB_Init(&st,app.renderer,g_game_default_root,gameui));
    mobj_t *barracks = spawn(MT_SURV_BARRACKS,(fvec2_t){10,10});
    assert(click(&st,&app,SDL_BUTTON_LEFT,(ivec2_t){930,450}));
    assert(!st.order); /* No lab yet. */
    mobj_t *lab = spawn(MT_SURV_RESEARCH_LAB,(fvec2_t){15,15});
    assert(click(&st,&app,SDL_BUTTON_LEFT,(ivec2_t){930,450}));
    assert(st.order == UI_PRODUCT);
    assert(click(&st,&app,SDL_BUTTON_LEFT,(ivec2_t){320,320}));
    assert(lab->research.target == barracks->id && !st.order);
    assert(click(&st,&app,SDL_BUTTON_LEFT,(ivec2_t){930,450}));
    assert(click(&st,&app,SDL_BUTTON_RIGHT,(ivec2_t){320,320}));
    assert(!st.order && lab->research.target == barracks->id);
    assert(click(&st,&app,SDL_BUTTON_LEFT,(ivec2_t){930,450}));
    assert(click(&st,&app,SDL_BUTTON_LEFT,(ivec2_t){320,320}));
    assert(!lab->research.target); /* Repeating order cancels research. */
    StaticProductDefinition products[64];
    int count = G_ModelGetProducts(NULL,0,products,64);
    assert(count == 27);
    for (int i=0;i<count;++i) assert(products[i].faction == 0);
    mobj_t *warriors = spawn(MT_MUTE_WARRIOR_HALL,(fvec2_t){20,20});
    warriors->owner = 1;
    count = G_ModelGetProducts(NULL,1,products,64);
    assert(count == 28);
    for (int i=0;i<count;++i) assert(products[i].faction == 1);
    consoleplayer = 1;
    st.production_category = 0;
    level.player_resources[1][0] = 1000;
    SB_ProductionDrawer(&st,&app);
    assert(SDL_SaveBMP(surface,"/private/tmp/open-rts-kknd-evolved.bmp") == 0);
    SB_Shutdown(&st);
    SDL_DestroyRenderer(app.renderer);
    SDL_FreeSurface(surface);
    P_FreeLevel(&level);
    SDL_Quit();
    puts("PASS: research button targeting, right cancel, repeated order and faction palettes");
    return 0;
}
