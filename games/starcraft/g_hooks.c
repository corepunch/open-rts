#include "engine.h"
#include "starcraft.h"

bool G_TakeCameraRequest(fvec2_t *cell) { return sc_take_camera(cell); }

void G_ProductionBegin(int elapsed_ms) { sc_zerg_ticker(elapsed_ms); }

bool G_QueueLocksProduct(const StaticProductDefinition *product) {
    return product->product_class!=RTS_PRODUCT_UNIT;
}

void G_UnitProduced(mobj_t *unit,const mobj_t *producer) {
    if(!unit||!producer||!(unit->traits&MF_MOBILE)||!producer->sc.rally.set)return;
    mobj_t *target=P_MobjById(producer->sc.rally.target);
    fixed2_t point=target&&target->hp>0&&!target->remove?
        fixed3_xy(target->core.position):producer->sc.rally.at;
    P_MoveUnitTo(&level,unit,point);
}

void G_UnitKilled(mobj_t *victim,mobj_t *killer) {
    if(killer&&victim&&killer!=victim&&!P_IsAlly(killer,victim)&&killer->sc.kills<UINT16_MAX)
        killer->sc.kills++;
}

bool G_GameUnitCommand(int player, const ticcmd_t *cmd, mobj_t *const *units,
                       int count, mobj_t *target) {
    (void)player;
    if (cmd->order == TC_STOP || cmd->order == TC_MOVE || cmd->order == TC_ATTACK || cmd->order == TC_ORDER ||
        cmd->order == TC_PATH || cmd->order == TC_HARVEST || cmd->order == TC_WAYPOINT)
        for (int i = 0; i < count; ++i) sc_interrupt(units[i]);
    return sc_order(cmd->order, units, count, cmd->product, target, fixed3_xy(cmd->position));
}
