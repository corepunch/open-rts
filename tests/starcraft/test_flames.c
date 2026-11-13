/* The Firebat's flames (Flame Thrower, weapons.dat behavior 5, enemy splash): its attack script
 * makes three bullets 24, 52 and 80 pixels ahead on script frames 1, 3 and 4 (attkshiftproj);
 * each strikes on frame 5 of its Death script, around where it appeared. */
#include "t_local.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c, "StarCraft flames", #c)

static const char *map = "data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk";
static const char *root = "data/STARCRAFT";
static tileset_t tiles;
static spritesheet_t fallback;
static spritecache_t cache;

static mobj_t *spawn(int type, int x, int y, int owner, int allegiance) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fixed2((fixed2_t){x * SC_PIXELS(1), y * SC_PIXELS(1)}, 0), (uint16_t)type);
    if (u) { u->owner = u->team = (uint8_t)owner; u->allegiance = (uint8_t)allegiance; sc_start(u); }
    return u;
}
/* Whole 1/256 points taken. */
static int lost(const mobj_t *u) { return u->max_hp * 256 - (u->hp * 256 - u->sc.wound) + (sc_units[u->type_id - 1].shields << 8) - u->sc.shields; }
static int tick_of_frame(int start, int k) {
    int tick = start;
    for (int seen = 0; seen < k;) { ++tick; seen += tick * 24 / RTS_TICRATE - (tick - 1) * 24 / RTS_TICRATE; }
    return tick;
}
static void clear_level(void) {
    P_FreeThinkers(); P_InitThinkers();
    memset(level.cell_solid, 0, (size_t)level.width * level.height);
    memset(level.blocked, 0, (size_t)level.width * level.height);
}
static void run_shots(void) {
    uint32_t ids[16]; int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap && n < 16; th = th->next)
        if (th->function == P_MobjThinker && ((mobj_t *)th)->type_id == MT_SC_PROJECTILE) ids[n++] = ((mobj_t *)th)->id;
    for (int i = 0; i < n; ++i) { mobj_t *s = P_MobjById(ids[i]); if (s && !s->remove) gameinfo->mobj_ticker(s); }
}
static int shots(void) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && ((mobj_t *)th)->type_id == MT_SC_PROJECTILE && !((mobj_t *)th)->remove) ++n;
    return n;
}
static mobj_t *bat, *a, *b, *c, *d, *e, *own, *ally;
/* A Firebat at (160,160) facing east, enemy marines on its row 32 pixels apart (a, b, c, d), one
 * beside the first bullet's path (e), a marine of its own player and one of an allied player. */
static void setup(void) {
    clear_level();
    bat = spawn(MT_FIREBAT, 160, 160, 0, ALLEGIANCE_PLAYER);
    a = spawn(MT_MARINE, 192, 160, 1, ALLEGIANCE_ENEMY); b = spawn(MT_MARINE, 224, 160, 1, ALLEGIANCE_ENEMY);
    c = spawn(MT_MARINE, 256, 160, 1, ALLEGIANCE_ENEMY); d = spawn(MT_MARINE, 288, 160, 1, ALLEGIANCE_ENEMY);
    e = spawn(MT_MARINE, 200, 190, 1, ALLEGIANCE_ENEMY);
    own = spawn(MT_MARINE, 200, 130, 0, ALLEGIANCE_PLAYER); ally = spawn(MT_MARINE, 232, 190, 2, ALLEGIANCE_PLAYER);
    bat->core.angle = 0;
    ((sc_mission_t *)level.mission)->rng = 2;
}
static int start;
static bool fire(void) {
    bat->attack.target = a;
    start = leveltime;
    if (!sc_projectile_fire(bat, &P_ActorType(MT_FIREBAT)->attack, a)) return false;
    run_shots();
    return true;
}
static void until(int tick) { while (leveltime < tick) { ++leveltime; run_shots(); } }

/* Frames and offsets of attkshiftproj in the real attack animations. */
static int seen_n, seen_frame[8], seen_arg[8], frame_now;
static uint32_t quiet(void *ctx, int source) { (void)ctx; (void)source; return 0; }
static void note(void *ctx, int op, const int *arg, int n) {
    (void)ctx; (void)n;
    if (op == 49 && seen_n < 8) { seen_frame[seen_n] = frame_now; seen_arg[seen_n++] = arg[0]; }
}
static bool script_volley(int unit, int anim) {
    const sc_script_host_t host = {quiet, note, NULL, NULL, NULL};
    sc_script_t cursor;
    seen_n = 0; frame_now = 0;
    if (sc_unit_script[unit - 1] < 0) return false;
    const blob_t *bin = sc_iscript_bin();
    sc_script_start(bin, &cursor, (unsigned)sc_unit_script[unit - 1], anim, &host);
    for (frame_now = 1; frame_now < 12 && cursor.active; ++frame_now) sc_script_frame(bin, &cursor, &host);
    return seen_n == 3 && seen_frame[0] == 1 && seen_frame[1] == 3 && seen_frame[2] == 4 &&
           seen_arg[0] == 24 && seen_arg[1] == 52 && seen_arg[2] == 80;
}

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
/* Whether a save loads once the bullet id's delay is replaced by weapon. */
static bool loads_with_delay(const void *data, size_t length, uint32_t id, int weapon) {
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
        record.object.sc.projectile.delay = (uint8_t)weapon;
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
static int totals(void) {
    /* Bullets at x 184, 212, 240 (y 160). Marines span [x-8, x+9): a is struck by the first (inside) and
     * the second (11 px off, inner radius 15) for 8 each, b by the second (4) and third (7), c by the third
     * (8) alone, d is 40 px off the last. Marine armor is 0 and concussive hits small units in full. */
    setup();
    CHECK(fire() && shots() == 3 && lost(a) == 0);
    until(tick_of_frame(start, 6) - 1);
    CHECK(lost(a) == 0 && lost(b) == 0);                       /* nothing before the first bullet's frame 5 */
    until(tick_of_frame(start, 6));
    CHECK(lost(a) == 8 * 256 && lost(b) == 0);                 /* bullet one: made on frame 1, strikes on 6 */
    until(tick_of_frame(start, 8) - 1);
    CHECK(lost(a) == 8 * 256 && lost(b) == 0);
    until(tick_of_frame(start, 8));
    CHECK(lost(a) == 16 * 256 && lost(b) == 8 * 256 && lost(c) == 0);
    until(tick_of_frame(start, 9) - 1);
    CHECK(lost(b) == 8 * 256 && lost(c) == 0);
    until(tick_of_frame(start, 9));
    CHECK(lost(b) == 16 * 256 && lost(c) == 8 * 256 && lost(d) == 0 && shots() == 0);
    /* Beside the row: a quarter from the outer ring, per bullet; the unit of its own player is spared and the
     * allied player's marine is not (OpenBW enemy splash spares only the bullet owner's player). */
    CHECK(lost(e) > 0 && lost(e) <= 4 * 256 && lost(own) == 0 && lost(ally) > 0 && lost(ally) <= 4 * 256);
    return 0;
}

static int cancelled(void) {
    /* The target is gone before the first frame: attack_with finds no order target, no bullet is made. */
    setup();
    CHECK(fire());
    P_RemoveMobj(a);
    until(tick_of_frame(start, 12));
    CHECK(lost(b) == 0 && lost(e) == 0 && shots() == 0);
    /* The attacker dies before its script makes any: nothing. */
    setup();
    CHECK(fire());
    bat->hp = 0;
    until(tick_of_frame(start, 12));
    CHECK(lost(a) == 0 && lost(b) == 0 && shots() == 0);
    /* It dies after the first: that flame (made on frame 1) still strikes, the others never exist. */
    setup();
    CHECK(fire());
    until(tick_of_frame(start, 2));
    bat->hp = 0;
    until(tick_of_frame(start, 12));
    CHECK(lost(a) == 8 * 256 && lost(b) == 0 && shots() == 0);
    /* The target dies after the first bullet: the others are never made. */
    setup();
    CHECK(fire());
    until(tick_of_frame(start, 2));
    bat->attack.target = NULL;
    until(tick_of_frame(start, 12));
    CHECK(lost(a) == 8 * 256 && lost(b) == 0 && shots() == 0);
    return 0;
}

static int saved(void) {
    setup();
    CHECK(fire());
    until(tick_of_frame(start, 2)); /* one flame made, two waiting */
    CHECK(shots() == 3);
    uint32_t hash = G_Consistency(), a_id = a->id, b_id = b->id, bat_id = bat->id, shot_id = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *m = (mobj_t *)th;
        if (th->function == P_MobjThinker && m->type_id == MT_SC_PROJECTILE && m->sc.projectile.delay) { shot_id = m->id; break; }
    }
    CHECK(shot_id);
    app_t app = {0}; AiContext ai = {0}; hudtext_t hud = {0};
    void *blob = NULL; size_t length = 0;
    CHECK(G_SaveGameBlob(&blob, &length, "flames", &app, &ai, &hud));
    CHECK(loads_with_delay(blob, length, shot_id, 1));
    CHECK(!loads_with_delay(blob, length, shot_id, 200));
    int from = start;
    P_FreeLevel(&level); P_InitThinkers();
    CHECK(G_DoLoadLevel(map, &level) && P_InitSight());
    CHECK(G_LoadGameBlob(blob, length, &app, &ai, &hud));
    free(blob);
    CHECK(G_Consistency() == hash && shots() == 3);
    a = P_MobjById(a_id); b = P_MobjById(b_id); bat = P_MobjById(bat_id);
    CHECK(a && b && bat && lost(a) == 0);
    until(tick_of_frame(from, 9));
    CHECK(lost(a) == 16 * 256 && lost(b) == 16 * 256 && shots() == 0);
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO) == 0);
    G_InitGame(); P_InitThinkers();
    CHECK(G_DoLoadLevel(map, &level) && P_InitSight());
    CHECK(W_LoadAssets(root, &level, "sc-000", &tiles, &fallback));
    CHECK(P_LoadThings(level.map_path) > 0);
    mobjlist_t units = P_ListMobjs();
    CHECK(R_InitSprites(root, &level, units.items, units.count, &cache));
    P_FreeMobjList(&units); P_FreeThinkers(); P_InitThinkers(); CHECK(P_InitSight());
    CHECK(sc_iscript_load(root));
    CHECK(sc_weapons[25].behavior == 5 && sc_weapon_script[25] > 0);
    CHECK(script_volley(MT_FIREBAT, 2) && script_volley(MT_FIREBAT, 5) && script_volley(10 + 1, 2) && script_volley(211 + 1, 2) && script_volley(213 + 1, 2));
    if (totals() || cancelled() || saved()) return 1;
    puts("PASS: Firebat flames from the attack script");
    return 0;
}
