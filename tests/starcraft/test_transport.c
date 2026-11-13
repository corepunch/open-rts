#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c, "StarCraft transport", #c)
/* What a passenger may do after a mobile transport sets it down. OpenBW
 * unit_unload gives a Reaver a 30-frame main-order timer (its attack order
 * waits on it), and every other passenger a 30-frame spell cooldown plus the
 * modified cooldown of each weapon its own hull carries, ground and air
 * apart. A Bunker hands out none of these. The attack gate must spend
 * nothing while it holds: no cooldown, no sound, no Scarab. */

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
static fixed2_t at_of(const mobj_t *mo) { return fixed3_xy(mo->core.position); }
static void tick(int tics) {
    for (int t = 0; t < tics; t++) { P_Ticker(); G_ProductionTicker(RTS_TICK_MS); }
}
static void reset(void) {
    P_FreeThinkers();
    P_InitThinkers();
    memset(level.upgrades, 0, sizeof(level.upgrades));
}
/* A target that cannot shoot back or run away. */
static mobj_t *dummy(int type, fixed2_t at) {
    mobj_t *t = spawn(type, at, 1);
    if (t) { t->traits &= ~(MF_ATTACK | MF_MOBILE); t->hp = t->max_hp = 20000; }
    return t;
}
static mobj_t *carry(int ship_type, int passenger_type, fixed2_t at, mobj_t **ship_out) {
    mobj_t *ship = spawn(ship_type, at, 0), *p = spawn(passenger_type, at, 0);
    if (!ship || !p || !sc_board(p, ship) || !(p->sc.flags & SC_LOADED)) return NULL;
    *ship_out = ship;
    return p;
}
/* Unload All on the spot, one tic at a time until the passenger is down. */
static bool unload(mobj_t *ship, mobj_t *p) {
    sc_unload_at(ship, at_of(ship));
    for (int t = 0; t < 8 && (p->sc.flags & SC_LOADED); t++) tick(1);
    return !(p->sc.flags & SC_LOADED);
}

/* Native 24 Hz frames the tic just run spanned: the thinkers see leveltime
 * before P_Ticker advances it. */
static int tic_frames(void) { return (leveltime - 1) * 24 / RTS_TICRATE - (leveltime - 2) * 24 / RTS_TICRATE; }
static int timer_of(const mobj_t *mo, int timer) { return mo->sc.timers[timer]; }
static bool weapon_timers_idle(const mobj_t *mo) {
    return !timer_of(mo, SC_TIMER_GROUND_COOLDOWN) && !timer_of(mo, SC_TIMER_AIR_COOLDOWN) &&
           !timer_of(mo, SC_TIMER_SPELL_COOLDOWN);
}
/* Runs tics until the Reaver launches a Scarab or plays its attack sound,
 * which are one event. While its timer is up nothing may be spent. Returns
 * the native frames counted from the first tic run, or -1 if it never fired. */
static int frames_to_fire(mobj_t *reaver, int limit_tics) {
    int frames = 0;
    for (int t = 0; t < limit_tics; t++) {
        int hangar = reaver->sc.hangar, sounds = attack_sounds, timer = timer_of(reaver, SC_TIMER_MAIN_ORDER);
        tick(1);
        frames += tic_frames();
        bool fired = reaver->sc.hangar != hangar || attack_sounds != sounds;
        if (timer_of(reaver, SC_TIMER_MAIN_ORDER) > 0 || (timer > 0 && !fired)) {
            /* Still delayed at the end of this tic: nothing was spent. */
            if (fired || reaver->attack.cooldown_left_ms || reaver->sc.hangar != hangar || attack_sounds != sounds) return -2;
        }
        if (fired) return frames;
    }
    return -1;
}

static int reaver_delay(void) {
    reset();
    mobj_t *ship, *reaver = carry(MT_SHUTTLE, MT_REAVER, FIXED2_LIT(8.5, 8.5), &ship);
    CHECK(reaver);
    reaver->sc.hangar = 5;
    mobj_t *target = dummy(MT_ZERGLING, FIXED2_LIT(12.5, 8.5));
    CHECK(target);
    attack_sound_origin = reaver;
    attack_sounds = 0;
    CHECK(unload(ship, reaver));
    int timer = timer_of(reaver, SC_TIMER_MAIN_ORDER);
    CHECK(timer == 29 || timer == 30);
    CHECK(weapon_timers_idle(reaver)); /* A Reaver takes the timer alone. */
    CHECK(reaver->attack.cooldown_left_ms == 0);
    /* Blocked attempts spend nothing: no cooldown, no Scarab, no sound. */
    reaver->attack.target = target;
    for (int t = 0; t < 40 && !P_VisibleTo(reaver, target); t++) tick(1); /* Sight catches up. */
    CHECK(P_VisibleTo(reaver, target));
    timer = timer_of(reaver, SC_TIMER_MAIN_ORDER);
    CHECK(timer > 15 && reaver->sc.hangar == 5 && attack_sounds == 0);
    CHECK(!P_Attack(reaver) && !P_Attack(reaver));
    CHECK(reaver->attack.cooldown_left_ms == 0 && reaver->sc.hangar == 5 && attack_sounds == 0);
    CHECK(reaver->attack.target == target); /* The order survives the wait. */
    /* Move and Stop answer at once. */
    ticcmd_t move = {.order = TC_MOVE, .count = 1, .units = {reaver->id}, .position = fixed3_from_fixed2(FIXED2_LIT(8.5, 12.5), 0)};
    G_RunTiccmd(0, &move);
    CHECK(P_HasMoveOrder(reaver));
    ticcmd_t stop = {.order = TC_STOP, .count = 1, .units = {reaver->id}};
    G_RunTiccmd(0, &stop);
    CHECK(!P_HasMoveOrder(reaver) && timer_of(reaver, SC_TIMER_MAIN_ORDER) == timer);
    /* An explicit attack ordered on the first frame fires on the very
     * frame the timer runs out and not a frame sooner. */
    ticcmd_t attack = {.order = TC_ATTACK, .count = 1, .units = {reaver->id}, .target = target->id};
    G_RunTiccmd(0, &attack);
    CHECK(frames_to_fire(reaver, 100) == timer);
    CHECK(attack_sounds == 1 && reaver->sc.hangar == 4 && reaver->attack.cooldown_left_ms > 0);
    CHECK(timer_of(reaver, SC_TIMER_MAIN_ORDER) == 0);

    /* The same through automatic acquisition: no order, a target in reach. */
    reset();
    reaver = carry(MT_SHUTTLE, MT_REAVER, FIXED2_LIT(8.5, 8.5), &ship);
    CHECK(reaver);
    reaver->sc.hangar = 5;
    target = dummy(MT_ZERGLING, FIXED2_LIT(12.5, 8.5));
    CHECK(target);
    attack_sound_origin = reaver;
    attack_sounds = 0;
    CHECK(unload(ship, reaver));
    CHECK(timer_of(reaver, SC_TIMER_MAIN_ORDER) >= 29);
    for (int t = 0; t < 40 && !P_VisibleTo(reaver, target); t++) tick(1);
    CHECK(P_VisibleTo(reaver, target));
    timer = timer_of(reaver, SC_TIMER_MAIN_ORDER);
    CHECK(timer > 10 && reaver->sc.hangar == 5 && attack_sounds == 0);
    /* Never early; the idle state's own look cadence may add a frame or two
     * before it notices the target, which the explicit order above does not. */
    int auto_frames = frames_to_fire(reaver, 100);
    CHECK(auto_frames >= timer && auto_frames <= timer + 2);
    CHECK(attack_sounds == 1 && reaver->sc.hangar == 4);

    /* A Reaver that was never carried fires at once, so the delay is the
     * transport's doing, and a Bunker-free ground passenger is not held by
     * the Reaver's main-order timer. */
    reset();
    reaver = spawn(MT_REAVER, FIXED2_LIT(8.5, 8.5), 0);
    target = dummy(MT_ZERGLING, FIXED2_LIT(12.5, 8.5));
    CHECK(reaver && target);
    reaver->sc.hangar = 5;
    attack_sound_origin = reaver;
    attack_sounds = 0;
    int frames = frames_to_fire(reaver, 100);
    CHECK(frames >= 0 && frames < 10);
    mobj_t *zergling = spawn(MT_ZERGLING, FIXED2_LIT(8.5, 14.5), 0);
    CHECK(zergling);
    zergling->sc.timers[SC_TIMER_MAIN_ORDER] = 30;
    zergling->attack.target = target;
    CHECK(zergling->attack.target && !(sc_hangar_type(zergling->type_id)));
    reset();
    return 0;
}

static int reaver_save_mid_delay(void) {
    reset();
    mobj_t *ship, *reaver = carry(MT_SHUTTLE, MT_REAVER, FIXED2_LIT(8.5, 8.5), &ship);
    CHECK(reaver);
    reaver->sc.hangar = 5;
    mobj_t *target = dummy(MT_ZERGLING, FIXED2_LIT(12.5, 8.5));
    CHECK(target);
    CHECK(unload(ship, reaver));
    ticcmd_t attack = {.order = TC_ATTACK, .count = 1, .units = {reaver->id}, .target = target->id};
    G_RunTiccmd(0, &attack);
    while (timer_of(reaver, SC_TIMER_MAIN_ORDER) > 12) tick(1);
    int saved_timer = timer_of(reaver, SC_TIMER_MAIN_ORDER);
    CHECK(saved_timer > 0 && saved_timer <= 12 && reaver->sc.hangar == 5);
    uint32_t reaver_id = reaver->id;
    uint32_t hash = G_Consistency();
    reaver->sc.timers[SC_TIMER_MAIN_ORDER]++;
    CHECK(G_Consistency() != hash); /* The timer is part of the hash. */
    reaver->sc.timers[SC_TIMER_MAIN_ORDER]--;
    CHECK(G_Consistency() == hash);
    app_t app = {0};
    AiContext ai = {0};
    hudtext_t hud = {0};
    void *blob = NULL;
    size_t length = 0;
    CHECK(G_SaveGameBlob(&blob, &length, "transport", &app, &ai, &hud));
    P_FreeLevel(&level);
    P_InitThinkers();
    CHECK(G_DoLoadLevel(map, &level) && P_InitSight());
    CHECK(G_LoadGameBlob(blob, length, &app, &ai, &hud));
    free(blob);
    CHECK(G_Consistency() == hash);
    reaver = P_MobjById(reaver_id);
    CHECK(reaver && timer_of(reaver, SC_TIMER_MAIN_ORDER) == saved_timer && reaver->sc.hangar == 5);
    attack_sound_origin = reaver;
    attack_sounds = 0;
    /* It resumes the wait where it stopped and then fires on the frame. */
    CHECK(frames_to_fire(reaver, 100) == saved_timer);
    CHECK(attack_sounds == 1 && reaver->sc.hangar == 4);
    reset();
    return 0;
}

/* One tic at a time until the Marine fires; true when it did. */
static bool fires_within(int tics) {
    int sounds = attack_sounds;
    for (int t = 0; t < tics; t++) {
        tick(1);
        if (attack_sounds != sounds) return true;
    }
    return false;
}
/* Native frames, counted from the next tic, until `unit` fires; -1 if never. */
static int frames_until_fire(int tics) {
    int sounds = attack_sounds, frames = 0;
    for (int t = 0; t < tics; t++) {
        tick(1);
        frames += tic_frames();
        if (attack_sounds != sounds) return frames;
    }
    return -1;
}

static int passenger_cooldowns(void) {
    reset();
    mobj_t *ship, *marine = carry(MT_DROPSHIP, MT_MARINE, FIXED2_LIT(8.5, 8.5), &ship);
    CHECK(marine);
    marine->sc.timers[SC_TIMER_STIM] = 1000;
    int expected = sc_modified_cooldown_frames(marine, sc_weapon(sc_units[MT_MARINE - 1].ground_weapon)->cooldown);
    CHECK(expected == 7); /* Gauss Rifle's 15 frames, hastened by Stim. */
    CHECK(unload(ship, marine));
    int passed = 0; /* Frames the passenger's own ticker already took. */
    int ground = timer_of(marine, SC_TIMER_GROUND_COOLDOWN), air = timer_of(marine, SC_TIMER_AIR_COOLDOWN);
    passed = expected - ground;
    CHECK(passed == 0 || passed == 1);
    /* Both weapons of the one Gauss Rifle row get the same value, set apart
     * from the shared engine cooldown, which stays untouched. */
    CHECK(ground == air && marine->attack.cooldown_left_ms == 0);
    CHECK(timer_of(marine, SC_TIMER_SPELL_COOLDOWN) == 30 - passed);
    CHECK(timer_of(marine, SC_TIMER_MAIN_ORDER) == 0);
    CHECK(timer_of(ship, SC_TIMER_MAIN_ORDER) == 15 - passed);

    /* A ground-only weapon gets no air timer. */
    reset();
    mobj_t *ling = carry(MT_OVERLORD, MT_ZERGLING, FIXED2_LIT(8.5, 8.5), &ship);
    CHECK(!ling); /* Without Ventral Sacs an Overlord carries nothing. */
    reset();
    ling = carry(MT_SHUTTLE, MT_ZERGLING, FIXED2_LIT(8.5, 8.5), &ship);
    CHECK(ling && unload(ship, ling));
    int base = sc_weapon(sc_units[MT_ZERGLING - 1].ground_weapon)->cooldown;
    CHECK(base == 8 && timer_of(ling, SC_TIMER_GROUND_COOLDOWN) >= base - 1 && timer_of(ling, SC_TIMER_GROUND_COOLDOWN) <= base);
    CHECK(timer_of(ling, SC_TIMER_AIR_COOLDOWN) == 0 && timer_of(ling, SC_TIMER_SPELL_COOLDOWN) >= 29);

    /* A hull without a weapon of its own (the Goliath's guns sit on its
     * turret) keeps no weapon timer, but still owes the spell cooldown. */
    reset();
    mobj_t *goliath = carry(MT_DROPSHIP, MT_GOLIATH, FIXED2_LIT(8.5, 8.5), &ship);
    CHECK(goliath && unload(ship, goliath));
    CHECK(!timer_of(goliath, SC_TIMER_GROUND_COOLDOWN) && !timer_of(goliath, SC_TIMER_AIR_COOLDOWN));
    CHECK(timer_of(goliath, SC_TIMER_SPELL_COOLDOWN) >= 29);

    /* Ground and air timers gate their own targets and nothing else. */
    reset();
    marine = spawn(MT_MARINE, FIXED2_LIT(8.5, 8.5), 0);
    mobj_t *ground_target = dummy(MT_ZERGLING, FIXED2_LIT(11.5, 8.5)), *air_target = dummy(MT_MUTALISK, FIXED2_LIT(8.5, 11.5));
    CHECK(marine && ground_target && air_target && (air_target->traits & MF_FLY));
    air_target->traits |= MF_FLY;
    attack_sound_origin = marine;
    attack_sounds = 0;
    marine->sc.timers[SC_TIMER_GROUND_COOLDOWN] = 40;
    marine->attack.target = ground_target;
    CHECK(!P_Attack(marine) && attack_sounds == 0 && marine->attack.cooldown_left_ms == 0);
    marine->attack.target = air_target;
    CHECK(P_Attack(marine) && attack_sounds == 1 && marine->attack.cooldown_left_ms > 0);
    marine->attack.cooldown_left_ms = 0;
    marine->sc.timers[SC_TIMER_GROUND_COOLDOWN] = 0;
    marine->sc.timers[SC_TIMER_AIR_COOLDOWN] = 40;
    marine->attack.target = air_target;
    CHECK(!P_Attack(marine) && attack_sounds == 1 && marine->attack.cooldown_left_ms == 0);
    marine->attack.target = ground_target;
    CHECK(P_Attack(marine) && attack_sounds == 2);

    /* Unloaded and left alone, a Marine fires on the frame its ground
     * cooldown ends. */
    reset();
    marine = carry(MT_DROPSHIP, MT_MARINE, FIXED2_LIT(8.5, 8.5), &ship);
    ground_target = dummy(MT_ZERGLING, FIXED2_LIT(11.5, 8.5));
    CHECK(marine && ground_target);
    attack_sound_origin = marine;
    attack_sounds = 0;
    CHECK(unload(ship, marine));
    ground = timer_of(marine, SC_TIMER_GROUND_COOLDOWN);
    CHECK(ground >= 14 && ground <= 15);
    ticcmd_t attack = {.order = TC_ATTACK, .count = 1, .units = {marine->id}, .target = ground_target->id};
    G_RunTiccmd(0, &attack);
    /* Never before the cooldown ends; the Marine's attack animation takes
     * a frame or two to reach the shot once the gate opens. */
    int fired = frames_until_fire(100);
    CHECK(fired >= ground && fired <= ground + 2);
    CHECK(!fires_within(0));
    reset();
    return 0;
}

static int spell_cooldown(void) {
    reset();
    level.upgrades[SC_UPGRADES + SC_TECH_PSIONIC_STORM][0].weapon = 1;
    level.upgrades[SC_UPGRADES + SC_TECH_HALLUCINATION][0].weapon = 1;
    mobj_t *ship, *templar = carry(MT_SHUTTLE, MT_HIGH_TEMPLAR, FIXED2_LIT(8.5, 8.5), &ship);
    CHECK(templar);
    templar->sc.energy = 200 << 8;
    CHECK(unload(ship, templar));
    int spell = timer_of(templar, SC_TIMER_SPELL_COOLDOWN);
    CHECK(spell == 29 || spell == 30);
    CHECK(!timer_of(templar, SC_TIMER_GROUND_COOLDOWN) && !timer_of(templar, SC_TIMER_AIR_COOLDOWN));
    /* Psionic Storm goes through order_Spell: held until the cooldown ends,
     * with the energy untouched and the cast order kept. */
    int energy = templar->sc.energy;
    CHECK(sc_cast(templar, SC_TECH_PSIONIC_STORM, NULL, FIXED2_LIT(12.5, 8.5)));
    CHECK(templar->sc.order.kind == SC_ORDER_CAST && templar->sc.energy == energy);
    int frames = 0;
    while (templar->sc.order.kind == SC_ORDER_CAST && frames < 60) {
        tick(1);
        frames += tic_frames();
        if (templar->sc.order.kind == SC_ORDER_CAST) CHECK(templar->sc.energy == energy);
    }
    CHECK(frames == spell && templar->sc.energy < energy);
    /* Hallucination is its own order in OpenBW and ignores the cooldown. */
    reset();
    level.upgrades[SC_UPGRADES + SC_TECH_HALLUCINATION][0].weapon = 1;
    templar = carry(MT_SHUTTLE, MT_HIGH_TEMPLAR, FIXED2_LIT(8.5, 8.5), &ship);
    CHECK(templar);
    templar->sc.energy = 200 << 8;
    mobj_t *friend = spawn(MT_MARINE, FIXED2_LIT(9.5, 9.5), 0);
    CHECK(friend && unload(ship, templar) && timer_of(templar, SC_TIMER_SPELL_COOLDOWN) > 0);
    energy = templar->sc.energy;
    CHECK(sc_cast(templar, SC_TECH_HALLUCINATION, friend, at_of(friend)));
    CHECK(templar->sc.energy < energy);
    reset();
    return 0;
}

static int bunker_exits(void) {
    reset();
    mobj_t *bunker = spawn(MT_BUNKER, FIXED2_LIT(20.5, 20.5), 0);
    mobj_t *marine = spawn(MT_MARINE, FIXED2_LIT(20.5, 22.5), 0);
    CHECK(bunker && marine);
    P_SyncBuildingBlocking();
    CHECK(sc_board(marine, bunker));
    for (int t = 0; t < 120 && !(marine->sc.flags & SC_LOADED); t++) tick(1);
    CHECK(marine->sc.flags & SC_LOADED);
    sc_unload(bunker);
    CHECK(!(marine->sc.flags & SC_LOADED));
    CHECK(weapon_timers_idle(marine) && !timer_of(marine, SC_TIMER_MAIN_ORDER) && !timer_of(bunker, SC_TIMER_MAIN_ORDER));
    CHECK(marine->attack.cooldown_left_ms == 0);
    /* Cargo that steps out of a destroyed Bunker takes nothing either. */
    CHECK(sc_board(marine, bunker));
    for (int t = 0; t < 120 && !(marine->sc.flags & SC_LOADED); t++) tick(1);
    CHECK(marine->sc.flags & SC_LOADED);
    P_DamageMobj(bunker, NULL, bunker->hp);
    tick(3);
    CHECK(!(marine->sc.flags & SC_LOADED) && marine->hp > 0 && weapon_timers_idle(marine));
    reset();
    return 0;
}

static int unload_sequence_and_failures(void) {
    reset();
    mobj_t *ship, *first = carry(MT_SHUTTLE, MT_MARINE, FIXED2_LIT(8.5, 8.5), &ship);
    CHECK(first);
    mobj_t *second = spawn(MT_ZERGLING, FIXED2_LIT(8.5, 8.5), 0);
    CHECK(second && sc_board(second, ship) && (second->sc.flags & SC_LOADED));
    sc_unload_at(ship, at_of(ship));
    for (int t = 0; t < 8 && (first->sc.flags & SC_LOADED) && (second->sc.flags & SC_LOADED); t++) tick(1);
    mobj_t *down = (first->sc.flags & SC_LOADED) ? second : first, *aboard = down == first ? second : first;
    CHECK(!(down->sc.flags & SC_LOADED) && (aboard->sc.flags & SC_LOADED));
    CHECK(weapon_timers_idle(aboard)); /* Still aboard: nothing set yet. */
    int frames = 0;
    while ((aboard->sc.flags & SC_LOADED) && frames < 60) { tick(1); frames += tic_frames(); }
    CHECK(!(aboard->sc.flags & SC_LOADED) && frames >= 14 && frames <= 15);
    /* The second passenger starts its own 30 frames when it is put down;
     * the first has had 15 of them run. */
    CHECK(timer_of(aboard, SC_TIMER_SPELL_COOLDOWN) >= 29);
    CHECK(timer_of(down, SC_TIMER_SPELL_COOLDOWN) >= 14 && timer_of(down, SC_TIMER_SPELL_COOLDOWN) <= 16);

    /* A disabled transport sets nobody down and sets nothing. */
    reset();
    mobj_t *marine = carry(MT_DROPSHIP, MT_MARINE, FIXED2_LIT(8.5, 8.5), &ship);
    CHECK(marine);
    ship->sc.timers[SC_TIMER_STASIS] = 100;
    sc_unload_at(ship, at_of(ship));
    tick(5);
    CHECK((marine->sc.flags & SC_LOADED) && weapon_timers_idle(marine) && !timer_of(ship, SC_TIMER_MAIN_ORDER));
    ship->sc.timers[SC_TIMER_STASIS] = 0;

    /* Terrain that cannot hold the passenger at the origin fails the unload
     * and leaves it aboard with nothing set. */
    unsigned char *blocked = level.blocked;
    int cell = (int)(fixed2_cell(at_of(ship)).y * level.width + fixed2_cell(at_of(ship)).x);
    unsigned char saved = blocked[cell];
    blocked[cell] = 1;
    P_NavFree(&level);
    P_NavBeginTick();
    sc_unload_at(ship, at_of(ship));
    tick(5);
    CHECK((marine->sc.flags & SC_LOADED) && weapon_timers_idle(marine) && ship->sc.order.kind == SC_ORDER_NONE);
    blocked[cell] = saved;
    P_NavFree(&level);
    P_NavBeginTick();

    /* A transport shot down with the passenger aboard kills it; no timer
     * is ever handed out. */
    uint32_t id = marine->id;
    P_DamageMobj(ship, NULL, ship->hp);
    tick(3);
    CHECK(!P_MobjById(id) || P_MobjById(id)->hp <= 0);
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
    CHECK(!reaver_delay());
    CHECK(!reaver_save_mid_delay());
    CHECK(!passenger_cooldowns());
    CHECK(!spell_cooldown());
    CHECK(!bunker_exits());
    CHECK(!unload_sequence_and_failures());
    S_Shutdown();
    puts("PASS: StarCraft transport passenger timers");
    return 0;
}
