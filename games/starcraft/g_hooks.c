#include "sc_local.h"

/* Mission extras and projectile state differ from earlier saves. */
uint32_t G_SaveLayout(void) { return 5; }

const uint32_t *G_AllianceMasks(const level_t *map) {
    if (!map) return NULL;
    const sc_mission_t *m = map->mission;
    return m ? m->allies : map->sight.allies;
}

uint32_t G_ConsistencyExtra(uint32_t hash) {
    const sc_mission_t *m = level.mission;
    if (m) hash = G_HashValue(hash, m->rng);
    if (m) { hash = G_HashValue(hash, m->previous_bullet_source); hash = G_HashValue(hash, m->previous_bullet_clockwise); }
    if (m) for (int i = 0; i < 8; ++i) {
        hash = G_HashValue(hash, m->allies[i]);
        hash = G_HashValue(hash, level.sight.allies[i]);
        for (int t = 0; t < SC_TYPES; t++) hash = G_HashValue(hash, m->deaths[i][t]);
    }
    return hash;
}

bool G_GameCommand(int player, const ticcmd_t *cmd) {
    if (cmd->order != TC_ALLY && cmd->order != TC_SHARE_SIGHT) return false;
    sc_mission_t *m = level.mission;
    if (!m || player < 0 || player >= 8 || cmd->target >= 8 || cmd->target == (unsigned)player ||
        (cmd->product != 0 && cmd->product != 1) || !G_PlayerSide(player) || !G_PlayerSide(cmd->target)) return true;
    uint32_t *row = cmd->order == TC_ALLY ? &m->allies[player] : &level.sight.allies[cmd->target];
    uint32_t bit = UINT32_C(0x40000000) >> (cmd->order == TC_ALLY ? cmd->target : player);
    if (cmd->product) *row |= bit; else *row &= ~bit;
    if (cmd->order == TC_SHARE_SIGHT) P_UpdateSight();
    return true;
}

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
    if(victim)victim->sc.queue=(sc_order_queue_t){0};
    sc_transport_killed(victim);
    if(killer&&victim&&killer!=victim&&!P_IsAlly(killer,victim)&&killer->sc.kills<UINT16_MAX)
        killer->sc.kills++;
}

bool G_UnitCommandable(const mobj_t *unit) {
    return !(unit->sc.flags&SC_LOADED) && !sc_disabled(unit) && unit->sc.order.kind!=SC_ORDER_INFEST &&
        unit->sc.order.kind!=SC_ORDER_LIFT &&
        !(unit->sc.order.kind==SC_ORDER_LAND&&unit->sc.order.time);
}

bool G_GameUnitCommand(int player, const ticcmd_t *cmd, mobj_t *const *units,
                       int count, mobj_t *target) {
    if(sc_queue_order(player,cmd,units,count))return true;
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
