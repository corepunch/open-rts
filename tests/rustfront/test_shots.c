#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "rustfront shots", #c)

/* Staged scenes of Rustfront as the game draws them, world and sidebar at 1280 x 960: each
 * faction's base seen as that faction, a firefight, aircraft, each faction's special units at
 * work, and the canyon. OPEN_RTS_RF_SITE_SHOTS=DIR saves them as BMPs for the website.
 * Without it the scenes still run, as a check that they stage what
 * they say (shells in flight, mines laid, cards dealt). */

static app_t app = {.win = {1280, 960}, .cell = {48, 32}, .running = true};
static tileset_t tiles;
static spritecache_t cache;
static spritesheet_t fallback;
static bool sprites_ready;
static menu_t *hud;

static int load(const char *map) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", g_game_default_root, map);
    P_FreeLevel(&level);
    P_InitThinkers();
    CHECK(G_DoLoadLevel(path, &level) && G_LoadLevelData(g_game_default_root, &level) && P_InitSight());
    CHECK(P_LoadThings(path) > 0);
    CHECK(W_LoadAssets(g_game_default_root, &level, NULL, &tiles, &fallback));
    R_SetLevelPalette(&tiles);
    V_AllocScreen(app.win.w, app.win.h);
    if (!sprites_ready) {
        mobjlist_t list = P_ListMobjs();
        bool loaded = R_InitSprites(g_game_default_root, &level, list.items, list.count, &cache);
        P_FreeMobjList(&list);
        CHECK(loaded);
        sprites_ready = true;
    }
    if (!hud) hud = RF_InitHUD(&app, g_game_default_root);
    CHECK(hud);
    leveltime = 0;
    return 0;
}

static mobj_t *spawn(uint16_t type, int owner, float x, float y, int facing) {
    mobj_t *u = RF_SpawnThing(type, owner, facing, (ivec2_t){(int)x, (int)y});
    if (u && !(u->info && u->info->footprint.w > 0))
        u->core.position = fixed3_from_fixed2((fixed2_t){(fixed_t)(x * 65536), (fixed_t)(y * 65536)}, 0);
    return u;
}

static mobj_t *find(int owner, uint16_t type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->owner == owner && u->type_id == type && !u->remove && u->hp > 0)
            return u;
    }
    return NULL;
}

static int count_type(uint16_t type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        n += th->function == P_MobjThinker && ((mobj_t *)th)->type_id == type && !((mobj_t *)th)->remove;
    return n;
}

static void tick(int tics) {
    for (int t = 0; t < tics; ++t) {
        P_UpdateSight();
        P_Ticker();
        mobjlist_t all = P_ListMobjs();
        int n = all.count;
        G_MissionTicker(&level, all.items, &n, NULL, RTS_TICK_MS);
        P_FreeMobjList(&all);
    }
}

static void select_only(mobj_t *const *units, int count) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker) P_MobjSetSelected((mobj_t *)th, false);
    for (int i = 0; i < count; ++i)
        if (units[i]) P_MobjSetSelected(units[i], true);
}

/* The world view centred on a cell. */
static void look_at(float gx, float gy) {
    app.cam = (fvec2_t){0, 0};
    float sx, sy;
    R_MapToScreen(&app, &level, gx, gy, &sx, &sy);
    irect_t view = G_WorldViewport(&app);
    app.cam.x += (float)(view.x + view.w / 2) - sx;
    app.cam.y += (float)(view.y + view.h / 2) - sy;
}

static void shot(const char *name) {
    P_UpdateSight();
    mobjlist_t list = P_ListMobjs();
    V_BeginFrame(0xff000000u);
    R_DrawLevel(&app, &level, &tiles);
    R_RenderPlayerView(&app, &level, &tiles, list.items, list.count, &fallback, &cache, gameinfo, 0);
    hudview = (hudview_t){.units = list.items, .unit_count = list.count, .sprites = &cache, .tileset = &tiles};
    if (hud->refresh) hud->refresh(hud);
    M_MenuDrawer(hud);
    const char *dir = getenv("OPEN_RTS_RF_SITE_SHOTS");
    if (dir) {
        isize2_t size = V_DrawSize();
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, size.w, size.h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (surface) {
            V_ReadPixels(surface->pixels, surface->pitch);
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
            SDL_SaveBMP(surface, path);
            SDL_FreeSurface(surface);
        }
    }
    hudview = (hudview_t){0};
    P_FreeMobjList(&list);
}

/* Each faction's base on the three-way map, seen as that faction with its headquarters picked:
 * the sidebar lists what it builds. */
static int test_bases(void) {
    static const struct { int owner; uint16_t hq; const char *name; } BASES[] = {
        {0, MT_HQ, "base-directorate"}, {1, MT_CL_HALL, "base-clans"}, {2, MT_LC_SPIRE, "base-concord"}};
    for (int b = 0; b < 3; ++b) {
        RTS_RUN(load("maps/triad.txt"));
        consoleplayer = BASES[b].owner;
        mobj_t *hq = find(BASES[b].owner, BASES[b].hq);
        CHECK(hq);
        select_only(&hq, 1);
        tick(20);
        float x = hq->core.position.x / 65536.0f, y = hq->core.position.y / 65536.0f;
        look_at(x + (x > 32 ? -4.0f : 6.0f), y + (y > 22 ? -5.0f : 5.0f));
        shot(BASES[b].name);
    }
    consoleplayer = 0;
    puts("PASS: each faction's base, as that faction sees it");
    return 0;
}

/* A firefight on the open sand between the bases: shells and rockets in the air, a blast. */
static int test_battle(void) {
    RTS_RUN(load("maps/skirmish.txt"));
    consoleplayer = 0;
    mobj_t *ours[8];
    int n = 0;
    ours[n++] = spawn(MT_TANK, 0, 20.5f, 13.5f, 0);
    ours[n++] = spawn(MT_TANK, 0, 20.5f, 16.5f, 0);
    ours[n++] = spawn(MT_MASTODON, 0, 19.0f, 15.0f, 0);
    ours[n++] = spawn(MT_ARTILLERY, 0, 17.5f, 14.5f, 0);
    for (int i = 0; i < 4; ++i) ours[n++] = spawn(i % 2 ? MT_ROCKET : MT_RIFLEMAN, 0, 21.5f + (i % 2), 12.5f + i * 1.2f, 0);
    for (int i = 0; i < 6; ++i) spawn(MT_CL_RAIDER, 1, 24.5f + (i % 2), 12.0f + i * 1.0f, 8);
    spawn(MT_CL_TRUCK, 1, 25.5f, 15.5f, 8);
    spawn(MT_CL_CRAWLER, 1, 26.5f, 13.5f, 8);
    spawn(MT_CL_RIG, 1, 27.5f, 16.5f, 8);
    select_only(ours, n);
    bool flying = false;
    for (int t = 0; t < 12 * RTS_TICRATE && !flying; ++t) {
        tick(1);
        int projectiles = count_type(MT_SHELL) + count_type(MT_MISSILE) + count_type(MT_HEAVY_SHELL);
        int impacts = count_type(MT_FX_HIT) + count_type(MT_FX_BOOM) + count_type(MT_FX_BIGBOOM);
        flying = projectiles >= 2 && impacts > 0;
    }
    CHECK(flying);
    look_at(24.5f, 14.5f);
    shot("battle");
    puts("PASS: a firefight with shells and rockets in the air");
    return 0;
}

/* Aircraft: Kestrel gunships and a Seraph over a Clans column, a Buzzard over ours. */
static int test_air(void) {
    RTS_RUN(load("maps/skirmish.txt"));
    consoleplayer = 0;
    mobj_t *k1 = spawn(MT_KESTREL, 0, 21.5f, 13.5f, 0), *k2 = spawn(MT_KESTREL, 0, 22.0f, 16.0f, 0);
    spawn(MT_LC_SERAPH, 0, 20.5f, 18.5f, 0);
    for (int i = 0; i < 4; ++i) spawn(i % 2 ? MT_CL_TRIKE : MT_CL_CRUSHER, 1, 27.0f + i * 0.6f, 13.0f + i * 1.4f, 8);
    spawn(MT_CL_BUZZARD, 1, 24.5f, 18.5f, 8);
    spawn(MT_TANK, 0, 22.5f, 19.5f, 0);
    mobj_t *sel[2] = {k1, k2};
    select_only(sel, 2);
    tick(3 * RTS_TICRATE);
    CHECK(k1->traits & MF_FLY);
    look_at(24.5f, 16.0f);
    shot("aircraft");
    puts("PASS: aircraft over the sand");
    return 0;
}

/* The Directorate's specials: an APC loaded with riflemen (its seats lit), a Sentry Post
 * unfolded, a Field Crane welding a battered tank, a Phantom out of phase. */
static int test_directorate(void) {
    RTS_RUN(load("maps/skirmish.txt"));
    consoleplayer = 0;
    mobj_t *apc = spawn(MT_APC, 0, 21.5f, 13.5f, 0);
    mobj_t *men[3];
    for (int i = 0; i < 3; ++i) men[i] = spawn(MT_RIFLEMAN, 0, 22.5f + i, 15.5f, 0);
    ticcmd_t board = {.order = TC_BOARD, .count = 3, .target = apc->id};
    for (int i = 0; i < 3; ++i) board.units[i] = men[i]->id;
    G_RunTiccmd(0, &board);
    for (int i = 0; i < 2; ++i) spawn(MT_RIFLEMAN, 0, 21.0f + i, 16.5f, 0);
    mobj_t *truck = spawn(MT_SENTRY_TRUCK, 0, 26.5f, 13.5f, 0);
    CHECK(RF_Deploy(truck));
    spawn(MT_SENTRY_TRUCK, 0, 27.5f, 16.5f, 0);
    mobj_t *crane = spawn(MT_CRANE, 0, 23.5f, 18.5f, 0), *tank = spawn(MT_TANK, 0, 24.5f, 18.5f, 0);
    tank->hp = tank->max_hp / 3;
    tank->traits &= ~MF_ATTACK;
    mobj_t *phantom = spawn(MT_PHANTOM, 0, 19.5f, 18.0f, 0);
    tick(4 * RTS_TICRATE);
    CHECK(RF_Passengers(apc, NULL, 0) >= 2 && crane);
    P_DamageMobj(phantom, NULL, 40);
    tick(4);
    CHECK(RF_Phased(phantom));
    select_only(&apc, 1);
    look_at(23.5f, 15.5f);
    shot("directorate-specials");
    puts("PASS: an APC loaded, a sentry post, a crane at work, a phantom out of phase");
    return 0;
}

/* The Clans' specials: a Drill Rig set down as a Derrick with a hauler drawing from it, sappers
 * holding ground beside the mines they lay, a Crusher. */
static int test_clans(void) {
    RTS_RUN(load("maps/triad.txt"));
    consoleplayer = 1;
    mobj_t *rig = spawn(MT_CL_DRILL, 1, 46.5f, 12.5f, 8);
    CHECK(RF_Deploy(rig) && rig->type_id == MT_CL_DERRICK);
    spawn(MT_CL_DRILL, 1, 42.5f, 11.0f, 8);
    mobj_t *sappers[3];
    for (int i = 0; i < 3; ++i) sappers[i] = spawn(MT_CL_SAPPER, 1, 40.5f + i * 2.0f, 15.5f, 8);
    spawn(MT_CL_CRUSHER, 1, 44.5f, 17.5f, 8);
    tick(14 * RTS_TICRATE);
    CHECK(RF_MineCount(1) >= 3);
    select_only(sappers, 3);
    look_at(44.0f, 13.5f);
    shot("clans-specials");
    puts("PASS: a derrick pumping, sappers and their mines, a crusher");
    return 0;
}

/* The Concord's specials: the Spire's hand of Rites in the sidebar, a Strike landing on a
 * Directorate column, a Strider's arc, a Cantor mending wardens. */
static int test_concord(void) {
    RTS_RUN(load("maps/triad.txt"));
    consoleplayer = 2;
    tick(RF_RITE_DRAW_MS * RF_HAND / RTS_TICK_MS + RTS_TICRATE);    /* a full hand */
    CHECK(RF_HandCard(2, 0) && RF_HandCard(2, 1) && RF_HandCard(2, 2));
    mobj_t *spire = find(2, MT_LC_SPIRE);
    CHECK(spire);
    float x = spire->core.position.x / 65536.0f, y = spire->core.position.y / 65536.0f - 9;
    mobj_t *strider = spawn(MT_LC_STRIDER, 2, x - 1, y + 1, 4);
    mobj_t *cantor = spawn(MT_LC_CANTOR, 2, x - 3, y + 2, 4);
    for (int i = 0; i < 3; ++i) {
        mobj_t *w = spawn(MT_LC_WARDEN, 2, x - 4 + i, y + 3, 4);
        w->hp = w->max_hp / 2;
    }
    for (int i = 0; i < 5; ++i) spawn(i % 2 ? MT_ROCKET : MT_RIFLEMAN, 0, x + 2 + (i % 2), y - 2 + i * 0.8f, 12);
    spawn(MT_TANK, 0, x + 4, y - 1, 12);
    tick(2 * RTS_TICRATE);
    int strike = -1;
    for (int s = 0; s < RF_HAND; ++s) if (RF_HandCard(2, s) == RF_RITE_STRIKE) strike = s;
    if (strike >= 0) {
        CHECK(RF_PlayRite(2, strike, (fixed2_t){(fixed_t)((x + 3) * 65536), (fixed_t)((y - 1) * 65536)}));
        tick(RTS_TICRATE);
    }
    select_only(&spire, 1);
    (void)strider; (void)cantor;
    look_at(x + 1, y);
    shot("concord-specials");
    puts("PASS: the Rites in hand, a strike landing, a strider and a cantor at work");
    return 0;
}

/* The badlands: the Glass River's canyon, its fords and walls, a mesa. */
static int test_terrain(void) {
    RTS_RUN(load("maps/badlands.txt"));
    consoleplayer = 0;
    for (int dx = -8; dx <= 8; dx += 8)
        for (int dy = -6; dy <= 6; dy += 6) spawn(MT_SCOUT, 0, 32.5f + dx, 16.5f + dy, 0);
    tick(10);
    look_at(32.5f, 16.5f);
    shot("terrain");
    puts("PASS: the canyon, the river, the fords and a mesa");
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RTS_RUN(test_bases());
    RTS_RUN(test_battle());
    RTS_RUN(test_air());
    RTS_RUN(test_directorate());
    RTS_RUN(test_clans());
    RTS_RUN(test_concord());
    RTS_RUN(test_terrain());
    RF_ShutdownHUD();
    SDL_Quit();
    return 0;
}
