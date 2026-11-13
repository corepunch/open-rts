/* Appear-on-target weapons (weapons.dat behavior 2) deal their damage when
 * their bullet's Death script reaches domissiledmg, not on the firing tick:
 * the exact 24 Hz script frame per weapon from the real iscript.bin, a target
 * or an attacker that goes before the frame, a target that moves, the
 * upgrade level read at the hit, and a pending hit through a save. */
#include "t_local.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c, "StarCraft delayed damage", #c)

static const char *map = "data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk";
static const char *root = "data/STARCRAFT";
static tileset_t tiles;
static spritesheet_t fallback;
static spritecache_t cache;

static mobj_t *spawn(int type, fixed2_t at, int owner) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fixed2(at, 0), (uint16_t)type);
    if (u) { u->owner = u->team = (uint8_t)owner; u->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER; sc_start(u); }
    return u;
}
/* The unit type whose attack (ground or air) is native weapon id w. */
static int user_of(int w, const weapondef_t **def) {
    for (int t = 1; t <= SC_TYPES; ++t) {
        if (P_ActorType((uint16_t)t)->attack.native_id == w && P_ActorType((uint16_t)t)->attack.damage > 0) { *def = &P_ActorType((uint16_t)t)->attack; return t; }
        if (P_ActorType((uint16_t)t)->air_attack.native_id == w && P_ActorType((uint16_t)t)->air_attack.damage > 0) { *def = &P_ActorType((uint16_t)t)->air_attack; return t; }
    }
    return 0;
}
static int vitals(const mobj_t *u) { return u->hp * 256 + u->sc.shields; }
/* The engine tick on which script frame k runs for a bullet fired during tick start. */
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
static mobj_t *shooter, *victim;
static const weapondef_t *shooter_def;
/* A source of weapon w and a ground or air target 3 cells away. */
static bool setup(int w) {
    clear_level();
    int type = user_of(w, &shooter_def);
    static weapondef_t bare; /* a weapon no unit row carries (Acid Spray) is fired by a Marine */
    if (!type) { bare = (weapondef_t){.native_id = (uint16_t)w, .damage = sc_weapons[w].damage, .hits = 1}; shooter_def = &bare; type = MT_MARINE; }
    shooter = spawn(type, FIXED2_LIT(5, 5), 0);
    bool ground = sc_weapons[w].targets & 2;
    victim = spawn(ground ? MT_SIEGE_TANK : MT_BATTLECRUISER, FIXED2_LIT(8, 5), 1);
    if (!shooter || !victim) return false;
    shooter->core.angle = 0;
    shooter->attack.target = victim;
    P_UpdateSight();
    return true;
}
static mobj_t *fire(void) {
    ((sc_mission_t *)level.mission)->rng = 2;
    uint32_t id = level.next_mobj_id + 1;
    if (!sc_projectile_fire(shooter, shooter_def, victim)) return NULL;
    return P_MobjById(id);
}
/* One engine tick of a bullet; the firing tick itself runs the bullet without advancing the clock. */
static void same_tick(mobj_t *shot) { if (shot && !shot->remove) gameinfo->mobj_ticker(shot); }
static void tick(mobj_t *shot) { ++leveltime; if (shot && !shot->remove) { P_TickMobjState(shot); if (!shot->remove) gameinfo->mobj_ticker(shot); } }

/* The tick offset (from the firing tick) at which the target's vitals first drop, or -1. */
static int fire_start;
static int damage_tick(int w) {
    CHECK(setup(w));
    int start = fire_start = leveltime, before = vitals(victim);
    mobj_t *shot = fire();
    if (!shot && sc_weapons[w].behavior != 2) return -2;
    int hit = -1;
    if (vitals(victim) < before) hit = 0;
    same_tick(shot);
    for (int t = 1; t < 60 && hit < 0; ++t) { tick(shot); if (vitals(victim) < before) hit = leveltime - start; }
    return hit;
}

static int frames(void) {
    /* Death script frame of domissiledmg (test_iscript_inventory prints them): the first hit lands on that
     * 24 Hz frame after the firing tick, frame 0 on the firing tick. */
    static const struct { int weapon, frame; } expect[] = {
        {0, 0},   /* Gauss Rifle */
        {38, 0},  /* Needle Spines */
        {27, 1},  /* Arclite Shock Cannon */
        {70, 3},  /* Psionic Shockwave */
        {53, 5},  /* Subterranean Tentacle */
        {45, 5},  /* Acid Spray */
    };
    for (unsigned i = 0; i < sizeof(expect) / sizeof(*expect); ++i) {
        int w = expect[i].weapon;
        CHECK(sc_weapons[w].behavior == 2 && sc_weapon_script[w] > 0);
        int hit = damage_tick(w);
        CHECK(hit >= 0);
        CHECK(hit == (expect[i].frame ? tick_of_frame(fire_start, expect[i].frame) - fire_start : 0));
        if (expect[i].frame) CHECK(hit > 0);
    }
    return 0;
}

/* Single-target weapon (Tentacle, frame 5). */
static int single_target(void) {
    /* The target moves: the hit still lands on it. */
    CHECK(setup(53));
    int before = vitals(victim);
    mobj_t *shot = fire(); CHECK(shot && shot->sc.projectile.pending);
    same_tick(shot);
    victim->core.position = fixed3_from_fixed2(FIXED2_LIT(30, 30), 0);
    for (int t = 0; t < 40 && !shot->remove; ++t) tick(shot);
    CHECK(shot->remove && vitals(victim) < before);

    /* The target is gone before the frame: no damage and no pending bullet left. */
    CHECK(setup(53));
    mobj_t *bystander = spawn(MT_MARINE, FIXED2_LIT(8, 5), 1);
    int marine = vitals(bystander);
    shot = fire(); CHECK(shot);
    same_tick(shot);
    P_RemoveMobj(victim);
    CHECK(!shot->attack.target);
    for (int t = 0; t < 40 && !shot->remove; ++t) tick(shot);
    CHECK(shot->remove && vitals(bystander) == marine);

    /* A target that died (hp 0, not yet removed) takes nothing more. */
    CHECK(setup(53));
    shot = fire(); CHECK(shot);
    same_tick(shot);
    victim->hp = 0;
    for (int t = 0; t < 40 && !shot->remove; ++t) tick(shot);
    CHECK(shot->remove && victim->hp == 0);

    /* A missed roll deals nothing to a single target. */
    CHECK(setup(53));
    ((sc_mission_t *)level.mission)->rng = 2;
    before = vitals(victim);
    uint32_t id = level.next_mobj_id + 1;
    CHECK(sc_projectile_fire(shooter, shooter_def, victim));
    shot = P_MobjById(id); CHECK(shot);
    shot->sc.projectile.missed = true;
    same_tick(shot);
    for (int t = 0; t < 40 && !shot->remove; ++t) tick(shot);
    CHECK(shot->remove && vitals(victim) == before);
    return 0;
}

/* The attacker goes before the frame: the hit still lands, credited to its player. */
static int attacker_gone(void) {
    CHECK(setup(53));
    shooter->owner = shooter->team = 2;
    int before = vitals(victim);
    mobj_t *shot = fire(); CHECK(shot && shot->owner == 2);
    same_tick(shot);
    P_RemoveMobj(shooter);
    CHECK(!shot->target);
    for (int t = 0; t < 40 && !shot->remove; ++t) tick(shot);
    CHECK(shot->remove && vitals(victim) < before && victim->sc.attacker == 2);
    /* A lethal hit from a removed attacker kills and is credited to the same player. */
    CHECK(setup(53));
    shooter->owner = shooter->team = 2;
    victim->hp = 1; victim->sc.shields = 0;
    shot = fire(); CHECK(shot);
    same_tick(shot);
    P_RemoveMobj(shooter);
    for (int t = 0; t < 40 && !shot->remove; ++t) tick(shot);
    CHECK(shot->remove && victim->hp == 0 && victim->sc.attacker == 2);
    /* An attacker that is dead but not removed: the same. */
    CHECK(setup(53));
    before = vitals(victim);
    shot = fire(); CHECK(shot);
    same_tick(shot);
    shooter->hp = 0;
    for (int t = 0; t < 40 && !shot->remove; ++t) tick(shot);
    CHECK(shot->remove && vitals(victim) < before);
    return 0;
}

/* Splash (Arclite Shock Cannon, frame 1): the centre is where the bullet appeared. */
static int splash_centre(void) {
    CHECK(setup(27));
    mobj_t *near_centre = spawn(MT_MARINE, FIXED2_LIT(8, 5), 1), *far_unit = spawn(MT_MARINE, FIXED2_LIT(8, 9), 1);
    CHECK(near_centre && far_unit);
    int target0 = vitals(victim), near0 = vitals(near_centre), far0 = vitals(far_unit);
    mobj_t *shot = fire(); CHECK(shot && shot->sc.projectile.pending);
    fixed2_t centre = fixed3_xy(shot->core.position);
    CHECK(vitals(victim) == target0);
    same_tick(shot);
    /* The target is gone and the neighbours move: the centre stays put. */
    P_RemoveMobj(victim);
    far_unit->core.position = fixed3_from_fixed2(centre, 0);            /* moves into the centre */
    near_centre->core.position = fixed3_from_fixed2(FIXED2_LIT(25, 25), 0); /* leaves it */
    for (int t = 0; t < 40 && !shot->remove; ++t) tick(shot);
    CHECK(shot->remove && vitals(far_unit) < far0 && vitals(near_centre) == near0);
    return 0;
}

/* Upgrades are read at the hit. */
static int upgrade_at_hit(void) {
    const sc_weapon_t *w = &sc_weapons[27];
    CHECK(w->bonus > 0 && w->upgrade >= 0 && w->upgrade < SC_UPGRADES);
    int lost[2];
    for (int raise = 0; raise < 2; ++raise) {
        CHECK(setup(27));
        level.upgrades[w->upgrade][0].weapon = raise ? 0 : 3;
        int before = vitals(victim);
        mobj_t *shot = fire(); CHECK(shot);
        level.upgrades[w->upgrade][0].weapon = raise ? 3 : 0; /* changes between the firing tick and the hit */
        same_tick(shot);
        for (int t = 0; t < 40 && !shot->remove; ++t) tick(shot);
        lost[raise] = before - vitals(victim);
        level.upgrades[w->upgrade][0].weapon = 0;
    }
    CHECK(lost[0] > 0 && lost[1] > lost[0]); /* [0]: fired with 3, hit with 0; [1]: fired with 0, hit with 3 */
    return 0;
}

/* A pending hit through a save. */
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
/* Whether a save loads once the bullet id's weapon is replaced by weapon. */
static bool loads_with_weapon(const void *data, size_t length, uint32_t id, int weapon) {
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
        record.object.sc.projectile.weapon = weapon;
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
static int saved_pending(void) {
    CHECK(setup(53));
    int before = vitals(victim), start = leveltime;
    mobj_t *shot = fire(); CHECK(shot && shot->sc.projectile.pending);
    uint32_t shot_id = shot->id, victim_id = victim->id;
    same_tick(shot);
    int hit_tick = tick_of_frame(start, 5);
    while (leveltime < tick_of_frame(start, 2)) tick(shot);
    CHECK(!shot->remove && shot->sc.script.active && vitals(victim) == before);
    uint32_t hash = G_Consistency();
    sc_projectile_t pending = shot->sc.projectile;
    sc_script_t cursor = shot->sc.script;
    app_t app = {0}; AiContext ai = {0}; hudtext_t hud = {0};
    void *saved = NULL; size_t length = 0;
    CHECK(G_SaveGameBlob(&saved, &length, "pending", &app, &ai, &hud));
    CHECK(loads_with_weapon(saved, length, shot_id, pending.weapon));
    CHECK(!loads_with_weapon(saved, length, shot_id, 999));
    CHECK(!loads_with_weapon(saved, length, shot_id, -1));
    P_FreeLevel(&level); P_InitThinkers();
    CHECK(G_DoLoadLevel(map, &level) && P_InitSight());
    CHECK(G_LoadGameBlob(saved, length, &app, &ai, &hud));
    free(saved);
    shot = P_MobjById(shot_id);
    victim = P_MobjById(victim_id);
    CHECK(shot && victim && shot->sc.projectile.pending && !memcmp(&shot->sc.script, &cursor, sizeof(cursor)) &&
          shot->sc.projectile.weapon == pending.weapon && shot->attack.target == victim && G_Consistency() == hash);
    before = vitals(victim);
    while (leveltime < hit_tick - 1) tick(shot);
    CHECK(vitals(victim) == before && !shot->remove);
    tick(shot);
    CHECK(vitals(victim) < before && shot->remove);
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
    if (frames() || single_target() || attacker_gone() || splash_centre() || upgrade_at_hit() || saved_pending()) return 1;
    puts("PASS: delayed weapon damage from the Death script");
    return 0;
}
