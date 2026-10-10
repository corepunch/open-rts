#include "engine.h"
#include "starcraft.h"

bool G_TakeCameraRequest(fvec2_t *cell) { return sc_take_camera(cell); }

void G_ProductionBegin(int elapsed_ms) { sc_zerg_ticker(elapsed_ms); }

bool G_GameUnitCommand(int player, const ticcmd_t *cmd, mobj_t *const *units,
                       int count, mobj_t *target) {
    (void)player;
    if (cmd->order == TC_STOP || cmd->order == TC_MOVE || cmd->order == TC_ATTACK || cmd->order == TC_ORDER ||
        cmd->order == TC_PATH || cmd->order == TC_HARVEST || cmd->order == TC_WAYPOINT)
        for (int i = 0; i < count; ++i) sc_interrupt(units[i]);
    return sc_order(cmd->order, units, count, cmd->product, target, fixed3_xy(cmd->position));
}
