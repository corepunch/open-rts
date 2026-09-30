#ifndef __P_WEAPON__
#define __P_WEAPON__

#include "game.h"

void DC_AimMissile(mobj_t *missile, fixed3_t destination);
void DC_ScatterMissile(mobj_t *missile, fixed3_t destination);
int DC_WeaponLevel(const mobj_t *actor);
int DC_DefendedDamage(const mobj_t *victim, int damage);
bool DC_LoadWeapons(level_t *map, const char *root);
void DC_FreeWeapons(level_t *map);
mobj_t *DC_FireMissiles(mobj_t *source, mobj_t *target, uint16_t type);

#endif
