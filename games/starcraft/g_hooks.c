#include "sc_local.h"

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

bool G_UnitCommandable(const mobj_t *unit) {
    return !(unit->sc.flags&SC_LOADED) && !unit->sc.timers[SC_TIMER_LOCKDOWN] &&
        unit->sc.order.kind!=SC_ORDER_LIFT &&
        !(unit->sc.order.kind==SC_ORDER_LAND&&unit->sc.order.time);
}

bool G_GameUnitCommand(int player, const ticcmd_t *cmd, mobj_t *const *units,
                       int count, mobj_t *target) {
    if(sc_core_order(player,cmd,units,count,target))return true;
    /* A stale individual passenger click must not become Unload All. */
    if(cmd->order==TC_UNLOAD&&cmd->target&&!target)return true;
    return sc_order(cmd->order, units, count, cmd->product, target, fixed3_xy(cmd->position));
}

void G_InterruptOrders(const ticcmd_t *cmd, mobj_t *const *units, int count, bool late) {
    if (!late && (cmd->order == TC_STOP || cmd->order == TC_MOVE || cmd->order == TC_ATTACK || cmd->order == TC_ORDER ||
        cmd->order == TC_PATH || cmd->order == TC_HARVEST || cmd->order == TC_WAYPOINT))
        for (int i = 0; i < count; ++i) {
            sc_interrupt(units[i]);
            units[i]->move_only=false;
        }
}
