#include "p_ai.h"
#include "dc_types.h"
#include <math.h>

typedef struct {
    int think_interval_ms;
    int attack_wave_interval_ms;
    float defense_radius;
    float attack_eagerness;
} AiConfig;

/* These values mirror the observable Krusty attack policy: think in batches,
 * prefer dangerous mobile targets, then use distance as the stable tie-break.
 * The table is intentionally plugin-local until DC's unitid/depend tables are
 * fully decoded. */
static const AiConfig ai_config = {
    .think_interval_ms = 500,
    .attack_wave_interval_ms = 5000,
    .defense_radius = 12.0f,
    .attack_eagerness = 1.0f,
};

static int ai_target(const mobj_t *attacker, const mobj_t *units,
                                 int unit_count, int preferred_index, bool defending) {
    int best = -1;
    float best_score = -INFINITY;
    fvec2_t attacker_position = fixed3_xy_to_fvec2(attacker->core.position);
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *candidate = &units[i];
        if (candidate == attacker || candidate->remove || candidate->hp <= 0 ||
            candidate->owner == attacker->owner) continue;
        fvec2_t delta = fvec2_sub(fixed3_xy_to_fvec2(candidate->core.position),
                                  attacker_position);
        float distance2 = fvec2_length_squared(delta);
        float threat = (candidate->traits & MF_ATTACK) != 0 ? 2.0f : 0.0f;
        float mobility = (candidate->traits & MF_MOBILE) != 0 ? 0.5f : 0.0f;
        float preferred = i == preferred_index ? 1.5f : 0.0f;
        if (defending && (candidate->traits & MF_ATTACK) != 0) preferred += 1.0f;
        float score = ai_config.attack_eagerness * (threat + mobility) -
                      distance2 * 0.02f + preferred;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

static int ai_find_wave_target(const mobj_t *units, int unit_count) {
    int best = -1;
    float best_score = -INFINITY;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *candidate = &units[i];
        if (candidate->remove || candidate->hp <= 0 || candidate->owner != 0) continue;
        float score = (candidate->traits & MF_ATTACK) != 0 ? 3.0f : 0.0f;
        score += (candidate->traits & MF_MOBILE) != 0 ? 1.0f : 0.0f;
        score += candidate->max_hp > 0 ? (float)candidate->max_hp / 2000.0f : 0.0f;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

static bool ai_is_defending(const mobj_t *units, int unit_count,
                                        fvec2_t base_position) {
    float radius2 = ai_config.defense_radius *
                    ai_config.defense_radius;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *unit = &units[i];
        if (unit->remove || unit->hp <= 0 || unit->owner != 0 ||
            (unit->traits & MF_ATTACK) == 0) continue;
        if (fvec2_distance_squared(fixed3_xy_to_fvec2(unit->core.position),
                                   base_position) <= radius2) return true;
    }
    return false;
}

static int ai_nearest_vent(const level_t *map, fvec2_t position) {
    int best = -1;
    float best_distance2 = INFINITY;
    if (!map || !map->resource_vents) return best;
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!vent->active || vent->amount <= 0 || vent->rate <= 0) continue;
        float distance2 = fvec2_distance_squared(position, vent->attachment);
        if (distance2 < best_distance2) {
            best_distance2 = distance2;
            best = i;
        }
    }
    return best;
}

static void update_ai_economy(const level_t *map, mobj_t *units,
                                          int unit_count) {
    if (!map || !units) return;
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *unit = &units[i];
        if (unit->remove || unit->hp <= 0 || unit->owner == 0 ||
            (unit->traits & (MF_MOBILE | MF_HARVESTER)) !=
                (MF_MOBILE | MF_HARVESTER) || unit->harvest.target >= 0) continue;
        int vent_index = ai_nearest_vent(
            map, fixed3_xy_to_fvec2(unit->core.position));
        if (vent_index >= 0)
            P_HarvestUnitTo(map, unit, map->resource_vents[vent_index].attachment);
    }
}

void DC_UpdateAI(AiState *ai, const level_t *map,
                                  mobj_t *units, int unit_count, int dt_ms) {
    if (!map || !units || unit_count <= 0 ||
        !map_has_ai(map, 1)) return;
    ai->elapsed_ms += dt_ms;
    if (ai->elapsed_ms < ai_config.think_interval_ms) return;
    ai->elapsed_ms %= ai_config.think_interval_ms;
    update_ai_economy(map, units, unit_count);

    ai->wave_elapsed_ms += ai_config.think_interval_ms;
    int wave_target = -1;
    if (ai->wave_elapsed_ms >= ai_config.attack_wave_interval_ms) {
        ai->wave_elapsed_ms %= ai_config.attack_wave_interval_ms;
        wave_target = ai_find_wave_target(units, unit_count);
        ai->wave_target_id = wave_target >= 0 ? units[wave_target].id : 0;
    }
    if (wave_target < 0 && ai->wave_target_id != 0) {
        for (int i = 0; i < unit_count; ++i) {
            if (units[i].id == ai->wave_target_id &&
                !units[i].remove && units[i].hp > 0) {
                wave_target = i;
                break;
            }
        }
    }

    fvec2_t base_position = { 0.0f, 0.0f };
    int base_count = 0;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *unit = &units[i];
        if (unit->remove || unit->hp <= 0 || unit->owner == 0 ||
            (unit->traits & MF_MOBILE) != 0) continue;
        base_position = fvec2_add(base_position,
                                  fixed3_xy_to_fvec2(unit->core.position));
        base_count++;
    }
    if (base_count > 0) base_position = fvec2_scale(base_position, 1.0f / (float)base_count);
    bool defending = base_count > 0 &&
                     ai_is_defending(units, unit_count, base_position);

    for (int i = 0; i < unit_count; ++i) {
        mobj_t *attacker = &units[i];
        if (attacker->remove || attacker->hp <= 0 || attacker->owner == 0 ||
            (attacker->traits & (MF_MOBILE | MF_ATTACK)) != (MF_MOBILE | MF_ATTACK)) {
            continue;
        }
        int target_index = ai_target(attacker, units, unit_count,
                                                 defending ? -1 : wave_target,
                                                 defending);
        if (target_index < 0) continue;
        mobj_t *target = &units[target_index];
        attacker->attack.target = target_index;
        fvec2_t target_position = fixed3_xy_to_fvec2(target->core.position);
        fvec2_t attacker_position = fixed3_xy_to_fvec2(attacker->core.position);
        float range = attacker->info && attacker->info->attack.range > 0.0f ?
            attacker->info->attack.range : 1.0f;
        if (fvec2_distance_squared(attacker_position, target_position) > range * range) {
            P_MoveUnitTo(map, attacker, target_position);
        }
    }
}

