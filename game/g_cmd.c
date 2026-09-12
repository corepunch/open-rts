#include "d_net.h"
#include "game.h"
#include "p_local.h"

enum { MAXPENDINGCOMMANDS = 64 };
static ticcmd_t pending[MAXPENDINGCOMMANDS];
static unsigned commandhead, commandcount;

void G_ClearTiccmds(void) {
    commandhead = commandcount = 0;
}

bool G_QueueTiccmd(const ticcmd_t *cmd) {
    if (!cmd || cmd->count > MAXCOMMANDUNITS || (unsigned)cmd->order > TC_BUILD) return false;
    if (!netactive) { G_RunTiccmd(consoleplayer, cmd); return true; }
    if (commandcount == MAXPENDINGCOMMANDS) {
        fprintf(stderr, "Order queue full; order was not accepted.\n");
        return false;
    }
    pending[(commandhead + commandcount++) % MAXPENDINGCOMMANDS] = *cmd;
    return true;
}

void G_BuildTiccmd(ticcmd_t *cmd) {
    *cmd = (ticcmd_t){0};
    if (commandcount) {
        *cmd = pending[commandhead];
        commandhead = (commandhead + 1) % MAXPENDINGCOMMANDS;
        --commandcount;
    }
}

bool G_SelectedTiccmd(ticorder_t order, mobj_t *const *units, int count,
                     fvec2_t position, uint32_t target) {
    ticcmd_t cmd = { .order = order, .position = fixed3_from_fvec2(position, 0), .target = target };
    for (int i = 0; i < count; ++i) {
        const mobj_t *unit = units[i];
        if (!P_MobjIsSelected(unit) || unit->owner != consoleplayer || unit->remove || unit->hp <= 0) continue;
        if (cmd.count == MAXCOMMANDUNITS) {
            fprintf(stderr, "Too many units in one order (maximum %d).\n", MAXCOMMANDUNITS);
            return false;
        }
        cmd.units[cmd.count++] = unit->id;
    }
    return cmd.count && G_QueueTiccmd(&cmd);
}

bool G_BuildOrder(mobj_t *producer, int product) {
    if (!producer || producer->owner != consoleplayer) return false;
    ticcmd_t cmd = { .order = TC_BUILD, .product = product, .count = 1, .units = { producer->id } };
    return G_QueueTiccmd(&cmd);
}

void G_RunTiccmd(int player, const ticcmd_t *cmd) {
    if (!cmd || cmd->order == TC_NONE || cmd->count > MAXCOMMANDUNITS ||
        player < 0 || player >= RTS_MODEL_MAX_PLAYERS) return;
    mobj_t *units[MAXCOMMANDUNITS], *target = NULL;
    int count = 0;
    /* Resolve in thinker order, never in UI selection order or by array index. */
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        if (unit->remove || unit->hp <= 0) continue;
        if (unit->id == cmd->target) target = unit;
        if (unit->owner != player) continue;
        for (unsigned i = 0; i < cmd->count; ++i) {
            if (unit->id != cmd->units[i]) continue;
            units[count++] = unit;
            break;
        }
    }
    if (!count) return;
    fvec2_t goal = fixed3_xy_to_fvec2(cmd->position);
    if (cmd->order == TC_BUILD) {
        const StaticProductDefinition *product = G_ModelProductByUIId(NULL, cmd->product);
        G_PlayerBuildProduct(units[0], product);
        return;
    }
    if (cmd->order == TC_STOP) {
        for (int i = 0; i < count; ++i) {
            mobj_t *unit = units[i];
            unit->movement.flow_field = NULL;
            unit->movement.goal = fixed3_xy_to_fvec2(unit->core.position);
            unit->movement.order_id = 0;
            unit->movement.order_arrived = true;
            unit->attack.target = NULL;
            unit->harvest.target = -1;
            unit->harvest.phase = unit->harvest.timer_ms = 0;
            unit->core.momentum = fixed3_zero();
        }
        return;
    }
    if (cmd->order == TC_ATTACK) {
        if (!target || target->owner == player) return;
        goal = fixed3_xy_to_fvec2(target->core.position);
    } else {
        target = NULL;
        if (cmd->order == TC_HARVEST || cmd->order == TC_ORDER) {
            bool harvested = P_HarvestUnitsAt(&level, units, count, goal);
            if (harvested || cmd->order == TC_HARVEST) return;
        }
    }
    if (!L_Contains(&level, cmd->position.x >> FIXED_FRAC_BITS,
                   cmd->position.y >> FIXED_FRAC_BITS) && cmd->order != TC_ATTACK) return;
    for (int i = 0; i < count; ++i) {
        units[i]->attack.target = (units[i]->traits & MF_ATTACK) ? target : NULL;
        units[i]->harvest.target = -1;
        units[i]->harvest.timer_ms = 0;
    }
    P_MoveUnitsAt(&level, units, count, goal);
}

static uint32_t hash_value(uint32_t hash, uint32_t value) {
    for (int i = 0; i < 4; ++i) { hash = (hash ^ (value & 255)) * UINT32_C(16777619); value >>= 8; }
    return hash;
}

/* Doom samples mo->x or rndindex. Include every mobj and the RTS economy;
 * local selection, fog exploration, render caches and pointers are excluded. */
uint32_t G_Consistency(void) {
    uint32_t hash = UINT32_C(2166136261);
#define HASH(v) hash = hash_value(hash, (uint32_t)(v))
    HASH(leveltime); HASH(level.random_index); HASH(level.next_mobj_id); HASH(level.next_move_order_id);
    for (int p = 0; p < RTS_MODEL_MAX_PLAYERS; ++p)
        for (int r = 0; r < RTS_MAX_RESOURCES; ++r) HASH(level.player_resources[p][r]);
    for (int i = 0; i < level.resource_vent_count; ++i) {
        HASH(level.resource_vents[i].amount); HASH(level.resource_vents[i].active);
    }
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *u = (mobj_t *)th;
        HASH(u->id); HASH(u->type_id); HASH(u->owner); HASH(u->team); HASH(u->allegiance);
        HASH(u->core.position.x); HASH(u->core.position.y); HASH(u->core.position.z);
        HASH(u->core.momentum.x); HASH(u->core.momentum.y); HASH(u->core.momentum.z);
        HASH(u->core.angle); HASH(u->core.state_id); HASH(u->core.tics);
        HASH(u->hp); HASH(u->traits & ~MF_SELECTED); HASH(u->remove);
        HASH(u->target ? u->target->id : 0);
        HASH(u->attack.target ? u->attack.target->id : 0); HASH(u->attack.cooldown_left_ms);
        HASH(u->harvest.target); HASH(u->harvest.phase); HASH(u->harvest.timer_ms); HASH(u->harvest.cargo);
        fixed3_t goal = fixed3_from_fvec2(u->movement.goal, 0);
        HASH(goal.x); HASH(goal.y); HASH(u->movement.order_id); HASH(u->movement.order_arrived);
        if (u->production) {
            HASH(u->production->actor_id); HASH(u->production->queue_count);
            HASH(u->production->time_left_ms); HASH(u->production->release_active);
            HASH(u->production->release_ready);
        }
    }
#undef HASH
    return hash;
}

bool G_NetSignature(const char *map_path, uint32_t *signature) {
    FILE *file = fopen(map_path, "rb");
    if (!file) return false;
    uint32_t hash = G_Consistency();
    int byte;
    while ((byte = fgetc(file)) != EOF) hash = hash_value(hash, (unsigned)byte);
    bool ok = !ferror(file);
    fclose(file);
    for (const char *s = g_game_id; *s; ++s) hash = hash_value(hash, (unsigned char)*s);
    if (gameinfo) {
        for (int i = 0; i < gameinfo->state_count; ++i) {
            const state_t *state = &gameinfo->states[i];
            hash = hash_value(hash, state->sprite); hash = hash_value(hash, state->frame);
            hash = hash_value(hash, state->tics); hash = hash_value(hash, state->nextstate);
            hash = hash_value(hash, state->group);
        }
    }
    *signature = hash;
    return ok;
}
