#include "z_local.h"

/* Retail unit health (ZED.EXE). The damage routine 0x86ca4 subtracts from
 * the driven body's word at +8 or the unit's byte +0x3a, and its callers
 * test only the sign: the bullet pass at 0x5af58 and the explosion unit pass
 * at 0x85ea4 (`jns`) leave the unit standing at exactly zero, and only a
 * negative result kills. The engine's shared liveness test is `hp > 0`, so
 * a unit at native zero is the explicit state `z.zero_hp` with the engine
 * hp held at 1. Nothing is added to any other health: the flag is set only
 * by an exact-zero bullet or explosion result, any later damage takes the
 * engine hp to zero or below and kills (native zero minus a hit is negative),
 * and every reader of retail health goes through Z_NativeHp. */

static bool native_sign_test;

int Z_NativeHp(const mobj_t *mo) { return mo->hp - (mo->z.zero_hp ? 1 : 0); }

/* Store a retail health value: positive is stored as is, zero is the flagged
 * survivor, and a negative value leaves the unit dead. */
void Z_SetNativeHp(mobj_t *mo, int hp) {
    mo->z.zero_hp = hp == 0 && Z_UnitType(mo) >= 0;
    mo->hp = hp == 0 ? (mo->z.zero_hp ? 1 : 0) : hp;
}

/* The shared damage function asks this when a hit leaves exactly zero. */
bool G_DamageSurvives(mobj_t *target, const mobj_t *source) {
    (void)source;
    if (!native_sign_test || target->z.zero_hp || Z_UnitType(target) < 0) return false;
    target->z.zero_hp = true;
    target->hp = 1;
    return true;
}

/* A bullet or explosion hit on a unit: the retail sign-test callers. Other
 * damage paths keep the engine's rule until their callers are traced. */
void Z_NativeDamage(mobj_t *target, mobj_t *source, int damage) {
    native_sign_test = true;
    P_DamageMobj(target, source, damage);
    native_sign_test = false;
}

/* A saved zero-health unit must be a live unit at the sentinel hp. */
bool G_SavedObjectValid(const mobj_t *object) {
    unsigned char flag;
    memcpy(&flag, &object->z.zero_hp, 1);
    if (flag > 1 || (flag && (Z_UnitType(object) < 0 || object->hp != 1))) return false;
    for (int i = 0; i < object->z.crew_count && i < Z_CREW_MAX; ++i)
        if (object->z.crew[i].zero > 1 || (object->z.crew[i].zero && object->z.crew[i].hp != 1)) return false;
    return true;
}
