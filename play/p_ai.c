#include "p_ai.h"
#include "p_local.h"
#include "engine.h"

#include <math.h>
#include <string.h>

void P_AiInit(AiContext *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->initialized = true;
    for (int t = 0; t < AI_MAX_TEAMS; ++t) {
        ctx->teams[t].attack_wave_timer_ms = AI_ATTACK_WAVE_INTERVAL_MS;
    }
}

static bool is_idle_slug(const mobj_t *u, int team_owner) {
    return u && u->hp > 0 && !u->remove &&
           u->owner == team_owner &&
           (u->traits & MF_HARVESTER) != 0 &&
           u->harvest.phase == HARVEST_PHASE_NONE;
}

static bool vent_occupied_by_team(const AiTeamState *team, int vent_index) {
    for (int i = 0; i < team->harvest_assignment_count; ++i) {
        if (team->harvest_assignments[i].vent_index == vent_index)
            return true;
    }
    return false;
}

static void ai_tick_harvesting(AiTeamState *team, int team_owner,
                                level_t *map, mobj_t *units, int unit_count) {
    if (!team || !map) return;

    for (int i = 0; i < unit_count; ++i) {
        mobj_t *u = &units[i];
        if (!is_idle_slug(u, team_owner)) continue;
        if (team->harvest_assignment_count >= AI_MAX_HARVEST_ASSIGNMENTS) break;

        float ux = u->core.position.x;
        float uy = u->core.position.y;
        int best_vent = -1;
        float best_dist2 = 1e30f;
        for (int v = 0; v < map->resource_vent_count; ++v) {
            const resourcevent_t *vent = &map->resource_vents[v];
            if (!vent->active || vent->amount <= 0) continue;
            if (vent_occupied_by_team(team, v)) continue;
            float dx = vent->attachment.x - ux;
            float dy = vent->attachment.y - uy;
            float dist2 = dx * dx + dy * dy;
            if (dist2 < best_dist2) {
                best_dist2 = dist2;
                best_vent = v;
            }
        }
        if (best_vent < 0) continue;

        const resourcevent_t *vent = &map->resource_vents[best_vent];
        if (P_HarvestUnitTo(map, u, vent->attachment)) {
            AiHarvestAssignment *a = &team->harvest_assignments[team->harvest_assignment_count++];
            a->vent_index = best_vent;
            a->slug_unit_index = i;
        }
    }

    int write = 0;
    for (int read = 0; read < team->harvest_assignment_count; ++read) {
        AiHarvestAssignment *a = &team->harvest_assignments[read];
        if (a->slug_unit_index < 0 || a->slug_unit_index >= unit_count ||
            units[a->slug_unit_index].hp <= 0 || units[a->slug_unit_index].remove ||
            units[a->slug_unit_index].harvest.phase == HARVEST_PHASE_NONE) {
            continue;
        }
        if (write != read)
            team->harvest_assignments[write] = *a;
        write++;
    }
    team->harvest_assignment_count = write;
}

static void ai_tick_defense(AiTeamState *team, int team_owner,
                             level_t *map, mobj_t *units, int unit_count,
                             const gameinfo_t *game_info) {
    (void)game_info;
    if (!team || !team->has_base || !map) return;

    for (int i = 0; i < unit_count; ++i) {
        mobj_t *enemy = &units[i];
        if (enemy->hp <= 0 || enemy->remove) continue;
        if (enemy->owner == team_owner) continue;

        float ex = fixed_to_float(enemy->core.position.x);
        float ey = fixed_to_float(enemy->core.position.y);
        float dx = ex - team->base_position.x;
        float dy = ey - team->base_position.y;
        float dist2 = dx * dx + dy * dy;
        if (dist2 > AI_DEFENSE_RADIUS * AI_DEFENSE_RADIUS) continue;

        for (int j = 0; j < unit_count; ++j) {
            mobj_t *defender = &units[j];
            if (defender->hp <= 0 || defender->remove) continue;
            if (defender->owner != team_owner) continue;
            if ((defender->traits & MF_ATTACK) == 0) continue;
            if (defender->harvest.phase != HARVEST_PHASE_NONE) continue;
            if (defender->movement.order_arrived) {
                defender->attack.target = i;
                fvec2_t enemy_pos = fixedvec3_xy_to_fvec2(enemy->core.position);
                P_MoveUnitTo(map, defender, enemy_pos);
            }
        }
    }
}

static void ai_tick_attack_waves(AiTeamState *team, int team_owner,
                                  level_t *map, mobj_t *units, int unit_count,
                                  int dt_ms) {
    if (!team || !team->has_base || !map) return;

    team->attack_wave_timer_ms -= dt_ms;
    if (team->attack_wave_timer_ms > 0) return;
    team->attack_wave_timer_ms = AI_ATTACK_WAVE_INTERVAL_MS;

    int enemy_base = -1;
    float enemy_dist2 = 1e30f;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].hp <= 0 || units[i].remove) continue;
        if (units[i].owner == team_owner) continue;
        if ((units[i].traits & MF_RESOURCE_BASE) == 0) continue;
        float ex = fixed_to_float(units[i].core.position.x);
        float ey = fixed_to_float(units[i].core.position.y);
        float dx = ex - team->base_position.x;
        float dy = ey - team->base_position.y;
        float dist2 = dx * dx + dy * dy;
        if (dist2 < enemy_dist2) {
            enemy_dist2 = dist2;
            enemy_base = i;
        }
    }
    if (enemy_base < 0) return;

    fvec2_t target = fixedvec3_xy_to_fvec2(units[enemy_base].core.position);
    int dispatched = 0;
    for (int i = 0; i < unit_count && dispatched < AI_ATTACK_WAVE_MAX_SIZE; ++i) {
        mobj_t *u = &units[i];
        if (u->hp <= 0 || u->remove) continue;
        if (u->owner != team_owner) continue;
        if ((u->traits & MF_ATTACK) == 0) continue;
        if (u->harvest.phase != HARVEST_PHASE_NONE) continue;
        if (!u->movement.order_arrived) continue;
        u->attack.target = enemy_base;
        P_MoveUnitTo(map, u, target);
        dispatched++;
    }
    team->attack_wave_active = dispatched >= AI_ATTACK_WAVE_MIN_SIZE;
}

void P_AiTick(AiContext *ctx, level_t *map, mobj_t *units, int unit_count,
              const gameinfo_t *game_info, int dt_ms) {
    if (!ctx || !ctx->initialized || !map || !units || unit_count <= 0) return;

    for (int t = 0; t < AI_MAX_TEAMS; ++t) {
        AiTeamState *team = &ctx->teams[t];
        int team_owner = t;
        team->combat_unit_count = 0;
        team->harvester_count = 0;
        team->has_base = false;

        for (int i = 0; i < unit_count; ++i) {
            const mobj_t *u = &units[i];
            if (u->hp <= 0 || u->remove || u->owner != team_owner) continue;
            if ((u->traits & MF_RESOURCE_BASE) != 0) {
                team->base_position = fixedvec3_xy_to_fvec2(u->core.position);
                team->has_base = true;
            }
            if ((u->traits & MF_ATTACK) != 0) team->combat_unit_count++;
            if ((u->traits & MF_HARVESTER) != 0) team->harvester_count++;
        }
    }

    for (int t = 0; t < AI_MAX_TEAMS; ++t) {
        AiTeamState *team = &ctx->teams[t];
        if (!team->has_base) continue;
        ai_tick_harvesting(team, t, map, units, unit_count);
        ai_tick_defense(team, t, map, units, unit_count, game_info);
        ai_tick_attack_waves(team, t, map, units, unit_count, dt_ms);
    }
}
