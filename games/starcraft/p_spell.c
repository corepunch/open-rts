#include "sc_local.h"
#include <math.h>
/* StarCraft abilities: techdata.dat costs, weapons.dat ranges and areas,
 * OpenBW's timers. A caster walks into range, pays its energy and casts;
 * lasting effects are timers on the units they touch, and an area that
 * lingers (Psionic Storm, Dark Swarm, Scanner Sweep) is a mobj that ticks
 * like any other. Timers count 24 Hz frames. */
enum {
    SC_BEAT = 8,              /* Irradiate and Plague hurt every 8 frames. */
    SC_IRRADIATE_BEATS = 37, SC_IRRADIATE_DAMAGE = 250,
    SC_PLAGUE_BEATS = 75, SC_PLAGUE_DAMAGE = 300,
    SC_MATRIX_HP = 250 << 8, SC_MATRIX_FRAMES = 168 * 8,
    SC_LOCKDOWN_FRAMES = 131 * 8, SC_ENSNARE_FRAMES = 75 * 8,
    SC_HALLUCINATION_FRAMES = 1350, SC_BROODLING_FRAMES = 1800,
    SC_SWARM_FRAMES = 900, SC_SWEEP_FRAMES = 262,
    /* Seven strikes of 16, nine frames apart: 112 over 2.6 seconds. */
    SC_STORM_STRIKE = 9, SC_STORM_FRAMES = 7 * SC_STORM_STRIKE,
    SC_NUKE_FRAMES = 14 * 24, /* A Ghost paints the target for 14 seconds. */
    SC_CONSUME_ENERGY = 50 << 8,
    SC_WEAPON_YAMATO = 30, SC_WEAPON_NUKE = 31, SC_WEAPON_STORM = 84,
};
/* Ensnare and Plague cover this many cells around the spot (weapons.dat
 * leaves their radius 0); EMP's 64 pixels are the same. Irradiate burns
 * organic units within a cell of its host; Dark Swarm's cloud is 5 by 5. */
#define SC_SPELL_RADIUS 2.0f
#define SC_IRRADIATE_RADIUS 1.0f
#define SC_SWARM_RADIUS 2.5f
#define SC_NUKE_RADIUS 8.0f

/* Who casts what and how far. A weapons.dat row gives the reach; range is
 * used without one, negative for anywhere, zero for the caster itself. */
typedef struct {
    int tech, weapon;
    float range;
    mobjtype_id_t casters[2];
    bool given; /* Retail melee grants it without research. */
} sc_spell_t;
static const sc_spell_t spells[] = {
    {SC_TECH_LOCKDOWN, 32, 0, {MT_GHOST}, false},
    {SC_TECH_EMP, 33, 0, {MT_SCIENCE_VESSEL}, false},
    {SC_TECH_SCANNER_SWEEP, -1, -1, {MT_COMSAT_STATION}, true},
    {SC_TECH_SIEGE_MODE, -1, 0, {MT_SIEGE_TANK, MT_SIEGE_MODE}, false},
    {SC_TECH_DEFENSIVE_MATRIX, -1, 10, {MT_SCIENCE_VESSEL}, true},
    {SC_TECH_IRRADIATE, 34, 0, {MT_SCIENCE_VESSEL}, false},
    {SC_TECH_YAMATO_GUN, SC_WEAPON_YAMATO, 0, {MT_BATTLECRUISER}, false},
    {SC_TECH_CLOAKING_FIELD, -1, 0, {MT_WRAITH}, false},
    {SC_TECH_PERSONNEL_CLOAKING, -1, 0, {MT_GHOST}, false},
    {SC_TECH_SPAWN_BROODLING, 57, 0, {MT_QUEEN}, false},
    {SC_TECH_DARK_SWARM, 59, 0, {MT_DEFILER}, true},
    {SC_TECH_PLAGUE, 60, 0, {MT_DEFILER}, false},
    {SC_TECH_CONSUME, -1, 1.5f, {MT_DEFILER}, false},
    {SC_TECH_ENSNARE, 58, 0, {MT_QUEEN}, false},
    {SC_TECH_PARASITE, 56, 0, {MT_QUEEN}, true},
    {SC_TECH_PSIONIC_STORM, SC_WEAPON_STORM, 0, {MT_HIGH_TEMPLAR}, false},
    {SC_TECH_HALLUCINATION, -1, 8, {MT_HIGH_TEMPLAR}, false},
    {SC_TECH_ARCHON_WARP, -1, 0, {MT_HIGH_TEMPLAR}, true},
    {SC_TECH_NUCLEAR_STRIKE, -1, 8, {MT_GHOST}, true},
};
enum { SC_SPELLS = sizeof(spells) / sizeof(*spells) };

static const sc_spell_t *spell(int tech) {
    for (int i = 0; i < SC_SPELLS; i++) if (spells[i].tech == tech) return &spells[i];
    return NULL;
}
static bool cloak(int tech) { return tech == SC_TECH_CLOAKING_FIELD || tech == SC_TECH_PERSONNEL_CLOAKING; }
/* Cloaking follows units.dat: any cloakable flyer uses Cloaking Field, any
 * cloakable walker Personnel Cloaking, heroes included. */
static bool casts(uint16_t type, const sc_spell_t *s) {
    if (type < 1 || type > SC_TYPES) return false;
    uint32_t flags = sc_units[type - 1].flags;
    if (cloak(s->tech))
        return (flags & SC_UNIT_CLOAKABLE) && !(flags & SC_UNIT_PERMANENT_CLOAK) &&
            s->tech == ((flags & 4) ? SC_TECH_CLOAKING_FIELD : SC_TECH_PERSONNEL_CLOAKING);
    return type == s->casters[0] || type == s->casters[1];
}

bool sc_tech_researched(int tech) { const sc_spell_t *s = spell(tech); return !s || !s->given; }
bool sc_has_tech(int owner, int tech) {
    if (!sc_tech_researched(tech)) return true;
    return tech >= 0 && tech < SC_TECHS && owner >= 0 && owner < 8 && level.upgrades[SC_UPGRADES + tech][owner].weapon;
}
static bool may_cast(const mobj_t *caster, int tech) {
    return sc_has_tech(caster->owner, tech) || (sc_units[caster->type_id - 1].flags & 0x40); /* heroes know all */
}
int sc_unit_techs(uint16_t type, int *out, int cap) {
    int n = 0;
    for (int i = 0; i < SC_SPELLS && n < cap; i++) if (casts(type, &spells[i])) out[n++] = spells[i].tech;
    return n;
}
bool sc_tech_aimed(int tech) {
    const sc_spell_t *s = spell(tech);
    return s && !cloak(tech) && tech != SC_TECH_SIEGE_MODE && tech != SC_TECH_ARCHON_WARP;
}
static float reach(const sc_spell_t *s) {
    const sc_weapon_t *w = sc_weapon(s->weapon);
    return w ? (float)((w->max_range + 31) / 32) : s->range;
}
static int energy_cost(int tech) { return tech < SC_TECHS ? sc_techs[tech].energy << 8 : 0; }

/* A unit spells may touch: alive, on the map, not inside a Bunker or an area. */
static bool body(const mobj_t *mo) {
    return mo->thinker.function == P_MobjThinker && !mo->remove && mo->hp > 0 &&
        !(mo->traits & (MF_NOBLOCKMAP | MF_MISSILE)) && sc_unit(mo);
}
static bool building(const mobj_t *mo) { return (sc_units[mo->type_id - 1].flags & SC_UNIT_BUILDING) != 0; }
static fvec2_t where(const mobj_t *mo) { return fixed3_xy_to_fvec2(mo->core.position); }
static bool near(const mobj_t *mo, fvec2_t at, float radius) {
    return fvec2_distance_squared(where(mo), at) <= radius * radius;
}
static ivec2_t pixel(fvec2_t at) { return (ivec2_t){(int)lroundf(at.x * 32), (int)lroundf(at.y * 32)}; }
static fvec2_t from_pixel(ivec2_t p) { return (fvec2_t){p.x / 32.0f, p.y / 32.0f}; }

/* Whether the spell may land on target. */
static bool target_ok(const mobj_t *caster, int tech, const mobj_t *target) {
    if (!target || !body(target) || target == caster || !P_VisibleTo(caster, target)) return false;
    uint32_t flags = sc_units[target->type_id - 1].flags;
    switch (tech) {
    case SC_TECH_YAMATO_GUN: return true;
    case SC_TECH_LOCKDOWN: return !building(target) && (flags & SC_UNIT_MECHANICAL);
    case SC_TECH_SPAWN_BROODLING:
        return !building(target) && !(target->traits & MF_FLY) && !(flags & SC_UNIT_ROBOTIC) &&
            target->type_id != MT_LARVA && target->type_id != MT_EGG;
    case SC_TECH_CONSUME:
        return target->owner == caster->owner && !building(target) && (sc_units[target->type_id - 1].race & 1) &&
            target->type_id != MT_LARVA && target->type_id != MT_EGG;
    case SC_TECH_ARCHON_WARP:
        return target->owner == caster->owner && target->type_id == MT_HIGH_TEMPLAR;
    default: return !building(target);
    }
}
static bool needs_target(int tech) {
    return tech == SC_TECH_LOCKDOWN || tech == SC_TECH_DEFENSIVE_MATRIX || tech == SC_TECH_IRRADIATE ||
        tech == SC_TECH_YAMATO_GUN || tech == SC_TECH_SPAWN_BROODLING || tech == SC_TECH_CONSUME ||
        tech == SC_TECH_PARASITE || tech == SC_TECH_HALLUCINATION;
}

/* An area that lingers. The neutral owner keeps it out of triggers and
 * scores; team decides whose sight it gives (none past 8). */
static mobj_t *spawn_area(uint16_t type, fvec2_t at, const mobj_t *caster, int tech, int frames, uint8_t team) {
    mobj_t *area = sc_spawn_actor(type - 1u, pixel(at), 11);
    if (!area) return NULL;
    area->team = team;
    area->allegiance = ALLEGIANCE_NEUTRAL;
    area->traits = MF_NOBLOCKMAP | (type == MT_DARK_SWARM ? MF_RENDERABLE : MF_DONTDRAW);
    area->sc.order.tech = tech;
    area->sc.attacker = caster->owner;
    area->sc.parent = caster->id;
    area->sc.timers[SC_TIMER_LIFE] = frames;
    return area;
}
/* A copy of type for caster's side at a spot beside at. */
static mobj_t *spawn_for(const mobj_t *caster, uint16_t type, fvec2_t at) {
    mobj_t *mo = sc_spawn_actor(type - 1u, pixel(at), caster->owner);
    if (!mo) return NULL;
    mo->team = caster->team;
    mo->allegiance = caster->allegiance;
    return mo;
}

static void storm_strike(mobj_t *storm) {
    fvec2_t at = where(storm);
    const sc_weapon_t *w = sc_weapon(SC_WEAPON_STORM);
    mobj_t *source = P_MobjById(storm->sc.parent);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *v = (mobj_t *)th;
        /* A unit already hurt by a storm this strike is spared: storms do not stack. */
        if (!body(v) || building(v) || v->sc.timers[SC_TIMER_STORM] > 0 || !near(v, at, w->splash[0] / 32.0f)) continue;
        v->sc.timers[SC_TIMER_STORM] = SC_STORM_STRIKE;
        int damage = sc_hit(storm->sc.attacker, w, v, w->damage, 1);
        if (damage > 0) P_DamageMobj(v, source, damage);
    }
}

static void nuke(mobj_t *ghost, fvec2_t at) {
    const sc_weapon_t *w = sc_weapon(SC_WEAPON_NUKE);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *v = (mobj_t *)th;
        if (!body(v)) continue;
        float d = sqrtf(fvec2_distance_squared(where(v), at)) - P_MobjRadius(v);
        int divisor = d <= w->splash[0] / 32.0f ? 1 : d <= w->splash[1] / 32.0f ? 2 : d <= w->splash[2] / 32.0f ? 4 : 0;
        if (!divisor) continue;
        int damage = sc_hit(ghost->owner, w, v, w->damage, divisor);
        if (damage > 0) P_DamageMobj(v, ghost, damage);
    }
}

/* Every unit in radius around at but the caster. */
#define AROUND(v, at, radius, caster) \
    for (thinker_t *th_ = thinkercap.next; th_ != &thinkercap; th_ = th_->next) \
        for (mobj_t *v = (mobj_t *)th_; v; v = NULL) \
            if (v != (caster) && body(v) && near(v, at, radius))

/* The effect itself, in range, energy paid. */
static void cast_now(mobj_t *caster, int tech, mobj_t *target, fvec2_t at) {
    caster->sc.energy -= energy_cost(tech);
    switch (tech) {
    case SC_TECH_LOCKDOWN: target->sc.timers[SC_TIMER_LOCKDOWN] = SC_LOCKDOWN_FRAMES; break;
    case SC_TECH_DEFENSIVE_MATRIX:
        target->sc.matrix = SC_MATRIX_HP;
        target->sc.timers[SC_TIMER_MATRIX] = SC_MATRIX_FRAMES;
        break;
    case SC_TECH_IRRADIATE: target->sc.timers[SC_TIMER_IRRADIATE] = SC_IRRADIATE_BEATS; break;
    case SC_TECH_PARASITE: target->sc.parasite |= UINT32_C(0x40000000) >> (caster->team & 7); break;
    case SC_TECH_YAMATO_GUN: {
        const sc_weapon_t *w = sc_weapon(SC_WEAPON_YAMATO);
        int damage = sc_hit(caster->owner, w, target, w->damage, 1);
        if (damage > 0) P_DamageMobj(target, caster, damage);
        break;
    }
    case SC_TECH_SPAWN_BROODLING:
        /* Shields do not save it: the broodlings hatch from the body. */
        at = where(target);
        P_DamageMobj(target, caster, target->hp);
        for (int i = 0; i < 2; i++) {
            mobj_t *brood = spawn_for(caster, MT_BROODLING, fvec2_add(at, (fvec2_t){i ? 0.4f : -0.4f, 0}));
            if (brood) brood->sc.timers[SC_TIMER_LIFE] = SC_BROODLING_FRAMES;
        }
        break;
    case SC_TECH_CONSUME:
        P_DamageMobj(target, NULL, target->hp);
        caster->sc.energy += SC_CONSUME_ENERGY;
        if (caster->sc.energy > sc_max_energy(caster)) caster->sc.energy = sc_max_energy(caster);
        break;
    case SC_TECH_HALLUCINATION:
        /* Two copies that deal nothing and take double damage. */
        for (int i = 0; i < 2; i++) {
            mobj_t *copy = spawn_for(caster, target->type_id, fvec2_add(where(target), (fvec2_t){i ? 0.75f : -0.75f, 0.5f}));
            if (!copy) continue;
            copy->hp = target->hp;
            copy->sc.flags |= SC_HALLUCINATION;
            copy->sc.timers[SC_TIMER_LIFE] = SC_HALLUCINATION_FRAMES;
        }
        break;
    case SC_TECH_EMP:
        AROUND(v, at, sc_weapon(33)->splash[0] / 32.0f, caster) { sc_start(v); v->sc.shields = 0; v->sc.energy = 0; }
        break;
    case SC_TECH_ENSNARE:
        AROUND(v, at, SC_SPELL_RADIUS, caster) {
            if (building(v)) continue;
            v->sc.timers[SC_TIMER_ENSNARE] = SC_ENSNARE_FRAMES;
            v->speed = v->info->speed * 0.5f;
        }
        break;
    case SC_TECH_PLAGUE:
        AROUND(v, at, SC_SPELL_RADIUS, caster) v->sc.timers[SC_TIMER_PLAGUE] = SC_PLAGUE_BEATS;
        break;
    case SC_TECH_PSIONIC_STORM: {
        mobj_t *storm = spawn_area(MT_MAP_REVEALER, at, caster, tech, SC_STORM_FRAMES, 255);
        if (storm) storm_strike(storm);
        break;
    }
    case SC_TECH_DARK_SWARM: spawn_area(MT_DARK_SWARM, at, caster, tech, SC_SWARM_FRAMES, 255); break;
    case SC_TECH_SCANNER_SWEEP: {
        mobj_t *sweep = spawn_area(MT_MAP_REVEALER, at, caster, tech, SC_SWEEP_FRAMES, caster->team);
        if (sweep) sweep->traits |= MF_DETECTOR;
        break;
    }
    case SC_TECH_NUCLEAR_STRIKE: {
        mobj_t *silo = sc_armed_silo(caster->owner);
        if (!silo) break;
        silo->sc.hangar--;
        caster->sc.order = (sc_order_t){.kind = SC_ORDER_NUKE, .tech = tech, .at = pixel(at), .time = SC_NUKE_FRAMES};
        return;
    }
    }
    caster->sc.order = (sc_order_t){0};
}

/* Walks into range, then casts; false when the cast cannot go on. */
static bool step_cast(mobj_t *caster) {
    sc_order_t *o = &caster->sc.order;
    mobj_t *target = o->target ? P_MobjById(o->target) : NULL;
    if (o->target && !target_ok(caster, o->tech, target)) return false;
    if (caster->sc.energy < energy_cost(o->tech) ||
        (o->tech == SC_TECH_NUCLEAR_STRIKE && !sc_armed_silo(caster->owner))) return false;
    fvec2_t at = target ? where(target) : from_pixel(o->at);
    float range = reach(spell(o->tech));
    if (range < 0 || near(caster, at, range + 0.5f + (target ? P_MobjRadius(target) : 0))) {
        P_ClearMove(caster);
        caster->move_only = false;
        cast_now(caster, o->tech, target, at);
        return true;
    }
    if (!(caster->traits & MF_MOBILE)) return false;
    if (!P_HasMoveOrder(caster) && !P_MoveUnitTo(&level, caster, at)) return false;
    caster->move_only = true; /* No stopping to shoot on the way. */
    return true;
}

bool sc_cast(mobj_t *caster, int tech, mobj_t *target, fvec2_t at) {
    const sc_spell_t *s = spell(tech);
    if (!caster || caster->remove || caster->hp <= 0 || !s || !casts(caster->type_id, s) ||
        (caster->sc.flags & SC_HALLUCINATION) || !may_cast(caster, tech)) return false;
    sc_start(caster);
    if (cloak(tech)) {
        if (caster->traits & MF_CLOAKED) { caster->traits &= ~MF_CLOAKED; return true; }
        if (caster->sc.energy < energy_cost(tech)) return false;
        caster->sc.energy -= energy_cost(tech);
        caster->traits |= MF_CLOAKED;
        return true;
    }
    if (tech == SC_TECH_SIEGE_MODE) return P_Deploy(caster);
    if (tech == SC_TECH_ARCHON_WARP) return sc_merge(caster, target);
    if (caster->sc.energy < energy_cost(tech) || (needs_target(tech) && !target_ok(caster, tech, target)) ||
        (tech == SC_TECH_NUCLEAR_STRIKE && !sc_armed_silo(caster->owner))) return false;
    if (!needs_target(tech)) target = NULL;
    caster->sc.order = (sc_order_t){.kind = SC_ORDER_CAST, .tech = tech, .target = target ? target->id : 0,
                                    .at = pixel(target ? where(target) : at)};
    caster->attack.target = NULL;
    if (step_cast(caster)) return true;
    caster->sc.order = (sc_order_t){0};
    return false;
}

void sc_interrupt(mobj_t *unit) {
    if (!unit || !sc_unit(unit)) return;
    /* A nuke being painted is lost when the Ghost turns away. */
    if (unit->sc.order.kind == SC_ORDER_MERGE) {
        mobj_t *partner = P_MobjById(unit->sc.order.target);
        if (partner && partner->sc.order.kind == SC_ORDER_MERGE) partner->sc.order = (sc_order_t){0};
    }
    unit->sc.order = (sc_order_t){0};
}

/* Stands still and holds fire this tic. */
static void hold(mobj_t *mo) {
    P_ClearMove(mo);
    mo->movement.goal = where(mo);
    mo->movement.order_id = 0;
    mo->movement.order_arrived = true;
    mo->waypoints = (waypoints_t){0};
    mo->attack.target = NULL;
    if (mo->attack.cooldown_left_ms < 2000 / RTS_TICRATE) mo->attack.cooldown_left_ms = 2000 / RTS_TICRATE;
    mo->core.momentum = fixed3_zero();
}

static void irradiate(mobj_t *host) {
    int k = SC_IRRADIATE_BEATS - host->sc.timers[SC_TIMER_IRRADIATE]--;
    int damage = SC_IRRADIATE_DAMAGE * (k + 1) / SC_IRRADIATE_BEATS - SC_IRRADIATE_DAMAGE * k / SC_IRRADIATE_BEATS;
    fvec2_t at = where(host);
    AROUND(v, at, SC_IRRADIATE_RADIUS, NULL)
        if (!building(v) && (sc_units[v->type_id - 1].flags & SC_UNIT_ORGANIC)) P_DamageMobj(v, NULL, damage);
}

int sc_spell_hit(const mobj_t *attacker, const weapondef_t *weapon, mobj_t *target, int dealt) {
    if (attacker->sc.flags & SC_HALLUCINATION) return 0;
    if (target->sc.flags & SC_HALLUCINATION) dealt *= 2;
    /* Under Dark Swarm only blows from up close land. */
    if (!(target->traits & MF_FLY) && weapon->range > 1.0f)
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            const mobj_t *cloud = (const mobj_t *)th;
            if (th->function == P_MobjThinker && !cloud->remove && cloud->type_id == MT_DARK_SWARM &&
                cloud->sc.order.tech == SC_TECH_DARK_SWARM && cloud->sc.timers[SC_TIMER_LIFE] > 0 &&
                fabsf(fixed_to_float(cloud->core.position.x - target->core.position.x)) <= SC_SWARM_RADIUS &&
                fabsf(fixed_to_float(cloud->core.position.y - target->core.position.y)) <= SC_SWARM_RADIUS) return 0;
        }
    if (target->sc.matrix > 0) {
        int absorbed = dealt < target->sc.matrix ? dealt : target->sc.matrix;
        target->sc.matrix -= absorbed;
        dealt -= absorbed;
    }
    return dealt;
}

uint32_t sc_sight_teams(const mobj_t *mo) { return mo->sc.parasite; }

void sc_spell_ticker(mobj_t *mo, int frames) {
    int *t = mo->sc.timers;
    if (t[SC_TIMER_LOCKDOWN] > 0 || mo->sc.order.kind == SC_ORDER_NUKE) hold(mo);
    if (frames > 0) {
        static const int countdown[] = {SC_TIMER_MATRIX, SC_TIMER_ENSNARE, SC_TIMER_LOCKDOWN, SC_TIMER_STORM};
        for (unsigned i = 0; i < sizeof(countdown) / sizeof(*countdown); i++)
            if (t[countdown[i]] > 0 && (t[countdown[i]] -= frames) <= 0) {
                t[countdown[i]] = 0;
                if (countdown[i] == SC_TIMER_MATRIX) mo->sc.matrix = 0;
                if (countdown[i] == SC_TIMER_ENSNARE) mo->speed = mo->info->speed;
            }
        if ((leveltime * 24 / RTS_TICRATE) % SC_BEAT == 0) {
            if (t[SC_TIMER_IRRADIATE] > 0) irradiate(mo);
            if (t[SC_TIMER_PLAGUE] > 0) {
                t[SC_TIMER_PLAGUE]--;
                int damage = SC_PLAGUE_DAMAGE / SC_PLAGUE_BEATS;
                if (damage > mo->hp - 1) damage = mo->hp - 1; /* Plague never kills. */
                if (damage > 0) P_DamageMobj(mo, NULL, damage);
            }
            if (mo->remove || mo->hp <= 0) return;
        }
        if (t[SC_TIMER_LIFE] > 0 && (t[SC_TIMER_LIFE] -= frames) <= 0) {
            t[SC_TIMER_LIFE] = 0;
            /* Areas and hallucinations vanish; broodlings die. */
            if ((mo->traits & MF_NOBLOCKMAP) || (mo->sc.flags & SC_HALLUCINATION)) P_RemoveMobj(mo);
            else P_DamageMobj(mo, NULL, mo->hp);
            return;
        }
        if (mo->sc.order.tech == SC_TECH_PSIONIC_STORM && (mo->traits & MF_NOBLOCKMAP) &&
            t[SC_TIMER_LIFE] % SC_STORM_STRIKE == 0) storm_strike(mo);
    }
    sc_order_t *o = &mo->sc.order;
    if (o->kind == SC_ORDER_CAST && !step_cast(mo)) { *o = (sc_order_t){0}; mo->move_only = false; }
    else if (o->kind == SC_ORDER_NUKE && (o->time -= frames) <= 0) {
        fvec2_t at = from_pixel(o->at);
        *o = (sc_order_t){0};
        nuke(mo, at);
    }
}
