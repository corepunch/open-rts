#ifndef __D_TICCMD__
#define __D_TICCMD__

#include "m_vec.h"
#include "actor.h"

/* RTS replacement for Doom's movement/buttons. A group order is atomic. */
#define MAXCOMMANDUNITS 1024
typedef enum {
    TC_NONE, TC_ORDER, TC_MOVE, TC_HARVEST, TC_ATTACK, TC_STOP, TC_BUILD
} ticorder_t;

typedef struct {
    uint32_t consistancy;
    ticorder_t order;
    fixed3_t position;
    uint32_t target;
    int product;
    unsigned count;
    uint32_t units[MAXCOMMANDUNITS];
} ticcmd_t;

void G_BuildTiccmd(ticcmd_t *cmd);
bool G_QueueTiccmd(const ticcmd_t *cmd);
void G_ClearTiccmds(void);
void G_RunTiccmd(int player, const ticcmd_t *cmd);
uint32_t G_Consistency(void);
bool G_NetSignature(const char *map_path, uint32_t *signature);
bool G_SelectedTiccmd(ticorder_t order, mobj_t *const *units, int count,
                     fvec2_t position, uint32_t target);
bool G_BuildOrder(mobj_t *producer, int product);

#endif
