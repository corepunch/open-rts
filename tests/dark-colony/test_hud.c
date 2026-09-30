#include "game.h"
#include "g_game.h"
#include "engine.h"
#include "info.h"
#include "d_net.h"
#include "../rts_model_test.h"
#include <assert.h>

static mobj_t *find(int type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *actor = (mobj_t *)th;
        if (!actor->remove && actor->owner == 0 && actor->type_id == type) return actor;
    }
    return NULL;
}

static void click(void *ui, app_t *app, int id, int button) {
    FILE *file = fopen("data/DCOLONY/INTRFACE/MAINE", "r");
    assert(file);
    char line[512], kind[16];
    irect_t rect = {0};
    while (fgets(line, sizeof(line), file)) {
        int control, description;
        irect_t value;
        if (sscanf(line, "%15s %d %d %d %d %d %d", kind, &control, &description,
                   &value.x, &value.y, &value.w, &value.h) != 7 || control != id) continue;
        if (!strcmp(kind,"pushb") || !strcmp(kind,"checkb") || !strcmp(kind,"count")) {
            rect = value; break;
        }
    }
    fclose(file);
    assert(rect.w > 0 && rect.h > 0);
    SDL_Event event = {.button = {.type = SDL_MOUSEBUTTONDOWN, .button = button,
                                  .x = rect.x + rect.w / 2, .y = rect.y + rect.h / 2}};
    mobjlist_t objects = P_ListMobjs();
    assert(G_CustomUIResponder(ui, app, &level, objects.items, objects.count, &event));
    P_FreeMobjList(&objects);
}

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = "SCENARIO/HUMAN/HUMAN02.MAP"};
    assert(model && rts_game_model_load(model, &config));
    if (level.destroy_mission) level.destroy_mission(level.mission);
    level.mission = NULL; level.destroy_mission = NULL;
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0,640,480,32,SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(r_renderer);
    app_t app = {.renderer = r_renderer, .win = {640,480}, .cell = {32,32}, .running = true};
    void *ui = G_InitCustomUI(&app, "data/DCOLONY");
    assert(ui);
    mobj_t *barracks = find(MT_BRRKPOD), *center = find(MT_EXCOPOD);
    assert(barracks && center);
    for (int t = 0; t < 300; ++t) P_Ticker();
    level.player_resources[0][0] = 100000;
    click(ui,&app,0,SDL_BUTTON_LEFT);
    click(ui,&app,81,SDL_BUTTON_LEFT);
    assert(level.player_resources[0][0] == 98000 && level.purchases[0][2].selected == 1);
    assert(!find(MT_SCNCPOD)); /* Product clicks reserve; Build starts construction. */
    click(ui,&app,81,SDL_BUTTON_LEFT);
    assert(level.player_resources[0][0] == 98000);
    click(ui,&app,81,SDL_BUTTON_RIGHT);
    assert(level.player_resources[0][0] == 100000 && !level.purchases[0][2].selected);
    click(ui,&app,81,SDL_BUTTON_LEFT);
    click(ui,&app,19,SDL_BUTTON_LEFT);
    assert(find(MT_SCNCPOD) && level.player_resources[0][0] == 98000);
    click(ui,&app,81,SDL_BUTTON_LEFT);
    assert(level.player_resources[0][0] == 98000); /* No duplicate construction. */
    click(ui,&app,89,SDL_BUTTON_LEFT);
    click(ui,&app,89,SDL_BUTTON_LEFT);
    assert(!barracks->production && level.purchases[0][9].selected == 2);
    assert(level.player_resources[0][0] == 97300);
    click(ui,&app,89,SDL_BUTTON_RIGHT);
    assert(level.player_resources[0][0] == 97650 && level.purchases[0][9].selected == 1);
    click(ui,&app,19,SDL_BUTTON_LEFT);
    assert(barracks->production && barracks->production->queue_count == 1);
    assert(!level.purchases[0][9].selected && level.player_resources[0][0] == 97650);
    for (int t = 0; t < 300; ++t) P_Ticker();
    click(ui,&app,1,SDL_BUTTON_LEFT);
    click(ui,&app,110,SDL_BUTTON_LEFT);
    assert(!level.upgrades[0][0].weapon && level.purchases[0][59].selected == 1);
    click(ui,&app,19,SDL_BUTTON_LEFT);
    assert(level.upgrades[0][0].weapon == 1 && !level.upgrades[0][1].weapon);
    int money = level.player_resources[0][0];
    click(ui,&app,110,SDL_BUTTON_LEFT);
    assert(level.player_resources[0][0] == money); /* Purchased tier stays hidden. */
    click(ui,&app,2,SDL_BUTTON_LEFT);
    click(ui,&app,196,SDL_BUTTON_LEFT);
    assert(paused);
    int stopped_at = leveltime;
    assert(rts_game_model_tick(model,FIXED_DT) && leveltime == stopped_at);
    click(ui,&app,196,SDL_BUTTON_LEFT);
    assert(!paused);
    mobj_t *trooper = P_SpawnMobj(center->core.position,MT_TROOPER);
    assert(trooper);
    P_MobjSetSelected(trooper,true);
    click(ui,&app,0,SDL_BUTTON_LEFT);
    click(ui,&app,33,SDL_BUTTON_LEFT);
    assert(trooper->move_only);
    click(ui,&app,35,SDL_BUTTON_LEFT);
    assert(!trooper->move_only);
    G_SelectedTiccmd(TC_MOVE,&trooper,1,(fvec2_t){30.5f,30.5f},0);
    click(ui,&app,150,SDL_BUTTON_LEFT);
    assert(!P_HasMoveOrder(trooper));
    click(ui,&app,36,SDL_BUTTON_LEFT);
    SDL_Event point = {.button = {.type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT,.x = 100,.y = 100}};
    assert(G_CustomUIResponder(ui,&app,&level,&trooper,1,&point));
    point.button.x = 150;
    assert(G_CustomUIResponder(ui,&app,&level,&trooper,1,&point));
    assert(!trooper->waypoints.count && !P_HasMoveOrder(trooper));
    point.button.button = SDL_BUTTON_RIGHT;
    assert(G_CustomUIResponder(ui,&app,&level,&trooper,1,&point));
    assert(trooper->waypoints.count == 2);
    click(ui,&app,150,SDL_BUTTON_LEFT);
    assert(!trooper->waypoints.count && !P_HasMoveOrder(trooper));
    G_ShutdownCustomUI(ui);
    SDL_DestroyRenderer(r_renderer); r_renderer = NULL;
    SDL_FreeSurface(surface); SDL_Quit();
    rts_game_model_destroy(model);
    puts("PASS: native HUD tabs, reserved purchases/refunds/Build, research, pause, orders and waypoints");
    return 0;
}
