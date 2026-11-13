#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II transports", #c)

static mobj_t *spawn(int type, fixed2_t at, int owner, uint8_t allegiance) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(at, 0), type);
    assert(unit);
    unit->owner = unit->team = owner;
    unit->allegiance = allegiance;
    unit->traits |= MF_NOAUTOTARGET;
    return unit;
}

/* Same coastline as test_naval.c: land from x = 11, a coast column at x = 10. */
static void fixture(void) {
    P_FreeLevel(&level); P_InitThinkers(); G_InitGame();
    consoleplayer = 0; leveltime = 1;
    level.width = level.height = 32;
    level.tile_ids = calloc(1024, sizeof(*level.tile_ids));
    level.blocked = calloc(1024, 1);
    level.cell_solid = calloc(1024, 1);
    level.cell_terrain = calloc(1024, 1);
    level.speeds = calloc(1, sizeof(*level.speeds));
    level.speeds->class_count = 5;
    level.speeds->terrain[1][0] = 100;
    level.speeds->terrain[2][1] = 100;
    level.speeds->terrain[4][1] = level.speeds->terrain[4][3] = 100;
    level.speeds->footprint[2] = level.speeds->footprint[4] = 2;
    for (int y = 0; y < 32; ++y) for (int x = 10; x < 32; ++x) {
        level.cell_terrain[L_Index(&level, x, y)] = x == 10 ? 3 : 1;
        level.blocked[L_Index(&level, x, y)] = 1;
    }
    for (int r = 0; r < 3; ++r) level.player_resources[0][r] = 10000;
}

static int aboard(const mobj_t *ship, uint32_t *ids) {
    return W2_PassengerIds(ship, ids, 9);
}

static bool in_list(uint32_t id, const uint32_t *ids, int count) {
    for (int i = 0; i < count; ++i) if (ids[i] == id) return true;
    return false;
}

/* Six footmen board a beached transport; a seventh is refused. */
static mobj_t *loaded_ship(mobj_t *passengers[6], mobj_t **spare) {
    mobj_t *ship = spawn(MT_HUMAN_TRANSPORT, FIXED2_LIT(11, 13), 0, ALLEGIANCE_PLAYER);
    if (!P_CheckPosition(&level, ship, FIXED2_LIT(11, 13))) return NULL;
    for (int i = 0; i < 6; ++i) {
        passengers[i] = spawn(MT_FOOTMAN, FIXED2_LIT(9.5, 12.5), 0, ALLEGIANCE_PLAYER);
        if (!W2_BoardOrder(passengers[i], ship)) return NULL;
    }
    P_Ticker();
    *spare = spawn(MT_FOOTMAN, FIXED2_LIT(9.5, 12.5), 0, ALLEGIANCE_PLAYER);
    for (int i = 0; i < 6; ++i) if (!passengers[i]->w2.boarded) return NULL;
    return ship;
}

/* Selective unload: one chosen passenger lands at the shore, the rest stay
 * aboard; a stale or foreign target changes nothing; unload-all still empties
 * the ship. */
static int unload_one(void) {
    fixture();
    mobj_t *passengers[6], *spare = NULL;
    mobj_t *ship = loaded_ship(passengers, &spare);
    CHECK(ship && spare);
    CHECK(!W2_BoardOrder(spare, ship));  /* capacity is six */
    uint32_t ids[9];
    CHECK(aboard(ship, ids) == 6);

    /* A passenger id is the only valid addressed target. */
    fixed3_t shore = fixed3_from_fixed2(fixed3_xy(ship->core.position), 0);
    ticcmd_t stranger = {.order = TC_UNLOAD, .count = 1, .units = {ship->id}, .target = spare->id, .position = shore};
    G_RunTiccmd(0, &stranger);
    for (int i = 0; i < 10; ++i) P_Ticker();
    CHECK(aboard(ship, ids) == 6 && !spare->w2.boarded);

    mobj_t *chosen = passengers[2];
    uint32_t chosen_id = chosen->id;
    ticcmd_t one = {.order = TC_UNLOAD, .count = 1, .units = {ship->id}, .target = chosen_id, .position = shore};
    G_RunTiccmd(0, &one);
    for (int i = 0; i < 40; ++i) P_Ticker();
    CHECK(!chosen->w2.boarded && !chosen->w2.carrier && !(chosen->traits & MF_NOBLOCKMAP) &&
          P_MobjById(chosen_id) == chosen);
    CHECK(!ship->w2.unloading && ship->w2.unload_one == 0);
    CHECK(aboard(ship, ids) == 5 && !in_list(chosen_id, ids, 5));
    for (int i = 0; i < 6; ++i) if (i != 2) CHECK(passengers[i]->w2.boarded && passengers[i]->w2.carrier == ship->id);
    CHECK(P_CheckPosition(&level, chosen, fixed3_xy(chosen->core.position)));
    P_FreeLevel(&level);
    return 0;
}

/* Unload all empties a ship whose passengers all find a bay: the west coast
 * offers four landing cells beside a 2x2 hull. */
static int unload_all(void) {
    fixture();
    mobj_t *passengers[4];
    mobj_t *ship = spawn(MT_HUMAN_TRANSPORT, FIXED2_LIT(11, 13), 0, ALLEGIANCE_PLAYER);
    CHECK(P_CheckPosition(&level, ship, FIXED2_LIT(11, 13)));
    for (int i = 0; i < 4; ++i) {
        passengers[i] = spawn(MT_FOOTMAN, FIXED2_LIT(9.5, 12.5), 0, ALLEGIANCE_PLAYER);
        CHECK(W2_BoardOrder(passengers[i], ship));
    }
    P_Ticker();
    uint32_t ids[9];
    CHECK(aboard(ship, ids) == 4);
    ticcmd_t all = {.order = TC_UNLOAD, .count = 1, .units = {ship->id},
        .position = fixed3_from_fixed2(fixed3_xy(ship->core.position), 0)};
    G_RunTiccmd(0, &all);
    for (int i = 0; i < 40; ++i) P_Ticker();
    CHECK(aboard(ship, ids) == 0 && !ship->w2.unloading && ship->w2.unload_one == 0);
    for (int i = 0; i < 4; ++i) CHECK(!passengers[i]->w2.boarded && !passengers[i]->w2.carrier);
    P_FreeLevel(&level);
    return 0;
}

/* The status row lists a selected transport's passengers as targets, one
 * unit each; the command card keeps its unload-all button. */
static int status_row(void) {
    fixture();
    mobj_t *passengers[3];
    mobj_t *ship = spawn(MT_HUMAN_TRANSPORT, FIXED2_LIT(11, 13), 0, ALLEGIANCE_PLAYER);
    CHECK(P_CheckPosition(&level, ship, FIXED2_LIT(11, 13)));
    for (int i = 0; i < 3; ++i) {
        passengers[i] = spawn(MT_FOOTMAN, FIXED2_LIT(9.5, 12.5), 0, ALLEGIANCE_PLAYER);
        CHECK(W2_BoardOrder(passengers[i], ship));
    }
    P_Ticker();
    P_MobjSetSelected(ship, true);
    mobj_t *selected[] = {ship};
    hudview = (hudview_t){.units = selected, .unit_count = 1};
    app_t app = {.win = {640, 480}, .cell = {32, 32}};
    menu_t *menu = G_InitHUD(&app, "data/WAR2");
    CHECK(menu && menu->refresh);
    menu->refresh(menu);
    int rows = 0, all = 0;
    for (int i = 0; i < menu->numitems; ++i) {
        const menuitem_t *item = &menu->items[i];
        if (!item->tooltip || !item->visible) continue;
        if (!strcmp(item->tooltip, "Unload")) ++all;
        else if (!strncmp(item->tooltip, "Unload ", 7) && item->enabled) ++rows;
    }
    CHECK(rows == 3 && all == 1);
    /* With no passengers left the row is empty again. */
    for (int i = 0; i < 3; ++i) P_RemoveMobj(passengers[i]);
    menu->refresh(menu);
    rows = 0;
    for (int i = 0; i < menu->numitems; ++i)
        if (menu->items[i].tooltip && menu->items[i].visible && !strncmp(menu->items[i].tooltip, "Unload ", 7)) ++rows;
    CHECK(rows == 0);
    hudview = (hudview_t){0};
    P_FreeLevel(&level);
    return 0;
}

/* Allied owners board an allied transport; enemies are refused. */
static int allies(void) {
    fixture();
    mobj_t *ship = spawn(MT_HUMAN_TRANSPORT, FIXED2_LIT(11, 13), 0, ALLEGIANCE_PLAYER);
    CHECK(P_CheckPosition(&level, ship, FIXED2_LIT(11, 13)));
    mobj_t *ally = spawn(MT_FOOTMAN, FIXED2_LIT(9.5, 12.5), 1, ALLEGIANCE_ALLIED);
    mobj_t *enemy = spawn(MT_FOOTMAN, FIXED2_LIT(9.5, 12.5), 2, ALLEGIANCE_ENEMY);
    CHECK(!W2_BoardOrder(enemy, ship));
    /* The ally is commanded by its own player, so the order travels as player 1. */
    ticcmd_t board = {.order = TC_BOARD, .count = 1, .units = {ally->id}, .target = ship->id};
    G_RunTiccmd(1, &board);
    P_Ticker();
    uint32_t ids[9];
    CHECK(ally->w2.boarded && ally->w2.carrier == ship->id && !enemy->w2.boarded);
    CHECK(aboard(ship, ids) == 1 && ids[0] == ally->id);
    P_FreeLevel(&level);
    return 0;
}

int main(void) {
    RTS_RUN(unload_one());
    RTS_RUN(unload_all());
    RTS_RUN(status_row());
    RTS_RUN(allies());
    puts("PASS: transports unload one chosen passenger or all at the shore, refuse strangers, take allied boarders, and list passengers in the status row");
    return 0;
}
