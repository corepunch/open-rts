#include "t_local.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c, "StarCraft saves", #c)

int main(void) {
    G_InitGame();
    P_InitThinkers();
    CHECK(G_DoLoadLevel("data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk", &level));
    CHECK(P_LoadThings(level.map_path) > 0 && P_InitSight());
    sc_mission_t *m = level.mission;
    CHECK(m && m->trig_count > 0);
    m->switches[12] = 1;
    m->deaths[1][0] = 7;
    m->elapsed_ms = 12345;
    m->countdown_ms = 9876;
    m->ai_on[4] = false;
    m->rt[0] = (sc_trig_t){.cursor = 3, .wait_ms = 1200, .current = 1, .running = true};
    snprintf(m->objectives, sizeof(m->objectives), "Saved objective");
    app_t app = {0};
    AiContext ai = {0};
    hudtext_t hud = {0};
    void *saved = NULL;
    size_t length = 0;
    CHECK(G_SaveGameBlob(&saved, &length, "mission", &app, &ai, &hud));
    /* Reload the map as the driver does, so no saved pointer can accidentally
     * stand in for the new level's CHK allocation or trigger runtime. */
    P_FreeLevel(&level);
    CHECK(G_DoLoadLevel("data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk", &level));
    CHECK(P_LoadThings(level.map_path) > 0 && P_InitSight());
    m = level.mission;
    const uint8_t *strings = m->str;
    blob_t file = m->file;
    const uint8_t *triggers = m->trig, *properties = m->uprp;
    sc_trig_t *runtime = m->rt;
    CHECK(G_LoadGameBlob(saved, length, &app, &ai, &hud));
    CHECK(m->str == strings && m->switches[12] == 1 && m->deaths[1][0] == 7);
    CHECK(m->file.bytes == file.bytes && m->trig == triggers && m->uprp == properties && m->rt == runtime);
    CHECK(m->elapsed_ms == 12345 && m->countdown_ms == 9876 && !m->ai_on[4]);
    CHECK(m->rt[0].cursor == 3 && m->rt[0].wait_ms == 1200 && m->rt[0].running);
    CHECK(!strcmp(m->objectives, "Saved objective"));
    free(saved);
    size_t extra_size = G_SaveExtraSize();
    saved = malloc(extra_size);
    CHECK(saved);
    G_SaveExtra(saved);
    ((sc_mission_t *)saved)->trig_count++;
    CHECK(!G_LoadExtra(saved, extra_size) && m->elapsed_ms == 12345);
    CHECK(!G_LoadExtra(saved, extra_size - 1));
    G_SaveExtra(saved);
    sc_trig_t *bad = (sc_trig_t *)((uint8_t *)saved + extra_size - (size_t)m->trig_count * 8 * sizeof(*bad));
    bad->cursor = 65;
    CHECK(!G_LoadExtra(saved, extra_size) && m->rt[0].cursor == 3);
    bad->cursor = 3; bad->current = 8;
    CHECK(!G_LoadExtra(saved, extra_size) && m->rt[0].current == 1);
    free(saved);
    P_FreeLevel(&level);
    /* A fresh session has no custom-lobby state. Restore its player before
     * constructing the HUD, whose dialog art is selected by player race. */
    const char *map = "data/STARCRAFT/maps/(2)road war.scm/staredit/scenario.chk";
    int kinds[8] = {SC_SLOT_COMPUTER, SC_SLOT_HUMAN, SC_SLOT_CLOSED, SC_SLOT_CLOSED,
                    SC_SLOT_CLOSED, SC_SLOT_CLOSED, SC_SLOT_CLOSED, SC_SLOT_CLOSED};
    int races[8] = {0, 2};
    sc_set_custom_slots(kinds, races);
    CHECK(G_DoLoadLevel(map, &level));
    CHECK(P_LoadThings(level.map_path) > 0 && P_InitSight());
    int player = consoleplayer;
    CHECK(sc_player_side(player) == 2);
    CHECK(G_SaveGameBlob(&saved, &length, "custom", &app, &ai, &hud));
    P_FreeLevel(&level);
    sc_set_custom_slots(NULL, NULL);
    consoleplayer = 0;
    CHECK(G_DoLoadLevel(map, &level));
    CHECK(P_LoadThings(level.map_path) > 0 && P_InitSight());
    CHECK(consoleplayer != player || sc_player_side(consoleplayer) != 2);
    CHECK(G_LoadGameBlob(saved, length, &app, &ai, &hud));
    CHECK(consoleplayer == player && sc_player_side(consoleplayer) == 2);
    size_t custom_extra_size = G_SaveExtraSize();
    void *extra = malloc(custom_extra_size); CHECK(extra); G_SaveExtra(extra);
    netgame = true; consoleplayer = (player + 1) % 8;
    CHECK(G_LoadExtra(extra, custom_extra_size) && consoleplayer == (player + 1) % 8);
    netgame = false; CHECK(G_LoadExtra(extra, custom_extra_size) && consoleplayer == player);
    free(extra);
    CHECK(SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO) == 0);
    CHECK(M_Init(&app, "data/STARCRAFT"));
    menu_t *bar = G_InitHUD(&app, "data/STARCRAFT"); CHECK(bar);
    menuitem_t *f10 = NULL;
    for (int i = 0; i < bar->numitems; ++i) if (bar->items[i].hotkey == SDLK_F10) f10 = &bar->items[i];
    blob_t art = {0}; spritesheet_t expected = {0}; uint32_t palette[256] = {0};
    CHECK(f10 && f10->sheet && sc_read("data/STARCRAFT", "dlgs/protoss.grp", &art));
    CHECK(sc_decode_grp(&art, palette, false, &expected));
    CHECK(f10->sheet->numlumps == expected.numlumps);
    size_t pixels = (size_t)expected.cells[1].rect.w * expected.cells[1].rect.h;
    CHECK(!memcmp(f10->sheet->lumps[1].indices, expected.lumps[1].indices, pixels));
    R_FreeSprite(&expected); W_FreeFile(&art); G_ShutdownHUD(); M_Shutdown(); SDL_Quit();
    free(saved); P_FreeLevel(&level);
    puts("PASS: StarCraft mission save round trip and malformed runtime rejection");
    return 0;
}
