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
 * in the installed campaign CHKs is classified, and none is unsupported (Set
 * Doodad State is partial: traps only). */
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
        CHECK(!unsupported);
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
    action(t, 1, 58, 0, 0, 0, 0, 0, 0, 0);
    action(t, 2, 59, 0, 0, 0, 0, 0, 0, 0);
    action(t, 3, 57, 5, 0, 0, 0, 17, 0, 207);
    action(t, 4, 58, 0, 0, 0, 0, 0, 0, 0);
    action(t, 5, 200, 0, 0, 0, 0, 0, 0, 0);
    action(t, 6, 54, 0, 0, 0, 0, 0, 0, 0);
    CHECK(load(t, NULL, 0));
    sc_mission_t *m = level.mission;
    CHECK(!m->trigger_warnings);
    tick(3);
    CHECK(m->trigger_warnings == 3);
    CHECK(sc_trigger_reported(m, true, 58) && sc_trigger_reported(m, true, 59) &&
          sc_trigger_reported(m, true, 200));
    CHECK(!sc_trigger_reported(m, true, 54) && !sc_trigger_reported(m, true, 3));
    unload();

    /* A disabled action is skipped, not reported; a supported one is silent. */
    t = trigger();
    CHECK(t);
    action(t, 0, 58, 0, 0, 0, 0, 0, 0, 0);
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

static mobj_t *spawn_at(int type_id, int owner, int cx, int cy) {
    mobj_t *u = P_SpawnMobj((fixed3_t){cx * FIXED_ONE, cy * FIXED_ONE, 0}, (uint16_t)type_id);
    if (u) {
        u->owner = u->team = (uint8_t)owner;
        u->allegiance = owner == consoleplayer ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
    }
    return u;
}

/* One map with a Set Doodad State action over location 1 (the whole map) for
 * unit `unit`, All Players, with the given state. */
static bool doodad_map(unsigned loc, unsigned state, unsigned unit) {
    uint8_t whole[20] = {0};
    put_u32(whole + 8, 1023); put_u32(whole + 12, 1023);
    uint8_t *t = trigger();
    if (!t) return false;
    action(t, 0, 42, state, loc, 0, 0, 17, 0, unit);
    return load(t, whole, 1);
}

enum { TRAP_FLOOR_GUN = 210, DOOR_LEFT_PIT = 208 };

/* Set Doodad State (action 42, 0x4ae7b0): decoding, selection and the
 * disabled trap. Raw state 0, which the campaigns use 62 times, toggles like
 * 6; 4 enables and 5 disables; 1-3 and values above 6 do nothing; no
 * location does nothing; only the first matching unit is affected. */
static int doodads(void) {
    /* Disable (5): the trap stops shooting. A marine next to an enabled trap
     * is hurt; next to a disabled one it is not. */
    for (int pass = 0; pass < 2; pass++) {
        CHECK(pass ? doodad_map(1, 5, TRAP_FLOOR_GUN - 1) : doodad_map(1, 2, TRAP_FLOOR_GUN - 1));
        sc_mission_t *m = level.mission;
        mobj_t *trap = spawn_at(TRAP_FLOOR_GUN, 1, 5, 5), *marine = spawn_at(MT_MARINE, 0, 7, 5);
        CHECK(trap && marine);
        int full = marine->hp;
        tick(1);
        CHECK(sc_doodad_disabled(trap) == (pass == 1) && sc_disabled(trap) == (pass == 1));
        CHECK(m->doodad_off_count == pass);
        for (int n = 0; n < 300; n++) P_Ticker();
        if (pass) CHECK(marine->hp == full);
        else CHECK(marine->hp < full);
        unload();
    }

    /* Raw 0 and 6 toggle, 4 enables, 5 disables; the rest do nothing. */
    static const struct { unsigned state; bool before, after; } cases[] = {
        {0, false, true}, {0, true, false}, {6, false, true}, {6, true, false},
        {4, true, false}, {4, false, false}, {5, false, true}, {5, true, true},
        {1, false, false}, {2, true, true}, {3, false, false}, {7, true, true}, {255, false, false},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK(doodad_map(1, cases[i].state, TRAP_FLOOR_GUN - 1));
        mobj_t *trap = spawn_at(TRAP_FLOOR_GUN, 1, 5, 5);
        CHECK(trap);
        sc_mission_t *m = level.mission;
        if (cases[i].before) m->doodad_off[m->doodad_off_count++] = trap->id;
        tick(1);
        CHECK(sc_doodad_disabled(trap) == cases[i].after);
        unload();
    }

    /* No location, a different unit type and a unit outside the location do
     * nothing; doors take another path and are left alone. */
    CHECK(doodad_map(0, 5, TRAP_FLOOR_GUN - 1));
    mobj_t *trap = spawn_at(TRAP_FLOOR_GUN, 1, 5, 5);
    tick(1);
    CHECK(trap && !sc_doodad_disabled(trap));
    unload();
    CHECK(doodad_map(1, 5, TRAP_FLOOR_GUN));
    trap = spawn_at(TRAP_FLOOR_GUN, 1, 5, 5);
    tick(1);
    CHECK(trap && !sc_doodad_disabled(trap));
    unload();
    CHECK(doodad_map(1, 5, DOOR_LEFT_PIT - 1));
    mobj_t *door = spawn_at(DOOR_LEFT_PIT, 1, 5, 5);
    tick(1);
    CHECK(door && !sc_doodad_disabled(door));
    unload();
    CHECK(doodad_map(1, 5, TRAP_FLOOR_GUN - 1));
    trap = spawn_at(TRAP_FLOOR_GUN, 1, 100, 100);
    tick(1);
    CHECK(trap && !sc_doodad_disabled(trap));
    unload();

    /* Only the first matching unit; an already disabled unit (lockdown) is
     * not flagged by Disable; Enable on a locked down unit clears only the
     * flag. */
    CHECK(doodad_map(1, 5, TRAP_FLOOR_GUN - 1));
    mobj_t *first = spawn_at(TRAP_FLOOR_GUN, 1, 5, 5), *second = spawn_at(TRAP_FLOOR_GUN, 1, 8, 5);
    CHECK(first && second);
    tick(1);
    CHECK(sc_doodad_disabled(first) + sc_doodad_disabled(second) == 1);
    unload();
    CHECK(doodad_map(1, 5, TRAP_FLOOR_GUN - 1));
    trap = spawn_at(TRAP_FLOOR_GUN, 1, 5, 5);
    CHECK(trap);
    trap->sc.timers[SC_TIMER_LOCKDOWN] = 1000;
    tick(1);
    CHECK(!sc_doodad_disabled(trap) && ((sc_mission_t *)level.mission)->doodad_off_count == 0);
    unload();

    /* The state is saved, hashed and validated. */
    CHECK(doodad_map(1, 5, TRAP_FLOOR_GUN - 1));
    trap = spawn_at(TRAP_FLOOR_GUN, 1, 5, 5);
    CHECK(trap);
    tick(1);
    CHECK(sc_doodad_disabled(trap));
    uint32_t hash = G_ConsistencyExtra(0);
    size_t size = G_SaveExtraSize();
    uint8_t *blob = malloc(size);
    CHECK(blob);
    G_SaveExtra(blob);
    sc_mission_t *m = level.mission;
    m->doodad_off_count = 0;
    CHECK(!sc_doodad_disabled(trap) && G_ConsistencyExtra(0) != hash);
    CHECK(G_LoadExtra(blob, size));
    CHECK(sc_doodad_disabled(trap) && G_ConsistencyExtra(0) == hash);
    sc_mission_t *saved = (sc_mission_t *)blob;
    saved->doodad_off_count = SC_DOODAD_OFF + 1;
    CHECK(!G_LoadExtra(blob, size));
    saved->doodad_off_count = 1; saved->doodad_off[0] = 0;
    CHECK(!G_LoadExtra(blob, size));
    saved->doodad_off[0] = trap->id;
    CHECK(G_LoadExtra(blob, size));
    free(blob);
    unload();
    return 0;
}

/* A real mission segment: the first Set Doodad State record of the installed
 * Terran04 that names a trap, run against the mission's own locations and
 * placed units. */
static int doodads_terran04(void) {
    const char *file = "data/STARCRAFT/install/campaign/terran/terran04/staredit/scenario.chk";
    CHECK(G_DoLoadLevel(file, &level) && level.mission);
    sc_mission_t *m = level.mission;
    CHECK(sc_spawn_things() > 0);
    const uint8_t *record = NULL;
    for (int i = 0; i < m->trig_count && !record; i++) for (int a = 0; a < 64; a++) {
        const uint8_t *r = m->trig + (size_t)i * 2400 + 320 + a * 32;
        if (r[26] == 42 && (read_u16_le(r + 24) == 203 || read_u16_le(r + 24) == 209)) { record = r; break; }
    }
    CHECK(record && record[27] == 0);
    uint8_t *t = trigger();
    CHECK(t);
    memcpy(t + 320, record, 32);
    memcpy(t + 320 + 32, record, 32);
    int human = 0;
    while (human < 8 && m->owners[human] != 6) human++;
    CHECK(human < 8);
    memset(t + 2372, 0, 8); t[2372 + human] = 1;
    /* Swap in one synthetic trigger; the map, locations and units stay real. */
    free(m->rt);
    m->trig = t; m->trig_size = 2400; m->trig_count = 1;
    m->rt = calloc(8, sizeof(*m->rt));
    CHECK(m->rt);
    int before = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && sc_doodad_disabled((mobj_t *)th)) before++;
    CHECK(before == 0);
    tick(1);
    /* Two toggles of the same trap: disabled, then enabled again. */
    CHECK(m->doodad_off_count == 0);
    t[320 + 32 + 26] = 0;
    m->rt[human] = (sc_trig_t){0};
    tick(1);
    CHECK(m->doodad_off_count == 1);
    m->trig = NULL; m->trig_size = 0;
    free(t);
    P_FreeLevel(&level);
    menuleave = false; menuactive = false; netgame = false;
    return 0;
}

/* Several triggers in one map. */
static bool load_many(uint8_t *t, size_t count) {
    bool ok = !write_map(t, count, NULL, 0) && G_DoLoadLevel(path, &level) && level.mission;
    free(t);
    return ok;
}

static uint8_t *triggers(size_t count) {
    uint8_t *t = calloc(count, 2400);
    for (size_t i = 0; t && i < count; i++) { t[i * 2400 + 2372] = 1; t[i * 2400 + 15] = 22; }
    return t;
}

/* Pause Game and Unpause Game (actions 5 and 6, 0x4ac330 and 0x4ac3a0): one
 * boolean, human-owned triggers only, never in a network game; triggers and
 * Wait timers keep running while elapsed time and the countdown stop; a
 * trigger that holds the pause releases it when its list ends. */
static int pausing(void) {
    /* Pause freezes the world, and elapsed time and the countdown with it. */
    uint8_t *t = trigger();
    CHECK(t);
    action(t, 0, 14, 7, 0, 0, 10, 0, 0, 0);
    action(t, 1, 5, 0, 0, 0, 0, 0, 0, 0);
    action(t, 2, 4, 0, 0, 0, 10 * RTS_TICK_MS, 0, 0, 0);
    action(t, 3, 13, 4, 0, 0, 0, 0, 0, 0);
    action(t, 4, 6, 0, 0, 0, 0, 0, 0, 0);
    CHECK(load(t, NULL, 0));
    sc_mission_t *m = level.mission;
    CHECK(!sc_world_frozen() && !G_WorldFrozen());
    tick(1);
    CHECK(sc_world_frozen() && G_WorldFrozen() && m->rt[0].pauser);
    CHECK(m->elapsed_ms == RTS_TICK_MS && m->countdown_ms == 10000);
    int ticks = 0;
    while (!m->switches[0] && ticks < 40) {
        tick(1);
        ticks++;
        if (!m->switches[0]) CHECK(sc_world_frozen() && m->elapsed_ms == RTS_TICK_MS && m->countdown_ms == 10000);
    }
    /* The Wait kept counting while paused; Unpause then ran. */
    CHECK(m->switches[0] == 1 && ticks >= 9 && ticks <= 11);
    CHECK(!sc_world_frozen() && !m->rt[0].pauser);
    int elapsed = m->elapsed_ms;
    tick(2);
    CHECK(m->elapsed_ms == elapsed + 2 * RTS_TICK_MS && m->countdown_ms == 10000 - 2 * RTS_TICK_MS);
    unload();

    /* Pausing twice and unpausing once is not paused: no count. */
    t = trigger();
    CHECK(t);
    action(t, 0, 5, 0, 0, 0, 0, 0, 0, 0);
    action(t, 1, 5, 0, 0, 0, 0, 0, 0, 0);
    action(t, 2, 6, 0, 0, 0, 0, 0, 0, 0);
    action(t, 3, 4, 0, 0, 0, 100000, 0, 0, 0);
    CHECK(load(t, NULL, 0));
    tick(1);
    CHECK(!sc_world_frozen());
    unload();

    /* A computer-owned trigger cannot pause. */
    t = trigger();
    CHECK(t);
    memset(t + 2372, 0, 8); t[2373] = 1;
    action(t, 0, 5, 0, 0, 0, 0, 0, 0, 0);
    action(t, 1, 4, 0, 0, 0, 100000, 0, 0, 0);
    CHECK(load(t, NULL, 0));
    m = level.mission;
    tick(2);
    CHECK(!sc_world_frozen() && !m->rt[1].pauser);
    unload();

    /* ... and cannot end a human's pause, or unpause when none is held. */
    t = triggers(2);
    CHECK(t);
    action(t, 0, 5, 0, 0, 0, 0, 0, 0, 0);
    action(t, 1, 4, 0, 0, 0, 100000, 0, 0, 0);
    memset(t + 2400 + 2372, 0, 8); t[2400 + 2373] = 1;
    action(t + 2400, 0, 6, 0, 0, 0, 0, 0, 0, 0);
    CHECK(load_many(t, 2));
    m = level.mission;
    tick(3);
    CHECK(sc_world_frozen() && m->rt[0].pauser);
    unload();

    /* A network game ignores both actions. */
    t = trigger();
    CHECK(t);
    action(t, 0, 5, 0, 0, 0, 0, 0, 0, 0);
    action(t, 1, 4, 0, 0, 0, 100000, 0, 0, 0);
    CHECK(load(t, NULL, 0));
    netgame = true;
    tick(1);
    CHECK(!sc_world_frozen());
    unload();

    /* Another trigger can unpause, but only the pausing trigger's mark is
     * cleared; the mark stays until that trigger's own list ends. */
    t = triggers(2);
    CHECK(t);
    action(t, 0, 5, 0, 0, 0, 0, 0, 0, 0);
    action(t, 1, 4, 0, 0, 0, 5 * RTS_TICK_MS, 0, 0, 0);
    action(t + 2400, 0, 6, 0, 0, 0, 0, 0, 0, 0);
    CHECK(load_many(t, 2));
    m = level.mission;
    tick(1);
    CHECK(!sc_world_frozen() && m->rt[0].pauser && !m->rt[8].pauser);
    for (int n = 0; n < 12; n++) tick(1);
    CHECK(!sc_world_frozen() && m->rt[0].disabled);
    unload();

    /* A trigger that holds the pause releases it when its list ends. */
    t = trigger();
    CHECK(t);
    action(t, 0, 5, 0, 0, 0, 0, 0, 0, 0);
    action(t, 1, 4, 0, 0, 0, 5 * RTS_TICK_MS, 0, 0, 0);
    CHECK(load(t, NULL, 0));
    m = level.mission;
    tick(1);
    CHECK(sc_world_frozen());
    tick(3);
    CHECK(sc_world_frozen() && !m->rt[0].disabled);
    for (int n = 0; n < 6 && !m->rt[0].disabled; n++) tick(1);
    CHECK(m->rt[0].disabled && !sc_world_frozen() && !m->rt[0].pauser);
    unload();

    /* Save and load while paused, with malformed records rejected. */
    t = trigger();
    CHECK(t);
    action(t, 0, 5, 0, 0, 0, 0, 0, 0, 0);
    action(t, 1, 4, 0, 0, 0, 100000, 0, 0, 0);
    CHECK(load(t, NULL, 0));
    m = level.mission;
    tick(2);
    CHECK(sc_world_frozen());
    uint32_t hash = G_ConsistencyExtra(0);
    size_t size = G_SaveExtraSize();
    uint8_t *blob = malloc(size);
    CHECK(blob);
    G_SaveExtra(blob);
    m->trigger_paused = false;
    m->rt[0].pauser = false;
    CHECK(G_ConsistencyExtra(0) != hash);
    CHECK(G_LoadExtra(blob, size));
    m = level.mission;
    CHECK(sc_world_frozen() && m->rt[0].pauser && G_ConsistencyExtra(0) == hash);
    m->trigger_paused = false;
    CHECK(G_ConsistencyExtra(0) != hash);
    m->trigger_paused = true;
    m->rt[0].pauser = false;
    CHECK(G_ConsistencyExtra(0) != hash);
    m->rt[0].pauser = true;
    CHECK(G_ConsistencyExtra(0) == hash);
    sc_mission_t *saved = (sc_mission_t *)blob;
    unsigned char good = *(unsigned char *)&saved->trigger_paused;
    *(unsigned char *)&saved->trigger_paused = 2;
    CHECK(!G_LoadExtra(blob, size));
    *(unsigned char *)&saved->trigger_paused = good;
    CHECK(G_LoadExtra(blob, size));
    sc_trig_t *runtime = (sc_trig_t *)(blob + size - 8 * sizeof(sc_trig_t));
    unsigned char *mark = (unsigned char *)&runtime[0].pauser, mark_good = *mark;
    *mark = 2;
    CHECK(!G_LoadExtra(blob, size));
    *mark = mark_good;
    CHECK(G_LoadExtra(blob, size));
    free(blob);
    CHECK(G_SaveLayout() == 22);
    unload();
    return 0;
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    consoleplayer = 0;
    if (inventory() || reports() || leaderboards() || portrait() || ai_scripts() || save_state() || pausing() || doodads() || doodads_terran04()) return 1;
    remove(path);
    puts("PASS: trigger coverage, leaderboards, portrait, AI scripts and saves");
    return 0;
}
