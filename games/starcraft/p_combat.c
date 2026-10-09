#include "sc_local.h"
/* StarCraft combat on the engine's weapon model: weapons.dat rows become
 * weapondef_t, every hit goes through damage types, armor and shields, and
 * casters keep energy, which pays for their spells. Rates are OpenBW's, in
 * 1/256 points per 24 Hz frame. */
enum {
    SC_UPGRADE_PLASMA_SHIELDS = 15,
    SC_SHIELD_REGEN = 7, SC_ENERGY_REGEN = 8, SC_CLOAK_DRAIN = 13,
    SC_START_ENERGY = 50 << 8, SC_MAX_ENERGY = 200 << 8, SC_REACTOR_ENERGY = 50 << 8,
    /* An interceptor strikes once a pass; weapons.dat's cooldown of 1 is
     * its attack script's repeat, so this engine times a pass instead. */
    SC_INTERCEPTOR_PASS = 30,
    SC_REAVER_COOLDOWN = 60, /* Retail Reaver: a scarab every 60 frames. */
};
/* A simple Shield Battery: each frame it restores up to two shield points
 * to a unit in reach, at one energy per two points, as the retail battery
 * trades energy for shields. Reach and rate are this engine's choice. */
#define SC_BATTERY_RANGE 4.0f
enum { SC_BATTERY_RATE = 2 << 8 };

const sc_weapon_t *sc_weapon(int id) {
    return id >= 0 && id < SC_WEAPONS && sc_weapons[id].name ? &sc_weapons[id] : NULL;
}

/* Attack scripts that strike twice for one cooldown (two attackmelee in
 * ZealotGndAttkRpt; the Firebat's flames). Brood War's units.dat records max
 * ground hits 2 for these; the classic DAT has no such column, and
 * weapons.dat's damage factor already gives the Goliath and Scout 2 air hits. */
static const mobjtype_id_t strikes_twice[] = {MT_FIREBAT, MT_ZEALOT, 10 + 1 /* Gui Montag */, 77 + 1 /* Fenix */};

static int frames_ms(int frames) { return (frames * 1000 + 23) / 24; }

static weapondef_t weapon(int id, uint8_t targets) {
    const sc_weapon_t *w = sc_weapon(id);
    if (!w || !w->damage) return (weapondef_t){0};
    uint8_t splash = w->explosion == SC_EXPLOSION_RADIAL ? SPLASH_RADIAL :
                     w->explosion == SC_EXPLOSION_ENEMY ? SPLASH_ENEMY :
                     w->explosion == SC_EXPLOSION_AIR ? SPLASH_AIR : SPLASH_NONE;
    return (weapondef_t){.range = (float)((w->max_range + 31) / 32), .min_range = w->min_range / 32.0f,
        .damage = w->damage, .cooldown_ms = frames_ms(w->cooldown), .targets = targets, .hits = (uint8_t)w->factor,
        .splash = splash, .radius = {w->splash[0] / 32.0f, w->splash[1] / 32.0f, w->splash[2] / 32.0f},
        /* OpenBW: a bouncing bullet strikes three targets in all. */
        .bounces = w->behavior == SC_BEHAVIOR_BOUNCE ? 2 : 0, .native_id = (uint16_t)id};
}

void sc_unit_weapons(int type, mobjtype_t *out) {
    const sc_unit_t *u = &sc_units[type - 1];
    /* Goliaths, tanks and turrets fire from their subunit; Carriers and
     * Reavers with what their hangar launches. */
    uint16_t child = sc_hangar_type((uint16_t)type);
    if (child && child != MT_NUCLEAR_MISSILE) u = &sc_units[child - 1];
    else if (!sc_weapon(u->ground_weapon) && !sc_weapon(u->air_weapon) && u->subunit < SC_TYPES)
        u = &sc_units[u->subunit];
    weapondef_t ground = weapon(u->ground_weapon, MOBJ_TARGET_GROUND), air = weapon(u->air_weapon, MOBJ_TARGET_AIR);
    out->air_attack = (weapondef_t){0};
    if (ground.damage && u->ground_weapon == u->air_weapon) {
        out->attack = ground;
        out->attack.targets = MOBJ_TARGET_GROUND | MOBJ_TARGET_AIR;
    } else if (ground.damage) {
        out->attack = ground;
        out->air_attack = air;
    } else out->attack = air;
    for (unsigned i = 0; i < sizeof(strikes_twice) / sizeof(*strikes_twice); i++)
        if (type == (int)strikes_twice[i]) out->attack.hits = 2;
    /* Both launchers reach 8 cells. The Carrier's four hits are its first
     * four interceptors, for the AI's strength, and it relaunches as often as
     * an interceptor passes. A scarab strikes on contact. */
    if (type == MT_CARRIER) {
        out->attack.range = 8;
        out->attack.hits = 4;
        out->attack.cooldown_ms = frames_ms(SC_INTERCEPTOR_PASS);
    } else if (type == MT_REAVER) {
        out->attack.range = 8;
        out->attack.cooldown_ms = frames_ms(SC_REAVER_COOLDOWN);
    } else if (type == MT_SCARAB) out->attack.range = 1;
    else if (type == MT_INTERCEPTOR) out->attack.cooldown_ms = frames_ms(SC_INTERCEPTOR_PASS);
    if (sc_units[type - 1].flags & SC_UNIT_PERMANENT_CLOAK) out->traits |= MF_CLOAKED;
}

static bool sc_type(const mobj_t *mo) { return sc_unit(mo) != NULL; }

/* Shields and energy take their spawn values on first use. */
void sc_start(mobj_t *mo) {
    if (!sc_type(mo) || (mo->sc.flags & SC_STARTED)) return;
    const sc_unit_t *u = &sc_units[mo->type_id - 1];
    mo->sc.flags |= SC_STARTED;
    mo->sc.shields = u->shields << 8;
    mo->sc.energy = (u->flags & SC_UNIT_SPELLCASTER) ? SC_START_ENERGY : 0;
}

int sc_shields(const mobj_t *mo) {
    if (!sc_type(mo)) return 0;
    return (mo->sc.flags & SC_STARTED ? mo->sc.shields : sc_units[mo->type_id - 1].shields << 8) >> 8;
}
int sc_energy(const mobj_t *mo) {
    if (!sc_type(mo) || !(sc_units[mo->type_id - 1].flags & SC_UNIT_SPELLCASTER)) return 0;
    return (mo->sc.flags & SC_STARTED ? mo->sc.energy : SC_START_ENERGY) >> 8;
}

/* Each caster's reactor upgrade (upgrades.dat) adds 50 to its 200. */
static const struct { mobjtype_id_t type; int upgrade; } reactors[] = {
    {MT_SCIENCE_VESSEL, 19}, {MT_GHOST, 21}, {MT_WRAITH, 22}, {MT_BATTLECRUISER, 23}, {MT_QUEEN, 31},
    {MT_DEFILER, 32}, {MT_HIGH_TEMPLAR, 40}, {MT_ARBITER, 44},
};
int sc_max_energy(const mobj_t *mo) {
    for (unsigned i = 0; i < sizeof(reactors) / sizeof(*reactors); i++)
        if (mo->type_id == reactors[i].type && sc_upgrade_level(mo->owner, reactors[i].upgrade))
            return SC_MAX_ENERGY + SC_REACTOR_ENERGY;
    return SC_MAX_ENERGY;
}

/* Percent damage by SC_DAMAGE_* and target SC_SIZE_*. */
static const uint8_t type_factor[5][4] = {
    {100, 100, 100, 100}, {100, 50, 75, 100}, {100, 100, 50, 25}, {100, 100, 100, 100}, {100, 100, 100, 100},
};

/* OpenBW: shields take the hit first, less Plasma Shields and whatever the
 * damage type; what passes them loses armor and is scaled by damage type
 * against size, at least half a point. Hit points keep the fraction. */
static int take(mobj_t *target, int type, int dealt) {
    const sc_unit_t *t = &sc_units[target->type_id - 1];
    if (target->sc.shields > 0) {
        int hit = dealt - (sc_upgrade_level(target->owner, SC_UPGRADE_PLASMA_SHIELDS) << 8);
        if (hit < 0) hit = 0;
        if (hit <= target->sc.shields) { target->sc.shields -= hit; return 0; }
        dealt = hit - target->sc.shields;
        target->sc.shields = 0;
    }
    if (type != SC_DAMAGE_IGNORE_ARMOR) dealt -= (t->armor + sc_upgrade_level(target->owner, t->armor_upgrade)) << 8;
    dealt = dealt * type_factor[type][t->size & 3] / 100;
    if (dealt < 128) dealt = 128;
    dealt += target->sc.wound;
    target->sc.wound = (uint8_t)(dealt & 255);
    return dealt >> 8;
}

/* The weapon's base damage plus its upgrade bonus, split for splash and
 * bounces, in 1/256 points. */
static int base_damage(int owner, const sc_weapon_t *w, int damage, int divisor) {
    if (w) damage += w->bonus * sc_upgrade_level(owner, w->upgrade);
    return (damage << 8) / divisor;
}

int sc_hit(int owner, const sc_weapon_t *w, mobj_t *target, int damage, int divisor) {
    if (!sc_type(target) || (target->sc.flags & SC_INVINCIBLE)) return 0;
    sc_start(target);
    return take(target, w && w->type < 5 ? w->type : SC_DAMAGE_NORMAL, base_damage(owner, w, damage, divisor));
}

static int hit_damage(const mobj_t *attacker, const weapondef_t *def, mobj_t *target, int damage, int divisor) {
    if (!sc_type(attacker) || !sc_type(target)) return damage / divisor;
    if (target->sc.flags & SC_INVINCIBLE) return 0;
    sc_start(target);
    const sc_weapon_t *w = sc_weapon(def->native_id);
    int dealt = sc_spell_hit(attacker, def, target, base_damage(attacker->owner, w, damage, divisor));
    return dealt > 0 ? take(target, w && w->type < 5 ? w->type : SC_DAMAGE_NORMAL, dealt) : 0;
}

static void recharge(mobj_t *battery, int frames) {
    fvec2_t at = fixed3_xy_to_fvec2(battery->core.position);
    for (thinker_t *th = thinkercap.next; th != &thinkercap && battery->sc.energy >= 128; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || mo == battery || mo->remove || mo->hp <= 0 || !sc_type(mo) ||
            !P_IsAlly(battery, mo) || fvec2_distance_squared(at, fixed3_xy_to_fvec2(mo->core.position)) >
            SC_BATTERY_RANGE * SC_BATTERY_RANGE) continue;
        sc_start(mo);
        int room = (sc_units[mo->type_id - 1].shields << 8) - mo->sc.shields;
        int give = SC_BATTERY_RATE * frames;
        if (give > room) give = room;
        if (give > battery->sc.energy * 2) give = battery->sc.energy * 2;
        if (give <= 0) continue;
        mo->sc.shields += give;
        battery->sc.energy -= (give + 1) / 2;
        return;
    }
}

/* Spells and unit orders run every tic (a locked-down unit must stay put);
 * then, once for each 24 Hz frame, shields regenerate and casters regain
 * energy, except while a cloak they switched on drains it. */
static void sc_mobj_ticker(mobj_t *mo) {
    if (!sc_type(mo)) return;
    sc_start(mo);
    int frames = leveltime * 24 / RTS_TICRATE - (leveltime - 1) * 24 / RTS_TICRATE;
    sc_spell_ticker(mo, frames);
    if (mo->remove || mo->hp <= 0) return;
    sc_unit_ticker(mo, frames);
    if (mo->remove || mo->hp <= 0 || frames <= 0) return;
    const sc_unit_t *u = &sc_units[mo->type_id - 1];
    if (mo->sc.shields < u->shields << 8) {
        mo->sc.shields += SC_SHIELD_REGEN * frames;
        if (mo->sc.shields > u->shields << 8) mo->sc.shields = u->shields << 8;
    }
    if (!(u->flags & SC_UNIT_SPELLCASTER)) return;
    if ((mo->traits & MF_CLOAKED) && !(mo->info->traits & MF_CLOAKED)) {
        mo->sc.energy -= SC_CLOAK_DRAIN * frames;
        if (mo->sc.energy <= 0) { mo->sc.energy = 0; mo->traits &= ~MF_CLOAKED; }
    } else if (mo->sc.energy < sc_max_energy(mo)) {
        mo->sc.energy += SC_ENERGY_REGEN * frames;
        if (mo->sc.energy > sc_max_energy(mo)) mo->sc.energy = sc_max_energy(mo);
    }
    if (mo->type_id == MT_SHIELD_BATTERY) recharge(mo, frames);
}

void sc_init_combat(void) {
    game_info.hit_damage = hit_damage;
    game_info.mobj_ticker = sc_mobj_ticker;
    game_info.attack = sc_launch;
    game_info.range_bonus = sc_range_bonus;
    game_info.sight_teams = sc_sight_teams;
}
