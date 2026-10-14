#include "z_local.h"

/* Z's share of the engine's game hooks: its rules run once a tic after the
 * thinkers, and an order onto an empty vehicle or gun sends robots in. */

void G_ClockEnd(int64_t before, int64_t clock) {
    Z_Ticker();
    if (clock != before && (clock & 3) == 0) P_UpdateSight();
}

bool G_GameUnitCommand(int player, const ticcmd_t *cmd, mobj_t *const *units,
                       int count, mobj_t *target) {
    (void)player;
    return cmd->order == TC_ATTACK && Z_BoardOrder(units, count, target);
}

/* Any other order calls a robot off the vehicle it was walking to. */
void G_InterruptOrders(const ticcmd_t *cmd, mobj_t *const *units, int count, bool late) {
    bool interrupts = late ? cmd->order == TC_ORDER :
        cmd->order == TC_STOP || cmd->order == TC_MOVE || cmd->order == TC_ATTACK ||
        cmd->order == TC_PATH || cmd->order == TC_WAYPOINT;
    if (!interrupts) return;
    for (int i = 0; i < count; ++i) units[i]->z.board = 0;
}
