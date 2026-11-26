/* Shared by the campaign tests: load a mission headless, solve it by script, or play it with a
 * simple player bot against the engine AI and the mission's waves (docs/CAMPAIGN.md). */
#ifndef RF_CAMPAIGN_HARNESS_H
#define RF_CAMPAIGN_HARNESS_H

#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CCHECK(c, what) do { if (!(c)) { fprintf(stderr, "FAIL (%s): %s\n", mission_name, what); return 1; } } while (0)

static char mission_name[64];

/* Progress goes to a scratch directory, never the user's own. */
static void campaign_user_dir(void) {
    static char dir[] = "/tmp/rustfront-campaign-XXXXXX";
    if (mkdtemp(dir)) setenv("OPEN_RTS_USER_DIR", dir, 1);
}

static RtsGameModel *load_mission(int faction, int index) {
    static char path[128];
    RF_MissionPath(faction, index, path, sizeof(path));
    snprintf(mission_name, sizeof(mission_name), "%s", path);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = g_game_default_root, .map_path = path};
    if (!model || !rts_game_model_load(model, &config)) {
        fprintf(stderr, "FAIL (%s): does not load\n", path);
        if (model) rts_game_model_destroy(model);
        return NULL;
    }
    return model;
}

static bool live(const mobj_t *u) {
    return u && u->thinker.function == P_MobjThinker && !u->remove && u->hp > 0 && u->info &&
           G_UnitHoldsSide(u);
}

static mobj_t *find_id(uint32_t id) {
    for (thinker_t *th = thinkercap.next; id && th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && ((mobj_t *)th)->id == id) return (mobj_t *)th;
    return NULL;
}

static ivec2_t cell_of(const mobj_t *u) { return fixed2_cell(fixed3_xy(u->core.position)); }

static int count_side(int owner) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        n += live((mobj_t *)th) && ((mobj_t *)th)->owner == owner;
    return n;
}

static void kill(mobj_t *u) {
    if (live(u)) P_DamageMobj(u, NULL, u->hp + 1);
}

static bool ticks(RtsGameModel *model, int count) {
    for (int i = 0; i < count && !G_LevelOutcome(); ++i)
        if (!rts_game_model_tick(model, RTS_TICK_MS)) return false;
    return true;
}

/* ── reachability: every enemy thing can be reached on foot from the player's start ── */

static bool reachable_from_player(int *unreached_x, int *unreached_y) {
    int w = level.width, h = level.height;
    uint8_t *seen = calloc((size_t)w * h, 1);
    int *queue = malloc(sizeof(int) * (size_t)w * h), head = 0, tail = 0;
    if (!seen || !queue) { free(seen); free(queue); return false; }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (!live(u) || u->owner != 0 || !(u->traits & MF_MOBILE)) continue;
        ivec2_t c = cell_of(u);
        int i = L_Index(&level, c.x, c.y);
        if (!seen[i]) { seen[i] = 1; queue[tail++] = i; }
    }
    while (head < tail) {
        int i = queue[head++], x = i % w, y = i / w;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
            int nx = x + dx[d], ny = y + dy[d];
            if (!L_Contains(&level, nx, ny)) continue;
            int n = L_Index(&level, nx, ny);
            if (seen[n] || RF_CellBlocks(&level, n)) continue;
            seen[n] = 1;
            queue[tail++] = n;
        }
    }
    bool ok = true;
    for (thinker_t *th = thinkercap.next; th != &thinkercap && ok; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (!live(u) || u->owner == 0) continue;
        irect_t r = P_MobjCells(u);
        bool near = false;
        for (int y = r.y - 1; y <= r.y + r.h && !near; ++y)
            for (int x = r.x - 1; x <= r.x + r.w && !near; ++x)
                near = L_Contains(&level, x, y) && seen[L_Index(&level, x, y)];
        if (!near) { ok = false; *unreached_x = r.x; *unreached_y = r.y; }
    }
    for (int i = 0; ok && i < RF_ObjectiveCount(); ++i) {
        rf_objective_t o;
        RF_ObjectiveDetail(i, &o);
        if (o.kind != RF_OBJ_HOLD) continue;
        bool any = false;
        for (int y = o.area.y; y < o.area.y + o.area.h; ++y)
            for (int x = o.area.x; x < o.area.x + o.area.w; ++x) any = any || seen[L_Index(&level, x, y)];
        if (!any) { ok = false; *unreached_x = o.area.x; *unreached_y = o.area.y; }
    }
    free(seen);
    free(queue);
    return ok;
}

/* ── helpers shared by the scripted solver and the bot ── */

static mobj_t *player_first(bool mobile) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (live(u) && u->owner == 0 && !!(u->traits & MF_MOBILE) == mobile &&
            (!mobile || !(u->traits & MF_HARVESTER))) return u;
    }
    return NULL;
}

static mobj_t *owned(int owner, uint16_t type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (live(u) && u->owner == owner && u->type_id == type && !RF_IsSite(u)) return u;
    }
    return NULL;
}

/* A site near `from` with a free cell all round, as the AI picks (g_ai.c). */
static bool free_site(uint16_t type, ivec2_t from, ivec2_t *out) {
    const mobjtype_t *info = P_ActorType(type);
    if (!info) return false;
    for (int r = 2; r < 24; ++r)
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x) {
                if (abs(x) != r && abs(y) != r) continue;
                ivec2_t c = {from.x + x, from.y + y};
                bool ok = P_CanPlaceBuildingForOwner(type, c, 0);
                for (int yy = c.y - 1; ok && yy <= c.y + info->footprint.h; ++yy)
                    for (int xx = c.x - 1; ok && xx <= c.x + info->footprint.w; ++xx)
                        ok = L_Contains(&level, xx, yy) && !level.blocked[L_Index(&level, xx, yy)];
                for (thinker_t *th = thinkercap.next; ok && th != &thinkercap; th = th->next) {
                    const mobj_t *u = (const mobj_t *)th;
                    if (!live(u) || !(u->traits & MF_MOBILE)) continue;
                    ivec2_t uc = cell_of(u);
                    ok = !(uc.x >= c.x - 1 && uc.y >= c.y - 1 && uc.x <= c.x + info->footprint.w &&
                           uc.y <= c.y + info->footprint.h);
                }
                if (ok) { *out = c; return true; }
            }
    return false;
}

/* ── the scripted solver: meet every objective by fiat, to prove the win fires ── */

static void solve_step(void) {
    for (int i = 0; i < RF_ObjectiveCount(); ++i) {
        rf_objective_t o;
        RF_ObjectiveDetail(i, &o);
        if (RF_ObjectiveState(i) == RF_OBJECTIVE_DONE) continue;
        mobj_t *tagged = find_id(o.tag_id);
        switch (o.kind) {
        case RF_OBJ_DESTROY_ALL:
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
                if (((mobj_t *)th)->owner != 0) kill((mobj_t *)th);
            break;
        case RF_OBJ_DESTROY: kill(tagged); break;
        case RF_OBJ_PROTECT: if (live(tagged)) tagged->hp = tagged->max_hp; break;
        case RF_OBJ_CREDITS: level.player_resources[0][0] = o.amount; break;
        case RF_OBJ_BUILD: {
            mobj_t *anchor = player_first(false);
            ivec2_t c;
            if (anchor && free_site(o.type, cell_of(anchor), &c)) RF_SpawnThing(o.type, 0, 0, c);
            break;
        }
        case RF_OBJ_HOLD: {
            mobj_t *u = player_first(true);
            ivec2_t centre = {o.area.x + o.area.w / 2, o.area.y + o.area.h / 2};
            if (u && !P_HasMoveOrder(u) && !(cell_of(u).x >= o.area.x && cell_of(u).x < o.area.x + o.area.w &&
                                              cell_of(u).y >= o.area.y && cell_of(u).y < o.area.y + o.area.h)) {
                mobj_t *one[1] = {u};
                P_MoveUnitsAt(&level, one, 1, fixed2_cell_center(centre));
            }
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                mobj_t *e = (mobj_t *)th;
                ivec2_t ec = live(e) ? cell_of(e) : (ivec2_t){-99, -99};
                if (e->owner != 0 && abs(ec.x - centre.x) < o.area.w + 4 && abs(ec.y - centre.y) < o.area.h + 4) kill(e);
            }
            break;
        }
        default: break;
        }
    }
    /* Waves are not the point here: the bot plays them. */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *e = (mobj_t *)th;
        if (e->owner != 0 && (e->traits & MF_MOBILE) && live(e)) kill(e);
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (live(u) && u->owner == 0) u->hp = u->max_hp;
    }
}

/* Solve by script; the mission must end in victory and record its win. */
static int scripted_win(int faction, int index) {
    RtsGameModel *model = load_mission(faction, index);
    if (!model) return 1;
    CCHECK(ticks(model, RTS_TICRATE * 3) && !G_LevelOutcome(), "ends by itself at once");
    int limit = RTS_TICRATE * 60 * 12;
    for (int t = 0; t < limit && !G_LevelOutcome(); t += RTS_TICRATE / 3) {
        solve_step();
        CCHECK(ticks(model, RTS_TICRATE / 3), "tick");
    }
    printf("%s: scripted %s after %d s\n", mission_name,
           G_LevelOutcome() == OUTCOME_VICTORY ? "victory" : "no victory", RF_MissionElapsedMs() / 1000);
    CCHECK(G_LevelOutcome() == OUTCOME_VICTORY, "scripted play does not win");
    for (int i = 0; i < RF_ObjectiveCount(); ++i) {
        rf_objective_t o;
        RF_ObjectiveDetail(i, &o);
        CCHECK(o.kind == RF_OBJ_PROTECT ? RF_ObjectiveState(i) == RF_OBJECTIVE_PENDING
                                        : RF_ObjectiveState(i) == RF_OBJECTIVE_DONE, "objective state at the win");
    }
    CCHECK(RF_CampaignWon(faction) >= index + 1 && RF_MissionUnlocked(faction, index + 1 < 5 ? index + 1 : index),
           "the win is not recorded");
    rts_game_model_destroy(model);
    return 0;
}

/* Losing: a protected thing destroyed, or the player's whole side. */
static int scripted_losses(int faction, int index) {
    RtsGameModel *model = load_mission(faction, index);
    if (!model) return 1;
    int protect = -1;
    for (int i = 0; i < RF_ObjectiveCount(); ++i) {
        rf_objective_t o;
        RF_ObjectiveDetail(i, &o);
        if (o.kind == RF_OBJ_PROTECT && o.tag_id) protect = i;
    }
    CCHECK(ticks(model, RTS_TICRATE * 2), "tick");
    if (protect >= 0) {
        rf_objective_t o;
        RF_ObjectiveDetail(protect, &o);
        kill(find_id(o.tag_id));
        CCHECK(ticks(model, RTS_TICRATE), "tick");
        CCHECK(G_LevelOutcome() == OUTCOME_DEFEAT && RF_ObjectiveState(protect) == RF_OBJECTIVE_FAILED,
               "losing the protected thing does not lose");
        rts_game_model_destroy(model);
        model = load_mission(faction, index);
        if (!model) return 1;
        CCHECK(ticks(model, RTS_TICRATE * 2), "tick");
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (((mobj_t *)th)->owner == 0) kill((mobj_t *)th);
    CCHECK(ticks(model, RTS_TICRATE * 2), "tick");
    CCHECK(G_LevelOutcome() == OUTCOME_DEFEAT, "losing every unit does not lose");
    rts_game_model_destroy(model);
    return 0;
}

/* RF_CAMPAIGN_SHOTS=dir saves the world around `cell` as the game draws it (as test_opponent). */
static void campaign_shot(const char *name, ivec2_t cell) {
    const char *dir = getenv("RF_CAMPAIGN_SHOTS");
    if (!dir) return;
    static tileset_t tiles;
    static spritecache_t cache;
    static spritesheet_t fallback;
    static bool loaded;
    app_t app = {.win = {640, 480}, .cell = {48, 32}, .running = true};
    mobjlist_t list = P_ListMobjs();
    if (!loaded) {
        SDL_Init(SDL_INIT_VIDEO);
        loaded = W_LoadAssets(g_game_default_root, &level, NULL, &tiles, &fallback) &&
                 R_InitSprites(g_game_default_root, &level, list.items, list.count, &cache);
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
        snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
        SDL_SaveBMP(surface, path);
        SDL_FreeSurface(surface);
    }
    P_FreeMobjList(&list);
}

/* ── the bot: a plain player that builds, trains, defends and attacks ── */

static const StaticProductDefinition *product_of(uint16_t type) {
    const StaticProductDefinition *p = G_ModelProductByClassType(NULL, RTS_PRODUCT_BUILDING, type);
    return p ? p : G_ModelProductByClassType(NULL, RTS_PRODUCT_UNIT, type);
}

static bool can_build(uint16_t type) {
    const StaticProductDefinition *p = product_of(type);
    return p && G_ModelProductAvailable(NULL, 0, p) && level.player_resources[0][0] >= p->cost;
}

static int objective_pending(int kind) {
    for (int i = 0; i < RF_ObjectiveCount(); ++i) {
        rf_objective_t o;
        RF_ObjectiveDetail(i, &o);
        if (o.kind == kind && RF_ObjectiveState(i) != RF_OBJECTIVE_DONE) return i;
    }
    return -1;
}

static void bot_build(int faction, int reserve) {
    mobj_t *hq = owned(0, RF_Role(faction, RF_ROLE_HQ));
    if (!hq || RF_SiteInProgress(0)) return;
    static const rf_role_t order[] = {RF_ROLE_POWER, RF_ROLE_REFINERY, RF_ROLE_BARRACKS, RF_ROLE_FACTORY,
                                      RF_ROLE_TURRET, RF_ROLE_TURRET};
    int have[RF_ROLE_COUNT] = {0};
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        for (int r = 0; live(u) && u->owner == 0 && r < RF_ROLE_COUNT; ++r) have[r] += RF_IsRole(u->type_id, (rf_role_t)r);
    }
    int want[RF_ROLE_COUNT] = {0};
    for (size_t i = 0; i < sizeof(order) / sizeof(*order); ++i) {
        rf_role_t r = order[i];
        if (have[r] > want[r]++) continue;
        uint16_t type = RF_Role(faction, r);
        const StaticProductDefinition *p = product_of(type);
        if (!p || !G_ModelProductAvailable(NULL, 0, p)) continue;
        ivec2_t c;
        if (level.player_resources[0][0] - p->cost >= reserve && free_site(type, cell_of(hq), &c))
            G_PlaceProduct(hq, p, c);
        return;
    }
}

static void bot_train(int faction, int reserve) {
    /* two harvesters first */
    int harvesters = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        harvesters += live(u) && u->owner == 0 && (u->traits & MF_HARVESTER);
    }
    mobj_t *factory = owned(0, RF_Role(faction, RF_ROLE_FACTORY));
    const StaticProductDefinition *harvester = product_of(RF_Role(faction, RF_ROLE_HARVESTER));
    if (harvesters < 2 && factory && harvester && G_ModelProductAvailable(NULL, 0, harvester)) {
        bool queued = factory->production && factory->production->queue_count > 0 &&
                      RF_IsRole((uint16_t)factory->production->product_type, RF_ROLE_HARVESTER);
        if (!queued && level.player_resources[0][0] >= harvester->cost) G_QueueProduct(factory, harvester);
        if (!queued) return;
    }
    static const rf_role_t makers[] = {RF_ROLE_FACTORY, RF_ROLE_BARRACKS};
    for (size_t m = 0; m < 2; ++m) {
        mobj_t *maker = owned(0, RF_Role(faction, makers[m]));
        if (!maker) continue;
        StaticProductDefinition list[64];
        int n = G_ModelGetProducts(NULL, 0, list, 64), best = -1;
        for (int i = 0; i < n; ++i) {
            const StaticProductDefinition *p = &list[i];
            if (p->product_class != RTS_PRODUCT_UNIT || p->makers[0] != maker->type_id ||
                RF_IsRole((uint16_t)p->product_type, RF_ROLE_HARVESTER) || !G_ModelProductAvailable(NULL, 0, p) ||
                level.player_resources[0][0] - p->cost < reserve) continue;
            /* the dearest it can afford, but every third pick the cheapest, for numbers */
            if (best < 0 || ((leveltime / 90) % 3 ? p->cost > list[best].cost : p->cost < list[best].cost)) best = i;
        }
        if (best >= 0 && (!maker->production || maker->production->queue_count < 2))
            G_QueueProduct(maker, product_of((uint16_t)list[best].product_type));
    }
}

static mobj_t *nearest_enemy(ivec2_t from, int radius, bool structures_only) {
    mobj_t *best = NULL;
    int best_d = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *e = (mobj_t *)th;
        if (!live(e) || e->owner == 0 || (structures_only && (e->traits & MF_MOBILE))) continue;
        ivec2_t c = cell_of(e);
        int d = (c.x - from.x) * (c.x - from.x) + (c.y - from.y) * (c.y - from.y);
        if ((radius <= 0 || d <= radius * radius) && (!best || d < best_d)) { best = e; best_d = d; }
    }
    return best;
}

static bool protected_thing(const mobj_t *u) {
    for (int i = 0; i < RF_ObjectiveCount(); ++i) {
        rf_objective_t o;
        if (RF_ObjectiveDetail(i, &o) && o.kind == RF_OBJ_PROTECT && o.tag_id == u->id) return true;
    }
    return false;
}

/* Standing among our own structures: new units crowd out of the factory and may never reach
 * their rally point, but they are at home and free to go. */
static bool at_home(const mobj_t *u) {
    ivec2_t c = cell_of(u);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *s = (const mobj_t *)th;
        if (!live(s) || s->owner != 0 || (s->traits & MF_MOBILE)) continue;
        ivec2_t sc = cell_of(s);
        if (abs(sc.x - c.x) <= 8 && abs(sc.y - c.y) <= 8) return true;
    }
    return false;
}

/* The army: idle mobile fighters, not harvesters, and not what the mission says to keep safe. */
static int idle_army(mobj_t **out, int max) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap && n < max; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (!live(u) || u->owner != 0 || !(u->traits & MF_MOBILE) || !(u->traits & MF_ATTACK) || protected_thing(u) ||
            (u->traits & MF_HARVESTER) || (u->attack.target && live(u->attack.target) && !at_home(u)) ||
            (P_HasMoveOrder(u) && !at_home(u))) continue;
        out[n++] = u;
    }
    return n;
}

/* When the bot last ordered each unit: a person does not re-order a unit every second, and
 * re-planning a crowd that often jams it. */
static struct { uint32_t id; int second; } ordered[1024];
static int ordered_count, bot_second;

static bool recently_ordered(const mobj_t *u) {
    for (int i = 0; i < ordered_count; ++i)
        if (ordered[i].id == u->id) return bot_second - ordered[i].second < 20;
    return false;
}

static void note_order(const mobj_t *u) {
    for (int i = 0; i < ordered_count; ++i)
        if (ordered[i].id == u->id) { ordered[i].second = bot_second; return; }
    if (ordered_count < 1024) ordered[ordered_count++] = (__typeof__(ordered[0])){u->id, bot_second};
}

/* No harvester and no money for one: what is left must win it. */
static bool broke(int faction) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (live(u) && u->owner == 0 && (u->traits & MF_HARVESTER)) return false;
    }
    const StaticProductDefinition *p = product_of(RF_Role(faction, RF_ROLE_HARVESTER));
    return !p || level.player_resources[0][0] < p->cost;
}

static int army_size(void) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        n += live(u) && u->owner == 0 && (u->traits & MF_MOBILE) && (u->traits & MF_ATTACK) &&
             !(u->traits & MF_HARVESTER) && !protected_thing(u);
    }
    return n;
}

static void send_units(mobj_t **units, int n, mobj_t *goal, fixed2_t at) {
    int k = 0;
    for (int i = 0; i < n; ++i)
        if (!recently_ordered(units[i])) units[k++] = units[i];
    if (k <= 0) return;
    /* Move, and let units take targets in range on the way, as an attack-move: chasing a
     * structure far off as the attack target can leave a unit with no route (one on a mesa). */
    (void)goal;
    P_MoveUnitsAt(&level, units, k, at);
    for (int i = 0; i < k; ++i) note_order(units[i]);
}

/* Where to fight: the cells to hold, a tagged target, else the nearest enemy structure. */
static bool bot_goal(ivec2_t from, mobj_t **goal, fixed2_t *at) {
    int hold = objective_pending(RF_OBJ_HOLD);
    for (int i = 0; i < RF_ObjectiveCount(); ++i) {
        rf_objective_t o;
        RF_ObjectiveDetail(i, &o);
        mobj_t *tagged = find_id(o.tag_id);
        if (o.kind == RF_OBJ_DESTROY && live(tagged) && RF_ObjectiveState(i) != RF_OBJECTIVE_DONE && hold < 0) {
            *goal = tagged;
            *at = fixed3_xy(tagged->core.position);
            return true;
        }
    }
    if (hold >= 0) {
        rf_objective_t o;
        RF_ObjectiveDetail(hold, &o);
        ivec2_t centre = {o.area.x + o.area.w / 2, o.area.y + o.area.h / 2};
        *goal = nearest_enemy(centre, o.area.w, false);
        *at = *goal ? fixed3_xy((*goal)->core.position) : fixed2_cell_center(centre);
        return true;
    }
    if (objective_pending(RF_OBJ_DESTROY_ALL) >= 0) {
        *goal = nearest_enemy(from, 0, true);
        if (!*goal) *goal = nearest_enemy(from, 0, false);
        if (*goal) *at = fixed3_xy((*goal)->core.position);
        return *goal != NULL;
    }
    return false;
}

/* Play the mission as a player would, with no knowledge the screen does not show. */
static int bot_play(int faction, int index, int minutes) {
    RtsGameModel *model = load_mission(faction, index);
    if (!model) return 1;
    int start_army = 0;
    mobj_t *buf[256];
    start_army = idle_army(buf, 256);
    int waves = 0;
    /* RF_CAMPAIGN_TRACE=blocked also prints the blocked cells ('#'), cliffs ('c') and ramps ('/'). */
    const char *trace = getenv("RF_CAMPAIGN_TRACE");
    for (int y = 0; trace && !strcmp(trace, "blocked") && y < level.height; ++y) {
        char row[256];
        for (int x = 0; x < level.width && x < 255; ++x) {
            int relief = RF_CellRelief(&level, x, y);
            row[x] = relief == RF_RELIEF_RAMP ? '/' : relief == RF_RELIEF_CLIFF ? 'c' :
                     level.blocked[L_Index(&level, x, y)] ? '#' : '.';
            row[x + 1] = '\0';
        }
        printf("  %2d %s\n", y, row);
    }
    ordered_count = 0;
    for (int s = 0; s < minutes * 60 && !G_LevelOutcome(); ++s) {
        bot_second = s;
        /* Hoarding for a credits goal: keep 1500 back once the buildings it needs are up. */
        int credits = objective_pending(RF_OBJ_CREDITS);
        int reserve = 0;
        if (credits >= 0) {
            rf_objective_t o;
            RF_ObjectiveDetail(credits, &o);
            reserve = objective_pending(RF_OBJ_BUILD) >= 0 ? 0 : o.amount;
        }
        bot_build(faction, reserve);
        if (credits < 0) bot_train(faction, 0);
        mobj_t *home = player_first(false);
        if (!home) home = player_first(true);
        if (home) {
            /* defend: an enemy near our structures (or, with none, our protected thing or
             * army) gets the idle army */
            ivec2_t hc = cell_of(home);
            mobj_t *threat = NULL;
            bool structures = player_first(false) != NULL;
            for (thinker_t *th = thinkercap.next; th != &thinkercap && !threat; th = th->next) {
                mobj_t *u = (mobj_t *)th;
                if (live(u) && u->owner == 0 && (!(u->traits & MF_MOBILE) || protected_thing(u) || !structures))
                    threat = nearest_enemy(cell_of(u), 10, false);
            }
            mobj_t *goal = NULL;
            fixed2_t at;
            int n = idle_army(buf, 256);
            if (threat) {
                send_units(buf, n, threat, fixed3_xy(threat->core.position));
            } else if (s % 8 == 0 && bot_goal(hc, &goal, &at) &&
                       ((!structures && n * 4 >= army_size() * 3) ||
                        (structures && n >= (start_army * 5 / 4 > 10 ? start_army * 5 / 4 : 10)) ||
                        (s > 480 && n >= 8) || (broke(faction) && n >= 6))) {
                send_units(buf, n, goal, at);
                waves++;
                if (getenv("RF_CAMPAIGN_SHOTS") && (waves == 1 || waves % 20 == 0)) {
                    char name[96];
                    snprintf(name, sizeof(name), "%s-%ds", strrchr(mission_name, '/') + 1, s);
                    campaign_shot(name, cell_of(buf[0]));
                }
                if (trace && !strcmp(trace, "goal")) {
                    ivec2_t gc = fixed2_cell(at), uc = cell_of(buf[0]);
                    printf("  %d s: %d units at %s to %d,%d; first at %d,%d, ordered %d\n", s, n,
                           goal && goal->info ? goal->info->name : "-", gc.x, gc.y, uc.x, uc.y, P_HasMoveOrder(buf[0]));
                }
            }
        }
        if (trace && !strcmp(trace, "where") && s % 60 == 30) {
            /* a coarse map of the player's mobile units: digits are counts per 4 x 4 block */
            int w4 = (level.width + 3) / 4, h4 = (level.height + 3) / 4;
            for (int by = 0; by < h4; ++by) {
                char row[80] = "";
                for (int bx = 0; bx < w4 && bx < 79; ++bx) {
                    int n = 0, e = 0;
                    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                        const mobj_t *u = (const mobj_t *)th;
                        if (!live(u) || !(u->traits & MF_MOBILE)) continue;
                        ivec2_t c = cell_of(u);
                        if (c.x / 4 == bx && c.y / 4 == by) { n += u->owner == 0; e += u->owner != 0; }
                    }
                    row[bx] = n ? (n > 9 ? '+' : '0' + n) : e ? 'e' : '.';
                    row[bx + 1] = '\0';
                }
                printf("   %s\n", row);
            }
        }
        if (trace && s % 60 == 0)
            printf("  %3d s: credits %5d, units %3d, idle %3d | enemy %3d %3d, credits %5d %5d\n", s,
                   level.player_resources[0][0], count_side(0), idle_army(buf, 256), count_side(1), count_side(2),
                   level.player_resources[1][0], level.player_resources[2][0]);
        if (!ticks(model, RTS_TICRATE)) break;
    }
    outcome_t outcome = G_LevelOutcome();
    printf("%s: bot %s after %d s, %d attacks, %d units left, enemy %d %d, last message \"%s\"\n", mission_name,
           outcome == OUTCOME_VICTORY ? "won" : outcome == OUTCOME_DEFEAT ? "lost" : "did not finish",
           RF_MissionElapsedMs() / 1000, waves, count_side(0), count_side(1), count_side(2), RF_LastMissionMessage());
    for (int i = 0; i < RF_ObjectiveCount(); ++i)
        printf("  [%s] %s\n", RF_ObjectiveState(i) == RF_OBJECTIVE_DONE ? "x" :
               RF_ObjectiveState(i) == RF_OBJECTIVE_FAILED ? "!" : " ", RF_ObjectiveText(i));
    rts_game_model_destroy(model);
    CCHECK(outcome == OUTCOME_VICTORY, "the bot cannot win it");
    return 0;
}

/* "--play=clans-3": one mission by the bot; else every mission of `faction` scripted. */
static int campaign_faction_main(int argc, char **argv, int faction) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    campaign_user_dir();
    if (argc > 1 && !strncmp(argv[1], "--play=", 7)) {
        for (int f = 0; f < RF_FACTION_COUNT; ++f)
            for (int i = 0; i < RF_CAMPAIGN_MISSIONS; ++i) {
                char path[128], name[64];
                RF_MissionPath(f, i, path, sizeof(path));
                snprintf(name, sizeof(name), "maps/campaign/%s.txt", argv[1] + 7);
                if (!strcmp(path, name)) return bot_play(f, i, 25);
            }
        fprintf(stderr, "no mission %s\n", argv[1] + 7);
        return 1;
    }
    for (int i = 0; i < RF_CAMPAIGN_MISSIONS; ++i) {
        RTS_RUN(scripted_win(faction, i));
        RTS_RUN(scripted_losses(faction, i));
    }
    printf("PASS: every %s mission is won by meeting its objectives and lost by losing\n",
           RF_CampaignName(faction));
    return 0;
}

#endif
