#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "rustfront ai", #c)

static int count_owned(int owner, uint16_t type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        n += th->function == P_MobjThinker && u->owner == owner && u->type_id == type && !u->remove && u->hp > 0;
    }
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
        loaded = W_LoadAssets(g_game_default_root, &level, NULL, &tiles, &fallback) &&
                 R_InitSprites(g_game_default_root, &level, list.items, list.count, &cache);
        V_AllocScreen(640, 480);
    }
    R_InitSprites(g_game_default_root, &level, list.items, list.count, &cache);
    R_SetLevelPalette(&tiles);
    app.cam.x = -(cell.x * 48 - 320);
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

/* Red, the computer, on the skirmish map: it builds its factory and turrets as rising sites
 * on spaced-out ground, keeps harvesting, fields a mixed army and brings a wave to Blue's
 * base. Blue (owner 0) is the human and does nothing. */
static int test_red_plays(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = g_game_default_root};
    CHECK(rts_game_model_load(model, &config));
    AiContext *ai = rts_game_model_ai(model);
    CHECK(ai && G_AiInterface());
    int red = 1;
    int start = level.player_resources[red][0];
    bool saw_site = false, saw_red_near_blue = false, sites_roomy = true;
    int max_army = 0;
    for (int t = 0; t < 30 * 60 * 8; ++t) {
        CHECK(rts_game_model_tick(model, RTS_TICK_MS));
        const mobj_t *site = RF_SiteInProgress(red);
        if (site && !saw_site) {
            saw_site = true;
            /* AI sites keep a free cell all round. */
            irect_t c = P_MobjCells(site);
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                const mobj_t *u = (const mobj_t *)th;
                if (th->function != P_MobjThinker || u == site || u->remove || (u->traits & MF_MOBILE) ||
                    !u->info || u->info->footprint.w <= 0) continue;
                irect_t o = P_MobjCells(u);
                if (o.x < c.x + c.w + 1 && c.x < o.x + o.w + 1 && o.y < c.y + c.h + 1 && c.y < o.y + o.h + 1)
                    sites_roomy = false;
            }
        }
        int army = 0;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            const mobj_t *u = (const mobj_t *)th;
            if (th->function != P_MobjThinker || u->owner != red || u->remove || u->hp <= 0 ||
                !(u->traits & MF_ATTACK) || !(u->traits & MF_MOBILE)) continue;
            army++;
            ivec2_t at = fixed2_cell(fixed3_xy(u->core.position));
            if (at.x < 14 && at.y < 16 && !saw_red_near_blue) {
                saw_red_near_blue = true;
                shot("ai-attack", at);
            }
        }
        if (army > max_army) max_army = army;
        if (t == 30 * 60 * 4) shot("ai-base", (ivec2_t){37, 20});
    }
    const AiStats *stats = P_AiStats(ai, red);
    printf("red after 8 minutes: buys %d, waves %d, harvest orders %d, credits %d -> %d, "
           "factories %d, turrets %d, harvesters %d, largest army %d\n",
           stats->purchases, stats->waves, stats->harvest_orders, start, level.player_resources[red][0],
           count_owned(red, MT_FACTORY), count_owned(red, MT_GUN_TURRET), count_owned(red, MT_HARVESTER), max_army);
    CHECK(saw_site && sites_roomy);
    CHECK(count_owned(red, MT_FACTORY) >= 1 && count_owned(red, MT_GUN_TURRET) >= 2);
    CHECK(count_owned(red, MT_HARVESTER) >= 2);
    CHECK(stats->purchases >= 15 && max_army >= 8);
    CHECK(stats->waves >= 1 && saw_red_near_blue);
    rts_game_model_destroy(model);
    puts("PASS: red builds on rising sites, harvests, masses a mixed army and attacks Blue's base");
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RTS_RUN(test_red_plays());
    SDL_Quit();
    return 0;
}
