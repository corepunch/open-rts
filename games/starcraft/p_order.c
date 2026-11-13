#include "sc_local.h"

int sc_flight_states[SC_TYPES][2];
static bool queue_done(const mobj_t *u);
static bool repairable(const mobj_t *worker, const mobj_t *target);
static bool queueable(int order) {
    return order==TC_ORDER || order==TC_MOVE || order==TC_ATTACK || order==TC_HARVEST ||
        order==TC_REPAIR || order==TC_FOLLOW || order==TC_RETURN_GOODS || order==TC_PATH || order==TC_BOARD ||
        order==TC_STOP || order==TC_STAND_GROUND;
}

static void queue_run(int player, const ticcmd_t *cmd) {
    ticcmd_t continuation=*cmd;
    continuation.internal=true;
    G_RunTiccmd(player,&continuation);
}

void sc_cancel_pending_construction(mobj_t *worker,bool preserve_move) {
    production_t *production=worker?worker->production:NULL;
    bool pending=production&&production->placed&&
        production->product_class==RTS_PRODUCT_BUILDING&&production->actor_id!=worker->type_id;
    mobj_t *site=worker?worker->target:NULL;
    bool attached=site&&!site->remove&&site->target==worker&&site->production&&
        site->production->placed&&site->production->product_class==RTS_PRODUCT_BUILDING&&
        site->production->actor_id==site->type_id;
    if(!pending&&!attached)return;
    sc_order_queue_t replacement=worker->sc.queue;
    bool has_replacement=replacement.active!=TC_CONSTRUCT||replacement.target!=0;
    navpath_t path=worker->movement.path;
    int stuck=worker->movement.stuck_tics,replans=worker->movement.replans;
    fixed_t best_goal=worker->movement.best_goal_dist,best_leg=worker->movement.best_leg_dist;
    bool plan_pending=worker->movement.plan_pending;
    if(pending)sc_cancel_production(worker,0);
    if(attached) {
        site->target=NULL;
        worker->target=NULL;
    }
    if(has_replacement)worker->sc.queue=replacement;
    if(preserve_move) {
        worker->movement.path=path;
        worker->movement.stuck_tics=stuck;
        worker->movement.replans=replans;
        worker->movement.best_goal_dist=best_goal;
        worker->movement.best_leg_dist=best_leg;
        worker->movement.plan_pending=plan_pending;
    }
}

static bool immediate_replacement_order(int order) {
    return order==TC_STOP||order==TC_MOVE||order==TC_ORDER||order==TC_ATTACK||
        order==TC_REPAIR||order==TC_PATH||order==TC_BOARD||
        order==TC_STAND_GROUND||order==TC_WAYPOINT;
}

bool sc_queue_order(int player, const ticcmd_t *cmd, mobj_t *const *units, int count) {
    if(cmd->internal)return false;
    if(cmd->order==TC_SPELL)return false; /* Accepted casts own cancellation. */
    /* A direct harvest command can fail (for example, when aimed at empty
     * ground). P_HarvestUnitsAt commits it per worker only after pathing and
     * resource validation succeed, so leave the existing queue untouched. */
    if(cmd->order==TC_HARVEST&&!cmd->queued)return false;
    if(cmd->order==TC_CONSTRUCT) {
        const StaticProductDefinition *product=G_ModelProductByUIId(NULL,cmd->product);
        if(cmd->product==0) {
            if(count)sc_cancel_production(units[0],-1);
            return true;
        }
        ivec2_t cell=fixed2_cell(fixed3_xy(cmd->position));
        if(count&&sc_resume_construction(units[0],product,cell))return true;
        if(count && G_PlaceProduct(units[0],product,cell))
            units[0]->sc.queue=(sc_order_queue_t){.active=TC_CONSTRUCT,.owner=player};
        return true;
    }
    bool affects=queueable(cmd->order) || cmd->order==TC_STOP || cmd->order==TC_STAND_GROUND ||
        cmd->order==TC_BOARD || cmd->order==TC_UNLOAD ||
        cmd->order==TC_CONSTRUCT || cmd->order==TC_LIFT || cmd->order==TC_LAND;
    if(!affects)return false;
    if((cmd->order==TC_MOVE || cmd->order==TC_ORDER || cmd->order==TC_HARVEST ||
        (cmd->order==TC_ATTACK && !cmd->target)) &&
       !L_Contains(&level,fixed_floor_int(cmd->position.x),fixed_floor_int(cmd->position.y)))return true;
    if(cmd->target && (cmd->order==TC_ATTACK || cmd->order==TC_REPAIR || cmd->order==TC_FOLLOW) &&
       !P_MobjById(cmd->target))return true;
    bool can_queue=(queueable(cmd->order) || (cmd->order==TC_UNLOAD && (cmd->position.x || cmd->position.y))) &&
        (cmd->order!=TC_PATH || cmd->path.count==1);
    ticcmd_t immediate=*cmd; immediate.queued=false; immediate.count=0;
    bool appended=false;
    for(int i=0;i<count;i++) {
        mobj_t *u=units[i]; sc_order_queue_t *q=&u->sc.queue;
        if(cmd->order==TC_REPAIR && !repairable(u,P_MobjById(cmd->target)))continue;
        if(cmd->order==TC_ATTACK && cmd->target && !P_CanTarget(u,P_MobjById(cmd->target)))continue;
        if(cmd->order==TC_LIFT || cmd->order==TC_LAND) {
            const sc_unit_t *type=sc_unit(u);
            if(!type || !(type->flags&SC_UNIT_FLYING_BUILDING) || u->production ||
               (u->sc.flags&SC_HALLUCINATION))continue;
            if(cmd->order==TC_LIFT && (u->traits&MF_FLY))continue;
            if(cmd->order==TC_LAND && (!(u->traits&MF_FLY) ||
               !P_CanPlaceBuilding(u->type_id,fixed2_cell(fixed3_xy(cmd->position)),u)))continue;
        }
        if(cmd->order==TC_BOARD) {
            mobj_t *target=P_MobjById(cmd->target);
            if(!target || (!sc_can_board(u,target) && !sc_can_board(target,u)))continue;
        }
        if(q->count<0 || q->count>SC_QUEUED_ORDERS)*q=(sc_order_queue_t){0};
        bool patrol=q->active==TC_PATH && u->waypoints.mode==WP_BACKTRACK;
        if(cmd->queued && can_queue && q->active && !patrol && !queue_done(u)) {
            if(q->count<SC_QUEUED_ORDERS) {
                sc_pending_order_t *p=&q->pending[q->count++];
                *p=(sc_pending_order_t){.order=cmd->order,.product=cmd->product,.position=cmd->position,
                    .target=cmd->target,.point=cmd->path.points[0],.mode=cmd->path.mode};
            }
            appended=true; continue;
        }
        if(!cmd->queued&&immediate_replacement_order(cmd->order))
            sc_cancel_pending_construction(u,false);
        *q=(sc_order_queue_t){0};
        immediate.units[immediate.count++]=u->id;
        if(cmd->order!=TC_STOP){q->active=cmd->order;q->owner=player;q->target=cmd->target;q->harvest_phase=u->harvest.phase;
            mobj_t *target=P_MobjById(cmd->target);if(target)q->target_owner=target->owner;}
    }
    if(!appended)return false;
    if(immediate.count)queue_run(player,&immediate);
    return true;
}

static bool queue_done(const mobj_t *u) {
    const sc_order_queue_t *q=&u->sc.queue;
    if(!q->active)return true;
    if(q->active==TC_CONSTRUCT&&q->target) {
        const mobj_t *site=P_MobjById(q->target);
        if(site&&!site->remove&&site->hp>0&&site->owner==u->owner&&site->target==u&&
           site->production&&site->production->placed&&
           site->production->product_class==RTS_PRODUCT_BUILDING&&
           site->production->actor_id==site->type_id)return false;
    }
    if(q->active==TC_STAND_GROUND && (u->sc.flags&SC_HOLD))return false;
    if((q->active==TC_ATTACK || q->active==TC_ORDER) && q->target) {
        mobj_t *target=P_MobjById(q->target);
        if(!target || target->remove || target->hp<=0)return true;
        if(q->active==TC_ATTACK)return (q->target_owner!=target->owner && P_IsAlly(u,target)) || !P_CanTarget(u,target);
        if(!P_IsAlly(u,target))return false;
    }
    if(u->sc.order.kind || (u->production && u->production->placed) || u->harvest.phase!=0)return false;
    if(u->waypoints.count)return false;
    if(u->attack.target && !u->attack.target->remove && u->attack.target->hp>0)return false;
    return u->movement.order_arrived || (!P_HasMoveOrder(u) && !u->movement.plan_pending);
}

static void queue_tick(mobj_t *u) {
    sc_order_queue_t *q=&u->sc.queue;
    if(q->active && q->owner!=u->owner){*q=(sc_order_queue_t){0};return;}
    if(!G_UnitCommandable(u))return;
    if(q->count>0 && q->harvest_phase==HARVEST_PHASE_MINING &&
       u->harvest.phase==HARVEST_PHASE_TO_BASE && u->harvest.resource_type==0) {
        q->count=0;
        memset(q->pending,0,sizeof(q->pending));
    }
    /* Gas workers finish the current extraction before accepting a pending
     * player order; OpenBW HarvestGas checks the queue on exit. */
    if(q->count>0 && q->harvest_phase==HARVEST_PHASE_MINING &&
       u->harvest.phase==HARVEST_PHASE_TO_BASE && u->harvest.cargo>0 &&
       u->harvest.resource_type==1)q->active=0;
    q->harvest_phase=u->harvest.phase;
    if(q->count<=0 || q->count>SC_QUEUED_ORDERS || !queue_done(u))return;
    sc_pending_order_t p=q->pending[0];
    memmove(q->pending,q->pending+1,(size_t)(--q->count)*sizeof(*q->pending));
    q->pending[q->count]=(sc_pending_order_t){0}; q->active=0;
    if(p.target && !P_MobjById(p.target))return;
    ticcmd_t cmd={.order=p.order,.product=p.product,.position=p.position,.target=p.target,
                  .count=1,.units={u->id}};
    if(p.order==TC_PATH)cmd.path=(waypoints_t){.count=1,.mode=p.mode,.points={p.point}};
    mobj_t *target=P_MobjById(p.target);
    q->active=p.order; q->target=p.target;
    q->target_owner=target?target->owner:0;
    q->forced=p.order==TC_ATTACK && target && P_IsAlly(u,target);
    queue_run(u->owner,&cmd);
}

static ivec2_t pixels(fixed2_t at) {
    return (ivec2_t){at.x / SC_PIXELS(1), at.y / SC_PIXELS(1)};
}
static irect_t collision_box(const mobj_t *unit, fixed2_t at) {
    irect_t box=sc_unit(unit)->bounds;
    ivec2_t origin=ivec2_add(pixels(at),(ivec2_t){box.x,box.y});
    return (irect_t){origin.x,origin.y,box.w,box.h};
}
static int distance_xy(int dx,int dy) {
    if(dx<dy){int swap=dx;dx=dy;dy=swap;}
    return dx/4<dy?dx-dx/16+dy*3/8-dx/64+dy*3/256:dx;
}
int sc_point_distance(const mobj_t *unit, fixed2_t at) {
    irect_t box=collision_box(unit,fixed3_xy(unit->core.position));
    ivec2_t point=pixels(at);
    int dx=SDL_max(0,SDL_max(box.x-point.x,point.x-box.x-box.w));
    int dy=SDL_max(0,SDL_max(box.y-point.y,point.y-box.y-box.h));
    return distance_xy(dx,dy);
}
int sc_unit_distance(const mobj_t *a, const mobj_t *b) {
    irect_t x=collision_box(a,fixed3_xy(a->core.position));
    irect_t y=collision_box(b,fixed3_xy(b->core.position));
    int dx=SDL_max(0,SDL_max(x.x-y.x-y.w,y.x-x.x-x.w));
    int dy=SDL_max(0,SDL_max(x.y-y.y-y.h,y.y-x.y-x.h));
    return distance_xy(dx,dy);
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
        G_ModelUnitReady(target) && target->sc.timers[SC_TIMER_STASIS]==0 &&
        (target->hp<target->max_hp || target->sc.wound) &&
        (u->race&2) && (u->flags&SC_UNIT_MECHANICAL) &&
        !(target->sc.flags&(SC_LOADED|SC_HALLUCINATION)) &&
        !(worker->sc.flags&(SC_LOADED|SC_HALLUCINATION)) &&
        !(worker->production&&worker->production->placed&&
          worker->production->product_class==RTS_PRODUCT_BUILDING&&
          worker->production->actor_id==worker->type_id);
}

/* Use the normal Stop command so orders share one cancellation lifecycle. */
static void stop(int player, mobj_t *unit) {
    ticcmd_t cmd={.order=TC_STOP,.count=1,.units={unit->id}};
    queue_run(player,&cmd);
}

void sc_finish_recharge_order(mobj_t *unit, uint32_t battery_id) {
    if (!unit || unit->sc.order.kind != SC_ORDER_FOLLOW || unit->sc.order.target != battery_id) return;
    sc_order_queue_t *queue = &unit->sc.queue;
    if (queue->count > 0 && queue->count <= SC_QUEUED_ORDERS) {
        queue->active = 0;
        queue->target = 0;
        queue->target_owner = 0;
        queue->forced = false;
    } else {
        *queue = (sc_order_queue_t){0};
    }
    ticcmd_t cmd = {.order=TC_STOP,.count=1,.units={unit->id},.internal=true};
    G_RunTiccmd(unit->owner,&cmd);
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
            bool construction=target->owner==u->owner&&u->type_id==MT_SCV&&target->production&&
                target->production->placed&&target->production->product_class==RTS_PRODUCT_BUILDING&&
                target->production->actor_id==target->type_id;
            if(construction) {
                const StaticProductDefinition *product=G_ModelProductByClassType(NULL,
                    target->production->product_class,target->production->product_type);
                if(product&&sc_resume_construction(u,product,target->production->cell)) {
                    handled=true;
                    continue;
                }
            }
            bool cargo=(u->traits&MF_HARVESTER) && u->harvest.cargo>0 &&
                (target->traits&MF_RESOURCE_BASE) && target->owner==u->owner;
            bool fix=target->type_id!=MT_REFINERY && repairable(u,target);
            bool trail=target!=u&&P_IsAlly(u,target)&&
                (((target->traits&MF_MOBILE)&&!sc_can_board(u,target)&&
                  !(sc_unit(u)->space_provided&&sc_can_board(target,u)))||
                 sc_can_recharge_shields(u,target));
            if (!cargo && !fix && !trail) {remaining.units[remaining.count++]=u->id;continue;}
            sc_cancel_pending_construction(u,false);
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
                sc_detach_addon(u);
                u->traits=(u->traits&~MF_RESOURCE_BASE)|MF_FLY|MF_MOBILE;
                u->speed=SC_PIXELS(24); /* OpenBW leaves buildings at one pixel/frame. */
                u->sc.order=(sc_order_t){.kind=SC_ORDER_LIFT};
                int state=sc_flight_states[u->type_id-1][0];
                if(state)P_SetMobjState(u,state);
                sc_script_begin(u,sc_unit_script[u->type_id-1],SC_ANIM_LIFT_OFF);
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
            u->sc.timers[SC_TIMER_LOCKDOWN]>0 ||
            (u->production&&u->production->placed&&u->production->actor_id==u->type_id)) continue;
        if (repair && !repairable(u,target)) continue;
        if (follow&&(!target||target==u||!P_IsAlly(u,target)||(target->sc.flags&SC_LOADED)))continue;
        if (returning && (!(u->traits&MF_HARVESTER) || u->harvest.cargo<=0)) continue;
        sc_cancel_pending_construction(u,false);
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
        if (hp==maxhp) {
            stop(worker->owner,worker);
            P_SetMobjState(worker,mobjinfo[worker->type_id].spawnstate);
            return;
        }
    }
}

void sc_order_ticker(mobj_t *mo, int frames) {
    queue_tick(mo);
    sc_order_t *o=&mo->sc.order;
    if(mo->sc.timers[SC_TIMER_LOCKDOWN]>0)return;
    if(o->kind==SC_ORDER_FOLLOW) {
        mobj_t *target=P_MobjById(o->target);
        if(!target||target->remove||target->hp<=0||!P_IsAlly(mo,target)||(target->sc.flags&SC_LOADED)) {
            stop(mo->owner,mo);return;
        }
        if(o->time>0){o->time-=frames;return;}
        o->time=7;
        bool recharge=sc_can_recharge_shields(mo,target);
        int reach=recharge?128:96;
        if(sc_unit_distance(mo,target)<=reach) {
            P_ClearMove(mo);
            if(!(target->traits&MF_MOBILE)&&!recharge)stop(mo->owner,mo);
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
            sc_script_begin(mo,sc_unit_script[mo->type_id-1],SC_ANIM_LANDING);
        }
        mo->core.position.z=fixed_max(0,mo->core.position.z-SC_PIXELS(frames));
        if(mo->core.position.z)return;
        mo->traits=(mo->traits&~(MF_MOBILE|MF_FLY))|(mo->info->traits&MF_RESOURCE_BASE);
        mo->speed=0;
        sc_reconnect_addon(mo, o->at);
        *o=(sc_order_t){0};
        stop(mo->owner,mo);
        P_SetMobjState(mo,mobjinfo[mo->type_id].spawnstate);
        P_SyncBuildingBlocking();
    }
}
