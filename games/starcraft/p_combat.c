#include "sc_local.h"
/* StarCraft combat on the engine's weapon model: weapons.dat rows become
 * weapondef_t, every hit goes through damage types, armor and shields, and
 * casters keep energy, which pays for cloaking. Rates are OpenBW's, in
 * 1/256 points per 24 Hz frame. */
enum {
    SC_UNIT_CLOAKABLE = 0x200, SC_UNIT_SPELLCASTER = 0x200000, SC_UNIT_PERMANENT_CLOAK = 0x400000,
    SC_UPGRADE_PLASMA_SHIELDS = 15,
    SC_SHIELD_REGEN = 7, SC_ENERGY_REGEN = 8, SC_CLOAK_DRAIN = 13,
    SC_START_ENERGY = 50 << 8, SC_MAX_ENERGY = 200 << 8,
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

static weapondef_t weapon(int id, uint8_t targets) {
    const sc_weapon_t *w = sc_weapon(id);
    if (!w || !w->damage) return (weapondef_t){0};
    uint8_t splash = w->explosion == SC_EXPLOSION_RADIAL ? SPLASH_RADIAL :
                     w->explosion == SC_EXPLOSION_ENEMY ? SPLASH_ENEMY :
                     w->explosion == SC_EXPLOSION_AIR ? SPLASH_AIR : SPLASH_NONE;
    return (weapondef_t){.range = (float)((w->max_range + 31) / 32), .damage = w->damage,
        .cooldown_ms = (w->cooldown * 1000 + 23) / 24, .targets = targets, .hits = (uint8_t)w->factor,
        .splash = splash, .radius = {w->splash[0] / 32.0f, w->splash[1] / 32.0f, w->splash[2] / 32.0f},
        /* OpenBW: a bouncing bullet strikes three targets in all. */
        .bounces = w->behavior == SC_BEHAVIOR_BOUNCE ? 2 : 0, .native_id = (uint16_t)id};
}

void sc_unit_weapons(int type, mobjtype_t *out) {
    const sc_unit_t *u = &sc_units[type - 1];
    /* Goliaths, tanks and turrets fire from their subunit. */
    if (!sc_weapon(u->ground_weapon) && !sc_weapon(u->air_weapon) && u->subunit < SC_TYPES)
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
    if (sc_units[type - 1].flags & SC_UNIT_PERMANENT_CLOAK) out->traits |= MF_CLOAKED;
}

static bool sc_type(const mobj_t *mo) { return mo && mo->type_id >= 1 && mo->type_id <= SC_TYPES; }

/* Shields and energy take their spawn values on first use. */
static void sc_start(mobj_t *mo) {
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

/* Percent damage by SC_DAMAGE_* and target SC_SIZE_*. */
static const uint8_t type_factor[5][4] = {
    {100, 100, 100, 100}, {100, 50, 75, 100}, {100, 100, 50, 25}, {100, 100, 100, 100}, {100, 100, 100, 100},
};

/* OpenBW weapon damage: base plus the weapon's upgrade bonus, split for
 * splash and bounces. Shields take it first, less Plasma Shields and whatever
 * the damage type; what passes them loses armor and is scaled by damage
 * type against size, at least half a point. Hit points keep the fraction. */
static int hit_damage(const mobj_t *attacker, const weapondef_t *def, mobj_t *target, int damage, int divisor) {
    if (!sc_type(attacker) || !sc_type(target)) return damage / divisor;
    if (target->sc.flags & SC_INVINCIBLE) return 0;
    sc_start(target);
    const sc_unit_t *t = &sc_units[target->type_id - 1];
    const sc_weapon_t *w = sc_weapon(def->native_id);
    int type = w && w->type < 5 ? w->type : SC_DAMAGE_NORMAL;
    if (w) damage += w->bonus * sc_upgrade_level(attacker->owner, w->upgrade);
    int dealt = (damage << 8) / divisor;
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

/* Shields regenerate; casters regain energy, except while a cloak they
 * switched on drains it. Runs once per engine tic for each 24 Hz frame. */
static void sc_mobj_ticker(mobj_t *mo) {
    if (!sc_type(mo)) return;
    sc_start(mo);
    int frames = leveltime * 24 / RTS_TICRATE - (leveltime - 1) * 24 / RTS_TICRATE;
    if (frames <= 0) return;
    const sc_unit_t *u = &sc_units[mo->type_id - 1];
    if (mo->sc.shields < u->shields << 8) {
        mo->sc.shields += SC_SHIELD_REGEN * frames;
        if (mo->sc.shields > u->shields << 8) mo->sc.shields = u->shields << 8;
    }
    if (!(u->flags & SC_UNIT_SPELLCASTER)) return;
    if ((mo->traits & MF_CLOAKED) && !(mo->info->traits & MF_CLOAKED)) {
        mo->sc.energy -= SC_CLOAK_DRAIN * frames;
        if (mo->sc.energy <= 0) { mo->sc.energy = 0; mo->traits &= ~MF_CLOAKED; }
    } else {
        mo->sc.energy += SC_ENERGY_REGEN * frames;
        if (mo->sc.energy > SC_MAX_ENERGY) mo->sc.energy = SC_MAX_ENERGY;
    }
    if (mo->type_id == MT_SHIELD_BATTERY) recharge(mo, frames);
}

bool sc_cast(mobj_t *caster, int tech, mobj_t *target, fvec2_t at) {
    (void)target; (void)at;
    if (!sc_type(caster) || caster->hp <= 0) return false;
    const sc_unit_t *u = &sc_units[caster->type_id - 1];
    if (tech != SC_TECH_CLOAKING_FIELD && tech != SC_TECH_PERSONNEL_CLOAKING) return false;
    /* Wraiths use Cloaking Field, Ghosts Personnel Cloaking; casting again decloaks. */
    if (!(u->flags & SC_UNIT_CLOAKABLE) || (caster->info->traits & MF_CLOAKED) ||
        tech != ((caster->traits & MF_FLY) ? SC_TECH_CLOAKING_FIELD : SC_TECH_PERSONNEL_CLOAKING)) return false;
    if (caster->traits & MF_CLOAKED) { caster->traits &= ~MF_CLOAKED; return true; }
    sc_start(caster);
    if (caster->sc.energy < sc_techs[tech].energy << 8) return false;
    caster->sc.energy -= sc_techs[tech].energy << 8;
    caster->traits |= MF_CLOAKED;
    return true;
}

void sc_init_combat(void) {
    game_info.hit_damage = hit_damage;
    game_info.mobj_ticker = sc_mobj_ticker;
}
