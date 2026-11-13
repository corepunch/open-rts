#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) RTS_CHECK(c, "StarCraft trigger coverage", #c)

static void put_u16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, unsigned v) { put_u16(p, v); put_u16(p + 2, v >> 16); }
static unsigned get_u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static const char *path = "/private/tmp/sc-trigger-coverage.chk";

static int write_chunk(FILE *f, const char *tag, const void *data, size_t size) {
    uint8_t hdr[8];
    memcpy(hdr, tag, 4); put_u32(hdr + 4, (unsigned)size);
    return fwrite(hdr, 1, 8, f) != 8 || (size && fwrite(data, 1, size, f) != size);
}

/* One 32x32 map, owners human (0) and computer (1), the given TRIG records
 * and, optionally, MRGN locations. */
static int write_map(const uint8_t *trig, size_t triggers, const uint8_t *locations, size_t count) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    uint8_t dim[4], era[2] = {0}, terrain[32 * 32 * 2] = {0}, owners[12] = {6, 5};
    put_u16(dim, 32); put_u16(dim + 2, 32);
    int bad = write_chunk(f, "DIM ", dim, 4) || write_chunk(f, "ERA ", era, 2) ||
        write_chunk(f, "MTXM", terrain, sizeof(terrain)) || write_chunk(f, "OWNR", owners, 12) ||
        (trig && write_chunk(f, "TRIG", trig, triggers * 2400)) ||
        (count && write_chunk(f, "MRGN", locations, count * 20));
    return fclose(f) || bad;
}

/* A trigger for player 0 with an Always condition and no actions. */
static uint8_t *trigger(void) {
    uint8_t *t = calloc(1, 2400);
    if (t) { t[2372] = 1; t[15] = 22; }
    return t;
}

static void action(uint8_t *t, int i, unsigned type, unsigned modifier, unsigned loc,
                   unsigned text, unsigned time, unsigned player, unsigned number, unsigned unit) {
    uint8_t *a = t + 320 + i * 32;
    put_u32(a, loc); put_u32(a + 4, text); put_u32(a + 12, time);
    put_u32(a + 16, player); put_u32(a + 20, number); put_u16(a + 24, unit);
    a[26] = (uint8_t)type; a[27] = (uint8_t)modifier;
}

static void tick(int n) {
    hudtext_t hud = {0};
    while (n--) G_MissionTicker(&level, NULL, &(int){0}, &hud, RTS_TICK_MS);
}

static bool load(uint8_t *t, const uint8_t *locations, size_t count) {
    bool ok = !write_map(t, t ? 1 : 0, locations, count) && G_DoLoadLevel(path, &level) &&
        level.mission;
    free(t);
    return ok;
}

static void unload(void) {
    P_FreeLevel(&level);
    menuleave = false; menuactive = false; netgame = false;
}

static mobj_t *spawn(int native, int owner) {
    mobj_t *u = P_SpawnMobj((fixed3_t){(native + 1) * FIXED_ONE, (owner + 1) * FIXED_ONE, 0}, (uint16_t)(native + 1));
    if (u) {
        u->owner = u->team = (uint8_t)owner;
        u->allegiance = owner == consoleplayer ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
    }
    return u;
}

/* The inventory in docs/SC_EXE_FINDINGS.md: every action and condition type
 * in the installed campaign CHKs is classified, and the unsupported ones are
 * exactly Pause Game, Unpause Game and Set Doodad State. */
static unsigned action_count[256], condition_count[256];
static int scan_chk(const char *file) {
    FILE *f = fopen(file, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    uint8_t *bytes = malloc((size_t)size);
    if (!bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size) { free(bytes); fclose(f); return 0; }
    fclose(f);
    for (long at = 0; at + 8 <= size;) {
        unsigned n = get_u32(bytes + at + 4);
        if (!memcmp(bytes + at, "TRIG", 4)) for (unsigned off = 0; off + 2400 <= n; off += 2400) {
            const uint8_t *t = bytes + at + 8 + off;
            for (int c = 0; c < 16 && t[c * 20 + 15]; c++) condition_count[t[c * 20 + 15]]++;
            for (int a = 0; a < 64 && t[320 + a * 32 + 26]; a++) action_count[t[320 + a * 32 + 26]]++;
        }
        at += 8 + n;
    }
    free(bytes);
    return 1;
}

static int inventory(void) {
    static const char *races[] = {"terran", "zerg", "protoss"};
    static const char *folders[] = {"tutorial", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12"};
    int files = 0;
    for (int r = 0; r < 3; r++) for (int i = 0; i < 13; i++) {
        char file[256];
        snprintf(file, sizeof(file), "data/STARCRAFT/install/campaign/%s/%s%s/staredit/scenario.chk",
                 races[r], races[r], folders[i]);
        if (i == 0) snprintf(file, sizeof(file), "data/STARCRAFT/install/campaign/%s/tutorial/staredit/scenario.chk", races[r]);
        files += scan_chk(file);
    }
    CHECK(files == 35);
    for (int id = 0; id < 256; id++) {
        if (action_count[id]) CHECK(id < SC_TRIG_ACTION_TYPES);
        if (condition_count[id]) CHECK(id < SC_TRIG_CONDITION_TYPES);
        bool unsupported = action_count[id] && sc_trigger_action_status(id) == SC_TRIG_UNSUPPORTED;
        CHECK(unsupported == (id == 5 || id == 6 || id == 42));
        CHECK(!condition_count[id] || sc_trigger_condition_status(id) != SC_TRIG_UNSUPPORTED);
    }
    CHECK(action_count[5] == 81 && action_count[6] == 23 && action_count[42] == 69);
    CHECK(action_count[15] == 9 && action_count[16] == 89 && action_count[17] == 1 &&
          action_count[19] == 1 && action_count[32] == 2 && action_count[29] == 0);
    return 0;
}

static int reports(void) {
    /* One report per type however often it runs, including unknown types. */
    uint8_t *t = trigger();
    CHECK(t);
    action(t, 0, 3, 0, 0, 0, 0, 0, 0, 0);
    action(t, 1, 5, 0, 0, 0, 0, 0, 0, 0);
    action(t, 2, 6, 0, 0, 0, 0, 0, 0, 0);
    action(t, 3, 42, 5, 0, 0, 0, 17, 0, 207);
    action(t, 4, 5, 0, 0, 0, 0, 0, 0, 0);
    action(t, 5, 200, 0, 0, 0, 0, 0, 0, 0);
    action(t, 6, 54, 0, 0, 0, 0, 0, 0, 0);
    CHECK(load(t, NULL, 0));
    sc_mission_t *m = level.mission;
    CHECK(!m->trigger_warnings);
    tick(3);
    CHECK(m->trigger_warnings == 4);
    CHECK(sc_trigger_reported(m, true, 5) && sc_trigger_reported(m, true, 6) &&
          sc_trigger_reported(m, true, 42) && sc_trigger_reported(m, true, 200));
    CHECK(!sc_trigger_reported(m, true, 54) && !sc_trigger_reported(m, true, 3));
    unload();

    /* A disabled action is skipped, not reported; a supported one is silent. */
    t = trigger();
    CHECK(t);
    action(t, 0, 5, 0, 0, 0, 0, 0, 0, 0);
    t[320 + 28] = 2;
    action(t, 1, 13, 4, 0, 0, 0, 0, 1, 0);
    CHECK(load(t, NULL, 0));
    tick(2);
    CHECK(!((sc_mission_t *)level.mission)->trigger_warnings);
    unload();

    /* Conditions: Mission Briefing (13) and an unknown type are reported and
     * never satisfied. */
    for (unsigned type = 13; type <= 99; type += 86) {
        t = trigger();
        CHECK(t);
        t[15] = (uint8_t)type;
        action(t, 0, 13, 4, 0, 0, 0, 0, 3, 0);
        CHECK(load(t, NULL, 0));
        tick(2);
        m = level.mission;
        CHECK(m->trigger_warnings == 1 && sc_trigger_reported(m, false, type));
        CHECK(!m->switches[3]);
        unload();
    }
    return 0;
}

static int leaderboards(void) {
    uint8_t loc[2 * 20] = {0};
    put_u32(loc + 8, 1023); put_u32(loc + 12, 1023);     /* Location 1: the map. */
    put_u32(loc + 20, 900); put_u32(loc + 24, 900);
    put_u32(loc + 28, 1000); put_u32(loc + 32, 1000);    /* Location 2: an empty corner. */
    struct { unsigned type, loc, unit, number; int64_t expect[2]; } rows[] = {
        {17, 0, MT_MARINE - 1, 0, {3, 2}},
        {18, 1, MT_MARINE - 1, 0, {3, 2}},
        {18, 2, MT_MARINE - 1, 0, {0, 0}},
        {19, 0, 0, 0, {111, 222}},   /* Ore. */
        {19, 0, 1, 0, {7, 9}},       /* Gas. */
        {19, 0, 2, 0, {118, 231}},   /* Both. */
        {20, 0, MT_MARINE - 1, 0, {5, 6}},
        {21, 0, 7, 0, {44, 55}},     /* Custom score. */
        {33, 0, MT_MARINE - 1, 25, {3, 2}},
        {34, 1, MT_MARINE - 1, 25, {3, 2}},
        {35, 0, 0, 500, {111, 222}},
        {36, 0, MT_MARINE - 1, 25, {5, 6}},
        {37, 0, 7, 25, {44, 55}},
        {40, 0, 0, 7500, {0, 0}},
    };
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        uint8_t *t = trigger();
        CHECK(t);
        action(t, 0, rows[i].type, 0, rows[i].loc, 77, 0, 0, rows[i].number, rows[i].unit);
        CHECK(load(t, loc, 2));
        consoleplayer = 0;
        for (int n = 0; n < 3; n++) CHECK(spawn(MT_MARINE - 1, 0));
        for (int n = 0; n < 2; n++) CHECK(spawn(MT_MARINE - 1, 1));
        sc_mission_t *m = level.mission;
        level.player_resources[0][0] = 111; level.player_resources[1][0] = 222;
        level.player_resources[0][1] = 7; level.player_resources[1][1] = 9;
        m->kills[0][MT_MARINE - 1] = 5; m->kills[1][MT_MARINE - 1] = 6;
        m->custom_score[0] = 44; m->custom_score[1] = 55;
        CHECK(!sc_leaderboard());
        tick(1);
        const sc_leaderboard_t *lb = sc_leaderboard();
        CHECK(lb && lb->kind != SC_LB_NONE);
        CHECK(lb->value[0] == rows[i].expect[0] && lb->value[1] == rows[i].expect[1]);
        CHECK(lb->value[2] == 0 && lb->title == (rows[i].type == 40 ? 0 : 77));
        CHECK(lb->goal == (rows[i].type >= 33) && lb->quantity == (rows[i].type >= 33 ? rows[i].number : 0));
        CHECK(lb->location == (rows[i].type == 18 || rows[i].type == 34 ? rows[i].loc : 0));
        /* Values follow the game rather than the action's moment. */
        level.player_resources[0][0] += 5;
        tick(1);
        CHECK(sc_leaderboard()->value[0] == rows[i].expect[0] +
              (rows[i].type == 19 && rows[i].unit != 1 ? 5 : rows[i].type == 35 ? 5 : 0));
        unload();
    }

    /* The latest leaderboard replaces the active one; Computer Players
     * stores Set, Clear and Toggle, and is separate from the leaderboard. */
    uint8_t *t = trigger();
    CHECK(t);
    action(t, 0, 17, 0, 0, 5, 0, 0, 0, MT_MARINE - 1);
    action(t, 1, 32, 4, 0, 0, 0, 0, 0, 0);
    action(t, 2, 20, 0, 0, 6, 0, 0, 0, MT_MARINE - 1);
    action(t, 3, 32, 6, 0, 0, 0, 0, 0, 0);
    action(t, 4, 32, 6, 0, 0, 0, 0, 0, 0);
    action(t, 5, 32, 5, 0, 0, 0, 0, 0, 0);
    CHECK(load(t, NULL, 0));
    tick(1);
    const sc_leaderboard_t *lb = sc_leaderboard();
    CHECK(lb && lb->kind == SC_LB_KILLS && lb->title == 6);
    CHECK(!((sc_mission_t *)level.mission)->leaderboard_computers);
    unload();
    t = trigger();
    CHECK(t);
    action(t, 0, 32, 4, 0, 0, 0, 0, 0, 0);
    CHECK(load(t, NULL, 0));
    tick(1);
    CHECK(((sc_mission_t *)level.mission)->leaderboard_computers && !sc_leaderboard());
    unload();
    return 0;
}

static int portrait(void) {
    uint8_t *t = trigger();
    CHECK(t);
    action(t, 0, 29, 0, 0, 0, 100, 0, 0, MT_MARINE - 1);
    CHECK(load(t, NULL, 0));
    uint16_t unit = 0;
    int ms = 0;
    CHECK(!sc_talking_portrait(&unit, &ms));
    tick(1);
    CHECK(sc_talking_portrait(&unit, &ms) && unit == MT_MARINE - 1 && ms == 100);
    tick(3);
    CHECK(sc_talking_portrait(&unit, &ms) && ms == 100 - 3 * RTS_TICK_MS);
    tick(1);
    CHECK(!sc_talking_portrait(&unit, &ms));
    CHECK(!((sc_mission_t *)level.mission)->portrait_unit);
    unload();
    /* A zero duration shows nothing. */
    t = trigger();
    CHECK(t);
    action(t, 0, 29, 0, 0, 0, 0, 0, 0, MT_MARINE - 1);
    CHECK(load(t, NULL, 0));
    tick(1);
    CHECK(!sc_talking_portrait(NULL, NULL));
    unload();
    return 0;
}

static int ai_scripts(void) {
    /* The script and location are kept for the trigger owner. Shared vision
     * scripts are native and not reported; the rest report their fallback
     * once per script ID. */
    uint8_t *t = trigger();
    CHECK(t);
    action(t, 0, 3, 0, 0, 0, 0, 0, 0, 0);
    action(t, 1, 16, 0, 9, 0, 0, 0, 0x41756c56, 0);   /* VluA at location 9. */
    action(t, 2, 15, 0, 0, 0, 0, 0, 0x63697553, 0);   /* Suic. */
    action(t, 3, 15, 0, 0, 0, 0, 0, 0x3169562b, 0);   /* +Vi1. */
    CHECK(load(t, NULL, 0));
    sc_mission_t *m = level.mission;
    tick(1);
    CHECK(m->ai_script[0].id == 0x3169562b && m->ai_script[0].location == 0 && m->ai_script[0].runs == 3);
    CHECK(m->ai_fallback_count == 2 && m->ai_fallback[0] == 0x41756c56 && m->ai_fallback[1] == 0x63697553);
    CHECK(m->trigger_warnings == 2);
    tick(2);
    CHECK(m->ai_fallback_count == 2 && m->trigger_warnings == 2 && m->ai_script[0].runs == 9);
    CHECK(m->ai_script[1].id == 0 && sc_player_ai(0) && !sc_player_ai(1));
    unload();
    t = trigger();
    CHECK(t);
    action(t, 0, 16, 0, 20, 0, 0, 0, 0x43726c43, 0);   /* ClrC at location 20. */
    CHECK(load(t, NULL, 0));
    tick(1);
    m = level.mission;
    CHECK(m->ai_script[0].id == 0x43726c43 && m->ai_script[0].location == 20 && m->ai_script[0].runs == 1);
    unload();
    return 0;
}

static int save_state(void) {
    uint8_t loc[20] = {0};
    put_u32(loc + 8, 1023); put_u32(loc + 12, 1023);
    uint8_t *t = trigger();
    CHECK(t);
    action(t, 0, 18, 0, 1, 31, 0, 0, 0, MT_MARINE - 1);
    action(t, 1, 29, 0, 0, 0, 5000, 0, 0, MT_MARINE - 1);
    action(t, 2, 32, 4, 0, 0, 0, 0, 0, 0);
    action(t, 3, 16, 0, 12, 0, 0, 0, 0x41756c56, 0);
    CHECK(load(t, loc, 1));
    consoleplayer = 0;
    for (int n = 0; n < 4; n++) CHECK(spawn(MT_MARINE - 1, 0));
    tick(2);
    sc_mission_t *m = level.mission;
    CHECK(m->leaderboard.value[0] == 4 && m->portrait_ms == 5000 - RTS_TICK_MS);
    uint32_t hash = G_ConsistencyExtra(0);
    size_t size = G_SaveExtraSize();
    void *blob = malloc(size);
    CHECK(blob);
    G_SaveExtra(blob);

    sc_mission_t before = *m;
    m->leaderboard = (sc_leaderboard_t){0};
    m->leaderboard_computers = false;
    m->portrait_unit = 0; m->portrait_ms = 0;
    memset(m->ai_script, 0, sizeof(m->ai_script));
    CHECK(G_ConsistencyExtra(0) != hash);
    CHECK(G_LoadExtra(blob, size));
    m = level.mission;
    CHECK(G_ConsistencyExtra(0) == hash);
    CHECK(!memcmp(&m->leaderboard, &before.leaderboard, sizeof(before.leaderboard)));
    CHECK(m->leaderboard.kind == SC_LB_CONTROL_AT && m->leaderboard.title == 31 &&
          m->leaderboard.location == 1 && m->leaderboard.value[0] == 4);
    CHECK(m->leaderboard_computers && m->portrait_unit == MT_MARINE - 1 && m->portrait_ms == before.portrait_ms);
    CHECK(m->ai_script[0].id == 0x41756c56 && m->ai_script[0].location == 12 && m->ai_script[0].runs == 1);
    CHECK(m->ai_fallback_count == 1 && sc_trigger_reported(m, true, 5) == false);
    /* Each field changes the consistency hash. */
    m->leaderboard.value[3]++; CHECK(G_ConsistencyExtra(0) != hash); m->leaderboard.value[3]--;
    m->leaderboard_computers = false; CHECK(G_ConsistencyExtra(0) != hash); m->leaderboard_computers = true;
    m->ai_script[2].location = 3; CHECK(G_ConsistencyExtra(0) != hash); m->ai_script[2].location = 0;
    m->portrait_ms++; CHECK(G_ConsistencyExtra(0) != hash); m->portrait_ms--;
    CHECK(G_ConsistencyExtra(0) == hash);

    /* Malformed records are rejected without changing the mission. */
    sc_mission_t *saved = (sc_mission_t *)blob;
    CHECK(G_SaveExtraSize() == size);
    sc_leaderboard_t good = saved->leaderboard;
    saved->leaderboard.kind = 99;
    CHECK(!G_LoadExtra(blob, size));
    saved->leaderboard = good;
    saved->portrait_ms = -1;
    CHECK(!G_LoadExtra(blob, size));
    saved->portrait_ms = 0; saved->portrait_unit = 4;
    CHECK(!G_LoadExtra(blob, size));
    saved->portrait_unit = 0;
    saved->ai_fallback_count = SC_AI_FALLBACK_LOG + 1;
    CHECK(!G_LoadExtra(blob, size));
    saved->ai_fallback_count = 1;
    CHECK(G_LoadExtra(blob, size) && G_ConsistencyExtra(0) != hash);
    free(blob);
    unload();
    return 0;
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    consoleplayer = 0;
    if (inventory() || reports() || leaderboards() || portrait() || ai_scripts() || save_state()) return 1;
    remove(path);
    puts("PASS: trigger coverage, leaderboards, portrait, AI scripts and saves");
    return 0;
}
