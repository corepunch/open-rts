#include "engine.h"
#include "info.h"
#include "rustfront.h"

/* Who fires what: the projectile, how far in front of the shooter it appears (cells) and how
 * high it flies (cells; the heavy shell follows its arc instead). */
static const struct { uint16_t shooter, projectile; fixed_t muzzle, height; } GUNS[] = {
    {MT_TANK, MT_SHELL, FIXED_LIT(1.0), FIXED_LIT(0.55)},
    {MT_GUN_TURRET, MT_SHELL, FIXED_LIT(0.7), FIXED_LIT(0.5)},
    {MT_ARTILLERY, MT_HEAVY_SHELL, FIXED_LIT(1.0), 0},
    {MT_ROCKET, MT_MISSILE, FIXED_LIT(0.4), FIXED_LIT(0.55)},
};

/* gameinfo_t.attack: launch the shooter's projectile at the target. It flies to where the
 * target stands now; A_RfImpact hits the target if it is still near when it lands. */
bool RF_Attack(mobj_t *attacker, const weapondef_t *weapon, mobj_t *target) {
    for (size_t i = 0; i < sizeof(GUNS) / sizeof(*GUNS); ++i) {
        if (GUNS[i].shooter != attacker->type_id) continue;
        mobj_t *shot = P_SpawnMissile(attacker, target, GUNS[i].projectile);
        if (!shot) return false;
        shot->rf_victim = target->id;
        shot->missile.damage = weapon->damage;
        /* Start at the muzzle: skip the steps the barrel covers. */
        const missiledef_t *flight = &shot->info->missile;
        int skip = (int)(GUNS[i].muzzle / flight->step);
        if (skip >= shot->missile.duration) skip = shot->missile.duration - 1;
        shot->core.position.x += shot->core.momentum.x * skip;
        shot->core.position.y += shot->core.momentum.y * skip;
        shot->missile.duration -= skip;
        shot->core.position.z = GUNS[i].height;
        return true;
    }
    return false;
}

static bool impact_in_progress;
bool RF_ImpactInProgress(void) { return impact_in_progress; }

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
        P_DamageMobj(victim, source, damage);
    else
        victim = NULL;
    if (shot->type_id == MT_HEAVY_SHELL) {
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
            mobj_t *other = (mobj_t *)th;
            if (th->function != P_MobjThinker || other == victim || other == shot || other->remove ||
                other->hp <= 0 || other->type_id >= MT_FX_HIT || other->team == shot->team) continue;
            if (within(other, at, FIXED_LIT(1.5))) P_DamageMobj(other, source, damage / 2);
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
