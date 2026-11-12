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
    /* OpenBW status timers tick every 8 frames: Stim 37, Stasis 131. */
    SC_STIM_FRAMES = 37 * 8, SC_STIM_HP = 10, SC_STASIS_FRAMES = 131 * 8,
    SC_RECALL_DELAY = 22, /* The field opens before anyone is taken. */
    SC_WEAPON_STASIS = 83,
    SC_CONSUME_ENERGY = 50 << 8,
    SC_WEAPON_YAMATO = 30, SC_WEAPON_NUKE = 31, SC_WEAPON_EMP = 33, SC_WEAPON_STORM = 84,
};
/* Ensnare and Plague cover this many cells around the spot (weapons.dat
 * leaves their radius 0); EMP's 64 pixels are the same. Irradiate burns
 * organic units within a cell of its host; Dark Swarm's cloud is 5 by 5. */
#define SC_SPELL_RADIUS FIXED_FROM_INT(2)
#define SC_IRRADIATE_RADIUS FIXED_ONE
#define SC_SWARM_RADIUS (FIXED_ONE * 5 / 2)
/* Recall takes everyone in a 128-pixel square; Stasis Field a 96-pixel one. */
#define SC_RECALL_HALF SC_PIXELS(64)
#define SC_STASIS_HALF SC_PIXELS(48)

/* Who casts what and how far. A weapons.dat row gives the reach; range is
 * used without one, negative for anywhere, zero for the caster itself. */
typedef struct {
    int tech, weapon;
    fixed_t range;
    mobjtype_id_t casters[4];
    bool given; /* Retail melee grants it without research. */
} sc_spell_t;
enum { HERO_GUI_MONTAG = 11, HERO_RAYNOR_VULTURE = 20, HERO_RAYNOR_MARINE = 21, HERO_DANIMOTH = 87 };
static const sc_spell_t spells[] = {
    {SC_TECH_STIM_PACKS, -1, 0, {MT_MARINE, MT_FIREBAT, HERO_RAYNOR_MARINE, HERO_GUI_MONTAG}, false},
    {SC_TECH_SPIDER_MINES, -1, SC_PIXELS(4), {MT_VULTURE, HERO_RAYNOR_VULTURE}, false},
    {SC_TECH_LOCKDOWN, 32, 0, {MT_GHOST}, false},
    {SC_TECH_EMP, SC_WEAPON_EMP, 0, {MT_SCIENCE_VESSEL}, false},
    {SC_TECH_SCANNER_SWEEP, -1, -FIXED_ONE, {MT_COMSAT_STATION}, true},
    {SC_TECH_SIEGE_MODE, -1, 0, {MT_SIEGE_TANK, MT_SIEGE_MODE}, false},
    {SC_TECH_DEFENSIVE_MATRIX, -1, FIXED_FROM_INT(10), {MT_SCIENCE_VESSEL}, true},
    {SC_TECH_IRRADIATE, 34, 0, {MT_SCIENCE_VESSEL}, false},
    {SC_TECH_YAMATO_GUN, SC_WEAPON_YAMATO, 0, {MT_BATTLECRUISER}, false},
    {SC_TECH_CLOAKING_FIELD, -1, 0, {MT_WRAITH}, false},
    {SC_TECH_PERSONNEL_CLOAKING, -1, 0, {MT_GHOST}, false},
    {SC_TECH_BURROWING, -1, 0, {MT_NONE}, false}, /* units.dat's burrowable units */
    {SC_TECH_INFESTATION, -1, 0, {MT_QUEEN}, true},
    {SC_TECH_SPAWN_BROODLING, 57, 0, {MT_QUEEN}, false},
    {SC_TECH_DARK_SWARM, 59, 0, {MT_DEFILER}, true},
    {SC_TECH_PLAGUE, 60, 0, {MT_DEFILER}, false},
    {SC_TECH_CONSUME, -1, FIXED_ONE * 3 / 2, {MT_DEFILER}, false},
    {SC_TECH_ENSNARE, 58, 0, {MT_QUEEN}, false},
    {SC_TECH_PARASITE, 56, 0, {MT_QUEEN}, true},
    {SC_TECH_PSIONIC_STORM, SC_WEAPON_STORM, 0, {MT_HIGH_TEMPLAR}, false},
    {SC_TECH_HALLUCINATION, -1, FIXED_FROM_INT(8), {MT_HIGH_TEMPLAR}, false},
    {SC_TECH_RECALL, -1, -FIXED_ONE, {MT_ARBITER, HERO_DANIMOTH}, false},
    {SC_TECH_STASIS_FIELD, SC_WEAPON_STASIS, 0, {MT_ARBITER, HERO_DANIMOTH}, false},
    {SC_TECH_ARCHON_WARP, -1, 0, {MT_HIGH_TEMPLAR}, true},
    {SC_TECH_NUCLEAR_STRIKE, -1, FIXED_FROM_INT(8), {MT_GHOST}, true},
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
    if (s->tech == SC_TECH_BURROWING) return (flags & SC_UNIT_BURROWABLE) != 0;
    for (int i = 0; i < 4; i++) if (s->casters[i] && type == s->casters[i]) return true;
    return false;
}

bool sc_tech_researched(int tech) { const sc_spell_t *s = spell(tech); return !s || !s->given; }
bool sc_has_tech(int owner, int tech) {
    if (!sc_tech_researched(tech)) return true;
    return tech >= 0 && tech < SC_TECHS && owner >= 0 && owner < 8 && level.upgrades[SC_UPGRADES + tech][owner].weapon;
}
static bool may_cast(const mobj_t *caster, int tech) {
    return sc_has_tech(caster->owner, tech) || (sc_units[caster->type_id - 1].flags & SC_UNIT_HERO); /* heroes know all */
}
int sc_unit_techs(uint16_t type, int *out, int cap) {
    int n = 0;
    for (int i = 0; i < SC_SPELLS && n < cap; i++) if (casts(type, &spells[i])) out[n++] = spells[i].tech;
    return n;
}
/* Cast at once on the caster: cloaks, siege, Stim Packs and Burrow. */
static bool self_cast(int tech) {
    return cloak(tech) || tech == SC_TECH_SIEGE_MODE || tech == SC_TECH_STIM_PACKS || tech == SC_TECH_BURROWING;
}
bool sc_tech_aimed(int tech) {
    if (tech == SC_TECH_NYDUS_EXIT) return true;
    const sc_spell_t *s = spell(tech);
    return s && !self_cast(tech) && tech != SC_TECH_ARCHON_WARP;
}
bool sc_disabled(const mobj_t *mo) {
    return mo->sc.timers[SC_TIMER_LOCKDOWN] > 0 || mo->sc.timers[SC_TIMER_STASIS] > 0;
}
static fixed_t reach(const sc_spell_t *s) {
    const sc_weapon_t *w = sc_weapon(s->weapon);
    return w ? FIXED_FROM_INT((w->max_range + 31) / 32) : s->range;
}
static int energy_cost(int tech) { return tech < SC_TECHS ? sc_techs[tech].energy << 8 : 0; }

/* A unit spells may touch: alive, on the map, not inside a Bunker or an
 * area, and not one a map made invincible. */
static bool body(const mobj_t *mo) {
    return mo->thinker.function == P_MobjThinker && !mo->remove && mo->hp > 0 &&
        !(mo->traits & (MF_NOBLOCKMAP | MF_MISSILE)) && !(mo->sc.flags & SC_INVINCIBLE) &&
        !mo->sc.timers[SC_TIMER_STASIS] && sc_unit(mo);
}
static bool building(const mobj_t *mo) { return (sc_units[mo->type_id - 1].flags & SC_UNIT_BUILDING) != 0; }
static fixed2_t where(const mobj_t *mo) { return fixed3_xy(mo->core.position); }
static bool near(const mobj_t *mo, fixed2_t at, fixed_t radius) {
    return fixed2_distance_squared64(where(mo), at) <= fixed_sq64(radius);
}
static ivec2_t pixel(fixed2_t at) { return (ivec2_t){(at.x + FIXED_ONE / 64) >> (FIXED_FRAC_BITS - 5), (at.y + FIXED_ONE / 64) >> (FIXED_FRAC_BITS - 5)}; }
static fixed2_t from_pixel(ivec2_t p) { return (fixed2_t){SC_PIXELS(p.x), SC_PIXELS(p.y)}; }

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
    /* A finished Command Center below half its hit points. */
    case SC_TECH_INFESTATION:
        return target->type_id == MT_COMMAND_CENTER && !(target->traits & MF_FLY) &&
            target->hp * 2 < target->max_hp && !P_IsAlly(caster, target);
    default: return !building(target);
    }
}
static bool needs_target(int tech) {
    return tech == SC_TECH_LOCKDOWN || tech == SC_TECH_DEFENSIVE_MATRIX || tech == SC_TECH_IRRADIATE ||
        tech == SC_TECH_YAMATO_GUN || tech == SC_TECH_SPAWN_BROODLING || tech == SC_TECH_CONSUME ||
        tech == SC_TECH_PARASITE || tech == SC_TECH_HALLUCINATION || tech == SC_TECH_INFESTATION;
}

/* An area that lingers. The neutral owner keeps it out of triggers and
 * scores; team decides whose sight it gives (none past 8). */
static mobj_t *spawn_area(uint16_t type, fixed2_t at, const mobj_t *caster, int tech, int frames, uint8_t team) {
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
static mobj_t *spawn_for(const mobj_t *caster, uint16_t type, fixed2_t at) {
    mobj_t *mo = sc_spawn_actor(type - 1u, pixel(at), caster->owner);
    if (!mo) return NULL;
    mo->team = caster->team;
    mo->allegiance = caster->allegiance;
    return mo;
}

static void storm_strike(mobj_t *storm) {
    fixed2_t at = where(storm);
    const sc_weapon_t *w = sc_weapon(SC_WEAPON_STORM);
    mobj_t *source = P_MobjById(storm->sc.parent);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *v = (mobj_t *)th;
        /* A unit already hurt by a storm this strike is spared: storms do not stack. */
        if (!body(v) || building(v) || v->sc.timers[SC_TIMER_STORM] > 0 || !near(v, at, SC_PIXELS(w->splash[0]))) continue;
        v->sc.timers[SC_TIMER_STORM] = SC_STORM_STRIKE;
        int damage = sc_hit(storm->sc.attacker, w, v, w->damage, 1);
        if (damage > 0) P_DamageMobj(v, source, damage);
    }
}

static void nuke(mobj_t *ghost, fixed2_t at) {
    const sc_weapon_t *w = sc_weapon(SC_WEAPON_NUKE);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *v = (mobj_t *)th;
        if (!body(v)) continue;
        fixed_t d = fixed2_length(fixed2_sub(where(v), at)) - P_MobjRadius(v);
        int divisor = d <= SC_PIXELS(w->splash[0]) ? 1 : d <= SC_PIXELS(w->splash[1]) ? 2 : d <= SC_PIXELS(w->splash[2]) ? 4 : 0;
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

/* ── burrowing ──────────────────────────────────────────────────────── */

/* iscript Burrow and UnBurrow, where a unit has them (w_assets.c). */
int sc_burrow_states[SC_TYPES][2];
/* An animation that has reached the frame it holds is done. */
static bool settled(const mobj_t *mo) {
    int id = mo->core.state_id;
    return !gameinfo || id <= 0 || id >= gameinfo->state_count || gameinfo->states[id].nextstate == id;
}
static void stand(mobj_t *mo) {
    hold(mo);
    mo->harvest.target = -1;
    mo->harvest.base = NULL;
    mo->harvest.phase = mo->harvest.timer_ms = 0;
}
/* Down: it stops, plays Burrow and, at its last frame, is dug in: hidden
 * from players without a detector, unable to move, fight or gather. */
void sc_burrow(mobj_t *mo) {
    sc_interrupt(mo);
    mo->sc.order = (sc_order_t){0};
    mo->waypoints = (waypoints_t){0};
    stand(mo);
    mo->traits &= ~(MF_MOBILE | MF_ATTACK);
    mo->sc.order = (sc_order_t){.kind = SC_ORDER_BURROW};
    int state = sc_burrow_states[mo->type_id - 1][0];
    if (state) P_SetMobjState(mo, state);
}
static void dug_in(mobj_t *mo) {
    mo->sc.flags |= SC_BURROWED;
    mo->traits |= MF_CLOAKED;
    mo->sc.order = (sc_order_t){0};
}
/* Up: UnBurrow, then it stands and may move and fight again. */
void sc_unburrow(mobj_t *mo) {
    if (!(mo->sc.flags & SC_BURROWED)) return;
    mo->sc.flags &= ~SC_BURROWED;
    mo->traits &= ~MF_CLOAKED;
    mo->traits |= mo->info->traits & MF_CLOAKED;
    mo->sc.order = (sc_order_t){.kind = SC_ORDER_UNBURROW};
    int state = sc_burrow_states[mo->type_id - 1][1];
    if (state) P_SetMobjState(mo, state);
}
static void dug_out(mobj_t *mo) {
    /* A mine only bursts; it keeps the prey it woke for. */
    mo->traits |= mo->info->traits & (mo->type_id == MT_SPIDER_MINE ? MF_MOBILE : MF_MOBILE | MF_ATTACK);
    mo->sc.order = (sc_order_t){.target = mo->sc.order.target};
    P_SetMobjState(mo, mobjinfo[mo->type_id].spawnstate);
}
static void burrow_step(mobj_t *mo) {
    if (mo->sc.order.kind == SC_ORDER_BURROW) { hold(mo); if (settled(mo)) dug_in(mo); }
    else if (mo->sc.order.kind == SC_ORDER_UNBURROW && settled(mo)) dug_out(mo);
}

/* ── Recall and Stasis Field ─────────────────────────────────────────── */

static bool square(const mobj_t *mo, fixed2_t at, fixed_t half) {
    return abs(mo->core.position.x - at.x) <= half && abs(mo->core.position.y - at.y) <= half;
}
/* The nearest open spot to at for unit: flyers stack, walkers need clear,
 * unoccupied ground, searched ring by ring out to eight cells. */
bool sc_place_near(mobj_t *unit, fixed2_t at) {
    if (unit->traits & MF_FLY) { unit->core.position = fixed3_with_xy(unit->core.position, at); return true; }
    for (int ring = 0; ring <= 8; ring++)
        for (int y = -ring; y <= ring; y++)
            for (int x = -ring; x <= ring; x++) {
                if (abs(x) != ring && abs(y) != ring) continue;
                fixed2_t spot = fixed2_add(at, (fixed2_t){FIXED_FROM_INT(x), FIXED_FROM_INT(y)});
                if (!L_Contains(&level, spot.x >> FIXED_FRAC_BITS, spot.y >> FIXED_FRAC_BITS) ||
                    !P_CheckPosition(&level, unit, spot)) continue;
                bool taken = false;
                for (thinker_t *th = thinkercap.next; th != &thinkercap && !taken; th = th->next) {
                    const mobj_t *mo = (const mobj_t *)th;
                    taken = th->function == P_MobjThinker && mo != unit && !mo->remove && mo->hp > 0 &&
                        !(mo->traits & (MF_FLY | MF_NOBLOCKMAP)) && (mo->traits & MF_MOBILE) &&
                        fixed2_distance_squared64(where(mo), spot) < fixed_sq64(P_MobjRadius(unit) + P_MobjRadius(mo));
                }
                if (taken) continue;
                unit->core.position = fixed3_with_xy(unit->core.position, spot);
                return true;
            }
    return false;
}
/* OpenBW order_CastRecall: the Arbiter's own units in the field, but not
 * buildings, hallucinations, burrowed units, larvae or eggs, come to it. */
static void recall(mobj_t *field) {
    mobj_t *arbiter = P_MobjById(field->sc.parent);
    if (!arbiter || arbiter->hp <= 0 || arbiter->remove) return;
    fixed2_t at = where(field), home = where(arbiter);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *v = (mobj_t *)th;
        if (v == arbiter || !body(v) || v->owner != arbiter->owner || building(v) || !(v->traits & MF_MOBILE) ||
            (v->sc.flags & (SC_HALLUCINATION | SC_BURROWED | SC_LOADED)) || v->type_id == MT_LARVA ||
            v->type_id == MT_EGG || !square(v, at, SC_RECALL_HALF)) continue;
        fixed3_t was = v->core.position;
        if (!sc_place_near(v, home)) { v->core.position = was; continue; }
        sc_interrupt(v);
        stand(v);
        v->waypoints = (waypoints_t){0};
    }
}
/* Stasis Field freezes everyone in its square, friend or foe: invincible,
 * disabled, out of reach of spells, for 131 status beats. */
static void stasis(mobj_t *caster, fixed2_t at) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *v = (mobj_t *)th;
        if (v == caster || !body(v) || building(v) || (v->sc.flags & SC_BURROWED) || !square(v, at, SC_STASIS_HALF)) continue;
        sc_interrupt(v);
        v->waypoints = (waypoints_t){0};
        v->sc.timers[SC_TIMER_STASIS] = SC_STASIS_FRAMES;
        stand(v);
    }
}

/* ── Infestation ─────────────────────────────────────────────────────── */

/* The Queen goes in, the Command Center turns Zerg under her owner, and she
 * comes out 60 frames later (OpenBW order_InfestingCommandCenter). */
static void infest(mobj_t *queen, mobj_t *cc) {
    mobj_t *addon = sc_addon_of(cc);
    if (addon) { addon->sc.parent = 0; addon->owner = addon->team = 11; addon->allegiance = ALLEGIANCE_NEUTRAL; }
    cc->sc.addon = 0;
    if (cc->production) sc_cancel_production(cc, -1);
    cc->sc.rally = (sc_rally_t){0};
    int hp = cc->hp;
    if (!P_MorphMobj(cc, MT_INFESTED_COMMAND_CENTER)) return;
    cc->hp = hp;
    cc->owner = queen->owner;
    cc->team = queen->team;
    cc->allegiance = queen->allegiance;
    P_MobjSetSelected(cc, false);
    P_SyncBuildingBlocking();
    queen->traits = (queen->traits & ~(MF_SELECTABLE | MF_SELECTED | MF_MOBILE)) | MF_NOBLOCKMAP | MF_DONTDRAW;
    stand(queen);
    queen->sc.order = (sc_order_t){.kind = SC_ORDER_INFEST, .target = cc->id, .time = 60};
}
static void infesting(mobj_t *queen, int frames) {
    if ((queen->sc.order.time -= frames) > 0) return;
    mobj_t *cc = P_MobjById(queen->sc.order.target);
    queen->traits = (queen->traits & ~(MF_NOBLOCKMAP | MF_DONTDRAW)) | (queen->info->traits & (MF_SELECTABLE | MF_MOBILE));
    if (cc) sc_place_near(queen, where(cc));
    queen->sc.order = (sc_order_t){0};
    stand(queen);
}

/* ── Spider Mines ────────────────────────────────────────────────────── */

/* Every Vulture comes with three mines, usable once researched. */
enum { SC_VULTURE_MINES = 3 };
void sc_lay_mine(mobj_t *vulture, fixed2_t at) {
    mobj_t *mine = spawn_for(vulture, MT_SPIDER_MINE, at);
    if (!mine) return;
    vulture->sc.hangar--;
    mine->core.angle = vulture->core.angle;
    mine->sc.parent = vulture->id;
    sc_burrow(mine);
}

/* The effect itself, in range, energy paid. */
static void cast_now(mobj_t *caster, int tech, mobj_t *target, fixed2_t at) {
    caster->sc.energy -= energy_cost(tech);
    switch (tech) {
    case SC_TECH_LOCKDOWN: target->sc.timers[SC_TIMER_LOCKDOWN] = SC_LOCKDOWN_FRAMES; hold(target); break;
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
            mobj_t *brood = spawn_for(caster, MT_BROODLING, fixed2_add(at, (fixed2_t){i ? 26214 : -26214, 0}));
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
            mobj_t *copy = spawn_for(caster, target->type_id, fixed2_add(where(target), (fixed2_t){i ? FIXED_ONE * 3 / 4 : -FIXED_ONE * 3 / 4, FIXED_ONE / 2}));
            if (!copy) continue;
            copy->hp = target->hp;
            copy->sc.flags |= SC_HALLUCINATION;
            copy->sc.timers[SC_TIMER_LIFE] = SC_HALLUCINATION_FRAMES;
        }
        break;
    case SC_TECH_EMP:
        AROUND(v, at, SC_PIXELS(sc_weapon(SC_WEAPON_EMP)->splash[0]), caster) { sc_start(v); v->sc.shields = 0; v->sc.energy = 0; }
        break;
    case SC_TECH_ENSNARE:
        AROUND(v, at, SC_SPELL_RADIUS, caster) {
            if (building(v)) continue;
            v->sc.timers[SC_TIMER_ENSNARE] = SC_ENSNARE_FRAMES;
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
    case SC_TECH_SPIDER_MINES: sc_lay_mine(caster, at); break;
    case SC_TECH_INFESTATION: infest(caster, target); return;
    case SC_TECH_STASIS_FIELD: stasis(caster, at); break;
    case SC_TECH_RECALL: {
        mobj_t *field = spawn_area(MT_MAP_REVEALER, at, caster, tech, SC_RECALL_DELAY, 255);
        (void)field;
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
        (o->tech == SC_TECH_NUCLEAR_STRIKE && !sc_armed_silo(caster->owner)) ||
        (o->tech == SC_TECH_SPIDER_MINES && caster->sc.hangar <= 0)) return false;
    fixed2_t at = target ? where(target) : from_pixel(o->at);
    fixed_t range = reach(spell(o->tech));
    if (range < 0 || near(caster, at, range + FIXED_ONE / 2 + (target ? P_MobjRadius(target) : 0))) {
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

bool sc_cast(mobj_t *caster, int tech, mobj_t *target, fixed2_t at) {
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
    if (tech == SC_TECH_SIEGE_MODE) {
        if (!P_Deploy(caster)) return false;
        sc_interrupt(caster);
        caster->waypoints=(waypoints_t){0};
        return true;
    }
    if (tech == SC_TECH_ARCHON_WARP) return sc_merge(caster, target);
    if (tech == SC_TECH_STIM_PACKS) {
        if (caster->hp <= SC_STIM_HP || sc_disabled(caster) || (caster->sc.flags & SC_LOADED)) return false;
        caster->hp -= SC_STIM_HP; /* OpenBW: ten points straight off, no armor. */
        if (caster->sc.timers[SC_TIMER_STIM] < SC_STIM_FRAMES) caster->sc.timers[SC_TIMER_STIM] = SC_STIM_FRAMES;
        return true;
    }
    if (tech == SC_TECH_BURROWING) {
        if (caster->sc.order.kind == SC_ORDER_BURROW || caster->sc.order.kind == SC_ORDER_UNBURROW ||
            sc_disabled(caster) || (caster->sc.flags & SC_LOADED) || (caster->traits & MF_FLY)) return false;
        if (caster->sc.flags & SC_BURROWED) sc_unburrow(caster);
        else sc_burrow(caster);
        return true;
    }
    if (caster->sc.flags & SC_BURROWED) return false;
    if (tech == SC_TECH_SPIDER_MINES) {
        if (caster->sc.hangar <= 0 || !L_Contains(&level, at.x >> FIXED_FRAC_BITS, at.y >> FIXED_FRAC_BITS)) return false;
        int cell = L_Index(&level, at.x >> FIXED_FRAC_BITS, at.y >> FIXED_FRAC_BITS);
        if ((level.blocked && level.blocked[cell]) || (level.cell_solid && (level.cell_solid[cell] & 1))) return false;
    }
    if (caster->sc.energy < energy_cost(tech) || (needs_target(tech) && !target_ok(caster, tech, target)) ||
        (tech == SC_TECH_NUCLEAR_STRIKE && !sc_armed_silo(caster->owner))) return false;
    if (!needs_target(tech)) target = NULL;
    fixed2_t destination=target?where(target):from_pixel(pixel(at));
    fixed_t range=reach(s);
    bool in_range=range<0||near(caster,destination,range+FIXED_ONE/2+(target?P_MobjRadius(target):0));
    if(!in_range) {
        fixed3_t momentum=caster->core.momentum;
        if(!P_MoveUnitTo(&level,caster,destination)) {
            caster->core.momentum=momentum;
            return false;
        }
        caster->movement.best_goal_dist=caster->movement.best_leg_dist=0;
    }
    sc_interrupt(caster);
    caster->waypoints=(waypoints_t){0};
    if(in_range)P_ClearMove(caster);
    caster->sc.order = (sc_order_t){.kind = SC_ORDER_CAST, .tech = tech, .target = target ? target->id : 0,
                                    .at = pixel(target ? where(target) : at)};
    caster->attack.target = NULL;
    if (!in_range) { caster->move_only=true; return true; }
    if (step_cast(caster)) return true;
    caster->sc.order = (sc_order_t){0};
    return false;
}

void sc_interrupt(mobj_t *unit) {
    if (!unit || !sc_unit(unit)) return;
    /* Burrowing, an infesting Queen and a mine are past recall. */
    int kind = unit->sc.order.kind;
    if (kind == SC_ORDER_BURROW || kind == SC_ORDER_UNBURROW || kind == SC_ORDER_INFEST) return;
    /* A nuke being painted is lost when the Ghost turns away. */
    if (unit->sc.order.kind == SC_ORDER_MERGE) {
        mobj_t *partner = P_MobjById(unit->sc.order.target);
        if (partner && partner->sc.order.kind == SC_ORDER_MERGE) partner->sc.order = (sc_order_t){0};
    }
    unit->sc.order = (sc_order_t){0};
    unit->sc.flags &= ~SC_HOLD;
}

static void irradiate(mobj_t *host) {
    int k = SC_IRRADIATE_BEATS - host->sc.timers[SC_TIMER_IRRADIATE]--;
    int damage = SC_IRRADIATE_DAMAGE * (k + 1) / SC_IRRADIATE_BEATS - SC_IRRADIATE_DAMAGE * k / SC_IRRADIATE_BEATS;
    fixed2_t at = where(host);
    AROUND(v, at, SC_IRRADIATE_RADIUS, NULL)
        if (!building(v) && (sc_units[v->type_id - 1].flags & SC_UNIT_ORGANIC)) P_DamageMobj(v, NULL, damage);
}

bool sc_dark_swarm_protects(const mobj_t *target) {
    if(!target || (target->traits & MF_FLY) || building(target))return false;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *cloud=(const mobj_t *)th;
        if(th->function==P_MobjThinker&&!cloud->remove&&cloud->type_id==MT_DARK_SWARM&&
           cloud->sc.order.tech==SC_TECH_DARK_SWARM&&cloud->sc.timers[SC_TIMER_LIFE]>0&&
           abs(cloud->core.position.x-target->core.position.x)<=SC_SWARM_RADIUS&&
           abs(cloud->core.position.y-target->core.position.y)<=SC_SWARM_RADIUS)return true;
    }
    return false;
}

int sc_projectile_spell_hit(const mobj_t *attacker, mobj_t *target, int dealt) {
    if (attacker->sc.flags & SC_HALLUCINATION) return 0;
    if (target->sc.flags & SC_HALLUCINATION) dealt *= 2;
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
    if (sc_disabled(mo) || mo->sc.order.kind == SC_ORDER_NUKE) hold(mo);
    if (mo->sc.order.kind == SC_ORDER_INFEST) { infesting(mo, frames); return; }
    burrow_step(mo);
    if (mo->remove || mo->hp <= 0) return;
    if (frames > 0) {
        static const int countdown[] = {SC_TIMER_MATRIX, SC_TIMER_ENSNARE, SC_TIMER_LOCKDOWN, SC_TIMER_STORM,
                                        SC_TIMER_STIM, SC_TIMER_STASIS};
        for (unsigned i = 0; i < sizeof(countdown) / sizeof(*countdown); i++)
            if (t[countdown[i]] > 0 && (t[countdown[i]] -= frames) <= 0) {
                t[countdown[i]] = 0;
                if (countdown[i] == SC_TIMER_MATRIX) mo->sc.matrix = 0;
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
            /* Areas and hallucinations vanish; broodlings die. A Recall
             * field takes its units as it closes. */
            if ((mo->traits & MF_NOBLOCKMAP) && mo->sc.order.tech == SC_TECH_RECALL) recall(mo);
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
        fixed2_t at = from_pixel(o->at);
        *o = (sc_order_t){0};
        nuke(mo, at);
    }
}
