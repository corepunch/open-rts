#ifndef __P_BLOOD__
#define __P_BLOOD__

#include "actor.h"

void A_DC_Damage(mobj_t *target);
int P_DC_BloodStates(uint16_t native_type, int out[7]);
uint32_t P_DC_Random(void);

#endif
