#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "rustfront build", #c)

static const char *const MAP = "games/rustfront/maps/skirmish.txt";
static app_t app = {.win = {640, 480}, .cell = {48, 32}, .running = true};
static tileset_t tiles;
static spritecache_t cache;
static spritesheet_t fallback;

static void tick(int tics) {
    for (int t = 0; t < tics; ++t) {
        P_Ticker();
        G_ProductionTicker(RTS_TICK_MS);
        mobjlist_t list = P_ListMobjs();
        int n = list.count;
        G_MissionTicker(&level, list.items, &n, NULL, RTS_TICK_MS);
        P_FreeMobjList(&list);
    }
}

static int count_type(uint16_t type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        n += th->function == P_MobjThinker && ((mobj_t *)th)->type_id == type && !((mobj_t *)th)->remove;
    return n;
}

static mobj_t *find_type(uint16_t type, int owner) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->type_id == type && u->owner == owner && !u->remove) return u;
    }
    return NULL;
}

/* OPEN_RTS_RUSTFRONT_SHOTS=dir saves what the game draws. */
static void shot(const char *name) {
    const char *dir = getenv("OPEN_RTS_RUSTFRONT_SHOTS");
    if (!dir) return;
    P_UpdateSight();
    mobjlist_t list = P_ListMobjs();
    V_BeginFrame(0xff000000u);
    R_DrawLevel(&app, &level, &tiles);
    R_RenderPlayerView(&app, &level, &tiles, list.items, list.count, &fallback, &cache, gameinfo, 0);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface) {
        V_ReadPixels(surface->pixels, surface->pitch);
        char path[1024];
        snprintf(path, sizeof(path), "%s/rustfront-%s.bmp", dir, name);
        SDL_SaveBMP(surface, path);
        SDL_FreeSurface(surface);
    }
    P_FreeMobjList(&list);
}

/* A structure stands on its site from placement, steps through its construction stages as
 * its hit points grow, and only then shoots (a turret) or takes ore (a refinery). Its side
 * builds one structure at a time. */
static int test_construction(void) {
    P_FreeLevel(&level);
    P_InitThinkers();
    CHECK(G_DoLoadLevel(MAP, &level) && P_InitSight());
    CHECK(P_LoadThings(MAP) > 0);
    CHECK(W_LoadAssets(g_game_default_root, &level, NULL, &tiles, &fallback));
    R_SetLevelPalette(&tiles);
    V_AllocScreen(640, 480);
    mobjlist_t list = P_ListMobjs();
    CHECK(R_InitSprites(g_game_default_root, &level, list.items, list.count, &cache));
    P_FreeMobjList(&list);
    level.player_resources[0][0] = 10000;
    mobj_t *hq = find_type(MT_HQ, 0);
    CHECK(hq);
    int factories = count_type(MT_FACTORY), turrets = count_type(MT_GUN_TURRET);
    const StaticProductDefinition *factory = G_ModelProductByUIId(NULL, MT_FACTORY);
    const StaticProductDefinition *turret = G_ModelProductByUIId(NULL, MT_GUN_TURRET);
    CHECK(G_PlaceProduct(hq, factory, (ivec2_t){2, 19}));
    mobj_t *site = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && RF_IsSite((mobj_t *)th)) site = (mobj_t *)th;
    CHECK(site && site->type_id == MT_FACTORY && count_type(MT_FACTORY) == factories + 1);
    CHECK(site->core.state_id == S_FACTORY_BUILD && site->core.state_frame == 0);
    CHECK(site->hp == site->max_hp / 10);
    CHECK(level.player_resources[0][0] == 10000 - factory->cost);
    CHECK(level.blocked[L_Index(&level, 3, 20)]);
    CHECK(!G_PlaceProduct(hq, turret, (ivec2_t){8, 19}));           /* one site at a time */
    CHECK(count_type(MT_GUN_TURRET) == turrets);
    app.cam.x = 0;
    app.cam.y = -14 * 32;
    tick(1);
    CHECK(site->core.state_id == S_FACTORY_BUILD && site->core.state_frame == 0);
    shot("build-0");
    int time = G_ModelProductTrainingTimeMs(factory);
    int hp = site->hp, stage = 0;
    for (int part = 1; part <= 3; ++part) {
        tick(time / 4 / RTS_TICK_MS + 1);
        CHECK(site->core.state_id == S_FACTORY_BUILD);
        CHECK(site->core.state_frame > stage && site->hp > hp);
        stage = site->core.state_frame;
        hp = site->hp;
        char name[16];
        snprintf(name, sizeof(name), "build-%d", part);
        shot(name);
    }
    tick(time / 4 / RTS_TICK_MS + 2);
    CHECK(!RF_IsSite(site) && site->core.state_id >= S_FACTORY && site->core.state_id <= S_FACTORY_DAMAGED);
    CHECK(site->hp == site->max_hp);

    /* A turret site cannot shoot until it is finished. */
    CHECK(G_PlaceProduct(hq, turret, (ivec2_t){8, 19}));
    mobj_t *gun = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && RF_IsSite((mobj_t *)th)) gun = (mobj_t *)th;
    CHECK(gun && gun->type_id == MT_GUN_TURRET && gun->core.state_id == S_GUN_TURRET_BUILD);
    CHECK(!(gun->traits & MF_ATTACK));
    tick(G_ModelProductTrainingTimeMs(turret) / RTS_TICK_MS + 2);
    CHECK((gun->traits & (MF_ATTACK | MF_TURRET)) == (MF_ATTACK | MF_TURRET));
    puts("PASS: a structure stands on its site, rises through four stages and earns its traits");
    return 0;
}

/* Under half its hit points a structure shows its scorched loop and smokes; under a quarter
 * it burns too. Repaired, it is clean again. A badly hurt vehicle smokes. */
static int test_damage(void) {
    mobj_t *factory = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->type_id == MT_FACTORY && u->owner == 0 && !RF_IsSite(u))
            factory = u;
    }
    CHECK(factory);
    factory->hp = factory->max_hp * 2 / 5;
    tick(30);
    CHECK(factory->core.state_id == S_FACTORY_DAMAGED);
    CHECK(count_type(MT_FX_PLUME) > 0 && count_type(MT_FX_FIRE) == 0);
    factory->hp = factory->max_hp / 5;
    tick(30);
    CHECK(count_type(MT_FX_FIRE) > 0);
    mobj_t *tank = find_type(MT_TANK, 0);
    CHECK(tank);
    tank->hp = tank->max_hp / 4;
    app.cam.x = 0;
    app.cam.y = -14 * 32;
    tick(15);
    shot("damage");
    factory->hp = factory->max_hp;
    tick(2);
    CHECK(factory->core.state_id == S_FACTORY);
    R_FreeSpriteCache(&cache);
    R_FreeTileset(&tiles);
    V_FreeScreen();
    P_FreeLevel(&level);
    puts("PASS: hurt structures scorch, smoke and burn; repaired ones are clean; hurt vehicles smoke");
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RTS_RUN(test_construction());
    RTS_RUN(test_damage());
    SDL_Quit();
    return 0;
}
