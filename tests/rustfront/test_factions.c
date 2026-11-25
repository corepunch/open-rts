#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "rustfront factions", #c)

static const char *const TRIAD = "maps/triad.txt";

static int count_owned(int owner, uint16_t type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        n += th->function == P_MobjThinker && u->owner == owner && u->type_id == type && !u->remove && u->hp > 0;
    }
    return n;
}

static int count_type(uint16_t type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        n += th->function == P_MobjThinker && ((mobj_t *)th)->type_id == type && !((mobj_t *)th)->remove;
    return n;
}

/* OPEN_RTS_RUSTFRONT_SHOTS=dir saves the world around `cell` as the game draws it. */
static void shot(const char *name, ivec2_t cell) {
    const char *dir = getenv("OPEN_RTS_RUSTFRONT_SHOTS");
    if (!dir) return;
    static tileset_t tiles;
    static spritecache_t cache;
    static spritesheet_t fallback;
    static bool loaded;
    app_t app = {.win = {640, 480}, .cell = {48, 32}, .running = true};
    mobjlist_t list = P_ListMobjs();
    if (!loaded) {
        loaded = W_LoadAssets(g_game_default_root, &level, NULL, &tiles, &fallback);
        V_AllocScreen(640, 480);
    }
    R_InitSprites(g_game_default_root, &level, list.items, list.count, &cache);
    R_SetLevelPalette(&tiles);
    app.cam.x = -(cell.x * 48 - 240);
    app.cam.y = -(cell.y * 32 - 240);
    P_UpdateSight();
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

/* Each side of the three-way map plays its headquarters' faction, sees only that faction's
 * products, and a refinery of each faction arrives with its own harvester. */
static int test_roles(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = g_game_default_root, .map_path = TRIAD};
    CHECK(rts_game_model_load(model, &config));
    CHECK(RF_OwnerFaction(0) == RF_DIRECTORATE && RF_OwnerFaction(1) == RF_CLANS && RF_OwnerFaction(2) == RF_CONCORD);
    for (int owner = 0; owner < RF_FACTION_COUNT; ++owner) {
        StaticProductDefinition products[64];
        int n = G_ModelGetProducts(NULL, owner, products, 64);
        CHECK(n >= 11);
        for (int i = 0; i < n; ++i) CHECK(RF_FactionOfType((uint16_t)products[i].product_type) == owner);
        /* Every product's icon is in the strip, and every type has its sheets. */
        for (int i = 0; i < n; ++i) CHECK(products[i].icon_frame >= 0);
    }
    CHECK(G_ModelProductByUIId(NULL, MT_MASTODON) && G_ModelProductByUIId(NULL, MT_CL_TORCHER) &&
          G_ModelProductByUIId(NULL, MT_LC_PRISM));
    for (int f = 0; f < RF_FACTION_COUNT; ++f) {
        uint16_t refinery = RF_Role(f, RF_ROLE_REFINERY), harvester = RF_Role(f, RF_ROLE_HARVESTER);
        int before = count_owned(f, harvester);
        mobj_t *r = P_SpawnMobj(fixed3_from_fixed2(P_BuildingPosition(refinery, (ivec2_t){30, 12}), 0), refinery);
        CHECK(r);
        r->owner = r->team = f;
        G_UnitProduced(r, NULL);
        CHECK(count_owned(f, harvester) == before + 1);
        P_RemoveMobj(r);
    }
    rts_game_model_destroy(model);
    puts("PASS: each side plays its faction's catalog; refineries bring their own harvesters");
    return 0;
}

/* The Mastodon's two guns fire a pair of shells side by side; a Rocket Rig fires three
 * rockets; a Torcher throws flame and a Glaive a bolt. */
static int test_weapons(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = g_game_default_root, .map_path = TRIAD};
    CHECK(rts_game_model_load(model, &config));
    struct { uint16_t shooter, projectile; int count; } cases[] = {
        {MT_MASTODON, MT_SHELL, 2}, {MT_CL_RIG, MT_MISSILE, 3}, {MT_CL_TORCHER, MT_FLAME, 1},
        {MT_LC_GLAIVE, MT_BOLT, 1}, {MT_LC_PRISM, MT_HEAVY_BOLT, 1},
    };
    for (size_t c = 0; c < sizeof(cases) / sizeof(*cases); ++c) {
        mobj_t *gun = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(30.5, 22.5), 0), cases[c].shooter);
        mobj_t *mark = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(31.8, 22.5), 0), MT_MASTODON);
        CHECK(gun && mark);
        gun->owner = gun->team = 0;
        mark->owner = mark->team = 1;
        mark->hp = mark->max_hp = 100000;
        int before = count_type(cases[c].projectile);
        CHECK(RF_Attack(gun, &gun->info->attack, mark));
        CHECK(count_type(cases[c].projectile) == before + cases[c].count);
        P_RemoveMobj(gun);
        P_RemoveMobj(mark);
        for (int t = 0; t < 3 * RTS_TICRATE; ++t) P_Ticker();
    }
    rts_game_model_destroy(model);
    puts("PASS: twin shells, rocket salvos, flame and bolts");
    return 0;
}

/* Ten minutes of the three-way map with the player idle: the Clans and the Concord each build
 * their own factory and towers, harvest, field an army of their own units and send waves. */
static int test_three_way(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = g_game_default_root, .map_path = TRIAD};
    CHECK(rts_game_model_load(model, &config));
    AiContext *ai = rts_game_model_ai(model);
    CHECK(ai);
    int max_army[3] = {0};
    for (int t = 0; t < 30 * 60 * 10; ++t) {
        CHECK(rts_game_model_tick(model, RTS_TICK_MS));
        if (t % 30) continue;
        int army[3] = {0};
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            const mobj_t *u = (const mobj_t *)th;
            if (th->function != P_MobjThinker || u->owner < 1 || u->owner > 2 || u->remove || u->hp <= 0 ||
                !(u->traits & MF_ATTACK) || !(u->traits & MF_MOBILE)) continue;
            CHECK(RF_FactionOfType(u->type_id) == u->owner);   /* nobody builds another's units */
            army[u->owner]++;
        }
        for (int o = 1; o <= 2; ++o) if (army[o] > max_army[o]) max_army[o] = army[o];
        if (t == 30 * 60 * 5) {
            shot("triad-clans", (ivec2_t){55, 9});
            shot("triad-concord", (ivec2_t){31, 36});
            shot("triad-directorate", (ivec2_t){8, 9});
        }
    }
    for (int o = 1; o <= 2; ++o) {
        const AiStats *stats = P_AiStats(ai, o);
        printf("%s after 10 minutes: buys %d, waves %d, factories %d, towers %d, harvesters %d, largest army %d\n",
               rf_factions[o].name, stats->purchases, stats->waves, count_owned(o, RF_Role(o, RF_ROLE_FACTORY)),
               count_owned(o, RF_Role(o, RF_ROLE_TURRET)), count_owned(o, RF_Role(o, RF_ROLE_HARVESTER)), max_army[o]);
        CHECK(count_owned(o, RF_Role(o, RF_ROLE_FACTORY)) >= 1 || stats->purchases >= 10);
        CHECK(stats->purchases >= 8 && max_army[o] >= 4);
    }
    rts_game_model_destroy(model);
    puts("PASS: the Clans and the Concord each build up and field their own armies");
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RTS_RUN(test_roles());
    RTS_RUN(test_weapons());
    RTS_RUN(test_three_way());
    SDL_Quit();
    return 0;
}
