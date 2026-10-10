#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

/* Wargus repairs once per complete worker animation, using the TARGET's
 * RepairHp/RepairCosts. The worker pays, including when helping an ally. */
int W2_Distance(const mobj_t *a, const mobj_t *b) {
    irect_t ac = P_MobjCells(a), bc = P_MobjCells(b);
    int dx = ac.x > bc.x ? ac.x - bc.x - bc.w + 1 : bc.x - ac.x - ac.w + 1;
    int dy = ac.y > bc.y ? ac.y - bc.y - bc.h + 1 : bc.y - ac.y - ac.h + 1;
    int distance = dx > dy ? dx : dy;
    return distance > 0 ? distance : 0;
}

static bool repairable(const mobj_t *worker, const mobj_t *target) {
    return worker && target && worker != target && worker->owner < 8 &&
        (worker->type_id == MT_PEASANT || worker->type_id == MT_PEON) &&
        worker->hp > 0 && !worker->remove && target->hp > 0 && !target->remove &&
        mobjinfo[worker->type_id].w2.repair.range > 0 &&
        mobjinfo[target->type_id].w2.repair.hp > 0 &&
        target->hp < target->max_hp && P_IsAlly(worker, target) &&
        worker->w2.build_phase != W2_BUILD_WORKING;
}

void W2_InterruptRepair(mobj_t *worker) {
    if (!worker || !worker->w2.repair.target) return;
    worker->w2.repair = (w2_repair_t){0};
    P_ClearMove(worker);
    P_SetMobjState(worker, mobjinfo[worker->type_id].spawnstate);
}

static bool approach(mobj_t *worker, const mobj_t *target) {
    isize2_t foot = mobjinfo[target->type_id].w2.footprint;
    ivec2_t cell = fixed2_foot_origin_cell(fixed3_xy(target->core.position), foot);
    fixed2_t bay;
    return P_ApproachFootprint(worker, cell, foot, &bay) && P_MoveUnitTo(&level, worker, bay);
}

bool W2_RepairOrder(mobj_t *worker, mobj_t *target) {
    if (!repairable(worker, target)) return false;
    W2_InterruptBuild(worker);
    W2_InterruptHarvest(worker);
    W2_InterruptRepair(worker);
    worker->w2.carrier = 0;
    worker->harvest.phase = HARVEST_PHASE_NONE;
    worker->harvest.target = -1;
    worker->harvest.base = NULL;
    worker->attack.target = NULL;
    worker->waypoints = (waypoints_t){0};
    worker->w2.stand_ground = false;
    P_ClearMove(worker);
    if (W2_Distance(worker, target) > mobjinfo[worker->type_id].w2.repair.range &&
        !approach(worker, target)) return false;
    worker->w2.repair.target = target->id;
    return true;
}

void A_W2_Repair(mobj_t *worker) {
    mobj_t *target = P_MobjById(worker->w2.repair.target);
    if (!repairable(worker, target) ||
        W2_Distance(worker, target) > mobjinfo[worker->type_id].w2.repair.range) return;
    if (W2_UnderConstruction(target)) {
        /* action_repair.cpp: ProgressHp(100 * RepairCycle); the default
         * ResourcesMultiBuildersMultiplier is zero, so assistance is free. */
        w2_advance_build(target, worker->w2.repair.tics);
        worker->w2.repair.tics = 0;
        return;
    }
    worker->w2.repair.tics = 0;
    const w2_stats_t *stats = &mobjinfo[target->type_id].w2;
    int *stock = level.player_resources[worker->owner];
    for (int r = 0; r < 3; ++r)
        if (stock[r] < stats->repair.costs[r]) { W2_InterruptRepair(worker); return; }
    for (int r = 0; r < 3; ++r) stock[r] -= stats->repair.costs[r];
    target->hp += stats->repair.hp;
    if (target->hp > target->max_hp) target->hp = target->max_hp;
    S_ActorSound(worker, SE_ATTACK);
}

bool W2_TickRepair(mobj_t *worker) {
    if (!worker->w2.repair.target) return false;
    mobj_t *target = P_MobjById(worker->w2.repair.target);
    if (!repairable(worker, target)) { W2_InterruptRepair(worker); return false; }
    if (W2_Distance(worker, target) > mobjinfo[worker->type_id].w2.repair.range) {
        worker->w2.repair.tics = 0;
        if (!P_HasMoveOrder(worker) && !approach(worker, target)) W2_InterruptRepair(worker);
        return false;
    }
    P_ClearMove(worker);
    ++worker->w2.repair.tics;
    fixed2_t facing = fixed2_sub(fixed3_xy(target->core.position),
                               fixed3_xy(worker->core.position));
    worker->core.angle = P_PointToAngle(facing.x, facing.y);
    if (states[worker->core.state_id].group != W2_GROUP_WORK)
        P_SetMobjState(worker, W2_REPAIR_STATE(worker->type_id - 1));
    return true;
}
