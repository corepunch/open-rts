#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c,"StarCraft gameplay",#c)
/* A whole game on retail data: native animations (Stargus/PyMS iscript),
 * gathering, construction, training, research, combat, deaths, the mission
 * result, the score screen, the in-level dialogs and the briefing script. */
static const char *root = "data/STARCRAFT";
static const char road[] = "data/STARCRAFT/maps/(2)road war.scm/staredit/scenario.chk";
static app_t app = {.win = {640, 480}, .cell = {32, 32}, .running = true};
static tileset_t tiles;
static spritesheet_t fallback;
static spritecache_t cache;
static hudtext_t hudtext;

static bool save(const char *path) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, screens[0].w, screens[0].h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return false;
    for (int y = 0; y < s->h; y++) for (int x = 0; x < s->w; x++)
        ((uint32_t *)((uint8_t *)s->pixels + y * s->pitch))[x] = vpalette[screens[0].pixels[y * s->w + x]];
    SDL_Surface *rgb = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGB24, 0);
    bool ok = rgb && SDL_SaveBMP(rgb, path) == 0;
    SDL_FreeSurface(rgb);
    SDL_FreeSurface(s);
    return ok;
}
static void draw(const char *path) {
    V_BeginFrame(0xff000000);
    if (level.width) {
        R_DrawLevel(&app, &level, &tiles);
        mobjlist_t all = P_ListMobjs();
        R_RenderPlayerView(&app, &level, &tiles, all.items, all.count, &fallback, &cache, gameinfo, 0);
        P_FreeMobjList(&all);
    }
    if (currentmenu && menuactive) M_MenuDrawer(currentmenu);
    if (path) save(path);
}
static void look(const mobj_t *unit) { M_CentreView(&app, fixed3_xy_to_fvec2(unit->core.position)); }
static void tick(int n) {
    while (n--) {
        P_Ticker();
        G_ProductionTicker(1.0f / RTS_TICRATE);
        int count = 0;
        G_MissionTicker(&level, NULL, &count, &hudtext, FIXED_DT);
    }
}
static void order(mobj_t *u, ticorder_t type, fvec2_t at, mobj_t *target, int product) {
    ticcmd_t cmd = {.order = type, .count = 1, .units = {u->id}, .target = target ? target->id : 0,
                    .position = fixed3_from_fvec2(at, 0), .product = product};
    G_RunTiccmd(u->owner, &cmd);
}
static mobj_t *find(int owner, int type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && !mo->remove && mo->hp > 0 && mo->owner == owner &&
            mo->type_id == type) return mo;
    }
    return NULL;
}
static bool alive(const mobj_t *unit) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if ((const mobj_t *)th == unit && th->function == P_MobjThinker) return true;
    return false;
}
static mobj_t *spawn(int type, fvec2_t at, int owner) {
    mobj_t *u = sc_spawn_actor((unsigned)type - 1, (ivec2_t){(int)(at.x * 32), (int)(at.y * 32)}, (uint8_t)owner);
    return u;
}
static bool site(int type, const mobj_t *near, const mobj_t *worker, ivec2_t *cell) {
    fvec2_t at = fixed3_xy_to_fvec2(near->core.position);
    for (int r = 4; r < 20; r++) for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) {
        if (abs(x) != r && abs(y) != r) continue;
        ivec2_t c = {(int)at.x + x, (int)at.y + y};
        if (P_CanPlaceBuilding((uint16_t)type, c, worker)) { *cell = c; return true; }
    }
    return false;
}
static int press(int id) {
    menuitem_t *item = currentmenu ? M_MenuFind(currentmenu, id) : NULL;
    if (!item || !item->routine || !item->visible || !item->enabled) return 1;
    item->routine(currentmenu, item, MA_ACTIVATE);
    return 0;
}
static bool titled(const char *title) {
    if (!currentmenu) return false;
    for (int i = 0; i < currentmenu->numitems; i++)
        if (currentmenu->items[i].id == 65535 && !strcmp(currentmenu->items[i].text, title)) return true;
    return false;
}
static const char *text(int id) {
    menuitem_t *item = currentmenu ? M_MenuFind(currentmenu, id) : NULL;
    return item ? (item->prose ? item->prose : item->text) : "";
}
/* Follows a death chain to its end; counts its states and the extra sprites. */
static int chain(int state, int *extras, int *exact, const spritesheet_t *sheet) {
    int n = 0, last_sprite = -1;
    *extras = *exact = 0;
    while (state != S_NULL && n < 256) {
        const state_t *s = &states[state];
        if (s->sprite >= SC_TYPES && s->sprite != last_sprite) ++*extras;
        if (s->sprite < SC_TYPES && sheet && s->frame >= (sheet->numlumps + 16) / 17) ++*exact;
        last_sprite = s->sprite;
        if (s->nextstate == state) return -1; /* a death must end */
        state = s->nextstate;
        ++n;
    }
    return state == S_NULL ? n : -1;
}
static bool load(const int *kinds, const int *races) {
    sc_set_custom_slots(kinds, races);
    P_InitThinkers();
    if (!G_DoLoadLevel(road, &level)) return false;
    R_FreeTileset(&tiles);
    if (!W_LoadAssets(root, &level, "sc-000", &tiles, &fallback)) return false;
    if (P_LoadThings(level.map_path) <= 0 || !P_InitSight()) return false;
    mobjlist_t all = P_ListMobjs();
    bool ok = R_InitSprites(root, &level, all.items, all.count, &cache);
    P_FreeMobjList(&all);
    P_SyncBuildingBlocking();
    return ok && G_InitHUD(&app, root);
}
static void unload(void) {
    G_ShutdownHUD();
    M_ClearMenus();
    P_FreeThinkers();
    P_FreeLevel(&level);
    menuleave = false;
    menumap = NULL;
}

static int animations(void) {
    const spritesheet_t *marine = R_CacheLookup(&cache, "sc-000");
    CHECK(marine && marine->numlumps == 229);
    /* Turning sets of 17, then one single-direction frame per picture. */
    CHECK(marine->spritedef.numframes == 14 + 229);
    CHECK(marine->spritedef.spriteframes[14 + 221].rotations == 1);
    CHECK(marine->spritedef.spriteframes[14 + 221].directions[0].layers[0].lump == 221);
    /* Idle is set 4 (0x44); walking waits a tick in that pose, then 0x55 onward. */
    CHECK(states[1].frame == 4 && states[2].frame == 4 && states[states[2].nextstate].frame == 5);
    CHECK(states[2].group == 2 && states[2].action == A_Chase);
    /* MarineDeath: setfldirect 0, playfram 0xdd..0xe4, lowsprul tmaDeath. */
    int extras, exact, steps = chain(mobjinfo[MT_MARINE].deathstate, &extras, &exact, marine);
    CHECK(steps == 12 && exact == 9 && extras == 1); /* the last frame holds a tick over lowsprul */
    CHECK(states[mobjinfo[MT_MARINE].deathstate].frame == 14 + 221);
    CHECK(states[mobjinfo[MT_MARINE].deathstate].group == 4 && !states[mobjinfo[MT_MARINE].deathstate].action);
    CHECK(R_CacheLookup(&cache, "sc-img-241")); /* tmaDeath.grp */
    /* The attack loop strikes on an attack opcode's frame and returns to idle. */
    bool strike = false;
    for (int s = 1 + SC_TYPES * 2, n = 0; n < 32; s = states[s].nextstate, n++) {
        if (states[s].action == A_Attack) strike = true;
        if (states[s].nextstate == 1) break;
    }
    CHECK(strike);
    /* ZealotGndAttkRpt's two attackmelee are one attack of two hits (weapondef_t.hits). */
    int strikes = 0, zealot = MT_ZEALOT - 1;
    for (int s = 1 + SC_TYPES * 2 + zealot, n = 0; n < 64; s = states[s].nextstate, n++) {
        if (states[s].action == A_Attack) ++strikes;
        if (states[s].nextstate == 1 + zealot * 2) break;
    }
    CHECK(strikes == 1 && actor_types[MT_ZEALOT - 1].attack.hits == 2);
    /* SCV and probe explode; the drone plays its own frames, then leaves a corpse. */
    CHECK(chain(mobjinfo[MT_SCV].deathstate, &extras, &exact, NULL) > 0 && extras == 1);
    CHECK(chain(mobjinfo[MT_PROBE].deathstate, &extras, &exact, NULL) > 0 && extras == 1);
    CHECK(chain(mobjinfo[MT_DRONE].deathstate, &extras, &exact, NULL) > 0 && extras == 1);
    CHECK(states[mobjinfo[MT_DRONE].deathstate].frame >= 10);
    /* A Terran building explodes and leaves rubble. */
    CHECK(chain(mobjinfo[MT_COMMAND_CENTER].deathstate, &extras, &exact, NULL) > 0 && extras == 2);
    /* Mining is the AlmostBuilt loop: a tick in pose, then 0x22 and 0x11 round again. */
    int mine = actor_types[MT_SCV - 1].harvest.state_id;
    int cut = mine > 0 ? states[mine].nextstate : 0, rest = cut ? states[cut].nextstate : 0;
    CHECK(mine > 0 && states[cut].frame == 2 && states[rest].frame == 1 && states[rest].nextstate == cut);
    CHECK(actor_types[MT_DRONE - 1].harvest.state_id > 0 && actor_types[MT_PROBE - 1].harvest.state_id > 0);
    return 0;
}

static int economy_and_combat(void) {
    mobj_t *hall = find(consoleplayer, MT_COMMAND_CENTER), *scv = find(consoleplayer, MT_SCV);
    CHECK(hall && scv && find(1, MT_HATCHERY) && find(1, MT_DRONE));
    CHECK(level.player_resources[consoleplayer][0] == 50);
    /* Gathering: the nearest mineral field, mined with the AlmostBuilt loop. */
    fvec2_t base = fixed3_xy_to_fvec2(hall->core.position);
    int best = -1; float distance = 1e9f;
    for (int i = 0; i < level.resource_vent_count; i++) {
        const resourcevent_t *v = &level.resource_vents[i];
        if (v->resource_type || !v->active) continue;
        float d = fvec2_length_squared(fvec2_sub(v->attachment, base));
        if (d < distance) { distance = d; best = i; }
    }
    CHECK(best >= 0);
    order(scv, TC_ORDER, level.resource_vents[best].attachment, NULL, 0);
    bool mined = false;
    for (int t = 0; t < 3000 && level.player_resources[consoleplayer][0] < 58; t++) {
        tick(1);
        if (states[scv->core.state_id].group == 6) mined = true;
    }
    CHECK(mined && level.player_resources[consoleplayer][0] >= 58);
    look(scv);
    draw("/private/tmp/starcraft-gameplay-mining.bmp");
    /* Construction: barracks and engineering bay by the worker. */
    level.player_resources[consoleplayer][0] = 2000;
    level.player_resources[consoleplayer][1] = 1000;
    ivec2_t cell;
    CHECK(site(MT_BARRACKS, hall, scv, &cell));
    order(scv, TC_CONSTRUCT, (fvec2_t){cell.x, cell.y}, NULL, MT_BARRACKS);
    CHECK(scv->production && scv->production->placed);
    for (int t = 0; t < 4000 && !find(consoleplayer, MT_BARRACKS); t++) tick(1);
    mobj_t *barracks = find(consoleplayer, MT_BARRACKS);
    CHECK(barracks && !scv->production);
    CHECK(site(MT_ENGINEERING_BAY, hall, scv, &cell));
    order(scv, TC_CONSTRUCT, (fvec2_t){cell.x, cell.y}, NULL, MT_ENGINEERING_BAY);
    for (int t = 0; t < 4000 && !find(consoleplayer, MT_ENGINEERING_BAY); t++) tick(1);
    mobj_t *bay = find(consoleplayer, MT_ENGINEERING_BAY);
    CHECK(bay);
    /* Training: a marine costs 50 and one supply. */
    int minerals = level.player_resources[consoleplayer][0], used, have;
    order(barracks, TC_BUILD, (fvec2_t){0}, NULL, MT_MARINE);
    CHECK(barracks->production && level.player_resources[consoleplayer][0] == minerals - 50);
    sc_supply_counts(consoleplayer, &used, &have);
    CHECK(used == 2 * 5 && have == 2 * 10); /* four SCVs plus the queued marine; the centre gives 10 */
    for (int t = 0; t < 1000 && !find(consoleplayer, MT_MARINE); t++) tick(1);
    mobj_t *marine = find(consoleplayer, MT_MARINE);
    CHECK(marine && !barracks->production);
    /* Research: Terran Infantry Weapons 1 (upgrades.dat 7) at the bay. */
    const StaticProductDefinition *weapons1 = G_ModelProductByUIId(NULL, 1000 + 7 * 4);
    const StaticProductDefinition *weapons2 = G_ModelProductByUIId(NULL, 1000 + 7 * 4 + 1);
    CHECK(weapons1 && weapons2 && weapons1->cost == 100 && weapons1->extra_costs[0] == 100);
    CHECK(weapons2->cost == 175 && G_ModelProductTrainingTimeMs(weapons2) > G_ModelProductTrainingTimeMs(weapons1));
    CHECK(G_ModelProductAvailable(NULL, consoleplayer, weapons1) && !G_ModelProductAvailable(NULL, consoleplayer, weapons2));
    minerals = level.player_resources[consoleplayer][0];
    order(bay, TC_BUILD, (fvec2_t){0}, NULL, weapons1->ui_id);
    CHECK(bay->production && bay->production->product_class == RTS_PRODUCT_UPGRADE);
    CHECK(level.player_resources[consoleplayer][0] == minerals - 100);
    CHECK(!G_ModelProductAvailable(NULL, consoleplayer, weapons1)); /* already researching */
    for (int t = 0; t < 6000 && sc_upgrade_level(consoleplayer, 7) < 1; t++) tick(1);
    CHECK(sc_upgrade_level(consoleplayer, 7) == 1 && !bay->production);
    CHECK(!sc_upgrade_offered(consoleplayer, weapons1) && G_ModelProductAvailable(NULL, consoleplayer, weapons2));
    /* Combat: an upgraded marine hits a zergling for 6 + 1 - armor. */
    fvec2_t at = fixed3_xy_to_fvec2(marine->core.position);
    mobj_t *zergling = spawn(MT_ZERGLING, (fvec2_t){at.x + 2.5f, at.y}, 1);
    CHECK(zergling);
    zergling->traits &= ~MF_ATTACK; /* a target, not a duel */
    int hp = zergling->hp, expected = 6 + 1 - sc_units[MT_ZERGLING - 1].armor;
    order(marine, TC_ATTACK, fixed3_xy_to_fvec2(zergling->core.position), zergling, 0);
    for (int t = 0; t < 200 && zergling->hp == hp; t++) tick(1);
    CHECK(hp - zergling->hp == expected);
    look(marine);
    draw("/private/tmp/starcraft-gameplay-attack.bmp");
    /* Dying: the zergling plays its death and is removed when it ends. */
    for (int t = 0; t < 600 && zergling->hp > 0; t++) tick(1);
    CHECK(zergling->hp <= 0 && states[zergling->core.state_id].group == 4);
    draw("/private/tmp/starcraft-gameplay-death.bmp");
    for (int t = 0; t < 4000 && alive(zergling); t++) tick(1);
    CHECK(!alive(zergling));
    sc_stats_t stats;
    sc_player_stats(consoleplayer, &stats);
    CHECK(stats.killed[MT_ZERGLING - 1] == 1 && stats.gathered[0] >= 8 && stats.spent > 0);
    sc_player_stats(1, &stats);
    CHECK(stats.lost[MT_ZERGLING - 1] == 1);
    /* A dying structure frees its cells for building again. */
    ivec2_t footprint = {(int)fixed3_xy_to_fvec2(bay->core.position).x, (int)fixed3_xy_to_fvec2(bay->core.position).y};
    P_DamageMobj(bay, NULL, bay->hp);
    for (int t = 0; t < 6000 && alive(bay); t++) tick(1);
    CHECK(!alive(bay) && !level.cell_solid[L_Index(&level, footprint.x, footprint.y)]);
    return 0;
}

static int victory_and_score(void) {
    CHECK(sc_mission_result() == 0);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->owner == 1 && mo->hp > 0) P_DamageMobj(mo, NULL, mo->hp);
    }
    tick(2);
    CHECK(sc_mission_result() == 1 && menuactive);
    CHECK(strstr(text(65516), "victorious") && M_MenuFind(currentmenu, 1)->visible); /* wmission */
    draw("/private/tmp/starcraft-victory.bmp");
    CHECK(!press(65534)); /* Victory */
    CHECK(!strcmp(text(2), "Victory!") && !strcmp(text(8), "Total Score"));
    CHECK(!strcmp(text(14), "Player") && !strcmp(text(20), "Computer"));
    CHECK(!strcmp(text(10), "Units") && strstr(text(9), "Elapsed Time:"));
    CHECK(M_MenuFind(currentmenu, 13)->visible && !M_MenuFind(currentmenu, 25)->visible);
    draw("/private/tmp/starcraft-score.bmp");
    menuitem_t *tab = M_MenuFind(currentmenu, 4);
    tab->routine(currentmenu, tab, MA_ACTIVATE);
    CHECK(!strcmp(text(10), "Produced") && !strcmp(text(16), "1")); /* the zergling */
    CHECK(!press(7) && menuleave && !menuactive); /* a custom game ends here */
    return 0;
}

static int defeat(void) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->owner == consoleplayer && mo->hp > 0) P_DamageMobj(mo, NULL, mo->hp);
    }
    tick(2);
    CHECK(sc_mission_result() == 2 && menuactive && strstr(text(65516), "failed"));
    CHECK(!M_MenuFind(currentmenu, 1)->visible); /* no observers offline */
    draw("/private/tmp/starcraft-defeat.bmp");
    CHECK(!press(65534) && !strcmp(text(2), "Defeat!"));
    draw("/private/tmp/starcraft-score-defeat.bmp");
    CHECK(!press(7) && menuleave);
    return 0;
}

static int dialogs(void) {
    menu_t *game = G_ControlPanel(&app, true);
    CHECK(game && M_MenuFind(game, 65533));
    M_SetupNextMenu(game);
    draw("/private/tmp/starcraft-game-menu.bmp");
    struct { int press; const char *title; const char *shot; } route[] = {
        {3, "Game Options", "options"}, {1, "Speed Settings", "speed"}, {65533, "Game Options", NULL},
        {2, "Sound Options", "sound"}, {65534, "Game Options", NULL}, {3, "Video Options", "video"},
        {65533, "Game Options", NULL}, {65533, "Game Menu", NULL}, {1, "Save Game", "save"},
        {65533, "Game Menu", NULL}, {2, "Load Game", "load-game"}, {65533, "Game Menu", NULL},
        {4, "Help Menu", NULL}, {1, "Starcraft Help", "help"}, {65534, "Help Menu", NULL},
        {65533, "Game Menu", NULL}, {5, "Mission Objectives", "objectives"}, {65533, "Game Menu", NULL},
        {6, "End Mission", "end-mission"}, {1, "Are you sure you", "restart"}, {65533, "End Mission", NULL},
        {3, "Are you sure you", "quit"}, {65533, "End Mission", NULL}, {65533, "Game Menu", NULL},
    };
    for (unsigned i = 0; i < sizeof(route) / sizeof(*route); i++) {
        CHECK(!press(route[i].press));
        if (!titled(route[i].title)) fprintf(stderr, "dialog step %u: expected %s\n", i, route[i].title);
        CHECK(titled(route[i].title));
        if (route[i].shot) {
            char path[128];
            snprintf(path, sizeof(path), "/private/tmp/starcraft-dialog-%s.bmp", route[i].shot);
            draw(path);
        }
        if (!strcmp(route[i].title, "Starcraft Help")) CHECK(strlen(text(1)) > 100);
        if (!strcmp(route[i].title, "Save Game")) CHECK(M_MenuFind(currentmenu, 2)->kind == MI_TEXTFIELD);
    }
    /* Escape walks back up; on the Game Menu it resumes. */
    CHECK(!press(3) && !press(1));
    currentmenu->escape(currentmenu);
    CHECK(titled("Game Options"));
    currentmenu->escape(currentmenu);
    currentmenu->escape(currentmenu);
    CHECK(!menuactive);
    return 0;
}

static int campaign_flow(void) {
    /* Terran 01 won: Victory, the score, then Terran 02's briefing. */
    P_InitThinkers();
    sc_set_custom_slots(NULL, NULL);
    CHECK(G_DoLoadLevel("data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk", &level));
    CHECK(P_LoadThings(level.map_path) > 0);
    CHECK(G_InitHUD(&app, root));
    sc_show_result(1);
    CHECK(!press(65534) && !strcmp(text(2), "Victory!"));
    CHECK(!press(7) && menuactive && M_MenuFind(currentmenu, 65526) && M_MenuFind(currentmenu, 13));
    CHECK(strstr(text(65525), "Marines") == NULL && text(65525)[0]); /* Terran 02's objectives */
    CHECK(!press(13) && menumap && strstr(menumap, "terran02"));
    menumap = NULL;
    /* A loss briefs the same mission again. */
    sc_show_result(2);
    CHECK(!press(65534) && !press(7) && menuactive && M_MenuFind(currentmenu, 65526));
    CHECK(!press(13) && menumap && strstr(menumap, "terran01"));
    unload();
    return 0;
}

static int briefing_script(void) {
    sc_briefing_t script;
    CHECK(sc_briefing_script("data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk", &script));
    CHECK(script.count == 13 && script.actions[1].op == SC_BRIEF_SHOW_PORTRAIT);
    CHECK(script.actions[1].slot == 3 && script.actions[1].unit == MT_COMMAND_CENTER - 1);
    CHECK(script.actions[3].op == SC_BRIEF_TRANSMISSION && script.actions[3].time == 26469);
    CHECK(!strncmp(script.strings + script.actions[3].text, "Adjutant Online", 15));
    /* Through the menus: main, registry, campaign, Terran. */
    menu_t *front = G_ControlPanel(&app, false);
    M_SetupNextMenu(front);
    CHECK(!press(3) && !press(4) && !press(7) && M_MenuFind(currentmenu, 15));
    bool talking = true;
    CHECK(sc_briefing_slot(3, &talking) && !talking && !sc_briefing_slot(0, NULL));
    CHECK(!text(65526)[0]);
    sc_briefing_advance(1100); /* past the one-second wait */
    CHECK(sc_briefing_slot(3, &talking) && talking && !strncmp(text(65526), "Adjutant Online", 15));
    draw("/private/tmp/starcraft-briefing-adjutant.bmp");
    sc_briefing_advance(26469 + 600); /* Duke appears in slot 0 and speaks */
    CHECK(sc_briefing_slot(0, &talking) && talking && sc_briefing_slot(3, &talking) && !talking);
    CHECK(!strncmp(text(65526), "Greetings", 9));
    draw("/private/tmp/starcraft-briefing-duke.bmp");
    sc_briefing_advance(60000);
    CHECK(!sc_briefing_slot(0, NULL) && !sc_briefing_slot(3, NULL) && strstr(text(65526), "End of Briefing"));
    /* Replay starts over. */
    CHECK(!press(20) && !text(65526)[0] && sc_briefing_slot(3, NULL));
    currentmenu->escape(currentmenu);
    M_ClearMenus();
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO) == 0);
    G_InitGame();
    V_AllocScreen(640, 480);
    CHECK(M_Init(&app, root));
    menu_t *title = G_ControlPanel(&app, false);
    title->escape(title);
    M_ClearMenus();
    int kinds[8] = {SC_SLOT_HUMAN, SC_SLOT_COMPUTER, SC_SLOT_CLOSED, SC_SLOT_CLOSED,
                    SC_SLOT_CLOSED, SC_SLOT_CLOSED, SC_SLOT_CLOSED, SC_SLOT_CLOSED};
    int races[8] = {0, 1};
    CHECK(load(kinds, races));
    CHECK(!animations());
    CHECK(!economy_and_combat());
    CHECK(!dialogs());
    CHECK(!victory_and_score());
    unload();
    CHECK(load(kinds, races));
    CHECK(!defeat());
    unload();
    CHECK(!campaign_flow());
    CHECK(!briefing_script());
    sc_set_custom_slots(NULL, NULL);
    M_Shutdown();
    R_FreeSpriteCache(&cache);
    R_FreeTileset(&tiles);
    SDL_Quit();
    puts("PASS: native animations, gathering, construction, training, research, combat, deaths, "
         "victory, defeat, score, in-level dialogs, campaign flow and briefing portraits");
    return 0;
}
