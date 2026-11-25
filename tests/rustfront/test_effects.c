#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "rustfront effects", #c)

static const char *const MAP = "games/rustfront/maps/skirmish.txt";

static mobj_t *spawn(uint16_t type, int owner, fixed2_t at, angle_t angle) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(at, 0), type);
    if (!unit) return NULL;
    unit->owner = unit->team = owner;
    unit->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER;
    unit->core.angle = angle;
    return unit;
}

static int count_type(uint16_t type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        n += th->function == P_MobjThinker && ((mobj_t *)th)->type_id == type && !((mobj_t *)th)->remove;
    return n;
}

static app_t app = {.win = {640, 480}, .cell = {48, 32}, .running = true};
static tileset_t tiles;
static spritecache_t cache;
static spritesheet_t fallback;

/* Optional evidence: OPEN_RTS_RUSTFRONT_SHOTS=dir saves the world as the game draws it. */
static void shot(const char *name) {
    const char *dir = getenv("OPEN_RTS_RUSTFRONT_SHOTS");
    if (!dir) return;
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

/* A firefight on open sand: shells and rockets fly and land later than they are fired, the
 * heavy shell arcs, rockets trail smoke; bullets flash and land as puffs, turrets show their firing frames,
 * vehicles die in an explosion and soldiers fall, and every effect removes itself. */
static int test_firefight(void) {
    P_FreeLevel(&level);
    P_InitThinkers();
    CHECK(G_DoLoadLevel(MAP, &level) && P_InitSight());
    CHECK(W_LoadAssets(g_game_default_root, &level, NULL, &tiles, &fallback));
    R_SetLevelPalette(&tiles);
    V_AllocScreen(640, 480);
    app.cam.x = 0;
    app.cam.y = -12 * 32; /* the camera is a screen offset: this shows rows 12 and down */
    mobj_t *tank = spawn(MT_TANK, 0, FIXED2_LIT(3.5, 16.5), 0);
    mobj_t *gun = spawn(MT_GUN_TURRET, 0, FIXED2_LIT(2.5, 19.5), 0);
    mobj_t *arty = spawn(MT_ARTILLERY, 0, FIXED2_LIT(2.5, 14.5), 0);
    mobj_t *rifle = spawn(MT_RIFLEMAN, 0, FIXED2_LIT(4.5, 18.5), 0);
    mobj_t *enemy = spawn(MT_TANK, 1, FIXED2_LIT(7.5, 16.5), ANG180);
    mobj_t *apc = spawn(MT_APC, 1, FIXED2_LIT(7.5, 19.5), ANG180);
    mobj_t *foe = spawn(MT_RIFLEMAN, 1, FIXED2_LIT(6.5, 18.5), ANG180);
    mobj_t *bazooka = spawn(MT_ROCKET, 1, FIXED2_LIT(6.5, 13.5), ANG180);
    CHECK(tank && gun && arty && rifle && enemy && apc && foe && bazooka);
    int enemy_hp = enemy->hp;
    mobjlist_t list = P_ListMobjs();
    CHECK(R_InitSprites(g_game_default_root, &level, list.items, list.count, &cache));
    P_FreeMobjList(&list);
    CHECK(R_CacheLookup(&cache, "fx_boom") && R_CacheLookup(&cache, "tank_turret"));
    CHECK(R_CacheLookup(&cache, "tank_turret")->spritedef.numframes == 3);      /* idle, shot, run-out */
    CHECK(R_CacheLookup(&cache, "tank_hull")->spritedef.numframes == 2);
    /* the radar's eight-frame turn, its scorched copy and four construction stages */
    CHECK(R_CacheLookup(&cache, "hq_body")->spritedef.numframes == 8 + 8 + BUILD_STAGE_COUNT);

    bool saw_shot = false, saw_hit = false, saw_boom = false, saw_fall = false;
    bool saw_shell = false, saw_rocket = false, saw_smoke = false, saw_arc = false, hit_late = false;
    int shots_saved = 0, flight_saved = 0;
    for (int tic = 0; tic < 40 * RTS_TICRATE; ++tic) {
        P_Ticker();
        mobjlist_t all = P_ListMobjs();
        int n = all.count;
        G_MissionTicker(&level, all.items, &n, NULL, RTS_TICK_MS);
        P_FreeMobjList(&all);
        /* A shell is in the air before its damage lands. */
        if (count_type(MT_SHELL) && !saw_shell) hit_late = enemy->hp == enemy_hp;
        saw_shell |= count_type(MT_SHELL) > 0;
        saw_rocket |= count_type(MT_MISSILE) > 0;
        saw_smoke |= count_type(MT_FX_SMOKE) > 0;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            const mobj_t *u = (const mobj_t *)th;
            if (th->function == P_MobjThinker && u->type_id == MT_HEAVY_SHELL && (u->traits & MF_MISSILE))
                saw_arc |= u->core.position.z > FIXED_ONE / 2;
        }
        if (!flight_saved && count_type(MT_SHELL) && count_type(MT_MISSILE) && count_type(MT_FX_SMOKE) > 2) {
            shot("flight");
            flight_saved = 1;
        }
        bool firing = false;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            const mobj_t *u = (const mobj_t *)th;
            if (th->function != P_MobjThinker || u->remove) continue;
            firing |= u->core.state_id == S_TANK_FIRE || u->core.state_id == S_ARTILLERY_FIRE ||
                      u->core.state_id == S_GUN_TURRET_FIRE;
            saw_fall |= u->core.state_id == S_RIFLEMAN_DIE;
        }
        saw_shot |= firing;
        saw_hit |= count_type(MT_FX_HIT) > 0;
        bool boom = count_type(MT_FX_BOOM) > 0;
        if (firing && count_type(MT_FX_HIT) && shots_saved == 0) { shot("firefight"); shots_saved = 1; }
        if (boom && !saw_boom) {
            for (int i = 0; i < 6; ++i) P_Ticker();
            shot("explosion");
        }
        saw_boom |= boom;
    }
    CHECK(saw_shot && saw_hit && saw_boom && saw_fall);
    if (getenv("RF_DEBUG")) printf("shell %d rocket %d smoke %d arc %d late %d\n", saw_shell, saw_rocket, saw_smoke, saw_arc, hit_late);
    CHECK(saw_shell && saw_rocket && saw_smoke && saw_arc && hit_late);
    CHECK(count_type(MT_SHELL) == 0 && count_type(MT_HEAVY_SHELL) == 0 && count_type(MT_MISSILE) == 0);
    /* The fight is over on one side, and no effect outlives its run. */
    for (int tic = 0; tic < 3 * RTS_TICRATE; ++tic) P_Ticker();
    CHECK(count_type(MT_FX_HIT) == 0 && count_type(MT_FX_BOOM) == 0 && count_type(MT_FX_BIGBOOM) == 0);
    CHECK(count_type(MT_FX_SMOKE) == 0);

    /* A structure goes up in several blasts over its footprint. */
    mobj_t *plant = spawn(MT_POWER, 0, P_BuildingPosition(MT_POWER, (ivec2_t){8, 2}), 0);
    CHECK(plant);
    P_DamageMobj(plant, NULL, plant->hp);
    CHECK(count_type(MT_FX_BOOM) + count_type(MT_FX_BIGBOOM) == 4);
    app.cam.x = -4 * 48;
    app.cam.y = 0;
    for (int i = 0; i < 9; ++i) P_Ticker();
    shot("building");
    R_FreeSpriteCache(&cache);
    R_FreeTileset(&tiles);
    V_FreeScreen();
    P_FreeLevel(&level);
    puts("PASS: shells and rockets fly, arc, trail and land; things explode or fall; effects expire");
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RTS_RUN(test_firefight());
    SDL_Quit();
    return 0;
}
