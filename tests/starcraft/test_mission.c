#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c,"StarCraft mission",#c)

static void put_u16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, unsigned v) { put_u16(p, v); put_u16(p + 2, v >> 16); }

static int write_map(const char *path, const uint8_t owners[12], const uint8_t *trig) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    uint8_t hdr[8], dim[4], era[2] = {0}, terrain[32 * 32 * 2];
    memset(terrain, 0, sizeof(terrain));
    put_u16(dim, 32); put_u16(dim + 2, 32);
    memcpy(hdr, "DIM ", 4); put_u32(hdr + 4, 4); fwrite(hdr, 1, 8, f); fwrite(dim, 1, 4, f);
    memcpy(hdr, "ERA ", 4); put_u32(hdr + 4, 2); fwrite(hdr, 1, 8, f); fwrite(era, 1, 2, f);
    memcpy(hdr, "MTXM", 4); put_u32(hdr + 4, sizeof(terrain)); fwrite(hdr, 1, 8, f); fwrite(terrain, 1, sizeof(terrain), f);
    memcpy(hdr, "OWNR", 4); put_u32(hdr + 4, 12); fwrite(hdr, 1, 8, f); fwrite(owners, 1, 12, f);
    if (trig) {
        memcpy(hdr, "TRIG", 4); put_u32(hdr + 4, 2400); fwrite(hdr, 1, 8, f); fwrite(trig, 1, 2400, f);
    }
    int bad = ferror(f);
    fclose(f);
    return bad;
}

static uint8_t *blank_trigger(void) {
    uint8_t *t = calloc(1, 2400);
    if (t) t[2372 + 17] = 1; /* All Players */
    return t;
}

static void ticks(int n, hudtext_t *hud) {
    int count = 0;
    while (n--) G_MissionTicker(&level, NULL, &count, hud, FIXED_DT);
}

static int press(int id) {
    if (!currentmenu) return 1;
    for (int i = 0; i < currentmenu->numitems; i++) {
        menuitem_t *item = &currentmenu->items[i];
        if (item->id == id && item->kind == MI_BUTTON && item->routine) {
            item->routine(currentmenu, item, MA_ACTIVATE);
            return 0;
        }
    }
    return 1;
}

static bool button(int id) {
    if (!currentmenu) return false;
    for (int i = 0; i < currentmenu->numitems; i++)
        if (currentmenu->items[i].id == id && currentmenu->items[i].kind == MI_BUTTON) return true;
    return false;
}

static mobj_t *spawn(int native, int owner) {
    mobj_t *u = P_SpawnMobj((fixed3_t){(native + 1) * FIXED_ONE, (owner + 1) * FIXED_ONE, 0}, (uint16_t)(native + 1));
    if (u) {
        u->owner = u->team = (uint8_t)owner;
        u->allegiance = owner == consoleplayer ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
    }
    return u;
}

static void unload(void) {
    P_FreeLevel(&level);
    menuleave = false;
    menuactive = false;
    netgame = false;
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    consoleplayer = 0;
    char next[256];
    const char *terran01 = "data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk";
    CHECK(sc_campaign_next(terran01, next, sizeof(next)));
    CHECK(!strcmp(next, "install/campaign/terran/terran02/staredit/scenario.chk"));
    CHECK(!sc_campaign_next("data/STARCRAFT/install/campaign/terran/terran12/staredit/scenario.chk", next, sizeof(next)));
    CHECK(sc_campaign_next("data/STARCRAFT/install/campaign/terran/tutorial/staredit/scenario.chk", next, sizeof(next)));
    CHECK(!strcmp(next, "install/campaign/terran/terran01/staredit/scenario.chk"));

    uint8_t owners[12] = {6};
    uint8_t *trig = blank_trigger();
    CHECK(trig);
    trig[15] = 22;
    trig[320 + 26] = 1;
    const char *victory = "/tmp/open-rts-sc-victory.chk";
    CHECK(!write_map(victory, owners, trig));
    CHECK(G_DoLoadLevel(victory, &level));
    ticks(1, &(hudtext_t){0});
    /* Without the menus (test_gameplay drives those) a result ends the session. */
    CHECK(sc_mission_result() == 1 && menuleave && !menuactive);
    ticks(5, &(hudtext_t){0});
    CHECK(sc_mission_result() == 1);
    unload();

    netgame = true;
    consoleplayer = 0;
    CHECK(G_DoLoadLevel(victory, &level));
    ticks(1, &(hudtext_t){0});
    CHECK(sc_mission_result() == 1 && menuleave);
    free(trig);
    unload();

    trig = blank_trigger();
    CHECK(trig);
    trig[15] = 22;
    uint8_t *action = trig + 320;
    put_u32(action + 20, 5);
    action[26] = 26;
    action[27] = 8;
    CHECK(!write_map("/tmp/open-rts-sc-minerals.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-minerals.chk", &level));
    CHECK(spawn(0, 0) && level.player_resources[0][0] == 0);
    ticks(1, &(hudtext_t){0});
    CHECK(level.player_resources[0][0] == 5);
    ticks(5, &(hudtext_t){0});
    CHECK(level.player_resources[0][0] == 5 && sc_mission_result() == 0);
    free(trig);
    unload();

    memset(owners, 0, sizeof(owners));
    owners[0] = 6; owners[1] = 5;
    trig = blank_trigger();
    CHECK(trig && !write_map("/tmp/open-rts-sc-melee.chk", owners, trig));
    free(trig);
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-melee.chk", &level));
    mobj_t *human = spawn(0, 0), *enemy = spawn(0, 1);
    CHECK(human && enemy);
    P_DamageMobj(enemy, human, enemy->hp);
    ticks(1, &(hudtext_t){0});
    CHECK(sc_mission_result() == 1);
    unload();

    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-melee.chk", &level));
    human = spawn(0, 0); enemy = spawn(0, 1);
    CHECK(human && enemy);
    P_DamageMobj(human, enemy, human->hp);
    ticks(1, &(hudtext_t){0});
    CHECK(sc_mission_result() == 2 && menuleave);
    unload();

    memset(owners, 0, sizeof(owners));
    owners[0] = 6;
    trig = blank_trigger();
    CHECK(trig && !write_map("/tmp/open-rts-sc-solo.chk", owners, trig));
    free(trig);
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-solo.chk", &level));
    CHECK(spawn(0, 0));
    ticks(3, &(hudtext_t){0});
    CHECK(sc_mission_result() == 0 && !menuactive);
    unload();

    /* Play Custom: the player and a Zerg computer on Road War, then the same
     * map with the computer's slot closed. */
    {
        static const char road[] = "data/STARCRAFT/maps/(2)road war.scm/staredit/scenario.chk";
        int kinds[8] = {SC_SLOT_HUMAN, SC_SLOT_COMPUTER, SC_SLOT_CLOSED, SC_SLOT_CLOSED,
                        SC_SLOT_CLOSED, SC_SLOT_CLOSED, SC_SLOT_CLOSED, SC_SLOT_CLOSED};
        int races[8] = {2, 1};
        for (int pass = 0; pass < 2; ++pass) {
            if (pass) kinds[1] = SC_SLOT_CLOSED;
            sc_set_custom_slots(kinds, races);
            P_InitThinkers();
            CHECK(G_DoLoadLevel(road, &level));
            CHECK(P_LoadThings(level.map_path) > 0);
            int nexus = 0, hatchery = 0, other = -1;
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                if (th->function != P_MobjThinker) continue;
                const mobj_t *mo = (const mobj_t *)th;
                if (mo->type_id == MT_NEXUS && mo->owner == consoleplayer) ++nexus;
                if (mo->type_id == MT_HATCHERY) { ++hatchery; other = mo->owner; }
            }
            CHECK(nexus == 1 && hatchery == (pass ? 0 : 1));
            CHECK(level.player_resources[consoleplayer][0] == 50);
            if (!pass) {
                CHECK(other != consoleplayer && sc_owner_kind(other) == 5);
                CHECK(G_AiInterface()->player_level(&level, other) == AI_LEVEL_NORMAL);
                CHECK(G_AiInterface()->player_level(&level, consoleplayer) == AI_LEVEL_NONE);
            }
            P_FreeThinkers();
            unload();
        }
        sc_set_custom_slots(NULL, NULL);
    }

    level.player_resources[0][0] = 10000;
    consoleplayer = 0;
    mobj_t *base = spawn(106, 0), *barracks = spawn(111, 0);
    CHECK(base && barracks);
    for (int i = 0; i < 9; i++) CHECK(spawn(0, 0));
    const StaticProductDefinition *marine = G_ModelProductByUIId(NULL, 1);
    CHECK(marine && G_QueueProduct(barracks, marine));
    CHECK(!G_QueueProduct(barracks, marine));
    unload();
    level.player_resources[0][0] = 10000;
    base = spawn(106, 0); barracks = spawn(111, 0);
    CHECK(base && barracks);
    for (int i = 0; i < 10; i++) CHECK(spawn(0, 0));
    int minerals = level.player_resources[0][0];
    CHECK(!G_QueueProduct(barracks, marine) && level.player_resources[0][0] == minerals);
    unload();

    CHECK(G_DoLoadLevel(terran01, &level));
    CHECK(level.player_resources[1][0] == 40 && consoleplayer == 1);
    CHECK(P_LoadThings(level.map_path) == 46 && P_InitSight());
    const AiGameInterface *ai = G_AiInterface();
    CHECK(ai->player_level(&level, 0) == AI_LEVEL_NONE);
    CHECK(ai->player_level(&level, 1) == AI_LEVEL_NONE);
    CHECK(ai->player_level(&level, 2) == AI_LEVEL_NONE);
    CHECK(ai->player_level(&level, 4) == AI_LEVEL_NORMAL);
    CHECK((level.sight.allies[1] & (UINT32_C(0x40000000) >> 4)) == 0);
    CHECK(level.sight.allies[1] & (UINT32_C(0x40000000) >> 1));
    hudtext_t hud = {0};
    ticks(60, &hud);
    CHECK(level.player_resources[1][0] == 40 && sc_mission_result() == 0);
    CHECK(hud.count > 0);
    unload();
    puts("PASS: campaign advance, triggers, elimination, supply and Terran 01");
    return 0;
}
