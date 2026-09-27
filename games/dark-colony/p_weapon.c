#include "game.h"
#include "info.h"
#include "m_random.h"

void A_DC_ArtilleryExplode(mobj_t *actor) {
    /* 0x43e501..0x43e521 selects one BOOMSTAT animation, not both. */
    int state = P_DC_Random() % 2 ? S_GASY1 : S_NUKE1;
    A_Explode(actor);
    P_SetMobjState(actor, state);
}
