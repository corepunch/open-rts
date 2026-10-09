#include "p_ai_local.h"

#include <math.h>
#include <string.h>

void P_AiInit(AiContext *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->initialized = true;
    ctx->features = AI_FEATURE_ALL;
    for (int t = 0; t < AI_MAX_TEAMS; ++t) {
        ctx->teams[t].attack_wave_timer_ms = AI_ATTACK_WAVE_INTERVAL_MS;
    }
}

void P_AiAttachGame(AiContext *ctx, const AiGameInterface *game) {
    if (!ctx) return;
    ctx->game = game;
    ctx->features = game ? game->features : AI_FEATURE_ALL;
    for (int t = 0; t < AI_MAX_TEAMS; ++t) ctx->teams[t].plan_loaded = false;
}

void P_AiPlanAdd(AiPlan *plan, int product, int count) {
    if (plan && plan->goal_count < AI_MAX_GOALS)
        plan->goals[plan->goal_count++] = (AiGoal){ .product = product, .count = count };
}

int P_AiLevelNonHuman(const level_t *map, int owner) {
    (void)map;
    return owner >= 0 && owner < AI_MAX_TEAMS && !D_PlayerIsHuman(owner) ?
        AI_LEVEL_NORMAL : AI_LEVEL_NONE;
}

void P_AiSetFeatures(AiContext *ctx, uint32_t features) {
    if (ctx) ctx->features = features;
}

const AiStats *P_AiStats(const AiContext *ctx, int owner) {
    return ctx && owner >= 0 && owner < AI_MAX_TEAMS ? &ctx->teams[owner].stats : NULL;
}

bool P_AiPollEvent(AiContext *ctx, AiEvent *out) {
    if (!ctx || ctx->event_count <= 0) return false;
    if (out) *out = ctx->events[ctx->event_head];
    ctx->event_head = (ctx->event_head + 1) % AI_EVENT_LOG_SIZE;
    ctx->event_count--;
    return true;
}

void P_AiEmit(AiContext *ctx, AiEventType type, int owner, int value) {
    if (ctx->event_count >= AI_EVENT_LOG_SIZE) {
        /* Overwrite the oldest entry; stats still carry exact totals. */
        ctx->event_head = (ctx->event_head + 1) % AI_EVENT_LOG_SIZE;
        ctx->event_count--;
        ctx->events_dropped++;
    }
    int slot = (ctx->event_head + ctx->event_count) % AI_EVENT_LOG_SIZE;
    ctx->events[slot] = (AiEvent){ .type = type, .owner = owner,
                                   .time_ms = ctx->clock_ms, .value = value };
    ctx->event_count++;
}

static bool ai_is_base(const AiContext *ctx, const mobj_t *u) {
    if (ctx && ctx->game && ctx->game->is_base) return ctx->game->is_base(u);
    return (u->traits & MF_RESOURCE_BASE) != 0;
}

static bool ai_is_anchor(const AiContext *ctx, const mobj_t *u) {
    if (ctx && ctx->game && ctx->game->is_anchor) return ctx->game->is_anchor(u);
    return ai_is_base(ctx, u);
}

static bool ai_is_busy(const AiContext *ctx, const mobj_t *u) {
    return ctx && ctx->game && ctx->game->is_busy && ctx->game->is_busy(u);
}

static bool is_idle_slug(const AiContext *ctx, const mobj_t *u, int owner) {
    return u && u->hp > 0 && !u->remove &&
           u->owner == owner &&
           (u->traits & MF_HARVESTER) != 0 &&
           u->harvest.phase == HARVEST_PHASE_NONE && !ai_is_busy(ctx, u);
}

static bool vent_occupied_by_team(const AiTeamState *team, int vent_index) {
    for (int i = 0; i < team->harvest_assignment_count; ++i) {
        if (team->harvest_assignments[i].vent_index == vent_index)
            return true;
    }
    return false;
}

static bool find_friendly_base(const AiContext *ctx, int owner, mobj_t *const *units,
                                int unit_count, fvec2_t *out_position) {
    if (!out_position) return false;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *u = units[i];
        if (u->hp <= 0 || u->remove) continue;
        if (u->owner != owner) continue;
        if (!ai_is_base(ctx, u)) continue;
        *out_position = fixed3_xy_to_fvec2(u->core.position);
        return true;
    }
    return false;
}

static void ai_tick_harvesting(AiContext *ctx, AiTeamState *team, int owner,
                                level_t *map, mobj_t *const *units, int unit_count) {
    if (!team || !map) return;

    for (int i = 0; i < unit_count; ++i) {
        mobj_t *u = units[i];
        if (!is_idle_slug(ctx, u, owner)) continue;
        if (ctx->game && ctx->game->assign_harvester) {
            if (!ctx->game->assign_harvester(map, owner, u)) continue;
            team->stats.harvest_orders++;
            P_AiEmit(ctx, AI_EVENT_HARVEST_ASSIGNED, owner, -1);
            continue;
        }
        if (team->harvest_assignment_count >= AI_MAX_HARVEST_ASSIGNMENTS) break;

        fvec2_t position = fixed3_xy_to_fvec2(u->core.position);
        /* Nearest free vent first; a vent the unit cannot path to is skipped
         * so one unreachable patch never idles the harvester for good. */
        bool tried[AI_MAX_VENT_TRIES] = { false };
        int vent_limit = map->resource_vent_count < AI_MAX_VENT_TRIES ?
            map->resource_vent_count : AI_MAX_VENT_TRIES;
        for (int attempt = 0; attempt < vent_limit; ++attempt) {
            int best_vent = -1;
            float best_dist2 = 1e30f;
            for (int v = 0; v < vent_limit; ++v) {
                const resourcevent_t *vent = &map->resource_vents[v];
                if (tried[v] || !P_VentOpenTo(map, vent, u)) continue;
                if (vent_occupied_by_team(team, v)) continue;
                float dist2 = fvec2_distance_squared(vent->attachment, position);
                if (dist2 < best_dist2) {
                    best_dist2 = dist2;
                    best_vent = v;
                }
            }
            if (best_vent < 0) break;
            tried[best_vent] = true;
            const resourcevent_t *vent = &map->resource_vents[best_vent];
            if (!P_HarvestUnitTo(map, u, vent->attachment)) continue;
            AiHarvestAssignment *a = &team->harvest_assignments[team->harvest_assignment_count++];
            a->vent_index = best_vent;
            a->slug_unit_index = i;
            team->stats.harvest_orders++;
            P_AiEmit(ctx, AI_EVENT_HARVEST_ASSIGNED, owner, best_vent);

            fvec2_t base_pos;
            if (find_friendly_base(ctx, owner, units, unit_count, &base_pos)) {
                u->harvest.return_position = base_pos;
            }
            break;
        }
    }

    /* Recall far-flung returns to the base, unless the game routes its
     * own workers (to the nearest of several depots). */
    for (int i = 0; i < unit_count && !(ctx->game && ctx->game->assign_harvester); ++i) {
        mobj_t *u = units[i];
        if (u->hp <= 0 || u->remove) continue;
        if (u->owner != owner) continue;
        if ((u->traits & MF_HARVESTER) == 0) continue;
        if (u->harvest.phase != HARVEST_PHASE_TO_BASE) continue;

        fvec2_t base_pos;
        if (find_friendly_base(ctx, owner, units, unit_count, &base_pos)) {
            float dx = fixed_to_float(u->core.position.x) - base_pos.x;
            float dy = fixed_to_float(u->core.position.y) - base_pos.y;
            float dist2 = dx * dx + dy * dy;
            if (dist2 > AI_DEFENSE_RADIUS * AI_DEFENSE_RADIUS) {
                u->harvest.return_position = base_pos;
                P_MoveUnitTo(map, u, base_pos);
            }
        }
    }

    int write = 0;
    for (int read = 0; read < team->harvest_assignment_count; ++read) {
        AiHarvestAssignment *a = &team->harvest_assignments[read];
        if (a->slug_unit_index < 0 || a->slug_unit_index >= unit_count ||
            units[a->slug_unit_index]->hp <= 0 || units[a->slug_unit_index]->remove ||
            units[a->slug_unit_index]->harvest.phase == HARVEST_PHASE_NONE) {
            continue;
        }
        if (write != read)
            team->harvest_assignments[write] = *a;
        write++;
    }
    team->harvest_assignment_count = write;
}

bool P_AiIsAlly(const level_t *map, const AiTeamState *team, int owner, const mobj_t *other) {
    if (!map->player_teams) return P_AreAllegiancesAllied(other->allegiance, team->allegiance);
    const uint32_t *allies = map->sight.allies;
#ifdef RTS_GAME_DARK_COLONY
    allies = map->peace;
#endif
    return other->owner == owner || (other->team < 8 &&
        (allies[owner] & (UINT32_C(0x40000000) >> other->team)));
}

static void ai_tick_defense(AiContext *ctx, AiTeamState *team, int owner,
                             level_t *map, mobj_t *const *units, int unit_count,
                             const gameinfo_t *game_info) {
    (void)game_info;
    if (!team || !team->has_base || !map) return;

    int rallied = 0;
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *enemy = units[i];
        if (enemy->hp <= 0 || enemy->remove) continue;
        if (ctx->game && (enemy->owner >= AI_MAX_TEAMS || (enemy->traits & MF_NOBLOCKMAP))) continue;
        if (P_AiIsAlly(map, team, owner, enemy)) continue;

        float ex = fixed_to_float(enemy->core.position.x);
        float ey = fixed_to_float(enemy->core.position.y);
        float dx = ex - team->base_position.x;
        float dy = ey - team->base_position.y;
        float dist2 = dx * dx + dy * dy;
        if (dist2 > AI_DEFENSE_RADIUS * AI_DEFENSE_RADIUS) continue;

        for (int j = 0; j < unit_count; ++j) {
            mobj_t *defender = units[j];
            if (defender->hp <= 0 || defender->remove) continue;
            if (defender->owner != owner) continue;
            if ((defender->traits & MF_ATTACK) == 0) continue;
            /* Cowards (Warcraft workers) and units on a game job never rally. */
            if ((defender->traits & MF_NOAUTOTARGET) || ai_is_busy(ctx, defender)) continue;
            if (ctx->game && !(defender->traits & MF_MOBILE)) continue;
            if (defender->harvest.phase != HARVEST_PHASE_NONE) continue;
            /* Fresh spawns never "arrive"; in game mode idle means no order. */
            if (ctx->game ? !P_HasMoveOrder(defender) : defender->movement.order_arrived) {
                defender->attack.target = units[i];
                fvec2_t enemy_pos = fixed3_xy_to_fvec2(enemy->core.position);
                P_MoveUnitTo(map, defender, enemy_pos);
                rallied++;
            }
        }
    }
    if (rallied > 0) {
        team->stats.defense_rallies++;
        P_AiEmit(ctx, AI_EVENT_DEFENSE_RALLY, owner, rallied);
    }
}

static bool ai_unit_alive(const mobj_t *u) {
    return u->hp > 0 && !u->remove;
}

/* Legacy generic waves: fixed cadence, nearest MF_RESOURCE_BASE objective. */
static void ai_tick_attack_waves(AiContext *ctx, AiTeamState *team, int owner,
                                  level_t *map, mobj_t *const *units, int unit_count,
                                  int dt_ms) {
    if (!team || !team->has_base || !map) return;

    team->attack_wave_timer_ms -= dt_ms;
    if (team->attack_wave_timer_ms > 0) return;
    team->attack_wave_timer_ms = AI_ATTACK_WAVE_INTERVAL_MS;

    int enemy_base = -1;
    float enemy_dist2 = 1e30f;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i]->hp <= 0 || units[i]->remove) continue;
        if (P_AiIsAlly(map, team, owner, units[i])) continue;
        if (!ai_is_base(ctx, units[i])) continue;
        float ex = fixed_to_float(units[i]->core.position.x);
        float ey = fixed_to_float(units[i]->core.position.y);
        float dx = ex - team->base_position.x;
        float dy = ey - team->base_position.y;
        float dist2 = dx * dx + dy * dy;
        if (dist2 < enemy_dist2) {
            enemy_dist2 = dist2;
            enemy_base = i;
        }
    }
    if (enemy_base < 0) return;

    fvec2_t target = fixed3_xy_to_fvec2(units[enemy_base]->core.position);
    int dispatched = 0;
    for (int i = 0; i < unit_count && dispatched < AI_ATTACK_WAVE_MAX_SIZE; ++i) {
        mobj_t *u = units[i];
        if (u->hp <= 0 || u->remove) continue;
        if (u->owner != owner) continue;
        if ((u->traits & MF_ATTACK) == 0) continue;
        if (u->harvest.phase != HARVEST_PHASE_NONE) continue;
        if (!u->movement.order_arrived) continue;
        u->attack.target = units[enemy_base];
        P_MoveUnitTo(map, u, target);
        dispatched++;
    }
    team->attack_wave_active = dispatched >= AI_ATTACK_WAVE_MIN_SIZE;
    if (dispatched > 0) {
        team->stats.waves++;
        team->stats.wave_units += dispatched;
        P_AiEmit(ctx, AI_EVENT_WAVE_LAUNCHED, owner, dispatched);
    }
}

/* ── interface-driven modules ─────────────────────────────────────────── */

enum { AI_PURCHASES_PER_THINK = 2 };

/* Serves the goal ladder in priority order. A goal that is merely short of
 * credits stops the scan so cheaper low-priority goals cannot starve it. */
static void ai_ensure_plan(const AiContext *ctx, AiTeamState *team, int owner,
                           level_t *map) {
    if (team->plan_loaded || !ctx->game->plan) return;
    memset(&team->plan, 0, sizeof(team->plan));
    team->plan_loaded = ctx->game->plan(map, owner, team->level, &team->plan);
    if (team->plan.goal_count > AI_MAX_GOALS) team->plan.goal_count = AI_MAX_GOALS;
    /* The first wave waits one full interval after the plan is chosen. */
    team->attack_wave_timer_ms = team->plan.wave_interval_ms > 0 ?
        team->plan.wave_interval_ms : AI_ATTACK_WAVE_INTERVAL_MS;
}

AiTry P_AiTry(AiContext *ctx, AiTeamState *team, int owner, level_t *map, int product) {
    const AiGameInterface *game = ctx->game;
    int status = game->can_purchase(map, owner, product);
    if (status == AI_BUY_NEED_TECH) {
        if (!(ctx->features & AI_FEATURE_RESEARCH) || !game->develop ||
            !game->develop(map, owner, product)) return AI_TRY_SKIP;
        team->stats.research_orders++;
        P_AiEmit(ctx, AI_EVENT_RESEARCH, owner, product);
        return AI_TRY_SAVING; /* Wait for the tech-up like saving for credits. */
    }
    if (status == AI_BUY_BLOCKED) return AI_TRY_SKIP;
    if (status == AI_BUY_NEED_CREDITS) return AI_TRY_SAVING;
    if (!game->purchase(map, owner, product)) return AI_TRY_SKIP;
    team->stats.purchases++;
    P_AiEmit(ctx, AI_EVENT_PURCHASE, owner, product);
    return AI_TRY_BOUGHT;
}

/* Supply first, so production never stalls on it; then the opening ladder;
 * once nothing there waits for credits or tech, the doctrine's needs. */
static void ai_tick_production(AiContext *ctx, AiTeamState *team, int owner,
                               level_t *map, int elapsed_ms) {
    const AiGameInterface *game = ctx->game;
    if (!game->plan || !game->owned || !game->can_purchase || !game->purchase) return;
    if (P_AiBuySupply(ctx, team, owner, map, elapsed_ms)) return;
    int bought = 0;
    for (int i = 0; i < team->plan.goal_count && bought < AI_PURCHASES_PER_THINK; ++i) {
        const AiGoal *goal = &team->plan.goals[i];
        if (ctx->clock_ms < goal->after_ms) continue;
        if (game->owned(owner, goal->product) >= goal->count) continue;
        AiTry result = P_AiTry(ctx, team, owner, map, goal->product);
        if (result == AI_TRY_SAVING) return;
        if (result == AI_TRY_BOUGHT) ++bought;
    }
    if (bought < AI_PURCHASES_PER_THINK)
        P_AiBuyDoctrine(ctx, team, owner, map, AI_PURCHASES_PER_THINK - bought);
}

/* Waves wait for a minimum idle army, then send everything idle (up to the
 * plan cap) at the nearest enemy base, falling back to any enemy object. */
static void ai_tick_attack_game(AiContext *ctx, AiTeamState *team, int owner,
                                level_t *map, mobj_t *const *units, int unit_count,
                                int elapsed_ms) {
    if (!team->has_base) return;
    int interval = team->plan.wave_interval_ms > 0 ?
        team->plan.wave_interval_ms : AI_ATTACK_WAVE_INTERVAL_MS;
    int min_size = team->plan.wave_min_size > 0 ?
        team->plan.wave_min_size : AI_ATTACK_WAVE_MIN_SIZE;
    int max_size = team->plan.wave_max_size > 0 ?
        team->plan.wave_max_size : AI_ATTACK_WAVE_MAX_SIZE;
    if (team->attack_wave_timer_ms > 0) team->attack_wave_timer_ms -= elapsed_ms;
    if (team->attack_wave_timer_ms > 0) return;

    mobj_t *idle[unit_count > 0 ? unit_count : 1];
    int idle_count = 0;
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *u = units[i];
        if (!ai_unit_alive(u) || u->owner != owner) continue;
        if ((u->traits & (MF_ATTACK | MF_MOBILE)) != (MF_ATTACK | MF_MOBILE)) continue;
        if ((u->traits & MF_NOAUTOTARGET) || ai_is_busy(ctx, u)) continue;
        if (u->harvest.phase != HARVEST_PHASE_NONE || P_HasMoveOrder(u)) continue;
        if (u->attack.target && ai_unit_alive(u->attack.target)) continue;
        idle[idle_count++] = u;
    }
    /* Keep the timer expired and retry next think. */
    if (idle_count < min_size || !P_AiArmyReady(ctx, team, idle, idle_count, max_size)) return;

    /* Objective tiers: enemy base, else any enemy structure, else any object. */
    mobj_t *goal = NULL, *structure = NULL, *fallback = NULL;
    float goal_d2 = 1e30f, structure_d2 = 1e30f, fallback_d2 = 1e30f;
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *e = units[i];
        if (!ai_unit_alive(e) || e->owner >= AI_MAX_TEAMS ||
            (e->traits & MF_NOBLOCKMAP) || P_AiIsAlly(map, team, owner, e)) continue;
        float dx = fixed_to_float(e->core.position.x) - team->base_position.x;
        float dy = fixed_to_float(e->core.position.y) - team->base_position.y;
        float d2 = dx * dx + dy * dy;
        if (ai_is_anchor(ctx, e) && d2 < goal_d2) { goal_d2 = d2; goal = e; }
        if (!(e->traits & MF_MOBILE) && d2 < structure_d2) { structure_d2 = d2; structure = e; }
        if (d2 < fallback_d2) { fallback_d2 = d2; fallback = e; }
    }
    if (!goal) goal = structure ? structure : fallback;
    if (!goal) { team->attack_wave_timer_ms = interval; return; }

    int sent = idle_count < max_size ? idle_count : max_size;
    /* Idle casters and healers ride along; they do not make a wave ready. */
    for (int i = 0; i < unit_count && sent < max_size && sent < unit_count; ++i) {
        mobj_t *u = units[i];
        AiUnitInfo info;
        if (!ai_unit_alive(u) || u->owner != owner || !(u->traits & MF_MOBILE) || (u->traits & MF_ATTACK) ||
            ai_is_busy(ctx, u) || P_HasMoveOrder(u)) continue;
        P_AiUnitInfo(ctx, u->type_id, &info);
        if (info.roles & AI_ROLE_SUPPORT) idle[sent++] = u;
    }
    if (!ctx->game->dispatch || !ctx->game->dispatch(map, owner, idle, sent, goal)) {
        fvec2_t target = fixed3_xy_to_fvec2(goal->core.position);
        for (int i = 0; i < sent; ++i) idle[i]->attack.target = goal;
        P_MoveUnitsAt(map, idle, sent, target);
        /* Moving clears nothing the target needs; make sure the objective sticks. */
        for (int i = 0; i < sent; ++i) idle[i]->attack.target = goal;
    }
    P_AiTrackWave(team, idle, sent);
    team->attack_wave_active = true;
    team->attack_wave_timer_ms = interval;
    team->stats.waves++;
    team->stats.wave_units += sent;
    P_AiEmit(ctx, AI_EVENT_WAVE_LAUNCHED, owner, sent);
}

/* A town is a cluster of drop-offs: a second hatchery or a lumber mill
 * beside the hall feeds the same workers, an expansion does not. */
#define AI_TOWN_RADIUS 12.0f

static int ai_count_towns(const AiContext *ctx, int owner, mobj_t *const *units, int unit_count) {
    fvec2_t towns[AI_MAX_TOWNS];
    int count = 0;
    for (int i = 0; i < unit_count && count < AI_MAX_TOWNS; ++i) {
        const mobj_t *u = units[i];
        if (u->hp <= 0 || u->remove || u->owner != owner || (u->traits & MF_MOBILE) || !ai_is_base(ctx, u))
            continue;
        fvec2_t at = fixed3_xy_to_fvec2(u->core.position);
        bool near = false;
        for (int t = 0; t < count && !near; ++t)
            near = fvec2_distance_squared(at, towns[t]) < AI_TOWN_RADIUS * AI_TOWN_RADIUS;
        if (!near) towns[count++] = at;
    }
    return count;
}

static void ai_census(const AiContext *ctx, AiTeamState *team, int owner,
                      mobj_t *const *units, int unit_count) {
    team->combat_unit_count = 0;
    team->harvester_count = 0;
    team->has_base = false;
    team->towns = ai_count_towns(ctx, owner, units, unit_count);
    team->allegiance = ALLEGIANCE_NEUTRAL;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *u = units[i];
        if (u->hp <= 0 || u->remove || u->owner != owner) continue;
        if (team->allegiance == ALLEGIANCE_NEUTRAL) team->allegiance = u->allegiance;
        if (ai_is_anchor(ctx, u)) {
            team->base_position = fixed3_xy_to_fvec2(u->core.position);
            team->has_base = true;
        }
        if ((u->traits & MF_ATTACK) != 0) team->combat_unit_count++;
        if ((u->traits & MF_HARVESTER) != 0) team->harvester_count++;
    }
}

static void ai_tick_game(AiContext *ctx, level_t *map, mobj_t *const *units,
                         int unit_count, const gameinfo_t *game_info, int dt_ms) {
    const AiGameInterface *game = ctx->game;
    ctx->think_counter++;
    for (int t = 0; t < AI_MAX_TEAMS; ++t) {
        AiTeamState *team = &ctx->teams[t];
        int level = game->player_level ? game->player_level(map, t) : AI_LEVEL_NONE;
        if (level != team->level) team->plan_loaded = false;
        team->level = level;
        if (level == AI_LEVEL_NONE) continue;
        /* Stagger owners so one tick never thinks for every player. */
        if ((ctx->think_counter + t) % AI_THINK_INTERVAL_TICKS != 0) continue;
        int elapsed_ms = dt_ms * AI_THINK_INTERVAL_TICKS;
        team->stats.thinks++;
        ai_census(ctx, team, t, units, unit_count);
        if (ctx->features & (AI_FEATURE_PRODUCTION | AI_FEATURE_ATTACK))
            ai_ensure_plan(ctx, team, t, map);
        P_AiScout(ctx, team, t, map, units, unit_count, elapsed_ms);
        if (ctx->features & AI_FEATURE_ECONOMY)
            ai_tick_harvesting(ctx, team, t, map, units, unit_count);
        if (ctx->features & AI_FEATURE_PRODUCTION)
            ai_tick_production(ctx, team, t, map, elapsed_ms);
        if (ctx->features & AI_FEATURE_DEFENSE)
            ai_tick_defense(ctx, team, t, map, units, unit_count, game_info);
        if (ctx->features & AI_FEATURE_ATTACK) {
            P_AiCheckRetreat(ctx, team, t, map, units, unit_count);
            ai_tick_attack_game(ctx, team, t, map, units, unit_count, elapsed_ms);
        }
        if (game->tactics && (ctx->features & (AI_FEATURE_DEFENSE | AI_FEATURE_ATTACK)))
            game->tactics(map, t, units, unit_count);
    }
}

void P_AiTick(AiContext *ctx, level_t *map, mobj_t *const *units, int unit_count,
              const gameinfo_t *game_info, int dt_ms) {
    if (!ctx || !ctx->initialized || !map || !units || unit_count <= 0) return;
    ctx->clock_ms += dt_ms;
    if (ctx->game) {
        ai_tick_game(ctx, map, units, unit_count, game_info, dt_ms);
        return;
    }

    for (int t = 0; t < AI_MAX_TEAMS; ++t) {
        AiTeamState *team = &ctx->teams[t];
        ai_census(ctx, team, t, units, unit_count);
    }

    for (int t = 0; t < AI_MAX_TEAMS; ++t) {
        AiTeamState *team = &ctx->teams[t];
        if (D_PlayerIsHuman(t) || !team->has_base) continue;
        ai_tick_harvesting(ctx, team, t, map, units, unit_count);
        ai_tick_defense(ctx, team, t, map, units, unit_count, game_info);
        ai_tick_attack_waves(ctx, team, t, map, units, unit_count, dt_ms);
    }
}
