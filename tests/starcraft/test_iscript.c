/* The IScript runtime (p_iscript.c) against OpenBW's iscript_execute: exact
 * frames of frame changes, sounds and branches on synthetic programs, then
 * the real iscript.bin driving sounds of a missile impact, a lift-off, a
 * landing and a burrow in the running game, and the cursor through a save. */
#include "t_local.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c, "StarCraft IScript", #c)

static const char *map = "data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk";
static const char *root = "data/STARCRAFT";

/* ---- synthetic programs -------------------------------------------------- */

typedef struct { int frame; char kind; int value; } event_t;
static event_t events[64];
static int event_count, frame_now, draws, draw_sources[16], cond_calls[16], cond_count;
static uint32_t rolls[16];
static bool cond_answer;

static void note(char kind, int value) {
    if (event_count < 64) events[event_count++] = (event_t){frame_now, kind, value};
}
static uint32_t host_random(void *ctx, int source) {
    (void)ctx;
    draw_sources[draws & 15] = source;
    return rolls[draws++ & 15];
}
static void host_op(void *ctx, int op, const int *arg, int n) {
    (void)ctx;
    /* f playfram frame, e end, m attackmelee sound count, t turnrand flag, o any other opcode */
    if (op == 0) note('f', arg[0]);
    else if (op == 22) note('e', 0);
    else if (op == 28) note('m', arg[0]);
    else if (op == 34) note('t', arg[1]);
    else note('o', op);
    (void)n;
}
static bool host_cond(void *ctx, int op, const int *arg) {
    (void)ctx; (void)arg;
    cond_calls[cond_count++ & 15] = frame_now * 100 + op;
    return cond_answer;
}
static void host_sound(void *ctx, int row) { (void)ctx; note('s', row); }
static const sc_script_host_t host = {host_random, host_op, host_cond, host_sound, NULL};

/* Runs prog from its first byte for frames frames; frame 0 is the call that
 * starts it. */
static sc_script_t run(const uint8_t *prog, size_t size, int frames) {
    blob_t bin = {.bytes = (uint8_t *)prog, .size = size};
    sc_script_t c = {.pc = 1, .active = 1};
    event_count = draws = cond_count = 0;
    for (frame_now = 0; frame_now < frames && c.active; ++frame_now) sc_script_frame(&bin, &c, &host);
    return c;
}
/* Whether an event of this kind and value happened on frame. */
static bool has(int frame, char kind, int value) {
    for (int i = 0; i < event_count; ++i)
        if (events[i].frame == frame && events[i].kind == kind && events[i].value == value) return true;
    return false;
}
static bool is(int i, int frame, char kind, int value) {
    return i < event_count && events[i].frame == frame && events[i].kind == kind && events[i].value == value;
}
enum { PLAYFRAM = 0, WAIT = 5, WAITRAND = 6, GOTO = 7, END = 22, PLAYSND = 24, PLAYSNDRAND = 25, PLAYSNDBTWN = 26,
       ATTACKMELEE = 28, RANDCONDJMP = 30, TURNRAND = 34, IGNOREREST = 48, CALL = 53, RETURN = 54, LIFTOFFCONDJMP = 63 };

static int synthetic(void) {
    /* wait n resumes n frames later: frame changes at 0, 3 and 4 (OpenBW stores n-1 and counts it down). */
    const uint8_t wait[] = {0xff, PLAYFRAM, 1, 0, WAIT, 3, PLAYFRAM, 2, 0, WAIT, 1, PLAYFRAM, 3, 0, END};
    run(wait, sizeof(wait), 20);
    CHECK(event_count == 4 && is(0, 0, 'f', 1) && is(1, 3, 'f', 2) && is(2, 4, 'f', 3) && is(3, 4, 'e', 0));
    /* A wait of zero resumes on the next frame like a wait of one. */
    const uint8_t wait0[] = {0xff, PLAYFRAM, 1, 0, WAIT, 0, PLAYFRAM, 2, 0, END};
    run(wait0, sizeof(wait0), 5);
    CHECK(is(1, 1, 'f', 2));

    /* waitrand lo hi: lo + (roll & 0xff) % (hi - lo + 1) frames, drawn once from source 3. */
    const uint8_t wr[] = {0xff, WAITRAND, 2, 5, PLAYFRAM, 7, 0, END};
    rolls[0] = 0x1ff; /* & 0xff = 255, % 4 = 3 */
    run(wr, sizeof(wr), 20);
    CHECK(draws == 1 && draw_sources[0] == 3 && is(0, 5, 'f', 7));
    rolls[0] = 0x300; /* & 0xff = 0: the shortest wait */
    run(wr, sizeof(wr), 20);
    CHECK(is(0, 2, 'f', 7));
    rolls[0] = 1; /* hi - lo + 1 draws: 1 % 4 = 1 */
    run(wr, sizeof(wr), 20);
    CHECK(is(0, 3, 'f', 7));
    const uint8_t wr_same[] = {0xff, WAITRAND, 4, 4, PLAYFRAM, 7, 0, END};
    rolls[0] = 77;
    run(wr_same, sizeof(wr_same), 20);
    CHECK(is(0, 4, 'f', 7));

    /* goto loops: the frame changes every two frames for as long as it runs. */
    const uint8_t loop[] = {0xff, PLAYFRAM, 1, 0, WAIT, 2, PLAYFRAM, 2, 0, WAIT, 2, GOTO, 1, 0};
    sc_script_t c = run(loop, sizeof(loop), 9);
    CHECK(c.active && event_count == 5 && is(0, 0, 'f', 1) && is(1, 2, 'f', 2) && is(2, 4, 'f', 1) &&
          is(3, 6, 'f', 2) && is(4, 8, 'f', 1));

    /* call keeps one return address: the sound is at 0, the frame after the sub's wait. */
    const uint8_t sub[] = {0xff, CALL, 10, 0, PLAYFRAM, 9, 0, WAIT, 1, END, PLAYSND, 5, 0, WAIT, 2, RETURN};
    run(sub, sizeof(sub), 12);
    CHECK(event_count == 3 && is(0, 0, 's', 5) && is(1, 2, 'f', 9) && is(2, 3, 'e', 0));
    /* A return with no call has nowhere to go, and stops. */
    const uint8_t no_call[] = {0xff, RETURN};
    c = run(no_call, sizeof(no_call), 3);
    CHECK(!c.active && event_count == 0);

    /* randcondjmp chance target: taken when (roll & 0xff) <= chance, source 7. */
    const uint8_t rc[] = {0xff, RANDCONDJMP, 100, 9, 0, PLAYFRAM, 1, 0, END, PLAYFRAM, 2, 0, END};
    rolls[0] = 100;
    run(rc, sizeof(rc), 3);
    CHECK(draws == 1 && draw_sources[0] == 7 && is(0, 0, 'f', 2));
    rolls[0] = 101;
    run(rc, sizeof(rc), 3);
    CHECK(is(0, 0, 'f', 1));
    rolls[0] = 0x164; /* only the low byte counts: 0x64 = 100 */
    run(rc, sizeof(rc), 3);
    CHECK(is(0, 0, 'f', 2));

    /* playsnd plays at its own frame; none is drawn. */
    const uint8_t ps[] = {0xff, WAIT, 4, PLAYSND, 0x2c, 1, END};
    run(ps, sizeof(ps), 8);
    CHECK(draws == 0 && is(0, 4, 's', 300));
    /* playsndrand picks row roll % count, source 4. */
    const uint8_t pr[] = {0xff, PLAYSNDRAND, 3, 10, 0, 20, 0, 30, 0, END};
    rolls[0] = 7;
    run(pr, sizeof(pr), 2);
    CHECK(draws == 1 && draw_sources[0] == 4 && is(0, 0, 's', 20));
    /* playsndbtwn a b: a + roll % (b - a + 1), source 5. */
    const uint8_t pb[] = {0xff, PLAYSNDBTWN, 40, 0, 43, 0, END};
    rolls[0] = 5;
    run(pb, sizeof(pb), 2);
    CHECK(draws == 1 && draw_sources[0] == 5 && is(0, 0, 's', 41));
    /* attackmelee deals its blow, then plays one of its sounds from source 4. */
    const uint8_t am[] = {0xff, ATTACKMELEE, 2, 50, 0, 51, 0, END};
    rolls[0] = 3;
    run(am, sizeof(am), 2);
    CHECK(draws == 1 && draw_sources[0] == 4 && is(0, 0, 'm', 2) && is(1, 0, 's', 51));
    /* turnrand turns the other way on one roll in four (roll % 4 == 1), source 6. */
    const uint8_t tr[] = {0xff, TURNRAND, 2, END};
    rolls[0] = 5;
    run(tr, sizeof(tr), 2);
    CHECK(draw_sources[0] == 6 && is(0, 0, 't', 1));
    rolls[0] = 6;
    run(tr, sizeof(tr), 2);
    CHECK(is(0, 0, 't', 0));

    /* A world-dependent jump asks the host; liftoffcondjmp jumps when it is true. */
    const uint8_t lo[] = {0xff, LIFTOFFCONDJMP, 8, 0, PLAYFRAM, 1, 0, END, PLAYFRAM, 2, 0, END};
    cond_answer = true;
    run(lo, sizeof(lo), 3);
    CHECK(cond_count == 1 && is(0, 0, 'f', 2));
    cond_answer = false;
    run(lo, sizeof(lo), 3);
    CHECK(is(0, 0, 'f', 1));
    /* ignorerest waits ten frames and then asks again (frames 0, 11, 22) until the host lets it through. */
    const uint8_t ir[] = {0xff, IGNOREREST, PLAYFRAM, 4, 0, END};
    cond_answer = false;
    run(ir, sizeof(ir), 25);
    CHECK(event_count == 0 && cond_count == 3 && cond_calls[0] == 48 && cond_calls[1] == 1148 && cond_calls[2] == 2248);
    cond_answer = true;
    run(ir, sizeof(ir), 25);
    CHECK(is(1, 0, 'f', 4));

    /* Malformed programs stop instead of running away. */
    const uint8_t bad_op[] = {0xff, 0xee, END};
    c = run(bad_op, sizeof(bad_op), 3);
    CHECK(!c.active && event_count == 0);
    const uint8_t truncated[] = {0xff, PLAYSND, 1};
    c = run(truncated, sizeof(truncated), 3);
    CHECK(!c.active && event_count == 0);
    const uint8_t spin[] = {0xff, GOTO, 1, 0};
    c = run(spin, sizeof(spin), 3);
    CHECK(!c.active);
    const uint8_t falls_off[] = {0xff, PLAYFRAM, 1, 0};
    c = run(falls_off, sizeof(falls_off), 3);
    CHECK(!c.active && event_count == 1);
    /* An ended script does nothing further. */
    blob_t bin = {.bytes = (uint8_t *)wait, .size = sizeof(wait)};
    sc_script_t ended = {0};
    CHECK(!sc_script_frame(&bin, &ended, &host));

    /* Header lookup and start: SCPE with two entry points; start runs frame 0 at once. */
    uint8_t file[64] = {0x10, 0};
    file[0x10] = 5; file[0x12] = 0x20; file[0x14] = 0xff; file[0x15] = 0xff;
    memcpy(file + 0x20, "SCPE", 4); file[0x24] = 0;
    file[0x28] = 0x30; file[0x2a] = 0x34;
    file[0x30] = END; file[0x34] = PLAYSND; file[0x35] = 9; file[0x37] = END;
    bin = (blob_t){.bytes = file, .size = sizeof(file)};
    CHECK(sc_iscript_animation(&bin, 5, 0) == 0x30 && sc_iscript_animation(&bin, 5, 1) == 0x34 &&
          !sc_iscript_animation(&bin, 5, 2) && !sc_iscript_animation(&bin, 6, 0));
    event_count = 0; frame_now = 0;
    CHECK(!sc_script_start(&bin, &c, 5, 1, &host) && is(0, 0, 's', 9) && !c.active);
    CHECK(!sc_script_start(&bin, &c, 6, 1, &host) && !c.active);

    /* Saved cursors: idle is all zero; running needs an offset and a real animation. */
    CHECK(sc_script_valid(&(sc_script_t){0}) && sc_script_valid(&(sc_script_t){.pc = 9, .anim = 17, .active = 1}));
    CHECK(!sc_script_valid(&(sc_script_t){.pc = 9}) && !sc_script_valid(&(sc_script_t){.active = 2, .pc = 9}) &&
          !sc_script_valid(&(sc_script_t){.active = 1, .pc = 0}) &&
          !sc_script_valid(&(sc_script_t){.active = 1, .pc = 9, .anim = 40}) &&
          !sc_script_valid(&(sc_script_t){.wait = 3}));
    return 0;
}

/* ---- the real iscript.bin in the running game ------------------------------ */

typedef struct { int tick, row; } heard_t;
static heard_t sounds[32];
static int sound_count;
static void record(const mobj_t *actor, int row) {
    (void)actor;
    if (sound_count < 32) sounds[sound_count++] = (heard_t){leveltime, row};
}
static mobj_t *spawn(int type, fixed2_t at, int owner) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fixed2(at, 0), (uint16_t)type);
    if (u) { u->owner = u->team = (uint8_t)owner; u->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER; }
    return u;
}
static void step(mobj_t *mo) { ++leveltime; P_TickMobjState(mo); if (!mo->remove) gameinfo->mobj_ticker(mo); }
/* The engine tick on which script frame k runs, for a script started during
 * tick start: each tick advances the 24 Hz script by 0 or 1 frames. */
static int tick_of_frame(int start, int k) {
    int tick = start;
    for (int seen = 0; seen < k;) { ++tick; seen += tick * 24 / RTS_TICRATE - (tick - 1) * 24 / RTS_TICRATE; }
    return tick;
}
static void clear_level(void) {
    P_FreeThinkers(); P_InitThinkers();
    memset(level.cell_solid, 0, (size_t)level.width * level.height);
    memset(level.blocked, 0, (size_t)level.width * level.height);
    ((sc_mission_t *)level.mission)->rng = 2;
}

static int real_scripts(void) {
    CHECK(sc_iscript_load(root) && sc_iscript_bin());
    const blob_t bin = *sc_iscript_bin();
    int hall = MT_COMMAND_CENTER - 1;
    CHECK(sc_unit_script[hall] >= 0 && sc_weapon_script[48] == 264 && sc_unit_script[MT_SPIDER_MINE - 1] >= 0);
    /* iscript.bin by hand: Lift Off plays row 471 on its first frame; Landing plays 472 on
     * its 18th and ends; a Glave Wurm's Death plays 93, spawns, damages, and ends the next
     * frame, while its bounce (SpecialState1) plays 91 and then loops its trail. */
    sc_script_t c;
    event_count = 0; frame_now = 0;
    sc_script_start(&bin, &c, (unsigned)sc_unit_script[hall], SC_ANIM_LIFT_OFF, &host);
    CHECK(has(0, 's', 471));
    event_count = 0; frame_now = 0;
    sc_script_start(&bin, &c, (unsigned)sc_unit_script[hall], SC_ANIM_LANDING, &host);
    for (frame_now = 1; frame_now < 60 && c.active; ++frame_now) sc_script_frame(&bin, &c, &host);
    int sound_frame = -1;
    for (int i = 0; i < event_count; ++i) if (events[i].kind == 's') sound_frame = events[i].frame;
    CHECK(sound_frame == 18 && has(18, 's', 472) && c.active); /* it then idles: the engine host stops it at sigorder */
    event_count = 0; frame_now = 0;
    sc_script_start(&bin, &c, 264, SC_ANIM_DEATH, &host);
    for (frame_now = 1; frame_now < 4 && c.active; ++frame_now) sc_script_frame(&bin, &c, &host);
    CHECK(event_count == 4 && is(0, 0, 's', 93) && is(1, 0, 'o', 15) && is(2, 0, 'o', 27) && is(3, 1, 'e', 0));
    event_count = 0; frame_now = 0;
    sc_script_start(&bin, &c, 264, SC_ANIM_SPECIAL1, &host);
    for (frame_now = 1; frame_now < 30; ++frame_now) sc_script_frame(&bin, &c, &host);
    CHECK(is(0, 0, 's', 91) && c.active);
    event_count = 0; frame_now = 0;
    sc_script_start(&bin, &c, (unsigned)sc_unit_script[MT_SPIDER_MINE - 1], SC_ANIM_BURROW, &host);
    CHECK(has(0, 's', 354));

    /* In the game. A Spider Mine burrows with row 354 on the order's own tick. */
    sc_script_play = record;
    clear_level();
    mobj_t *mine = spawn(MT_SPIDER_MINE, FIXED2_LIT(5, 5), 0);
    CHECK(mine);
    sound_count = 0;
    int t0 = leveltime;
    sc_burrow(mine);
    CHECK(sound_count == 1 && sounds[0].row == 354 && sounds[0].tick == t0);
    sound_count = 0;
    sc_unburrow(mine); /* not dug in: nothing happens */
    CHECK(sound_count == 0);
    mine->sc.flags |= SC_BURROWED;
    sc_unburrow(mine);
    CHECK(sound_count == 1 && sounds[0].row == 354);

    /* A Command Center lifts (471 on the order's tick) and lands (472 on script frame 18). */
    clear_level();
    mobj_t *cc = spawn(MT_COMMAND_CENTER, P_BuildingPosition(MT_COMMAND_CENTER, (ivec2_t){8, 8}), 0);
    CHECK(cc); P_SyncBuildingBlocking();
    sound_count = 0;
    ticcmd_t lift = {.order = TC_LIFT, .count = 1, .units = {cc->id}};
    G_RunTiccmd(0, &lift);
    CHECK(cc->traits & MF_FLY);
    CHECK(sound_count == 1 && sounds[0].row == 471 && sounds[0].tick == leveltime);
    for (int i = 0; i < 100; ++i) P_Ticker();
    sound_count = 0;
    ticcmd_t land = {.order = TC_LAND, .count = 1, .units = {cc->id},
                     .position = fixed3_from_fixed2(FIXED2_LIT(18, 8), 0)};
    G_RunTiccmd(0, &land);
    CHECK(cc->sc.order.kind == SC_ORDER_LAND);
    int started = 0;
    for (int i = 0; i < 1200 && !started; ++i) {
        P_Ticker();
        if (cc->sc.script.active && cc->sc.script.anim == SC_ANIM_LANDING) started = leveltime - 1; /* P_Ticker runs the thinkers, then advances the clock */
    }
    CHECK(started && sound_count == 0);
    for (int i = 0; i < 80 && !sound_count; ++i) P_Ticker();
    CHECK(sound_count == 1 && sounds[0].row == 472 && sounds[0].tick == tick_of_frame(started, 18));
    for (int i = 0; i < 100; ++i) P_Ticker();
    CHECK(sound_count == 1 && !cc->sc.script.active);
    return 0;
}

/* A Mutalisk's Glave Wurm: row 91 when it bounces, 93 when the last hit lands. */
static int missile_sounds(void) {
    sc_script_play = record;
    clear_level();
    mobj_t *muta = spawn(MT_MUTALISK, FIXED2_LIT(5, 5), 0), *a = spawn(MT_MARINE, FIXED2_LIT(7, 5), 1),
           *b = spawn(MT_MARINE, FIXED2_LIT(8, 5), 1);
    CHECK(muta && a && b); P_UpdateSight();
    uint32_t id = level.next_mobj_id + 1;
    CHECK(sc_projectile_fire(muta, &muta->info->attack, a));
    mobj_t *shot = P_MobjById(id);
    CHECK(shot && !shot->sc.projectile.missed);
    sound_count = 0;
    int bounce_tick = 0, last_tick = 0;
    for (int t = 0; t < RTS_TICRATE * 8 && !shot->remove; ++t) {
        int phase = shot->missile.phase;
        step(shot);
        if (!bounce_tick && phase == 0 && shot->missile.phase == 1) bounce_tick = leveltime;
        if (!last_tick && shot->sc.projectile.weapon < 0) last_tick = leveltime;
    }
    CHECK(shot->remove && bounce_tick && last_tick > bounce_tick && sound_count == 2);
    CHECK(sounds[0].row == 91 && sounds[0].tick == bounce_tick && sounds[1].row == 93 && sounds[1].tick == last_tick);
    /* One target and no bounce: only the Death sound, on the tick the shot starts dying. */
    clear_level();
    muta = spawn(MT_MUTALISK, FIXED2_LIT(5, 5), 0); a = spawn(MT_MARINE, FIXED2_LIT(7, 5), 1);
    CHECK(muta && a); P_UpdateSight();
    id = level.next_mobj_id + 1;
    CHECK(sc_projectile_fire(muta, &muta->info->attack, a));
    shot = P_MobjById(id); CHECK(shot);
    sound_count = 0; last_tick = 0;
    for (int t = 0; t < RTS_TICRATE * 8 && !shot->remove; ++t) {
        step(shot);
        if (!last_tick && shot->sc.projectile.weapon < 0) last_tick = leveltime;
    }
    CHECK(sound_count == 1 && sounds[0].row == 93 && sounds[0].tick == last_tick);
    return 0;
}

/* The cursor survives a save in mid-wait and hashes with the game; a save with a
 * malformed cursor is refused. The header and record layouts are g_save.c's. */
typedef struct {
    char magic[8];
    uint32_t version, header_size, object_size, body_size, checksum, signature;
    saveinfo_t info;
    int width, height, time, speed, vents, objects;
    bool paused;
    fvec2_t camera;
    daylight_t daylight;
    int resources[8][RTS_MAX_RESOURCES];
    uint8_t random;
    uint32_t vision[8], next_id, next_order;
    uint16_t income_scale[8];
    uint8_t upgrades[sizeof(level.upgrades)];
    outcome_t outcome;
    uint8_t sides, foes;
    sidestats_t side_stats[8];
    size_t extra_size;
    AiContext ai;
    hudtext_t hud;
} save_header_t;
typedef struct {
    mobj_t object;
    uint32_t target, attack, base;
    bool production, removed;
    production_t queue;
} saved_mobj_t;
static uint32_t checksum(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * UINT32_C(16777619);
    return hash;
}
/* Whether a save whose mobj id has script cursor bad still loads. */
static bool loads_with_cursor(const void *data, size_t length, uint32_t id, sc_script_t bad) {
    uint8_t *copy = malloc(length);
    if (!copy) return false;
    memcpy(copy, data, length);
    save_header_t *header = (save_header_t *)copy;
    size_t at = sizeof(*header) + (size_t)header->width * header->height * 5 +
                (size_t)header->vents * sizeof(resourcevent_t);
    bool patched = false;
    for (int i = 0; i < header->objects; ++i) {
        saved_mobj_t record;
        memcpy(&record, copy + at + (size_t)i * sizeof(record), sizeof(record));
        if (record.object.id != id) continue;
        record.object.sc.script = bad;
        memcpy(copy + at + (size_t)i * sizeof(record), &record, sizeof(record));
        patched = true;
    }
    header->checksum = 0;
    header->checksum = checksum(header, sizeof(*header)) ^ checksum(copy + sizeof(*header), header->body_size);
    app_t app = {0}; AiContext ai = {0}; hudtext_t hud = {0};
    bool loaded = patched && G_LoadGameBlob(copy, length, &app, &ai, &hud);
    free(copy);
    return loaded;
}

static int saves(void) {
    clear_level();
    sc_script_play = record;
    int hall = MT_COMMAND_CENTER - 1;
    mobj_t *cc = spawn(MT_COMMAND_CENTER, P_BuildingPosition(MT_COMMAND_CENTER, (ivec2_t){8, 8}), 0);
    CHECK(cc); P_SyncBuildingBlocking();
    uint32_t idle_hash = G_Consistency();
    int start = leveltime;
    sc_script_begin(cc, sc_unit_script[hall], SC_ANIM_LANDING);
    CHECK(cc->sc.script.active && cc->sc.script.wait && G_Consistency() != idle_hash);
    sound_count = 0;
    while (leveltime < tick_of_frame(start, 9)) step(cc);
    CHECK(cc->sc.script.active && sound_count == 0);
    sc_script_t cursor = cc->sc.script;
    uint32_t id = cc->id, hash = G_Consistency();
    app_t app = {0}; AiContext ai = {0}; hudtext_t hud = {0};
    void *saved = NULL; size_t length = 0;
    CHECK(G_SaveGameBlob(&saved, &length, "script", &app, &ai, &hud));
    CHECK(loads_with_cursor(saved, length, id, cursor));
    CHECK(!loads_with_cursor(saved, length, id, (sc_script_t){.pc = cursor.pc, .active = 2}));
    CHECK(!loads_with_cursor(saved, length, id, (sc_script_t){.pc = cursor.pc, .anim = 99, .active = 1}));
    CHECK(!loads_with_cursor(saved, length, id, (sc_script_t){.pc = 0, .active = 1}));
    CHECK(!loads_with_cursor(saved, length, id, (sc_script_t){.pc = 5}));
    P_FreeLevel(&level); P_InitThinkers();
    CHECK(G_DoLoadLevel(map, &level) && P_InitSight());
    CHECK(G_LoadGameBlob(saved, length, &app, &ai, &hud));
    free(saved);
    cc = P_MobjById(id);
    CHECK(cc && !memcmp(&cc->sc.script, &cursor, sizeof(cursor)) && G_Consistency() == hash);
    sound_count = 0;
    while (leveltime < tick_of_frame(start, 18) - 1) step(cc);
    CHECK(sound_count == 0);
    step(cc);
    CHECK(sound_count == 1 && sounds[0].row == 472 && sounds[0].tick == tick_of_frame(start, 18));
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO) == 0);
    if (synthetic()) return 1;
    static spritecache_t cache; static tileset_t tiles; static spritesheet_t fallback;
    G_InitGame(); P_InitThinkers();
    CHECK(G_DoLoadLevel(map, &level) && P_InitSight());
    CHECK(W_LoadAssets(root, &level, "sc-000", &tiles, &fallback));
    CHECK(P_LoadThings(level.map_path) > 0);
    mobjlist_t units = P_ListMobjs();
    CHECK(R_InitSprites(root, &level, units.items, units.count, &cache));
    P_FreeMobjList(&units); P_FreeThinkers(); P_InitThinkers(); CHECK(P_InitSight());
    if (real_scripts()) return 1; if (missile_sounds()) return 1; if (saves()) return 1;
    puts("PASS: IScript timing, branches, random draws, sounds and cursor saves");
    return 0;
}
