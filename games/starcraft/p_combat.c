#include "sc_local.h"
/* StarCraft combat on the engine's weapon model: weapons.dat rows become
 * weapondef_t, every hit goes through damage types, armor and shields, and
 * casters keep energy, which pays for their spells. Rates are OpenBW's, in
 * 1/256 points per 24 Hz frame. */
enum {
    SC_UPGRADE_PLASMA_SHIELDS = 15,
    SC_HP_REGEN = 4, SC_BURN_DAMAGE = 20, SC_SHIELD_REGEN = 7, SC_ENERGY_REGEN = 8, SC_CLOAK_DRAIN = 13,
    SC_START_ENERGY = 50 << 8, SC_MAX_ENERGY = 200 << 8, SC_REACTOR_ENERGY = 50 << 8,
    /* An interceptor strikes once a pass; weapons.dat's cooldown of 1 is
     * its attack script's repeat, so this engine times a pass instead. */
    SC_INTERCEPTOR_PASS = 30,
    SC_REAVER_COOLDOWN = 60, /* Retail Reaver: a scarab every 60 frames. */
};
/* The native recharge order accepts completed, damaged Protoss mobile units
 * owned by the battery's player, excluding Interceptors and Scarabs. */
enum { SC_BATTERY_RATE = 5 << 8 };

const sc_weapon_t *sc_weapon(int id) {
    return id >= 0 && id < SC_WEAPONS && sc_weapons[id].name ? &sc_weapons[id] : NULL;
}

/* Attack scripts that strike twice for one cooldown (two attackmelee in
 * ZealotGndAttkRpt; the Firebat's flames). Brood War's units.dat records max
 * ground hits 2 for these; the classic DAT has no such column, and
 * weapons.dat's damage factor already gives the Goliath and Scout 2 air hits. */
static const mobjtype_id_t strikes_twice[] = {MT_FIREBAT, MT_ZEALOT, 10 + 1 /* Gui Montag */, 77 + 1 /* Fenix */};

/* The 24 Hz frames this engine tic spans (0 or 1 at 35 tics a second). */
static int tic_frames(void) { return leveltime * 24 / RTS_TICRATE - (leveltime - 1) * 24 / RTS_TICRATE; }
static int frames_ms(int frames) { return (frames * 1000 + 23) / 24; }
static bool sc_type(const mobj_t *mo) { return sc_unit(mo) != NULL; }
enum { HERO_RAYNOR_VULTURE = 20, HERO_HUNTER_KILLER = 54, HERO_DEVOURING_ONE = 55, HERO_YGGDRASILL = 58,
       HERO_FENIX_ZEALOT = 78, HERO_MOJO = 81 };


static weapondef_t weapon(int id, uint8_t targets) {
    const sc_weapon_t *w = sc_weapon(id);
    if (!w || !w->damage) return (weapondef_t){0};
    uint8_t splash = w->explosion == SC_EXPLOSION_RADIAL ? SPLASH_RADIAL :
                     w->explosion == SC_EXPLOSION_ENEMY ? SPLASH_ENEMY :
                     w->explosion == SC_EXPLOSION_AIR ? SPLASH_AIR : SPLASH_NONE;
    return (weapondef_t){.range = FIXED_FROM_INT((w->max_range + 31) / 32), .min_range = SC_PIXELS(w->min_range),
        .damage = w->damage, .cooldown_ms = frames_ms(w->cooldown), .targets = targets, .hits = (uint8_t)w->factor,
        .splash = splash, .radius = {SC_PIXELS(w->splash[0]), SC_PIXELS(w->splash[1]), SC_PIXELS(w->splash[2])},
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
    if (child == MT_INTERCEPTOR) {
        out->attack.range = FIXED_FROM_INT(8);
        out->attack.hits = 4;
        out->attack.cooldown_ms = frames_ms(SC_INTERCEPTOR_PASS);
    } else if (child == MT_SCARAB) {
        out->attack.range = FIXED_FROM_INT(8);
        out->attack.cooldown_ms = frames_ms(SC_REAVER_COOLDOWN);
    } else if (type == MT_SCARAB) out->attack.range = FIXED_ONE;
    else if (type == MT_INTERCEPTOR) out->attack.cooldown_ms = frames_ms(SC_INTERCEPTOR_PASS);
    if (sc_units[type - 1].flags & SC_UNIT_PERMANENT_CLOAK) out->traits |= MF_CLOAKED;
}


/* Shields and energy take their spawn values on first use. */
void sc_start(mobj_t *mo) {
    if (!sc_type(mo) || (mo->sc.flags & SC_STARTED)) return;
    const sc_unit_t *u = &sc_units[mo->type_id - 1];
    mo->sc.flags |= SC_STARTED;
    mo->sc.shields = u->shields << 8;
    mo->sc.energy = (u->flags & SC_UNIT_SPELLCASTER) ? SC_START_ENERGY : 0;
    /* OpenBW create_unit: every Vulture carries three Spider Mines. */
    if (mo->type_id == MT_VULTURE || mo->type_id == HERO_RAYNOR_VULTURE) mo->sc.hangar = 3;
}

void sc_initial_hp(mobj_t *mo, unsigned percent) {
    int64_t max = (int64_t)mo->max_hp * 256;
    if (max <= 0) { mo->hp = 0; mo->sc.wound = 0; return; }
    int64_t hp = max * percent / 100;
    if (hp > max) hp = max;
    if (hp < 1) hp = 1;
    mo->hp = (int)((hp + 255) / 256);
    mo->sc.wound = (uint8_t)((int64_t)mo->hp * 256 - hp);
}

void sc_initial_vitals(mobj_t *mo, unsigned valid, unsigned shields, unsigned energy) {
    if (!(valid & (4 | 8))) return;
    const sc_unit_t *type = sc_unit(mo);
    if (!type) return;
    sc_start(mo);
    if ((valid & 4) && type->shields) {
        if (shields > 100) shields = 100;
        mo->sc.shields = (type->shields * (int)shields / 100) << 8;
    }
    if (valid & 8) {
        if (energy > 100) energy = 100;
        mo->sc.energy = sc_max_energy(mo) * (int)energy / 100;
    }
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
    const sc_unit_t *unit=sc_unit(mo);
    if(unit&&(unit->flags&SC_UNIT_HERO))
        return SC_MAX_ENERGY+SC_REACTOR_ENERGY;
    for (unsigned i = 0; i < sizeof(reactors) / sizeof(*reactors); i++)
        if (mo->type_id == reactors[i].type && sc_upgrade_level(mo->owner, reactors[i].upgrade))
            return SC_MAX_ENERGY + SC_REACTOR_ENERGY;
    return SC_MAX_ENERGY;
}

/* ── speed, cooldown and sight upgrades (OpenBW update_unit_speed_upgrades) ── */

static bool speed_upgraded(const mobj_t *mo) {
    int upgrade = -1;
    switch (mo->type_id) {
    case MT_VULTURE: case HERO_RAYNOR_VULTURE: upgrade = 17; break; /* Ion Thrusters */
    case MT_OVERLORD: upgrade = 26; break;                         /* Pneumatized Carapace */
    case MT_ZERGLING: upgrade = 27; break;                         /* Metabolic Boost */
    case MT_HYDRALISK: upgrade = 29; break;                        /* Muscular Augments */
    case MT_ZEALOT: upgrade = 34; break;                           /* Leg Enhancements */
    case MT_SCOUT: upgrade = 42; break;                            /* Gravitic Thrusters */
    case MT_SHUTTLE: upgrade = 37; break;                          /* Gravitic Drive */
    case MT_OBSERVER: upgrade = 39; break;                         /* Gravitic Boosters */
    case HERO_HUNTER_KILLER: case HERO_YGGDRASILL: case HERO_FENIX_ZEALOT: case HERO_MOJO: return true;
    }
    return upgrade >= 0 && sc_upgrade_level(mo->owner, upgrade);
}
static bool cooldown_upgraded(const mobj_t *mo) {
    return mo->type_id == HERO_DEVOURING_ONE || (mo->type_id == MT_ZERGLING && sc_upgrade_level(mo->owner, 28));
}
/* Stim Packs and a speed upgrade each count one up, Ensnare one down. */
static int speed_mod(const mobj_t *mo, bool upgraded) {
    return (mo->sc.timers[SC_TIMER_STIM] > 0) + upgraded - (mo->sc.timers[SC_TIMER_ENSNARE] > 0);
}
/* Top speed after Stim Packs, upgrades and Ensnare: half when slowed; when
 * hastened half again, at least 3 1/3 pixels a frame, or a Scout's 6 2/3. */
fixed_t sc_unit_speed(const mobj_t *mo) {
    fixed_t speed = mo->info->speed;
    int mod = speed_mod(mo, speed_upgraded(mo));
    if (mod < 0) return speed / 2;
    if (mod == 0) return speed;
    if (mo->type_id == MT_SCOUT) return SC_PIXELS(20 * 24) / 3;
    speed += speed / 2;
    return speed < SC_PIXELS(10 * 24) / 3 ? SC_PIXELS(10 * 24) / 3 : speed;
}
/* Corrosive Acid (OpenBW add_acid_spore, update_unit_values): the count is
 * the number of live spores; a tenth hit renews the one nearest to expiring. */
int sc_acid_spores(const mobj_t *unit) {
    int count = 0;
    for (int i = 0; i < SC_ACID_SPORES; i++) count += unit->sc.acid[i] != 0;
    return count;
}
void sc_add_acid_spore(mobj_t *unit) {
    uint8_t *best = unit->sc.acid;
    for (int i = 1; i < SC_ACID_SPORES; i++) if (unit->sc.acid[i] < *best) best = &unit->sc.acid[i];
    *best = SC_ACID_FRAMES;
}
void sc_acid_tick(mobj_t *unit, int frames) {
    for (int i = 0; i < SC_ACID_SPORES; i++)
        if (unit->sc.acid[i]) unit->sc.acid[i] = unit->sc.acid[i] > frames ? (uint8_t)(unit->sc.acid[i] - frames) : 0;
}
/* OpenBW get_modified_weapon_cooldown on whole frames, in its order: acid
 * spores add an eighth (at least 3) each, Stim Packs and a cooldown upgrade
 * halve, Ensnare adds a quarter (the three net out as one step), then the
 * result is kept to 5..250 frames. */
int sc_modified_cooldown_frames(const mobj_t *unit, int frames) {
    int spores = sc_acid_spores(unit);
    if (spores) frames += (frames / 8 > 3 ? frames / 8 : 3) * spores;
    int mod = speed_mod(unit, cooldown_upgraded(unit));
    if (mod > 0) frames /= 2;
    if (mod < 0) frames += frames / 4;
    return frames > 250 ? 250 : frames < 5 ? 5 : frames;
}
/* The cooldown a shot sets (OpenBW attack_unit_fire_weapon and
 * try_attack_something): the modified cooldown plus the shared LCG's
 * (roll & 3) - 1, which is added after the clamp. An interceptor's pass and
 * a Carrier's relaunch are this engine's own timings, not a weapon row,
 * and stay as set. */
int G_WeaponCooldown(const mobj_t *unit, const weapondef_t *weapon, int cooldown_ms) {
    (void)weapon;
    if (!sc_type(unit) || unit->type_id == MT_INTERCEPTOR || sc_hangar_type(unit->type_id) == MT_INTERCEPTOR) return cooldown_ms;
    int frames = sc_modified_cooldown_frames(unit, (cooldown_ms * 24 + 500) / 1000) + (int)(sc_random() & 3) - 1;
    return frames_ms(frames);
}
/* Ocular Implants, Antennae, Sensor Array and Apial Sensors: 11 cells.
 * A unit aboard a flying transport sees nothing of its own. */
bool G_SightRadius(const mobj_t *actor, int *radius) {
    if (!sc_type(actor)) return true;
    if (actor->sc.flags & SC_LOADED) {
        const mobj_t *carrier = P_MobjById(actor->sc.parent);
        if (!carrier || !(sc_units[carrier->type_id - 1].flags & SC_UNIT_BUILDING)) return false;
    }
    static const struct { mobjtype_id_t type; int upgrade; } eyes[] = {
        {MT_GHOST, 20}, {MT_OVERLORD, 25}, {MT_OBSERVER, 38}, {MT_SCOUT, 41},
    };
    for (unsigned i = 0; i < sizeof(eyes) / sizeof(*eyes); i++)
        if (actor->type_id == eyes[i].type && sc_upgrade_level(actor->owner, eyes[i].upgrade)) *radius = 11;
    return true;
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
    if (!sc_type(target) || (target->sc.flags & SC_INVINCIBLE) || target->sc.timers[SC_TIMER_STASIS]) return 0;
    sc_start(target);
    return take(target, w && w->type < 5 ? w->type : SC_DAMAGE_NORMAL, base_damage(owner, w, damage, divisor));
}

static int hit_damage(const mobj_t *attacker, const weapondef_t *def, mobj_t *target, int damage, int divisor) {
    if (!sc_type(attacker) || !sc_type(target)) return damage / divisor;
    if ((target->sc.flags & SC_INVINCIBLE) || target->sc.timers[SC_TIMER_STASIS]) return 0;
    sc_start(target);
    const sc_weapon_t *w = sc_weapon(def->native_id);
    int dealt = sc_projectile_spell_hit(attacker, target, base_damage(attacker->owner, w, damage, divisor));
    return dealt > 0 ? take(target, w && w->type < 5 ? w->type : SC_DAMAGE_NORMAL, dealt) : 0;
}

bool sc_projectile_weapon(int id) {
    const sc_weapon_t *w=sc_weapon(id);
    return w&&w->damage>0&&(w->behavior==0||w->behavior==1||w->behavior==SC_BEHAVIOR_BOUNCE)&&
        w->explosion==1&&w->movement==1&&w->speed>0&&w->acceleration>0;
}

static void projectile_visual(fixed3_t position,int state);

static bool weapon_misses(const mobj_t *source,const mobj_t *target) {
    unsigned src=sc_terrain_flags(fixed3_xy(source->core.position));
    unsigned dst=sc_terrain_flags(fixed3_xy(target->core.position));
    int chance=0;
    if(!(target->traits&MF_FLY)) {
        if(sc_dark_swarm_protects(target))chance=255;
        else if(dst&SC_TERRAIN_COVER)chance=119;
        int source_height=(src&SC_TERRAIN_HIGH)?2:(src&SC_TERRAIN_MIDDLE)?1:0;
        int target_height=(dst&SC_TERRAIN_HIGH)?2:(dst&SC_TERRAIN_MIDDLE)?1:0;
        if(!(source->traits&MF_FLY)&&target_height>source_height&&chance<119)chance=119;
    }
    return (sc_random()&255)<=(unsigned)chance;
}

static void immediate_hit(mobj_t *source,const sc_weapon_t *w,mobj_t *target,int divisor) {
    if(!sc_type(target)||target->remove||target->hp<=0||
       (target->sc.flags&SC_INVINCIBLE)||target->sc.timers[SC_TIMER_STASIS])return;
    sc_start(target);
    int dealt=sc_projectile_spell_hit(source,target,base_damage(source->owner,w,w->damage,divisor));
    int damage=dealt>0?take(target,w->type<5?w->type:SC_DAMAGE_NORMAL,dealt):0;
    if(damage>0)P_DamageMobj(target,source,damage);
    else P_ProvokeMobj(target,source);
}

/* OpenBW fire_weapon: clip the line from the target's shrunken inner box
 * centre toward the shooter, then place an appear-at-target bullet there. */
static fixed2_t immediate_position(const mobj_t *source,const mobj_t *target) {
    irect_t box=sc_unit(target)->bounds;
    ivec2_t unit={target->core.position.x/SC_PIXELS(1),target->core.position.y/SC_PIXELS(1)};
    int left=unit.x+box.x,top=unit.y+box.y,right=left+box.w-1,bottom=top+box.h-1;
    int mx=(right-left)/4,my=(bottom-top)/4;
    left+=mx;right-=mx;top+=my;bottom-=my;
    ivec2_t from={left+(right-left)/2,top+(bottom-top)/2};
    ivec2_t to={source->core.position.x/SC_PIXELS(1),source->core.position.y/SC_PIXELS(1)};
    for(;;) {
        int flags=(to.y<top?8:0)|(to.y>bottom?4:0)|(to.x>right?2:0)|(to.x<left?1:0);
        if(!flags)break;
        if(flags&8)to=(ivec2_t){from.x+(int)((int64_t)(top-from.y)*(to.x-from.x)/(to.y-from.y)),top};
        else if(flags&4)to=(ivec2_t){from.x+(int)((int64_t)(bottom-from.y)*(to.x-from.x)/(to.y-from.y)),bottom};
        else if(flags&2)to=(ivec2_t){right,from.y+(int)((int64_t)(right-from.x)*(to.y-from.y)/(to.x-from.x))};
        else to=(ivec2_t){left,from.y+(int)((int64_t)(left-from.x)*(to.y-from.y)/(to.x-from.x))};
    }
    return (fixed2_t){SC_PIXELS(to.x),SC_PIXELS(to.y)};
}

/* OpenBW bullet type 2 appears at the target and rolls once per projectile. */
enum { SC_BEHAVIOR_APPEAR_TARGET = 2 };
static void immediate_fire(mobj_t *source,const weapondef_t *def,mobj_t *target,const sc_weapon_t *w) {
    for(int i=0;i<(def->hits?def->hits:1);i++) {
        bool missed=weapon_misses(source,target);
        if(w->explosion==1) {
            if(!missed)immediate_hit(source,w,target,1);
            continue;
        }
        fixed2_t at=immediate_position(source,target);
        if(missed) {
            fixed2_t heading;angle_to_screen_vector_fixed(source->core.angle,&heading.x,&heading.y);
            at=fixed2_sub(at,fixed2_scale(heading,SC_PIXELS(30)));
        }
        for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
            mobj_t *victim=(mobj_t *)th;
            if(th->function!=P_MobjThinker||victim==source||victim->remove||victim->hp<=0||
               !sc_type(victim)||(victim->traits&(MF_NOBLOCKMAP|MF_MISSILE))||
               !(w->targets&((victim->traits&MF_FLY)?1u:2u))||
               (w->explosion==SC_EXPLOSION_ENEMY&&victim!=target&&P_IsAlly(source,victim)))continue;
            int d=sc_point_distance(victim,at);
            int divisor=d<=w->splash[0]?1:d<=w->splash[1]?2:d<=w->splash[2]?4:0;
            if(divisor)immediate_hit(source,w,victim,divisor);
        }
    }
}

bool sc_projectile_fire(mobj_t *source,const weapondef_t *def,mobj_t *target) {
    if(!source||!target||!def)return false;
    const sc_weapon_t *w=sc_weapon(def->native_id);
    if(!w)return false;
    if(w->behavior==SC_BEHAVIOR_APPEAR_TARGET&&w->damage>0&&(w->explosion==1||w->explosion==SC_EXPLOSION_RADIAL||w->explosion==SC_EXPLOSION_ENEMY)) {
        immediate_fire(source,def,target,w);return true;
    }
    if(!sc_projectile_weapon(def->native_id))return false;
    fixed2_t heading; angle_to_screen_vector_fixed(source->core.angle,&heading.x,&heading.y);
    fixed2_t at=fixed2_add(fixed3_xy(source->core.position),fixed2_scale(heading,SC_PIXELS(w->forward)));
    at=fixed2_add(at,(fixed2_t){0,-SC_PIXELS(w->upward)});
    for(int i=0;i<w->factor;i++) {
    mobj_t *shot=P_SpawnMobj(fixed3_from_fixed2(at,source->core.position.z),MT_SC_PROJECTILE);
    if(!shot)break;
    shot->owner=source->owner; shot->team=source->team; shot->allegiance=source->allegiance;
    shot->target=source; shot->attack.target=target; shot->core.angle=source->core.angle;
    sc_mission_t *mission=level.mission;
    if(w->spin) {
        bool clockwise=mission&&mission->previous_bullet_source==source->id?
            !mission->previous_bullet_clockwise:(sc_random()&1)!=0;
        if(mission){mission->previous_bullet_source=source->id;mission->previous_bullet_clockwise=clockwise;}
        angle_t offset=(angle_t)((uint32_t)w->spin<<24);
        shot->core.angle+=clockwise?offset:0u-offset;
    }
    shot->sc.flags=source->sc.flags&SC_HALLUCINATION;
    shot->movement.goal=fixed3_xy(target->core.position);
    bool missed=weapon_misses(source,target);
    if(missed)shot->movement.goal=fixed2_sub(shot->movement.goal,fixed2_scale(heading,SC_PIXELS(30)));
    shot->sc.projectile=(sc_projectile_t){.weapon=def->native_id,.turn=w->turn,
        .death_state=sc_projectile_death_state(def->native_id),
        .trail_state=sc_projectile_trail_state(def->native_id),.missed=missed,
        .trail_repeat=sc_projectile_trail_repeats(def->native_id)};
    if(sc_projectile_states[def->native_id]) {
        shot->traits|=MF_RENDERABLE;
        P_SetMobjState(shot,sc_projectile_states[def->native_id]);
    }
    }
    return true;
}

static void projectile_impact(mobj_t *shot) {
    mobj_t *target=shot->attack.target,*source=shot->target;
    const sc_weapon_t *w=sc_weapon(shot->sc.projectile.weapon);
    if(!w){P_RemoveMobj(shot);return;}
    mobj_t *next=NULL;
    if(w->behavior==SC_BEHAVIOR_BOUNCE&&shot->missile.phase<2&&source&&!source->remove&&source->hp>0) {
        fixed2_t at=fixed3_xy(shot->core.position);
        for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
            mobj_t *candidate=(mobj_t *)th;
            if(th->function!=P_MobjThinker||candidate==target||candidate->id==shot->sc.projectile.previous||
               candidate->allegiance==ALLEGIANCE_NEUTRAL||!P_CanTarget(source,candidate)||!P_VisibleTo(source,candidate))continue;
            fixed2_t delta=fixed2_sub(fixed3_xy(candidate->core.position),at);
            if(abs(delta.x)<=WEAPON_BOUNCE_RANGE&&abs(delta.y)<=WEAPON_BOUNCE_RANGE){next=candidate;break;}
        }
    }
    bool can_hit=!shot->sc.projectile.missed&&target&&!target->remove&&target->hp>0&&sc_type(target)&&
        !(target->sc.flags&SC_INVINCIBLE)&&!target->sc.timers[SC_TIMER_STASIS];
    int divisor=shot->missile.phase==0?1:shot->missile.phase==1?3:9;
    int damage=0;
    if(can_hit) {
        sc_start(target);
        int dealt=sc_projectile_spell_hit(shot,target,base_damage(shot->owner,w,w->damage,divisor));
        damage=dealt>0?take(target,w->type<5?w->type:SC_DAMAGE_NORMAL,dealt):0;
    }
    if(!next) {
        int death=shot->sc.projectile.death_state;
        sc_script_begin(shot,sc_weapon_script[shot->sc.projectile.weapon],SC_ANIM_DEATH);
        if(death>0&&P_SetMobjState(shot,death)) shot->sc.projectile.weapon=-1;
        else P_RemoveMobj(shot);
    }
    if(damage>0)P_DamageMobj(target,source?source:shot,damage);
    else if(can_hit&&source)P_ProvokeMobj(target,source);
    if(next) {
        sc_script_begin(shot,sc_weapon_script[shot->sc.projectile.weapon],SC_ANIM_SPECIAL1);
        /* GlaveWurmSpecialState1 spawns image 512 (SporeHit) at every
         * intermediate hit. The compiled Death chain shares that image's
         * Init state, so reuse it without adding projectile save state. */
        int hit_state=shot->sc.projectile.death_state;
        for(int n=0;hit_state>0&&hit_state<SC_STATES&&n<256;n++) {
            int sprite=states[hit_state].sprite;
            if(sprite>=SC_TYPES&&sprite<SC_SPRITES&&sprnames[sprite]&&
               !strcmp(sprnames[sprite],"sc-img-512")) {
                projectile_visual(shot->core.position,hit_state);
                break;
            }
            int following=states[hit_state].nextstate;
            if(following==hit_state)break;
            hit_state=following;
        }
        shot->sc.projectile.previous=target?target->id:0;
        shot->attack.target=next;
        if(!shot->sc.projectile.missed)shot->movement.goal=fixed3_xy(next->core.position);
        ++shot->missile.phase;
    }
}

static fixed2_t projectile_map_bounds(fixed2_t point) {
    fixed_t max_x=FIXED_FROM_INT(level.width)-SC_PIXELS(1), max_y=FIXED_FROM_INT(level.height)-SC_PIXELS(1);
    return (fixed2_t){point.x<0?0:point.x>max_x?max_x:point.x,
                      point.y<0?0:point.y>max_y?max_y:point.y};
}

static void projectile_visual(fixed3_t position,int state) {
    if(state<=0)return;
    mobj_t *effect=P_SpawnMobj(position,MT_SC_PROJECTILE);
    if(!effect)return;
    effect->traits|=MF_RENDERABLE;
    effect->sc.projectile.weapon=-1;
    P_SetMobjState(effect,state);
}

static void projectile_tick(mobj_t *shot,int frames) {
    sc_projectile_t *p=&shot->sc.projectile;
    const sc_weapon_t *w=sc_weapon(p->weapon);
    if(!w){P_RemoveMobj(shot);return;}
    while(frames-->0&&!shot->remove) {
        if(++shot->missile.age<=1)continue; /* IScript Init waits one frame before sigorder. */
        if(shot->missile.wait){projectile_impact(shot);return;}
        if(p->trail_state>0&&(p->trail_repeat||!p->trail_emitted)) {
            projectile_visual(shot->core.position,p->trail_state);
            p->trail_emitted=true;
        }
        bool expired=w->behavior!=SC_BEHAVIOR_BOUNCE&&shot->missile.age-1>w->lifetime;
        if((w->behavior==1||w->behavior==SC_BEHAVIOR_BOUNCE)&&shot->attack.target&&!shot->sc.projectile.missed)
            shot->movement.goal=fixed3_xy(shot->attack.target->core.position);
        fixed2_t delta=fixed2_sub(shot->movement.goal,fixed3_xy(shot->core.position));
        fixed_t distance=fixed2_length(delta);
        if(!distance){projectile_impact(shot);return;}
        angle_t desired=angle_from_screen_vector_fixed(delta.x,delta.y);
        angle_t turn=p->turn>=128?ANG180:(angle_t)((uint32_t)p->turn<<24);
        int32_t change=(int32_t)(desired-shot->core.angle);
        uint32_t difference=angle_distance(desired,shot->core.angle);
        int distance_px=distance/SC_PIXELS(1);
        bool accelerate=difference==0||distance_px>=32;
        if(!accelerate&&p->turn>0) {
            int angle=(int)(difference>>24);
            int64_t needed=(int64_t)(angle*2+p->turn-1)*p->speed/(p->turn*256);
            accelerate=needed*3/2<=distance_px;
        }
        if(difference<=turn)shot->core.angle=desired;
        else shot->core.angle+=change>0?turn:0u-turn;
        if(w->flingy>=0x8d&&w->flingy<=0xab)p->turn++;
        p->speed+=accelerate?w->acceleration:-w->acceleration;
        if(p->speed<0)p->speed=0;
        if(p->speed>w->speed)p->speed=w->speed;
        fixed_t step=(fixed_t)(p->speed*8); /* 8.8 pixels to 16.16 cells. */
        if(distance<=step&&shot->core.angle==desired){shot->core.position=fixed3_with_xy(shot->core.position,shot->movement.goal);projectile_impact(shot);return;}
        fixed2_t direction; angle_to_screen_vector_fixed(shot->core.angle,&direction.x,&direction.y);
        fixed2_t next=fixed2_add(fixed3_xy(shot->core.position),fixed2_scale(direction,step));
        if(level.width>0&&level.height>0&&!L_Contains(&level,fixed_floor_int(next.x),fixed_floor_int(next.y))) {
            next=projectile_map_bounds(next);
            shot->missile.wait=1; /* OpenBW expires an out-of-bounds bullet on its next frame. */
        }
        shot->core.position=fixed3_with_xy(shot->core.position,next);
        if(expired){projectile_impact(shot);return;}
    }
}

bool sc_can_recharge_shields(const mobj_t *unit, mobj_t *battery) {
    if (!unit || !battery || battery->type_id != MT_SHIELD_BATTERY || battery->owner != unit->owner ||
        sc_foundation(battery)) return false; /* unit_can_use_shield_battery needs a completed battery */
    sc_start(battery);
    if (battery->sc.energy <= 0 || sc_disabled(battery) ||
        !sc_type(unit) || unit->owner >= 12 || unit->type_id == MT_INTERCEPTOR ||
        unit->type_id == MT_SCARAB || unit->type_id == MT_SHIELD_BATTERY ||
        !(unit->traits & MF_MOBILE) || (sc_units[unit->type_id - 1].flags & SC_UNIT_BUILDING) ||
        !(sc_units[unit->type_id - 1].race & 4) || !sc_units[unit->type_id - 1].shields ||
        unit->type_id == MT_EGG || unit->type_id == MT_COCOON || sc_disabled(unit)) return false;
    return sc_shields(unit) < sc_units[unit->type_id - 1].shields;
}

static void finish_recharging(mobj_t *unit, const mobj_t *battery) {
    sc_finish_recharge_order(unit, battery->id);
}

static void recharge(mobj_t *battery, int frames) {
    if (battery->sc.energy <= 0 || sc_disabled(battery)) return;
    for (thinker_t *th = thinkercap.next; th != &thinkercap && battery->sc.energy > 0; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || mo->sc.order.kind != SC_ORDER_FOLLOW ||
            mo->sc.order.target != battery->id || mo->remove || mo->hp <= 0 ||
            !sc_can_recharge_shields(mo, battery) || sc_unit_distance(mo, battery) > 128) continue;
        sc_start(mo);
        int room = (sc_units[mo->type_id - 1].shields << 8) - mo->sc.shields;
        int give = SC_BATTERY_RATE * frames;
        if (give > room) give = room;
        if (give > battery->sc.energy * 2) give = battery->sc.energy * 2;
        if (give <= 0) { finish_recharging(mo, battery); continue; }
        mo->sc.shields += give;
        battery->sc.energy -= give / 2;
        if (mo->sc.shields >= sc_units[mo->type_id - 1].shields << 8 || battery->sc.energy <= 0)
            finish_recharging(mo, battery);
    }
}

/* Spells and unit orders run every tic (a locked-down unit must stay put);
 * then, once for each 24 Hz frame, shields regenerate and casters regain
 * energy, except while a cloak they switched on drains it. */
static void sc_mobj_ticker(mobj_t *mo) {
    int frames = tic_frames();
    sc_script_tick(mo, frames);
    if(mo->type_id==MT_SC_PROJECTILE) {
        if(mo->sc.projectile.weapon>=0)projectile_tick(mo,frames);
        return;
    }
    if (!sc_type(mo)) return;
    sc_start(mo);
    sc_spell_ticker(mo, frames);
    if (mo->remove || mo->hp <= 0) return;
    sc_unit_ticker(mo, frames);
    sc_repair_ticker(mo, frames);
    sc_order_ticker(mo, frames);
    if (mo->remove || mo->hp <= 0) return;
    const sc_unit_t *u = &sc_units[mo->type_id - 1];
    /* Lifted buildings keep their one pixel a frame; burrowed units stand. */
    if ((mo->traits & MF_MOBILE) && !(u->flags & SC_UNIT_BUILDING)) mo->speed = sc_unit_speed(mo);
    if (frames <= 0) return;
    /* OpenBW update_unit_values heals completed units with the DAT regen
     * flag by 4/256 HP per frame, including hidden and invincible units. */
    if ((u->flags & SC_UNIT_REGENERATES) &&
        !(mo->production && mo->production->product_class == RTS_PRODUCT_BUILDING) &&
        (mo->hp < mo->max_hp || mo->sc.wound)) {
        int64_t hp = (int64_t)mo->hp * 256 - mo->sc.wound + SC_HP_REGEN * frames;
        int64_t max = (int64_t)mo->max_hp * 256;
        if (hp > max) hp = max;
        mo->hp = (int)((hp + 255) / 256);
        mo->sc.wound = (uint8_t)((int64_t)mo->hp * 256 - hp);
    }
    /* OpenBW updates burn damage only for completed buildings. */
    if ((u->race & 2) && (u->flags & SC_UNIT_BUILDING) && mo->max_hp > 0 &&
        !(mo->production && mo->production->placed &&
          mo->production->product_class == RTS_PRODUCT_BUILDING &&
          mo->production->actor_id == mo->type_id) &&
        (int64_t)mo->hp * 100 / mo->max_hp <= 33) {
        int damage = mo->sc.wound + SC_BURN_DAMAGE * frames;
        mo->sc.wound = (uint8_t)(damage & 255);
        if (damage >= 256) P_DamageMobj(mo, NULL, damage >> 8);
        if (mo->remove || mo->hp <= 0) return;
    }
    if (mo->sc.shields < u->shields << 8) {
        mo->sc.shields += SC_SHIELD_REGEN * frames;
        if (mo->sc.shields > u->shields << 8) mo->sc.shields = u->shields << 8;
    }
    if (sc_foundation(mo)) return; /* update_unit_energy runs only for completed units */
    if (!(u->flags & SC_UNIT_SPELLCASTER)) return;
    if ((mo->traits & MF_CLOAKED) && !(mo->info->traits & MF_CLOAKED) &&
        !(mo->sc.flags & SC_BURROWED)) {
        mo->sc.energy -= SC_CLOAK_DRAIN * frames;
        if (mo->sc.energy <= 0) { mo->sc.energy = 0; mo->traits &= ~MF_CLOAKED; }
    } else if (mo->sc.energy < sc_max_energy(mo)) {
        mo->sc.energy += SC_ENERGY_REGEN * frames;
        if (mo->sc.energy > sc_max_energy(mo)) mo->sc.energy = sc_max_energy(mo);
    }
    if (mo->type_id == MT_SHIELD_BATTERY) recharge(mo, frames);
}

/* Nobody fires on a unit in stasis, a loaded passenger or a mine's
 * blast, and a disabled unit fires on nobody. */
static bool can_target(const mobj_t *attacker, const mobj_t *victim) {
    if (!sc_type(victim) || !sc_type(attacker)) return true;
    return !victim->sc.timers[SC_TIMER_STASIS] && !(victim->sc.flags & SC_LOADED) && !sc_disabled(attacker) &&
        !(attacker->sc.flags & SC_BURROWED);
}

/* A passenger aboard a mobile transport may not fire at all; one set down
 * from it may not fire until the
 * timers OpenBW unit_unload sets run out: a Reaver's main-order timer
 * blocks its attacks at any target, other units wait for the ground or air
 * weapon cooldown that matches the target. */
static bool attack_blocked(const mobj_t *attacker, const mobj_t *target) {
    if (!sc_type(attacker)) return false;
    /* Only a Bunker's passengers shoot from inside. */
    if (attacker->sc.flags & SC_LOADED) {
        const mobj_t *carrier = P_MobjById(attacker->sc.parent);
        if (!carrier || !(sc_units[carrier->type_id - 1].flags & SC_UNIT_BUILDING)) return true;
    }
    /* States run before the ticker counts this tic's frame down, and
     * OpenBW counts a frame down before orders run, so a timer on its last
     * frame no longer blocks. */
    int frames = tic_frames();
    uint16_t child = sc_hangar_type(attacker->type_id);
    if ((child == MT_SCARAB || child == MT_INTERCEPTOR) && attacker->sc.timers[SC_TIMER_MAIN_ORDER] > frames) return true;
    return attacker->sc.timers[(target->traits & MF_FLY) ? SC_TIMER_AIR_COOLDOWN : SC_TIMER_GROUND_COOLDOWN] > frames;
}

void sc_init_combat(void) {
    game_info.attack_blocked = attack_blocked;
    game_info.can_target = can_target;
    game_info.hit_damage = hit_damage;
    game_info.mobj_ticker = sc_mobj_ticker;
    game_info.attack = sc_launch;
    game_info.range_bonus = sc_range_bonus;
    game_info.sight_teams = sc_sight_teams;
}
