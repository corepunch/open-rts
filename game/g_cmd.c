#include "d_net.h"
#include "game.h"
#include "p_local.h"
#ifdef RTS_GAME_DARK_COLONY
#include "dc_types.h"
#endif

enum { MAXPENDINGCOMMANDS = 64 };
static ticcmd_t pending[MAXPENDINGCOMMANDS];
static unsigned commandhead, commandcount;
bool paused;

void G_ClearTiccmds(void) {
    commandhead = commandcount = 0;
}

bool G_QueueTiccmd(const ticcmd_t *cmd) {
    if (!cmd || cmd->count > MAXCOMMANDUNITS || (unsigned)cmd->order > TC_MAX) return false;
    if (cmd->order == TC_PATH && (cmd->path.count < 1 || cmd->path.count > MAXWAYPOINTS ||
                                 (unsigned)cmd->path.mode > WP_ONCE)) return false;
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

static bool selected_command(ticcmd_t *cmd, mobj_t *const *units, int count) {
    for (int i = 0; i < count; ++i) {
        const mobj_t *unit = units[i];
        if (!P_MobjIsSelected(unit) || unit->owner != consoleplayer || unit->remove || unit->hp <= 0) continue;
        if (cmd->count == MAXCOMMANDUNITS) {
            fprintf(stderr, "Too many units in one order (maximum %d).\n", MAXCOMMANDUNITS);
            return false;
        }
        cmd->units[cmd->count++] = unit->id;
    }
    return cmd->count && G_QueueTiccmd(cmd);
}

bool G_SelectedTiccmd(ticorder_t order, mobj_t *const *units, int count,
                     fvec2_t position, uint32_t target) {
    ticcmd_t cmd = { .order = order, .position = fixed3_from_fvec2(position, 0), .target = target };
    return selected_command(&cmd, units, count);
}

bool G_PathOrder(mobj_t *const *units, int count, const waypoints_t *path) {
    if (!path || path->count < 1 || path->count > MAXWAYPOINTS ||
        (unsigned)path->mode > WP_ONCE) return false;
    for (int i = 0; i < path->count; ++i)
        if (!L_Contains(&level, path->points[i].x, path->points[i].y)) return false;
    ticcmd_t cmd = {.order = TC_PATH, .path = *path};
    cmd.path.current = 0;
    cmd.path.backwards = false;
    return selected_command(&cmd, units, count);
}

bool G_BuildOrder(mobj_t *producer, int product) {
    if (!producer || producer->owner != consoleplayer) return false;
    ticcmd_t cmd = { .order = TC_BUILD, .product = product, .count = 1, .units = { producer->id } };
    return G_QueueTiccmd(&cmd);
}

void G_RunTiccmd(int player, const ticcmd_t *cmd) {
    if (!cmd || cmd->order == TC_NONE || (unsigned)cmd->order > TC_MAX || cmd->count > MAXCOMMANDUNITS ||
        player < 0 || player >= RTS_MODEL_MAX_PLAYERS) return;
    if (cmd->order == TC_PATH) {
        if (cmd->path.count < 1 || cmd->path.count > MAXWAYPOINTS ||
            (unsigned)cmd->path.mode > WP_ONCE) return;
        for (int i = 0; i < cmd->path.count; ++i)
            if (!L_Contains(&level, cmd->path.points[i].x, cmd->path.points[i].y)) return;
    }
    if (cmd->order == TC_PAUSE) { paused = !paused; return; }
#ifdef RTS_GAME_DARK_COLONY
    if (cmd->order == TC_PURCHASE) {
        DC_SelectPurchase(player, cmd->product, cmd->target != 0);
        return;
    }
    if (cmd->order == TC_SUBMIT) { DC_SubmitPurchases(player); return; }
#endif
    mobj_t *units[MAXCOMMANDUNITS], *target = NULL;
    int count = 0;
    /* Resolve in thinker order, never in UI selection order or by array index. */
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
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
    if (cmd->order == TC_ATTACK) {
        bool eligible = false;
        for (int i = 0; i < count; ++i) eligible |= P_CanTarget(units[i], target);
        if (!eligible) return;
    }
    if ((cmd->order == TC_MOVE || cmd->order == TC_ORDER || cmd->order == TC_HARVEST) &&
        !L_Contains(&level, cmd->position.x >> FIXED_FRAC_BITS,
                    cmd->position.y >> FIXED_FRAC_BITS)) return;
    if (cmd->order == TC_MODE) {
        for (int i = 0; i < count; ++i) {
            units[i]->move_only = cmd->target != 0;
            if (units[i]->move_only) units[i]->attack.target = NULL;
        }
        return;
    }
    if (cmd->order == TC_PATH || cmd->order == TC_WAYPOINT) {
        ivec2_t point = {cmd->position.x >> FIXED_FRAC_BITS, cmd->position.y >> FIXED_FRAC_BITS};
        if (cmd->order == TC_WAYPOINT && !L_Contains(&level, point.x, point.y)) return;
        for (int i = 0; i < count; ++i) {
            mobj_t *actor = units[i];
            if (!(actor->traits & MF_MOBILE)) continue;
            if (cmd->order == TC_PATH) {
                actor->waypoints = cmd->path;
                actor->waypoints.current = 0;
                actor->waypoints.backwards = false;
            } else {
                if (!cmd->target) actor->waypoints = (waypoints_t){.mode = WP_LOOP};
                if (actor->waypoints.count < MAXWAYPOINTS)
                    actor->waypoints.points[actor->waypoints.count++] = point;
            }
            if (cmd->order == TC_PATH || !cmd->target) {
                actor->attack.target = NULL;
                actor->harvest.target = -1;
                actor->harvest.base = NULL;
                actor->harvest.phase = actor->harvest.timer_ms = 0;
                P_ClearMove(actor);
                actor->movement.order_id = 0;
                actor->movement.order_arrived = false;
                P_MoveUnitTo(&level, actor, fvec2_cell_center(actor->waypoints.points[0]));
            }
        }
        return;
    }
    if (cmd->order != TC_BUILD)
        for (int i = 0; i < count; ++i) units[i]->waypoints = (waypoints_t){0};
    if (cmd->order == TC_DEPLOY) {
        for (int i = 0; i < count; ++i) P_Deploy(units[i]);
        return;
    }
    fvec2_t goal = fixed3_xy_to_fvec2(cmd->position);
    if (cmd->order == TC_BUILD) {
        const StaticProductDefinition *product = G_ModelProductByUIId(NULL, cmd->product);
        G_PlayerBuildProduct(units[0], product);
        return;
    }
    if (cmd->order == TC_STOP) {
        for (int i = 0; i < count; ++i) {
            mobj_t *unit = units[i];
            P_ClearMove(unit);
            unit->movement.goal = fixed3_xy_to_fvec2(unit->core.position);
            unit->movement.order_id = 0;
            unit->movement.order_arrived = true;
            unit->attack.target = NULL;
            unit->harvest.target = -1;
            unit->harvest.base = NULL;
            unit->harvest.phase = unit->harvest.timer_ms = 0;
            unit->core.momentum = fixed3_zero();
        }
        return;
    }
    if (cmd->order == TC_ATTACK) {
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
        units[i]->attack.target = P_CanTarget(units[i], target) ? target : NULL;
        units[i]->harvest.target = -1;
        units[i]->harvest.base = NULL;
        units[i]->harvest.phase = 0;
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
    HASH(leveltime); HASH(paused); HASH(level.random_index); HASH(level.next_mobj_id); HASH(level.next_move_order_id);
    for (int p = 0; p < RTS_MODEL_MAX_PLAYERS; ++p)
        for (int r = 0; r < RTS_MAX_RESOURCES; ++r) HASH(level.player_resources[p][r]);
#ifdef RTS_GAME_DARK_COLONY
    for (int owner = 0; owner < 8; ++owner)
        for (int row = 0; row < 110; ++row) {
            HASH(level.purchases[owner][row].selected);
            HASH(level.purchases[owner][row].queued);
        }
    for (int type = 0; type < 106; ++type)
        for (int owner = 0; owner < 8; ++owner) {
            HASH(level.upgrades[type][owner].weapon);
            HASH(level.upgrades[type][owner].armor);
        }
#endif
    for (int i = 0; i < level.resource_vent_count; ++i) {
        HASH(level.resource_vents[i].amount); HASH(level.resource_vents[i].active);
    }
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *u = (mobj_t *)th;
        HASH(u->id); HASH(u->type_id); HASH(u->owner); HASH(u->team); HASH(u->allegiance);
        HASH(u->core.position.x); HASH(u->core.position.y); HASH(u->core.position.z);
        HASH(u->core.momentum.x); HASH(u->core.momentum.y); HASH(u->core.momentum.z);
        HASH(u->missile.clock); HASH(u->missile.age); HASH(u->missile.duration);
        HASH(u->missile.damage);
        HASH(u->missile.wait);
        HASH(u->missile.phase);
        HASH(u->core.angle); HASH(u->core.state_id); HASH(u->core.tics);
        HASH(u->hp); HASH(u->traits & ~MF_SELECTED); HASH(u->remove);
        HASH(u->move_only); HASH(u->waypoints.count); HASH(u->waypoints.current);
        HASH(u->waypoints.mode); HASH(u->waypoints.backwards);
        for (int i = 0; i < u->waypoints.count; ++i) {
            HASH(u->waypoints.points[i].x); HASH(u->waypoints.points[i].y);
        }
#ifdef MOBJ_GAME_CHECKSUM
        MOBJ_GAME_CHECKSUM(HASH, u);
#endif
        HASH(u->target ? u->target->id : 0);
        HASH(u->attack.target ? u->attack.target->id : 0); HASH(u->attack.cooldown_left_ms);
        HASH(u->attack.shots);
        HASH(u->harvest.target); HASH(u->harvest.phase); HASH(u->harvest.timer_ms); HASH(u->harvest.cargo);
        HASH(u->harvest.resource_type); HASH(u->harvest.base ? u->harvest.base->id : 0);
        fixed3_t bay = fixed3_from_fvec2(u->harvest.return_position, 0);
        HASH(bay.x); HASH(bay.y);
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
