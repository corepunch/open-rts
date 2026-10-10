#include "engine.h"
#include "warcraft-2.h"

/* Warcraft II's share of the engine's game hooks: orders only it has, the
 * state it adds to the lockstep checksum, and how it interrupts a unit. */

uint32_t G_ConsistencyExtra(uint32_t hash) {
    hash = G_HashValue(hash, W2_CombatState());
    for (int p = 0; p < 8; ++p) {
        hash = G_HashValue(hash, (uint32_t)level.w2_research[p]);
        hash = G_HashValue(hash, (uint32_t)(level.w2_research[p] >> 32));
    }
    return hash;
}

bool G_UnitCommandable(const mobj_t *unit) { return !unit->w2.boarded; }

bool G_GameUnitCommand(int player, const ticcmd_t *cmd, mobj_t *const *units,
                       int count, mobj_t *target) {
    switch (cmd->order) {
    case TC_CANCEL_PRODUCTION:
        for (int i = 0; i < count; ++i) W2_CancelProduction(units[i]);
        return true;
    case TC_BOARD:
    case TC_UNLOAD:
        for (int i = 0; i < count; ++i) {
            if (cmd->order == TC_BOARD) W2_BoardOrder(units[i], target);
            else W2_UnloadOrder(units[i], fixed3_xy(cmd->position));
        }
        return true;
    case TC_SPELL:
        for (int i = 0; i < count; ++i) W2_CastOrder(units[i], cmd->product, target, cmd->position);
        return true;
    case TC_REPAIR:
        for (int i = 0; i < count; ++i) W2_RepairOrder(units[i], target);
        return true;
    case TC_STAND_GROUND: {
        ticcmd_t stop = *cmd;
        stop.order = TC_STOP;
        G_RunTiccmd(player, &stop);
        for (int i = 0; i < count; ++i) units[i]->w2.stand_ground = true;
        return true;
    }
    case TC_RETURN_GOODS:
        for (int i = 0; i < count; ++i) W2_ReturnGoods(units[i]);
        return true;
    case TC_CONSTRUCT: {
        if (cmd->product == 0) {
            for (int i = 0; i < count; ++i) W2_CancelConstruction(units[i]);
            return true;
        }
        ivec2_t cell = {cmd->position.x >> FIXED_FRAC_BITS, cmd->position.y >> FIXED_FRAC_BITS};
        if (cmd->product > 0 && cmd->product <= UINT16_MAX)
            W2_ConstructOrder(units[0], (uint16_t)cmd->product, cell);
        return true;
    }
    default:
        return false;
    }
}

void G_InterruptOrders(const ticcmd_t *cmd, mobj_t *const *units, int count, bool late) {
    bool interrupts = late ? cmd->order == TC_ORDER :
        cmd->order == TC_STOP || cmd->order == TC_MOVE || cmd->order == TC_ATTACK ||
        cmd->order == TC_PATH || cmd->order == TC_WAYPOINT;
    if (!interrupts) return;
    for (int i = 0; i < count; ++i) {
        W2_InterruptRepair(units[i]); W2_InterruptHarvest(units[i]); W2_InterruptBuild(units[i]);
        units[i]->w2.stand_ground = false;
        units[i]->w2.cast.spell = 0;
        if (!units[i]->w2.boarded) units[i]->w2.carrier = 0;
        units[i]->w2.unloading = false;
    }
}

/* A unit aboard a ship sees nothing; the rest see by their stats. */
bool G_SightRadius(const mobj_t *actor, int *radius) {
    if (actor->w2.boarded) return false;
    *radius = W2_SightRange(actor);
    return true;
}

bool G_ViewerSees(const mobj_t *target, int owner, int team) {
    (void)team;
    return owner >= 0 && W2_VisibleTo(target, owner);
}

bool G_GameHarvestAt(mobj_t *const *units, int count, fixed2_t position, bool *issued) {
    *issued = false;
    for (int i = 0; i < count; ++i) *issued = W2_HarvestOrder(units[i], position) || *issued;
    return true;
}
