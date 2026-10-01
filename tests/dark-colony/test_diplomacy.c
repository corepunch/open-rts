#include "dark-colony.h"
#include "info.h"
#include <assert.h>

static void click(void *ui, app_t *app, int x, int y) {
    SDL_Event event = {.button = {.type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT, .x=x,.y=y}};
    mobjlist_t objects = P_ListMobjs();
    assert(G_CustomUIResponder(ui, app, &level, objects.items, objects.count, &event));
    event.type = SDL_MOUSEBUTTONUP;
    G_CustomUIResponder(ui, app, &level, objects.items, objects.count, &event);
    P_FreeMobjList(&objects);
}

static void input(void *ui, app_t *app, SDL_Event event) {
    mobjlist_t objects = P_ListMobjs();
    assert(G_CustomUIResponder(ui, app, &level, objects.items, objects.count, &event));
    P_FreeMobjList(&objects);
}

int main(void) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(!SDL_Init(SDL_INIT_VIDEO));
    G_InitGame(); P_InitThinkers();
    dc_skirmish_t setup = {0};
    for (int i = 0; i < 8; ++i)
        setup.players[i] = (dc_skirmish_player_t){.team = i, .color = i, .type =
            i < 2 ? DC_PLAYER_HUMAN : i == 2 ? DC_PLAYER_AI : DC_PLAYER_NONE};
    strcpy(setup.players[0].name, "Commander"); strcpy(setup.players[1].name, "Yukito");
    const char *map = "data/DCOLONY/SCENARIO/MPLAYER/D8PLAY01.MAP";
    DC_RequestSkirmish(map, &setup);
    assert(G_DoLoadLevel(map, &level) && P_InitSight()); P_LoadThings(map);
    app_t app = {.win={640,480},.cell={32,32},.running=true};
    void *ui = G_InitCustomUI(&app, "data/DCOLONY"); assert(ui);
    mobj_t *players[2] = {0};
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *actor = (mobj_t *)th;
        if (actor->owner < 2 && (actor->traits & MF_SELECTABLE)) players[actor->owner] = actor;
    }
    assert(players[0] && players[1] && !P_IsAlly(players[0],players[1]));
    click(ui,&app,610,101); /* Tab 3. */
    click(ui,&app,540,250); /* Allies. */
    click(ui,&app,550,160); /* Offer peace to player 1. */
    assert(level.alliance_offers[0][0] & 2);
    assert(!P_IsAlly(players[0],players[1]));
    G_RunTiccmd(1,&(ticcmd_t){.order=TC_ALLY,.target=0,.product=1});
    assert(P_IsAlly(players[0],players[1]) && P_IsAlly(players[1],players[0]));
    assert(!(level.sight.allies[0] & (UINT32_C(0x40000000) >> 1)));
    click(ui,&app,575,160); /* Offer sight independently. */
    assert(!(level.sight.allies[0] & (UINT32_C(0x40000000) >> 1)));
    G_RunTiccmd(1,&(ticcmd_t){.order=TC_SHARE_SIGHT,.target=0,.product=1});
    assert(level.sight.allies[0] & (UINT32_C(0x40000000) >> 1));
    click(ui,&app,550,160); /* Withdrawing peace leaves shared vision intact. */
    assert(!P_IsAlly(players[0],players[1]) && (level.sight.allies[0] & (UINT32_C(0x40000000) >> 1)));
    level.player_resources[0][0] = 1000;
    int money = level.player_resources[1][0];
    click(ui,&app,623,160);
    assert(level.player_resources[0][0] == 1000 && level.player_resources[1][0] == money);
    level.player_resources[0][0] = 1001;
    click(ui,&app,623,160);
    assert(level.player_resources[0][0] == 1 && level.player_resources[1][0] == money + 1000);
    click(ui,&app,599,160); /* Clear player 1's native chat-recipient checkbox. */
    input(ui,&app,(SDL_Event){.key={.type=SDL_KEYDOWN,.keysym={.sym=SDLK_RETURN}}});
    input(ui,&app,(SDL_Event){.text={.type=SDL_TEXTINPUT,.text="hello allies"}});
    input(ui,&app,(SDL_Event){.key={.type=SDL_KEYDOWN,.keysym={.sym=SDLK_RETURN}}});
    assert(chat_text.count == 1 && strstr(chat_text.messages[0].text,"hello allies"));
    consoleplayer = 1;
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_CHAT,.target=1,.text="private"});
    assert(chat_text.count == 1);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_CHAT,.target=2,.text="for Yukito"});
    assert(chat_text.count == 2 && strstr(chat_text.messages[1].text,"Yukito"));
    netgame = true;
    int speed = game_speed;
    G_RunTiccmd(1,&(ticcmd_t){.order=TC_SPEED,.product=10}); assert(game_speed == speed);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_SPEED,.product=130}); assert(game_speed == 130);
    consoleplayer = 0; netgame = false;
    G_ShutdownCustomUI(ui); S_Shutdown(); P_FreeLevel(&level); SDL_Quit();
    puts("PASS: native allies clicks, reciprocal peace, independent sight, credit transfer boundary, chat recipients and host-only speed");
    return 0;
}
