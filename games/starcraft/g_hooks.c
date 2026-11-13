#include "sc_local.h"

/* Research restrictions, projectiles, construction progress, transport timers
 * and mission presentation are saved. */
uint32_t G_SaveLayout(void) { return 22; }

const uint32_t *G_AllianceMasks(const level_t *map) {
    if (!map) return NULL;
    const sc_mission_t *m = map->mission;
    return m ? m->allies : map->sight.allies;
}

uint32_t G_ConsistencyExtra(uint32_t hash) {
    const sc_mission_t *m = level.mission;
    if (m) hash = G_HashValue(hash, m->rng);
    if (m) hash = G_HashValue(hash, m->map_mode);
    if (m) hash = G_HashValue(hash, m->research_rules_loaded);
    if (m) hash = G_HashValue(hash, m->custom_slots_set);
    if (m) {
        hash = G_HashValue(hash, m->loc_count);
        for (int i = 0; i < m->loc_count; ++i) {
            hash = G_HashValue(hash, m->loc_left[i]);
            hash = G_HashValue(hash, m->loc_top[i]);
            hash = G_HashValue(hash, m->loc_right[i]);
            hash = G_HashValue(hash, m->loc_bottom[i]);
            hash = G_HashValue(hash, m->loc_elevation[i]);
        }
    }
    if (m) for (int i = 0; i < 8; ++i) {
        hash = G_HashValue(hash, m->custom_slot_kind[i]);
        hash = G_HashValue(hash, m->custom_slot_race[i]);
    }
    if (m) { hash = G_HashValue(hash, m->previous_bullet_source); hash = G_HashValue(hash, m->previous_bullet_clockwise); }
    if (m) {
        const sc_leaderboard_t *lb = &m->leaderboard;
        hash = G_HashValue(hash, lb->kind); hash = G_HashValue(hash, lb->goal);
        hash = G_HashValue(hash, lb->selector); hash = G_HashValue(hash, lb->unit);
        hash = G_HashValue(hash, lb->title); hash = G_HashValue(hash, lb->location);
        hash = G_HashValue(hash, lb->quantity);
        hash = G_HashValue(hash, m->leaderboard_computers);
        hash = G_HashValue(hash, m->portrait_unit); hash = G_HashValue(hash, m->portrait_ms);
        for (int i = 0; i < 8; ++i) {
            hash = G_HashValue(hash, (uint32_t)lb->value[i]);
            hash = G_HashValue(hash, (uint32_t)((uint64_t)lb->value[i] >> 32));
            hash = G_HashValue(hash, m->ai_script[i].id);
            hash = G_HashValue(hash, m->ai_script[i].location);
            hash = G_HashValue(hash, m->ai_script[i].runs);
        }
    }
    if (m) for (int i = 0; i < 8; ++i) {
        hash = G_HashValue(hash, m->allies[i]);
        hash = G_HashValue(hash, m->allied_victory[i]);
        hash = G_HashValue(hash, m->neutral_relations[i]);
        hash = G_HashValue(hash, m->player_result[i]);
        hash = G_HashValue(hash, m->custom_score[i]);
        for (int t = 0; t < 7; t++) hash = G_HashValue(hash, m->score_variables[i][t]);
        for (int u = 0; u < SC_UPGRADES; u++) hash = G_HashValue(hash, m->upgrade_max_level[i][u]);
        for (int t = 0; t < SC_TECHS; t++) hash = G_HashValue(hash, m->tech_available[i][t]);
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
    if (cmd->order == TC_ALLY) m->neutral_relations[player] &= ~bit;
    if (cmd->order == TC_ALLY && !cmd->product) m->allied_victory[player] &= ~bit;
    if (cmd->order == TC_SHARE_SIGHT) P_UpdateSight();
    return true;
}

bool G_TakeCameraRequest(fvec2_t *cell) { return sc_take_camera(cell); }

void G_ProductionBegin(int elapsed_ms) { sc_zerg_ticker(elapsed_ms); }

bool G_QueueLocksProduct(const StaticProductDefinition *product) {
    return product->product_class!=RTS_PRODUCT_UNIT;
}

void G_UnitProduced(mobj_t *unit,const mobj_t *producer) {
    if (unit && producer) unit->sc.sprite_owner = producer->owner;
    if(!unit||!producer||!(unit->traits&MF_MOBILE)||!producer->sc.rally.set)return;
    mobj_t *target=P_MobjById(producer->sc.rally.target);
    fixed2_t point=target&&target->hp>0&&!target->remove?
        fixed3_xy(target->core.position):producer->sc.rally.at;
    P_MoveUnitTo(&level,unit,point);
}

void G_UnitKilled(mobj_t *victim,mobj_t *killer) {
    if(victim) {
        production_t *production=victim->production;
        bool construction_site=production&&production->placed&&
            production->product_class==RTS_PRODUCT_BUILDING&&production->actor_id==victim->type_id;
        if(construction_site) {
            mobj_t *builder=victim->target;
            if(builder&&builder->target==victim) {
                builder->target=NULL;
                builder->sc.queue=(sc_order_queue_t){0};
            }
            victim->target=NULL;
        }
        if(victim->sc.queue.active==TC_CONSTRUCT&&victim->sc.queue.target) {
            mobj_t *site=P_MobjById(victim->sc.queue.target);
            if(site&&site->target==victim)site->target=NULL;
        }
        victim->sc.queue=(sc_order_queue_t){0};
    }
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
    bool replaces=cmd->order==TC_STOP||cmd->order==TC_MOVE||cmd->order==TC_ATTACK||
        cmd->order==TC_ORDER||cmd->order==TC_PATH||
        cmd->order==TC_WAYPOINT||cmd->order==TC_REPAIR||cmd->order==TC_FOLLOW||
        cmd->order==TC_RETURN_GOODS||cmd->order==TC_BOARD;
    if(cmd->order==TC_HARVEST&&late)replaces=true;
    if(replaces&&(!cmd->queued||(late&&cmd->order==TC_HARVEST)))
        for(int i=0;i<count;i++) {
            sc_order_queue_t accepted_queue=units[i]->sc.queue;
            sc_cancel_pending_construction(units[i],late&&cmd->order==TC_HARVEST);
            if(late&&cmd->order==TC_HARVEST) {
                if(!cmd->queued) {
                    accepted_queue.count=0;
                    memset(accepted_queue.pending,0,sizeof(accepted_queue.pending));
                }
                accepted_queue.active=TC_HARVEST;
                accepted_queue.owner=units[i]->owner;
                accepted_queue.target=0;
                accepted_queue.target_owner=0;
                accepted_queue.harvest_phase=units[i]->harvest.phase;
                accepted_queue.forced=false;
                units[i]->sc.queue=accepted_queue;
            }
        }
    if ((!late && (cmd->order == TC_STOP || cmd->order == TC_MOVE || cmd->order == TC_ATTACK || cmd->order == TC_ORDER ||
        cmd->order == TC_PATH || cmd->order == TC_WAYPOINT)) || (late&&cmd->order==TC_HARVEST))
        for (int i = 0; i < count; ++i) {
            sc_interrupt(units[i]);
            units[i]->move_only=false;
        }
}
