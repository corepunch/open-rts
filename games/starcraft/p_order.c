#include "sc_local.h"

static bool repairable(const mobj_t *worker, const mobj_t *target) {
    const sc_unit_t *u=sc_unit(target);
    return worker->type_id==MT_SCV && target && target!=worker && u &&
        !target->remove && target->hp>0 && P_IsAlly(worker,target) &&
        (target->hp<target->max_hp || target->sc.wound) &&
        (u->race&2) && (u->flags&SC_UNIT_MECHANICAL) &&
        !(target->sc.flags&(SC_LOADED|SC_HALLUCINATION)) &&
        !(worker->sc.flags&(SC_LOADED|SC_HALLUCINATION)) &&
        !(worker->production && worker->production->placed);
}

/* Use the normal Stop command so orders share one cancellation lifecycle. */
static void stop(int player, mobj_t *unit) {
    ticcmd_t cmd={.order=TC_STOP,.count=1,.units={unit->id}};
    G_RunTiccmd(player,&cmd);
}

bool sc_core_order(int player, const ticcmd_t *cmd, mobj_t *const *units,
                   int count, mobj_t *target) {
    bool patrol=cmd->order==TC_PATH && cmd->path.mode==WP_BACKTRACK && cmd->path.count==1;
    bool attack_move=cmd->order==TC_ATTACK && !cmd->target;
    bool repair=cmd->order==TC_REPAIR;
    bool contextual=cmd->order==TC_ORDER && target;
    bool returning=cmd->order==TC_RETURN_GOODS;
    if (!patrol && !attack_move && !repair && !contextual && !returning &&
        cmd->order!=TC_STAND_GROUND) return false;
    if (contextual) {
        bool handled=false;
        ticcmd_t remaining=*cmd;remaining.count=0;
        for (int i=0;i<count;i++) {
            mobj_t *u=units[i];
            bool cargo=(u->traits&MF_HARVESTER) && u->harvest.cargo>0 &&
                (target->traits&MF_RESOURCE_BASE) && target->owner==u->owner;
            bool fix=target->type_id!=MT_REFINERY && repairable(u,target);
            if (!cargo && !fix) {remaining.units[remaining.count++]=u->id;continue;}
            ticcmd_t next=*cmd;
            next.order=cargo?TC_RETURN_GOODS:TC_REPAIR;
            next.count=1;next.units[0]=u->id;
            G_RunTiccmd(player,&next);
            handled=true;
        }
        if (!handled) return false;
        /* Preserve the ordinary right-click for the rest of a mixed group. */
        if (remaining.count) G_RunTiccmd(player,&remaining);
        return true;
    }
    ivec2_t at=patrol?cmd->path.points[0]:fixed2_cell(fixed3_xy(cmd->position));
    if ((patrol||attack_move) && !L_Contains(&level,at.x,at.y)) return true;
    if (returning && cmd->target && (!target || !(target->traits&MF_RESOURCE_BASE) ||
        target->owner!=player)) return true;
    for (int i=0;i<count;i++) {
        mobj_t *u=units[i];
        if (!(u->traits&MF_MOBILE) || (u->sc.flags&SC_LOADED) ||
            u->sc.timers[SC_TIMER_LOCKDOWN]>0 || (u->production&&u->production->placed)) continue;
        if (repair && !repairable(u,target)) continue;
        if (returning && (!(u->traits&MF_HARVESTER) || u->harvest.cargo<=0)) continue;
        int resource=u->harvest.target;
        stop(player,u);
        if (repair) u->sc.order=(sc_order_t){.kind=SC_ORDER_REPAIR,.target=target->id};
        else if (returning) {
            u->harvest.target=resource;
            u->harvest.phase=HARVEST_PHASE_TO_BASE;
            u->move_only=true;
            P_ReturnHarvest(&level,u,target);
        } else if (patrol||attack_move) {
            u->waypoints=(waypoints_t){.count=patrol?2:1,.mode=patrol?WP_BACKTRACK:WP_ONCE,
                .points={at,fixed2_cell(fixed3_xy(u->core.position))}};
            u->movement.order_arrived=false;
            P_MoveUnitTo(&level,u,fixed2_cell_center(at));
        } else u->sc.flags|=SC_HOLD;
        P_SetMobjState(u,mobjinfo[u->type_id].spawnstate);
    }
    return true;
}

void sc_repair_ticker(mobj_t *worker, int frames) {
    if (worker->sc.order.kind!=SC_ORDER_REPAIR) return;
    mobj_t *target=P_MobjById(worker->sc.order.target);
    if (!repairable(worker,target)) {
        stop(worker->owner,worker);
        P_SetMobjState(worker,mobjinfo[worker->type_id].spawnstate);
        return;
    }
    if (worker->sc.timers[SC_TIMER_LOCKDOWN]>0) return;
    const sc_unit_t *u=sc_unit(target);
    fixed2_t at=fixed3_xy(target->core.position), from=fixed3_xy(worker->core.position);
    irect_t bounds=P_MobjCells(target);
    /* Stargus uses one tile of repair reach. Native five-pixel reach needs
     * units.dat collision bounds, not the placement rectangles we have. */
    fixed2_t nearest=at;
    if (u->flags&SC_UNIT_BUILDING)
        nearest=(fixed2_t){fixed_max(FIXED_FROM_INT(bounds.x),fixed_min(from.x,FIXED_FROM_INT(bounds.x+bounds.w))),
                          fixed_max(FIXED_FROM_INT(bounds.y),fixed_min(from.y,FIXED_FROM_INT(bounds.y+bounds.h)))};
    if (fixed2_distance_squared64(from,nearest)>fixed_sq64(FIXED_ONE)) {
        if (u->flags&SC_UNIT_BUILDING) {
            if (!P_ApproachFootprint(worker,(ivec2_t){bounds.x,bounds.y},
                                     (isize2_t){bounds.w,bounds.h},&at)) return;
        }
        if (!P_HasMoveOrder(worker) || !ivec2_equal(fixed2_cell(worker->movement.goal),fixed2_cell(at)))
            P_MoveUnitTo(&level,worker,at);
        return;
    }
    P_ClearMove(worker);
    worker->attack.target=NULL;
    fixed2_t delta=fixed2_sub(at,from);
    worker->core.angle=P_PointToAngle(delta.x,delta.y);
    if (worker->info->harvest.state_id>0 && states[worker->core.state_id].group!=6)
        P_SetMobjState(worker,worker->info->harvest.state_id);
    /* OpenBW's construction rate and repair debit cadence, in 1/256 HP
     * per 24 Hz frame. Keep fractions in the existing damage remainder. */
    int64_t maxhp=(int64_t)target->max_hp*256;
    int rate=u->build_time>0?(int)((maxhp-maxhp/10+u->build_time-256)/u->build_time):256;
    if (rate<=0) rate=256;
    for (int f=0;f<frames;f++) {
        if (worker->sc.order.time) --worker->sc.order.time;
        else {
            int low=u->gas>0 && u->gas<u->minerals?u->gas:u->minerals;
            int costs[2]={0}, period=65535;
            if (low>0) {
                period=(int)(maxhp*3/((int64_t)low*rate));
                int price[2]={u->minerals,u->gas};
                for (int r=0;r<2;r++) {
                    costs[r]=period?price[r]/low:(int)((int64_t)price[r]*rate/(maxhp*3));
                    if (!period && price[r]>0 && !costs[r]) costs[r]=1;
                }
                if (!period) period=1;
            }
            int *resources=level.player_resources[worker->owner];
            if (resources[0]<costs[0] || resources[1]<costs[1]) {
                stop(worker->owner,worker);
                P_SetMobjState(worker,mobjinfo[worker->type_id].spawnstate);
                return;
            }
            resources[0]-=costs[0];resources[1]-=costs[1];
            worker->sc.order.time=period;
        }
        int64_t hp=(int64_t)target->hp*256-target->sc.wound+rate;
        if (hp>maxhp) hp=maxhp;
        target->hp=(int)((hp+255)/256);
        target->sc.wound=(uint8_t)((int64_t)target->hp*256-hp);
        target->sc.guard_hp=target->hp;
        if (hp==maxhp) {
            stop(worker->owner,worker);
            P_SetMobjState(worker,mobjinfo[worker->type_id].spawnstate);
            return;
        }
    }
}
