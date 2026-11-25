#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "rustfront economy", #c)

static const char *const MAP = "games/rustfront/maps/skirmish.txt";

static mobj_t *spawn(uint16_t type, int owner, ivec2_t cell) {
    const mobjtype_t *info = P_ActorType(type);
    mobj_t *unit = P_SpawnMobj(fixed3_zero(), type);
    if (!unit || !info) return NULL;
    fixed2_t at = info->footprint.w > 0 ? P_BuildingPosition(type, cell) : fixed2_cell_center(cell);
    unit->core.position = fixed3_from_fixed2(at, 0);
    unit->owner = unit->team = owner;
    unit->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER;
    return unit;
}

static void open_level(int w, int h) {
    P_FreeLevel(&level);
    P_InitThinkers();
    level.width = w;
    level.height = h;
    level.tile_ids = calloc((size_t)w * h, sizeof(*level.tile_ids));
    level.blocked = calloc((size_t)w * h, 1);
}

static int count_of(int owner, uint16_t type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker) {
            const mobj_t *u = (const mobj_t *)th;
            n += u->owner == owner && u->type_id == type && u->hp > 0 && !u->remove;
        }
    return n;
}

static void run(int tics) {
    for (int t = 0; t < tics; ++t) {
        P_Ticker();
        G_ProductionTicker(RTS_TICK_MS);
        mobjlist_t list = P_ListMobjs();
        int count = list.count;
        G_MissionTicker(&level, list.items, &count, NULL, RTS_TICK_MS);
        P_FreeMobjList(&list);
    }
}

/* The Command Center builds structures behind their prerequisites; the barracks trains
 * infantry; a finished refinery arrives with a harvester and blocks its footprint. */
static int test_production(void) {
    open_level(40, 30);
    level.player_resources[0][0] = 10000;
    mobj_t *hq = spawn(MT_HQ, 0, (ivec2_t){2, 2});
    CHECK(hq);
    const StaticProductDefinition *power = G_ModelProductByUIId(NULL, MT_POWER);
    const StaticProductDefinition *barracks = G_ModelProductByUIId(NULL, MT_BARRACKS);
    const StaticProductDefinition *refinery = G_ModelProductByUIId(NULL, MT_REFINERY);
    const StaticProductDefinition *factory = G_ModelProductByUIId(NULL, MT_FACTORY);
    const StaticProductDefinition *rifleman = G_ModelProductByUIId(NULL, MT_RIFLEMAN);
    CHECK(power && barracks && refinery && factory && rifleman);
    CHECK(G_ModelProductAvailable(NULL, 0, power));
    CHECK(!G_ModelProductAvailable(NULL, 0, barracks));     /* needs power */
    CHECK(!G_ModelProductAvailable(NULL, 0, factory));      /* needs a refinery */
    CHECK(!G_ModelProductAvailable(NULL, 0, rifleman));     /* needs a barracks to train it */

    CHECK(G_ConstructOrder(hq, power->ui_id, (ivec2_t){8, 2}));
    run(G_ModelProductTrainingTimeMs(power) / RTS_TICK_MS + 2 * RTS_TICRATE);
    CHECK(count_of(0, MT_POWER) == 1);
    CHECK(level.player_resources[0][0] == 10000 - power->cost);
    CHECK(G_ModelProductAvailable(NULL, 0, barracks) && G_ModelProductAvailable(NULL, 0, refinery));
    CHECK(level.blocked[L_Index(&level, 8, 2)] && level.blocked[L_Index(&level, 9, 3)]);

    CHECK(G_ConstructOrder(hq, refinery->ui_id, (ivec2_t){12, 2}));
    run(G_ModelProductTrainingTimeMs(refinery) / RTS_TICK_MS + 2 * RTS_TICRATE);
    CHECK(count_of(0, MT_REFINERY) == 1);
    CHECK(count_of(0, MT_HARVESTER) == 1);                  /* comes with the refinery */
    CHECK(G_ModelProductAvailable(NULL, 0, factory));

    mobj_t *hut = spawn(MT_BARRACKS, 0, (ivec2_t){2, 8});
    CHECK(hut && G_ModelProductAvailable(NULL, 0, rifleman));
    int money = level.player_resources[0][0];
    CHECK(G_QueueProduct(hut, rifleman) && G_QueueProduct(hut, rifleman));
    CHECK(level.player_resources[0][0] == money - 2 * rifleman->cost);
    run(2 * G_ModelProductTrainingTimeMs(rifleman) / RTS_TICK_MS + 2 * RTS_TICRATE);
    CHECK(count_of(0, MT_RIFLEMAN) == 2);
    P_FreeLevel(&level);
    puts("PASS: structures follow prerequisites, a refinery brings a harvester, the barracks trains");
    return 0;
}

/* On the skirmish map the harvester mines the ore field and brings the money home, and a
 * mined-out cell turns back to sand. */
static int test_harvest(void) {
    P_FreeLevel(&level);
    P_InitThinkers();
    CHECK(G_DoLoadLevel(MAP, &level) && P_InitSight());
    CHECK(level.resource_vent_count > 0);
    CHECK(P_LoadThings(MAP) > 0);
    int start = level.player_resources[0][0];
    CHECK(start == 5000);
    run(90 * RTS_TICRATE);
    CHECK(level.player_resources[0][0] > start);
    int mined = 0;
    for (int i = 0; i < level.resource_vent_count; ++i) mined += 1000 - level.resource_vents[i].amount;
    CHECK(mined >= level.player_resources[0][0] - start);
    /* Empty one cell by hand: the ticker turns it to sand. */
    resourcevent_t *vent = &level.resource_vents[0];
    vent->amount = 0;
    vent->active = false;
    run(RTS_TICRATE);
    CHECK(level.tile_ids[L_Index(&level, vent->cell.x, vent->cell.y)] < 8);
    P_FreeLevel(&level);
    puts("PASS: the harvester mines ore into credits; mined-out cells become sand");
    return 0;
}

/* The sidebar draws the Command Center's build list, and a click on a row starts placing
 * that structure. OPEN_RTS_RUSTFRONT_SHOTS=dir saves the frame for review. */
static int test_sidebar(void) {
    open_level(40, 30);
    level.player_resources[0][0] = 5000;
    mobj_t *hq = spawn(MT_HQ, 0, (ivec2_t){2, 2});
    CHECK(hq);
    P_MobjSetSelected(hq, true);
    app_t app = {.win = {640, 480}, .cell = {48, 32}};
    V_AllocScreen(app.win.w, app.win.h);
    CHECK(screens[0].pixels);
    menu_t *bar = G_InitHUD(&app, g_game_default_root);
    CHECK(bar);
    tileset_t tiles = {0};
    CHECK(W_LoadAssets(g_game_default_root, &level, NULL, &tiles, NULL));
    R_SetLevelPalette(&tiles);
    mobjlist_t list = P_ListMobjs();
    hudview = (hudview_t){.units = list.items, .unit_count = list.count, .tileset = &tiles};
    bar->refresh(bar);
    menuitem_t *products = NULL;
    for (int i = 0; i < bar->numitems; ++i)
        if (bar->items[i].routine == HU_ProductList) products = &bar->items[i];
    CHECK(products && products->rows == 5);
    V_BeginFrame(0xff000000u);
    M_MenuDrawer(bar);
    const char *dir = getenv("OPEN_RTS_RUSTFRONT_SHOTS");
    if (dir) {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
        CHECK(surface);
        V_ReadPixels(surface->pixels, surface->pitch);
        char path[1024];
        snprintf(path, sizeof(path), "%s/rustfront-sidebar.bmp", dir);
        CHECK(SDL_SaveBMP(surface, path) == 0);
        SDL_FreeSurface(surface);
    }
    SDL_Event click = {.type = SDL_MOUSEBUTTONDOWN};
    click.button.button = SDL_BUTTON_LEFT;
    click.button.x = products->rect.x + 20;
    click.button.y = products->rect.y + 20;
    CHECK(M_MenuResponder(bar, &app, &click));
    CHECK(bar->target == products);                         /* placing the power plant */
    P_FreeMobjList(&list);
    G_ShutdownHUD();
    R_FreeTileset(&tiles);
    V_FreeScreen();
    P_FreeLevel(&level);
    puts("PASS: the sidebar lists the Command Center's structures and starts placement");
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RTS_RUN(test_production());
    RTS_RUN(test_harvest());
    RTS_RUN(test_sidebar());
    SDL_Quit();
    return 0;
}
