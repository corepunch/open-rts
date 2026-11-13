#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include "../../play/p_ai_local.h"
#include <limits.h>
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c,"StarCraft mission",#c)

static void put_u16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, unsigned v) { put_u16(p, v); put_u16(p + 2, v >> 16); }
static int force_fixture = -1;

static int write_map_sized(const char *path, const uint8_t owners[12], const uint8_t *trig, size_t trig_size) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    uint8_t hdr[8], dim[4], era[2] = {0}, terrain[32 * 32 * 2];
    memset(terrain, 0, sizeof(terrain));
    put_u16(dim, 32); put_u16(dim + 2, 32);
    memcpy(hdr, "DIM ", 4); put_u32(hdr + 4, 4); fwrite(hdr, 1, 8, f); fwrite(dim, 1, 4, f);
    memcpy(hdr, "ERA ", 4); put_u32(hdr + 4, 2); fwrite(hdr, 1, 8, f); fwrite(era, 1, 2, f);
    memcpy(hdr, "MTXM", 4); put_u32(hdr + 4, sizeof(terrain)); fwrite(hdr, 1, 8, f); fwrite(terrain, 1, sizeof(terrain), f);
    memcpy(hdr, "OWNR", 4); put_u32(hdr + 4, 12); fwrite(hdr, 1, 8, f); fwrite(owners, 1, 12, f);
    if(force_fixture>=0) {
        uint8_t force[20]={0};
        for(int i=2;i<8;i++)force[i]=1;
        force[16]=(uint8_t)force_fixture;
        memcpy(hdr,"FORC",4);put_u32(hdr+4,sizeof(force));fwrite(hdr,1,8,f);fwrite(force,1,sizeof(force),f);
    }
    if (trig) {
        memcpy(hdr, "TRIG", 4); put_u32(hdr + 4, (unsigned)trig_size); fwrite(hdr, 1, 8, f);
        fwrite(trig, 1, trig_size, f);
    }
    int bad = ferror(f);
    fclose(f);
    return bad;
}

static int write_map(const char *path, const uint8_t owners[12], const uint8_t *trig) {
    return write_map_sized(path, owners, trig, trig ? 2400 : 0);
}

static uint8_t *blank_trigger(void) {
    uint8_t *t = calloc(1, 2400);
    if (t) t[2372 + 17] = 1; /* All Players */
    return t;
}

/* The driver's mission and outcome steps of a tic. */
static void ticks(int n, hudtext_t *hud) {
    while (n--) {
        mobjlist_t all = P_ListMobjs();
        int count = all.count;
        G_MissionTicker(&level, all.items, &count, hud, RTS_TICK_MS);
        G_OutcomeTicker(all.items, all.count);
        P_FreeMobjList(&all);
    }
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
    uint8_t force_owners[12]={6,5};
    for(int flag=2;flag<=16;flag*=2) {
        force_fixture=flag;
        CHECK(!write_map("/private/tmp/sc-forces.chk",force_owners,NULL));
        CHECK(G_DoLoadLevel("/private/tmp/sc-forces.chk",&level));
        CHECK(!!(G_AllianceMasks(&level)[0]&(UINT32_C(0x40000000)>>1)) == (flag==2));
        CHECK(!!(((sc_mission_t *)level.mission)->allied_victory[0]&(UINT32_C(0x40000000)>>1)) == false);
        CHECK(!!(level.sight.allies[0]&(UINT32_C(0x40000000)>>1)) == (flag==16));
        unload();
    }
    force_fixture=6;
    CHECK(!write_map("/private/tmp/sc-forces.chk",force_owners,NULL));
    CHECK(G_DoLoadLevel("/private/tmp/sc-forces.chk",&level));
    CHECK(((sc_mission_t *)level.mission)->allied_victory[0] & (UINT32_C(0x40000000)>>1));
    unload();
    force_fixture=-1; remove("/private/tmp/sc-forces.chk");
    for (int status = 0; status <= 2; status++) {
        uint8_t *alliance_trigger=blank_trigger(); CHECK(alliance_trigger);
        alliance_trigger[2372+17]=0; alliance_trigger[2372]=1;
        alliance_trigger[15]=22; alliance_trigger[320+26]=57;
        put_u32(alliance_trigger+320+16,1); put_u16(alliance_trigger+320+24,status);
        CHECK(!write_map("/private/tmp/sc-alliance.chk",force_owners,alliance_trigger));
        CHECK(G_DoLoadLevel("/private/tmp/sc-alliance.chk",&level));
        ticks(1,&(hudtext_t){0});
        uint32_t bit = UINT32_C(0x40000000)>>1;
        CHECK(!!(G_AllianceMasks(&level)[0]&bit) == (status != 0));
        CHECK(!!(((sc_mission_t *)level.mission)->allied_victory[0]&bit) == (status == 2));
        CHECK(!(G_AllianceMasks(&level)[1]&(UINT32_C(0x40000000)>>0)));
        CHECK(!(level.sight.allies[1]&(UINT32_C(0x40000000)>>0)));
        unload(); free(alliance_trigger);
    }
    remove("/private/tmp/sc-alliance.chk");
    uint8_t *vision_trigger = blank_trigger(); CHECK(vision_trigger);
    vision_trigger[2372+17] = 0; vision_trigger[2372] = 1;
    vision_trigger[15] = 22; vision_trigger[320+26] = 15;
    put_u32(vision_trigger+320+20, 0x3169562b); /* +Vi1 */
    CHECK(!write_map("/private/tmp/sc-vision.chk", force_owners, vision_trigger));
    CHECK(G_DoLoadLevel("/private/tmp/sc-vision.chk", &level));
    CHECK(P_InitSight() && spawn(MT_MARINE - 1, 1));
    P_UpdateSight();
    int sight_cell = L_Index(&level, 1, 2);
    CHECK(!(level.sight.cells[sight_cell] & SIGHT_EXPLORED));
    CHECK(!(level.sight.allies[0] & (UINT32_C(0x40000000) >> 1)));
    ticks(1, &(hudtext_t){0});
    CHECK(level.sight.allies[0] & (UINT32_C(0x40000000) >> 1));
    CHECK(level.sight.cells[sight_cell] & SIGHT_EXPLORED);
    CHECK(!(level.sight.allies[1] & (UINT32_C(0x40000000) >> 0)));
    CHECK(!(G_AllianceMasks(&level)[0] & (UINT32_C(0x40000000) >> 1)));
    unload();
    put_u32(vision_trigger+320+20, 0x3169562d); /* -Vi1 */
    CHECK(!write_map("/private/tmp/sc-vision.chk", force_owners, vision_trigger));
    CHECK(G_DoLoadLevel("/private/tmp/sc-vision.chk", &level));
    CHECK(P_InitSight() && spawn(MT_MARINE - 1, 1));
    level.sight.allies[0] |= UINT32_C(0x40000000) >> 1;
    P_UpdateSight();
    CHECK(level.sight.cells[sight_cell] & SIGHT_EXPLORED);
    ticks(1, &(hudtext_t){0});
    CHECK(!(level.sight.allies[0] & (UINT32_C(0x40000000) >> 1)));
    CHECK(level.sight.cells[sight_cell] & SIGHT_EXPLORED);
    CHECK(!(level.sight.cells[sight_cell] & level.sight.allies[0]));
    unload();
    put_u32(vision_trigger+320+20, 0x3069562d); /* -Vi0 */
    CHECK(!write_map("/private/tmp/sc-vision.chk", force_owners, vision_trigger));
    CHECK(G_DoLoadLevel("/private/tmp/sc-vision.chk", &level));
    ticks(1, &(hudtext_t){0});
    CHECK(level.sight.allies[0] & UINT32_C(0x40000000));
    unload(); free(vision_trigger); remove("/private/tmp/sc-vision.chk");
    /* Both AI-script forms act for the trigger owner, not an unused player field. */
    for (int id = 15; id <= 16; id++) {
        uint8_t *ai_trigger = blank_trigger(); CHECK(ai_trigger);
        ai_trigger[2372+17] = 0; ai_trigger[2372+1] = 1;
        ai_trigger[15] = 22; ai_trigger[320+26] = (uint8_t)id;
        put_u32(ai_trigger+320+16, 0); put_u32(ai_trigger+320+20, 0x74657374);
        CHECK(!write_map("/private/tmp/sc-ai-script.chk", force_owners, ai_trigger));
        CHECK(G_DoLoadLevel("/private/tmp/sc-ai-script.chk", &level));
        ticks(1, &(hudtext_t){0});
        CHECK(!sc_player_ai(0) && sc_player_ai(1));
        unload(); free(ai_trigger);
    }
    remove("/private/tmp/sc-ai-script.chk");
    /* A zero-sized location is not the special Anywhere location. */
    uint8_t *empty_location = blank_trigger(); CHECK(empty_location);
    put_u32(empty_location, 1); put_u32(empty_location+8, 1);
    put_u16(empty_location+12, MT_MARINE - 1); empty_location[15] = 3;
    empty_location[320+26] = 26; empty_location[320+27] = 8;
    put_u32(empty_location+320+20, 10);
    uint8_t one_owner[12] = {6};
    CHECK(!write_map("/private/tmp/sc-empty-location.chk", one_owner, empty_location));
    CHECK(G_DoLoadLevel("/private/tmp/sc-empty-location.chk", &level));
    ((sc_mission_t *)level.mission)->loc_count = 1;
    CHECK(spawn(MT_MARINE - 1, 0));
    ticks(1, &(hudtext_t){0});
    CHECK(level.player_resources[0][0] == 0);
    unload(); free(empty_location); remove("/private/tmp/sc-empty-location.chk");
    /* Opponents counts active slots even without units, and alliance is mutual. */
    for (int relation = 0; relation < 5; relation++) {
        uint8_t *opponent_trigger = blank_trigger(); CHECK(opponent_trigger);
        opponent_trigger[2372+17] = 0; opponent_trigger[2372] = 1;
        put_u32(opponent_trigger+8, 1); opponent_trigger[14] = 10; opponent_trigger[15] = 14;
        opponent_trigger[320+26] = 26; opponent_trigger[320+27] = 8;
        put_u32(opponent_trigger+320+20, 10);
        CHECK(!write_map("/private/tmp/sc-opponents.chk", force_owners, opponent_trigger));
        CHECK(G_DoLoadLevel("/private/tmp/sc-opponents.chk", &level));
        sc_mission_t *opponent_mission = level.mission;
        if (relation >= 1) opponent_mission->allies[0] |= UINT32_C(0x40000000) >> 1;
        if (relation >= 2) opponent_mission->allies[1] |= UINT32_C(0x40000000);
        if (relation >= 3) opponent_mission->allied_victory[0] |= UINT32_C(0x40000000) >> 1;
        if (relation >= 4) opponent_mission->allied_victory[1] |= UINT32_C(0x40000000);
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[0][0] == (relation < 2 ? 10 : 0));
        unload(); free(opponent_trigger);
    }
    remove("/private/tmp/sc-opponents.chk");
    /* Neutral Players reads directed relation state, excluding the owner. */
    uint8_t neutral_group_owners[12] = {6, 5, 7, 7, 3};
    for (int neutral_case = 0; neutral_case < 4; neutral_case++) {
        uint8_t *neutral_group_trigger = blank_trigger(); CHECK(neutral_group_trigger);
        neutral_group_trigger[2372 + 17] = 0;
        int current = neutral_case == 0 ? 0 : 2;
        neutral_group_trigger[2372 + current] = 1;
        neutral_group_trigger[15] = 22; /* Always. */
        uint8_t *neutral_group_action = neutral_group_trigger + 320;
        if (neutral_case >= 2) {
            neutral_group_action[26] = 57; /* Ally or Enemy overrides current->slot 3. */
            put_u32(neutral_group_action + 16, 3);
            put_u16(neutral_group_action + 24, neutral_case == 2 ? 1 : 0);
            neutral_group_action += 32;
        }
        neutral_group_action[26] = 26; neutral_group_action[27] = 7;
        put_u32(neutral_group_action + 16, neutral_case < 2 ? 16 : neutral_case == 2 ? 15 : 14);
        put_u32(neutral_group_action + 20, 10);
        CHECK(!write_map("/private/tmp/sc-neutral-group.chk", neutral_group_owners, neutral_group_trigger));
        CHECK(G_DoLoadLevel("/private/tmp/sc-neutral-group.chk", &level));
        level.player_resources[2][0] = 5;
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[0][0] == (neutral_case == 1 ? 10 : 0));
        CHECK(level.player_resources[1][0] == (neutral_case == 1 ? 10 : 0));
        CHECK(level.player_resources[2][0] == (neutral_case == 0 ? 10 : 5));
        CHECK(level.player_resources[3][0] == 10);
        CHECK(level.player_resources[4][0] == (neutral_case < 2 ? 10 : 0));
        unload(); free(neutral_group_trigger);
    }
    remove("/private/tmp/sc-neutral-group.chk");
    /* Accumulate conditions use the same directed Enemy/Ally/Neutral groups. */
    const unsigned relation_groups[] = {14, 15, 16};
    for (size_t i = 0; i < sizeof(relation_groups) / sizeof(relation_groups[0]); i++) {
        uint8_t *relation_condition = blank_trigger(); CHECK(relation_condition);
        relation_condition[2372 + 17] = 0; relation_condition[2372 + 2] = 1;
        relation_condition[14] = 0; relation_condition[15] = 4;
        put_u32(relation_condition + 4, relation_groups[i]);
        put_u32(relation_condition + 8, 1); relation_condition[16] = 0;
        uint8_t *set_current = relation_condition + 320;
        set_current[26] = 26; set_current[27] = 7;
        put_u32(set_current + 16, 13); put_u32(set_current + 20, 10);
        CHECK(!write_map("/private/tmp/sc-neutral-condition.chk", neutral_group_owners, relation_condition));
        CHECK(G_DoLoadLevel("/private/tmp/sc-neutral-condition.chk", &level));
        sc_mission_t *relation_mission = level.mission;
        uint32_t p3_bit = UINT32_C(0x40000000) >> 3;
        if (relation_groups[i] != 16) relation_mission->neutral_relations[2] &= ~p3_bit;
        if (relation_groups[i] == 15) relation_mission->allies[2] |= p3_bit;
        else relation_mission->allies[2] &= ~p3_bit;
        level.player_resources[3][0] = 1;
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[2][0] == 10 && level.player_resources[3][0] == 1);
        unload(); free(relation_condition);
    }
    remove("/private/tmp/sc-neutral-condition.chk");
    /* Group 26 excludes a bilateral Ally state even without Allied Victory. */
    uint8_t *allied_group_triggers = calloc(3, 2400); CHECK(allied_group_triggers);
    uint8_t *set_allies = allied_group_triggers;
    set_allies[2372] = set_allies[2373] = 1; set_allies[2372 + 17] = 0;
    set_allies[15] = 22;
    set_allies[320 + 26] = 57;
    put_u32(set_allies + 320 + 16, 17); /* All Players. */
    put_u16(set_allies + 320 + 24, 1); /* CHK Ally. */
    uint8_t *allied_group_action = allied_group_triggers + 2400;
    allied_group_action[2372] = 1; allied_group_action[2372 + 17] = 0;
    allied_group_action[15] = 22;
    allied_group_action[320 + 26] = 26; allied_group_action[320 + 27] = 7;
    put_u32(allied_group_action + 320 + 16, 26); /* Non Allied Victory Players. */
    put_u32(allied_group_action + 320 + 20, 10);
    uint8_t *allied_group_condition = allied_group_triggers + 4800;
    allied_group_condition[2372] = 1; allied_group_condition[2372 + 17] = 0;
    allied_group_condition[14] = 0; allied_group_condition[15] = 4;
    put_u32(allied_group_condition + 4, 26); put_u32(allied_group_condition + 8, 1);
    allied_group_condition[16] = 0;
    allied_group_condition[320 + 26] = 26; allied_group_condition[320 + 27] = 7;
    put_u32(allied_group_condition + 320 + 16, 13); /* Current Player. */
    put_u32(allied_group_condition + 320 + 20, 10);
    uint8_t plain_allies[12] = {6, 5};
    CHECK(!write_map_sized("/private/tmp/sc-plain-allies-group26.chk", plain_allies,
                           allied_group_triggers, 3 * 2400));
    CHECK(G_DoLoadLevel("/private/tmp/sc-plain-allies-group26.chk", &level));
    level.player_resources[1][0] = 1;
    ticks(1, &(hudtext_t){0});
    sc_mission_t *plain_allies_mission = level.mission;
    uint32_t p1_bit = UINT32_C(0x40000000) >> 1;
    CHECK((plain_allies_mission->allies[0] & p1_bit) &&
          (plain_allies_mission->allies[1] & UINT32_C(0x40000000)));
    CHECK(!(plain_allies_mission->allied_victory[0] & p1_bit) &&
          !(plain_allies_mission->allied_victory[1] & UINT32_C(0x40000000)));
    CHECK(level.player_resources[1][0] == 1 && level.player_resources[0][0] == 0);
    unload(); free(allied_group_triggers); remove("/private/tmp/sc-plain-allies-group26.chk");

    /* Neutral-controller status overrides coincident force ally/victory masks. */
    force_fixture = 6;
    uint8_t neutral_force_owners[12] = {7, 6};
    uint8_t *neutral_force_trigger = blank_trigger(); CHECK(neutral_force_trigger);
    neutral_force_trigger[2372 + 17] = 0; neutral_force_trigger[2372] = 1;
    neutral_force_trigger[15] = 22;
    uint8_t *force_action = neutral_force_trigger + 320;
    force_action[26] = 26; force_action[27] = 7; put_u32(force_action + 16, 15);
    put_u32(force_action + 20, 5); put_u16(force_action + 24, 1); /* Allies -> gas 5. */
    force_action += 32; force_action[26] = 26; force_action[27] = 7; put_u32(force_action + 16, 16);
    put_u32(force_action + 20, 10); put_u16(force_action + 24, 0); /* Neutral -> ore 10. */
    force_action += 32; force_action[26] = 26; force_action[27] = 7; put_u32(force_action + 16, 26);
    put_u32(force_action + 20, 7); put_u16(force_action + 24, 1); /* Not Allied Victory -> gas 7. */
    CHECK(!write_map("/private/tmp/sc-neutral-force-relation.chk", neutral_force_owners, neutral_force_trigger));
    CHECK(G_DoLoadLevel("/private/tmp/sc-neutral-force-relation.chk", &level));
    ticks(1, &(hudtext_t){0});
    sc_mission_t *neutral_force_mission = level.mission;
    CHECK((neutral_force_mission->neutral_relations[0] & p1_bit) &&
          (neutral_force_mission->allies[0] & p1_bit) &&
          (neutral_force_mission->allied_victory[0] & p1_bit));
    CHECK(level.player_resources[1][0] == 10 && level.player_resources[1][1] == 7);
    unload(); free(neutral_force_trigger); remove("/private/tmp/sc-neutral-force-relation.chk");
    force_fixture = -1;
    /* An inactive source slot contributes no opponents. */
    uint8_t inactive_owners[12] = {0, 6};
    uint8_t *inactive_opponents = blank_trigger(); CHECK(inactive_opponents);
    inactive_opponents[2372+17] = 0; inactive_opponents[2372+1] = 1;
    put_u32(inactive_opponents+8, 1); inactive_opponents[14] = 10; inactive_opponents[15] = 14;
    inactive_opponents[320+26] = 26; inactive_opponents[320+27] = 8;
    put_u32(inactive_opponents+320+20, 10);
    CHECK(!write_map("/private/tmp/sc-inactive-opponents.chk", inactive_owners, inactive_opponents));
    CHECK(G_DoLoadLevel("/private/tmp/sc-inactive-opponents.chk", &level));
    ticks(1, &(hudtext_t){0});
    CHECK(level.player_resources[0][0] == 0);
    unload(); free(inactive_opponents); remove("/private/tmp/sc-inactive-opponents.chk");
    /* Most/least conditions use the trigger owner; their player field is unused. */
    const int comparison_conditions[] = {6, 7, 16, 17};
    for (int ci = 0; ci < 4; ci++) {
        int condition = comparison_conditions[ci];
        uint8_t *compare_trigger = blank_trigger(); CHECK(compare_trigger);
        compare_trigger[2372+17] = 0; compare_trigger[2372+1] = 1;
        put_u32(compare_trigger, 64); put_u32(compare_trigger+4, 0); /* decoy player */
        put_u16(compare_trigger+12, MT_MARINE - 1); compare_trigger[15] = (uint8_t)condition;
        compare_trigger[320+26] = 26; compare_trigger[320+27] = 8;
        put_u32(compare_trigger+320+16, 1); put_u32(compare_trigger+320+20, 10);
        CHECK(!write_map("/private/tmp/sc-compare-owner.chk", force_owners, compare_trigger));
        CHECK(G_DoLoadLevel("/private/tmp/sc-compare-owner.chk", &level));
        CHECK(spawn(MT_MARINE - 1, 0) && spawn(MT_MARINE - 1, 1));
        CHECK(spawn(MT_MARINE - 1, condition < 16 ? 1 : 0));
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[1][0] == 10);
        unload(); free(compare_trigger);
    }
    remove("/private/tmp/sc-compare-owner.chk");
    /* Neutral and rescuable player slots participate in Most and Least. */
    for (int kind = 3; kind <= 7; kind += 4) for (int least = 0; least <= 1; least++) {
        uint8_t compare_owners[12] = {6, 5, (uint8_t)kind};
        uint8_t *compare_trigger = blank_trigger(); CHECK(compare_trigger);
        compare_trigger[2372+17] = 0; compare_trigger[2372+1] = 1;
        put_u16(compare_trigger+12, MT_MARINE - 1); compare_trigger[15] = least ? 16 : 6;
        compare_trigger[320+26] = 26; compare_trigger[320+27] = 8;
        put_u32(compare_trigger+320+16, 1); put_u32(compare_trigger+320+20, 10);
        CHECK(!write_map("/private/tmp/sc-compare-neutral.chk", compare_owners, compare_trigger));
        CHECK(G_DoLoadLevel("/private/tmp/sc-compare-neutral.chk", &level));
        CHECK(spawn(MT_MARINE - 1, 1) && spawn(MT_MARINE - 1, 1));
        int others = least ? 3 : 1;
        for (int i = 0; i < others; i++) CHECK(spawn(MT_MARINE - 1, 0));
        mobj_t *third[3];
        int third_count = least ? 1 : 3;
        for (int i = 0; i < third_count; i++) { third[i] = spawn(MT_MARINE - 1, 2); CHECK(third[i]); }
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[1][0] == 0);
        if (least) CHECK(spawn(MT_MARINE - 1, 2) && spawn(MT_MARINE - 1, 2));
        else { P_RemoveMobj(third[1]); P_RemoveMobj(third[2]); }
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[1][0] == 10);
        unload(); free(compare_trigger);
    }
    remove("/private/tmp/sc-compare-neutral.chk");
    /* Giving a transport also transfers passengers and clears its old orders. */
    uint8_t *give_trigger = blank_trigger(); CHECK(give_trigger);
    give_trigger[2372+17] = 0; give_trigger[2372+1] = 1;
    give_trigger[15] = 22; give_trigger[320+26] = 48;
    put_u32(give_trigger+320, 64); put_u32(give_trigger+320+16, 0);
    put_u32(give_trigger+320+20, 13); put_u16(give_trigger+320+24, MT_DROPSHIP - 1);
    CHECK(!write_map("/private/tmp/sc-give.chk", force_owners, give_trigger));
    CHECK(G_DoLoadLevel("/private/tmp/sc-give.chk", &level));
    mobj_t *transport = spawn(MT_DROPSHIP - 1, 0), *passenger = spawn(MT_MARINE - 1, 0);
    CHECK(transport && passenger);
    transport->sc.cargo[0] = passenger->id;
    passenger->sc.flags |= SC_LOADED; passenger->sc.parent = transport->id;
    transport->sc.queue.count = 1; transport->sc.order.kind = SC_ORDER_FOLLOW; transport->sc.flags |= SC_HOLD;
    mobj_t *near_enemy = spawn(MT_MARINE - 1, 0); CHECK(near_enemy);
    P_MobjSetSelected(transport, true);
    ticks(1, &(hudtext_t){0});
    CHECK(transport->owner == 1 && transport->team == 1 && transport->allegiance == ALLEGIANCE_ENEMY);
    CHECK(passenger->owner == 1 && passenger->sc.parent == transport->id && (passenger->sc.flags & SC_LOADED));
    CHECK(!transport->attack.target && !passenger->attack.target && near_enemy->hp == near_enemy->max_hp);
    CHECK(!P_MobjIsSelected(transport) && !transport->sc.queue.count && !transport->sc.order.kind &&
          !(transport->sc.flags & SC_HOLD));
    unload();
    put_u16(give_trigger+320+24, MT_FACTORY - 1);
    CHECK(!write_map("/private/tmp/sc-give.chk", force_owners, give_trigger));
    CHECK(G_DoLoadLevel("/private/tmp/sc-give.chk", &level));
    mobj_t *factory = spawn(MT_FACTORY - 1, 0), *shop = spawn(MT_MACHINE_SHOP - 1, 0);
    CHECK(factory && shop); factory->sc.addon = shop->id; shop->sc.parent = factory->id;
    CHECK(spawn(MT_SUPPLY_DEPOT - 1, 0));
    level.player_resources[0][0] = 500;
    const StaticProductDefinition *vulture = G_ModelProductByUIId(NULL, MT_VULTURE);
    CHECK(vulture && G_QueueProduct(factory, vulture) && factory->production);
    level.player_resources[0][0] = INT_MAX - 1;
    int after_payment = level.player_resources[0][0];
    ticks(1, &(hudtext_t){0});
    CHECK(factory->owner == 1 && shop->owner == 1 && shop->sc.parent == factory->id);
    CHECK(!factory->production && level.player_resources[0][0] > after_payment && level.player_resources[0][0] == INT_MAX);
    unload();
    put_u16(give_trigger+320+24, MT_CARRIER - 1);
    CHECK(!write_map("/private/tmp/sc-give.chk", force_owners, give_trigger));
    CHECK(G_DoLoadLevel("/private/tmp/sc-give.chk", &level));
    mobj_t *carrier = spawn(MT_CARRIER - 1, 0), *fighter = spawn(MT_INTERCEPTOR - 1, 0);
    CHECK(carrier && fighter); fighter->sc.parent = carrier->id;
    level.upgrades[43][0].weapon = 0; level.upgrades[43][1].weapon = 1; /* Carrier Capacity */
    ticks(1, &(hudtext_t){0});
    CHECK(carrier->owner == 1 && fighter->owner == 1 && fighter->sc.parent == carrier->id);
    CHECK(sc_upgrade_level(1, 43) == 1);
    unload();
    put_u16(give_trigger+320+24, MT_MARINE - 1);
    give_trigger[320+27] = 1;
    CHECK(!write_map("/private/tmp/sc-give.chk", force_owners, give_trigger));
    CHECK(G_DoLoadLevel("/private/tmp/sc-give.chk", &level));
    transport = spawn(MT_DROPSHIP - 1, 0); passenger = spawn(MT_MARINE - 1, 0);
    mobj_t *free_marine = spawn(MT_MARINE - 1, 0); CHECK(transport && passenger && free_marine);
    mobj_t *near_ghost = spawn(MT_GHOST - 1, 0); CHECK(near_ghost && P_InitSight());
    level.upgrades[16][0].weapon = 1; /* U-238 Shells */
    level.upgrades[SC_UPGRADES + SC_TECH_STIM_PACKS][0].weapon = 0;
    level.upgrades[SC_UPGRADES + SC_TECH_STIM_PACKS][1].weapon = 0;
    P_UpdateSight();
    CHECK(mobjinfo[MT_MARINE].missilestate != mobjinfo[MT_MARINE].spawnstate);
    free_marine->core.state_id = mobjinfo[MT_MARINE].missilestate;
    transport->sc.cargo[0] = passenger->id;
    passenger->sc.flags |= SC_LOADED; passenger->sc.parent = transport->id;
    ticks(1, &(hudtext_t){0});
    CHECK(transport->owner == 0 && passenger->owner == 0 && passenger->sc.parent == transport->id);
    CHECK(free_marine->owner == 1 && free_marine->core.state_id == mobjinfo[MT_MARINE].spawnstate &&
          !free_marine->attack.target && near_ghost->hp == near_ghost->max_hp);
    CHECK(sc_upgrade_level(1, 16) == 1 && sc_has_tech(1, SC_TECH_STIM_PACKS));
    CHECK(!sc_has_tech(0, SC_TECH_STIM_PACKS));
    unload();
    put_u16(give_trigger+320+24, MT_GHOST - 1);
    CHECK(!write_map("/private/tmp/sc-give.chk", force_owners, give_trigger));
    CHECK(G_DoLoadLevel("/private/tmp/sc-give.chk", &level));
    mobj_t *given_ghost = spawn(MT_GHOST - 1, 0); CHECK(given_ghost);
    level.upgrades[20][0].weapon = 1; level.upgrades[20][1].weapon = 0;
    level.upgrades[21][0].weapon = 0; level.upgrades[21][1].weapon = 1;
    level.upgrades[SC_UPGRADES + SC_TECH_LOCKDOWN][1].weapon = 0;
    level.upgrades[SC_UPGRADES + SC_TECH_PERSONNEL_CLOAKING][1].weapon = 0;
    ticks(1, &(hudtext_t){0});
    CHECK(given_ghost->owner == 1 && sc_upgrade_level(1, 20) == 1 && sc_upgrade_level(1, 21) == 1);
    CHECK(sc_has_tech(1, SC_TECH_LOCKDOWN) && sc_has_tech(1, SC_TECH_PERSONNEL_CLOAKING));
    unload();
    put_u16(give_trigger+320+24, MT_MARINE - 1);
    give_trigger[320+27] = 0;
    CHECK(!write_map("/private/tmp/sc-give.chk", force_owners, give_trigger));
    CHECK(G_DoLoadLevel("/private/tmp/sc-give.chk", &level));
    mobj_t *incomplete = spawn(MT_MARINE - 1, 0); CHECK(incomplete);
    incomplete->production = calloc(1, sizeof(*incomplete->production)); CHECK(incomplete->production);
    incomplete->production->placed = true;
    incomplete->core.state_id = mobjinfo[MT_MARINE].missilestate;
    ticks(1, &(hudtext_t){0});
    CHECK(incomplete->owner == 1 && incomplete->production &&
          incomplete->core.state_id == mobjinfo[MT_MARINE].missilestate);
    unload();
    put_u16(give_trigger+320+24, MT_REFINERY - 1);
    CHECK(!write_map("/private/tmp/sc-give.chk", force_owners, give_trigger));
    CHECK(G_DoLoadLevel("/private/tmp/sc-give.chk", &level));
    mobj_t *gas_building = sc_spawn_actor(MT_REFINERY - 1, (ivec2_t){512, 528}, 0);
    mobj_t *gas_worker = sc_spawn_actor(MT_SCV - 1, (ivec2_t){336, 528}, 0);
    mobj_t *travel_worker = spawn(MT_SCV - 1, 0);
    CHECK(gas_building && gas_worker && travel_worker && sc_add_resource_vent(gas_building, 5000));
    P_SyncBuildingBlocking();
    CHECK(P_HarvestUnitTo(&level, gas_worker, level.resource_vents[0].attachment));
    for (int i = 0; i < 1200 && gas_worker->harvest.phase != HARVEST_PHASE_MINING; i++) P_Ticker();
    CHECK(gas_worker->harvest.phase == HARVEST_PHASE_MINING && level.resource_vents[0].occupant == gas_worker->id);
    travel_worker->harvest.target = 0;
    travel_worker->harvest.phase = HARVEST_PHASE_TO_MINE;
    ticks(1, &(hudtext_t){0});
    CHECK(gas_building->owner == 1 && gas_worker->owner == 1 && travel_worker->owner == 0);
    CHECK(gas_worker->harvest.phase == HARVEST_PHASE_NONE && gas_worker->harvest.target == -1);
    CHECK(!level.resource_vents[0].occupant && level.resource_vents[0].source_id == gas_building->id);
    unload(); free(give_trigger); remove("/private/tmp/sc-give.chk");
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

    /* A remote player's outcome is applied after its remaining actions;
     * it must not stop local simulation or depend on consoleplayer. */
    uint8_t result_owners[12] = {6, 5};
    trig = blank_trigger(); CHECK(trig); trig[15] = 22;
    trig[2372 + 17] = 0; trig[2372 + 1] = 1;
    trig[320 + 26] = 2;
    uint8_t *after_defeat = trig + 352; after_defeat[26] = 26; after_defeat[27] = 8;
    put_u32(after_defeat + 16, 13); put_u32(after_defeat + 20, 10);
    CHECK(!write_map("/tmp/open-rts-sc-remote-defeat.chk", result_owners, trig));
    consoleplayer = 0;
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-remote-defeat.chk", &level));
    mobj_t *defeated = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){144, 144}, 1); CHECK(defeated);
    mobj_t *lost_hatch = sc_spawn_actor(MT_HATCHERY - 1, (ivec2_t){320, 320}, 1);
    mobj_t *lost_worker = sc_spawn_actor(MT_SCV - 1, (ivec2_t){208, 144}, 1);
    CHECK(lost_hatch && lost_worker && P_EnsureMobjProduction(lost_worker));
    lost_hatch->sc.larva_ms = 1;
    lost_worker->production->placed = true; lost_worker->production->queue_count = 1;
    lost_worker->production->time_left_ms = 1;
    ticks(1, &(hudtext_t){0});
    sc_mission_t *result_mission = level.mission;
    CHECK(result_mission->player_result[1] == 2 && !result_mission->player_result[0]);
    CHECK(!sc_mission_result() && level.player_resources[1][0] == 10 && defeated->owner == 11);
    CHECK(lost_hatch->owner == 11 && lost_hatch->sc.larva_ms == INT_MIN &&
          lost_worker->owner == 11 && !lost_worker->production);
    uint32_t hatch_id = lost_hatch->id, defeated_id = defeated->id;
    app_t result_app = {0}; AiContext result_ai = {0}; hudtext_t result_hud = {0};
    void *result_save = NULL; size_t result_size = 0;
    CHECK(P_InitSight());
    CHECK(G_SaveGameBlob(&result_save, &result_size, "Defeated hatchery", &result_app, &result_ai, &result_hud));
    CHECK(G_LoadGameBlob(result_save, result_size, &result_app, &result_ai, &result_hud));
    free(result_save);
    result_mission = level.mission;
    lost_hatch = P_MobjById(hatch_id); defeated = P_MobjById(defeated_id);
    CHECK(lost_hatch && defeated && lost_hatch->sc.larva_ms == INT_MIN);
    G_ProductionTicker(RTS_TICK_MS);
    CHECK(lost_hatch->sc.larva_ms == INT_MIN);
    ticks(1, &(hudtext_t){0});
    CHECK(level.player_resources[1][0] == 10);
    uint32_t remote_hash = G_ConsistencyExtra(0);
    unload();
    consoleplayer = 1;
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-remote-defeat.chk", &level));
    consoleplayer = 1;
    defeated = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){144, 144}, 1); CHECK(defeated);
    ticks(2, &(hudtext_t){0});
    result_mission = level.mission;
    CHECK(result_mission->player_result[1] == 2 && defeated->owner == 11 && sc_mission_result() == 2);
    CHECK(G_ConsistencyExtra(0) == remote_hash);
    consoleplayer = 0;
    free(trig); unload(); remove("/tmp/open-rts-sc-remote-defeat.chk");

    /* Transmission's presentation is local; its wait blocks both peers. */
    for (int perspective = 0; perspective < 2; ++perspective) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22;
        trig[2372 + 17] = 0; trig[2372 + 1] = 1;
        trig[320 + 26] = 7; put_u32(trig + 320 + 12, 200);
        uint8_t *after_transmission = trig + 352;
        after_transmission[26] = 26; after_transmission[27] = 8;
        put_u32(after_transmission + 16, 13); put_u32(after_transmission + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-transmission-wait.chk", result_owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-transmission-wait.chk", &level));
        consoleplayer = perspective;
        ticks(1, &(hudtext_t){0});
        result_mission = level.mission;
        CHECK(result_mission->rt[1].wait_ms == 200 && level.player_resources[1][0] == 0);
        ticks(7, &(hudtext_t){0});
        CHECK(level.player_resources[1][0] == 10 && !result_mission->rt[1].wait_ms);
        free(trig); unload();
    }
    consoleplayer = 0; remove("/tmp/open-rts-sc-transmission-wait.chk");

    /* CHK action IDs 54/55 pause and resume the countdown; 56 draws. */
    for (int id = 54; id <= 56; id++) {
        trig = blank_trigger(); CHECK(trig);
        trig[15] = 22;
        trig[320 + 26] = (uint8_t)id;
        CHECK(!write_map("/tmp/open-rts-sc-timer.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-timer.chk", &level));
        sc_mission_t *m = level.mission;
        m->countdown_ms = 10000;
        m->countdown_paused = id == 55;
        ticks(1, &(hudtext_t){0});
        if (id == 54) {
            CHECK(m->countdown_paused && sc_mission_result() == 0);
            int remaining = m->countdown_ms;
            ticks(1, &(hudtext_t){0});
            CHECK(m->countdown_ms == remaining);
        } else if (id == 55) {
            CHECK(!m->countdown_paused && sc_mission_result() == 0);
            ticks(1, &(hudtext_t){0});
            CHECK(m->countdown_ms < 10000);
        } else CHECK(sc_mission_result() == 3);
        free(trig);
        unload();
    }
    /* Non Allied Victory Players includes active neutral and rescue slots. */
    {
        uint8_t non_allied_owners[12] = {6, 7, 3, 7, 8};
        uint8_t *non_allied_trigger = blank_trigger(); CHECK(non_allied_trigger);
        non_allied_trigger[2372 + 17] = 0; non_allied_trigger[2372] = 1;
        put_u32(non_allied_trigger + 4, 26);
        put_u32(non_allied_trigger + 8, 2);
        put_u16(non_allied_trigger + 12, MT_MARINE - 1);
        non_allied_trigger[14] = 10; non_allied_trigger[15] = 5;
        uint8_t *resource_action = non_allied_trigger + 320;
        resource_action[26] = 26; resource_action[27] = 8;
        put_u32(resource_action + 16, 13); put_u32(resource_action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-non-allied-victory.chk", non_allied_owners, non_allied_trigger));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-non-allied-victory.chk", &level));
        sc_mission_t *non_allied_mission = level.mission;
        non_allied_mission->allies[0] |= UINT32_C(0x40000000) >> 3;
        non_allied_mission->allies[3] |= UINT32_C(0x40000000);
        non_allied_mission->kills[1][MT_MARINE - 1] = 1;
        non_allied_mission->kills[2][MT_MARINE - 1] = 1;
        non_allied_mission->kills[3][MT_MARINE - 1] = 99;
        non_allied_mission->kills[4][MT_MARINE - 1] = 99; /* Closed slot must not join group 26. */
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[0][0] == 0); /* A one-way status 2 is not enough. */
        non_allied_mission->neutral_relations[0] &= ~(UINT32_C(0x40000000) >> 3);
        non_allied_mission->neutral_relations[3] &= ~UINT32_C(0x40000000);
        non_allied_mission->allies[3] |= UINT32_C(0x40000000);
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[0][0] == 10); /* Plain mutual Ally excludes p3; closed is excluded. */
        free(non_allied_trigger); unload();
    }
    remove("/tmp/open-rts-sc-non-allied-victory.chk");
    /* Most Kills (condition 8) compares the current owner by victim type. */
    const struct { uint32_t mine, rival, inactive; bool wins; } kill_cases[] = {
        {5, 4, 99, true},  /* sole leader; inactive slot's larger count is ignored */
        {5, 5, 0, true},   /* mapmaker documentation says ties share Most */
        {4, 5, 0, false},
        {0, 0, 99, true},  /* all active counts are zero; inferred max tie */
        {0, 1, 0, false}
    };
    uint8_t kill_owners[12] = {6, 5};
    for (size_t i = 0; i < sizeof(kill_cases) / sizeof(kill_cases[0]); i++) {
        uint8_t *kills_trigger = blank_trigger(); CHECK(kills_trigger);
        kills_trigger[2372 + 17] = 0; kills_trigger[2372 + 1] = 1;
        put_u32(kills_trigger + 4, 0); /* Player field is unused by MostKills. */
        put_u16(kills_trigger + 12, MT_MARINE - 1); kills_trigger[15] = 8;
        uint8_t *resource_action = kills_trigger + 320;
        resource_action[26] = 26; resource_action[27] = 8;
        put_u32(resource_action + 16, 13); put_u32(resource_action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-most-kills.chk", kill_owners, kills_trigger));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-most-kills.chk", &level));
        sc_mission_t *kills_mission = level.mission;
        kills_mission->kills[1][MT_MARINE - 1] = kill_cases[i].mine;
        kills_mission->kills[0][MT_MARINE - 1] = kill_cases[i].rival;
        kills_mission->kills[2][MT_MARINE - 1] = kill_cases[i].inactive;
        ticks(1, &(hudtext_t){0});
        CHECK((level.player_resources[1][0] == 10) == kill_cases[i].wins);
        free(kills_trigger); unload();
    }
    /* Neutral slots are active participants in Most comparisons. */
    {
        uint8_t neutral_owners[12] = {6, 5, 7};
        uint8_t *kills_trigger = blank_trigger(); CHECK(kills_trigger);
        kills_trigger[2372 + 17] = 0; kills_trigger[2372 + 1] = 1;
        put_u16(kills_trigger + 12, MT_MARINE - 1); kills_trigger[15] = 8;
        uint8_t *resource_action = kills_trigger + 320;
        resource_action[26] = 26; resource_action[27] = 8;
        put_u32(resource_action + 16, 13); put_u32(resource_action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-most-kills.chk", neutral_owners, kills_trigger));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-most-kills.chk", &level));
        sc_mission_t *kills_mission = level.mission;
        kills_mission->kills[1][MT_MARINE - 1] = 5;
        kills_mission->kills[0][MT_MARINE - 1] = 4;
        kills_mission->kills[2][MT_MARINE - 1] = 6;
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[1][0] == 0);
        free(kills_trigger); unload();
    }
    /* Any Unit compares the sum of the current owner's per-type kill counters. */
    {
        uint8_t *kills_trigger = blank_trigger(); CHECK(kills_trigger);
        kills_trigger[2372 + 17] = 0; kills_trigger[2372 + 1] = 1;
        put_u16(kills_trigger + 12, 229); kills_trigger[15] = 8;
        uint8_t *resource_action = kills_trigger + 320;
        resource_action[26] = 26; resource_action[27] = 8;
        put_u32(resource_action + 16, 13); put_u32(resource_action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-most-kills.chk", kill_owners, kills_trigger));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-most-kills.chk", &level));
        sc_mission_t *kills_mission = level.mission;
        kills_mission->kills[1][MT_MARINE - 1] = 2;
        kills_mission->kills[1][MT_GHOST - 1] = 3;
        kills_mission->kills[0][MT_MARINE - 1] = 4;
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[1][0] == 10);
        free(kills_trigger); unload();
    }
    remove("/tmp/open-rts-sc-most-kills.chk");
    /* Highest/Lowest Score compare the current player's selected score total. */
    const struct { uint8_t condition, selector; uint32_t mine, rival; bool qualifies; } score_cases[] = {
        {9, 4, 5, 4, true}, {9, 4, 5, 5, true}, {9, 4, 4, 5, false},
        {19, 4, 4, 5, true}, {19, 4, 5, 5, true}, {19, 4, 5, 4, false},
        {9, 7, 0, 0, false}, {19, 7, 0, 0, false}
    };
    for (size_t i = 0; i < sizeof(score_cases) / sizeof(score_cases[0]); i++) {
        uint8_t *score_trigger = blank_trigger(); CHECK(score_trigger);
        score_trigger[2372 + 17] = 0; score_trigger[2372 + 1] = 1;
        score_trigger[15] = score_cases[i].condition;
        score_trigger[16] = score_cases[i].selector;
        uint8_t *score_action = score_trigger + 320;
        score_action[26] = 26; score_action[27] = 8;
        put_u32(score_action + 16, 13); put_u32(score_action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-highest-score.chk", kill_owners, score_trigger));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-highest-score.chk", &level));
        sc_mission_t *score_mission = level.mission;
        score_mission->kills[1][MT_MARINE - 1] = score_cases[i].mine;
        score_mission->kills[0][MT_MARINE - 1] = score_cases[i].rival;
        ticks(1, &(hudtext_t){0});
        CHECK((level.player_resources[1][0] == 10) == score_cases[i].qualifies);
        free(score_trigger); unload();
    }
    remove("/tmp/open-rts-sc-highest-score.chk");
    /* Score condition selector bytes cover every modeled score category. */
    const struct { uint8_t selector, op; uint32_t qty; bool qualifies; } score_type_cases[] = {
        {0, 0, 1100, true}, /* Total */
        {1, 10, 100, true}, /* Units */
        {2, 0, 1, false},   /* Buildings */
        {3, 10, 100, true}, /* Units and Buildings */
        {4, 0, 100, true}, /* Kills */
        {5, 0, 1, false},  /* Razings */
        {6, 10, 100, true},/* Kills and Razings */
        {7, 0, 0, false}   /* Custom is unsupported. */
    };
    for (size_t i = 0; i < sizeof(score_type_cases) / sizeof(score_type_cases[0]); i++) {
        uint8_t *score_trigger = blank_trigger(); CHECK(score_trigger);
        score_trigger[2372 + 17] = 0; score_trigger[2372 + 1] = 1;
        score_trigger[15] = 21; /* Score */
        score_trigger[14] = score_type_cases[i].op;
        put_u32(score_trigger + 4, 1); /* Player 1. */
        score_trigger[16] = score_type_cases[i].selector;
        put_u32(score_trigger + 8, score_type_cases[i].qty);
        uint8_t *score_action = score_trigger + 320;
        score_action[26] = 26; score_action[27] = 8;
        put_u32(score_action + 16, 13); put_u32(score_action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-score-types.chk", kill_owners, score_trigger));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-score-types.chk", &level));
        sc_mission_t *score_mission = level.mission;
        score_mission->kills[1][MT_MARINE - 1] = 1;
        score_mission->kills[0][MT_COMMAND_CENTER - 1] = 1;
        score_mission->gathered[1][0] = 1000;
        ticks(1, &(hudtext_t){0});
        CHECK((level.player_resources[1][0] == 10) == score_type_cases[i].qualifies);
        free(score_trigger); unload();
    }
    remove("/tmp/open-rts-sc-score-types.chk");
    /* Least Kills (condition 18) ranks the current owner's unit kill totals. */
    const struct { uint32_t mine, rival, inactive; bool any, qualifies; } least_kill_cases[] = {
        {2, 4, 0, false, true},   /* inactive slot's lower count is ignored */
        {5, 5, 0, false, true},
        {5, 4, 0, false, false},
        {0, 0, 99, false, true},  /* inferred zero tie */
        {0, 1, 0, false, true},
        {6, 5, 99, true, true}    /* Any Unit totals all types */
    };
    for (size_t i = 0; i < sizeof(least_kill_cases) / sizeof(least_kill_cases[0]); i++) {
        uint8_t *least_kills_trigger = blank_trigger(); CHECK(least_kills_trigger);
        least_kills_trigger[2372 + 17] = 0; least_kills_trigger[2372 + 1] = 1;
        put_u16(least_kills_trigger + 12, least_kill_cases[i].any ? 229 : MT_MARINE - 1);
        least_kills_trigger[15] = 18;
        uint8_t *resource_action = least_kills_trigger + 320;
        resource_action[26] = 26; resource_action[27] = 8;
        put_u32(resource_action + 16, 13); put_u32(resource_action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-least-kills.chk", kill_owners, least_kills_trigger));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-least-kills.chk", &level));
        sc_mission_t *least_mission = level.mission;
        least_mission->kills[1][MT_MARINE - 1] = least_kill_cases[i].mine;
        least_mission->kills[0][MT_MARINE - 1] = least_kill_cases[i].rival;
        least_mission->kills[2][MT_MARINE - 1] = least_kill_cases[i].inactive;
        if (least_kill_cases[i].any) {
            least_mission->kills[1][MT_GHOST - 1] = 0;
            least_mission->kills[0][MT_GHOST - 1] = 2;
        }
        ticks(1, &(hudtext_t){0});
        CHECK((level.player_resources[1][0] == 10) == least_kill_cases[i].qualifies);
        free(least_kills_trigger); unload();
    }
    /* An active neutral slot participates in Least Kills rankings. */
    {
        uint8_t neutral_owners[12] = {6, 5, 7};
        uint8_t *least_kills_trigger = blank_trigger(); CHECK(least_kills_trigger);
        least_kills_trigger[2372 + 17] = 0; least_kills_trigger[2372 + 1] = 1;
        put_u16(least_kills_trigger + 12, MT_MARINE - 1); least_kills_trigger[15] = 18;
        uint8_t *resource_action = least_kills_trigger + 320;
        resource_action[26] = 26; resource_action[27] = 8;
        put_u32(resource_action + 16, 13); put_u32(resource_action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-least-kills.chk", neutral_owners, least_kills_trigger));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-least-kills.chk", &level));
        sc_mission_t *least_mission = level.mission;
        least_mission->kills[1][MT_MARINE - 1] = 5;
        least_mission->kills[0][MT_MARINE - 1] = 6;
        least_mission->kills[2][MT_MARINE - 1] = 2;
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[1][0] == 0);
        free(least_kills_trigger); unload();
    }
    remove("/tmp/open-rts-sc-least-kills.chk");
    /* Most Resources (condition 10) uses the current owner's selected balance. */
    const struct {
        uint8_t selector;
        int mine[2], rival[2], inactive[2];
        bool wins;
    } resource_cases[] = {
        {0, {5, 0}, {4, 0}, {99, 99}, true},  /* Ore; inactive slot ignored */
        {1, {1, 6}, {1, 5}, {99, 99}, true}, /* Gas */
        {2, {2, 3}, {5, 0}, {99, 99}, true}, /* Ore and Gas combined */
        {0, {5, 0}, {5, 0}, {0, 0}, true},   /* tie */
        {2, {0, 0}, {0, 0}, {0, 0}, true},   /* inferred zero tie */
        {1, {0, 4}, {0, 5}, {0, 0}, false}
    };
    uint8_t resource_owners[12] = {6, 5};
    for (size_t i = 0; i < sizeof(resource_cases) / sizeof(resource_cases[0]); i++) {
        uint8_t *resources_trigger = blank_trigger(); CHECK(resources_trigger);
        resources_trigger[2372 + 17] = 0; resources_trigger[2372 + 1] = 1;
        resources_trigger[16] = resource_cases[i].selector;
        resources_trigger[15] = 10;
        uint8_t *resource_action = resources_trigger + 320;
        resource_action[26] = 26; resource_action[27] = 8;
        put_u32(resource_action + 16, 13); put_u32(resource_action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-most-resources.chk", resource_owners, resources_trigger));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-most-resources.chk", &level));
        level.player_resources[1][0] = resource_cases[i].mine[0];
        level.player_resources[1][1] = resource_cases[i].mine[1];
        level.player_resources[0][0] = resource_cases[i].rival[0];
        level.player_resources[0][1] = resource_cases[i].rival[1];
        level.player_resources[2][0] = resource_cases[i].inactive[0];
        level.player_resources[2][1] = resource_cases[i].inactive[1];
        ticks(1, &(hudtext_t){0});
        CHECK((level.player_resources[1][0] == resource_cases[i].mine[0] + (resource_cases[i].wins ? 10 : 0)));
        free(resources_trigger); unload();
    }
    /* Active neutral slots participate in Most Resources comparisons. */
    {
        uint8_t neutral_owners[12] = {6, 5, 7};
        uint8_t *resources_trigger = blank_trigger(); CHECK(resources_trigger);
        resources_trigger[2372 + 17] = 0; resources_trigger[2372 + 1] = 1;
        resources_trigger[16] = 0; resources_trigger[15] = 10;
        uint8_t *resource_action = resources_trigger + 320;
        resource_action[26] = 26; resource_action[27] = 8;
        put_u32(resource_action + 16, 13); put_u32(resource_action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-most-resources.chk", neutral_owners, resources_trigger));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-most-resources.chk", &level));
        level.player_resources[1][0] = 5;
        level.player_resources[0][0] = 4;
        level.player_resources[2][0] = 6;
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[1][0] == 5);
        free(resources_trigger); unload();
    }
    /* Ore plus Gas must keep totals wider than the individual int balances. */
    {
        uint8_t *resources_trigger = blank_trigger(); CHECK(resources_trigger);
        resources_trigger[2372 + 17] = 0; resources_trigger[2372 + 1] = 1;
        resources_trigger[16] = 2; resources_trigger[15] = 10;
        uint8_t *resource_action = resources_trigger + 320;
        resource_action[24] = 0; resource_action[26] = 26; resource_action[27] = 9;
        put_u32(resource_action + 16, 13); put_u32(resource_action + 20, 1);
        CHECK(!write_map("/tmp/open-rts-sc-most-resources.chk", resource_owners, resources_trigger));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-most-resources.chk", &level));
        level.player_resources[1][0] = 1200000000;
        level.player_resources[1][1] = 1000000000;
        level.player_resources[0][0] = 1000000000;
        level.player_resources[0][1] = 1000000000;
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[1][0] == 1199999999);
        free(resources_trigger); unload();
    }
    remove("/tmp/open-rts-sc-most-resources.chk");
    /* Least Resources (condition 20) uses the same Ore/Gas selector. */
    const struct {
        uint8_t selector;
        int mine[2], rival[2];
        bool qualifies;
    } least_resource_cases[] = {
        {0, {4, 0}, {5, 0}, true},
        {1, {0, 5}, {0, 4}, false},
        {2, {2, 3}, {5, 1}, true},
        {0, {5, 0}, {5, 0}, true},  /* inferred tied least */
        {2, {0, 0}, {0, 0}, true}   /* inferred zero tie */
    };
    for (size_t i = 0; i < sizeof(least_resource_cases) / sizeof(least_resource_cases[0]); i++) {
        uint8_t *resources_trigger = blank_trigger(); CHECK(resources_trigger);
        resources_trigger[2372 + 17] = 0; resources_trigger[2372 + 1] = 1;
        resources_trigger[16] = least_resource_cases[i].selector;
        resources_trigger[15] = 20;
        uint8_t *resource_action = resources_trigger + 320;
        resource_action[26] = 26; resource_action[27] = 8;
        put_u32(resource_action + 16, 13); put_u32(resource_action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-least-resources.chk", resource_owners, resources_trigger));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-least-resources.chk", &level));
        level.player_resources[1][0] = least_resource_cases[i].mine[0];
        level.player_resources[1][1] = least_resource_cases[i].mine[1];
        level.player_resources[0][0] = least_resource_cases[i].rival[0];
        level.player_resources[0][1] = least_resource_cases[i].rival[1];
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[1][0] == least_resource_cases[i].mine[0] +
              (least_resource_cases[i].qualifies ? 10 : 0));
        free(resources_trigger); unload();
    }
    /* Active neutral slots also affect Least Resources rankings. */
    {
        uint8_t neutral_owners[12] = {6, 5, 7};
        uint8_t *resources_trigger = blank_trigger(); CHECK(resources_trigger);
        resources_trigger[2372 + 17] = 0; resources_trigger[2372 + 1] = 1;
        resources_trigger[16] = 0; resources_trigger[15] = 20;
        uint8_t *resource_action = resources_trigger + 320;
        resource_action[26] = 26; resource_action[27] = 8;
        put_u32(resource_action + 16, 13); put_u32(resource_action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-least-resources.chk", neutral_owners, resources_trigger));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-least-resources.chk", &level));
        level.player_resources[1][0] = 5;
        level.player_resources[0][0] = 6;
        level.player_resources[2][0] = 2;
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[1][0] == 5);
        free(resources_trigger); unload();
    }
    remove("/tmp/open-rts-sc-least-resources.chk");
    /* Timer quantities are unsigned seconds; large values must not overflow ms. */
    for (int op = 7; op <= 9; op++) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22;
        uint8_t *timer_action = trig + 320;
        put_u32(timer_action + 12, UINT32_C(0x80000000));
        timer_action[26] = 14; timer_action[27] = (uint8_t)op;
        CHECK(!write_map("/tmp/open-rts-sc-timer.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-timer.chk", &level));
        sc_mission_t *m = level.mission;
        m->countdown_ms = 10000; m->countdown_paused = true;
        ticks(1, &(hudtext_t){0});
        CHECK(m->countdown_ms == (op == 9 ? 0 : INT_MAX));
        free(trig); unload();
    }
    remove("/tmp/open-rts-sc-timer.chk");

    /* Waits contend per player, but a second trigger runs up to its Wait. */
    trig = blank_trigger(); CHECK(trig); trig[15] = 22;
    uint8_t *wait_action = trig + 320;
    put_u32(wait_action + 12, 100); wait_action[26] = 4;
    wait_action += 32; wait_action[26] = 26; wait_action[27] = 8; put_u32(wait_action + 20, 1);
    CHECK(!write_map("/tmp/open-rts-sc-wait.chk", owners, trig));
    uint8_t *second_trigger = blank_trigger(); CHECK(second_trigger); second_trigger[15] = 22;
    wait_action = second_trigger + 320; wait_action[26] = 26; wait_action[27] = 8; put_u32(wait_action + 20, 10);
    wait_action += 32; wait_action[26] = 4; put_u32(wait_action + 12, 100);
    wait_action += 32; wait_action[26] = 26; wait_action[27] = 8; put_u32(wait_action + 20, 100);
    FILE *wait_file = fopen("/tmp/open-rts-sc-wait.chk", "r+b"); CHECK(wait_file);
    CHECK(!fseek(wait_file, -2404, SEEK_END)); uint8_t wait_size[4]; put_u32(wait_size, 4800);
    CHECK(fwrite(wait_size, 1, 4, wait_file) == 4 && !fseek(wait_file, 0, SEEK_END));
    CHECK(fwrite(second_trigger, 1, 2400, wait_file) == 2400 && !fclose(wait_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-wait.chk", &level));
    ticks(1, &(hudtext_t){0}); CHECK(level.player_resources[0][0] == 10);
    ticks(1, &(hudtext_t){0}); CHECK(level.player_resources[0][0] == 10);
    for (int i = 0; i < 10 && level.player_resources[0][0] == 10; i++) ticks(1, &(hudtext_t){0});
    CHECK(level.player_resources[0][0] == 11);
    for (int i = 0; i < 10 && level.player_resources[0][0] == 11; i++) ticks(1, &(hudtext_t){0});
    CHECK(level.player_resources[0][0] == 111);
    unload();
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-wait.chk", &level));
    sc_mission_t *wait_mission = level.mission;
    wait_mission->rt[8] = (sc_trig_t){.cursor = 2, .wait_ms = 1, .running = true};
    ticks(1, &(hudtext_t){0});
    CHECK(level.player_resources[0][0] == 0 && wait_mission->rt[8].wait_ms == -1);
    size_t wait_save_size = G_SaveExtraSize(); void *wait_save = malloc(wait_save_size); CHECK(wait_save);
    G_SaveExtra(wait_save); wait_mission->rt[8].wait_ms = 0;
    CHECK(G_LoadExtra(wait_save, wait_save_size) && wait_mission->rt[8].wait_ms == -1);
    free(wait_save);
    for (int i = 0; i < 10 && level.player_resources[0][0] == 0; i++) ticks(1, &(hudtext_t){0});
    CHECK(level.player_resources[0][0] == 101);
    free(second_trigger); free(trig); unload(); remove("/tmp/open-rts-sc-wait.chk");

    uint8_t *action;
    /* Death counters and trigger quantities are unsigned 32-bit values. */
    for (int operation = 7; operation <= 9; operation++) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
        put_u16(action + 24, MT_MARINE - 1); action[26] = 45; action[27] = (uint8_t)operation;
        put_u32(action + 20, operation == 7 ? 0x80000000u : operation == 8 ? 1 : 3);
        CHECK(!write_map("/tmp/open-rts-sc-deaths.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-deaths.chk", &level));
        sc_mission_t *deaths_mission = level.mission;
        deaths_mission->deaths[0][MT_MARINE - 1] = operation == 8 ? UINT32_MAX : 2;
        ticks(1, &(hudtext_t){0});
        CHECK(deaths_mission->deaths[0][MT_MARINE - 1] == (operation == 7 ? UINT32_C(0x80000000) : 0));
        free(trig); unload();
    }
    trig = blank_trigger(); CHECK(trig);
    put_u32(trig + 8, UINT32_C(0x80000000)); put_u16(trig + 12, MT_MARINE - 1);
    trig[14] = 10; trig[15] = 15;
    action = trig + 320; action[26] = 26; action[27] = 8; put_u32(action + 20, 10);
    CHECK(!write_map("/tmp/open-rts-sc-deaths.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-deaths.chk", &level));
    ((sc_mission_t *)level.mission)->deaths[0][MT_MARINE - 1] = UINT32_C(0x80000000);
    ticks(1, &(hudtext_t){0});
    CHECK(level.player_resources[0][0] == 10);
    free(trig); unload(); remove("/tmp/open-rts-sc-deaths.chk");

    /* Aggregate resource comparisons do not overflow signed int. */
    trig = blank_trigger(); CHECK(trig); trig[2372+17] = 0; trig[2372] = 1;
    put_u32(trig + 4, 17); put_u32(trig + 8, UINT32_C(3000000000));
    trig[14] = 10; trig[15] = 4;
    action = trig + 320; action[26] = 26; action[27] = 8; put_u32(action + 20, 10);
    CHECK(!write_map("/tmp/open-rts-sc-accumulate.chk", force_owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-accumulate.chk", &level));
    level.player_resources[0][0] = level.player_resources[1][0] = 1500000000;
    ticks(1, &(hudtext_t){0});
    CHECK(level.player_resources[0][0] == 1500000010);
    free(trig); unload(); remove("/tmp/open-rts-sc-accumulate.chk");

    /* Resource actions interpret the quantity as unsigned and clamp to storage. */
    for (int op = 7; op <= 9; op++) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
        action[26] = 26; action[27] = (uint8_t)op; put_u32(action + 20, UINT32_C(0x80000000));
        CHECK(!write_map("/tmp/open-rts-sc-resource-limit.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-resource-limit.chk", &level));
        if (op == 7) CHECK(level.player_resources[0][0] == INT_MAX);
        sc_mission_t *resource_mission = level.mission;
        resource_mission->stock_seen = true;
        resource_mission->gathered[0][0] = INT_MAX - 1;
        level.player_resources[0][0] = 100;
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[0][0] == (op == 9 ? 0 : INT_MAX));
        CHECK(resource_mission->gathered[0][0] == INT_MAX);
        free(trig); unload();
    }
    remove("/tmp/open-rts-sc-resource-limit.chk");

    /* Create Unit stores its count in the action's byte at offset 27. */
    for (int create_id = 11; create_id <= 44; create_id += 33) for (int count = 0; count <= 3; count += 3) {
        trig = blank_trigger(); CHECK(trig);
        trig[15] = 22;
        action = trig + 320;
        put_u32(action, 64);
        put_u16(action + 24, MT_MARINE - 1);
        action[26] = (uint8_t)create_id; action[27] = (uint8_t)count;
        CHECK(!write_map("/tmp/open-rts-sc-create.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-create.chk", &level));
        ticks(1, &(hudtext_t){0});
        mobjlist_t list = P_ListMobjs();
        int created = 0;
        for (int i = 0; i < list.count; i++)
            if (list.items[i]->type_id == MT_MARINE && list.items[i]->owner == 0) created++;
        CHECK(created == count);
        P_FreeMobjList(&list);
        free(trig); unload();
    }
    remove("/tmp/open-rts-sc-create.chk");

    /* Create Unit uses modified Anywhere bounds, while location 0 stays global. */
    for (int create_id = 11; create_id <= 44; create_id += 33) for (int loc_case = 0; loc_case < 2; loc_case++) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
        put_u32(action, loc_case ? 64 : 0); put_u16(action + 24, MT_MARINE - 1);
        action[26] = (uint8_t)create_id; action[27] = 1;
        CHECK(!write_map("/private/tmp/sc-create-location.chk", owners, trig));
        {
            FILE *create_location_file = fopen("/private/tmp/sc-create-location.chk", "ab"); CHECK(create_location_file);
            uint8_t create_header[8] = {'M','R','G','N'}, create_locations[64 * 20] = {0};
            put_u32(create_header + 4, sizeof(create_locations));
            put_u32(create_locations + 63 * 20, 128); put_u32(create_locations + 63 * 20 + 4, 128);
            put_u32(create_locations + 63 * 20 + 8, 160); put_u32(create_locations + 63 * 20 + 12, 160);
            CHECK(fwrite(create_header, 1, sizeof(create_header), create_location_file) == sizeof(create_header));
            CHECK(fwrite(create_locations, 1, sizeof(create_locations), create_location_file) == sizeof(create_locations));
            CHECK(!fclose(create_location_file));
        }
        CHECK(G_DoLoadLevel("/private/tmp/sc-create-location.chk", &level));
        ticks(1, &(hudtext_t){0});
        mobjlist_t created_list = P_ListMobjs();
        mobj_t *created_marine = NULL;
        for (int i = 0; i < created_list.count; i++)
            if (created_list.items[i]->type_id == MT_MARINE && created_list.items[i]->owner == 0)
                created_marine = created_list.items[i];
        CHECK(created_marine);
        CHECK(fixed2_near(fixed3_xy(created_marine->core.position),
                          loc_case ? FIXED2_LIT(4.5, 4.5) : FIXED2_LIT(16, 16), FIXED_LIT(0.01)));
        P_FreeMobjList(&created_list);
        free(trig); unload();
    }
    remove("/private/tmp/sc-create-location.chk");

    /* UNIT hangar property applies only when valid, including neutral Carriers. */
    CHECK(!write_map("/tmp/open-rts-sc-placed-hangar.chk", owners, NULL));
    FILE *placed_file = fopen("/tmp/open-rts-sc-placed-hangar.chk", "ab"); CHECK(placed_file);
    uint8_t unit_header[8] = {'U','N','I','T'}, placed[4 * 36] = {0};
    put_u32(unit_header + 4, sizeof(placed));
    const int placed_types[] = {MT_CARRIER, MT_REAVER, MT_GANTRITHOR, MT_WARBRINGER};
    for (int i = 0; i < 4; i++) {
        uint8_t *entry = placed + i * 36;
        put_u16(entry + 4, 128 + i * 64); put_u16(entry + 6, 128);
        put_u16(entry + 8, placed_types[i] - 1); put_u16(entry + 14, i == 3 ? 0 : 0x20);
        entry[16] = i == 0 ? 11 : 0; put_u16(entry + 24, i == 2 ? 65535 : i + 3);
    }
    CHECK(fwrite(unit_header, 1, sizeof(unit_header), placed_file) == sizeof(unit_header));
    CHECK(fwrite(placed, 1, sizeof(placed), placed_file) == sizeof(placed)); CHECK(!fclose(placed_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-placed-hangar.chk", &level));
    CHECK(P_LoadThings(level.map_path) == 4);
    mobjlist_t placed_units = P_ListMobjs();
    int placed_seen = 0;
    for (int i = 0; i < placed_units.count; i++) for (int j = 0; j < 4; j++)
        if (placed_units.items[i]->type_id == placed_types[j]) {
            CHECK(placed_units.items[i]->sc.hangar == (j == 2 ? 8 : j == 3 ? 0 : j + 3));
            if (j == 0) CHECK(placed_units.items[i]->owner == 11);
            placed_seen++;
        }
    CHECK(placed_seen == 4); P_FreeMobjList(&placed_units); unload();
    remove("/tmp/open-rts-sc-placed-hangar.chk");

    /* UNIT shields and energy use independent validity bits and native rounding. */
    CHECK(!write_map("/tmp/open-rts-sc-placed-vitals.chk", owners, NULL));
    placed_file = fopen("/tmp/open-rts-sc-placed-vitals.chk", "ab"); CHECK(placed_file);
    uint8_t vital_units[16 * 36] = {0}; put_u32(unit_header + 4, sizeof(vital_units));
    for (int i = 0; i < 16; i++) {
        uint8_t *entry = vital_units + i * 36;
        put_u16(entry + 4, 128 + i * 48); put_u16(entry + 6, 128);
        put_u16(entry + 8, (i < 3 ? MT_ZEALOT : i < 6 ? MT_GHOST : i == 7 ? MT_GANTRITHOR : MT_MARINE) - 1);
        put_u16(entry + 14, i == 2 || i == 5 || i >= 11 ? 0 : i < 3 ? 4 : i < 8 ? 8 : 2);
        if (i >= 12) { put_u16(entry + 12, (i & 1) ? 0x10 : 0); put_u16(entry + 26, (i & 2) ? 0x10 : 0); }
        entry[17] = i == 8 ? 1 : i == 9 ? 0 : 200;
        entry[18] = i == 0 ? 1 : 150; entry[19] = i == 3 ? 33 : i >= 6 ? 50 : 150;
    }
    CHECK(fwrite(unit_header, 1, sizeof(unit_header), placed_file) == sizeof(unit_header));
    CHECK(fwrite(vital_units, 1, sizeof(vital_units), placed_file) == sizeof(vital_units)); CHECK(!fclose(placed_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-placed-vitals.chk", &level));
    level.upgrades[21][0].weapon = 1; /* Moebius Reactor */
    CHECK(P_LoadThings(level.map_path) == 16);
    placed_units = P_ListMobjs(); placed_seen = 0;
    for (int i = 0; i < placed_units.count; i++) {
        mobj_t *placed_unit = placed_units.items[i];
        int index = ((placed_unit->core.position.x >> 11) - 128) / 48;
        if (index < 0 || index >= 16) continue;
        if (index < 3) CHECK(sc_shields(placed_unit) == (index == 0 ? 0 : sc_units[MT_ZEALOT - 1].shields));
        else if (index == 3) CHECK(placed_unit->sc.energy == 82 * 256 + 128);
        else if (index < 6) CHECK(sc_energy(placed_unit) == (index == 4 ? 250 : 50));
        else if (index < 8) CHECK(placed_unit->sc.energy == (index == 6 ? 100 : 125) * 256);
        else if (index == 8) CHECK(placed_unit->hp == 1 && placed_unit->sc.wound == 154);
        else if (index == 9) CHECK(placed_unit->hp == 1 && placed_unit->sc.wound == 255);
        else if (index < 12) CHECK(placed_unit->hp == placed_unit->max_hp && !placed_unit->sc.wound);
        else {
            CHECK(!!(placed_unit->sc.flags & SC_INVINCIBLE) == (index == 15));
            CHECK(!!sc_hit(0, NULL, placed_unit, 10, 1) == (index != 15));
        }
        placed_seen++;
    }
    CHECK(placed_seen == 16); P_FreeMobjList(&placed_units); unload();
    remove("/tmp/open-rts-sc-placed-vitals.chk");

    /* UNIT hallucination status requires both bits and starts its lifetime. */
    CHECK(!write_map("/tmp/open-rts-sc-placed-illusion.chk", owners, NULL));
    placed_file = fopen("/tmp/open-rts-sc-placed-illusion.chk", "ab"); CHECK(placed_file);
    uint8_t illusions[7 * 36] = {0}; put_u32(unit_header + 4, sizeof(illusions));
    for (int i = 0; i < 7; i++) {
        uint8_t *entry = illusions + i * 36;
        put_u16(entry + 4, 128 + i * 64); put_u16(entry + 6, 128);
        put_u16(entry + 8, (i < 4 ? MT_MARINE : i == 4 ? MT_CARRIER : i == 5 ? MT_REAVER : 5) - 1); /* Goliath turret: row 4. */
        put_u16(entry + 12, (i >= 4 || (i & 1)) ? 8 : 0);
        put_u16(entry + 26, (i >= 4 || (i & 2)) ? 8 : 0);
        if (i == 4 || i == 5) { put_u16(entry + 14, 0x20); put_u16(entry + 24, 3); }
    }
    CHECK(fwrite(unit_header, 1, sizeof(unit_header), placed_file) == sizeof(unit_header));
    CHECK(fwrite(illusions, 1, sizeof(illusions), placed_file) == sizeof(illusions)); CHECK(!fclose(placed_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-placed-illusion.chk", &level));
    CHECK(P_LoadThings(level.map_path) == 7);
    placed_units = P_ListMobjs(); placed_seen = 0;
    for (int i = 0; i < placed_units.count; i++) {
        mobj_t *illusion = placed_units.items[i];
        int index = ((illusion->core.position.x >> 11) - 128) / 64;
        if (index < 0 || index >= 7) continue;
        bool active = index >= 3;
        CHECK(!!(illusion->sc.flags & SC_HALLUCINATION) == active);
        CHECK(illusion->sc.timers[SC_TIMER_LIFE] == (active && index != 6 ? 1350 : 0));
        if (index == 4) CHECK(illusion->sc.hangar == 0);
        if (index == 5) CHECK(illusion->sc.hangar == 3);
        if (index == 3) { sc_spell_ticker(illusion, 1350); CHECK(illusion->remove); }
        placed_seen++;
    }
    CHECK(placed_seen == 7); P_FreeMobjList(&placed_units); unload();
    remove("/tmp/open-rts-sc-placed-illusion.chk");

    /* Preplaced Broodlings age out; hallucination keeps its shorter lifetime. */
    CHECK(!write_map("/tmp/open-rts-sc-placed-brood.chk", owners, NULL));
    placed_file = fopen("/tmp/open-rts-sc-placed-brood.chk", "ab"); CHECK(placed_file);
    uint8_t brood_units[2 * 36] = {0}; put_u32(unit_header + 4, sizeof(brood_units));
    for (int i = 0; i < 2; i++) {
        uint8_t *entry = brood_units + i * 36;
        put_u16(entry + 4, 128 + i * 64); put_u16(entry + 6, 128);
        put_u16(entry + 8, MT_BROODLING - 1);
        if (i) { put_u16(entry + 12, 8); put_u16(entry + 26, 8); }
    }
    CHECK(fwrite(unit_header, 1, sizeof(unit_header), placed_file) == sizeof(unit_header));
    CHECK(fwrite(brood_units, 1, sizeof(brood_units), placed_file) == sizeof(brood_units)); CHECK(!fclose(placed_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-placed-brood.chk", &level)); CHECK(P_LoadThings(level.map_path) == 2);
    placed_units = P_ListMobjs(); CHECK(placed_units.count == 2);
    mobj_t *brood = placed_units.items[0], *illusion_brood = placed_units.items[1];
    CHECK(brood->sc.timers[SC_TIMER_LIFE] == 1800 && illusion_brood->sc.timers[SC_TIMER_LIFE] == 1350);
    sc_spell_ticker(brood, 1799); CHECK(brood->hp > 0 && brood->sc.timers[SC_TIMER_LIFE] == 1);
    sc_spell_ticker(brood, 1); CHECK(brood->hp <= 0 || brood->remove);
    sc_spell_ticker(illusion_brood, 1350); CHECK(illusion_brood->remove);
    P_FreeMobjList(&placed_units); unload(); remove("/tmp/open-rts-sc-placed-brood.chk");

    /* UNIT resource amounts are used only when the corresponding bit is valid. */
    CHECK(!write_map("/tmp/open-rts-sc-placed-resource.chk", owners, NULL));
    placed_file = fopen("/tmp/open-rts-sc-placed-resource.chk", "ab"); CHECK(placed_file);
    uint8_t resource_units[6 * 36] = {0}; put_u32(unit_header + 4, sizeof(resource_units));
    const int resource_types[] = {MT_MINERAL_FIELD1, MT_VESPENE_GEYSER, MT_REFINERY};
    for (int i = 0; i < 6; i++) {
        uint8_t *entry = resource_units + i * 36;
        put_u16(entry + 4, 128 + i * 64); put_u16(entry + 6, 128);
        put_u16(entry + 8, resource_types[i / 2] - 1);
        put_u16(entry + 14, i & 1 ? 0x10 : 0); entry[16] = i < 4 ? 11 : 0;
        put_u32(entry + 20, i == 5 ? UINT32_MAX : 24);
    }
    CHECK(fwrite(unit_header, 1, sizeof(unit_header), placed_file) == sizeof(unit_header));
    CHECK(fwrite(resource_units, 1, sizeof(resource_units), placed_file) == sizeof(resource_units)); CHECK(!fclose(placed_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-placed-resource.chk", &level));
    CHECK(P_LoadThings(level.map_path) == 6 && level.resource_vent_count == 6);
    for (int i = 0; i < 6; i++) {
        resourcevent_t *vent = &level.resource_vents[i];
        CHECK(vent->amount == (!(i & 1) ? 0 : i == 5 ? INT_MAX : 24));
        CHECK(P_MobjById(vent->source_id)->type_id == resource_types[i / 2]);
        if (i == 0) CHECK(vent->active && P_VentYields(vent));
        if (i == 1) CHECK(vent->active);
        if (i == 2) CHECK(!vent->active && vent->depleted && vent->rate == 2);
        if (i == 4) CHECK(vent->active && vent->depleted && vent->rate == 2);
    }
    mobj_t *placed_base = sc_spawn_actor(MT_COMMAND_CENTER - 1, (ivec2_t){192, 528}, 0);
    mobj_t *placed_worker = sc_spawn_actor(MT_SCV - 1, (ivec2_t){336, 528}, 0);
    CHECK(placed_base && placed_worker); P_SyncBuildingBlocking();
    CHECK(P_HarvestUnitTo(&level, placed_worker, level.resource_vents[1].attachment));
    for (int i = 0; i < 2400 && level.player_resources[0][0] < 8; i++) {
        P_Ticker(); G_ProductionTicker(RTS_TICK_MS);
    }
    CHECK(level.player_resources[0][0] >= 8 && level.resource_vents[1].amount < 24);
    unload();
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-placed-resource.chk", &level));
    CHECK(P_LoadThings(level.map_path) == 6 && level.resource_vent_count == 6);
    placed_base = sc_spawn_actor(MT_COMMAND_CENTER - 1, (ivec2_t){192, 528}, 0);
    placed_worker = sc_spawn_actor(MT_SCV - 1, (ivec2_t){336, 528}, 0);
    CHECK(placed_base && placed_worker); P_SyncBuildingBlocking();
    uint32_t empty_patch_id = level.resource_vents[0].source_id;
    CHECK(P_HarvestUnitTo(&level, placed_worker, level.resource_vents[0].attachment));
    for (int i = 0; i < 2400 && P_MobjById(empty_patch_id); i++) {
        P_Ticker(); G_ProductionTicker(RTS_TICK_MS);
    }
    CHECK(!P_MobjById(empty_patch_id) && !level.resource_vents[0].active && level.player_resources[0][0] == 0);
    unload(); remove("/tmp/open-rts-sc-placed-resource.chk");

    /* Action 11 has a one-based UPRP slot and a separate count byte. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action, 64); put_u32(action + 20, 1);
    put_u16(action + 24, MT_MARINE - 1); action[26] = 11; action[27] = 3; action[28] = 28;
    CHECK(!write_map("/tmp/open-rts-sc-cuwp.chk", owners, trig));
    FILE *cuwp_file = fopen("/tmp/open-rts-sc-cuwp.chk", "ab"); CHECK(cuwp_file);
    uint8_t cuwp_header[8] = {'U', 'P', 'R', 'P'}, cuwp[20] = {0};
    put_u32(cuwp_header + 4, sizeof(cuwp)); put_u16(cuwp + 2, 2); cuwp[5] = 50;
    CHECK(fwrite(cuwp_header, 1, sizeof(cuwp_header), cuwp_file) == sizeof(cuwp_header));
    CHECK(fwrite(cuwp, 1, sizeof(cuwp), cuwp_file) == sizeof(cuwp));
    CHECK(!fclose(cuwp_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-cuwp.chk", &level));
    ticks(1, &(hudtext_t){0});
    mobjlist_t cuwp_units = P_ListMobjs(); int cuwp_count = 0;
    for (int i = 0; i < cuwp_units.count; i++) if (cuwp_units.items[i]->type_id == MT_MARINE) {
        CHECK(cuwp_units.items[i]->hp == cuwp_units.items[i]->max_hp / 2);
        cuwp_count++;
    }
    CHECK(cuwp_count == 3);
    P_FreeMobjList(&cuwp_units); free(trig); unload(); remove("/tmp/open-rts-sc-cuwp.chk");

    for (int hp_percent = 0; hp_percent <= 1; hp_percent++) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
        put_u32(action, 64); put_u32(action + 20, 1); put_u16(action + 24, MT_MARINE - 1);
        action[26] = 11; action[27] = 1; action[28] = 28;
        CHECK(!write_map("/tmp/open-rts-sc-cuwp.chk", owners, trig));
        cuwp_file = fopen("/tmp/open-rts-sc-cuwp.chk", "ab"); CHECK(cuwp_file);
        memset(cuwp, 0, sizeof(cuwp)); put_u16(cuwp + 2, 2); cuwp[5] = (uint8_t)hp_percent;
        CHECK(fwrite(cuwp_header, 1, sizeof(cuwp_header), cuwp_file) == sizeof(cuwp_header));
        CHECK(fwrite(cuwp, 1, sizeof(cuwp), cuwp_file) == sizeof(cuwp)); CHECK(!fclose(cuwp_file));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-cuwp.chk", &level)); ticks(1, &(hudtext_t){0});
        cuwp_units = P_ListMobjs(); mobj_t *created_hp = NULL;
        for (int i = 0; i < cuwp_units.count; i++) if (cuwp_units.items[i]->type_id == MT_MARINE) created_hp = cuwp_units.items[i];
        CHECK(created_hp && created_hp->hp == 1 && created_hp->sc.wound == (hp_percent ? 154 : 255));
        P_FreeMobjList(&cuwp_units); free(trig); unload();
    }
    remove("/tmp/open-rts-sc-cuwp.chk");

    /* CUWP hallucinations share the initialized lifetime. */
    for (int bits = 0; bits < 4; bits++) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
        put_u32(action, 64); put_u32(action + 20, 1); put_u16(action + 24, MT_MARINE - 1);
        action[26] = 11; action[27] = 1; action[28] = 28;
        CHECK(!write_map("/tmp/open-rts-sc-cuwp.chk", owners, trig));
        cuwp_file = fopen("/tmp/open-rts-sc-cuwp.chk", "ab"); CHECK(cuwp_file);
        memset(cuwp, 0, sizeof(cuwp)); put_u16(cuwp, bits & 1 ? 8 : 0); put_u16(cuwp + 14, bits & 2 ? 8 : 0);
        CHECK(fwrite(cuwp_header, 1, sizeof(cuwp_header), cuwp_file) == sizeof(cuwp_header));
        CHECK(fwrite(cuwp, 1, sizeof(cuwp), cuwp_file) == sizeof(cuwp)); CHECK(!fclose(cuwp_file));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-cuwp.chk", &level)); ticks(1, &(hudtext_t){0});
        cuwp_units = P_ListMobjs(); mobj_t *created_illusion = NULL;
        for (int i = 0; i < cuwp_units.count; i++) if (cuwp_units.items[i]->type_id == MT_MARINE) created_illusion = cuwp_units.items[i];
        CHECK(created_illusion && !!(created_illusion->sc.flags & SC_HALLUCINATION) == (bits == 3));
        CHECK(created_illusion->sc.timers[SC_TIMER_LIFE] == (bits == 3 ? 1350 : 0));
        P_FreeMobjList(&cuwp_units); free(trig); unload();
    }
    remove("/tmp/open-rts-sc-cuwp.chk");

    /* CUWP shares the hangar-valid bit and stores the count at +12. */
    for (int i = 0; i < 4; i++) for (int valid = 0; valid < 2; valid++) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
        put_u32(action, 64); put_u32(action + 20, 1); put_u16(action + 24, placed_types[i] - 1);
        action[26] = 11; action[27] = 1; action[28] = 28;
        CHECK(!write_map("/tmp/open-rts-sc-cuwp.chk", owners, trig));
        cuwp_file = fopen("/tmp/open-rts-sc-cuwp.chk", "ab"); CHECK(cuwp_file);
        memset(cuwp, 0, sizeof(cuwp)); put_u16(cuwp + 2, valid ? 0x20 : 0); put_u16(cuwp + 12, 65535);
        CHECK(fwrite(cuwp_header, 1, sizeof(cuwp_header), cuwp_file) == sizeof(cuwp_header));
        CHECK(fwrite(cuwp, 1, sizeof(cuwp), cuwp_file) == sizeof(cuwp)); CHECK(!fclose(cuwp_file));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-cuwp.chk", &level)); ticks(1, &(hudtext_t){0});
        cuwp_units = P_ListMobjs(); mobj_t *created_hangar = NULL;
        for (int j = 0; j < cuwp_units.count; j++)
            if (cuwp_units.items[j]->type_id == placed_types[i]) created_hangar = cuwp_units.items[j];
        CHECK(created_hangar && created_hangar->sc.hangar == (valid ? sc_hangar_capacity(created_hangar) : 0));
        P_FreeMobjList(&cuwp_units); free(trig); unload();
    }
    remove("/tmp/open-rts-sc-cuwp.chk");

    /* Shields and energy use their own UPRP bytes and preserve lazy defaults. */
    for (int field = 0; field < 5; field++) {
        trig = blank_trigger(); CHECK(trig);
        trig[15] = 22; action = trig + 320;
        put_u32(action, 64); put_u32(action + 20, 1);
        bool energy_field = field == 1 || field == 3 || field == 4;
        int type = field == 4 ? MT_MARINE : energy_field ? MT_GHOST : MT_ZEALOT;
        put_u16(action + 24, type - 1); action[26] = 11; action[27] = 1; action[28] = 28;
        CHECK(!write_map("/tmp/open-rts-sc-cuwp.chk", owners, trig));
        cuwp_file = fopen("/tmp/open-rts-sc-cuwp.chk", "ab"); CHECK(cuwp_file);
        memset(cuwp, 0, sizeof(cuwp)); put_u16(cuwp + 2, energy_field ? 8 : 4);
        cuwp[energy_field ? 7 : 6] = field == 2 ? 1 : field == 3 ? 33 : field == 4 ? 50 : 75;
        CHECK(fwrite(cuwp_header, 1, sizeof(cuwp_header), cuwp_file) == sizeof(cuwp_header));
        CHECK(fwrite(cuwp, 1, sizeof(cuwp), cuwp_file) == sizeof(cuwp)); CHECK(!fclose(cuwp_file));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-cuwp.chk", &level));
        if (field == 3) level.upgrades[21][0].weapon = 1; /* Moebius Reactor */
        ticks(1, &(hudtext_t){0});
        cuwp_units = P_ListMobjs(); mobj_t *created = NULL;
        for (int i = 0; i < cuwp_units.count; i++) if (cuwp_units.items[i]->type_id == type) created = cuwp_units.items[i];
        CHECK(created && (created->sc.flags & SC_STARTED));
        if (field == 4) CHECK(created->sc.energy == 100 * 256);
        else if (field == 1) CHECK(sc_energy(created) == 150);
        else if (field == 3) CHECK(created->sc.energy == 82 * 256 + 128);
        else if (field == 2) CHECK(created->sc.shields == 0);
        else CHECK(sc_shields(created) == sc_units[type - 1].shields * 3 / 4);
        P_FreeMobjList(&cuwp_units); free(trig); unload();
    }

    /* Property resource amount initializes a usable trigger-created patch. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action, 64); put_u32(action + 20, 1);
    put_u16(action + 24, MT_MINERAL_FIELD1 - 1); action[26] = 11; action[27] = 1; action[28] = 28;
    CHECK(!write_map("/tmp/open-rts-sc-cuwp.chk", owners, trig));
    cuwp_file = fopen("/tmp/open-rts-sc-cuwp.chk", "ab"); CHECK(cuwp_file);
    memset(cuwp, 0, sizeof(cuwp)); put_u16(cuwp + 2, 16); put_u32(cuwp + 8, 24);
    CHECK(fwrite(cuwp_header, 1, sizeof(cuwp_header), cuwp_file) == sizeof(cuwp_header));
    CHECK(fwrite(cuwp, 1, sizeof(cuwp), cuwp_file) == sizeof(cuwp));
    CHECK(!fclose(cuwp_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-cuwp.chk", &level));
    ticks(1, &(hudtext_t){0});
    CHECK(level.resource_vent_count == 1 && level.resource_vents[0].amount == 24);
    mobj_t *cuwp_patch = P_MobjById(level.resource_vents[0].source_id);
    CHECK(cuwp_patch && cuwp_patch->owner == 11);
    mobj_t *cuwp_base = sc_spawn_actor(MT_COMMAND_CENTER - 1, (ivec2_t){192, 528}, 0);
    mobj_t *cuwp_worker = sc_spawn_actor(MT_SCV - 1, (ivec2_t){336, 528}, 0);
    CHECK(cuwp_base && cuwp_worker); P_SyncBuildingBlocking();
    CHECK(P_HarvestUnitTo(&level, cuwp_worker, level.resource_vents[0].attachment));
    for (int i = 0; i < 2400 && level.player_resources[0][0] < 8; i++) {
        P_Ticker(); G_ProductionTicker(RTS_TICK_MS);
    }
    CHECK(level.player_resources[0][0] >= 8 && level.resource_vents[0].amount < 24);
    unload();
    put_u16(action + 24, MT_REFINERY - 1); put_u32(cuwp + 8, 0);
    CHECK(!write_map("/tmp/open-rts-sc-cuwp.chk", owners, trig));
    cuwp_file = fopen("/tmp/open-rts-sc-cuwp.chk", "ab"); CHECK(cuwp_file);
    CHECK(fwrite(cuwp_header, 1, sizeof(cuwp_header), cuwp_file) == sizeof(cuwp_header));
    CHECK(fwrite(cuwp, 1, sizeof(cuwp), cuwp_file) == sizeof(cuwp));
    CHECK(!fclose(cuwp_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-cuwp.chk", &level));
    mobj_t *trigger_geyser = sc_spawn_actor(MT_VESPENE_GEYSER - 1, (ivec2_t){512, 512}, 11);
    CHECK(trigger_geyser && sc_add_resource_vent(trigger_geyser, 0));
    ticks(1, &(hudtext_t){0});
    CHECK(level.resource_vent_count == 2 && level.resource_vents[0].source_id == trigger_geyser->id &&
          !level.resource_vents[0].amount &&
          !level.resource_vents[0].active && level.resource_vents[1].amount == 0 &&
          level.resource_vents[1].active && level.resource_vents[1].depleted && level.resource_vents[1].rate == 2);
    unload();
    put_u16(action + 24, MT_VESPENE_GEYSER - 1);
    CHECK(!write_map("/tmp/open-rts-sc-cuwp.chk", owners, trig));
    cuwp_file = fopen("/tmp/open-rts-sc-cuwp.chk", "ab"); CHECK(cuwp_file);
    CHECK(fwrite(cuwp_header, 1, sizeof(cuwp_header), cuwp_file) == sizeof(cuwp_header));
    CHECK(fwrite(cuwp, 1, sizeof(cuwp), cuwp_file) == sizeof(cuwp)); CHECK(!fclose(cuwp_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-cuwp.chk", &level));
    ticks(1, &(hudtext_t){0});
    CHECK(level.resource_vent_count == 1 && level.resource_vents[0].depleted && !level.resource_vents[0].active);
    cuwp_base = sc_spawn_actor(MT_COMMAND_CENTER - 1, (ivec2_t){192, 528}, 0);
    cuwp_worker = sc_spawn_actor(MT_SCV - 1, (ivec2_t){336, 528}, 0);
    CHECK(cuwp_base && cuwp_worker); P_SyncBuildingBlocking();
    level.player_resources[0][0] = 500;
    ivec2_t gas_site = level.resource_vents[0].cell;
    ticcmd_t construct_gas = {.order = TC_CONSTRUCT, .count = 1, .units = {cuwp_worker->id},
        .product = MT_REFINERY, .position = {FIXED_FROM_INT(gas_site.x), FIXED_FROM_INT(gas_site.y), 0}};
    G_RunTiccmd(0, &construct_gas);
    CHECK(cuwp_worker->production && cuwp_worker->production->placed);
    for (int i = 0; i < 4000 && cuwp_worker->production; i++) { P_Ticker(); G_ProductionTicker(RTS_TICK_MS); }
    CHECK(!cuwp_worker->production && level.resource_vents[0].active && level.resource_vents[0].depleted);
    CHECK(P_MobjById(level.resource_vents[0].source_id)->type_id == MT_REFINERY);
    CHECK(P_HarvestUnitTo(&level, cuwp_worker, level.resource_vents[0].attachment));
    for (int i = 0; i < 2400 && level.player_resources[0][1] < 2; i++) {
        P_Ticker(); G_ProductionTicker(RTS_TICK_MS);
    }
    CHECK(level.player_resources[0][1] >= 2 && level.resource_vents[0].amount == 0);
    free(trig); unload(); remove("/tmp/open-rts-sc-cuwp.chk");

    /* Create Unit assigns mineral fields and geysers to neutral owner 11. */
    const int deposits[] = {MT_MINERAL_FIELD1, MT_VESPENE_GEYSER, MT_REFINERY, MT_EXTRACTOR, MT_ASSIMILATOR};
    for (int d = 0; d < 5; d++) {
        int type = deposits[d];
        trig = blank_trigger(); CHECK(trig);
        trig[15] = 22; action = trig + 320;
        put_u32(action, 64); put_u16(action + 24, type - 1);
        action[26] = 44; action[27] = 1;
        CHECK(!write_map("/tmp/open-rts-sc-create-resource.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-create-resource.chk", &level));
        if (d >= 2) {
            mobj_t *geyser = sc_spawn_actor(MT_VESPENE_GEYSER - 1, (ivec2_t){512, 512}, 11);
            CHECK(geyser && sc_add_resource_vent(geyser, 5000));
        }
        ticks(1, &(hudtext_t){0});
        mobjlist_t list = P_ListMobjs();
        bool found = false;
        for (int i = 0; i < list.count; i++)
            if (list.items[i]->type_id == type && list.items[i]->owner == (d < 2 ? 11 : 0)) found = true;
        CHECK(found);
        CHECK(level.resource_vent_count == (d >= 2 ? 2 : 1));
        resourcevent_t *created_vent = &level.resource_vents[d >= 2 ? 1 : 0];
        CHECK(created_vent->amount == (type == MT_MINERAL_FIELD1 ? 1500 : 5000));
        CHECK(created_vent->active == (type != MT_VESPENE_GEYSER));
        if (d >= 2) CHECK(level.resource_vents[0].source_id && level.resource_vents[0].amount == 5000);
        P_FreeMobjList(&list);
        if (type == MT_MINERAL_FIELD1) {
            mobj_t *base = sc_spawn_actor(MT_COMMAND_CENTER - 1, (ivec2_t){192, 528}, 0);
            mobj_t *worker = sc_spawn_actor(MT_SCV - 1, (ivec2_t){336, 528}, 0);
            CHECK(base && worker);
            P_SyncBuildingBlocking();
            CHECK(P_HarvestUnitTo(&level, worker, level.resource_vents[0].attachment));
            for (int i = 0; i < 2400 && level.player_resources[0][0] < 8; i++) {
                P_Ticker(); G_ProductionTicker(RTS_TICK_MS);
            }
            CHECK(level.player_resources[0][0] >= 8 && level.resource_vents[0].amount < 1500);
        }
        mobj_t *deposit = P_MobjById(level.resource_vents[0].source_id);
        CHECK(deposit);
        P_RemoveMobj(deposit);
        P_SyncDepositStructures(&level);
        CHECK(!level.resource_vents[0].active && level.resource_vents[0].amount == 0);
        free(trig); unload();
    }
    remove("/tmp/open-rts-sc-create-resource.chk");

    /* Create Unit honors units.dat's unit-listing availability bit. */
    CHECK(sc_units[MT_MARINE - 1].availability == 207);
    CHECK(sc_units[MT_INTERCEPTOR - 1].availability == 196);
    CHECK(sc_units[MT_VESPENE_GEYSER - 1].availability == 2);
    for (int create_action = 11; create_action <= 44; create_action += 33) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
        put_u32(action, 64); put_u16(action + 24, MT_INTERCEPTOR - 1);
        action[26] = (uint8_t)create_action; action[27] = 1;
        const char *path = create_action == 11 ? "/tmp/open-rts-sc-create-unavailable-11.chk" :
                                                 "/tmp/open-rts-sc-create-unavailable-44.chk";
        CHECK(!write_map(path, owners, trig));
        CHECK(G_DoLoadLevel(path, &level));
        ticks(1, &(hudtext_t){0});
        mobjlist_t unavailable_units = P_ListMobjs();
        for (int i = 0; i < unavailable_units.count; i++)
            CHECK(unavailable_units.items[i]->type_id != MT_INTERCEPTOR);
        P_FreeMobjList(&unavailable_units);
        free(trig); unload(); remove(path);
    }

    /* A trigger-created Factory reconnects a neutral Machine Shop at its native offset. */
    trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
    put_u32(action, 1); put_u16(action + 24, MT_FACTORY - 1);
    action[26] = 44; action[27] = 1;
    CHECK(!write_map("/tmp/open-rts-sc-create-addon.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-create-addon.chk", &level));
    sc_mission_t *create_mission = level.mission; create_mission->loc_count = 1;
    ivec2_t create_cell = {10, 10};
    fixed2_t factory_pos = P_BuildingPosition(MT_FACTORY, create_cell);
    create_mission->loc_left[0] = create_mission->loc_right[0] = factory_pos.x >> 11;
    create_mission->loc_top[0] = create_mission->loc_bottom[0] = factory_pos.y >> 11;
    uint16_t create_addon_type; irect_t create_addon_rect;
    CHECK(sc_addon_place(MT_FACTORY, create_cell, &create_addon_type, &create_addon_rect));
    fixed2_t create_addon_pos = P_BuildingPosition(create_addon_type, (ivec2_t){create_addon_rect.x, create_addon_rect.y});
    mobj_t *create_shop = sc_spawn_actor(create_addon_type - 1,
        (ivec2_t){create_addon_pos.x >> 11, create_addon_pos.y >> 11}, 11);
    CHECK(create_shop);
    int create_terrain = L_Index(&level, 1, 1);
    level.cell_solid[create_terrain] |= 1;
    ticks(1, &(hudtext_t){0});
    CHECK(level.cell_solid[create_terrain] & 1);
    mobj_t *create_factory = P_MobjById(create_shop->sc.parent);
    CHECK(create_factory && create_factory->type_id == MT_FACTORY && create_factory->sc.addon == create_shop->id);
    CHECK(create_shop->owner == 0);
    free(trig); unload(); remove("/tmp/open-rts-sc-create-addon.chk");

    /* Trigger-created buildings snap their placement footprint to 32px tiles. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; memset(trig + 2372, 0, 28); trig[2372] = 1;
    action = trig + 320;
    put_u32(action, 64); put_u16(action + 24, MT_SUPPLY_DEPOT - 1);
    action[26] = 44; action[27] = 1;
    action[32 + 26] = 3; /* Preserve and retry the same placement next tic. */
    uint8_t create_owners[12] = {6, 5};
    CHECK(!write_map("/tmp/open-rts-sc-create-snap.chk", create_owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-create-snap.chk", &level));
    sc_mission_t *snap_mission = level.mission;
    snap_mission->loc_count = 64;
    snap_mission->loc_left[63] = snap_mission->loc_top[63] = 48;
    snap_mission->loc_right[63] = snap_mission->loc_bottom[63] = 112;
    ticks(2, &(hudtext_t){0});
    mobjlist_t created_buildings = P_ListMobjs();
    mobj_t *snapped_depot = NULL;
    int depot_count = 0;
    for (int i = 0; i < created_buildings.count; i++)
        if (created_buildings.items[i]->type_id == MT_SUPPLY_DEPOT) { snapped_depot = created_buildings.items[i]; depot_count++; }
    CHECK(snapped_depot && depot_count == 1);
    fixed2_t expected_depot = P_BuildingPosition(MT_SUPPLY_DEPOT, (ivec2_t){1, 1});
    CHECK(fixed2_equal(fixed3_xy(snapped_depot->core.position), expected_depot));
    P_FreeMobjList(&created_buildings);
    free(trig); unload(); remove("/tmp/open-rts-sc-create-snap.chk");

    /* Trigger-created mobile units keep their native collision box in-map. */
    for (int far_edge = 0; far_edge < 2; far_edge++) {
        trig = blank_trigger(); CHECK(trig);
        trig[15] = 22; memset(trig + 2372, 0, 28); trig[2372] = 1;
        action = trig + 320;
        put_u32(action, 64); put_u16(action + 24, MT_MARINE - 1);
        action[26] = far_edge ? 11 : 44; action[27] = 1;
        const char *path = far_edge ? "/tmp/open-rts-sc-create-edge-max.chk" :
                                      "/tmp/open-rts-sc-create-edge-min.chk";
        CHECK(!write_map(path, create_owners, trig));
        CHECK(G_DoLoadLevel(path, &level));
        sc_mission_t *edge_mission = level.mission;
        edge_mission->loc_count = 64;
        int edge = far_edge ? level.width * 32 : 0;
        edge_mission->loc_left[63] = edge_mission->loc_right[63] = edge;
        edge_mission->loc_top[63] = edge_mission->loc_bottom[63] = edge;
        ticks(1, &(hudtext_t){0});
        mobj_t *marine = NULL;
        mobjlist_t edge_units = P_ListMobjs();
        for (int i = 0; i < edge_units.count; i++)
            if (edge_units.items[i]->type_id == MT_MARINE) marine = edge_units.items[i];
        CHECK(marine);
        /* DAT Marine extents are left/top/right/bottom = 8/9/8/10 pixels. */
        CHECK((marine->core.position.x >> 11) == (far_edge ? 1015 : 8) &&
              (marine->core.position.y >> 11) == (far_edge ? 1013 : 9));
        P_FreeMobjList(&edge_units);
        free(trig); unload(); remove(path);
    }

    /* Trigger-created buildings reject blocked and creep-less footprints. */
    for (int zerg = 0; zerg < 2; zerg++) {
        trig = blank_trigger(); CHECK(trig);
        trig[15] = 22; memset(trig + 2372, 0, 28); trig[2372] = 1;
        action = trig + 320;
        put_u32(action, 64); put_u16(action + 24,
            (zerg ? MT_HYDRALISK_DEN : MT_SUPPLY_DEPOT) - 1);
        action[26] = 44; action[27] = 1;
        const char *path = zerg ? "/tmp/open-rts-sc-create-no-creep.chk" : "/tmp/open-rts-sc-create-blocked.chk";
        CHECK(!write_map(path, create_owners, trig));
        CHECK(G_DoLoadLevel(path, &level));
        sc_mission_t *placement_mission = level.mission;
        placement_mission->loc_count = 64;
        placement_mission->loc_left[63] = placement_mission->loc_top[63] = 48;
        placement_mission->loc_right[63] = placement_mission->loc_bottom[63] = 112;
        if (!zerg) level.blocked[L_Index(&level, 1, 1)] = 1;
        ticks(1, &(hudtext_t){0});
        mobjlist_t placed = P_ListMobjs();
        uint16_t building = zerg ? MT_HYDRALISK_DEN : MT_SUPPLY_DEPOT;
        for (int i = 0; i < placed.count; i++) CHECK(placed.items[i]->type_id != building);
        P_FreeMobjList(&placed);
        free(trig); unload(); remove(path);
    }

    /* Trigger-created Protoss buildings require the target player's psi. */
    for (int pylon_owner = 0; pylon_owner < 2; pylon_owner++) {
        trig = blank_trigger(); CHECK(trig);
        trig[15] = 22; memset(trig + 2372, 0, 28); trig[2372] = 1;
        action = trig + 320;
        put_u32(action, 64); put_u16(action + 24, MT_PHOTON_CANNON - 1);
        action[26] = 44; action[27] = 1;
        const char *path = pylon_owner ? "/tmp/open-rts-sc-create-foreign-psi.chk" :
                                         "/tmp/open-rts-sc-create-own-psi.chk";
        CHECK(!write_map(path, create_owners, trig));
        CHECK(G_DoLoadLevel(path, &level));
        sc_mission_t *psi_mission = level.mission;
        psi_mission->loc_count = 64;
        psi_mission->loc_left[63] = psi_mission->loc_right[63] = 256;
        psi_mission->loc_top[63] = psi_mission->loc_bottom[63] = 256;
        CHECK(sc_spawn_actor(MT_PYLON - 1, (ivec2_t){160, 256}, pylon_owner));
        ticks(1, &(hudtext_t){0});
        mobjlist_t powered = P_ListMobjs();
        int cannon_count = 0;
        for (int i = 0; i < powered.count; i++)
            if (powered.items[i]->type_id == MT_PHOTON_CANNON && powered.items[i]->owner == 0) cannon_count++;
        CHECK(cannon_count == (pylon_owner == 0 ? 1 : 0));
        P_FreeMobjList(&powered);
        free(trig); unload(); remove(path);
    }

    /* Both creation actions leave a directly created add-on neutral. */
    for (int create_action = 11; create_action <= 44; create_action += 33) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
        put_u32(action, 64); put_u16(action + 24, MT_MACHINE_SHOP - 1);
        action[26] = create_action; action[27] = 1;
        CHECK(!write_map("/tmp/open-rts-sc-create-addon.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-create-addon.chk", &level));
        ticks(1, &(hudtext_t){0});
        mobjlist_t created = P_ListMobjs();
        bool neutral_shop = false;
        for (int i = 0; i < created.count; i++)
            if (created.items[i]->type_id == MT_MACHINE_SHOP && created.items[i]->owner == 11)
                neutral_shop = true;
        CHECK(neutral_shop);
        P_FreeMobjList(&created); free(trig); unload();
    }
    remove("/tmp/open-rts-sc-create-addon.chk");
    /* UPRP owner currently overrides the neutral default for action 11. */
    trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
    put_u32(action, 64); put_u32(action + 20, 1); put_u16(action + 24, MT_MACHINE_SHOP - 1);
    action[26] = 11; action[27] = 1; action[28] = 28;
    CHECK(!write_map("/tmp/open-rts-sc-create-addon.chk", owners, trig));
    cuwp_file = fopen("/tmp/open-rts-sc-create-addon.chk", "ab"); CHECK(cuwp_file);
    memset(cuwp, 0, sizeof(cuwp)); put_u16(cuwp + 2, 1); cuwp[4] = 1;
    CHECK(fwrite(cuwp_header, 1, sizeof(cuwp_header), cuwp_file) == sizeof(cuwp_header));
    CHECK(fwrite(cuwp, 1, sizeof(cuwp), cuwp_file) == sizeof(cuwp)); CHECK(!fclose(cuwp_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-create-addon.chk", &level));
    ticks(1, &(hudtext_t){0});
    mobjlist_t owner_shop = P_ListMobjs(); bool owner_found = false;
    for (int i = 0; i < owner_shop.count; i++)
        if (owner_shop.items[i]->type_id == MT_MACHINE_SHOP && owner_shop.items[i]->owner == 1)
            owner_found = true;
    CHECK(owner_found);
    P_FreeMobjList(&owner_shop); free(trig); unload(); remove("/tmp/open-rts-sc-create-addon.chk");

    /* Remove Unit includes hallucinations and has no 256-unit truncation. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u16(action + 24, MT_MARINE - 1); action[26] = 24;
    CHECK(!write_map("/tmp/open-rts-sc-remove.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-remove.chk", &level));
    mobj_t *removed = NULL;
    for (int i = 0; i < 260; i++) { removed = spawn(MT_MARINE - 1, 0); CHECK(removed); }
    removed->sc.flags |= SC_HALLUCINATION;
    ticks(1, &(hudtext_t){0});
    CHECK(removed->remove);
    mobjlist_t remaining = P_ListMobjs();
    for (int i = 0; i < remaining.count; i++) CHECK(remaining.items[i]->type_id != MT_MARINE);
    P_FreeMobjList(&remaining);
    free(trig); unload(); remove("/tmp/open-rts-sc-remove.chk");

    /* Remove all at a location includes hallucinations and has no buffer cap. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action, 1); put_u16(action + 24, MT_MARINE - 1); action[26] = 25;
    CHECK(!write_map("/tmp/open-rts-sc-remove-area.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-remove-area.chk", &level));
    sc_mission_t *remove_mission = level.mission;
    remove_mission->loc_count = 1;
    remove_mission->loc_right[0] = remove_mission->loc_bottom[0] = 64;
    mobj_t *removed_area = NULL;
    for (int i = 0; i < 260; i++) { removed_area = spawn(MT_MARINE - 1, 0); CHECK(removed_area); }
    removed_area->sc.flags |= SC_HALLUCINATION;
    mobj_t *outside = spawn(MT_MARINE - 1, 0); CHECK(outside);
    outside->core.position = (fixed3_t){FIXED_FROM_INT(5), FIXED_FROM_INT(5), 0};
    ticks(1, &(hudtext_t){0});
    CHECK(removed_area->remove && !outside->remove);
    free(trig); unload();
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action, 1); put_u16(action + 24, MT_MARINE - 1); action[26] = 25; action[27] = 1;
    CHECK(!write_map("/tmp/open-rts-sc-remove-area.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-remove-area.chk", &level));
    remove_mission = level.mission; remove_mission->loc_count = 1;
    remove_mission->loc_right[0] = remove_mission->loc_bottom[0] = 64;
    mobj_t *illusion = spawn(MT_MARINE - 1, 0), *survivor = spawn(MT_MARINE - 1, 0);
    CHECK(illusion && survivor); illusion->sc.flags |= SC_HALLUCINATION;
    ticks(1, &(hudtext_t){0});
    CHECK(illusion->remove && !survivor->remove);
    free(trig); unload();
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action, 1); put_u16(action + 24, MT_MARINE - 1); action[26] = 25; action[27] = 255;
    CHECK(!write_map("/tmp/open-rts-sc-remove-area.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-remove-area.chk", &level));
    remove_mission = level.mission; remove_mission->loc_count = 1;
    remove_mission->loc_right[0] = remove_mission->loc_bottom[0] = 64;
    mobj_t *limit_units[260];
    for (int i = 0; i < 260; i++) { limit_units[i] = spawn(MT_MARINE - 1, 0); CHECK(limit_units[i]); }
    ticks(1, &(hudtext_t){0});
    for (int i = 0; i < 260; i++) CHECK(limit_units[i]->remove == (i < 255));
    free(trig); unload(); remove("/tmp/open-rts-sc-remove-area.chk");

    /* Kill/Remove at location use modified Anywhere; whole-map actions stay global. */
    for (int id = 22; id <= 25; id++) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
        put_u32(action, 64); put_u16(action + 24, MT_MARINE - 1); action[26] = (uint8_t)id;
        CHECK(!write_map("/tmp/open-rts-sc-area-anywhere.chk", owners, trig));
        FILE *area_file = fopen("/tmp/open-rts-sc-area-anywhere.chk", "ab"); CHECK(area_file);
        uint8_t area_header[8] = {'M','R','G','N'}, area_locations[64 * 20] = {0};
        put_u32(area_header + 4, sizeof(area_locations));
        put_u32(area_locations + 63 * 20, 128); put_u32(area_locations + 63 * 20 + 4, 128);
        put_u32(area_locations + 63 * 20 + 8, 160); put_u32(area_locations + 63 * 20 + 12, 160);
        CHECK(fwrite(area_header, 1, sizeof(area_header), area_file) == sizeof(area_header));
        CHECK(fwrite(area_locations, 1, sizeof(area_locations), area_file) == sizeof(area_locations));
        CHECK(!fclose(area_file));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-area-anywhere.chk", &level));
        mobj_t *area_inside = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){144, 144}, 0);
        mobj_t *area_outside = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){240, 144}, 0);
        CHECK(area_inside && area_outside);
        ticks(1, &(hudtext_t){0});
        if (id <= 23) CHECK(area_inside->hp <= 0 && (id == 22 ? area_outside->hp <= 0 : area_outside->hp > 0));
        else CHECK(area_inside->remove && (id == 24 ? area_outside->remove : !area_outside->remove));
        free(trig); unload();
    }
    remove("/tmp/open-rts-sc-area-anywhere.chk");

    /* Kill/Remove selectors never target DAT turret subunits. */
    for (int id = 22; id <= 25; id++) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
        put_u32(action, 64); put_u16(action + 24, 229); action[26] = (uint8_t)id;
        CHECK(!write_map("/tmp/open-rts-sc-turret-select.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-turret-select.chk", &level));
        mobj_t *turret = sc_spawn_actor(4, (ivec2_t){128, 128}, 0); /* Goliath turret row. */
        mobj_t *marine = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){192, 128}, 0);
        CHECK(turret && marine); turret->hp = 1;
        ticks(1, &(hudtext_t){0});
        CHECK(!turret->remove && turret->hp == 1 && (marine->remove || marine->hp <= 0));
        free(trig); unload();
    }
    remove("/tmp/open-rts-sc-turret-select.chk");

    /* Kill Unit uses the same complete selection and the normal death path. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u16(action + 24, MT_MARINE - 1); action[26] = 22;
    CHECK(!write_map("/tmp/open-rts-sc-kill.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-kill.chk", &level));
    mobj_t *killed = NULL;
    for (int i = 0; i < 260; i++) { killed = spawn(MT_MARINE - 1, 0); CHECK(killed); }
    killed->sc.flags |= SC_HALLUCINATION;
    ticks(1, &(hudtext_t){0});
    CHECK(killed->hp == 0);
    sc_mission_t *kill_mission = level.mission;
    CHECK(kill_mission->deaths[0][MT_MARINE - 1] == 260);
    CHECK(kill_mission->kills[0][MT_MARINE - 1] == 0);
    free(trig); unload(); remove("/tmp/open-rts-sc-kill.chk");

    /* Kill all at a location includes hallucinations, beyond 256 matches. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action, 1); put_u16(action + 24, MT_MARINE - 1); action[26] = 23;
    CHECK(!write_map("/tmp/open-rts-sc-kill-area.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-kill-area.chk", &level));
    kill_mission = level.mission; kill_mission->loc_count = 1;
    kill_mission->loc_right[0] = kill_mission->loc_bottom[0] = 64;
    for (int i = 0; i < 260; i++) { killed = spawn(MT_MARINE - 1, 0); CHECK(killed); }
    killed->sc.flags |= SC_HALLUCINATION;
    mobj_t *outside_kill = spawn(MT_MARINE - 1, 0); CHECK(outside_kill);
    outside_kill->core.position = (fixed3_t){FIXED_FROM_INT(5), FIXED_FROM_INT(5), 0};
    ticks(1, &(hudtext_t){0});
    CHECK(killed->hp == 0 && outside_kill->hp > 0);
    CHECK(kill_mission->deaths[0][MT_MARINE - 1] == 260);
    free(trig); unload();
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action, 1); put_u16(action + 24, MT_MARINE - 1); action[26] = 23; action[27] = 1;
    CHECK(!write_map("/tmp/open-rts-sc-kill-area.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-kill-area.chk", &level));
    kill_mission = level.mission; kill_mission->loc_count = 1;
    kill_mission->loc_right[0] = kill_mission->loc_bottom[0] = 64;
    mobj_t *killed_illusion = spawn(MT_MARINE - 1, 0), *saved_marine = spawn(MT_MARINE - 1, 0);
    CHECK(killed_illusion && saved_marine); killed_illusion->sc.flags |= SC_HALLUCINATION;
    ticks(1, &(hudtext_t){0});
    CHECK(killed_illusion->hp == 0 && saved_marine->hp > 0);
    CHECK(kill_mission->deaths[0][MT_MARINE - 1] == 1);
    free(trig); unload(); remove("/tmp/open-rts-sc-kill-area.chk");

    /* Trigger kills bypass combat shields, armor, and invincibility. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u16(action + 24, MT_ZEALOT - 1); action[26] = 22;
    CHECK(!write_map("/tmp/open-rts-sc-kill-defense.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-kill-defense.chk", &level));
    mobj_t *shielded = spawn(MT_ZEALOT - 1, 0), *invincible = spawn(MT_ZEALOT - 1, 0);
    CHECK(shielded && invincible && sc_shields(shielded) > 0);
    invincible->sc.flags |= SC_INVINCIBLE;
    ticks(1, &(hudtext_t){0});
    CHECK(shielded->hp == 0 && invincible->hp == 0);
    kill_mission = level.mission;
    CHECK(kill_mission->deaths[0][MT_ZEALOT - 1] == 2);
    free(trig); unload(); remove("/tmp/open-rts-sc-kill-defense.chk");

    /* Set Invincibility applies to every matching unit, including illusions. */
    for (int state = 4; state <= 6; state++) {
        trig = blank_trigger(); CHECK(trig);
        trig[15] = 22; action = trig + 320;
        put_u32(action, 1); put_u16(action + 24, MT_MARINE - 1);
        action[26] = 43; action[27] = (uint8_t)state;
        CHECK(!write_map("/tmp/open-rts-sc-invincibility.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-invincibility.chk", &level));
        sc_mission_t *inv_mission = level.mission; inv_mission->loc_count = 1;
        inv_mission->loc_right[0] = inv_mission->loc_bottom[0] = 64;
        mobj_t *last_inv = NULL;
        for (int i = 0; i < 260; i++) {
            last_inv = spawn(MT_MARINE - 1, 0); CHECK(last_inv);
            if (state != 4) last_inv->sc.flags |= SC_INVINCIBLE;
        }
        last_inv->sc.flags |= SC_HALLUCINATION;
        mobj_t *outside_inv = spawn(MT_MARINE - 1, 0); CHECK(outside_inv);
        outside_inv->core.position = (fixed3_t){FIXED_FROM_INT(5), FIXED_FROM_INT(5), 0};
        ticks(1, &(hudtext_t){0});
        CHECK(!!(last_inv->sc.flags & SC_INVINCIBLE) == (state == 4));
        CHECK(!(outside_inv->sc.flags & SC_INVINCIBLE));
        free(trig); unload();
    }
    remove("/tmp/open-rts-sc-invincibility.chk");

    /* Set Invincibility scopes modified Anywhere to its stored MRGN bounds. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action, 64); put_u16(action + 24, MT_MARINE - 1);
    action[26] = 43; action[27] = 4;
    CHECK(!write_map("/private/tmp/sc-invincibility-anywhere.chk", owners, trig));
    FILE *inv_file = fopen("/private/tmp/sc-invincibility-anywhere.chk", "ab"); CHECK(inv_file);
    uint8_t inv_header[8] = {'M','R','G','N'}, inv_locations[64 * 20] = {0};
    put_u32(inv_header + 4, sizeof(inv_locations));
    put_u32(inv_locations + 63 * 20, 128); put_u32(inv_locations + 63 * 20 + 4, 128);
    put_u32(inv_locations + 63 * 20 + 8, 160); put_u32(inv_locations + 63 * 20 + 12, 160);
    CHECK(fwrite(inv_header, 1, sizeof(inv_header), inv_file) == sizeof(inv_header));
    CHECK(fwrite(inv_locations, 1, sizeof(inv_locations), inv_file) == sizeof(inv_locations));
    CHECK(!fclose(inv_file));
    CHECK(G_DoLoadLevel("/private/tmp/sc-invincibility-anywhere.chk", &level));
    mobj_t *inside_inv = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){144, 144}, 0);
    mobj_t *outside_inv = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){240, 144}, 0);
    CHECK(inside_inv && outside_inv);
    ticks(1, &(hudtext_t){0});
    CHECK(inside_inv->sc.flags & SC_INVINCIBLE);
    CHECK(!(outside_inv->sc.flags & SC_INVINCIBLE));
    free(trig); unload(); remove("/private/tmp/sc-invincibility-anywhere.chk");

    /* Move Location shifts the destination by the match's source-relative offset. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action, 1); put_u32(action + 20, 2);
    put_u16(action + 24, MT_MARINE - 1); action[26] = 38;
    CHECK(!write_map("/tmp/open-rts-sc-location.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-location.chk", &level));
    sc_mission_t *location_mission = level.mission;
    location_mission->loc_count = 2;
    location_mission->loc_right[0] = location_mission->loc_bottom[0] = 128;
    location_mission->loc_left[1] = location_mission->loc_top[1] = 200;
    location_mission->loc_right[1] = location_mission->loc_bottom[1] = 264;
    mobj_t *location_illusion = spawn(MT_MARINE - 1, 0); CHECK(location_illusion);
    sc_initial_hallucination(location_illusion);
    ticks(1, &(hudtext_t){0});
    CHECK(location_mission->loc_left[0] == 0 && location_mission->loc_right[0] == 128);
    CHECK(location_mission->loc_left[1] == 168 && location_mission->loc_top[1] == 168);
    CHECK(location_mission->loc_right[1] == 232 && location_mission->loc_bottom[1] == 232);
    unload();
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-location.chk", &level));
    location_mission = level.mission;
    location_mission->loc_count = 2;
    location_mission->loc_left[0] = 33; /* Marine center is just outside. */
    location_mission->loc_right[0] = location_mission->loc_bottom[0] = 128;
    location_mission->loc_left[1] = location_mission->loc_top[1] = -10;
    location_mission->loc_right[1] = location_mission->loc_bottom[1] = 54;
    CHECK(spawn(MT_MARINE - 1, 0));
    ticks(1, &(hudtext_t){0});
    CHECK(location_mission->loc_left[1] == 0 && location_mission->loc_top[1] == 0);
    CHECK(location_mission->loc_right[1] == 64 && location_mission->loc_bottom[1] == 64);
    unload();
    for (int transport_inside = 0; transport_inside < 2; transport_inside++) {
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-location.chk", &level));
        location_mission = level.mission; location_mission->loc_count = 2;
        location_mission->loc_right[0] = location_mission->loc_bottom[0] = 128;
        location_mission->loc_left[1] = location_mission->loc_top[1] = 200;
        location_mission->loc_right[1] = location_mission->loc_bottom[1] = 264;
        location_mission->loc_elevation[0] = 8; /* exclude ground, not the flying transport */
        if (!location_mission->terrain) location_mission->terrain = calloc((size_t)level.width * level.height, sizeof(*location_mission->terrain));
        CHECK(location_mission->terrain);
        mobj_t *ship = sc_spawn_actor(MT_DROPSHIP - 1,
            (ivec2_t){transport_inside ? 32 : 512, transport_inside ? 32 : 512}, 1);
        mobj_t *passenger = sc_spawn_actor(MT_MARINE - 1,
            (ivec2_t){transport_inside ? 512 : 32, transport_inside ? 512 : 32}, 0);
        CHECK(ship && passenger); ship->sc.cargo[0] = passenger->id;
        passenger->sc.parent = ship->id; passenger->sc.flags |= SC_LOADED;
        ticks(1, &(hudtext_t){0});
        CHECK(location_mission->loc_left[1] == (transport_inside ? 168 : 200));
        CHECK(location_mission->loc_top[1] == (transport_inside ? 168 : 200));
        unload();
    }
    put_u16(action + 24, 4); /* Goliath turret, units.dat row 4. */
    CHECK(!write_map("/tmp/open-rts-sc-location.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-location.chk", &level));
    location_mission = level.mission; location_mission->loc_count = 2;
    location_mission->loc_right[0] = location_mission->loc_bottom[0] = 128;
    location_mission->loc_left[1] = location_mission->loc_top[1] = 200;
    location_mission->loc_right[1] = location_mission->loc_bottom[1] = 264;
    mobj_t *location_turret = sc_spawn_actor(4, (ivec2_t){32, 32}, 0); CHECK(location_turret);
    location_turret->hp = 1;
    ticks(1, &(hudtext_t){0});
    CHECK(location_mission->loc_left[1] == 200 && location_mission->loc_top[1] == 200);
    CHECK(location_mission->loc_right[1] == 264 && location_mission->loc_bottom[1] == 264);
    free(trig); unload(); remove("/tmp/open-rts-sc-location.chk");

    trig = blank_trigger();
    CHECK(trig);
    trig[15] = 22;
    action = trig + 320;
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

    /* Modify all hit points is not limited to the former 256-unit buffer. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action + 8, 25); put_u32(action + 20, 50);
    put_u16(action + 24, MT_MARINE - 1); action[26] = 49;
    CHECK(!write_map("/tmp/open-rts-sc-hp.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-hp.chk", &level));
    mobj_t *last_hp = NULL;
    for (int i = 0; i < 260; i++) { last_hp = spawn(MT_MARINE - 1, 0); CHECK(last_hp); sc_initial_hp(last_hp, 1); }
    ticks(1, &(hudtext_t){0});
    CHECK(last_hp->hp == last_hp->max_hp / 2 && !last_hp->sc.wound);
    free(trig); unload();
    trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
    put_u32(action + 20, 100); put_u16(action + 24, MT_MARINE - 1); action[26] = 49;
    CHECK(!write_map("/tmp/open-rts-sc-hp.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-hp.chk", &level));
    last_hp = spawn(MT_MARINE - 1, 0); CHECK(last_hp); sc_initial_hp(last_hp, 1);
    ticks(1, &(hudtext_t){0}); CHECK(last_hp->hp == last_hp->max_hp && !last_hp->sc.wound);
    free(trig); unload();
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u16(action + 24, MT_ZEALOT - 1); action[26] = 49;
    CHECK(!write_map("/tmp/open-rts-sc-hp.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-hp.chk", &level));
    mobj_t *zero_hp = spawn(MT_ZEALOT - 1, 0); CHECK(zero_hp && sc_shields(zero_hp) > 0);
    zero_hp->sc.flags |= SC_INVINCIBLE;
    ticks(1, &(hudtext_t){0});
    CHECK(zero_hp->hp == 0);
    free(trig); unload(); remove("/tmp/open-rts-sc-hp.chk");

    /* Modify energy and shields affect only the selected owner and count. */
    for (int id = 50; id <= 51; id++) {
        trig = blank_trigger(); CHECK(trig);
        trig[15] = 22;
        action = trig + 320;
        put_u32(action + 8, 25); put_u32(action + 20, 75);
        put_u16(action + 24, id == 50 ? MT_GHOST - 1 : MT_ZEALOT - 1);
        action[26] = (uint8_t)id;
        action[27] = 1;
        CHECK(!write_map("/tmp/open-rts-sc-modify.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-modify.chk", &level));
        int type = id == 50 ? MT_GHOST - 1 : MT_ZEALOT - 1;
        mobj_t *selected = spawn(type, 0), *other = spawn(type, 0);
        CHECK(selected && other);
        ticks(1, &(hudtext_t){0});
        if (id == 50) {
            CHECK(sc_energy(selected) == 150 && sc_energy(other) == 50);
            CHECK(selected->sc.flags & SC_STARTED);
            CHECK(!(other->sc.flags & SC_STARTED));
        } else {
            CHECK(sc_shields(selected) == sc_units[type].shields * 3 / 4);
            CHECK(sc_shields(other) == sc_units[type].shields);
        }
        free(trig); unload();
    }
    /* Vitals actions apply the MRGN-backed modified Anywhere location. */
    for (int id = 49; id <= 51; id++) {
        trig = blank_trigger(); CHECK(trig);
        trig[15] = 22; action = trig + 320;
        put_u32(action, 64); put_u32(action + 20, 75);
        int type = id == 49 ? MT_MARINE - 1 : id == 50 ? MT_GHOST - 1 : MT_ZEALOT - 1;
        put_u16(action + 24, type); action[26] = (uint8_t)id;
        CHECK(!write_map("/tmp/open-rts-sc-modify.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-modify.chk", &level));
        sc_mission_t *vitals_mission = level.mission;
        vitals_mission->loc_count = 64;
        vitals_mission->loc_right[63] = vitals_mission->loc_bottom[63] = 128;
        mobj_t *inside = sc_spawn_actor(type, (ivec2_t){32, 32}, 0);
        mobj_t *outside = sc_spawn_actor(type, (ivec2_t){320, 320}, 0);
        CHECK(inside && outside);
        ticks(1, &(hudtext_t){0});
        if (id == 49) {
            CHECK(inside->hp == inside->max_hp * 3 / 4 && outside->hp == outside->max_hp);
        } else if (id == 50) {
            CHECK(sc_energy(inside) == 150 && sc_energy(outside) == 50);
        } else {
            CHECK(sc_shields(inside) == sc_units[type].shields * 3 / 4 &&
                  sc_shields(outside) == sc_units[type].shields);
        }
        free(trig); unload();
    }
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action, 64); put_u32(action + 20, 75);
    put_u16(action + 24, MT_MARINE - 1); action[26] = 49;
    CHECK(!write_map("/tmp/open-rts-sc-modify.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-modify.chk", &level));
    mobj_t *global_a = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){32, 32}, 0);
    mobj_t *global_b = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){320, 320}, 0);
    CHECK(global_a && global_b);
    ticks(1, &(hudtext_t){0});
    CHECK(global_a->hp == global_a->max_hp * 3 / 4 && global_b->hp == global_b->max_hp * 3 / 4);
    free(trig); unload();
    remove("/tmp/open-rts-sc-modify.chk");
    /* All includes units beyond the shared 256-entry gathering buffer. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u16(action + 24, MT_GHOST - 1); action[26] = 50;
    CHECK(!write_map("/tmp/open-rts-sc-modify.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-modify.chk", &level));
    mobj_t *last = NULL;
    for (int i = 0; i < 260; i++) { last = spawn(MT_GHOST - 1, 0); CHECK(last); }
    ticks(1, &(hudtext_t){0});
    CHECK(last && sc_energy(last) == 0);
    free(trig); unload();
    remove("/tmp/open-rts-sc-modify.chk");

    /* Modify Hangar adds up to capacity, including launched fighters. */
    for (int reaver = 0; reaver < 2; reaver++) for (int hero = 0; hero < 2; hero++)
      for (int all = 0; all < 2; all++) for (int upgraded = 0; upgraded < 2; upgraded++) {
        trig = blank_trigger(); CHECK(trig);
        trig[15] = 22; action = trig + 320;
        put_u32(action, 64); put_u32(action + 8, 1); put_u32(action + 20, 15);
        int type = reaver ? (hero ? MT_WARBRINGER : MT_REAVER) : (hero ? MT_GANTRITHOR : MT_CARRIER);
        put_u16(action + 24, type - 1); action[26] = 53; action[27] = all ? 0 : 1;
        CHECK(!write_map("/tmp/open-rts-sc-hangar.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-hangar.chk", &level));
        mobj_t *carrier_a = spawn(type - 1, 0), *carrier_b = spawn(type - 1, 0);
        mobj_t *launched = spawn((reaver ? MT_SCARAB : MT_INTERCEPTOR) - 1, 0);
        CHECK(carrier_a && carrier_b && launched);
        if (upgraded) level.upgrades[reaver ? 36 : 43][0].weapon = 1;
        carrier_a->sc.hangar = 1; launched->sc.parent = carrier_a->id;
        ticks(1, &(hudtext_t){0});
        int capacity = reaver ? (upgraded || hero ? 10 : 5) : (upgraded || hero ? 8 : 4);
        CHECK(carrier_a->sc.hangar == capacity - 1 && sc_hangar_count(carrier_a) == capacity);
        CHECK(carrier_b->sc.hangar == (all ? capacity : 0));
        free(trig); unload();
    }
    remove("/tmp/open-rts-sc-hangar.chk");

    /* Give Units and Modify Hangar Count use modified Anywhere bounds. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action, 64); put_u32(action + 20, 1);
    put_u16(action + 24, MT_MARINE - 1); action[26] = 48;
    CHECK(!write_map("/tmp/open-rts-sc-give-location.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-give-location.chk", &level));
    sc_mission_t *give_location_mission = level.mission;
    give_location_mission->loc_count = 64;
    give_location_mission->loc_right[63] = give_location_mission->loc_bottom[63] = 128;
    mobj_t *given_inside = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){32, 32}, 0);
    mobj_t *given_outside = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){320, 320}, 0);
    CHECK(given_inside && given_outside);
    ticks(1, &(hudtext_t){0});
    CHECK(given_inside->owner == 1 && given_outside->owner == 0);
    free(trig); unload(); remove("/tmp/open-rts-sc-give-location.chk");

    /* Production initializes the sprite owner, an active Give Unit changes it,
     * and a later defeat preserves that owner while making the unit neutral. */
    trig = blank_trigger(); CHECK(trig); trig[15] = 22;
    trig[2372 + 17] = 0; trig[2372] = 1; /* Player 0 executes the trigger. */
    action = trig + 320;
    put_u32(action + 16, 1); put_u32(action + 20, 0);
    put_u16(action + 24, MT_MARINE - 1); action[26] = 48;
    action[32 + 26] = 2;
    CHECK(!write_map("/tmp/open-rts-sc-produced-neutral.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-produced-neutral.chk", &level));
    CHECK(sc_spawn_actor(MT_COMMAND_CENTER - 1, (ivec2_t){128, 128}, 1));
    mobj_t *enemy_barracks = sc_spawn_actor(MT_BARRACKS - 1, (ivec2_t){256, 256}, 1);
    CHECK(enemy_barracks);
    const StaticProductDefinition *enemy_marine_product = G_ModelProductByUIId(NULL, MT_MARINE);
    level.player_resources[1][0] = 1000;
    CHECK(enemy_marine_product && G_QueueProduct(enemy_barracks, enemy_marine_product));
    enemy_barracks->production->time_left_ms = 1;
    CHECK(G_ProductionTicker(RTS_TICK_MS));
    mobj_t *produced_enemy = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *candidate = (mobj_t *)th;
        if (th->function == P_MobjThinker && candidate->type_id == MT_MARINE && candidate->owner == 1)
            produced_enemy = candidate;
    }
    CHECK(produced_enemy && produced_enemy->sc.sprite_owner == 1);
    ticks(1, &(hudtext_t){0});
    CHECK(((sc_mission_t *)level.mission)->player_result[0] == 2);
    CHECK(produced_enemy->owner == 11 && produced_enemy->team == 11 && produced_enemy->sc.sprite_owner == 0);
    level.player_colors[0] = 5;
    CHECK(gameinfo && gameinfo->mobj_palette && gameinfo->mobj_palette(produced_enemy) == 5);
    mobj_t *initially_neutral = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){192, 192}, 11);
    CHECK(initially_neutral);
    initially_neutral->sc.sprite_owner = 11;
    CHECK(gameinfo->mobj_palette(initially_neutral) == 11);
    free(trig); unload(); remove("/tmp/open-rts-sc-produced-neutral.chk");

    trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
    put_u32(action + 16, 1); put_u32(action + 20, 0);
    put_u16(action + 24, MT_MARINE - 1); action[26] = 48;
    CHECK(!write_map("/tmp/open-rts-sc-give-target.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-give-target.chk", &level));
    mobj_t *transferred_target = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){320, 128}, 1);
    mobj_t *current_attacker = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){128, 128}, 0);
    CHECK(transferred_target && current_attacker);
    mobj_t *lockdown_caster = sc_spawn_actor(MT_GHOST - 1, (ivec2_t){32, 160}, 0);
    mobj_t *irradiate_caster = sc_spawn_actor(MT_SCIENCE_VESSEL - 1, (ivec2_t){128, 192}, 0);
    CHECK(lockdown_caster && irradiate_caster);
    lockdown_caster->sc.order = (sc_order_t){.kind=SC_ORDER_CAST,.tech=SC_TECH_LOCKDOWN,
                                              .target=transferred_target->id};
    CHECK(P_MoveUnitTo(&level, lockdown_caster, fixed3_xy(transferred_target->core.position)));
    lockdown_caster->move_only = true;
    CHECK(P_HasMoveOrder(lockdown_caster));
    irradiate_caster->sc.order = (sc_order_t){.kind=SC_ORDER_CAST,.tech=SC_TECH_IRRADIATE,
                                               .target=transferred_target->id};
    current_attacker->attack.target = transferred_target;
    current_attacker->sc.queue.count = 1;
    current_attacker->sc.queue.pending[0] = (sc_pending_order_t){
        .order = TC_ATTACK, .target = transferred_target->id
    };
    ticks(1, &(hudtext_t){0});
    CHECK(transferred_target->owner == 0 && current_attacker->attack.target == NULL);
    CHECK(lockdown_caster->sc.order.kind == SC_ORDER_NONE && !lockdown_caster->move_only &&
          !P_HasMoveOrder(lockdown_caster) && lockdown_caster->movement.order_arrived &&
          fixed2_near(lockdown_caster->movement.goal, fixed3_xy(lockdown_caster->core.position), 1));
    CHECK(irradiate_caster->sc.order.kind == SC_ORDER_CAST &&
          irradiate_caster->sc.order.tech == SC_TECH_IRRADIATE);
    CHECK(current_attacker->sc.queue.count == 1 &&
          current_attacker->sc.queue.pending[0].order == TC_ATTACK &&
          current_attacker->sc.queue.pending[0].target == transferred_target->id);
    current_attacker->movement.order_arrived = true;
    P_Ticker();
    CHECK(current_attacker->sc.queue.count == 0 &&
          current_attacker->sc.queue.active == TC_ATTACK &&
          current_attacker->attack.target == transferred_target);
    free(trig); unload(); remove("/tmp/open-rts-sc-give-target.chk");

    /* Active support targets survive transfers under directed non-enemy status. */
    const uint32_t support_traits[] = {MF_HEAL, MF_REPAIR};
    for (size_t i = 0; i < sizeof(support_traits) / sizeof(support_traits[0]); i++) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
        put_u32(action + 16, 1); put_u32(action + 20, 0);
        put_u16(action + 24, MT_MARINE - 1); action[26] = 48;
        CHECK(!write_map("/tmp/open-rts-sc-give-support.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-give-support.chk", &level));
        sc_mission_t *support_mission = level.mission;
        support_mission->allies[0] |= UINT32_C(0x40000000) >> 1;
        mobj_t *support_target = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){160, 128}, 1);
        mobj_t *support_unit = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){128, 128}, 0);
        CHECK(support_target && support_unit);
        support_unit->traits |= support_traits[i];
        support_unit->attack.target = support_target;
        ticks(1, &(hudtext_t){0});
        CHECK(support_target->owner == 0 && support_unit->attack.target == support_target);
        free(trig); unload();
    }
    remove("/tmp/open-rts-sc-give-support.chk");

    /* A rescued neutral owner is not an enemy solely because it is not allied. */
    uint8_t neutral_give_owners[12] = {6, 5, 7};
    trig = blank_trigger(); CHECK(trig); trig[15] = 22; trig[2372 + 17] = 0; trig[2372] = 1;
    action = trig + 320;
    put_u32(action + 16, 1); put_u32(action + 20, 2);
    put_u16(action + 24, MT_MARINE - 1); action[26] = 48;
    CHECK(!write_map("/tmp/open-rts-sc-give-neutral.chk", neutral_give_owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-give-neutral.chk", &level));
    mobj_t *neutral_give_target = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){160, 128}, 1);
    mobj_t *neutral_give_attacker = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){128, 128}, 0);
    CHECK(neutral_give_target && neutral_give_attacker);
    neutral_give_attacker->attack.target = neutral_give_target;
    ticks(1, &(hudtext_t){0});
    CHECK(neutral_give_target->owner == 2 && neutral_give_attacker->attack.target == NULL);
    free(trig); unload(); remove("/tmp/open-rts-sc-give-neutral.chk");

    /* Defeat retains the target's native sprite owner for directional hostility. */
    uint8_t neutral_target_owners[12] = {6, 5};
    for (int allied_direction = 0; allied_direction < 2; allied_direction++) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22;
        trig[2372 + 17] = 0; trig[2372 + 1] = 1;
        trig[320 + 26] = 2;
        CHECK(!write_map("/tmp/open-rts-sc-neutral-target.chk", neutral_target_owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-neutral-target.chk", &level));
        sc_mission_t *neutral_target_mission = level.mission;
        uint32_t p0_bit = UINT32_C(0x40000000);
        uint32_t p1_bit = UINT32_C(0x20000000);
        if (allied_direction) neutral_target_mission->allies[0] |= p1_bit;
        else neutral_target_mission->allies[1] |= p0_bit;
        level.player_teams = true;
        CHECK(P_InitSight());
        mobj_t *neutral_target = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){160, 128}, 1);
        mobj_t *neutral_attacker = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){128, 128}, 0);
        CHECK(neutral_target && neutral_attacker);
        AiTeamState neutral_ai_team = {.allegiance = ALLEGIANCE_PLAYER};
        CHECK(P_AiIsAlly(&level, &neutral_ai_team, 0, neutral_target) == !!allied_direction);
        P_UpdateSight();
        size_t target_cell = L_Index(&level, 160 / 32, 128 / 32);
        CHECK(level.sight.cells[target_cell] & p1_bit);
        neutral_attacker->attack.target = neutral_target;
        ticks(1, &(hudtext_t){0});
        CHECK(neutral_target->owner == 11 && neutral_target->team == 11 && neutral_target->sc.sprite_owner == 1);
        CHECK(!P_AiIsAlly(&level, &neutral_ai_team, 0, neutral_target));
        P_UpdateSight();
        CHECK(!(level.sight.cells[target_cell] & p1_bit));
        CHECK((neutral_attacker->attack.target == NULL) == !!allied_direction);
        free(trig); unload();
    }
    remove("/tmp/open-rts-sc-neutral-target.chk");

    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action, 64); put_u32(action + 20, 2);
    put_u16(action + 24, MT_CARRIER - 1); action[26] = 53;
    CHECK(!write_map("/tmp/open-rts-sc-hangar-location.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-hangar-location.chk", &level));
    sc_mission_t *hangar_location_mission = level.mission;
    hangar_location_mission->loc_count = 64;
    hangar_location_mission->loc_right[63] = hangar_location_mission->loc_bottom[63] = 128;
    mobj_t *hangar_inside = sc_spawn_actor(MT_CARRIER - 1, (ivec2_t){32, 32}, 0);
    mobj_t *hangar_outside = sc_spawn_actor(MT_CARRIER - 1, (ivec2_t){320, 320}, 0);
    CHECK(hangar_inside && hangar_outside);
    ticks(1, &(hudtext_t){0});
    CHECK(hangar_inside->sc.hangar == 2 && hangar_outside->sc.hangar == 0);
    free(trig); unload(); remove("/tmp/open-rts-sc-hangar-location.chk");

    /* Resource amounts target the neutral resource object, not player stock. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action + 8, 123); put_u32(action + 16, 11); put_u32(action + 20, 700);
    action[26] = 52; action[27] = 1;
    CHECK(!write_map("/tmp/open-rts-sc-resource.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-resource.chk", &level));
    mobj_t *patch = spawn(MT_MINERAL_FIELD1 - 1, 11);
    mobj_t *second = spawn(MT_MINERAL_FIELD1 - 1, 11);
    CHECK(patch && second);
    level.resource_vents = calloc(2, sizeof(*level.resource_vents));
    CHECK(level.resource_vents);
    level.resource_vent_count = 2;
    level.resource_vents[0] = (resourcevent_t){.source_id = patch->id, .amount = 20};
    level.resource_vents[1] = (resourcevent_t){.source_id = second->id, .amount = 30};
    ticks(1, &(hudtext_t){0});
    CHECK(level.resource_vents[0].amount == 700 && level.resource_vents[1].amount == 30);
    CHECK(level.player_resources[0][0] == 0);
    free(trig); unload();
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action + 16, 17); put_u32(action + 20, 900);
    action[26] = 52;
    CHECK(!write_map("/tmp/open-rts-sc-resource.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-resource.chk", &level));
    mobj_t *refinery = spawn(MT_REFINERY - 1, 0);
    mobj_t *neutral = spawn(MT_VESPENE_GEYSER - 1, 11);
    mobj_t *nonresource = spawn(MT_MARINE - 1, 0);
    CHECK(refinery && neutral && nonresource);
    level.resource_vents = calloc(3, sizeof(*level.resource_vents)); CHECK(level.resource_vents);
    level.resource_vent_count = 3;
    level.resource_vents[0] = (resourcevent_t){.source_id = refinery->id, .amount = 0, .rate = 2,
                                               .depleted = true, .depleted_rate = 2};
    level.resource_vents[1] = (resourcevent_t){.source_id = neutral->id, .amount = 2};
    level.resource_vents[2] = (resourcevent_t){.source_id = nonresource->id, .amount = 3};
    ticks(1, &(hudtext_t){0});
    CHECK(level.resource_vents[0].amount == 900 && !level.resource_vents[0].depleted &&
          level.resource_vents[0].active && level.resource_vents[0].rate == 8);
    CHECK(level.resource_vents[1].amount == 900 && !level.resource_vents[1].active && level.resource_vents[2].amount == 3);
    free(trig); unload();
    trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
    put_u32(action + 16, 11); action[26] = 52;
    CHECK(!write_map("/tmp/open-rts-sc-resource.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-resource.chk", &level));
    patch = sc_spawn_actor(MT_MINERAL_FIELD1 - 1, (ivec2_t){480, 528}, 11);
    placed_base = sc_spawn_actor(MT_COMMAND_CENTER - 1, (ivec2_t){192, 528}, 0);
    placed_worker = sc_spawn_actor(MT_SCV - 1, (ivec2_t){336, 528}, 0);
    CHECK(patch && placed_base && placed_worker && sc_add_resource_vent(patch, 24));
    uint32_t zeroed_patch_id = patch->id; P_SyncBuildingBlocking();
    ticks(1, &(hudtext_t){0});
    CHECK(!level.resource_vents[0].amount && P_VentYields(&level.resource_vents[0]));
    CHECK(P_HarvestUnitTo(&level, placed_worker, level.resource_vents[0].attachment));
    for (int i = 0; i < 2400 && P_MobjById(zeroed_patch_id); i++) {
        P_Ticker(); G_ProductionTicker(RTS_TICK_MS);
    }
    CHECK(!P_MobjById(zeroed_patch_id) && !level.resource_vents[0].active && !level.player_resources[0][0]);
    free(trig); unload(); remove("/tmp/open-rts-sc-resource.chk");

    /* Modify Resource Amount honors the selected location's elevation mask. */
    trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
    put_u32(action, 1); put_u32(action + 16, 17); put_u32(action + 20, 700);
    action[26] = 52;
    CHECK(!write_map("/tmp/open-rts-sc-resource-height.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-resource-height.chk", &level));
    sc_mission_t *resource_mission = level.mission;
    resource_mission->loc_count = 1;
    resource_mission->loc_left[0] = resource_mission->loc_top[0] = 128;
    resource_mission->loc_right[0] = 192; resource_mission->loc_bottom[0] = 160;
    resource_mission->loc_elevation[0] = 1u << 4; /* Exclude middle-height ground. */
    resource_mission->terrain = calloc((size_t)level.width * level.height, sizeof(*resource_mission->terrain));
    CHECK(resource_mission->terrain);
    sc_terrain_t *high_tile = &resource_mission->terrain[L_Index(&level, 4, 4)];
    sc_terrain_t *middle_tile = &resource_mission->terrain[L_Index(&level, 5, 4)];
    for (int i = 0; i < 16; i++) {
        high_tile->vf4[i] = SC_TERRAIN_HIGH;
        middle_tile->vf4[i] = SC_TERRAIN_MIDDLE;
    }
    mobj_t *high_patch = sc_spawn_actor(MT_MINERAL_FIELD1 - 1, (ivec2_t){144, 144}, 11);
    mobj_t *middle_patch = sc_spawn_actor(MT_MINERAL_FIELD1 - 1, (ivec2_t){176, 144}, 11);
    CHECK(high_patch && middle_patch);
    level.resource_vents = calloc(2, sizeof(*level.resource_vents)); CHECK(level.resource_vents);
    level.resource_vent_count = 2;
    level.resource_vents[0] = (resourcevent_t){.source_id = high_patch->id, .amount = 20};
    level.resource_vents[1] = (resourcevent_t){.source_id = middle_patch->id, .amount = 30};
    ticks(1, &(hudtext_t){0});
    CHECK(level.resource_vents[0].amount == 700 && level.resource_vents[1].amount == 30);
    free(trig); unload(); remove("/tmp/open-rts-sc-resource-height.chk");

    /* Location 64 uses the modified Anywhere bounds for action 52. */
    trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
    put_u32(action, 64); put_u32(action + 16, 17); put_u32(action + 20, 700);
    action[26] = 52;
    CHECK(!write_map("/tmp/open-rts-sc-resource-anywhere.chk", owners, trig));
    FILE *resource_anywhere_file = fopen("/tmp/open-rts-sc-resource-anywhere.chk", "ab"); CHECK(resource_anywhere_file);
    uint8_t resource_location_header[8] = {'M','R','G','N'}, resource_locations[64 * 20] = {0};
    put_u32(resource_location_header + 4, sizeof(resource_locations));
    put_u32(resource_locations + 63 * 20, 128); put_u32(resource_locations + 63 * 20 + 4, 128);
    put_u32(resource_locations + 63 * 20 + 8, 160); put_u32(resource_locations + 63 * 20 + 12, 160);
    CHECK(fwrite(resource_location_header, 1, sizeof(resource_location_header), resource_anywhere_file) ==
          sizeof(resource_location_header));
    CHECK(fwrite(resource_locations, 1, sizeof(resource_locations), resource_anywhere_file) ==
          sizeof(resource_locations));
    CHECK(!fclose(resource_anywhere_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-resource-anywhere.chk", &level));
    mobj_t *inside_patch = sc_spawn_actor(MT_MINERAL_FIELD1 - 1, (ivec2_t){144, 144}, 11);
    mobj_t *outside_patch = sc_spawn_actor(MT_MINERAL_FIELD1 - 1, (ivec2_t){240, 144}, 11);
    CHECK(inside_patch && outside_patch);
    level.resource_vents = calloc(2, sizeof(*level.resource_vents)); CHECK(level.resource_vents);
    level.resource_vent_count = 2;
    level.resource_vents[0] = (resourcevent_t){.source_id = inside_patch->id, .amount = 20};
    level.resource_vents[1] = (resourcevent_t){.source_id = outside_patch->id, .amount = 30};
    ticks(1, &(hudtext_t){0});
    CHECK(level.resource_vents[0].amount == 700 && level.resource_vents[1].amount == 30);
    free(trig); unload(); remove("/tmp/open-rts-sc-resource-anywhere.chk");

    /* Bring respects ordinary, modified Anywhere, and invalid location IDs. */
    const struct { unsigned loc, qty; uint8_t op; bool qualifies; } bring_location_cases[] = {
        {0, 2, 10, true}, {1, 1, 10, true}, {64, 1, 10, true},
        {64, 2, 0, false}, {65, 1, 10, false}
    };
    for (size_t i = 0; i < sizeof(bring_location_cases) / sizeof(bring_location_cases[0]); i++) {
        trig = blank_trigger(); CHECK(trig);
        put_u32(trig, bring_location_cases[i].loc);
        put_u32(trig + 4, 0); put_u32(trig + 8, bring_location_cases[i].qty);
        put_u16(trig + 12, MT_MARINE - 1); trig[14] = bring_location_cases[i].op; trig[15] = 3;
        action = trig + 320; action[26] = 26; action[27] = 7;
        put_u32(action + 16, 13); put_u32(action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-bring-location.chk", owners, trig));
        if (bring_location_cases[i].loc == 64) {
            FILE *bring_location_file = fopen("/tmp/open-rts-sc-bring-location.chk", "ab"); CHECK(bring_location_file);
            uint8_t location_header[8] = {'M','R','G','N'}, locations[64 * 20] = {0};
            put_u32(location_header + 4, sizeof(locations));
            put_u32(locations + 63 * 20, 128); put_u32(locations + 63 * 20 + 4, 128);
            put_u32(locations + 63 * 20 + 8, 160); put_u32(locations + 63 * 20 + 12, 160);
            CHECK(fwrite(location_header, 1, sizeof(location_header), bring_location_file) == sizeof(location_header));
            CHECK(fwrite(locations, 1, sizeof(locations), bring_location_file) == sizeof(locations));
            CHECK(!fclose(bring_location_file));
        }
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-bring-location.chk", &level));
        if (bring_location_cases[i].loc == 1) {
            sc_mission_t *bring_mission = level.mission;
            bring_mission->loc_count = 1;
            bring_mission->loc_left[0] = bring_mission->loc_top[0] = 128;
            bring_mission->loc_right[0] = bring_mission->loc_bottom[0] = 160;
        }
        CHECK(sc_spawn_actor(MT_MARINE - 1, (ivec2_t){144, 144}, 0));
        CHECK(sc_spawn_actor(MT_MARINE - 1, (ivec2_t){240, 144}, 0));
        ticks(1, &(hudtext_t){0});
        CHECK((level.player_resources[0][0] == 10) == bring_location_cases[i].qualifies);
        free(trig); unload();
    }
    remove("/tmp/open-rts-sc-bring-location.chk");

    /* Order selects all matching units; Comment never issues an order. */
    for (int order = 0; order < 4; order++) {
        trig = blank_trigger(); CHECK(trig);
        trig[15] = 22; action = trig + 320;
        put_u32(action, 64); put_u32(action + 20, 64);
        put_u16(action + 24, MT_MARINE - 1);
        action[26] = order == 3 ? 47 : 46; action[27] = (uint8_t)order;
        CHECK(!write_map("/tmp/open-rts-sc-order-trigger.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-order-trigger.chk", &level));
        mobj_t *ordered = spawn(MT_MARINE - 1, 0), *other_type = spawn(MT_GHOST - 1, 0);
        mobj_t *hallucination = spawn(MT_MARINE - 1, 0);
        CHECK(ordered && other_type && hallucination);
        hallucination->sc.flags |= SC_HALLUCINATION;
        ticks(1, &(hudtext_t){0});
        CHECK(!P_HasMoveOrder(other_type));
        if (order == 3) CHECK(!P_HasMoveOrder(ordered) && !P_HasMoveOrder(hallucination));
        else {
            CHECK(P_HasMoveOrder(ordered) && P_HasMoveOrder(hallucination));
            if (order == 1) {
                CHECK(ordered->waypoints.mode == WP_BACKTRACK && ordered->waypoints.count == 2);
                bool returning = false, again = false;
                for (int i = 0; i < 1500 && !again; i++) {
                    P_Ticker();
                    returning |= ordered->waypoints.current == 1;
                    again = returning && ordered->waypoints.current == 0;
                }
                CHECK(returning && again);
            }
            if (order == 2) CHECK(ordered->waypoints.mode == WP_ONCE && ordered->waypoints.count == 1);
        }
        free(trig); unload();
    }
    /* Action 46 uses the edited MRGN record for Anywhere (location 64). */
    trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
    put_u32(action, 64); put_u32(action + 20, 64);
    put_u16(action + 24, MT_MARINE - 1); action[26] = 46;
    CHECK(!write_map("/tmp/open-rts-sc-order-anywhere.chk", owners, trig));
    FILE *anywhere_file = fopen("/tmp/open-rts-sc-order-anywhere.chk", "ab"); CHECK(anywhere_file);
    uint8_t location_header[8] = {'M','R','G','N'}, locations[64 * 20] = {0};
    put_u32(location_header + 4, sizeof(locations));
    put_u32(locations + 63 * 20, 320); put_u32(locations + 63 * 20 + 4, 320);
    put_u32(locations + 63 * 20 + 8, 448); put_u32(locations + 63 * 20 + 12, 448);
    CHECK(fwrite(location_header, 1, sizeof(location_header), anywhere_file) == sizeof(location_header));
    CHECK(fwrite(locations, 1, sizeof(locations), anywhere_file) == sizeof(locations));
    CHECK(!fclose(anywhere_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-order-anywhere.chk", &level));
    mobj_t *inside_anywhere = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){352, 352}, 0);
    mobj_t *outside_anywhere = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){512, 352}, 0);
    CHECK(inside_anywhere && outside_anywhere);
    ticks(1, &(hudtext_t){0});
    CHECK(P_HasMoveOrder(inside_anywhere) && !P_HasMoveOrder(outside_anywhere));
    CHECK(inside_anywhere->movement.goal.x == FIXED_LIT(12) &&
          inside_anywhere->movement.goal.y == FIXED_LIT(12));
    free(trig); unload(); remove("/tmp/open-rts-sc-order-anywhere.chk");

    trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
    put_u32(action, 64); put_u32(action + 20, 64); put_u16(action + 24, MT_ZERGLING - 1);
    action[26] = 46;
    uint8_t *second_order = action + 32;
    put_u32(second_order, 64); put_u32(second_order + 20, 64);
    put_u16(second_order + 24, MT_ZERGLING - 1); second_order[26] = 46; second_order[27] = 1;
    CHECK(!write_map("/tmp/open-rts-sc-order-trigger.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-order-trigger.chk", &level));
    mobj_t *burrowed = sc_spawn_actor(MT_ZERGLING - 1, (ivec2_t){128, 128}, 0); CHECK(burrowed);
    sc_burrow(burrowed);
    for (int i = 0; i < 500 && !(burrowed->sc.flags & SC_BURROWED); i++) {
        P_Ticker(); G_ProductionTicker(RTS_TICK_MS);
    }
    CHECK(burrowed->sc.flags & SC_BURROWED);
    ticks(1, &(hudtext_t){0});
    CHECK(!(burrowed->sc.flags & SC_BURROWED) && burrowed->sc.queue.count == 1 &&
          burrowed->sc.queue.pending[0].order == TC_PATH);
    for (int i = 0; i < 500 && !P_HasMoveOrder(burrowed); i++) {
        P_Ticker(); G_ProductionTicker(RTS_TICK_MS);
    }
    CHECK(P_HasMoveOrder(burrowed) && burrowed->waypoints.mode == WP_BACKTRACK);
    free(trig); unload();
    /* Order excludes fighters, nuclear missiles and rescuable owners. */
    uint8_t rescue_owners[12] = {6, 3, 4};
    trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
    put_u32(action, 64); put_u32(action + 16, 17); put_u32(action + 20, 64);
    put_u16(action + 24, 229); action[26] = 46;
    CHECK(!write_map("/tmp/open-rts-sc-order-trigger.chk", rescue_owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-order-trigger.chk", &level));
    const int excluded_types[] = {MT_INTERCEPTOR, MT_SCARAB, MT_NUCLEAR_MISSILE, MT_MARINE, MT_MARINE};
    mobj_t *excluded[5];
    for (int i = 0; i < 5; i++) {
        excluded[i] = sc_spawn_actor(excluded_types[i] - 1, (ivec2_t){128 + i * 32, 128}, i >= 3 ? i - 2 : 0);
        CHECK(excluded[i]); excluded[i]->traits |= MF_MOBILE;
    }
    mobj_t *normal_order = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){352, 128}, 0); CHECK(normal_order);
    mobj_t *lifting = sc_spawn_actor(MT_BARRACKS - 1, (ivec2_t){416, 128}, 0);
    mobj_t *landing = sc_spawn_actor(MT_BARRACKS - 1, (ivec2_t){576, 128}, 0);
    CHECK(lifting && landing);
    lifting->traits |= MF_MOBILE | MF_FLY; landing->traits |= MF_MOBILE | MF_FLY;
    lifting->sc.order.kind = SC_ORDER_LIFT; landing->sc.order.kind = SC_ORDER_LAND;
    ticks(1, &(hudtext_t){0});
    CHECK(P_HasMoveOrder(normal_order));
    for (int i = 0; i < 5; i++) CHECK(!excluded[i]->sc.queue.active && !P_HasMoveOrder(excluded[i]));
    CHECK(lifting->sc.order.kind == SC_ORDER_LIFT && landing->sc.order.kind == SC_ORDER_LAND);
    CHECK(!lifting->sc.queue.active && !landing->sc.queue.active);
    free(trig); unload();
    /* Any Unit counts native Men/Building groups, not resource objects. */
    uint8_t any_unit_owners[12] = {6, 5};
    trig = blank_trigger(); CHECK(trig);
    put_u32(trig + 4, 17); put_u32(trig + 8, 1); put_u16(trig + 12, 229);
    trig[14] = 10; trig[15] = 2; action = trig + 320;
    put_u32(action + 16, 0); put_u32(action + 20, 10); action[24] = 0;
    action[26] = 26; action[27] = 7;
    CHECK(!write_map("/tmp/open-rts-sc-any-unit-count.chk", any_unit_owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-any-unit-count.chk", &level));
    CHECK(sc_spawn_actor(MT_MARINE - 1, (ivec2_t){128, 128}, 0));
    CHECK(sc_spawn_actor(MT_MINERAL_FIELD1 - 1, (ivec2_t){256, 128}, 1));
    ticks(1, &(hudtext_t){0});
    CHECK(level.player_resources[0][0] == 10);
    free(trig); unload(); remove("/tmp/open-rts-sc-any-unit-count.chk");
    /* Eggs count toward incomplete Any Unit totals, but not completed totals. */
    const int any_egg_ops[] = {0, 1, 1, 10};
    const int any_egg_quantities[] = {1, 1, 0, 1};
    const bool any_egg_expected[] = {false, true, false, false};
    for (int condition = 2; condition <= 3; condition++) for (int i = 0; i < 4; i++) {
        int op = any_egg_ops[i];
        trig = blank_trigger(); CHECK(trig);
        if (condition == 3) put_u32(trig, 64);
        put_u32(trig + 8, (unsigned)any_egg_quantities[i]); put_u16(trig + 12, 229);
        trig[14] = (uint8_t)op; trig[15] = (uint8_t)condition; action = trig + 320;
        put_u32(action + 20, 10); action[24] = 0; action[26] = 26; action[27] = 7;
        CHECK(!write_map("/tmp/open-rts-sc-any-egg-count.chk", any_unit_owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-any-egg-count.chk", &level));
        CHECK(sc_spawn_actor(MT_EGG - 1, (ivec2_t){128, 128}, 0));
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[0][0] == (any_egg_expected[i] ? 10 : 0));
        free(trig); unload();
    }
    remove("/tmp/open-rts-sc-any-egg-count.chk");
    /* MRGN flags exclude individual air/ground terrain heights. */
    for (int flight = 0; flight < 2; flight++) for (int height = 0; height < 3; height++)
        for (int excluded_height = 0; excluded_height < 2; excluded_height++) {
            trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
            put_u32(action, 1); put_u32(action + 20, 2); put_u16(action + 24, 229); action[26] = 46;
            CHECK(!write_map("/tmp/open-rts-sc-order-trigger.chk", owners, trig));
            FILE *location_file = fopen("/tmp/open-rts-sc-order-trigger.chk", "ab"); CHECK(location_file);
            uint8_t location_header[8] = {'M','R','G','N'}, locations[40] = {0};
            put_u32(location_header + 4, sizeof(locations));
            put_u32(locations + 8, 256); put_u32(locations + 12, 256);
            put_u16(locations + 18, 1u << ((flight ? 0 : 3) + (excluded_height ? height : (height + 1) % 3)));
            put_u32(locations + 20, 448); put_u32(locations + 24, 448);
            put_u32(locations + 28, 576); put_u32(locations + 32, 576);
            CHECK(fwrite(location_header, 1, sizeof(location_header), location_file) == sizeof(location_header));
            CHECK(fwrite(locations, 1, sizeof(locations), location_file) == sizeof(locations)); CHECK(!fclose(location_file));
            CHECK(G_DoLoadLevel("/tmp/open-rts-sc-order-trigger.chk", &level));
            sc_mission_t *height_mission = level.mission;
            if (!height_mission->terrain) height_mission->terrain = calloc((size_t)level.width * level.height, sizeof(*height_mission->terrain));
            CHECK(height_mission->terrain);
            sc_terrain_t *tile = &height_mission->terrain[L_Index(&level, 4, 4)];
            for (int mini = 0; mini < 16; mini++) tile->vf4[mini] = height == 2 ? SC_TERRAIN_HIGH : height == 1 ? SC_TERRAIN_MIDDLE : 0;
            mobj_t *height_unit = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){128, 128}, 0); CHECK(height_unit);
            if (flight) height_unit->traits |= MF_FLY;
            ticks(1, &(hudtext_t){0});
            CHECK(!!P_HasMoveOrder(height_unit) == !excluded_height);
            free(trig); unload();
        }
    for (int condition = 2; condition <= 3; condition++) {
        trig = blank_trigger(); CHECK(trig);
        put_u32(trig, 64); put_u32(trig + 8, 1); put_u16(trig + 12, MT_MARINE - 1);
        trig[14] = 10; trig[15] = (uint8_t)condition; action = trig + 320;
        action[26] = 26; action[27] = 8; put_u32(action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-order-trigger.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-order-trigger.chk", &level));
        sc_mission_t *height_mission = level.mission; height_mission->loc_count = 64;
        height_mission->loc_elevation[63] = 8; /* exclude low ground */
        if (!height_mission->terrain) height_mission->terrain = calloc((size_t)level.width * level.height, sizeof(*height_mission->terrain));
        CHECK(height_mission->terrain);
        sc_terrain_t *tile = &height_mission->terrain[L_Index(&level, 4, 4)];
        memset(tile->vf4, 0, sizeof(tile->vf4));
        CHECK(sc_spawn_actor(MT_MARINE - 1, (ivec2_t){128, 128}, 0));
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[0][0] == (condition == 2 ? 10 : 0));
        free(trig); unload();
    }
    /* Command ignores hallucinations; Bring includes visible illusions. */
    const int illusion_ops[] = {0, 1, 10};
    for (int condition = 2; condition <= 3; condition++) for (int op = 0; op < 3; op++) {
        trig = blank_trigger(); CHECK(trig); put_u32(trig, 1);
        put_u32(trig + 8, illusion_ops[op] == 1 ? 0 : 1); put_u16(trig + 12, MT_MARINE - 1);
        trig[14] = illusion_ops[op]; trig[15] = (uint8_t)condition; action = trig + 320;
        action[26] = 26; action[27] = 8; put_u32(action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-order-trigger.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-order-trigger.chk", &level));
        sc_mission_t *illusion_mission = level.mission; illusion_mission->loc_count = 1;
        illusion_mission->loc_right[0] = illusion_mission->loc_bottom[0] = 256;
        mobj_t *illusion = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){128, 128}, 0); CHECK(illusion);
        sc_initial_hallucination(illusion); ticks(1, &(hudtext_t){0});
        bool expected = (condition == 2) == (illusion_ops[op] == 1);
        CHECK(level.player_resources[0][0] == (expected ? 10 : 0));
        free(trig); unload();
    }
    for (int condition = 2; condition <= 3; condition++) {
        trig = blank_trigger(); CHECK(trig); put_u32(trig, 1);
        put_u16(trig + 12, 4); trig[14] = 10; trig[15] = (uint8_t)condition; action = trig + 320; /* Goliath turret row. */
        action[26] = 26; action[27] = 8; put_u32(action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-order-trigger.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-order-trigger.chk", &level));
        sc_mission_t *turret_mission = level.mission; turret_mission->loc_count = 1;
        turret_mission->loc_right[0] = turret_mission->loc_bottom[0] = 256;
        mobj_t *turret = sc_spawn_actor(4, (ivec2_t){128, 128}, 0); CHECK(turret); turret->hp = 1;
        ticks(1, &(hudtext_t){0}); CHECK(level.player_resources[0][0] == 10);
        free(trig); unload();
    }
    /* Bring uses the transport's location for completed cargo; At Most omits it. */
    const int bring_ops[] = {0, 1, 10};
    for (int op_index = 0; op_index < 3; op_index++) for (int transport_inside = 0; transport_inside < 2; transport_inside++) {
        trig = blank_trigger(); CHECK(trig); put_u32(trig, 1);
        put_u32(trig + 8, bring_ops[op_index] == 1 ? 0 : 1);
        put_u16(trig + 12, MT_MARINE - 1); trig[14] = bring_ops[op_index]; trig[15] = 3;
        action = trig + 320; action[26] = 26; action[27] = 8; put_u32(action + 20, 10);
        CHECK(!write_map("/tmp/open-rts-sc-order-trigger.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-order-trigger.chk", &level));
        sc_mission_t *bring_mission = level.mission; bring_mission->loc_count = 1;
        bring_mission->loc_right[0] = bring_mission->loc_bottom[0] = 256;
        mobj_t *bring_ship = sc_spawn_actor(MT_DROPSHIP - 1, (ivec2_t){transport_inside ? 128 : 640, 128}, 0);
        mobj_t *bring_passenger = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){transport_inside ? 640 : 128, 128}, 0);
        CHECK(bring_ship && bring_passenger);
        bring_ship->sc.cargo[0] = bring_passenger->id;
        bring_passenger->sc.parent = bring_ship->id; bring_passenger->sc.flags |= SC_LOADED;
        ticks(1, &(hudtext_t){0});
        CHECK(level.player_resources[0][0] == (bring_ops[op_index] == 1 || transport_inside ? 10 : 0));
        free(trig); unload();
    }
    /* Stored Interceptors and ready silo missiles count only in completed Bring counts. */
    for (int hangar_type = 0; hangar_type < 3; hangar_type++) for (int op_index = 0; op_index < 3; op_index++)
        for (int hangar_inside = 0; hangar_inside < 2; hangar_inside++) {
            int child = hangar_type == 1 ? MT_NUCLEAR_MISSILE : MT_INTERCEPTOR;
            int quantity = hangar_type == 1 ? 1 : 2;
            trig = blank_trigger(); CHECK(trig); put_u32(trig, 1);
            put_u32(trig + 8, bring_ops[op_index] == 1 ? 0 : quantity);
            put_u16(trig + 12, child - 1); trig[14] = bring_ops[op_index]; trig[15] = 3;
            action = trig + 320; action[26] = 26; action[27] = 8; put_u32(action + 20, 10);
            CHECK(!write_map("/tmp/open-rts-sc-order-trigger.chk", owners, trig));
            CHECK(G_DoLoadLevel("/tmp/open-rts-sc-order-trigger.chk", &level));
            sc_mission_t *bring_mission = level.mission; bring_mission->loc_count = 1;
            bring_mission->loc_right[0] = bring_mission->loc_bottom[0] = 256;
            int parent = hangar_type == 1 ? MT_NUCLEAR_SILO : hangar_type == 2 ? MT_GANTRITHOR : MT_CARRIER;
            mobj_t *hangar = sc_spawn_actor(parent - 1,
                                             (ivec2_t){hangar_inside ? 128 : 640, 128}, 0);
            CHECK(hangar); hangar->sc.hangar = quantity;
            ticks(1, &(hudtext_t){0});
            CHECK(level.player_resources[0][0] == (bring_ops[op_index] == 1 || hangar_inside ? 10 : 0));
            free(trig); unload();
        }
    /* Completed counts omit active Zerg morphs, while At Most counts both. */
    const int morph_types[] = {MT_EGG, MT_COCOON, MT_HATCHERY, MT_HATCHERY};
    for (int morph = 0; morph < 4; morph++) for (int condition = 2; condition <= 3; condition++)
        for (int op_index = 0; op_index < 3; op_index++) {
            trig = blank_trigger(); CHECK(trig); put_u32(trig, 1);
            put_u32(trig + 8, bring_ops[op_index] == 1 ? 1 : 2);
            put_u16(trig + 12, morph_types[morph] - 1); trig[14] = bring_ops[op_index]; trig[15] = condition;
            action = trig + 320; action[26] = 26; action[27] = 8; put_u32(action + 20, 10);
            CHECK(!write_map("/tmp/open-rts-sc-order-trigger.chk", owners, trig));
            CHECK(G_DoLoadLevel("/tmp/open-rts-sc-order-trigger.chk", &level));
            sc_mission_t *morph_mission = level.mission; morph_mission->loc_count = 1;
            morph_mission->loc_right[0] = morph_mission->loc_bottom[0] = 256;
            mobj_t *morphing = sc_spawn_actor(morph_types[morph] - 1, (ivec2_t){128, 128}, 0);
            mobj_t *complete = sc_spawn_actor(morph_types[morph] - 1, (ivec2_t){192, 128}, 0);
            CHECK(morphing && complete);
            morphing->production = calloc(1, sizeof(*morphing->production)); CHECK(morphing->production);
            morphing->production->queue_count = 1;
            morphing->production->product_class = morph == 2 ? RTS_PRODUCT_BUILDING :
                morph == 3 ? RTS_PRODUCT_UPGRADE : RTS_PRODUCT_UNIT;
            ticks(1, &(hudtext_t){0});
            CHECK(level.player_resources[0][0] == (morph == 3 && bring_ops[op_index] != 1 ? 10 : 0));
            free(trig); unload();
        }
    remove("/tmp/open-rts-sc-order-trigger.chk");

    /* Move Unit teleports immediately; a count limits selected units. */
    for (int all = 0; all < 2; all++) {
        trig = blank_trigger(); CHECK(trig);
        trig[15] = 22; action = trig + 320;
        put_u32(action, 1); put_u32(action + 20, 2); put_u16(action + 24, MT_MARINE - 1);
        action[26] = 39; action[27] = all ? 0 : 1;
        CHECK(!write_map("/tmp/open-rts-sc-teleport.chk", owners, trig));
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-teleport.chk", &level));
        sc_mission_t *teleport_mission = level.mission; teleport_mission->loc_count = 2;
        teleport_mission->loc_right[0] = teleport_mission->loc_bottom[0] = 128;
        teleport_mission->loc_left[1] = teleport_mission->loc_top[1] = 448;
        teleport_mission->loc_right[1] = teleport_mission->loc_bottom[1] = 576;
        mobj_t *first = spawn(MT_MARINE - 1, 0), *second = spawn(MT_MARINE - 1, 0);
        CHECK(first && second);
        ticks(1, &(hudtext_t){0});
        CHECK(fixed2_near(fixed3_xy(first->core.position), FIXED2_LIT(16, 16), FIXED_LIT(0.01)));
        CHECK(!P_HasMoveOrder(first));
        if (all) CHECK(!fixed2_near(fixed3_xy(second->core.position), FIXED2_LIT(1, 1), FIXED_LIT(0.01)) &&
                       !fixed2_equal(fixed3_xy(first->core.position), fixed3_xy(second->core.position)));
        else CHECK(fixed2_near(fixed3_xy(second->core.position), FIXED2_LIT(1, 1), FIXED_LIT(0.01)));
        free(trig); unload();
    }
    remove("/tmp/open-rts-sc-teleport.chk");
    /* Move Unit source location 64 uses modified Anywhere bounds. */
    trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
    put_u32(action, 64); put_u32(action + 16, 0); put_u32(action + 20, 1);
    put_u16(action + 24, MT_MARINE - 1); action[26] = 39;
    CHECK(!write_map("/tmp/open-rts-sc-teleport-anywhere.chk", owners, trig));
    FILE *teleport_file = fopen("/tmp/open-rts-sc-teleport-anywhere.chk", "ab"); CHECK(teleport_file);
    uint8_t teleport_header[8] = {'M','R','G','N'}, teleport_locations[64 * 20] = {0};
    put_u32(teleport_header + 4, sizeof(teleport_locations));
    put_u32(teleport_locations, 448); put_u32(teleport_locations + 4, 448);
    put_u32(teleport_locations + 8, 512); put_u32(teleport_locations + 12, 512);
    put_u32(teleport_locations + 63 * 20, 128); put_u32(teleport_locations + 63 * 20 + 4, 128);
    put_u32(teleport_locations + 63 * 20 + 8, 160); put_u32(teleport_locations + 63 * 20 + 12, 160);
    CHECK(fwrite(teleport_header, 1, sizeof(teleport_header), teleport_file) == sizeof(teleport_header));
    CHECK(fwrite(teleport_locations, 1, sizeof(teleport_locations), teleport_file) == sizeof(teleport_locations));
    CHECK(!fclose(teleport_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-teleport-anywhere.chk", &level));
    mobj_t *source_inside = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){144, 144}, 0);
    mobj_t *source_outside = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){240, 144}, 0);
    CHECK(source_inside && source_outside);
    ticks(1, &(hudtext_t){0});
    CHECK(fixed2_near(fixed3_xy(source_inside->core.position), FIXED2_LIT(15, 15), FIXED_LIT(0.1)));
    CHECK(fixed2_near(fixed3_xy(source_outside->core.position), FIXED2_LIT(7.5, 4.5), FIXED_LIT(0.01)));
    free(trig); unload();

    /* Move Unit destination location 64 also uses its stored MRGN center. */
    trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
    put_u32(action, 1); put_u32(action + 16, 0); put_u32(action + 20, 64);
    put_u16(action + 24, MT_MARINE - 1); action[26] = 39;
    CHECK(!write_map("/tmp/open-rts-sc-teleport-anywhere.chk", owners, trig));
    teleport_file = fopen("/tmp/open-rts-sc-teleport-anywhere.chk", "ab"); CHECK(teleport_file);
    memset(teleport_locations, 0, sizeof(teleport_locations));
    put_u32(teleport_locations, 128); put_u32(teleport_locations + 4, 128);
    put_u32(teleport_locations + 8, 160); put_u32(teleport_locations + 12, 160);
    put_u32(teleport_locations + 63 * 20, 320); put_u32(teleport_locations + 63 * 20 + 4, 320);
    put_u32(teleport_locations + 63 * 20 + 8, 384); put_u32(teleport_locations + 63 * 20 + 12, 384);
    CHECK(fwrite(teleport_header, 1, sizeof(teleport_header), teleport_file) == sizeof(teleport_header));
    CHECK(fwrite(teleport_locations, 1, sizeof(teleport_locations), teleport_file) == sizeof(teleport_locations));
    CHECK(!fclose(teleport_file));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-teleport-anywhere.chk", &level));
    source_inside = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){144, 144}, 0);
    source_outside = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){240, 144}, 0);
    CHECK(source_inside && source_outside);
    ticks(1, &(hudtext_t){0});
    CHECK(fixed2_near(fixed3_xy(source_inside->core.position), FIXED2_LIT(11, 11), FIXED_LIT(0.1)));
    CHECK(fixed2_near(fixed3_xy(source_outside->core.position), FIXED2_LIT(7.5, 4.5), FIXED_LIT(0.01)));
    free(trig); unload();

    /* Missing MRGN Anywhere still falls back to the whole map. */
    trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
    put_u32(action, 64); put_u32(action + 16, 0); put_u32(action + 20, 64);
    put_u16(action + 24, MT_MARINE - 1); action[26] = 39;
    CHECK(!write_map("/tmp/open-rts-sc-teleport-anywhere.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-teleport-anywhere.chk", &level));
    source_inside = sc_spawn_actor(MT_MARINE - 1, (ivec2_t){144, 144}, 0); CHECK(source_inside);
    fixed2_t fallback_position = fixed3_xy(source_inside->core.position);
    ticks(1, &(hudtext_t){0});
    CHECK(!fixed2_near(fixed3_xy(source_inside->core.position), fallback_position, FIXED_LIT(0.1)));
    CHECK(fixed2_near(fixed3_xy(source_inside->core.position), FIXED2_LIT(16, 16), FIXED_LIT(0.1)));
    free(trig); unload(); remove("/tmp/open-rts-sc-teleport-anywhere.chk");

    /* Center View uses modified Anywhere bounds and keeps the missing-record fallback. */
    for (int modified = 0; modified < 2; modified++) {
        trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
        put_u32(action, 64); action[26] = 10;
        CHECK(!write_map("/tmp/open-rts-sc-center-anywhere.chk", owners, trig));
        if (modified) {
            FILE *center_file = fopen("/tmp/open-rts-sc-center-anywhere.chk", "ab"); CHECK(center_file);
            uint8_t center_header[8] = {'M','R','G','N'}, center_locations[64 * 20] = {0};
            put_u32(center_header + 4, sizeof(center_locations));
            put_u32(center_locations + 63 * 20, 320); put_u32(center_locations + 63 * 20 + 4, 320);
            put_u32(center_locations + 63 * 20 + 8, 384); put_u32(center_locations + 63 * 20 + 12, 384);
            CHECK(fwrite(center_header, 1, sizeof(center_header), center_file) == sizeof(center_header));
            CHECK(fwrite(center_locations, 1, sizeof(center_locations), center_file) == sizeof(center_locations));
            CHECK(!fclose(center_file));
        }
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-center-anywhere.chk", &level));
        ticks(1, &(hudtext_t){0});
        float expected_center = modified ? 11.0f : 16.0f;
        CHECK(level.has_camera && level.camera.x == expected_center && level.camera.y == expected_center);
        free(trig); unload();
    }
    remove("/tmp/open-rts-sc-center-anywhere.chk");
    /* A destination outside the level cannot place aircraft out of bounds. */
    trig = blank_trigger(); CHECK(trig);
    trig[15] = 22; action = trig + 320;
    put_u32(action, 64); put_u32(action + 20, 1); put_u16(action + 24, MT_WRAITH - 1);
    action[26] = 39; action[27] = 1;
    CHECK(!write_map("/tmp/open-rts-sc-teleport-edge.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-teleport-edge.chk", &level));
    sc_mission_t *edge_mission = level.mission; edge_mission->loc_count = 1;
    edge_mission->loc_left[0] = edge_mission->loc_top[0] = 1056;
    edge_mission->loc_right[0] = edge_mission->loc_bottom[0] = 1120;
    mobj_t *edge_flyer = spawn(MT_WRAITH - 1, 0); CHECK(edge_flyer);
    fixed3_t original_position = edge_flyer->core.position;
    ticks(1, &(hudtext_t){0});
    CHECK(fixed2_equal(fixed3_xy(edge_flyer->core.position), fixed3_xy(original_position)));
    free(trig); unload(); remove("/tmp/open-rts-sc-teleport-edge.chk");

    /* A grounded building without links can be moved and reblocks its new site. */
    trig = blank_trigger(); CHECK(trig); trig[15] = 22; action = trig + 320;
    put_u32(action, 1); put_u32(action + 20, 2); put_u16(action + 24, MT_BARRACKS - 1);
    action[26] = 39; action[27] = 1;
    CHECK(!write_map("/tmp/open-rts-sc-teleport-building.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-teleport-building.chk", &level));
    sc_mission_t *building_mission = level.mission; building_mission->loc_count = 2;
    building_mission->loc_right[0] = building_mission->loc_bottom[0] = 256;
    building_mission->loc_left[1] = building_mission->loc_top[1] = 448;
    building_mission->loc_right[1] = building_mission->loc_bottom[1] = 576;
    mobj_t *moved_building = sc_spawn_actor(MT_BARRACKS - 1, (ivec2_t){128, 128}, 0);
    CHECK(moved_building); P_SyncBuildingBlocking();
    irect_t before_building = P_MobjCells(moved_building);
    CHECK(level.cell_solid[L_Index(&level, before_building.x, before_building.y)] & 2);
    ticks(1, &(hudtext_t){0});
    irect_t after_building = P_MobjCells(moved_building);
    CHECK(!ivec2_equal((ivec2_t){before_building.x, before_building.y}, (ivec2_t){after_building.x, after_building.y}));
    CHECK(!(level.cell_solid[L_Index(&level, before_building.x, before_building.y)] & 2));
    CHECK(level.cell_solid[L_Index(&level, after_building.x, after_building.y)] & 2);
    unload();
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-teleport-building.chk", &level));
    building_mission = level.mission; building_mission->loc_count = 2;
    building_mission->loc_right[0] = building_mission->loc_bottom[0] = 256;
    building_mission->loc_left[1] = building_mission->loc_top[1] = 448;
    building_mission->loc_right[1] = building_mission->loc_bottom[1] = 576;
    moved_building = sc_spawn_actor(MT_BARRACKS - 1, (ivec2_t){128, 128}, 0);
    CHECK(moved_building); P_SyncBuildingBlocking();
    before_building = P_MobjCells(moved_building);
    level.blocked[L_Index(&level, after_building.x, after_building.y)] = 1;
    ticks(1, &(hudtext_t){0});
    CHECK(ivec2_equal((ivec2_t){before_building.x, before_building.y},
                      (ivec2_t){P_MobjCells(moved_building).x, P_MobjCells(moved_building).y}));
    unload();
    put_u16(action + 24, MT_FACTORY - 1);
    CHECK(!write_map("/tmp/open-rts-sc-teleport-building.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-teleport-building.chk", &level));
    building_mission = level.mission; building_mission->loc_count = 2;
    building_mission->loc_right[0] = building_mission->loc_bottom[0] = 256;
    building_mission->loc_left[1] = building_mission->loc_top[1] = 448;
    building_mission->loc_right[1] = building_mission->loc_bottom[1] = 576;
    moved_building = sc_spawn_actor(MT_FACTORY - 1, (ivec2_t){128, 128}, 0);
    mobj_t *attached_shop = sc_spawn_actor(MT_MACHINE_SHOP - 1, (ivec2_t){256, 128}, 0);
    CHECK(moved_building && attached_shop);
    moved_building->sc.addon = attached_shop->id; attached_shop->sc.parent = moved_building->id;
    P_SyncBuildingBlocking(); before_building = P_MobjCells(moved_building);
    ticks(1, &(hudtext_t){0});
    CHECK(!ivec2_equal((ivec2_t){before_building.x, before_building.y},
                       (ivec2_t){P_MobjCells(moved_building).x, P_MobjCells(moved_building).y}));
    CHECK(!moved_building->sc.addon && !attached_shop->sc.parent && attached_shop->owner == 11);
    unload();
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-teleport-building.chk", &level));
    building_mission = level.mission; building_mission->loc_count = 2;
    building_mission->loc_right[0] = building_mission->loc_bottom[0] = 256;
    building_mission->loc_left[1] = building_mission->loc_top[1] = 448;
    building_mission->loc_right[1] = building_mission->loc_bottom[1] = 576;
    moved_building = sc_spawn_actor(MT_FACTORY - 1, (ivec2_t){128, 128}, 0);
    CHECK(moved_building); attached_shop = sc_attach_addon(moved_building, MT_MACHINE_SHOP);
    CHECK(attached_shop);
    ivec2_t destination_cell = {14, 15}; uint16_t addon_type; irect_t addon_rect;
    CHECK(sc_addon_place(MT_FACTORY, destination_cell, &addon_type, &addon_rect));
    fixed2_t addon_position = P_BuildingPosition(addon_type, (ivec2_t){addon_rect.x, addon_rect.y});
    mobj_t *new_shop = sc_spawn_actor(addon_type - 1,
        (ivec2_t){addon_position.x >> 11, addon_position.y >> 11}, 11);
    CHECK(new_shop); P_SyncBuildingBlocking();
    ticks(1, &(hudtext_t){0});
    CHECK(!attached_shop->sc.parent && attached_shop->owner == 11);
    CHECK(moved_building->sc.addon == new_shop->id && new_shop->sc.parent == moved_building->id && new_shop->owner == 0);
    unload();
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-teleport-building.chk", &level));
    building_mission = level.mission; building_mission->loc_count = 2;
    building_mission->loc_right[0] = building_mission->loc_bottom[0] = 256;
    building_mission->loc_left[1] = building_mission->loc_top[1] = 448;
    building_mission->loc_right[1] = building_mission->loc_bottom[1] = 576;
    moved_building = sc_spawn_actor(MT_FACTORY - 1, (ivec2_t){128, 128}, 0);
    CHECK(moved_building); attached_shop = sc_attach_addon(moved_building, MT_MACHINE_SHOP);
    CHECK(attached_shop); before_building = P_MobjCells(moved_building);
    level.blocked[L_Index(&level, destination_cell.x, destination_cell.y)] = 1;
    ticks(1, &(hudtext_t){0});
    CHECK(ivec2_equal((ivec2_t){before_building.x, before_building.y},
                      (ivec2_t){P_MobjCells(moved_building).x, P_MobjCells(moved_building).y}));
    CHECK(moved_building->sc.addon == attached_shop->id && attached_shop->sc.parent == moved_building->id);
    unload();
    put_u16(action + 24, MT_MACHINE_SHOP - 1);
    CHECK(!write_map("/tmp/open-rts-sc-teleport-building.chk", owners, trig));
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-teleport-building.chk", &level));
    building_mission = level.mission; building_mission->loc_count = 2;
    building_mission->loc_right[0] = building_mission->loc_bottom[0] = 256;
    building_mission->loc_left[1] = building_mission->loc_top[1] = 448;
    building_mission->loc_right[1] = building_mission->loc_bottom[1] = 576;
    moved_building = sc_spawn_actor(MT_FACTORY - 1, (ivec2_t){128, 128}, 0);
    CHECK(moved_building); attached_shop = sc_attach_addon(moved_building, MT_MACHINE_SHOP);
    CHECK(attached_shop); before_building = P_MobjCells(attached_shop);
    P_SyncBuildingBlocking(); ticks(1, &(hudtext_t){0});
    CHECK(!ivec2_equal((ivec2_t){before_building.x, before_building.y},
                       (ivec2_t){P_MobjCells(attached_shop).x, P_MobjCells(attached_shop).y}));
    CHECK(!moved_building->sc.addon && !attached_shop->sc.parent && attached_shop->owner == 11);
    unload();
    CHECK(G_DoLoadLevel("/tmp/open-rts-sc-teleport-building.chk", &level));
    building_mission = level.mission; building_mission->loc_count = 2;
    building_mission->loc_right[0] = building_mission->loc_bottom[0] = 256;
    building_mission->loc_left[1] = building_mission->loc_top[1] = 448;
    building_mission->loc_right[1] = building_mission->loc_bottom[1] = 576;
    moved_building = sc_spawn_actor(MT_FACTORY - 1, (ivec2_t){128, 128}, 0);
    CHECK(moved_building); attached_shop = sc_attach_addon(moved_building, MT_MACHINE_SHOP);
    CHECK(attached_shop); before_building = P_MobjCells(attached_shop);
    ivec2_t blocked_shop = ivec2_sub((ivec2_t){16, 16}, (ivec2_t){attached_shop->info->footprint.w / 2, attached_shop->info->footprint.h / 2});
    level.blocked[L_Index(&level, blocked_shop.x, blocked_shop.y)] = 1;
    P_SyncBuildingBlocking(); ticks(1, &(hudtext_t){0});
    CHECK(ivec2_equal((ivec2_t){before_building.x, before_building.y},
                      (ivec2_t){P_MobjCells(attached_shop).x, P_MobjCells(attached_shop).y}));
    CHECK(moved_building->sc.addon == attached_shop->id && attached_shop->sc.parent == moved_building->id && attached_shop->owner == 0);
    unload();
    put_u16(action + 24, MT_BARRACKS - 1);
    CHECK(!write_map("/tmp/open-rts-sc-teleport-building.chk", owners, trig));
    for (int terrain = 0; terrain < 2; terrain++) {
        CHECK(G_DoLoadLevel("/tmp/open-rts-sc-teleport-building.chk", &level));
        building_mission = level.mission; building_mission->loc_count = 2;
        building_mission->loc_right[0] = building_mission->loc_bottom[0] = 256;
        building_mission->loc_left[1] = building_mission->loc_right[1] = 160;
        building_mission->loc_top[1] = building_mission->loc_bottom[1] = 112;
        moved_building = sc_spawn_actor(MT_BARRACKS - 1, (ivec2_t){128, 128}, 0);
        CHECK(moved_building); P_SyncBuildingBlocking(); before_building = P_MobjCells(moved_building);
        int overlap_index = L_Index(&level, before_building.x + 1, before_building.y);
        if (terrain) level.cell_solid[overlap_index] |= 1;
        ticks(1, &(hudtext_t){0});
        after_building = P_MobjCells(moved_building);
        CHECK(after_building.x == before_building.x + (terrain ? 0 : 1) && after_building.y == before_building.y);
        if (terrain) CHECK(level.cell_solid[overlap_index] & 1);
        unload();
    }
    free(trig); remove("/tmp/open-rts-sc-teleport-building.chk");

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
