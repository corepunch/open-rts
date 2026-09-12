#ifndef __PRODUCTION_REGRESSION__
#define __PRODUCTION_REGRESSION__
#include "game.h"
#include "sb_bar.h"
#include "info.h"
#include "rts_test.h"
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

static mobj_t *spawn_owner(uint16_t type, int owner, fvec2_t position) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2(position, 0), type);
    if (!unit) return NULL;
    unit->owner = unit->team = owner;
    unit->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER;
    return unit;
}

static int test_ai_goals(void) {
    empty_level();
    CHECK(level.blocked);
    for (int owner = 0; owner < 3; ++owner) {
        level.player_resources[owner][0] = 50000;
        CHECK(spawn_owner(OWNER_PRODUCER, owner, (fvec2_t){16 + owner * 30, 16}));
    }
    for (int tick = 0; tick < 600; ++tick) {
        G_ModelAIProduction(NULL, 1000);
        G_ProductionTicker(1.0f);
#ifdef RTS_GAME_KKND
        for (int tic = 0; tic < RTS_TICRATE; ++tic)
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
                if (th->function == P_MobjThinker) A_KkndResearch((mobj_t *)th);
#endif
    }
    CHECK(level.player_resources[0][0] == 50000);
    CHECK(G_CountPlannedActors(0, AI_ADVANCED_UNIT) == 0);
    for (int owner = 1; owner < 3; ++owner) {
        CHECK(level.player_resources[owner][0] < 50000);
        CHECK(G_CountPlannedActors(owner, AI_ADVANCED_UNIT) == AI_ADVANCED_COUNT);
        CHECK(G_CountPlannedActors(owner, AI_FIRST_UNIT) == AI_FIRST_COUNT);
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
    for (int i = 0; i < 100; ++i) G_ModelAIProduction(NULL, 33);
    CHECK(level.player_resources[1][0] == money);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        if (unit->owner == 1 && unit->type_id == AI_ADVANCED_UNIT) {
            P_RemoveMobj(unit);
            break;
        }
    }
    CHECK(G_CountPlannedActors(1, AI_ADVANCED_UNIT) == AI_ADVANCED_COUNT - 1);
    G_ModelAIProduction(NULL, 33);
    CHECK(G_CountPlannedActors(1, AI_ADVANCED_UNIT) == AI_ADVANCED_COUNT);
    CHECK(level.player_resources[0][0] == 50000);
    P_FreeLevel(&level);
    puts("PASS: enemy ownership, bounded build goals, progression and replacement");
    return 0;
}

static int test_interactive_queue(void) {
    empty_level();
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, PLAYER_PRODUCT);
    CHECK(product);
    mobj_t *producer = spawn_owner(PLAYER_PRODUCER, 0, (fvec2_t){16, 16});
    mobj_t *second = spawn_owner(PLAYER_PRODUCER, 0, (fvec2_t){32, 32});
    CHECK(producer && second);
    P_MobjSetSelected(second, true);
    level.player_resources[0][0] = product->cost;
    level.player_resources[1][0] = product->cost;
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    app_t app = {.renderer = SDL_CreateSoftwareRenderer(surface), .win = {640, 480}};
    CHECK(app.renderer);
    sb_state_t bar;
    CHECK(SB_Init(&bar, app.renderer, g_game_default_root, gameui));
    SDL_Event click = {.type = SDL_MOUSEBUTTONDOWN};
    click.button.button = SDL_BUTTON_LEFT;
    click.button.x = (gameui->command_grid.x + 10) * app.win.w / gameui->logical_width;
    click.button.y = (gameui->command_grid.y + 10) * app.win.h / gameui->logical_height;
    CHECK(SB_ProductionResponder(&bar, &app, &click));
    CHECK(!producer->production);
    CHECK(second->production && second->production->queue_count == 1);
    CHECK(second->production->product_type == product->product_type);
    CHECK(level.player_resources[0][0] == 0);
    CHECK(level.player_resources[1][0] == product->cost);
    CHECK(SB_ProductionResponder(&bar, &app, &click));
    CHECK(second->production->queue_count == 1); /* Cannot afford another. */
    SB_ProductionDrawer(&bar, &app);
    char screenshot[128];
    snprintf(screenshot, sizeof(screenshot), "/private/tmp/open-rts-production-%s.bmp", g_game_id);
    CHECK(SDL_SaveBMP(surface, screenshot) == 0);
    int initial = G_CountPlannedActors(0, G_ModelActorIdForProduct(product));
    CHECK(G_ProductionTicker((float)G_ModelProductTrainingTimeMs(product) / 1000));
    CHECK(!second->production && G_CountPlannedActors(0, G_ModelActorIdForProduct(product)) == initial);
    CHECK(G_CountPlannedActors(1, G_ModelActorIdForProduct(product)) == 0);
    /* Queue limits, a busy first producer, and ownership validation. */
    level.player_resources[0][0] = product->cost * RTS_MAX_PRODUCTION_QUEUE * 2;
    for (int i = 0; i < RTS_MAX_PRODUCTION_QUEUE; ++i) CHECK(G_QueueProduct(producer, product));
    CHECK(!G_QueueProduct(producer, product));
    CHECK(G_FindProducer(0, product) == second);
    CHECK(G_QueueProduct(second, product));
    SB_Shutdown(&bar);
    SDL_DestroyRenderer(app.renderer);
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
