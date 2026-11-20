/* Retail damage of one weapon hit: DAMAGE.TXT vulnerability by the weapon's
 * offense category and the target's defense class (dkreign.exe 0x474b40).
 *
 *   critical: rand(100) < crit%  -> the target dies outright
 *   variation: v = rand(2V) - V   (uniform, -V..V-1 percentage points)
 *   damage = ((vuln + v) * strength * F / 100 / 100) * weapon / 100
 *   F = (R^2 - d^2) * 100 / R^2 at distance d inside area R, 100 for a direct hit
 *
 * strength is the target's SetVulnerability percentage (100 neutral). Each
 * division truncates. A class with no rows takes the weapon's damage. */
#include "engine.h"
#include "dark-reign.h"
#include "info.h"

typedef struct { int16_t vuln; uint8_t variation, critical; } dr_vulnerability_t;
typedef struct { uint16_t native; int8_t defense; uint8_t strength; int8_t offense; uint8_t sight; } dr_defense_t;
typedef struct {
    uint16_t native;
    int16_t area, hitsize;
    struct { uint8_t times, cycles; int8_t offense; int16_t strength, area; } persist;
    struct { int8_t offense; int16_t strength, area; } self_destruct;
    struct { int16_t hp, cycles; uint16_t rate; uint8_t deadly; } booster;
    uint8_t cancels_boost;
} dr_weapon_t;
#include "damage_table.h"

static int native_of(const mobj_t *mobj) {
    if (!mobj || !gameinfo || mobj->type_id >= gameinfo->mobj_type_count) return -1;
    return gameinfo->mobjinfo[mobj->type_id].doomednum;
}

/* Both generated tables are sorted by native type. */
#define FIND(table, key) do { \
    int lo_ = 0, hi_ = (int)(sizeof(table) / sizeof(table[0])) - 1; \
    while (lo_ <= hi_) { \
        int mid_ = (lo_ + hi_) / 2; \
        if (table[mid_].native == (key)) return &table[mid_]; \
        if (table[mid_].native < (key)) lo_ = mid_ + 1; else hi_ = mid_ - 1; \
    } \
    return NULL; \
} while (0)

static const dr_defense_t *defense_of(const mobj_t *mobj) { int n = native_of(mobj); FIND(dr_defenses, n); }
static const dr_weapon_t *weapon_of(const mobj_t *mobj) { int n = native_of(mobj); FIND(dr_weapons, n); }

/* The retail game draws from one global LCG. Here each roll is a hash of
 * state every peer shares, so lockstep and saves need nothing new. */
static uint32_t roll(const mobj_t *attacker, const mobj_t *target, uint32_t salt) {
    uint32_t h = (uint32_t)leveltime * 0x9e3779b1u;
    h ^= (attacker ? attacker->id : 0) * 0x85ebca6bu;
    h ^= target->id * 0xc2b2ae35u ^ (uint32_t)target->hp * 0x27d4eb2fu ^ salt;
    h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
    return h;
}

int DR_OffenseOf(const mobj_t *mobj) {
    const dr_defense_t *d = defense_of(mobj);
    return d ? d->offense : -1;
}

int DR_DefenseOf(const mobj_t *mobj) {
    const dr_defense_t *d = defense_of(mobj);
    return d ? d->defense : -1;
}

int DR_VulnerableDamage(int offense, int defense, int strength, int damage, int falloff, int crit_roll,
                        int variation_roll, bool *critical) {
    if (critical) *critical = false;
    if (offense < 0 || offense >= DR_OFFENSES || defense < 0 || defense >= DR_DEFENSE_CLASSES) return damage;
    const dr_vulnerability_t *v = &dr_vulnerability[defense][offense];
    if (v->critical && crit_roll % 100 < v->critical) {
        if (critical) *critical = true;
        return damage;
    }
    int spread = v->variation ? variation_roll % (2 * v->variation) - v->variation : 0;
    int percent = (v->vuln + spread) * strength * falloff / 100 / 100;
    return percent * damage / 100;
}

int DR_HitDamage(const mobj_t *attacker, const weapondef_t *weapon, mobj_t *target, int damage, int divisor) {
    (void)weapon;
    if (divisor > 1) damage /= divisor;
    const dr_defense_t *a = defense_of(attacker), *t = defense_of(target);
    if (!a || !t || a->offense < 0) return damage;
    bool critical;
    damage = DR_VulnerableDamage(a->offense, t->defense, t->strength, damage, 100,
                                 (int)(roll(attacker, target, 1) % 100u),
                                 (int)(roll(attacker, target, 2) & 0x7fffffff), &critical);
    if (critical) return target->hp > 0 ? target->hp : damage;
    /* A hit always grazes: retail callers take the absolute value, and a
     * zero vulnerability (chaff on armour) leaves the target untouched. */
    return damage < 0 ? -damage : damage;
}

int DR_Falloff(int area, int distance) {
    if (distance < 0) distance = 0;
    if (area <= 0) return distance == 0 ? 100 : 0;
    if (distance >= area) return 0;
    return (int)(((int64_t)area * area - (int64_t)distance * distance) * 100 / ((int64_t)area * area));
}

/* Lingering blasts are simulation state: saved and in the lockstep hash. */
size_t G_SaveExtraSize(void) { return sizeof(level.dr_effects); }
void G_SaveExtra(void *out) { memcpy(out, level.dr_effects, sizeof(level.dr_effects)); }
bool G_LoadExtra(const void *data, size_t size) {
    if (size != sizeof(level.dr_effects)) return false;
    memcpy(level.dr_effects, data, size);
    return true;
}

uint32_t G_ConsistencyExtra(uint32_t hash) {
    for (int i = 0; i < DR_MAX_EFFECTS; ++i) {
        const dr_effect_t *e = &level.dr_effects[i];
        if (!e->remaining) continue;
        uint32_t v[] = {e->attacker, (uint32_t)e->x, (uint32_t)e->y, (uint32_t)e->strength, (uint32_t)e->area,
                        (uint32_t)e->offense, e->owner, e->remaining, e->period, e->wait, e->flying};
        for (unsigned k = 0; k < sizeof(v) / sizeof(*v); ++k) hash = (hash ^ v[k]) * 16777619u;
    }
    return hash;
}

bool G_LoadLevelData(const char *root, level_t *map) {
    (void)root;
    memset(map->dr_effects, 0, sizeof(map->dr_effects));
    return true;
}

enum { WORLD_PER_CELL = 24 };

static int32_t world_of(fixed_t cells) { return (int32_t)(((int64_t)cells * WORLD_PER_CELL) >> FIXED_FRAC_BITS); }

/* The victim's SetHitSize, or a building's footprint radius. */
static int hit_size(const mobj_t *victim) {
    const dr_weapon_t *w = weapon_of(victim);
    if (w && w->hitsize) return w->hitsize;
    return world_of(P_MobjRadius(victim));
}

static mobj_t *mobj_by_id(uint32_t id) {
    if (!id) return NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *m = (mobj_t *)th;
        if (th->function == P_MobjThinker && m->id == id && !m->remove) return m;
    }
    return NULL;
}

/* One impact (dkreign.exe 0x449e60): every unit within `area` of `at`,
 * less its hit size, takes the falloff share; `direct` takes it whole.
 * Retail's target filter was not decoded; a Martyr dies in its own blast,
 * so the blast spares nobody on the layer it lands on. */
static void impact(mobj_t *source, uint32_t source_id, fixed2_t at, int offense, int strength, int area,
                   mobj_t *direct, bool flying) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *victim = (mobj_t *)th;
        if (th->function != P_MobjThinker || victim->remove || victim->hp <= 0 ||
            (victim->traits & (MF_NOBLOCKMAP | MF_MISSILE))) continue;
        int falloff;
        if (victim == direct) falloff = 100;
        else {
            if (((victim->traits & MF_FLY) != 0) != flying) continue;
            fixed_t cells = fixed2_length(fixed2_sub(at, fixed3_xy(victim->core.position)));
            falloff = DR_Falloff(area, world_of(cells) - hit_size(victim));
            if (!falloff) continue;
        }
        const dr_defense_t *d = defense_of(victim);
        if (!d) continue;
        bool critical;
        uint32_t salt = source_id ^ ((uint32_t)offense << 24);
        int damage = DR_VulnerableDamage(offense, d->defense, d->strength, strength, falloff,
                                         (int)(roll(source, victim, salt + 1) % 100u),
                                         (int)(roll(source, victim, salt + 2) & 0x7fffffff), &critical);
        if (critical) damage = victim->hp;
        if (damage < 0) damage = -damage;
        if (damage > 0) P_DamageMobj(victim, source, damage);
        else if (source && victim == direct) P_ProvokeMobj(victim, source);
    }
}

static void linger(const mobj_t *source, fixed2_t at, bool flying, const dr_weapon_t *w) {
    if (!w->persist.times || w->persist.offense < 0) return;
    for (int i = 0; i < DR_MAX_EFFECTS; ++i) {
        dr_effect_t *e = &level.dr_effects[i];
        if (e->remaining) continue;
        *e = (dr_effect_t){.attacker = source->id, .x = at.x, .y = at.y, .strength = w->persist.strength,
                           .area = w->persist.area, .offense = w->persist.offense, .owner = source->owner,
                           .remaining = w->persist.times, .period = w->persist.cycles,
                           .wait = w->persist.cycles, .flying = flying};
        return;
    }
}

/* gameinfo_t.attack: Dark Reign's own impact for every damaging weapon.
 * Heals and repairs (negative strength) keep the engine's support path. */
bool DR_Attack(mobj_t *attacker, const weapondef_t *weapon, mobj_t *target) {
    if (!attacker || !weapon || !target || weapon->projectile_type) return false;
    const dr_weapon_t *w = weapon_of(attacker);
    if (w && w->booster.hp) {
        DR_Boost(target, w->booster.hp, w->booster.cycles, w->booster.rate, w->booster.deadly);
        return true;
    }
    /* CanCancelBoosting (0x474b40 caller, c:107968): the hit only ends the
     * boost; the unit keeps its hit points and the heal is lost. */
    if (w && w->cancels_boost && target->dr.boost) {
        target->dr.boost = 0;
        target->dr.timer = 0;
        target->dr.rate = 0;
        target->dr.floor = 0;
        return true;
    }
    if (weapon->damage <= 0) return false;
    const dr_defense_t *a = defense_of(attacker);
    if (!a || a->offense < 0) return false;
    fixed2_t at = fixed3_xy(target->core.position);
    bool flying = (target->traits & MF_FLY) != 0;
    for (int i = 0; i < (weapon->hits ? weapon->hits : 1); ++i)
        impact(attacker, attacker->id, at, a->offense, weapon->damage, w ? w->area : 0, target, flying);
    if (w) linger(attacker, at, flying, w);
    return true;
}

/* Each lingering blast strikes again every `period` cycles where it landed. */
void DR_TickEffects(void) {
    for (int i = 0; i < DR_MAX_EFFECTS; ++i) {
        dr_effect_t *e = &level.dr_effects[i];
        if (!e->remaining || --e->wait > 0) continue;
        e->wait = e->period;
        e->remaining--;
        mobj_t *source = mobj_by_id(e->attacker);
        if (!source) e->attacker = 0;
        impact(source, e->attacker, (fixed2_t){e->x, e->y}, e->offense, e->strength, e->area, NULL, e->flying);
    }
}

/* SetMovement(SelfDestruct): an order, never automatic. The unit dies and
 * blasts its position, the offense scaled by its health (SetSelfDestruct
 * 0 0 100 100 maps 0..100% health to 0..100%). */
bool DR_CanSelfDestruct(const mobj_t *unit) {
    const dr_weapon_t *w = weapon_of(unit);
    return unit && !unit->remove && unit->hp > 0 && w && w->self_destruct.offense >= 0;
}

bool DR_SelfDestruct(mobj_t *unit) {
    if (!DR_CanSelfDestruct(unit)) return false;
    const dr_weapon_t *w = weapon_of(unit);
    int percent = unit->max_hp > 0 ? unit->hp * 100 / unit->max_hp : 100;
    int strength = w->self_destruct.strength * percent / 100;
    fixed2_t at = fixed3_xy(unit->core.position);
    bool flying = (unit->traits & MF_FLY) != 0;
    uint32_t id = unit->id;
    P_DamageMobj(unit, NULL, unit->hp);
    impact(NULL, id, at, w->self_destruct.offense, strength, w->self_destruct.area, NULL, flying);
    return true;
}

bool G_GameUnitCommand(int player, const ticcmd_t *cmd, mobj_t *const *units, int count, mobj_t *target) {
    (void)player; (void)target;
    if (cmd->order != TC_SPELL || cmd->product != DR_SPELL_SELF_DESTRUCT) return false;
    for (int i = 0; i < count; ++i) DR_SelfDestruct(units[i]);
    return true;
}

/* SetSeeingRange (cells) for every type that declares one. */
bool G_SightRadius(const mobj_t *actor, int *radius) {
    const dr_defense_t *d = defense_of(actor);
    if (d && d->sight) *radius = d->sight;
    return true;
}

/* SetBooster (0x49d030): hit points become `hp` (capped at the type's
 * maximum), held for `cycles`, then fade by `rate`/256 a cycle down to the
 * old hit points, or to death when deadly. A boosted unit takes no other. */
bool DR_Boost(mobj_t *unit, int hp, int cycles, int rate, bool deadly) {
    if (!unit || unit->remove || unit->hp <= 0 || unit->dr.boost) return false;
    unit->dr.boost = 1;
    unit->dr.timer = (int16_t)cycles;
    unit->dr.rate = (uint16_t)rate;
    unit->dr.floor = (int16_t)(deadly ? 0 : unit->hp);
    unit->hp = hp < unit->max_hp ? hp : unit->max_hp;
    return true;
}

/* gameinfo_t.mobj_ticker: one retail cycle of the boost (0x4a20b6). */
void DR_MobjTicker(mobj_t *unit) {
    if (!unit->dr.boost) return;
    if (unit->dr.boost == 1) {
        if (--unit->dr.timer > 0) return;
        unit->dr.boost = 2;
        unit->dr.timer = 0;
        return;
    }
    int acc = (uint16_t)unit->dr.timer + unit->dr.rate;
    int loss = acc >> 8;
    unit->dr.timer = (int16_t)(acc & 0xff);
    if (unit->hp - loss > unit->dr.floor) {
        unit->hp -= loss;
        return;
    }
    int floor = unit->dr.floor;
    unit->dr.boost = 0;
    unit->dr.timer = 0;
    unit->dr.rate = 0;
    unit->dr.floor = 0;
    if (floor > 0) unit->hp = floor;
    else P_DamageMobj(unit, NULL, unit->hp); /* A deadly boost fades to death. */
}

/* gameinfo_t.can_target: a boosted unit takes no second boost. */
bool DR_CanTarget(const mobj_t *attacker, const mobj_t *victim) {
    const dr_weapon_t *w = weapon_of(attacker);
    return !(w && w->booster.hp && victim->dr.boost);
}
