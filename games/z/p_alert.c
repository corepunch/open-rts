#include "z_local.h"
#include "info.h"

/* Who notices whom. The engine fires at what is in reach and chases a
 * target it was given; ZED.EXE's own seeking is not traced, so this is
 * authored: a unit with nothing to do goes for whatever its squad is
 * fighting, or else for the nearest enemy a few cells past its reach. A
 * robot that is shot at already turns on the shooter (the engine's
 * provoke); its squad follows from here. */

enum {
    ALERT_CELLS = 3,                 /* Cells past a unit's reach that it notices an enemy. */
    ALERT_PERIOD = RTS_TICRATE / 3,  /* Tics between looks, spread over the units. */
};

static bool live(const mobj_t *mo) { return mo && !mo->remove && mo->hp > 0; }

/* A driven vehicle or a robot of a side, free to go after someone. */
static bool idle(const mobj_t *mo) {
    return live(mo) && Z_UnitType(mo) >= 0 && mo->team < Z_NEUTRAL &&
           (mo->traits & (MF_ATTACK | MF_MOBILE)) == (MF_ATTACK | MF_MOBILE) &&
           !live(mo->attack.target) && !mo->z.board && !P_HasMoveOrder(mo);
}

static bool goes_for(const mobj_t *unit, const mobj_t *enemy) {
    return live(enemy) && enemy->team < Z_NEUTRAL && P_CanTarget(unit, enemy) && P_VisibleTo(unit, enemy);
}

static mobj_t *squad_enemy(const mobj_t *unit) {
    if (!unit->z.squad) return NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mate = (const mobj_t *)th;
        if (th->function != P_MobjThinker || mate == unit || mate->z.squad != unit->z.squad ||
            mate->team != unit->team || !live(mate)) continue;
        if (goes_for(unit, mate->attack.target)) return mate->attack.target;
    }
    return NULL;
}

static mobj_t *nearest_enemy(const mobj_t *unit) {
    mobj_t *best = NULL;
    int64_t best2 = 0;
    fixed2_t at = fixed3_xy(unit->core.position);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *enemy = (mobj_t *)th;
        if (th->function != P_MobjThinker || enemy == unit || !goes_for(unit, enemy)) continue;
        const weapondef_t *weapon = P_MobjWeapon(unit, enemy);
        fixed_t notice = (weapon ? weapon->range : 0) + ALERT_CELLS * FIXED_ONE;
        int64_t d2 = fixed2_distance_squared64(at, fixed3_xy(enemy->core.position));
        if (d2 > fixed_sq64(notice) || (best && d2 >= best2)) continue;
        best = enemy;
        best2 = d2;
    }
    return best;
}

void Z_Alert(mobj_t *unit) {
    if ((leveltime + unit->id) % ALERT_PERIOD || !idle(unit)) return;
    mobj_t *enemy = squad_enemy(unit);
    if (!enemy) enemy = nearest_enemy(unit);
    if (enemy) unit->attack.target = enemy;
}
