#include "engine.h"
#define TECH_STARTERS { MT_SURV_MOBILE_OUTPOST }
void A_KkndResearch(mobj_t *actor);
/* A Survivor with only a mobile outpost needs a machine shop for the Bomber,
 * and for a drill rig a machine shop, then a mobile derrick to deploy. */
#define TECH_CASES { \
    { 36, 1, { 42 } }, \
    { 24, 1, { 42 } }, \
    { 55, 2, { 42, 30 } }, \
}
/* The AI deploys a rig by buying its derrick under the rig's own goal, so it is sent to a tank. */
#define TECH_AI_CASE 1
#define TECH_AI_STEP() do { for (int tic = 0; tic < RTS_TICRATE; ++tic) \
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) \
        if (th->function == P_MobjThinker) A_KkndResearch((mobj_t *)th); } while (0)
#include "../tech_path_regression.h"
