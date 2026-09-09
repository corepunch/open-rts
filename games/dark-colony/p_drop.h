#ifndef __P_DROP__
#define __P_DROP__

#include "game.h"

bool DC_StartDropship(int team, ivec2_t origin,
                      const DropshipPayload *payload, int payload_count);
#endif
