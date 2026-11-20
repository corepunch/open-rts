/* Retail damage of one weapon hit: DAMAGE.TXT vulnerability by the weapon's
 * offense category and the target's defense class (dkreign.exe 0x474b40).
 *
 *   critical: rand(100) < crit%  -> the target dies outright
 *   variation: v = rand(2V) - V   (uniform, -V..V-1 percentage points)
 *   damage = ((vuln + v) * strength / 100) * weapon / 100
 *
 * strength is the target's SetVulnerability percentage (100 neutral). Each
 * division truncates. A class with no rows takes the weapon's damage. */
#include "engine.h"
#include "dark-reign.h"
#include "info.h"

typedef struct { int16_t vuln; uint8_t variation, critical; } dr_vulnerability_t;
typedef struct { uint16_t native; int8_t defense; uint8_t strength; int8_t offense; } dr_defense_t;
#include "damage_table.h"

static const dr_defense_t *defense_of(const mobj_t *mobj) {
    if (!mobj || !gameinfo || mobj->type_id >= gameinfo->mobj_type_count) return NULL;
    int native = gameinfo->mobjinfo[mobj->type_id].doomednum;
    int lo = 0, hi = (int)(sizeof(dr_defenses) / sizeof(dr_defenses[0])) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (dr_defenses[mid].native == native) return &dr_defenses[mid];
        if (dr_defenses[mid].native < native) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

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

int DR_VulnerableDamage(int offense, int defense, int strength, int damage, int crit_roll, int variation_roll,
                        bool *critical) {
    if (critical) *critical = false;
    if (offense < 0 || offense >= DR_OFFENSES || defense < 0 || defense >= DR_DEFENSE_CLASSES) return damage;
    const dr_vulnerability_t *v = &dr_vulnerability[defense][offense];
    if (v->critical && crit_roll % 100 < v->critical) {
        if (critical) *critical = true;
        return damage;
    }
    int spread = v->variation ? variation_roll % (2 * v->variation) - v->variation : 0;
    int percent = (v->vuln + spread) * strength / 100;
    return percent * damage / 100;
}

int DR_HitDamage(const mobj_t *attacker, const weapondef_t *weapon, mobj_t *target, int damage, int divisor) {
    (void)weapon;
    if (divisor > 1) damage /= divisor;
    const dr_defense_t *a = defense_of(attacker), *t = defense_of(target);
    if (!a || !t || a->offense < 0) return damage;
    bool critical;
    damage = DR_VulnerableDamage(a->offense, t->defense, t->strength, damage,
                                 (int)(roll(attacker, target, 1) % 100u),
                                 (int)(roll(attacker, target, 2) & 0x7fffffff), &critical);
    if (critical) return target->hp > 0 ? target->hp : damage;
    /* A hit always grazes: retail callers take the absolute value, and a
     * zero vulnerability (chaff on armour) leaves the target untouched. */
    return damage < 0 ? -damage : damage;
}
