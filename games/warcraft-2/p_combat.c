#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

/* Melee and missile hits, Stratagus' damage roll, research bonuses, and
 * what happens to the ground when a building falls. */

static uint32_t w2_rng = 0x9E3779B9u;

/* The gameplay roll must match on every peer, so it is a plain xorshift
 * seeded with the level, never the presentation RNG. */
void W2_SeedCombat(uint32_t seed) {
    w2_rng = seed ? seed : 0x9E3779B9u;
}

uint32_t W2_SyncRand(void) {
    uint32_t x = w2_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    w2_rng = x;
    return x;
}

static int research_level(const mobj_t *unit, bool armor) {
    if (!unit || unit->owner >= 8 || unit->type_id == 0 || unit->type_id >= NUMMOBJTYPES) return 0;
    return armor ? level.upgrades[unit->type_id][unit->owner].armor :
                   level.upgrades[unit->type_id][unit->owner].weapon;
}

/* Sum of the bonuses of every researched tier that applies to the type. */
static int research_bonus(const mobj_t *unit, bool armor) {
    int tier = research_level(unit, armor), bonus = 0;
    for (int id = 1; id < W2_UPGRADE_COUNT; ++id) {
        const w2_upgrade_t *upgrade = W2_Upgrade(id);
        if (!upgrade || upgrade->armor != armor || upgrade->tier > tier) continue;
        for (int i = 0; i < 4 && upgrade->units[i]; ++i)
            if (upgrade->units[i] == unit->type_id) { bonus += upgrade->bonus; break; }
    }
    return bonus;
}

int W2_PiercingDamage(const mobj_t *unit) {
    if (!unit || unit->type_id == 0 || unit->type_id >= NUMMOBJTYPES) return 0;
    return mobjinfo[unit->type_id].w2.piercing_damage + research_bonus(unit, false);
}

int W2_Armor(const mobj_t *unit) {
    if (!unit || unit->type_id == 0 || unit->type_id >= NUMMOBJTYPES) return 0;
    return mobjinfo[unit->type_id].w2.armor + research_bonus(unit, true);
}

/* Stratagus CalculateDamageStats: armor eats basic damage down to one,
 * piercing damage always lands, and the roll takes back up to half. */
int W2_AttackDamage(const mobj_t *attacker, const mobj_t *target, uint32_t roll) {
    if (!attacker || !target || attacker->type_id == 0 || attacker->type_id >= NUMMOBJTYPES) return 0;
    int basic = mobjinfo[attacker->type_id].w2.basic_damage;
    int damage = basic - W2_Armor(target);
    if (damage < 1) damage = 1;
    damage += W2_PiercingDamage(attacker);
    damage -= (int)(roll % (uint32_t)((damage + 2) / 2));
    return damage < 1 ? 1 : damage;
}

/* The blow of the attack row: the target may have moved since the windup. */
void A_W2_Attack(mobj_t *unit) {
    mobj_t *target = unit ? unit->attack.target : NULL;
    if (!target || target->remove || target->hp <= 0 || unit->hp <= 0 ||
        !P_CanTarget(unit, target) || !P_InAttackRange(unit, target)) return;
    S_ActorSound(unit, SE_ATTACK);
    P_DamageMobj(target, unit, W2_AttackDamage(unit, target, W2_SyncRand()));
}

/* A destroyed building stops blocking its cells. */
void A_W2_Collapse(mobj_t *unit) {
    if (!unit || unit->type_id == 0 || unit->type_id >= NUMMOBJTYPES) return;
    isize2_t foot = mobjinfo[unit->type_id].w2.footprint;
    if (foot.w <= 0 || foot.h <= 0 || !level.cell_solid) return;
    fvec2_t centre = fixed3_xy_to_fvec2(unit->core.position);
    w2_clear_footprint((int)floorf(centre.x - foot.w * 0.5f + 0.001f),
                       (int)floorf(centre.y - foot.h * 0.5f + 0.001f), foot);
}

/* Saved games keep the combat dice (m_menu.c writes them with the campaign). */
uint32_t W2_CombatState(void) { return w2_rng; }
void W2_SetCombatState(uint32_t state) { w2_rng = state; }
