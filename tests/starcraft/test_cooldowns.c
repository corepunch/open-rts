#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c, "StarCraft cooldowns", #c)
/* OpenBW get_modified_weapon_cooldown term by term (acid spores, Stim
 * Packs, a cooldown upgrade, Ensnare, then the 5..250 clamp) and the
 * (roll & 3) - 1 the attack path adds after it. Frame counts are native
 * 24 Hz frames; the engine stores them as milliseconds. */

static const char *map = "data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk";
static const char *root = "data/STARCRAFT";
static soundinfo_t counting_sounds;
static const soundinfo_t *real_sounds;
static int attack_sounds;
static const mobj_t *attack_sound_origin;
static int counted_actor_sound(const mobj_t *actor, soundevent_t event) {
    if (event == SE_ATTACK && actor == attack_sound_origin) attack_sounds++;
    return real_sounds->actor_sound(actor, event);
}
static mobj_t *spawn(int type, fixed2_t at, int owner) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fixed2(at, 0), (uint16_t)type);
    if (u) {
        u->owner = u->team = (uint8_t)owner;
        u->allegiance = owner == consoleplayer ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
        sc_start(u);
    }
    return u;
}
static void tick(int tics) { for (int t = 0; t < tics; t++) { P_Ticker(); G_ProductionTicker(RTS_TICK_MS); } }
static void reset(void) {
    P_FreeThinkers();
    P_InitThinkers();
    memset(level.upgrades, 0, sizeof(level.upgrades));
}
static void upgrade(int owner, int id) { level.upgrades[id][owner].weapon = 1; }
static int frames_ms(int frames) { return (frames * 1000 + 23) / 24; }
static int base_frames(const mobj_t *u) {
    const sc_unit_t *hull = &sc_units[u->type_id - 1];
    return sc_weapon(hull->ground_weapon)->cooldown;
}
static fixed2_t at_of(const mobj_t *mo) { return fixed3_xy(mo->core.position); }
static sc_mission_t *mission(void) { return level.mission; }
/* The LCG roll G_WeaponCooldown will take next, without taking it. */
static int next_jitter(void) { return (int)(((mission()->rng * UINT32_C(22695477) + 1) >> 16) & 3) - 1; }
static void spores(mobj_t *u, int n) { for (int i = 0; i < n; i++) sc_add_acid_spore(u); }

/* Every term and the order they apply in, on whole frames. */
static int formula(void) {
    reset();
    mobj_t *marine = spawn(MT_MARINE, FIXED2_LIT(4.5, 4.5), 0), *ling = spawn(MT_ZERGLING, FIXED2_LIT(6.5, 4.5), 0);
    CHECK(marine && ling && base_frames(marine) == 15 && base_frames(ling) == 8);
    CHECK(sc_modified_cooldown_frames(marine, 15) == 15);
    marine->sc.timers[SC_TIMER_STIM] = 100;
    CHECK(sc_modified_cooldown_frames(marine, 15) == 7); /* halved, rounded down */
    marine->sc.timers[SC_TIMER_ENSNARE] = 100;
    CHECK(sc_modified_cooldown_frames(marine, 15) == 15); /* one up, one down */
    marine->sc.timers[SC_TIMER_STIM] = 0;
    CHECK(sc_modified_cooldown_frames(marine, 15) == 18); /* a quarter longer */
    CHECK(sc_modified_cooldown_frames(marine, 7) == 8);
    marine->sc.timers[SC_TIMER_ENSNARE] = 0;
    /* Adrenal Glands: a Zergling's 8 frames would be 4; the floor is 5. */
    CHECK(sc_modified_cooldown_frames(ling, 8) == 8);
    upgrade(0, 28);
    CHECK(sc_modified_cooldown_frames(ling, 8) == 5);
    CHECK(sc_modified_cooldown_frames(ling, 10) == 5 && sc_modified_cooldown_frames(ling, 11) == 5);
    CHECK(sc_modified_cooldown_frames(ling, 12) == 6);
    ling->sc.timers[SC_TIMER_STIM] = 100; /* two ups still halve once */
    CHECK(sc_modified_cooldown_frames(ling, 12) == 6);
    ling->sc.timers[SC_TIMER_STIM] = 0;
    ling->sc.timers[SC_TIMER_ENSNARE] = 100; /* adrenal cancels ensnare */
    CHECK(sc_modified_cooldown_frames(ling, 8) == 8);
    reset();

    /* Acid spores: an eighth of the base, at least 3, per spore, first. */
    marine = spawn(MT_MARINE, FIXED2_LIT(4.5, 4.5), 0);
    CHECK(marine);
    int counts[3] = {1, 5, 9};
    for (int i = 0; i < 3; i++) {
        marine->sc.timers[SC_TIMER_STIM] = 0;
        memset(marine->sc.acid, 0, sizeof(marine->sc.acid));
        spores(marine, counts[i]);
        CHECK(sc_acid_spores(marine) == counts[i]);
        CHECK(sc_modified_cooldown_frames(marine, 15) == 15 + 3 * counts[i]);
        marine->sc.timers[SC_TIMER_STIM] = 100; /* the sum is halved, not the base */
        CHECK(sc_modified_cooldown_frames(marine, 15) == (15 + 3 * counts[i]) / 2);
        marine->sc.timers[SC_TIMER_STIM] = 0;
        marine->sc.timers[SC_TIMER_ENSNARE] = 100;
        CHECK(sc_modified_cooldown_frames(marine, 15) == (15 + 3 * counts[i]) + (15 + 3 * counts[i]) / 4);
        marine->sc.timers[SC_TIMER_ENSNARE] = 0;
    }
    memset(marine->sc.acid, 0, sizeof(marine->sc.acid));
    spores(marine, 2);
    CHECK(sc_modified_cooldown_frames(marine, 23) == 23 + 2 * 3); /* 23/8 = 2: the floor of 3 applies */
    CHECK(sc_modified_cooldown_frames(marine, 24) == 24 + 2 * 3);
    CHECK(sc_modified_cooldown_frames(marine, 32) == 32 + 2 * 4);
    memset(marine->sc.acid, 0, sizeof(marine->sc.acid));
    spores(marine, 9);
    CHECK(sc_modified_cooldown_frames(marine, 120) == 250); /* 120 + 135 clamps */
    CHECK(sc_modified_cooldown_frames(marine, 100) == 100 + 12 * 9);
    memset(marine->sc.acid, 0, sizeof(marine->sc.acid));
    /* Clamp bounds, upper and lower. */
    CHECK(sc_modified_cooldown_frames(marine, 250) == 250 && sc_modified_cooldown_frames(marine, 251) == 250);
    CHECK(sc_modified_cooldown_frames(marine, 1000) == 250 && sc_modified_cooldown_frames(marine, 1) == 5);
    marine->sc.timers[SC_TIMER_ENSNARE] = 100;
    CHECK(sc_modified_cooldown_frames(marine, 200) == 250 && sc_modified_cooldown_frames(marine, 199) == 248);
    CHECK(sc_modified_cooldown_frames(marine, 240) == 250);
    marine->sc.timers[SC_TIMER_ENSNARE] = 0;
    marine->sc.timers[SC_TIMER_STIM] = 100;
    CHECK(sc_modified_cooldown_frames(marine, 9) == 5 && sc_modified_cooldown_frames(marine, 10) == 5);
    CHECK(sc_modified_cooldown_frames(marine, 11) == 5 && sc_modified_cooldown_frames(marine, 12) == 6);
    CHECK(sc_modified_cooldown_frames(marine, 600) == 250);
    reset();
    return 0;
}

/* The spore list: nine at most, the one nearest to expiring is renewed,
 * each runs 150 frames, nothing survives a save mismatch. */
static int spore_list(void) {
    reset();
    mobj_t *u = spawn(MT_MARINE, FIXED2_LIT(4.5, 4.5), 0);
    CHECK(u && !sc_acid_spores(u));
    sc_add_acid_spore(u);
    sc_acid_tick(u, 40);
    sc_add_acid_spore(u);
    CHECK(sc_acid_spores(u) == 2);
    sc_acid_tick(u, 110);
    CHECK(sc_acid_spores(u) == 1); /* the first lapses after 150 */
    sc_acid_tick(u, 39);
    CHECK(sc_acid_spores(u) == 1);
    sc_acid_tick(u, 1);
    CHECK(!sc_acid_spores(u));
    spores(u, 9);
    sc_acid_tick(u, 100);
    sc_add_acid_spore(u); /* a tenth refreshes one slot, the count stays */
    CHECK(sc_acid_spores(u) == 9);
    for (int i = 0; i < 9; i++) CHECK(u->sc.acid[i] == 50 || u->sc.acid[i] == SC_ACID_FRAMES);
    int renewed = 0;
    for (int i = 0; i < 9; i++) renewed += u->sc.acid[i] == SC_ACID_FRAMES;
    CHECK(renewed == 1);
    /* The ordinary ticker counts them down on the 24 Hz frame. */
    memset(u->sc.acid, 0, sizeof(u->sc.acid));
    sc_add_acid_spore(u);
    int frames = 0;
    while (sc_acid_spores(u) && frames < 400) { tick(1); frames += (leveltime - 1) * 24 / RTS_TICRATE - (leveltime - 2) * 24 / RTS_TICRATE; }
    CHECK(frames >= SC_ACID_FRAMES - 1 && frames <= SC_ACID_FRAMES + 1);
    reset();
    return 0;
}

/* The hook the attack path calls: modified frames plus one roll. */
static int shot_cooldown(void) {
    reset();
    mobj_t *marine = spawn(MT_MARINE, FIXED2_LIT(4.5, 4.5), 0), *ling = spawn(MT_ZERGLING, FIXED2_LIT(6.5, 4.5), 0);
    CHECK(marine && ling);
    int ms = frames_ms(base_frames(marine));
    /* The roll covers -1..+2 and each is seen. */
    bool seen[4] = {0};
    for (uint32_t seed = 1; seed < 64; seed++) {
        mission()->rng = seed;
        int jitter = next_jitter(), before = (int)mission()->rng;
        int got = G_WeaponCooldown(marine, &marine->info->attack, ms);
        CHECK(got == frames_ms(15 + jitter));
        CHECK((int)mission()->rng != before); /* exactly one roll taken */
        seen[jitter + 1] = true;
    }
    CHECK(seen[0] && seen[1] && seen[2] && seen[3]);
    uint32_t seed = 1;
    mission()->rng = seed;
    uint32_t once = seed * UINT32_C(22695477) + 1;
    (void)G_WeaponCooldown(marine, &marine->info->attack, ms);
    CHECK(mission()->rng == once);
    struct { int stim, ensnare, spores; bool adrenal; mobj_t *u; int base, expect; } rows[] = {
        {0, 0, 0, false, marine, 15, 15},     {1, 0, 0, false, marine, 15, 7},
        {0, 1, 0, false, marine, 15, 18},     {1, 1, 0, false, marine, 15, 15},
        {0, 0, 1, false, marine, 15, 18},     {0, 0, 5, false, marine, 15, 30},
        {0, 0, 9, false, marine, 15, 42},     {1, 0, 9, false, marine, 15, 21},
        {0, 0, 0, false, ling, 8, 8},         {0, 0, 0, true, ling, 8, 5},
        {0, 0, 5, true, ling, 8, 11}, /* 8 + 15 = 23, halved */
    };
    for (unsigned i = 0; i < sizeof(rows) / sizeof(*rows); i++) {
        mobj_t *u = rows[i].u;
        u->sc.timers[SC_TIMER_STIM] = rows[i].stim ? 100 : 0;
        u->sc.timers[SC_TIMER_ENSNARE] = rows[i].ensnare ? 100 : 0;
        memset(u->sc.acid, 0, sizeof(u->sc.acid));
        spores(u, rows[i].spores);
        memset(level.upgrades, 0, sizeof(level.upgrades));
        if (rows[i].adrenal) upgrade(0, 28);
        mission()->rng = 7 + i;
        int jitter = next_jitter();
        CHECK(base_frames(u) == rows[i].base);
        CHECK(G_WeaponCooldown(u, &u->info->attack, frames_ms(rows[i].base)) == frames_ms(rows[i].expect + jitter));
    }
    reset();
    return 0;
}

/* Shot to shot in the running attack path, in engine tics. */
static int shot_interval(mobj_t *shooter, mobj_t *target, int shots, int *first_ms) {
    ticcmd_t attack = {.order = TC_ATTACK, .count = 1, .units = {shooter->id}, .target = target->id};
    G_RunTiccmd(0, &attack);
    int last = -1, interval = -1, previous = 0;
    for (int t = 0; t < 400 && shots > 0; t++) {
        tick(1);
        if (shooter->attack.cooldown_left_ms > previous) {
            if (last >= 0) { interval = t - last; shots--; }
            else *first_ms = shooter->attack.cooldown_left_ms;
            last = t;
        }
        previous = shooter->attack.cooldown_left_ms;
    }
    return interval;
}
static int running_attack(void) {
    reset();
    mobj_t *marine = spawn(MT_MARINE, FIXED2_LIT(8.5, 8.5), 0);
    mobj_t *target = spawn(MT_ZERGLING, FIXED2_LIT(11.5, 8.5), 1);
    CHECK(marine && target);
    target->traits &= ~(MF_ATTACK | MF_MOBILE);
    target->hp = target->max_hp = 20000;
    int first = 0, plain = shot_interval(marine, target, 1, &first);
    /* 15 frames plus a roll of -1..+2 is 14..17 frames: 17..21 tics. */
    CHECK(plain >= 17 && plain <= 21);
    marine->sc.timers[SC_TIMER_STIM] = 1000;
    marine->attack.cooldown_left_ms = 0;
    int stimmed = shot_interval(marine, target, 1, &first);
    CHECK(stimmed >= 8 && stimmed <= 12 && stimmed < plain);
    marine->sc.timers[SC_TIMER_STIM] = 0;
    marine->sc.timers[SC_TIMER_ENSNARE] = 1000;
    marine->attack.cooldown_left_ms = 0;
    int ensnared = shot_interval(marine, target, 1, &first);
    CHECK(ensnared >= 21 && ensnared > plain); /* 17..20 frames */
    marine->sc.timers[SC_TIMER_STIM] = 1000;
    marine->attack.cooldown_left_ms = 0;
    int both = shot_interval(marine, target, 1, &first);
    CHECK(both >= plain - 4 && both <= plain + 4); /* they cancel */
    marine->sc.timers[SC_TIMER_STIM] = marine->sc.timers[SC_TIMER_ENSNARE] = 0;
    spores(marine, 9);
    marine->attack.cooldown_left_ms = 0;
    int acid = shot_interval(marine, target, 1, &first);
    CHECK(acid >= 41 && acid > ensnared); /* 41..44 frames */
    reset();
    return 0;
}

/* A passenger set down by a mobile transport takes the same modified value,
 * with no roll. */
static int unload_value(void) {
    reset();
    mobj_t *ship = spawn(MT_SHUTTLE, FIXED2_LIT(8.5, 8.5), 0), *p = spawn(MT_MARINE, FIXED2_LIT(8.5, 8.5), 0);
    CHECK(ship && p && sc_board(p, ship) && (p->sc.flags & SC_LOADED));
    spores(p, 5);
    p->sc.timers[SC_TIMER_ENSNARE] = 500;
    uint32_t rng = mission()->rng;
    sc_unload_at(ship, fixed3_xy(ship->core.position));
    for (int t = 0; t < 8 && (p->sc.flags & SC_LOADED); t++) tick(1);
    CHECK(!(p->sc.flags & SC_LOADED) && mission()->rng == rng);
    int expect = 30 + 30 / 4; /* 15 + 5 * 3 spores, then Ensnare */
    CHECK(p->sc.timers[SC_TIMER_GROUND_COOLDOWN] >= expect - 1 && p->sc.timers[SC_TIMER_GROUND_COOLDOWN] <= expect);
    reset();
    return 0;
}

/* Spores are saved and hashed. */
static int spores_saved(void) {
    reset();
    mobj_t *u = spawn(MT_MARINE, FIXED2_LIT(4.5, 4.5), 0);
    CHECK(u);
    spores(u, 3);
    sc_acid_tick(u, 20);
    uint32_t id = u->id, hash = G_Consistency();
    u->sc.acid[0]++;
    CHECK(G_Consistency() != hash);
    u->sc.acid[0]--;
    CHECK(G_Consistency() == hash);
    app_t app = {0};
    AiContext ai = {0};
    hudtext_t hud = {0};
    void *blob = NULL;
    size_t length = 0;
    CHECK(G_SaveGameBlob(&blob, &length, "cooldowns", &app, &ai, &hud));
    P_FreeLevel(&level);
    P_InitThinkers();
    CHECK(G_DoLoadLevel(map, &level) && P_InitSight());
    CHECK(G_LoadGameBlob(blob, length, &app, &ai, &hud));
    free(blob);
    CHECK(G_Consistency() == hash);
    u = P_MobjById(id);
    CHECK(u && sc_acid_spores(u) == 3 && u->sc.acid[0] == 130);
    reset();
    return 0;
}

/* ---- spell cooldown ---- */
static void learn(int tech) { level.upgrades[SC_UPGRADES + tech][0].weapon = 1; }
static int tic_frames(void) { return (leveltime - 1) * 24 / RTS_TICRATE - (leveltime - 2) * 24 / RTS_TICRATE; }
static int timer(const mobj_t *u) { return u->sc.timers[SC_TIMER_SPELL_COOLDOWN]; }
static mobj_t *templar(fixed2_t at) {
    mobj_t *t = spawn(MT_HIGH_TEMPLAR, at, 0);
    if (t) t->sc.energy = 200 << 8;
    return t;
}
/* A cast in range goes off inside the command, between two tics. */
static bool cast_now(mobj_t *caster, int tech, const mobj_t *target, fixed2_t at) {
    int energy = caster->sc.energy;
    return sc_cast(caster, tech, (mobj_t *)target, at) && caster->sc.energy < energy;
}
/* Native frames, counting every tic after the call, until the caster casts;
 * -1 if it never does. The order must stay pending and the energy whole
 * until then. */
static int frames_until_cast(mobj_t *caster, int limit) {
    int energy = caster->sc.energy, frames = 0;
    for (int t = 0; t < limit; t++) {
        tick(1);
        frames += tic_frames();
        if (caster->sc.energy < energy) return frames;
        if (caster->sc.order.kind != SC_ORDER_CAST) return -2;
    }
    return -1;
}
static int spell_rearm(void) {
    reset();
    learn(SC_TECH_PSIONIC_STORM); learn(SC_TECH_EMP);
    mobj_t *a = templar(FIXED2_LIT(8.5, 8.5)), *b = templar(FIXED2_LIT(8.5, 12.5)), *c = templar(FIXED2_LIT(8.5, 16.5));
    CHECK(a && b && c);
    c->sc.timers[SC_TIMER_ENSNARE] = 1000;
    CHECK(!timer(a) && !timer(b));
    CHECK(cast_now(a, SC_TECH_PSIONIC_STORM, NULL, FIXED2_LIT(12.5, 8.5))); /* nothing held the first cast */
    /* Storm's weapon is 45 frames; the roll makes it 44..47. */
    int first = timer(a);
    CHECK(first >= 44 && first <= 47);
    /* The same caster again is kept waiting with its energy untouched, then
     * casts on exactly the frame the timer runs out. */
    int energy = a->sc.energy;
    CHECK(sc_cast(a, SC_TECH_PSIONIC_STORM, NULL, FIXED2_LIT(12.5, 8.5)) && a->sc.energy == energy);
    CHECK(a->sc.order.kind == SC_ORDER_CAST);
    CHECK(frames_until_cast(a, 100) == first);
    CHECK(timer(a) >= 44 && timer(a) <= 47); /* and is armed again */
    /* Another caster is not held by it. */
    CHECK(cast_now(b, SC_TECH_PSIONIC_STORM, NULL, FIXED2_LIT(12.5, 12.5)) && timer(b) >= 44);
    /* Ensnare lengthens it: 45 + 45 / 4 = 56, rolled 55..58. */
    CHECK(cast_now(c, SC_TECH_PSIONIC_STORM, NULL, FIXED2_LIT(12.5, 16.5)) && timer(c) >= 55 && timer(c) <= 58);
    reset();

    /* Spells outside order_Spell ignore it: Hallucination, Defensive Matrix
     * and a cloak cast at once during the cooldown, and leave the timer. */
    learn(SC_TECH_PSIONIC_STORM); learn(SC_TECH_HALLUCINATION); learn(SC_TECH_EMP); learn(SC_TECH_PERSONNEL_CLOAKING);
    a = templar(FIXED2_LIT(8.5, 8.5));
    mobj_t *friend = spawn(MT_MARINE, FIXED2_LIT(9.5, 9.5), 0);
    mobj_t *vessel = spawn(MT_SCIENCE_VESSEL, FIXED2_LIT(8.5, 16.5), 0), *ghost = spawn(MT_GHOST, FIXED2_LIT(30.5, 16.5), 0);
    CHECK(a && friend && vessel && ghost);
    vessel->sc.energy = ghost->sc.energy = 200 << 8;
    CHECK(cast_now(a, SC_TECH_PSIONIC_STORM, NULL, FIXED2_LIT(12.5, 8.5)));
    int held = timer(a);
    CHECK(held > 0 && cast_now(a, SC_TECH_HALLUCINATION, friend, at_of(friend)) && timer(a) == held);
    CHECK(cast_now(vessel, SC_TECH_EMP, NULL, FIXED2_LIT(12.5, 16.5)));
    held = timer(vessel);
    CHECK(held > 0 && cast_now(vessel, SC_TECH_DEFENSIVE_MATRIX, friend, at_of(friend)) && timer(vessel) == held);
    CHECK(sc_cast(ghost, SC_TECH_PERSONNEL_CLOAKING, NULL, (fixed2_t){0}) && (ghost->traits & MF_CLOAKED));
    CHECK(!timer(ghost));
    reset();
    return 0;
}

static int spell_save(void) {
    reset();
    learn(SC_TECH_YAMATO_GUN);
    mobj_t *a = spawn(MT_BATTLECRUISER, FIXED2_LIT(8.5, 8.5), 0), *dummy = spawn(MT_ZERGLING, FIXED2_LIT(12.5, 8.5), 1);
    CHECK(a && dummy);
    dummy->traits &= ~(MF_ATTACK | MF_MOBILE);
    dummy->hp = dummy->max_hp = 20000;
    a->sc.energy = 200 << 8;
    CHECK(cast_now(a, SC_TECH_YAMATO_GUN, dummy, at_of(dummy)));
    while (timer(a) > 8) tick(1);
    int remaining = timer(a);
    CHECK(remaining > 0 && remaining <= 8);
    uint32_t dummy_id = dummy->id;
    uint32_t id = a->id, hash = G_Consistency();
    app_t app = {0};
    AiContext ai = {0};
    hudtext_t hud = {0};
    void *blob = NULL;
    size_t length = 0;
    CHECK(G_SaveGameBlob(&blob, &length, "spell cooldown", &app, &ai, &hud));
    P_FreeLevel(&level);
    P_InitThinkers();
    CHECK(G_DoLoadLevel(map, &level) && P_InitSight());
    CHECK(G_LoadGameBlob(blob, length, &app, &ai, &hud));
    free(blob);
    CHECK(G_Consistency() == hash);
    a = P_MobjById(id);
    dummy = P_MobjById(dummy_id);
    CHECK(a && dummy && timer(a) == remaining);
    a->sc.energy = 200 << 8; /* enough for the cast, though Yamato took 150 */
    CHECK(sc_cast(a, SC_TECH_YAMATO_GUN, dummy, at_of(dummy)));
    CHECK(frames_until_cast(a, 100) == remaining);
    reset();
    return 0;
}

int main(void) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    G_InitGame();
    P_InitThinkers();
    consoleplayer = 0;
    real_sounds = gameinfo->sound;
    counting_sounds = *real_sounds;
    counting_sounds.actor_sound = counted_actor_sound;
    game_info.sound = &counting_sounds;
    CHECK(S_Init(root));
    CHECK(G_DoLoadLevel(map, &level) && P_InitSight());
    CHECK(!formula());
    CHECK(!spore_list());
    CHECK(!shot_cooldown());
    CHECK(!running_attack());
    CHECK(!unload_value());
    CHECK(!spell_rearm());
    CHECK(!spell_save());
    CHECK(!spores_saved());
    S_Shutdown();
    puts("PASS: StarCraft weapon cooldowns");
    return 0;
}
