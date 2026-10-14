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
static menu_t *hudmenu;

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
        G_ProductionTicker(RTS_TICK_MS);
        int count = 0;
        G_MissionTicker(&level, NULL, &count, &hudtext, RTS_TICK_MS);
    }
}
static void order(mobj_t *u, ticorder_t type, fixed2_t at, mobj_t *target, int product) {
    ticcmd_t cmd = {.order = type, .count = 1, .units = {u->id}, .target = target ? target->id : 0,
                    .position = fixed3_from_fixed2(at, 0), .product = product};
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
static mobj_t *spawn(int type, fixed2_t at, int owner) {
    mobj_t *u = sc_spawn_actor((unsigned)type - 1, (ivec2_t){at.x >> 11, at.y >> 11} /* 32 pixels per cell */, (uint8_t)owner);
    return u;
}
static bool site(int type, const mobj_t *near, const mobj_t *worker, ivec2_t *cell) {
    fixed2_t at = fixed3_xy(near->core.position);
    for (int r = 4; r < 20; r++) for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) {
        if (abs(x) != r && abs(y) != r) continue;
        ivec2_t c = {fixed_floor_int(at.x) + x, fixed_floor_int(at.y) + y};
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
    return ok && (hudmenu = G_InitHUD(&app, root)) != NULL;
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
    /* Mining is one cutter swing: a tick in pose, then 0x22 and 0x11, back to idle. */
    int mine = actor_types[MT_SCV - 1].harvest.state_id;
    int cut = mine > 0 ? states[mine].nextstate : 0, rest = cut ? states[cut].nextstate : 0;
    CHECK(mine > 0 && states[cut].frame == 2 && states[rest].frame == 1 &&
          states[rest].nextstate == 1 + (MT_SCV - 1) * 2);
    CHECK(actor_types[MT_DRONE - 1].harvest.state_id > 0 && actor_types[MT_PROBE - 1].harvest.state_id > 0);
    /* SCVWalking's imgol 249 must draw both native engine-glow sets through
     * the Badlands fire lookup and disappear as soon as the pose is idle. */
    mobj_t *worker = find(consoleplayer, MT_SCV);
    CHECK(worker);
    int previous_state = worker->core.state_id;
    sc_glow_t previous_glow = worker->sc.glow;
    unitoverlaycontext_t overlay = {.unit = worker, .game_info = gameinfo, .anchor = {320, 240}};
    int changes[2] = {0};
    for (int phase = 0; phase < 2; ++phase) {
        V_BeginFrame(0xff000000);
        memset(screens[0].pixels, 64, (size_t)screens[0].w * screens[0].h);
        worker->core.state_id = mobjinfo[MT_SCV].seestate;
        worker->sc.glow = (sc_glow_t){.active = 1, .phase = phase};
        sc_draw_overlays(&overlay);
        for (int y = 204; y < 276; ++y) for (int x = 284; x < 356; ++x)
            changes[phase] += screens[0].pixels[y * screens[0].w + x] != 64;
    }
    CHECK(changes[0] > 0 && changes[1] > 0);
    V_BeginFrame(0xff000000);
    memset(screens[0].pixels, 64, (size_t)screens[0].w * screens[0].h);
    worker->core.state_id = mobjinfo[MT_SCV].spawnstate;
    sc_draw_overlays(&overlay);
    for (int y = 204; y < 276; ++y) for (int x = 284; x < 356; ++x)
        CHECK(screens[0].pixels[y * screens[0].w + x] == 64);
    worker->core.state_id = previous_state;
    worker->sc.glow = previous_glow;
    return 0;
}

static int economy_and_combat(void) {
    mobj_t *hall = find(consoleplayer, MT_COMMAND_CENTER), *scv = find(consoleplayer, MT_SCV);
    CHECK(hall && scv && find(1, MT_HATCHERY) && find(1, MT_DRONE));
    CHECK(level.player_resources[consoleplayer][0] == 50);
    /* Gathering: the nearest mineral field, mined with the cutter swing. */
    fixed2_t base = fixed3_xy(hall->core.position);
    int best = -1; int64_t distance = INT64_MAX;
    for (int i = 0; i < level.resource_vent_count; i++) {
        const resourcevent_t *v = &level.resource_vents[i];
        if (v->resource_type || !v->active) continue;
        int64_t d = fixed2_length_squared64(fixed2_sub(v->attachment, base));
        if (d < distance) { distance = d; best = i; }
    }
    CHECK(best >= 0);
    order(scv, TC_ORDER, level.resource_vents[best].attachment, NULL, 0);
    for (int t = 0; t < 10; ++t) tick(1);
    CHECK(states[scv->core.state_id].group == 2);
    CHECK(scv->sc.glow.active);
    look(scv);
    draw("/private/tmp/starcraft-scv-glow.bmp");
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
    order(scv, TC_CONSTRUCT, FIXED2_LIT(cell.x, cell.y), NULL, MT_BARRACKS);
    CHECK(scv->production && scv->production->placed);
    for (int t = 0; t < 4000 && !find(consoleplayer, MT_BARRACKS); t++) tick(1);
    mobj_t *barracks = find(consoleplayer, MT_BARRACKS);
    CHECK(barracks && !scv->production);
    CHECK(site(MT_ENGINEERING_BAY, hall, scv, &cell));
    order(scv, TC_CONSTRUCT, FIXED2_LIT(cell.x, cell.y), NULL, MT_ENGINEERING_BAY);
    for (int t = 0; t < 4000 && !find(consoleplayer, MT_ENGINEERING_BAY); t++) tick(1);
    mobj_t *bay = find(consoleplayer, MT_ENGINEERING_BAY);
    CHECK(bay);
    /* Training: a marine costs 50 and one supply. */
    int minerals = level.player_resources[consoleplayer][0], used, have;
    order(barracks, TC_BUILD, (fixed2_t){0}, NULL, MT_MARINE);
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
    order(bay, TC_BUILD, (fixed2_t){0}, NULL, weapons1->ui_id);
    CHECK(bay->production && bay->production->product_class == RTS_PRODUCT_UPGRADE);
    CHECK(level.player_resources[consoleplayer][0] == minerals - 100);
    CHECK(!G_ModelProductAvailable(NULL, consoleplayer, weapons1)); /* already researching */
    for (int t = 0; t < 6000 && sc_upgrade_level(consoleplayer, 7) < 1; t++) tick(1);
    CHECK(sc_upgrade_level(consoleplayer, 7) == 1 && !bay->production);
    /* Level two is the next level, but waits for a Science Facility. */
    CHECK(!sc_upgrade_offered(consoleplayer, weapons1) && sc_upgrade_offered(consoleplayer, weapons2));
    CHECK(!G_ModelProductAvailable(NULL, consoleplayer, weapons2));
    CHECK(spawn(MT_ARMORY, FIXED2_LIT(40, 40), consoleplayer));
    CHECK(!G_ModelProductAvailable(NULL, consoleplayer, weapons2));
    CHECK(spawn(MT_SCIENCE_FACILITY, FIXED2_LIT(44, 40), consoleplayer));
    CHECK(G_ModelProductAvailable(NULL, consoleplayer, weapons2));
    /* Combat: an upgraded marine hits a zergling for 6 + 1 - armor. */
    fixed2_t at = fixed3_xy(marine->core.position);
    mobj_t *zergling = spawn(MT_ZERGLING, (fixed2_t){at.x + FIXED_LIT(2.5), at.y}, 1);
    CHECK(zergling);
    zergling->traits &= ~MF_ATTACK; /* a target, not a duel */
    int hp = zergling->hp, expected = 6 + 1 - sc_units[MT_ZERGLING - 1].armor;
    order(marine, TC_ATTACK, fixed3_xy(zergling->core.position), zergling, 0);
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
    ivec2_t footprint = fixed2_cell(fixed3_xy(bay->core.position));
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

/* The command card's button with this tooltip for what is selected now. */
static menuitem_t *card(const char *tip, mobj_t *const *pick, int count) {
    static mobjlist_t all;
    P_FreeMobjList(&all);
    all = P_ListMobjs();
    for (int i = 0; i < all.count; i++) P_MobjSetSelected(all.items[i], false);
    for (int i = 0; i < count; i++) P_MobjSetSelected(pick[i], true);
    hudview = (hudview_t){.units = all.items, .unit_count = all.count, .sprites=&cache, .tileset=&tiles};
    hudmenu->refresh(hudmenu);
    for (int i = 0; i < hudmenu->numitems; i++) {
        menuitem_t *it = &hudmenu->items[i];
        if (it->visible && it->tooltip && !strcmp(it->tooltip, tip)) return it;
    }
    return NULL;
}
static void click(menuitem_t *it) { it->routine(hudmenu, it, MA_ACTIVATE); }
static void mouse_click(menuitem_t *it) {
    irect_t rect=M_MenuItemRect(hudmenu,it);
    SDL_Event event={.button={.type=SDL_MOUSEBUTTONDOWN,.button=SDL_BUTTON_LEFT,
        .x=rect.x+rect.w/2,.y=rect.y+rect.h/2}};
    M_MenuResponder(hudmenu,&app,&event);
    event.button.type=SDL_MOUSEBUTTONUP;
    M_MenuResponder(hudmenu,&app,&event);
}
static menuitem_t *pipeline_slot(int slot) {
    for(int i=0;i<hudmenu->numitems;i++) {
        menuitem_t *it=&hudmenu->items[i];
        if(it->visible&&it->tooltip&&!strcmp(it->tooltip,"Cancel queued unit")&&it->value==slot)return it;
    }
    return NULL;
}

/* Open a build page of the selected worker and look for a structure. */
static menuitem_t *on_page(mobj_t *worker, const char *page, const char *tip) {
    menuitem_t *open = card(page, &worker, 1);
    if (!open) return NULL;
    open->routine(hudmenu, open, MA_ACTIVATE);
    menuitem_t *found = card(tip, &worker, 1);
    menuitem_t *cancel = card("Cancel", &worker, 1);
    if (cancel) cancel->routine(hudmenu, cancel, MA_ACTIVATE);
    return found;
}
static mobj_t *beside(int type, const mobj_t *near, int dx, int dy) {
    fixed2_t at = fixed2_add(fixed3_xy(near->core.position), (fixed2_t){FIXED_FROM_INT(dx), FIXED_FROM_INT(dy)});
    return spawn(type, at, consoleplayer);
}
static const char *name(int type) { return sc_units[type - 1].name; }

/* Buttons appear as the tech tree opens: structures, units, add-on units,
 * research tiers, abilities greyed until researched, larva and gateway. */
static int tech_card(void) {
    mobj_t *hall = find(consoleplayer, MT_COMMAND_CENTER), *scv = find(consoleplayer, MT_SCV);
    CHECK(hall && scv);
    const char *basic = "Build Structure", *advanced = "Advanced Structures";
    CHECK(on_page(scv, basic, name(MT_SUPPLY_DEPOT)) && on_page(scv, basic, name(MT_BARRACKS)));
    CHECK(on_page(scv, basic, name(MT_ENGINEERING_BAY)) && on_page(scv, basic, name(MT_REFINERY)));
    CHECK(!on_page(scv, basic, name(MT_ACADEMY)) && !on_page(scv, basic, name(MT_BUNKER)));
    CHECK(!on_page(scv, basic, name(MT_MISSILE_TURRET)) && !on_page(scv, advanced, name(MT_FACTORY)));
    mobj_t *barracks = beside(MT_BARRACKS, hall, 6, 0);
    CHECK(barracks);
    CHECK(on_page(scv, basic, name(MT_ACADEMY)) && on_page(scv, basic, name(MT_BUNKER)));
    CHECK(on_page(scv, advanced, name(MT_FACTORY)) && !on_page(scv, advanced, name(MT_STARPORT)));
    CHECK(!on_page(scv, advanced, name(MT_ARMORY)));
    mobj_t *bay = beside(MT_ENGINEERING_BAY, hall, -6, 0);
    CHECK(bay && on_page(scv, basic, name(MT_MISSILE_TURRET)));
    /* Barracks: Firebat with an Academy, Ghost also with Covert Ops. */
    CHECK(card(name(MT_MARINE), &barracks, 1) && !card(name(MT_FIREBAT), &barracks, 1));
    CHECK(!card(name(MT_COMSAT_STATION), &hall, 1));
    mobj_t *academy = beside(MT_ACADEMY, hall, 0, 6);
    CHECK(academy && card(name(MT_FIREBAT), &barracks, 1) && !card(name(MT_GHOST), &barracks, 1));
    CHECK(card(name(MT_COMSAT_STATION), &hall, 1));
    /* Factory: the Siege Tank needs this factory's Machine Shop. */
    /* The first open spot that also leaves room for its add-on. */
    mobj_t *factory = NULL;
    ivec2_t at = fixed2_cell(fixed3_xy(hall->core.position));
    for (int r = 6; r < 24 && !factory; r += 2)
        for (int y = -r; y <= r && !factory; y += 2) for (int x = -r; x <= r && !factory; x += 2) {
            ivec2_t c = {at.x + x, at.y + y}, shop;
            if (!P_CanPlaceBuilding(MT_FACTORY, c, scv)) continue;
            mobj_t *f = spawn(MT_FACTORY, P_BuildingPosition(MT_FACTORY, c), consoleplayer);
            P_SyncBuildingBlocking();
            if (f && sc_addon_site(f, MT_MACHINE_SHOP, &shop)) factory = f;
            else if (f) { P_RemoveMobj(f); P_RunThinkers(); P_SyncBuildingBlocking(); }
        }
    mobj_t *other = beside(MT_FACTORY, hall, -6, 6);
    CHECK(factory && other);
    CHECK(on_page(scv, advanced, name(MT_STARPORT)) && on_page(scv, advanced, name(MT_ARMORY)));
    CHECK(card(name(MT_VULTURE), &factory, 1) && card(name(MT_MACHINE_SHOP), &factory, 1));
    CHECK(!card(name(MT_GOLIATH), &factory, 1));
    CHECK(sc_attach_addon(factory, MT_MACHINE_SHOP));
    menuitem_t *tank = card(name(MT_SIEGE_TANK), &factory, 1);
    CHECK(tank);
    CHECK(!card(name(MT_MACHINE_SHOP), &factory, 1));
    CHECK(card(name(MT_SIEGE_TANK), &other, 1)); /* listed; refused at click without a shop */
    CHECK(card(name(MT_MACHINE_SHOP), &other, 1));
    beside(MT_ARMORY, hall, 0, -6);
    CHECK(card(name(MT_GOLIATH), &factory, 1));
    /* Siege Mode stays greyed on the tank until the Machine Shop researches it. */
    mobj_t *unit = beside(MT_SIEGE_TANK, hall, 3, 3);
    menuitem_t *siege = card(sc_techs[SC_TECH_SIEGE_MODE].name, &unit, 1);
    CHECK(unit && siege && !siege->enabled);
    level.upgrades[SC_UPGRADES + SC_TECH_SIEGE_MODE][consoleplayer].weapon = 1;
    siege = card(sc_techs[SC_TECH_SIEGE_MODE].name, &unit, 1);
    CHECK(siege && siege->enabled);
    /* Engineering Bay: level 1 now, level 2 after it and a Science Facility. */
    const char *weapons = sc_upgrades[0].name;
    menuitem_t *tier = card(weapons, &bay, 1);
    CHECK(tier && tier->value == SC_UPGRADE_UI + 0 * 4 + 0);
    level.upgrades[0][consoleplayer].weapon = 1;
    CHECK(!card(weapons, &bay, 1));
    beside(MT_STARPORT, hall, -6, -6);
    beside(MT_SCIENCE_FACILITY, hall, 6, -6);
    tier = card(weapons, &bay, 1);
    CHECK(tier && tier->value == SC_UPGRADE_UI + 0 * 4 + 1);
    level.upgrades[0][consoleplayer].weapon = 3;
    CHECK(!card(weapons, &bay, 1));
    level.upgrades[0][consoleplayer].weapon = 0;
    level.upgrades[SC_UPGRADES + SC_TECH_SIEGE_MODE][consoleplayer].weapon = 0;

    /* Zerg: Zerglings and Lair wait for the Spawning Pool. */
    mobj_t *hatchery = beside(MT_HATCHERY, hall, 12, 0), *larva = beside(MT_LARVA, hall, 12, 3);
    CHECK(hatchery && larva);
    CHECK(card(name(MT_DRONE), &larva, 1) && card(name(MT_OVERLORD), &larva, 1));
    CHECK(!card(name(MT_ZERGLING), &larva, 1) && !card(name(MT_LAIR), &hatchery, 1));
    CHECK(card("Select Larva", &hatchery, 1));
    beside(MT_SPAWNING_POOL, hall, 12, -6);
    CHECK(card(name(MT_ZERGLING), &larva, 1) && card(name(MT_LAIR), &hatchery, 1));
    CHECK(!card(name(MT_HYDRALISK), &larva, 1));
    /* Protoss: Dragoons wait for the Cybernetics Core. */
    mobj_t *gateway = beside(MT_GATEWAY, hall, -12, 0);
    CHECK(gateway && card(name(MT_ZEALOT), &gateway, 1) && !card(name(MT_DRAGOON), &gateway, 1));
    beside(MT_CYBERNETICS_CORE, hall, -12, 6);
    CHECK(card(name(MT_DRAGOON), &gateway, 1) && !card(name(MT_HIGH_TEMPLAR), &gateway, 1));
    return 0;
}

/* Abilities, siege mode, Archon Warp and Unload All from the command card. */
static int command_card(void) {
    mobj_t *hall = find(consoleplayer, MT_COMMAND_CENTER);
    CHECK(hall);
    level.player_resources[consoleplayer][0]=200;
    menuitem_t *train = card("Terran SCV", &hall, 1);
    CHECK(train && train->enabled && train->hotkey==SDLK_s);
    /* Spending the last minerals must never remove the training command. */
    irect_t train_rect=train->rect;
    level.player_resources[consoleplayer][0]=50;
    mouse_click(train);
    train=card("Terran SCV",&hall,1);
    CHECK(train&&train->enabled&&train->rect.x==train_rect.x&&train->rect.y==train_rect.y);
    CHECK(hall->production&&hall->production->queue_count==1&&level.player_resources[consoleplayer][0]==0);
    mouse_click(train);
    CHECK(hall->production->queue_count==1&&level.player_resources[consoleplayer][0]==0);
    CHECK(chat_text.count>0);
    draw(NULL);M_MenuDrawer(hudmenu);CHECK(save("/private/tmp/starcraft-zero-minerals.bmp"));
    bool feedback=false;
    for(int i=0;i<hudmenu->numitems;i++)feedback|=hudmenu->items[i].visible&&hudmenu->items[i].prose&&
        !strcmp(hudmenu->items[i].prose,"Not enough minerals");
    CHECK(feedback);
    CHECK(sc_cancel_production(hall,0));
    level.player_resources[consoleplayer][0]=200;
    menuitem_t *building_name=M_MenuFind(hudmenu,65531);
    CHECK(building_name&&building_name->visible&&
          !strcmp(building_name->text,"Terran Command Center")&&
          building_name->font->line_h==11&&
          V_TextBounds(building_name->font,building_name->text).w==119);
    draw(NULL);M_MenuDrawer(hudmenu);
    CHECK(save("/private/tmp/starcraft-command-center-status.bmp"));
    mouse_click(train);
    CHECK(hall->production && hall->production->queue_count==1 &&
          level.player_resources[consoleplayer][0]==150);
    mouse_click(card("Terran SCV",&hall,1));
    CHECK(hall->production->queue_count==2 && level.player_resources[consoleplayer][0]==100);
    app.win=(isize2_t){1280,960};
    V_AllocScreen(app.win.w,app.win.h);
    train=card("Terran SCV",&hall,1);
    CHECK(train && M_MenuItemRect(hudmenu,train).x==train->rect.x*2);
    mouse_click(train);
    CHECK(hall->production->queue_count==3 && level.player_resources[consoleplayer][0]==50);
    hudmenu->refresh(hudmenu);
    CHECK(pipeline_slot(0)&&M_MenuItemRect(hudmenu,pipeline_slot(0)).x==480&&
          M_MenuItemRect(hudmenu,pipeline_slot(4)).y==868);
    app.win=(isize2_t){640,480};
    V_AllocScreen(app.win.w,app.win.h);
    hudmenu->refresh(hudmenu);
    for(int slot=0;slot<5;slot++)CHECK(pipeline_slot(slot)&&
        pipeline_slot(slot)->enabled==(slot<3));
    bool progress_shown=false,building_shown=false;
    for(int i=0;i<hudmenu->numitems;i++) {
        progress_shown|=hudmenu->items[i].visible&&hudmenu->items[i].ownerdraw&&
            hudmenu->items[i].rect.x==278&&hudmenu->items[i].rect.w==108;
        building_shown|=hudmenu->items[i].visible&&!strcmp(hudmenu->items[i].text,"Building");
    }
    CHECK(progress_shown&&building_shown);
    draw(NULL);M_MenuDrawer(hudmenu);CHECK(save("/private/tmp/starcraft-queue.bmp"));
    uint8_t empty_bar[108*9];
    for(int y=0;y<9;y++)memcpy(empty_bar+y*108,screens[0].pixels+(422+y)*640+278,108);
    hall->production->time_left_ms=hall->production->time_ms/2;
    draw(NULL);M_MenuDrawer(hudmenu);
    bool bar_advanced=false;
    for(int y=0;y<9;y++)
        bar_advanced|=memcmp(empty_bar+y*108,screens[0].pixels+(422+y)*640+278,108)!=0;
    CHECK(bar_advanced);
    mouse_click(pipeline_slot(2));
    CHECK(hall->production&&hall->production->queue_count==2&&
          level.player_resources[consoleplayer][0]==100);
    mouse_click(card("Cancel Unit Training",&hall,1));
    CHECK(!hall->production&&level.player_resources[consoleplayer][0]==200);
    menuitem_t *rally_button=card("Set Rally Point",&hall,1);
    CHECK(rally_button&&rally_button->look[0].cell==286);
    mouse_click(rally_button);
    CHECK(hudmenu->target&&hudmenu->target->routine==rally_button->routine);
    CHECK(card("Cancel",&hall,1)&&!card("Terran SCV",&hall,1));
    ivec2_t rally_cell=ivec2_add(fixed2_cell(fixed3_xy(hall->core.position)),(ivec2_t){0,5});
    M_CentreView(&app,fixed3_xy_to_fvec2(hall->core.position));
    fvec2_t rally_screen;
    R_MapToScreen(&app,&level,rally_cell.x+0.5f,rally_cell.y+0.5f,&rally_screen.x,&rally_screen.y);
    SDL_Event rally_click={.button={.type=SDL_MOUSEBUTTONDOWN,.button=SDL_BUTTON_LEFT,
        .x=(int)rally_screen.x,.y=(int)rally_screen.y}};
    M_MenuResponder(hudmenu,&app,&rally_click);
    rally_click.button.type=SDL_MOUSEBUTTONUP;
    M_MenuResponder(hudmenu,&app,&rally_click);
    CHECK(!hudmenu->target&&hall->sc.rally.set);
    uint32_t last_scv=0;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next)
        if(th->function==P_MobjThinker) {
            mobj_t *mo=(mobj_t *)th;
            if(mo->type_id==MT_SCV&&mo->id>last_scv)last_scv=mo->id;
        }
    mouse_click(card("Terran SCV",&hall,1));
    CHECK(hall->production&&hall->production->queue_count==1);
    hall->production->time_left_ms=1;
    G_ProductionTicker(2);
    mobj_t *rallied=NULL;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next)
        if(th->function==P_MobjThinker) {
            mobj_t *mo=(mobj_t *)th;
            if(mo->type_id==MT_SCV&&mo->id>last_scv)rallied=mo;
        }
    CHECK(rallied&&P_HasMoveOrder(rallied));
    CHECK(!card("Terran Comsat Station",&hall,1));
    mobj_t *worker = find(consoleplayer, MT_SCV);
    CHECK(worker);
    level.player_resources[consoleplayer][0]=1000;
    menuitem_t *build = card("Build Structure", &worker, 1);
    CHECK(build);
    CHECK(build->sheet->frame_size.w==36 && build->sheet->frame_size.h==34);
    CHECK(ivec2_equal(build->sheet->cells[234].displacement,
                      (ivec2_t){(36-build->sheet->cells[234].rect.w)/2,
                                (34-build->sheet->cells[234].rect.h)/2}));
    V_BeginFrame(0xff000000);
    M_MenuDrawer(hudmenu);
    CHECK(save("/private/tmp/starcraft-hud.bmp"));
    mouse_click(build);
    menuitem_t *depot = card("Terran Supply Depot", &worker, 1);
    CHECK(depot && depot->enabled);
    mouse_click(depot);
    CHECK(hudmenu->target && hudmenu->target->value==depot->value);
    CHECK(card("Cancel",&worker,1)&&!card("Terran Supply Depot",&worker,1));
    ivec2_t construction;
    level.player_resources[consoleplayer][0] = 1000;
    CHECK(site(MT_SUPPLY_DEPOT, hall, worker, &construction));
    M_CentreView(&app, (fvec2_t){construction.x, construction.y});
    fvec2_t screen;
    R_MapToScreen(&app, &level, construction.x + 0.2f, construction.y + 0.2f, &screen.x, &screen.y);
    SDL_Event mouse={.button={.type=SDL_MOUSEBUTTONDOWN,.button=SDL_BUTTON_LEFT,
        .x=(int)screen.x,.y=(int)screen.y}};
    M_MenuResponder(hudmenu, &app, &mouse);
    mouse.button.type = SDL_MOUSEBUTTONUP;
    M_MenuResponder(hudmenu, &app, &mouse);
    CHECK(!hudmenu->target && worker->production && worker->production->placed &&
          worker->production->product_type==MT_SUPPLY_DEPOT &&
          level.player_resources[consoleplayer][0]==900);
    fixed2_t at = fixed2_add(fixed3_xy(hall->core.position), FIXED2_LIT(0, 4));
    mobj_t *ghost = spawn(MT_GHOST, at, consoleplayer), *tank = spawn(MT_SIEGE_TANK, fixed2_add(at, FIXED2_LIT(2, 0)), consoleplayer);
    mobj_t *a = spawn(MT_HIGH_TEMPLAR, fixed2_add(at, FIXED2_LIT(-2, 0)), consoleplayer);
    mobj_t *b = spawn(MT_HIGH_TEMPLAR, fixed2_add(at, FIXED2_LIT(-3, 0)), consoleplayer);
    CHECK(ghost && tank && a && b);
    /* Greyed until researched, then cast from the button. */
    menuitem_t *cloak = card("Personnel Cloaking", &ghost, 1);
    CHECK(cloak && !cloak->enabled && card("Lockdown", &ghost, 1) && card("Nuclear Strike", &ghost, 1));
    level.upgrades[SC_UPGRADES + SC_TECH_PERSONNEL_CLOAKING][consoleplayer].weapon = 1;
    cloak = card("Personnel Cloaking", &ghost, 1);
    CHECK(cloak->enabled && cloak->look[0].cell == sc_techs[SC_TECH_PERSONNEL_CLOAKING].icon);
    click(cloak);
    CHECK(ghost->traits & MF_CLOAKED);
    /* An aimed spell waits for a click on the world. */
    level.upgrades[SC_UPGRADES + SC_TECH_LOCKDOWN][consoleplayer].weapon = 1;
    menuitem_t *lockdown = card("Lockdown", &ghost, 1);
    click(lockdown);
    CHECK(hudmenu->target && hudmenu->target->value==lockdown->value);
    hudmenu->target = NULL;
    level.upgrades[SC_UPGRADES + SC_TECH_SIEGE_MODE][consoleplayer].weapon = 1;
    click(card("Tank Siege Mode", &tank, 1));
    tick(RTS_TICRATE * 3);
    CHECK(tank->type_id == MT_SIEGE_MODE && card("Tank Siege Mode", &tank, 1)->look[0].cell == 246);
    mobj_t *pair[2] = {a, b};
    click(card("Archon Warp", pair, 2));
    CHECK(a->sc.order.kind == SC_ORDER_MERGE && b->sc.order.kind == SC_ORDER_MERGE);
    /* A Bunker's card unloads it. */
    ivec2_t cell;
    CHECK(site(MT_BUNKER, hall, NULL, &cell));
    fixed2_t door = P_BuildingPosition(MT_BUNKER, cell);
    mobj_t *bunker = spawn(MT_BUNKER, door, consoleplayer);
    mobj_t *marine = spawn(MT_MARINE, fixed2_add(door, FIXED2_LIT(0, 1.6)), consoleplayer);
    CHECK(bunker && marine && sc_board(marine, bunker) && (marine->sc.flags & SC_LOADED));
    click(card("Unload All", &bunker, 1));
    CHECK(!(marine->sc.flags & SC_LOADED));
    /* A building card can add a different unit behind the active product. */
    mobj_t *barracks=spawn(MT_BARRACKS,fixed2_add(at,FIXED2_LIT(8,8)),consoleplayer);
    mobj_t *academy=spawn(MT_ACADEMY,fixed2_add(at,FIXED2_LIT(14,8)),consoleplayer);
    mobj_t *supply=spawn(MT_SUPPLY_DEPOT,fixed2_add(at,FIXED2_LIT(18,8)),consoleplayer);
    CHECK(barracks&&academy&&supply);
    level.player_resources[consoleplayer][0]=1000;
    level.player_resources[consoleplayer][1]=1000;
    menuitem_t *train_marine=card("Terran Marine",&barracks,1);
    CHECK(train_marine);
    mouse_click(train_marine);
    menuitem_t *train_firebat=card("Terran Firebat",&barracks,1);
    CHECK(train_firebat);
    mouse_click(train_firebat);
    CHECK(barracks->production&&barracks->production->queue_count==2&&
          P_ProductionItem(barracks->production,1).product_type==MT_FIREBAT);
    /* Rank, personal kills, energy and equipment use the native hero panel. */
    mobj_t *hero=spawn(MT_SARAH_KERRIGAN,fixed2_add(at,FIXED2_LIT(3,9)),consoleplayer);
    mobj_t *victim=spawn(MT_MARINE,fixed2_add(at,FIXED2_LIT(4,9)),1);
    CHECK(hero&&victim&&sc_max_energy(hero)==250<<8);
    sc_start(hero);hero->sc.energy=221<<8;
    P_DamageMobj(victim,hero,victim->hp);
    CHECK(hero->sc.kills==1);
    (void)card("Personnel Cloaking",&hero,1);
    menuitem_t *name=M_MenuFind(hudmenu,65531),*rank=M_MenuFind(hudmenu,65516),
               *kills=M_MenuFind(hudmenu,65515),*energy=M_MenuFind(hudmenu,65528);
    CHECK(name&&rank&&kills&&energy&&name->visible&&rank->visible&&kills->visible&&energy->visible);
    CHECK(!strcmp(name->text,"Sarah Kerrigan")&&!strcmp(rank->text,"Lieutenant")&&
          !strcmp(kills->text,"Kills: 1")&&!strcmp(energy->text,"221/250"));
    CHECK(name->font==rank->font&&name->font->line_h==11&&
          (name->align&MALIGN_HCENTER));
    menuitem_t *armor=M_MenuFind(hudmenu,9),*weapon=M_MenuFind(hudmenu,10);
    CHECK(armor&&weapon&&armor->visible&&weapon->visible&&armor->ownerdraw&&weapon->ownerdraw&&
          armor->value==292&&weapon->value==324);
    draw(NULL);M_MenuDrawer(hudmenu);CHECK(save("/private/tmp/starcraft-hero-status.bmp"));
    (void)card("",&worker,1);
    CHECK(!strcmp(name->text,"Terran SCV")&&!strcmp(rank->text,"Private")&&
          !strcmp(kills->text,"Kills: 0")&&armor->visible&&weapon->visible&&
          armor->value==292&&weapon->value==329);
    for(int i=0;i<hudmenu->numitems;i++)
        CHECK(!hudmenu->items[i].visible||strcmp(hudmenu->items[i].text,"Constructing"));
    draw(NULL);M_MenuDrawer(hudmenu);CHECK(save("/private/tmp/starcraft-scv-status.bmp"));
    return 0;
}

static int flight_hud(void) {
    mobj_t *hall=find(consoleplayer,MT_COMMAND_CENTER),*worker=find(consoleplayer,MT_SCV);
    ivec2_t cell;CHECK(hall&&worker&&site(MT_BARRACKS,hall,worker,&cell));
    mobj_t *building=spawn(MT_BARRACKS,P_BuildingPosition(MT_BARRACKS,cell),consoleplayer);
    CHECK(building);P_SyncBuildingBlocking();
    menuitem_t *lift=card("Lift Off",&building,1);
    CHECK(lift&&lift->hotkey==SDLK_l&&lift->look[0].cell==282);
    mouse_click(lift);CHECK(building->traits&MF_FLY);
    tick(100);
    menuitem_t *land=card("Land",&building,1);
    CHECK(land&&land->hotkey==SDLK_l&&land->look[0].cell==283);
    CHECK(!card("Terran Marine",&building,1)&&card("Move",&building,1));
    M_CentreView(&app,fixed3_xy_to_fvec2(building->core.position));
    draw(NULL);M_MenuDrawer(hudmenu);CHECK(save("/private/tmp/starcraft-lifted-building.bmp"));
    mouse_click(card("Land",&building,1));CHECK(hudmenu->target);
    fvec2_t screen;
    R_MapToScreen(&app,&level,cell.x+0.5f,cell.y+0.5f,&screen.x,&screen.y);
    SDL_Event event={.button={.type=SDL_MOUSEBUTTONDOWN,.button=SDL_BUTTON_LEFT,
        .x=(int)screen.x,.y=(int)screen.y}};
    M_MenuResponder(hudmenu,&app,&event);
    event.button.type=SDL_MOUSEBUTTONUP;M_MenuResponder(hudmenu,&app,&event);
    CHECK(building->sc.order.kind==SC_ORDER_LAND);tick(100);
    CHECK(!(building->traits&MF_FLY)&&card("Lift Off",&building,1));
    P_RemoveMobj(building);P_SyncBuildingBlocking();
    return 0;
}
static int hud_contract(void) {
    mobj_t *hall=find(consoleplayer,MT_COMMAND_CENTER),*worker=find(consoleplayer,MT_SCV);
    CHECK(hall&&worker);
    CHECK(card("Repair",&worker,1)->hotkey==SDLK_r);
    CHECK(!card("Return Cargo",&worker,1));
    worker->harvest.cargo=8;
    CHECK(card("Return Cargo",&worker,1)->hotkey==SDLK_c);
    mouse_click(card("Return Cargo",&worker,1));
    CHECK(worker->harvest.phase==HARVEST_PHASE_TO_BASE);
    mouse_click(card("Stop",&worker,1));
    worker->harvest.cargo=0;
    mobj_t *guard=spawn(MT_MARINE,FIXED2_LIT(20,20),consoleplayer);
    CHECK(guard&&card("Patrol",&guard,1)->hotkey==SDLK_p);
    CHECK(card("Hold Position",&guard,1)->hotkey==SDLK_h);
    mouse_click(card("Hold Position",&guard,1));
    CHECK(guard->sc.flags&SC_HOLD);
    mouse_click(card("Stop",&guard,1));
    CHECK(!(guard->sc.flags&SC_HOLD));
    M_CentreView(&app,fixed3_xy_to_fvec2(guard->core.position));
    fvec2_t destination;
    R_MapToScreen(&app,&level,21.5f,24.5f,&destination.x,&destination.y);
    const char *orders[]={"Patrol","Attack"};
    for(int i=0;i<2;i++) {
        mouse_click(card(orders[i],&guard,1));
        CHECK(hudmenu->target);
        SDL_Event target_click={.button={.type=SDL_MOUSEBUTTONDOWN,.button=SDL_BUTTON_LEFT,
            .x=(int)destination.x,.y=(int)destination.y}};
        M_MenuResponder(hudmenu,&app,&target_click);
        target_click.button.type=SDL_MOUSEBUTTONUP;M_MenuResponder(hudmenu,&app,&target_click);
        CHECK(!hudmenu->target&&guard->waypoints.count==(i?1:2));
        CHECK(guard->waypoints.mode==(i?WP_ONCE:WP_BACKTRACK));
    }
    card("Patrol",&guard,1);draw(NULL);M_MenuDrawer(hudmenu);
    CHECK(save("/private/tmp/starcraft-core-orders.bmp"));
    mobj_t *leader=spawn(MT_MARINE,FIXED2_LIT(22,20),consoleplayer);CHECK(leader);
    P_UpdateSight();
    mouse_click(card("Move",&guard,1));
    R_MapToScreen(&app,&level,22,20,&destination.x,&destination.y);
    SDL_Event follow_click={.button={.type=SDL_MOUSEBUTTONDOWN,.button=SDL_BUTTON_LEFT,
        .x=(int)destination.x,.y=(int)destination.y}};
    M_MenuResponder(hudmenu,&app,&follow_click);
    follow_click.button.type=SDL_MOUSEBUTTONUP;M_MenuResponder(hudmenu,&app,&follow_click);
    CHECK(guard->sc.order.kind==SC_ORDER_FOLLOW&&guard->sc.order.target==leader->id);
    P_RemoveMobj(leader);
    P_RemoveMobj(guard);
    level.player_resources[consoleplayer][0]=1000;
    level.player_resources[consoleplayer][1]=1000;
    /* A full five-slot queue keeps its command, rejects a sixth click, and
     * refunds every paid entry through Stargus's cancel-all command. */
    for(int i=0;i<5;i++)mouse_click(card("Terran SCV",&hall,1));
    menuitem_t *train=card("Terran SCV",&hall,1);
    CHECK(train&&hall->production->queue_count==5);
    int minerals=level.player_resources[consoleplayer][0];
    mouse_click(train);
    CHECK(hall->production->queue_count==5&&level.player_resources[consoleplayer][0]==minerals);
    mouse_click(card("Cancel Unit Training",&hall,1));
    CHECK(!hall->production&&level.player_resources[consoleplayer][0]==1000);
    train=card("Terran SCV",&hall,1);
    SDL_Event key={.key={.type=SDL_KEYDOWN,.keysym={.sym=SDLK_s}}};
    CHECK(M_MenuResponder(hudmenu,&app,&key));
    key.type=SDL_KEYUP;CHECK(M_MenuResponder(hudmenu,&app,&key));
    CHECK(hall->production&&hall->production->queue_count==1);
    CHECK(sc_cancel_production(hall,-1));
    mobj_t *army[7];
    for(int i=0;i<7;i++)CHECK((army[i]=spawn(MT_MARINE,FIXED2_LIT(20,20),consoleplayer)));
    train=card("Terran SCV",&hall,1);
    CHECK(train&&train->enabled);
    mouse_click(train);
    CHECK(!hall->production&&level.player_resources[consoleplayer][0]==1000);
    for(int i=0;i<7;i++)P_RemoveMobj(army[i]);
    /* A group of producers routes training to the shortest usable queue. */
    mobj_t *hall2=spawn(MT_COMMAND_CENTER,FIXED2_LIT(24,24),consoleplayer);
    CHECK(hall2);
    mouse_click(card("Terran SCV",&hall,1));
    mobj_t *halls[]={hall,hall2};
    mouse_click(card("Terran SCV",halls,2));
    CHECK(hall->production->queue_count==1&&hall2->production&&hall2->production->queue_count==1);
    CHECK(sc_cancel_production(hall,-1)&&sc_cancel_production(hall2,-1));
    /* Selection identity, not type alone, abandons an old target. */
    mouse_click(card("Set Rally Point",&hall,1));
    CHECK(hudmenu->target);
    CHECK(card("Terran SCV",&hall2,1)&&!hudmenu->target);
    mouse_click(card("Set Rally Point",&hall2,1));
    mouse_click(card("Cancel",&hall2,1));
    CHECK(!hudmenu->target&&!hall2->sc.rally.set);
    /* Worker pages preserve the scripted gaps and remain visible at zero. */
    mouse_click(card("Build Structure",&worker,1));
    level.player_resources[consoleplayer][0]=0;
    menuitem_t *depot=card("Terran Supply Depot",&worker,1);
    CHECK(depot&&depot->hotkey==SDLK_s);
    mouse_click(depot);
    CHECK(!hudmenu->target&&!worker->production);
    menuitem_t *engineering=card("Terran Engineering Bay",&worker,1);
    CHECK(engineering&&engineering->hotkey==SDLK_e);
    irect_t engineering_rect=engineering->rect;
    mobj_t *barracks=spawn(MT_BARRACKS,FIXED2_LIT(30,24),consoleplayer);
    CHECK(barracks);
    menuitem_t *academy=card("Terran Academy",&worker,1);
    CHECK(academy&&academy->rect.y>engineering_rect.y);
    mouse_click(card("Cancel",&worker,1));
    mobj_t *marine=spawn(MT_MARINE,FIXED2_LIT(20,20),consoleplayer);
    mobj_t *mixed[]={worker,marine};
    CHECK(card("Move",mixed,2)&&card("Attack",mixed,2)&&!card("Build Structure",mixed,2));
    /* Gas shortage does not shuffle the command card or charge minerals. */
    CHECK(spawn(MT_ACADEMY,FIXED2_LIT(34,24),consoleplayer));
    level.player_resources[consoleplayer][0]=500;
    level.player_resources[consoleplayer][1]=0;
    menuitem_t *firebat=card("Terran Firebat",&barracks,1);
    CHECK(firebat&&firebat->hotkey==SDLK_f);
    irect_t firebat_rect=firebat->rect;
    mouse_click(firebat);
    CHECK(!barracks->production&&level.player_resources[consoleplayer][0]==500);
    level.player_resources[consoleplayer][1]=500;
    firebat=card("Terran Firebat",&barracks,1);
    CHECK(firebat&&firebat->rect.x==firebat_rect.x&&firebat->rect.y==firebat_rect.y);
    mouse_click(firebat);
    CHECK(barracks->production&&level.player_resources[consoleplayer][1]==475);
    CHECK(sc_cancel_production(barracks,-1));
    /* Research has a single status icon and cancel-only card. */
    mobj_t *bay=spawn(MT_ENGINEERING_BAY,FIXED2_LIT(38,24),consoleplayer);
    CHECK(bay);
    const StaticProductDefinition *research=G_ModelProductByUIId(NULL,sc_upgrade_product(7,1));
    CHECK(research&&card(research->label,&bay,1));
    mouse_click(card(research->label,&bay,1));
    CHECK(bay->production&&card("Cancel Research",&bay,1)&&!card(research->label,&bay,1));
    CHECK(pipeline_slot(0)&&!pipeline_slot(1));
    mouse_click(card("Cancel Research",&bay,1));
    CHECK(!bay->production);
    /* Cargo icons unload one passenger; empty transports hide Unload All. */
    mobj_t *bunker=spawn(MT_BUNKER,FIXED2_LIT(20,20),consoleplayer);
    CHECK(bunker&&sc_board(marine,bunker));
    mobj_t *passenger2=spawn(MT_MARINE,FIXED2_LIT(20,20),consoleplayer);
    CHECK(passenger2&&sc_board(passenger2,bunker));
    CHECK(card("Unload All",&bunker,1));
    menuitem_t *passenger=card("Unload passenger",&bunker,1);
    CHECK(passenger);
    mouse_click(passenger);
    CHECK(!(marine->sc.flags&SC_LOADED)&&(passenger2->sc.flags&SC_LOADED));
    for(unsigned i=0;i<sizeof(bunker->sc.cargo)/sizeof(*bunker->sc.cargo);i++)
        CHECK(bunker->sc.cargo[i]!=marine->id);
    /* Missing target IDs are not the explicit target-zero Unload All. */
    ticcmd_t stale={.order=TC_UNLOAD,.count=1,.units={bunker->id},.target=UINT32_MAX};
    G_RunTiccmd(consoleplayer,&stale);
    CHECK(passenger2->sc.flags&SC_LOADED);
    mouse_click(card("Unload All",&bunker,1));
    CHECK(!(passenger2->sc.flags&SC_LOADED)&&!card("Unload All",&bunker,1));
    /* Zerg buildings select larvae; their nine products do not collide
     * with rally, research, or building morph buttons. */
    mobj_t *hatchery=spawn(MT_HATCHERY,FIXED2_LIT(42,24),consoleplayer);
    CHECK(hatchery);
    mouse_click(card("Select Larva",&hatchery,1));
    CHECK(P_MobjIsSelected(hatchery));
    mobj_t *larva=spawn(MT_LARVA,FIXED2_LIT(42,27),consoleplayer);
    CHECK(hatchery&&larva);larva->sc.parent=hatchery->id;
    CHECK(card("Select Larva",&hatchery,1)&&!card("Zerg Drone",&hatchery,1));
    mouse_click(card("Select Larva",&hatchery,1));
    CHECK(P_MobjIsSelected(larva)&&!P_MobjIsSelected(hatchery));
    CHECK(card("Zerg Drone",&larva,1)&&card("Zerg Overlord",&larva,1));
    mouse_click(card("Zerg Drone",&larva,1));
    CHECK(larva->production);
    G_ProductionTicker(RTS_TICK_MS);
    CHECK(larva->type_id==MT_EGG&&card("Cancel Production",&larva,1));
    mouse_click(card("Cancel Production",&larva,1));
    CHECK(larva->type_id==MT_LARVA&&!larva->production);
    mobj_t *drone=spawn(MT_DRONE,FIXED2_LIT(40,28),consoleplayer);
    CHECK(drone);
    mouse_click(card("Advanced Structures",&drone,1));
    /* With a Lair the Spire must be the first advanced product, not lost
     * on the basic page because the Zerg has fewer basic structures. */
    CHECK(spawn(MT_LAIR,FIXED2_LIT(46,24),consoleplayer));
    menuitem_t *spire=card("Zerg Spire",&drone,1);
    CHECK(spire&&spire->hotkey==SDLK_s);
    mobj_t *enemy=spawn(MT_BARRACKS,FIXED2_LIT(50,24),1);
    CHECK(enemy);
    level.player_resources[1][0]=1000;
    CHECK(G_QueueProduct(enemy,G_ModelProductByUIId(NULL,MT_MARINE)));
    CHECK(!card("Terran Marine",&enemy,1)&&!pipeline_slot(0));
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
    CHECK(load(kinds, races));
    CHECK(!command_card());
    unload();
    CHECK(load(kinds, races));
    CHECK(!tech_card());
    unload();
    CHECK(load(kinds, races));
    CHECK(!hud_contract());
    CHECK(!flight_hud());
    unload();
    CHECK(!campaign_flow());
    CHECK(!briefing_script());
    sc_set_custom_slots(NULL, NULL);
    M_Shutdown();
    R_FreeSpriteCache(&cache);
    R_FreeTileset(&tiles);
    SDL_Quit();
    puts("PASS: native animations, gathering, construction, training, research, combat, deaths, "
         "victory, defeat, score, in-level dialogs, campaign flow, briefing portraits, the ability command card and its tech-tree buttons");
    return 0;
}
