#include "engine.h"
#include "info.h"
#include "rustfront.h"

static bool impact_in_progress;
bool RF_ImpactInProgress(void) { return impact_in_progress; }
static bool within(const mobj_t *a, fixed2_t at, fixed_t reach);

/* Who fires what: the projectile, how far in front of the shooter it appears (cells), how high
 * it flies (cells; arcing shells follow their arc instead), how many fly at once and how far
 * apart side by side (cells). Shooters not listed hit at once (machine guns). */
static const struct { uint16_t shooter, projectile; fixed_t muzzle, height; int count; fixed_t spread; } GUNS[] = {
    {MT_TANK, MT_SHELL, FIXED_LIT(1.0), FIXED_LIT(0.55), 1, 0},
    {MT_MASTODON, MT_SHELL, FIXED_LIT(1.4), FIXED_LIT(0.65), 2, FIXED_LIT(0.18)},
    {MT_GUN_TURRET, MT_SHELL, FIXED_LIT(0.7), FIXED_LIT(0.5), 1, 0},
    {MT_ARTILLERY, MT_HEAVY_SHELL, FIXED_LIT(1.0), 0, 1, 0},
    {MT_ROCKET, MT_MISSILE, FIXED_LIT(0.4), FIXED_LIT(0.55), 1, 0},
    {MT_CL_CRAWLER, MT_SHELL, FIXED_LIT(0.9), FIXED_LIT(0.5), 1, 0},
    {MT_CL_RIG, MT_MISSILE, FIXED_LIT(0.6), FIXED_LIT(0.7), 3, FIXED_LIT(0.25)},
    {MT_CL_TORCHER, MT_FLAME, FIXED_LIT(0.3), FIXED_LIT(0.35), 1, 0},
    {MT_LC_WARDEN, MT_BOLT, FIXED_LIT(0.4), FIXED_LIT(0.5), 1, 0},
    {MT_LC_LANCER, MT_BOLT, FIXED_LIT(0.4), FIXED_LIT(0.6), 1, 0},
    {MT_LC_SKIMMER, MT_BOLT, FIXED_LIT(0.6), FIXED_LIT(0.4), 1, 0},
    {MT_LC_GLAIVE, MT_BOLT, FIXED_LIT(1.2), FIXED_LIT(0.6), 1, 0},
    {MT_LC_PRISM, MT_HEAVY_BOLT, FIXED_LIT(0.8), FIXED_LIT(0.9), 1, 0},
    {MT_LC_TOWER, MT_BOLT, FIXED_LIT(0.6), FIXED_LIT(0.9), 1, 0},
    {MT_HAMMER, MT_HEAVY_MISSILE, FIXED_LIT(1.2), FIXED_LIT(0.9), 2, FIXED_LIT(0.3)},
    {MT_LC_AEGIS, MT_BOLT, FIXED_LIT(0.6), FIXED_LIT(0.7), 1, 0},
};

/* gameinfo_t.attack: launch the shooter's projectiles at the target. They fly to where the
 * target stands now; A_RfImpact hits the target if it is still near when one lands. Each
 * projectile carries the weapon's full damage. */
bool RF_Attack(mobj_t *attacker, const weapondef_t *weapon, mobj_t *target) {
    if (attacker->type_id == MT_CL_WRECKER) {
        RF_Detonate(attacker, target, RF_RankDamage(attacker, weapon->damage));
        return true;
    }
    for (size_t i = 0; i < sizeof(GUNS) / sizeof(*GUNS); ++i) {
        if (GUNS[i].shooter != attacker->type_id) continue;
        for (int n = 0; n < GUNS[i].count; ++n) {
            mobj_t *shot = P_SpawnMissile(attacker, target, GUNS[i].projectile);
            if (!shot) return n > 0;
            shot->rf_victim = target->id;
            shot->missile.damage = RF_RankDamage(attacker, weapon->damage);
            /* Start at the muzzle: skip the steps the barrel covers. */
            const missiledef_t *flight = &shot->info->missile;
            int skip = (int)(GUNS[i].muzzle / flight->step);
            if (skip >= shot->missile.duration) skip = shot->missile.duration - 1;
            shot->core.position.x += shot->core.momentum.x * skip;
            shot->core.position.y += shot->core.momentum.y * skip;
            shot->missile.duration -= skip;
            /* Side by side across the line of fire, centred on it. */
            if (GUNS[i].count > 1) {
                fixed_t side = GUNS[i].spread * (2 * n - (GUNS[i].count - 1)) / 2;
                fixed_t k = fixed_div32(side, flight->step);
                shot->core.position.x -= fixed_mul32(shot->core.momentum.y, k);
                shot->core.position.y += fixed_mul32(shot->core.momentum.x, k);
            }
            shot->core.position.z = GUNS[i].height;
        }
        return true;
    }
    return false;
}

/* An Aegis (the Concord's shield craft) takes a third off every hit on its side's units
 * within its field, itself included; fields do not stack. */
#define AEGIS_FIELD FIXED_LIT(3.5)
int RF_ShieldDamage(const mobj_t *victim, int damage) {
    if (!victim || victim->type_id >= MT_FX_HIT) return damage;
    fixed2_t at = fixed3_xy(victim->core.position);
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *aegis = (const mobj_t *)th;
        if (th->function != P_MobjThinker || aegis->type_id != MT_LC_AEGIS || aegis->remove || aegis->hp <= 0 ||
            aegis->team != victim->team || !within(aegis, at, AEGIS_FIELD)) continue;
        return damage - damage / 3;
    }
    return damage;
}

/* Each second an Aegis mends a dozen hit points of every vehicle of its side in its field. */
void RF_AegisMend(mobj_t *const *mobjs, int count) {
    for (int i = 0; i < count; ++i) {
        const mobj_t *aegis = mobjs[i];
        if (!aegis || aegis->type_id != MT_LC_AEGIS || aegis->remove || aegis->hp <= 0) continue;
        for (int j = 0; j < count; ++j) {
            mobj_t *u = mobjs[j];
            if (!u || u == aegis || u->remove || u->hp <= 0 || u->team != aegis->team || u->hp >= u->max_hp ||
                !(u->traits & MF_MOBILE) || RF_IsInfantry(u->type_id) || u->type_id >= MT_FX_HIT ||
                !within(u, fixed3_xy(aegis->core.position), AEGIS_FIELD)) continue;
            u->hp = u->hp + 12 < u->max_hp ? u->hp + 12 : u->max_hp;
        }
    }
}

/* Every enemy of `team` within `radius` of `at` takes `damage` (the shield applies). */
static void blast(mobj_t *source, fixed2_t at, fixed_t radius, int damage, int team, const mobj_t *spare) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *other = (mobj_t *)th;
        if (th->function != P_MobjThinker || other == spare || other == source || other->remove || other->hp <= 0 ||
            other->type_id >= MT_FX_HIT || other->team == team) continue;
        fixed_t reach = radius + (RF_IsStructure(other) ? other->info->footprint.w * (FIXED_ONE / 2) : 0);
        if (within(other, at, reach)) P_DamageMobj(other, source, RF_ShieldDamage(other, damage));
    }
}

/* A Wrecker reaching its target goes up with its drums: full damage to the target, half to
 * every other enemy within two cells. Shot dead on the way, it still goes up, for a third. */
static bool detonating;
void RF_Detonate(mobj_t *wrecker, mobj_t *target, int damage) {
    if (!wrecker || detonating || wrecker->remove) return;
    detonating = true;
    impact_in_progress = true;
    fixed2_t at = fixed3_xy(wrecker->core.position);
    if (target && !target->remove && target->hp > 0)
        P_DamageMobj(target, wrecker, RF_ShieldDamage(target, damage));
    blast(wrecker, at, FIXED_LIT(2.0), target ? damage / 2 : damage / 3, wrecker->team, target);
    mobj_t *boom = P_SpawnMobj(wrecker->core.position, MT_FX_BIGBOOM);
    if (boom) { boom->owner = boom->team = 0; boom->allegiance = ALLEGIANCE_NEUTRAL; }
    if (wrecker->hp > 0) P_DamageMobj(wrecker, NULL, wrecker->hp);
    impact_in_progress = false;
    detonating = false;
}


static fixed_t reach_of(const mobj_t *victim) {
    const isize2_t foot = victim->info ? victim->info->footprint : (isize2_t){0, 0};
    int span = foot.w > foot.h ? foot.w : foot.h;
    return (victim->traits & MF_MOBILE) || span <= 0 ? FIXED_LIT(0.9) : span * (FIXED_ONE / 2) + FIXED_ONE / 2;
}

static bool within(const mobj_t *a, fixed2_t at, fixed_t reach) {
    return fixed2_distance_squared64(fixed3_xy(a->core.position), at) <= (int64_t)reach * reach;
}

/* The first frame of a projectile's blast: full damage to its target if it is still within
 * reach of the impact; a heavy shell also does half to every other enemy within 1.5 cells. */
void A_RfImpact(mobj_t *shot) {
    if (!shot) return;
    fixed2_t at = fixed3_xy(shot->core.position);
    mobj_t *source = shot->target;
    mobj_t *victim = P_MobjById(shot->rf_victim);
    int damage = shot->missile.damage;
    impact_in_progress = true;
    if (victim && !victim->remove && victim->hp > 0 && within(victim, at, reach_of(victim)))
        P_DamageMobj(victim, source, RF_ShieldDamage(victim, damage));
    else
        victim = NULL;
    if (shot->type_id == MT_HEAVY_SHELL || shot->type_id == MT_HEAVY_BOLT || shot->type_id == MT_HEAVY_MISSILE) {
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
            mobj_t *other = (mobj_t *)th;
            if (th->function != P_MobjThinker || other == victim || other == shot || other->remove ||
                other->hp <= 0 || other->type_id >= MT_FX_HIT || other->team == shot->team) continue;
            if (within(other, at, FIXED_LIT(1.5))) P_DamageMobj(other, source, RF_ShieldDamage(other, damage / 2));
        }
    }
    impact_in_progress = false;
}

/* A flying rocket leaves a puff of smoke every other tic. */
void RF_TrailRockets(mobj_t *const *mobjs, int count) {
    for (int i = 0; i < count; ++i) {
        const mobj_t *rocket = mobjs[i];
        if (!rocket || rocket->remove || rocket->type_id != MT_MISSILE || !(rocket->traits & MF_MISSILE)) continue;
        mobj_t *puff = P_SpawnMobj(rocket->core.position, MT_FX_SMOKE);
        if (!puff) continue;
        puff->owner = puff->team = 0;
        puff->allegiance = ALLEGIANCE_NEUTRAL;
    }
}

/* gameinfo_t.mobj_ticker: a standing soldier keeps looking for targets every tic, whatever
 * frame of its slow idle loop is showing. */
void RF_MobjTicker(mobj_t *mobj) {
    if (RF_IsInfantry(mobj->type_id) && mobj->hp > 0 &&
        mobj->core.state_id == mobjinfo[mobj->type_id].spawnstate)
        A_Look(mobj);
}
