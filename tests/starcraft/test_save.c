#include "t_local.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c, "StarCraft saves", #c)

int main(void) {
    G_InitGame();
    P_InitThinkers();
    CHECK(G_DoLoadLevel("data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk", &level));
    CHECK(G_LoadLevelData(g_game_default_root,&level));
    CHECK(P_LoadThings(level.map_path) > 0 && P_InitSight());
    sc_mission_t *m = level.mission;
    CHECK(m && m->trig_count > 0);
    m->rng=1;
    uint32_t random_hash=G_ConsistencyExtra(0);
    CHECK(sc_random()==346 && G_ConsistencyExtra(0)!=random_hash);
    uint32_t outcome_hash = G_ConsistencyExtra(0);
    m->player_result[7] = 2;
    CHECK(G_ConsistencyExtra(0) != outcome_hash);
    m->switches[12] = 1;
    m->deaths[1][0] = UINT32_C(0x80000000);
    m->elapsed_ms = 12345;
    m->countdown_ms = 9876;
    m->ai_on[4] = false;
    m->rt[0] = (sc_trig_t){.cursor = 3, .wait_ms = 1200, .current = 1, .running = true};
    mobj_t *queued_unit=P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(4.5,4.5),0),MT_MARINE);
    CHECK(queued_unit);queued_unit->owner=queued_unit->team=0;
    ticcmd_t move={.order=TC_MOVE,.count=1,.units={queued_unit->id},.position=fixed3_from_fixed2(FIXED2_LIT(9.5,4.5),0)};
    G_RunTiccmd(0,&move);move.queued=true;move.position=fixed3_from_fixed2(FIXED2_LIT(12.5,4.5),0);
    G_RunTiccmd(0,&move);CHECK(queued_unit->sc.queue.count==1);
    mobj_t *attacker=P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(5.5,5.5),0),MT_MARINE);
    mobj_t *target=P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(7.5,5.5),0),MT_MARINE);
    CHECK(attacker && target);
    attacker->owner=attacker->team=0; attacker->allegiance=ALLEGIANCE_PLAYER;
    target->owner=target->team=1; target->allegiance=ALLEGIANCE_ENEMY;
    ticcmd_t first={.order=TC_MOVE,.count=1,.units={attacker->id},
        .position=fixed3_from_fixed2(FIXED2_LIT(6.5,5.5),0)};
    G_RunTiccmd(0,&first);
    ticcmd_t attack={.order=TC_ATTACK,.queued=true,.count=1,.units={attacker->id},
        .target=target->id,.position=target->core.position};
    G_RunTiccmd(0,&attack); CHECK(attacker->sc.queue.count==1);
    target->owner=target->team=0; target->allegiance=ALLEGIANCE_PLAYER;
    P_ClearMove(attacker); attacker->movement.order_arrived=true;
    P_Ticker();
    CHECK(attacker->sc.queue.active==TC_ATTACK && attacker->sc.queue.forced &&
          attacker->attack.target==target && P_CanTarget(attacker,target));
    uint32_t attacker_id=attacker->id, target_id=target->id;
    mobj_t *geyser = sc_spawn_actor(MT_VESPENE_GEYSER - 1, (ivec2_t){320, 320}, 11);
    CHECK(geyser && sc_add_resource_vent(geyser, 5000));
    mobj_t *saved_carrier = sc_spawn_actor(MT_CARRIER - 1, (ivec2_t){384, 384}, 11);
    mobj_t *saved_fighter = sc_spawn_actor(MT_INTERCEPTOR - 1, (ivec2_t){384, 384}, 11);
    CHECK(saved_carrier && saved_fighter); sc_initial_hangar(saved_carrier, 3); saved_fighter->sc.parent = saved_carrier->id;
    sc_initial_vitals(saved_carrier, 4, 33, 0);
    mobj_t *saved_ghost = sc_spawn_actor(MT_GHOST - 1, (ivec2_t){448, 384}, 0); CHECK(saved_ghost);
    level.upgrades[21][0].weapon = 1; sc_initial_vitals(saved_ghost, 8, 0, 33);
    sc_initial_hp(saved_ghost, 1);
    sc_initial_hallucination(saved_ghost);
    saved_ghost->sc.flags |= SC_INVINCIBLE;
    uint32_t ghost_id = saved_ghost->id;
    mobj_t *saved_duke = sc_spawn_actor(MT_DUKE_TANK - 1, (ivec2_t){512, 384}, 0); CHECK(saved_duke);
    ticcmd_t duke_deploy = {.order = TC_DEPLOY, .count = 1, .units = {saved_duke->id}};
    G_RunTiccmd(0, &duke_deploy); CHECK(saved_duke->core.state_id == SC_DUKE_SIEGE_STATE);
    uint32_t duke_id = saved_duke->id;
    uint32_t carrier_id = saved_carrier->id;
    level.sight.allies[0] |= UINT32_C(0x40000000) >> 1;
    level.sight.allies[0] &= ~(UINT32_C(0x40000000) >> 2); /* revoked sharing */
    uint32_t geyser_id = geyser->id;
    int geyser_vent = level.resource_vent_count - 1;
    uint32_t queued_id=queued_unit->id, queued_hash=G_Consistency();
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
    CHECK(G_LoadLevelData(g_game_default_root,&level));
    CHECK(P_LoadThings(level.map_path) > 0 && P_InitSight());
    m = level.mission;
    const uint8_t *strings = m->str;
    blob_t file = m->file;
    const uint8_t *triggers = m->trig, *properties = m->uprp;
    sc_trig_t *runtime = m->rt;
    sc_terrain_t *terrain=m->terrain;
    CHECK(terrain);
    level.sight.allies[0] |= UINT32_C(0x40000000) >> 2;
    CHECK(G_LoadGameBlob(saved, length, &app, &ai, &hud));
    queued_unit=P_MobjById(queued_id);
    CHECK(queued_unit && queued_unit->sc.queue.count==1 && queued_unit->sc.queue.pending[0].position.x==FIXED_LIT(12.5));
    attacker=P_MobjById(attacker_id); target=P_MobjById(target_id);
    CHECK(attacker && target && attacker->sc.queue.active==TC_ATTACK && attacker->sc.queue.forced &&
          attacker->sc.queue.target_owner==target->owner && attacker->attack.target==target &&
          P_CanTarget(attacker,target));
    geyser = P_MobjById(geyser_id);
    CHECK(geyser && geyser->owner == 11 && geyser->type_id == MT_VESPENE_GEYSER);
    CHECK(geyser_vent < level.resource_vent_count && level.resource_vents[geyser_vent].source_id == geyser_id &&
          level.resource_vents[geyser_vent].amount == 5000 && !level.resource_vents[geyser_vent].active);
    CHECK(level.sight.allies[0] & (UINT32_C(0x40000000) >> 1));
    saved_carrier = P_MobjById(carrier_id);
    CHECK(saved_carrier && saved_carrier->owner == 11 && saved_carrier->sc.hangar == 3 && sc_hangar_count(saved_carrier) == 4);
    CHECK(saved_carrier->sc.shields == 49 * 256);
    saved_ghost = P_MobjById(ghost_id);
    CHECK(saved_ghost && saved_ghost->sc.energy == 82 * 256 + 128 &&
          saved_ghost->hp == 1 && saved_ghost->sc.wound == 141 && (saved_ghost->sc.flags & SC_INVINCIBLE) &&
          (saved_ghost->sc.flags & SC_HALLUCINATION) && saved_ghost->sc.timers[SC_TIMER_LIFE] == 1350);
    saved_duke = P_MobjById(duke_id);
    CHECK(saved_duke && saved_duke->core.state_id == SC_DUKE_SIEGE_STATE);
    CHECK(!(level.sight.allies[0] & (UINT32_C(0x40000000) >> 2)));
    CHECK(G_Consistency()==queued_hash);
    CHECK(m->str == strings && m->switches[12] == 1 && m->deaths[1][0] == UINT32_C(0x80000000));
    CHECK(m->file.bytes == file.bytes && m->trig == triggers && m->uprp == properties && m->rt == runtime);
    CHECK(m->terrain==terrain && m->rng==22695478u);
    CHECK(m->player_result[7] == 2);
    CHECK(m->elapsed_ms == 12345 && m->countdown_ms == 9876 && !m->ai_on[4]);
    CHECK(m->rt[0].cursor == 3 && m->rt[0].wait_ms == 1200 && m->rt[0].running);
    int target_hp=target->hp;
    for (int i = 0; i < RTS_TICRATE * 3; i++) { P_Ticker(); G_ProductionTicker(RTS_TICK_MS); }
    CHECK(target->hp<target_hp);
    CHECK(saved_duke->type_id == MT_DUKE_SIEGE);
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
    CHECK(G_LoadLevelData(g_game_default_root,&level));
    CHECK(P_LoadThings(level.map_path) > 0 && P_InitSight());
    int player = consoleplayer;
    CHECK(sc_player_side(player) == 2);
    int other = player == 0 ? 1 : 0;
    uint32_t pbit = UINT32_C(0x40000000) >> player, obit = UINT32_C(0x40000000) >> other;
    ticcmd_t ally = {.order=TC_ALLY,.target=other,.product=1};
    ticcmd_t vision = {.order=TC_SHARE_SIGHT,.target=other,.product=1};
    uint32_t hash = G_ConsistencyExtra(0);
    CHECK((G_AllianceMasks(&level)[player]&pbit) && (level.sight.allies[player]&pbit));
    netactive=true;
    CHECK(G_QueueTiccmd(&ally));
    CHECK(!(G_AllianceMasks(&level)[player]&obit));
    ticcmd_t queued; G_BuildTiccmd(&queued); G_RunTiccmd(player,&queued);
    netactive=false;
    G_RunTiccmd(player,&vision);
    CHECK((G_AllianceMasks(&level)[player]&obit) && !(G_AllianceMasks(&level)[other]&pbit));
    CHECK((level.sight.allies[other]&pbit) && !(level.sight.allies[player]&obit));
    mobj_t self = {.owner=player,.team=player,.allegiance=ALLEGIANCE_PLAYER};
    mobj_t foe = {.owner=other,.team=other,.allegiance=ALLEGIANCE_ENEMY};
    CHECK(P_IsAlly(&self,&foe) && !P_IsAlly(&foe,&self));
    foe.core.position.x=FIXED_ONE;
    level.sight.cells[0]=pbit; level.sight.cells[1]=obit;
    CHECK(P_VisibleTo(&foe,&self) && !P_VisibleTo(&self,&foe));
    CHECK(G_ConsistencyExtra(0) != hash);
    ally.target=player; G_RunTiccmd(player,&ally);
    ally.target=other; ally.product=2; G_RunTiccmd(player,&ally);
    ally.target=7; ally.product=1; G_RunTiccmd(player,&ally);
    CHECK(G_AllianceMasks(&level)[player]&obit);
    CHECK(!(G_AllianceMasks(&level)[player] & (UINT32_C(0x40000000)>>7)));
    sc_mission_t *alliance_mission = level.mission;
    alliance_mission->allied_victory[player] |= obit;
    uint32_t victory_hash = G_ConsistencyExtra(0);
    ally.target=other; ally.product=0; G_RunTiccmd(player,&ally);
    CHECK(!(alliance_mission->allies[player]&obit) && !(alliance_mission->allied_victory[player]&obit));
    ally.product=1; G_RunTiccmd(player,&ally);
    CHECK((alliance_mission->allies[player]&obit) && !(alliance_mission->allied_victory[player]&obit));
    CHECK(G_ConsistencyExtra(0) != victory_hash);
    alliance_mission->allied_victory[player] |= obit;
    CHECK(G_SaveGameBlob(&saved, &length, "custom", &app, &ai, &hud));
    P_FreeLevel(&level);
    sc_set_custom_slots(NULL, NULL);
    consoleplayer = 0;
    CHECK(G_DoLoadLevel(map, &level));
    CHECK(G_LoadLevelData(g_game_default_root,&level));
    CHECK(P_LoadThings(level.map_path) > 0 && P_InitSight());
    CHECK(consoleplayer != player || sc_player_side(consoleplayer) != 2);
    CHECK(G_LoadGameBlob(saved, length, &app, &ai, &hud));
    CHECK(consoleplayer == player && sc_player_side(consoleplayer) == 2);
    CHECK(((sc_mission_t *)level.mission)->allied_victory[player]&obit);
    CHECK((G_AllianceMasks(&level)[player]&obit) && !(G_AllianceMasks(&level)[other]&pbit));
    CHECK((level.sight.allies[other]&pbit) && !(level.sight.allies[player]&obit));
    size_t custom_extra_size = G_SaveExtraSize();
    ((sc_mission_t *)level.mission)->result = 1;
    void *extra = malloc(custom_extra_size); CHECK(extra); G_SaveExtra(extra);
    ((sc_mission_t *)level.mission)->result = 2;
    netgame = true; consoleplayer = (player + 1) % 8;
    CHECK(G_LoadExtra(extra, custom_extra_size) && consoleplayer == (player + 1) % 8);
    CHECK(((sc_mission_t *)level.mission)->result == 1); /* Ordinary load restores its result. */
    netgame = false; CHECK(G_LoadExtra(extra, custom_extra_size) && consoleplayer == player);
    free(extra);
    /* A resync carries shared player outcomes, not the authority's local UI. */
    alliance_mission = level.mission;
    alliance_mission->player_result[player] = 1;
    alliance_mission->player_result[other] = 2;
    alliance_mission->result = 1; level.outcome = OUTCOME_VICTORY; level.foes = 0x80;
    void *resync = NULL; size_t resync_size = 0;
    CHECK(G_ResyncSave(&resync, &resync_size, &app, &ai, &hud));
    consoleplayer = other; netgame = true;
    alliance_mission->result = 0; level.outcome = OUTCOME_NONE; level.foes = 0x02;
    CHECK(G_ResyncLoad(resync, resync_size, &app, &ai, &hud));
    free(resync);
    alliance_mission = level.mission;
    CHECK(consoleplayer == other && !alliance_mission->result && level.outcome == OUTCOME_NONE && level.foes == 0x02 &&
          alliance_mission->player_result[player] == 1 && alliance_mission->player_result[other] == 2);
    sc_mission_tick(&level, &hud, RTS_TICK_MS);
    CHECK(alliance_mission->result == 2 && level.outcome == OUTCOME_DEFEAT);
    netgame = false; consoleplayer = player; menuleave = false;
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
