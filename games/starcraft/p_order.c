#include "sc_local.h"

int sc_flight_states[SC_TYPES][2];

static ivec2_t pixels(fixed2_t at) {
    return (ivec2_t){at.x / SC_PIXELS(1), at.y / SC_PIXELS(1)};
}
static irect_t collision_box(const mobj_t *unit, fixed2_t at) {
    irect_t box=sc_unit(unit)->bounds;
    ivec2_t origin=ivec2_add(pixels(at),(ivec2_t){box.x,box.y});
    return (irect_t){origin.x,origin.y,box.w,box.h};
}
int sc_unit_distance(const mobj_t *a, const mobj_t *b) {
    irect_t x=collision_box(a,fixed3_xy(a->core.position));
    irect_t y=collision_box(b,fixed3_xy(b->core.position));
    int dx=SDL_max(0,SDL_max(x.x-y.x-y.w,y.x-x.x-x.w));
    int dy=SDL_max(0,SDL_max(x.y-y.y-y.h,y.y-x.y-x.h));
    if(dx<dy){int swap=dx;dx=dy;dy=swap;}
    /* OpenBW xy_length, including its integer rounding. */
    return dx/4<dy?dx-dx/16+dy*3/8-dx/64+dy*3/256:dx;
}
static int overlap(irect_t a, irect_t b) {
    return SDL_max(0,SDL_min(a.x+a.w,b.x+b.w)-SDL_max(a.x,b.x))*
           SDL_max(0,SDL_min(a.y+a.h,b.y+b.h)-SDL_max(a.y,b.y));
}
static bool repair_step_clear(const level_t *map, const mobj_t *unit, fixed2_t at) {
    irect_t box=collision_box(unit,at), old=collision_box(unit,fixed3_xy(unit->core.position));
    if(box.x<0||box.y<0||box.x+box.w>map->width*32||box.y+box.h>map->height*32)return false;
    for(int y=box.y/32;y<=(box.y+box.h-1)/32;y++)
        for(int x=box.x/32;x<=(box.x+box.w-1)/32;x++) {
            int index=L_Index(map,x,y);
            if((map->blocked&&map->blocked[index])||(map->cell_solid&&(map->cell_solid[index]&1))) {
                irect_t tile={x*32,y*32,32,32};
                if(overlap(box,tile)>overlap(old,tile))return false;
            }
        }
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *other=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||other==unit||other->remove||other->hp<=0||!sc_unit(other)||
           (other->traits&(MF_MOBILE|MF_FLY|MF_NOBLOCKMAP)))continue;
        irect_t obstacle=collision_box(other,fixed3_xy(other->core.position));
        if(overlap(box,obstacle)>overlap(old,obstacle))return false;
    }
    return true;
}

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
    bool follow=cmd->order==TC_FOLLOW || (cmd->order==TC_MOVE&&target);
    bool lift=cmd->order==TC_LIFT,land=cmd->order==TC_LAND;
    if (!patrol && !attack_move && !repair && !contextual && !returning &&
        !follow&&!lift&&!land&&cmd->order!=TC_STAND_GROUND) return false;
    if (contextual) {
        bool handled=false;
        ticcmd_t remaining=*cmd;remaining.count=0;
        for (int i=0;i<count;i++) {
            mobj_t *u=units[i];
            bool cargo=(u->traits&MF_HARVESTER) && u->harvest.cargo>0 &&
                (target->traits&MF_RESOURCE_BASE) && target->owner==u->owner;
            bool fix=target->type_id!=MT_REFINERY && repairable(u,target);
            bool trail=target!=u&&P_IsAlly(u,target)&&(target->traits&MF_MOBILE)&&
                !sc_can_board(u,target);
            if (!cargo && !fix && !trail) {remaining.units[remaining.count++]=u->id;continue;}
            ticcmd_t next=*cmd;
            next.order=cargo?TC_RETURN_GOODS:fix?TC_REPAIR:TC_FOLLOW;
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
        if(lift||land) {
            const sc_unit_t *type=sc_unit(u);
            if(!type||!(type->flags&SC_UNIT_FLYING_BUILDING)||u->production||(u->sc.flags&SC_HALLUCINATION))continue;
            if(lift) {
                if(u->traits&MF_FLY)continue;
                stop(player,u);
                mobj_t *addon=sc_addon_of(u);
                if(addon) {
                    sc_interrupt(addon);
                    if(addon->production)sc_cancel_production(addon,-1);
                    addon->sc.parent=0;addon->owner=addon->team=11;
                    addon->allegiance=ALLEGIANCE_NEUTRAL;
                    P_MobjSetSelected(addon,false);
                }
                u->sc.addon=0;
                u->traits=(u->traits&~MF_RESOURCE_BASE)|MF_FLY|MF_MOBILE;
                u->speed=SC_PIXELS(24); /* OpenBW leaves buildings at one pixel/frame. */
                u->sc.order=(sc_order_t){.kind=SC_ORDER_LIFT};
                int state=sc_flight_states[u->type_id-1][0];
                if(state)P_SetMobjState(u,state);
                P_SyncBuildingBlocking();
            }else {
                if(!(u->traits&MF_FLY)||!P_CanPlaceBuilding(u->type_id,at,u))continue;
                stop(player,u);
                u->move_only=true;
                u->sc.order=(sc_order_t){.kind=SC_ORDER_LAND,.at=at};
                P_MoveUnitTo(&level,u,P_BuildingPosition(u->type_id,at));
            }
            continue;
        }
        if (!(u->traits&MF_MOBILE) || (u->sc.flags&SC_LOADED) ||
            u->sc.timers[SC_TIMER_LOCKDOWN]>0 || (u->production&&u->production->placed)) continue;
        if (repair && !repairable(u,target)) continue;
        if (follow&&(!target||target==u||!P_IsAlly(u,target)||(target->sc.flags&SC_LOADED)))continue;
        if (returning && (!(u->traits&MF_HARVESTER) || u->harvest.cargo<=0)) continue;
        int resource=u->harvest.target;
        stop(player,u);
        if (repair) u->sc.order=(sc_order_t){.kind=SC_ORDER_REPAIR,.target=target->id};
        else if(follow) {
            u->sc.order=(sc_order_t){.kind=SC_ORDER_FOLLOW,.target=target->id};
            u->move_only=true;
        }
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
    int distance=sc_unit_distance(worker,target);
    if (distance>5) {
        /* The coarse route gets us beside the foundation. Finish the short
         * approach against native collision bounds, not placement tiles. */
        if(!P_HasMoveOrder(worker)) {
            fixed2_t delta=fixed2_sub(at,from);
            fixed_t length=fixed2_length(delta), step=FIXED_STEP_PER_TIC(worker->speed);
            if(length>step)delta=fixed2_rescale(delta,length,step);
            fixed2_t next=fixed2_add(from,delta);
            if(repair_step_clear(&level,worker,next)) {
                fixed3_t position=fixed3_from_fixed2(next,worker->core.position.z);
                worker->core.momentum=fixed3_planar_displacement(worker->core.position,position);
                worker->core.position=position;
                worker->core.angle=P_PointToAngle(delta.x,delta.y);
                if(states[worker->core.state_id].group!=2)P_SetMobjState(worker,mobjinfo[worker->type_id].seestate);
                return;
            }
        }
        if ((u->flags&SC_UNIT_BUILDING)&&!(target->traits&MF_FLY)) {
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

void sc_order_ticker(mobj_t *mo, int frames) {
    sc_order_t *o=&mo->sc.order;
    if(mo->sc.timers[SC_TIMER_LOCKDOWN]>0)return;
    if(o->kind==SC_ORDER_FOLLOW) {
        mobj_t *target=P_MobjById(o->target);
        if(!target||target->remove||target->hp<=0||!P_IsAlly(mo,target)||(target->sc.flags&SC_LOADED)) {
            stop(mo->owner,mo);return;
        }
        if(o->time>0){o->time-=frames;return;}
        o->time=7;
        if(sc_unit_distance(mo,target)<=96) {
            P_ClearMove(mo);
            if(!(target->traits&MF_MOBILE))stop(mo->owner,mo);
        }else {
            fixed2_t at=fixed3_xy(target->core.position);
            if(!(target->traits&(MF_MOBILE|MF_FLY))) {
                irect_t box=P_MobjCells(target);
                if(!P_ApproachFootprint(mo,(ivec2_t){box.x,box.y},(isize2_t){box.w,box.h},&at))return;
            }
            if(!P_HasMoveOrder(mo)||!fixed2_equal(at,mo->movement.goal))P_MoveUnitTo(&level,mo,at);
        }
    }else if(o->kind==SC_ORDER_LIFT) {
        mo->core.position.z=fixed_min(SC_PIXELS(42),mo->core.position.z+SC_PIXELS(frames));
        if(mo->core.position.z==SC_PIXELS(42)) {
            *o=(sc_order_t){0};
            P_SetMobjState(mo,mobjinfo[mo->type_id].spawnstate);
        }
    }else if(o->kind==SC_ORDER_LAND) {
        if(!mo->movement.order_arrived)return;
        if(!P_CanPlaceBuilding(mo->type_id,o->at,mo)) {
            mo->core.position.z=SC_PIXELS(42);
            *o=(sc_order_t){0};
            stop(mo->owner,mo);return;
        }
        if(!o->time) {
            o->time=1;
            int state=sc_flight_states[mo->type_id-1][1];
            if(state)P_SetMobjState(mo,state);
        }
        mo->core.position.z=fixed_max(0,mo->core.position.z-SC_PIXELS(frames));
        if(mo->core.position.z)return;
        mo->traits=(mo->traits&~(MF_MOBILE|MF_FLY))|(mo->info->traits&MF_RESOURCE_BASE);
        mo->speed=0;
        /* Reconnect a compatible abandoned add-on at its native offset. */
        for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
            mobj_t *addon=(mobj_t *)th;
            if(th->function!=P_MobjThinker||addon->remove||addon->hp<=0||addon->sc.parent||
               (addon->owner!=11&&addon->owner!=mo->owner)||sc_addon_parent(addon->type_id)!=mo->type_id)continue;
            ivec2_t offset=sc_unit(addon)->addon;
            ivec2_t cell=ivec2_add(o->at,(ivec2_t){offset.x/32,offset.y/32});
            if(!fixed2_equal(P_BuildingPosition(addon->type_id,cell),fixed3_xy(addon->core.position)))continue;
            addon->sc.parent=mo->id;mo->sc.addon=addon->id;
            addon->owner=mo->owner;addon->team=mo->team;addon->allegiance=mo->allegiance;break;
        }
        *o=(sc_order_t){0};
        stop(mo->owner,mo);
        P_SetMobjState(mo,mobjinfo[mo->type_id].spawnstate);
        P_SyncBuildingBlocking();
    }
}
