#ifndef __PRODUCTION_REGRESSION__
#define __PRODUCTION_REGRESSION__
#include "engine.h"
#include "info.h"
#include "t_local.h"
#ifdef RTS_GAME_KKND
void A_KkndResearch(mobj_t *actor);
#endif

#define CHECK(c) RTS_CHECK(c, g_game_id, #c)

static void empty_level(void) {
    P_FreeLevel(&level);
    level.width = level.height = 96;
    level.blocked = calloc(96 * 96, 1);
    P_InitThinkers();
}

static mobj_t *spawn_owner(uint16_t type, int owner, fixed2_t position) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(position, 0), type);
    if (!unit) return NULL;
    unit->owner = unit->team = owner;
    unit->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER;
    return unit;
}

/* Runs the universal AI (economy and production only, so no wave disturbs the
 * counts) and the shared production ticker. */
static void ai_run(AiContext *ai, int ticks, int dt_ms) {
    for (int tick = 0; tick < ticks; ++tick) {
        mobjlist_t list = P_ListMobjs();
        P_AiTick(ai, &level, list.items, list.count, gameinfo, dt_ms);
        P_FreeMobjList(&list);
        G_ProductionTicker(dt_ms);
#ifdef RTS_GAME_KKND
        for (int tic = 0; tic < RTS_TICRATE; ++tic)
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
                if (th->function == P_MobjThinker) A_KkndResearch((mobj_t *)th);
#endif
    }
}

static int test_ai_goals(void) {
    empty_level();
    CHECK(level.blocked);
    CHECK(G_AiInterface());
    for (int owner = 0; owner < 3; ++owner) {
        level.player_resources[owner][0] = 50000;
        CHECK(spawn_owner(OWNER_PRODUCER, owner, FIXED2_LIT(16 + owner * 30, 16)));
    }
    AiContext ai;
    P_AiInit(&ai);
    P_AiAttachGame(&ai, G_AiInterface());
    P_AiSetFeatures(&ai, AI_FEATURE_ECONOMY | AI_FEATURE_PRODUCTION | AI_FEATURE_RESEARCH);
    /* The opening ladder alone; the game's test_ai covers the doctrine. */
    for (int owner = 1; owner < 3; ++owner) {
        AiTeamState *team = &ai.teams[owner];
        team->level = G_AiInterface()->player_level(&level, owner);
        team->plan_loaded = R_OwnerPlan(&level, owner, team->level, &team->plan);
        CHECK(team->plan_loaded && team->plan.doctrine.roster_count > 0);
        memset(&team->plan.doctrine, 0, sizeof(team->plan.doctrine));
    }
    ai_run(&ai, 600, 1000);
    CHECK(level.player_resources[0][0] == 50000);
    CHECK(G_CountPlannedActors(0, AI_ADVANCED_UNIT) == 0);
    CHECK(P_AiStats(&ai, 0)->purchases == 0);
    int advanced[3] = {0};
    for (int owner = 1; owner < 3; ++owner) {
        CHECK(level.player_resources[owner][0] < 50000);
        CHECK(P_AiStats(&ai, owner)->purchases > 0);
        advanced[owner] = G_CountPlannedActors(owner, AI_ADVANCED_UNIT);
        CHECK(advanced[owner] >= AI_ADVANCED_COUNT && advanced[owner] <= AI_ADVANCED_MAX);
        int first = G_CountPlannedActors(owner, AI_FIRST_UNIT);
        CHECK(first >= AI_FIRST_COUNT && first <= AI_FIRST_MAX);
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *u = (mobj_t *)th;
            if (u->owner != owner) continue;
            CHECK(!u->production);
            CHECK(u->type_id < gameinfo->mobj_type_count);
            CHECK(u->core.state_id == gameinfo->mobjinfo[u->type_id].spawnstate);
            CHECK(u->allegiance == ALLEGIANCE_ENEMY);
        }
    }
    int money = level.player_resources[1][0];
    ai_run(&ai, 100, 33);
    CHECK(level.player_resources[1][0] == money); /* The ladder is finished. */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        if (unit->owner == 1 && unit->type_id == AI_ADVANCED_UNIT) {
            P_RemoveMobj(unit);
            break;
        }
    }
    CHECK(G_CountPlannedActors(1, AI_ADVANCED_UNIT) == advanced[1] - 1);
    ai_run(&ai, 20, 1000);
    CHECK(G_CountPlannedActors(1, AI_ADVANCED_UNIT) == advanced[1]);
    CHECK(level.player_resources[0][0] == 50000);
    P_FreeLevel(&level);
    puts("PASS: enemy ownership, bounded build goals, progression and replacement");
    return 0;
}

static int test_interactive_queue(void) {
    empty_level();
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, PLAYER_PRODUCT);
    CHECK(product);
    mobj_t *producer = spawn_owner(PLAYER_PRODUCER, 0, FIXED2_LIT(16, 16));
    mobj_t *second = spawn_owner(PLAYER_PRODUCER, 0, FIXED2_LIT(32, 32));
    CHECK(producer && second);
    P_MobjSetSelected(second, true);
    level.player_resources[0][0] = product->cost;
    level.player_resources[1][0] = product->cost;
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    app_t app = {.win = {640, 480}, .cell = {8, 8}};
    V_AllocScreen(app.win.w, app.win.h);
    CHECK(screens[0].pixels);
    /* The engine's product list: a row click buys from the selection. */
    menuitem_t list = {.kind = MI_LIST, .visible = true, .enabled = true, .rect = {480, 32, 160, 416},
                       .row_height = 32, .value = -1, .fill = 0xff0c1216u, .link = -1,
                       .routine = HU_ProductList, .ownerdraw = HU_DrawProducts};
    menu_t bar = {.items = &list, .numitems = 1, .itemOn = -1, .refresh = HU_RefreshProducts};
    SDL_Event click = {.type = SDL_MOUSEBUTTONDOWN};
    click.button.button = SDL_BUTTON_LEFT;
    click.button.x = list.rect.x + 10;
    click.button.y = list.rect.y + 10;
    CHECK(M_MenuResponder(&bar, &app, &click));
    CHECK(!producer->production);
    if (product->product_class == RTS_PRODUCT_BUILDING) {
        CHECK(bar.target == &list && !second->production);
        CHECK(level.player_resources[0][0] == product->cost);
        click.button.x = 40 * app.cell.w;
        click.button.y = 40 * app.cell.h;
        CHECK(M_MenuResponder(&bar, &app, &click));
        CHECK(!bar.target);
    }
    CHECK(second->production && second->production->queue_count == 1);
    CHECK(second->production->product_type == product->product_type);
    CHECK(level.player_resources[0][0] == 0);
    CHECK(level.player_resources[1][0] == product->cost);
    click.button.x = list.rect.x + 10;
    click.button.y = list.rect.y + 10;
    CHECK(M_MenuResponder(&bar, &app, &click));
    CHECK(second->production->queue_count == 1); /* Cannot afford another. */
    V_BeginFrame(0xff000000u);
    M_MenuDrawer(&bar);
    V_ReadPixels(surface->pixels, surface->pitch);
    char screenshot[128];
    snprintf(screenshot, sizeof(screenshot), "/private/tmp/open-rts-production-%s.bmp", g_game_id);
    CHECK(SDL_SaveBMP(surface, screenshot) == 0);
    int initial = G_CountPlannedActors(0, G_ModelActorIdForProduct(product));
    CHECK(G_ProductionTicker(G_ModelProductTrainingTimeMs(product)));
    CHECK(!second->production && G_CountPlannedActors(0, G_ModelActorIdForProduct(product)) == initial);
    CHECK(G_CountPlannedActors(1, G_ModelActorIdForProduct(product)) == 0);
    /* Queue limits, a busy first producer, and ownership validation. */
    level.player_resources[0][0] = product->cost * RTS_MAX_PRODUCTION_QUEUE * 2;
    for (int i = 0; i < RTS_MAX_PRODUCTION_QUEUE; ++i) CHECK(G_QueueProduct(producer, product));
    CHECK(!G_QueueProduct(producer, product));
    CHECK(G_FindProducer(0, product) == second);
    CHECK(G_QueueProduct(second, product));
    V_FreeScreen();
    SDL_FreeSurface(surface);
    P_FreeLevel(&level);
    puts("PASS: interactive sidebar queues on the selected producer and shares production ticking");
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RTS_RUN(test_ai_goals());
    RTS_RUN(test_interactive_queue());
    SDL_Quit();
    return 0;
}
#endif
