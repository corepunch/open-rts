/* A LAN / custom match must hand every human slot the same start the retail
 * game does: its chosen race, 1500 credits, a base of that race, and a working
 * Exploiter / Brozaar purchase (reserve with the product button, start with
 * Build). Everything here goes through the same command path a network
 * game uses: ticcmds queued by the HUD and executed with G_RunTiccmd. */
#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include "t_local.h"
#include <stdio.h>
#include <stdlib.h>

#define REQUIRE(cond, msg) do { if (!(cond)) return rts_fail("lan_start", msg); } while (0)
#define TICKS_PER_SECOND 30
#define BUILD_BUTTON 19
enum { UI_EXPLOITER = 87, UI_BROZAAR = 46, START_CREDITS = 1500 };

static const char *MAPS[] = {"SCENARIO/MPLAYER/D2PLAY01.MAP", "SCENARIO/MPLAYER/J4PLAY01.MAP"};

static RtsGameModel *start_lan(const char *map, const int races[2], int humans) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = map};
    dc_skirmish_t setup = {.quantity = 4, .flow = 4, .erupting = 1};
    for (int i = 0; i < 8; ++i)
        setup.players[i] = (dc_skirmish_player_t){.type = i < humans ? DC_PLAYER_HUMAN : DC_PLAYER_NONE,
                                                  .race = i < 2 ? races[i] : 0, .color = i, .team = i};
    DC_RequestSkirmish(map, &setup);
    if (!model || !rts_game_model_load(model, &config)) return NULL;
    return model;
}

static int count_owned(int owner, int type) {
    int n = 0;
    mobjlist_t l = P_ListMobjs();
    for (int i = 0; i < l.count; ++i)
        n += l.items[i]->owner == owner && !l.items[i]->remove && l.items[i]->hp > 0 &&
             (type < 0 || l.items[i]->type_id == type);
    P_FreeMobjList(&l);
    return n;
}

static int count_harvesters(int owner) {
    int n = 0;
    mobjlist_t l = P_ListMobjs();
    for (int i = 0; i < l.count; ++i)
        n += l.items[i]->owner == owner && !l.items[i]->remove && l.items[i]->hp > 0 &&
             (l.items[i]->traits & MF_HARVESTER);
    P_FreeMobjList(&l);
    return n;
}

/* Race-specific base structures: human pods or alien hives. */
static int count_race_buildings(int owner, int race) {
    int n = 0;
    mobjlist_t l = P_ListMobjs();
    for (int i = 0; i < l.count; ++i) {
        const mobj_t *m = l.items[i];
        int t = m->type_id;
        if (m->owner != owner || m->remove || m->hp <= 0) continue;
        n += race ? (t >= MT_ALIEN_MINDHIVE && t <= MT_ALIEN_RSCHIVE) : (t >= MT_EXCOPOD && t <= MT_RSCHPOD);
    }
    P_FreeMobjList(&l);
    return n;
}

static bool tick_seconds(RtsGameModel *model, int seconds) {
    for (int t = 0; t < seconds * TICKS_PER_SECOND; ++t) if (!rts_tick(model, NULL)) return false;
    return true;
}

/* The executable's tic body (driver/d_main.c), including the custom UI's
 * production update, which differs from the model's G_ProductionTicker. */
static void driver_tic(menu_t *ui, AiContext *ai) {
    P_Ticker();
    mobjlist_t objects = P_ListMobjs();
    int count = objects.count;
    P_AiTick(ai, &level, objects.items, count, gameinfo, (int)(FIXED_DT * 1000));
    G_UpdateProduction(&level, objects.items, &count, FIXED_DT);
    P_FreeMobjList(&objects);
}

static void driver_seconds(menu_t *ui, AiContext *ai, int seconds) {
    for (int t = 0; t < seconds * TICKS_PER_SECOND; ++t) driver_tic(ui, ai);
}

/* Executes everything the local HUD queued, as the network tic loop would. */
static void pump(int player) {
    ticcmd_t cmd;
    for (G_BuildTiccmd(&cmd); cmd.order != TC_NONE; G_BuildTiccmd(&cmd)) G_RunTiccmd(player, &cmd);
}

static void click(menu_t *ui, app_t *app, int id, int button) {
    FILE *file = fopen("data/DCOLONY/INTRFACE/MAINE", "r");
    char line[512], kind[16];
    irect_t rect = {0};
    while (file && fgets(line, sizeof(line), file)) {
        int control, description;
        irect_t value;
        if (sscanf(line, "%15s %d %d %d %d %d %d", kind, &control, &description,
                   &value.x, &value.y, &value.w, &value.h) != 7 || control != id) continue;
        if (!strcmp(kind, "pushb") || !strcmp(kind, "checkb") || !strcmp(kind, "count")) { rect = value; break; }
    }
    if (file) fclose(file);
    assert(rect.w > 0 && rect.h > 0);
    SDL_Event event = {.button = {.type = SDL_MOUSEBUTTONDOWN, .button = button,
                                  .x = rect.x + rect.w / 2, .y = rect.y + rect.h / 2}};
    mobjlist_t objects = P_ListMobjs();
    assert(t_hud_event(ui, app, objects.items, objects.count, &event));
    P_FreeMobjList(&objects);
}

/* Both slots begin as their chosen race, funded with 1500, with a base. */
static int test_start(const char *map, const int races[2]) {
    RtsGameModel *model = start_lan(map, races, 2);
    REQUIRE(model, "LAN skirmish loads");
    for (int p = 0; p < 2; ++p) {
        REQUIRE(DC_PlayerRace(p) == races[p], "slot keeps the race chosen in the lobby");
        REQUIRE(level.player_resources[p][0] == START_CREDITS, "every human starts with 1500 credits");
        REQUIRE(count_owned(p, -1) > 0, "every human slot owns units");
        REQUIRE(count_owned(p, races[p] ? MT_ALIEN_MINDHIVE : MT_EXCOPOD) == 1,
                "the starting base is the race's exploiter pod / mindhive");
        REQUIRE(count_race_buildings(p, !races[p]) == 0, "no buildings of the other race");
    }
    rts_game_model_destroy(model);
    return 0;
}

/* Reserve, then Build: charged once, harvester appears, no leftovers. */
static int test_purchase_commands(const char *map, const int races[2], int player) {
    RtsGameModel *model = start_lan(map, races, 2);
    REQUIRE(model, "LAN skirmish loads");
    int ui = races[player] ? UI_BROZAAR : UI_EXPLOITER;
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui);
    REQUIRE(product && product->cost > 0 && product->cost <= START_CREDITS, "harvester is affordable at start");
    int row = product->row_id, before = count_harvesters(player), other = count_harvesters(!player);
    netactive = true; consoleplayer = player;

    G_RunTiccmd(player, &(ticcmd_t){.order = TC_PURCHASE, .product = ui});
    REQUIRE(level.purchases[player][row].selected == 1, "product click reserves one");
    REQUIRE(level.player_resources[player][0] == START_CREDITS - product->cost, "reservation is charged");
    REQUIRE(count_harvesters(player) == before, "reserving alone builds nothing");
    REQUIRE(level.purchases[!player][row].selected == 0 &&
            level.player_resources[!player][0] == START_CREDITS, "other player is untouched");

    G_RunTiccmd(player, &(ticcmd_t){.order = TC_SUBMIT});
    REQUIRE(level.purchases[player][row].selected == 0, "Build consumes the reservation");
    REQUIRE(level.player_resources[player][0] == START_CREDITS - product->cost, "Build does not charge twice");
    int ready = G_ModelProductTrainingTimeMs(product) / 33, tic = 0;
    for (; tic < 5 * TICKS_PER_SECOND && count_harvesters(player) == before; ++tic)
        REQUIRE(rts_tick(model, NULL), "simulation runs");
    REQUIRE(count_harvesters(player) == before + 1, "the harvester appears");
    REQUIRE(tic >= ready - 2 && tic <= ready + TICKS_PER_SECOND,
            "the harvester leaves after its native release time, not a cost-based timer");
    REQUIRE(tick_seconds(model, 30), "simulation runs");
    REQUIRE(count_harvesters(!player) == other, "the other player gains no harvester");
    REQUIRE(level.purchases[player][row].queued == 0, "nothing stays queued");
    REQUIRE(level.player_resources[player][0] >= START_CREDITS - product->cost, "no extra charge");
    netactive = false; consoleplayer = 0;
    rts_game_model_destroy(model);
    return 0;
}

/* The real sidebar: product button, then Build, for either slot. */
static int test_purchase_hud(const char *map, const int races[2], int player) {
    RtsGameModel *model = start_lan(map, races, 2);
    REQUIRE(model, "LAN skirmish loads");
    app_t app = {.win = {640, 480}, .cell = {32, 32}, .running = true};
    netactive = true; consoleplayer = player;
    menu_t *ui = G_InitHUD(&app, "data/DCOLONY");
    REQUIRE(ui, "HUD loads");
    int id = races[player] ? UI_BROZAAR : UI_EXPLOITER;
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, id);
    int before = count_harvesters(player);
    G_ClearTiccmds();
    REQUIRE(tick_seconds(model, 2), "HUD settles");
    mobjlist_t objects = P_ListMobjs();
    mobj_t *base = NULL;
    for (int i = 0; i < objects.count; ++i) {
        mobj_t *m = objects.items[i];
        P_MobjSetSelected(m, false);
        if (m->owner == player && m->type_id == (races[player] ? MT_ALIEN_MINDHIVE : MT_EXCOPOD)) base = m;
    }
    REQUIRE(base, "the slot owns its base");
    P_MobjSetSelected(base, true);
    P_FreeMobjList(&objects);

    int funds = level.player_resources[player][0]; /* Includes exo income accrued while settling. */
    click(ui, &app, 0, SDL_BUTTON_LEFT); pump(player);
    click(ui, &app, id, SDL_BUTTON_LEFT);
    pump(player);
    REQUIRE(level.purchases[player][product->row_id].selected == 1, "clicking the product shows 1 on its badge");
    REQUIRE(level.player_resources[player][0] == funds - product->cost, "badge reserves the cost");
    click(ui, &app, BUILD_BUTTON, SDL_BUTTON_LEFT); pump(player);
    AiContext ai;
    P_AiInit(&ai);
    P_AiAttachGame(&ai, G_AiInterface());
    int tic = 0;
    for (; tic < 5 * TICKS_PER_SECOND && count_harvesters(player) == before; ++tic) driver_tic(ui, &ai);
    REQUIRE(count_harvesters(player) == before + 1, "Build produces the harvester");
    REQUIRE(tic <= G_ModelProductTrainingTimeMs(product) / 33 + TICKS_PER_SECOND,
            "the harvester appears within a second of its release time after Build");
    driver_seconds(ui, &ai, 30);
    REQUIRE(level.player_resources[player][0] >= funds - product->cost, "charged once");
    netactive = false; consoleplayer = 0;
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return rts_fail("lan_start", "SDL video init");
    static const int combos[4][2] = {{0, 1}, {1, 0}, {0, 0}, {1, 1}};
    for (unsigned m = 0; m < sizeof(MAPS) / sizeof(MAPS[0]); ++m)
        for (int c = 0; c < 4; ++c) {
            RTS_RUN(test_start(MAPS[m], combos[c]));
            for (int player = 0; player < 2; ++player) {
                RTS_RUN(test_purchase_commands(MAPS[m], combos[c], player));
                RTS_RUN(test_purchase_hud(MAPS[m], combos[c], player));
            }
        }
    puts("PASS: LAN start gives each slot its race, 1500 credits and a base; Exploiter/Brozaar purchase works");
    return 0;
}
