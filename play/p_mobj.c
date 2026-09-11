#define _DEFAULT_SOURCE
#include "p_local.h"
#include "game.h"
#include "info.h"

enum {
    RTS_HARVEST_INTERVAL_MS = 1000,
    RTS_TURN_STEP_MS = 75,
};

static const state_t *state_at(const gameinfo_t *game_info, int state_id) {
    if (!game_info || !game_info->states || state_id < 0 || state_id >= game_info->state_count)
        return NULL;
    return &game_info->states[state_id];
}

static float mobj_attack_range(const mobj_t *unit) {
    return unit && unit->info ? unit->info->attack.range : 0.0f;
}

static int mobj_attack_damage(const mobj_t *unit) {
    return unit && unit->info ? unit->info->attack.damage : 0;
}

static int mobj_attack_cooldown_ms(const mobj_t *unit) {
    return unit && unit->info ? unit->info->attack.cooldown_ms : 0;
}

static int mobj_harvest_state(const mobj_t *unit) {
    return unit && unit->info ? unit->info->harvest.state_id : 0;
}

static int mobj_harvest_capacity(const mobj_t *unit) {
    return unit && unit->info ? unit->info->harvest.capacity : 0;
}

static void apply_state_visuals(const gameinfo_t *game_info, mobjcore_t *mobj,
                                const state_t *state, bool apply_offsets) {
    if (!game_info || !mobj || !state) return;
    mobj->sprite_id = state->sprite;
    mobj->frame = state->frame;
    mobj->render_flags = 0;
    mobj->render_remap = 0;
    mobj->render_intensity = 16;
    if (apply_offsets) mobj->render_offset = (ivec2_t){ 0, 0 };
    if (mobj->sprite_id >= 0 && mobj->sprite_id < game_info->sprite_count &&
        game_info->sprnames && game_info->sprnames[mobj->sprite_id]) {
        snprintf(mobj->sprite_name, sizeof(mobj->sprite_name), "%s",
                 game_info->sprnames[mobj->sprite_id]);
    }
}

bool P_SetMobjState(mobj_t *unit, int state_id) {
    const gameinfo_t *game_info = gameinfo;
    if (!game_info || !unit || unit->remove) return false;
    int guard = 0;
    while (guard++ < game_info->state_count + 1) {
        if (state_id == game_info->null_state || state_id < 0 ||
            state_id >= game_info->state_count) {
            unit->core.state_id = game_info->null_state;
            unit->core.tics = 0;
            unit->core.momentum = fixed3_zero();
            P_RemoveMobj(unit);
            return false;
        }
        const state_t *state = &game_info->states[state_id];
        unit->core.state_id = state_id;
        unit->core.tics = state->tics;
        apply_state_visuals(game_info, &unit->core, state, false);
        debug_effects_log("state unit type=%u state=%d sprite=%d frame=%d tics=%d",
                          unit->type_id, unit->core.state_id, unit->core.sprite_id,
                          unit->core.frame, unit->core.tics);
        if (state->group == 3) {
            const char *sprite_name = "(unknown)";
            if (unit->core.sprite_id >= 0 && unit->core.sprite_id < game_info->sprite_count &&
                game_info->sprnames && game_info->sprnames[unit->core.sprite_id]) {
                sprite_name = game_info->sprnames[unit->core.sprite_id];
            }
            debug_effects_log("shoot state unit_type=%u state=%d sprite=%s frame=%d",
                              unit->type_id, unit->core.state_id,
                              sprite_name, unit->core.frame);
        }
        if (state->action) state->action(unit);
        if (unit->remove || unit->core.state_id != state_id) return !unit->remove;
        if (unit->core.tics != 0) return true;
        state_id = state->nextstate;
    }
    P_RemoveMobj(unit);
    return false;
}

bool P_TickMobjState(mobj_t *unit) {
    if (!gameinfo || !unit || unit->remove) return false;
    if (unit->core.state_id <= 0) return false;
    if (unit->core.tics > 0) unit->core.tics--;
    if (unit->core.tics != 0) return true;
    const state_t *state = state_at(gameinfo, unit->core.state_id);
    return P_SetMobjState(unit,
                          state ? state->nextstate : gameinfo->null_state);
}

production_t *P_EnsureMobjProduction(mobj_t *unit) {
    if (!unit) return NULL;
    if (!unit->production) unit->production = calloc(1, sizeof(*unit->production));
    return unit->production;
}

void P_FreeMobjProduction(mobj_t *unit) {
    if (!unit) return;
    free(unit->production);
    unit->production = NULL;
}

void P_ApplyActorTypeDefaults(mobj_t *unit, const mobjtype_t *type) {
    if (!unit || !type) return;
    if (unit->info != type) {
        unit->native_type_id = type->native_type_id;
        unit->traits = type->traits;
    }
    unit->info = type;
    unit->type_id = type->id;
    if (unit->speed <= 0.0f) unit->speed = type->speed;
    if (unit->max_hp <= 0) unit->max_hp = type->max_hp;
    if (unit->hp <= 0) unit->hp = unit->max_hp;
    if (unit->core.render_intensity == 0) unit->core.render_intensity = 16;
    if (unit->harvest.target == 0) unit->harvest.target = -1;
    if (unit->core.sprite_name[0] == '\0' && type->sprite_name)
        snprintf(unit->core.sprite_name, sizeof(unit->core.sprite_name), "%s", type->sprite_name);
}

mobj_t *P_SpawnMobj(fixed3_t position, uint16_t type) {
    mobj_t *mobj = calloc(1, sizeof(*mobj));
    if (!mobj) return NULL;
    mobj->core.position = position;
    mobj->type_id = type;
    mobj->id = ++level.next_mobj_id;
    for (int i = 0; i < num_actor_types; ++i) {
        if (actor_types[i].id == type) {
            P_ApplyActorTypeDefaults(mobj, &actor_types[i]);
            break;
        }
    }
    P_InitMobj(gameinfo, mobj);
    mobj->thinker.function = P_MobjThinker;
    P_AddThinker(&mobj->thinker);
    return mobj;
}

void P_InitMobj(const gameinfo_t *game_info, mobj_t *unit) {
    if (!game_info || !unit || !game_info->mobjinfo ||
        unit->type_id <= 0 || unit->type_id >= game_info->mobj_type_count) {
        return;
    }
    if (unit->core.render_intensity == 0) unit->core.render_intensity = 16;
    const mobjinfo_t *info = &game_info->mobjinfo[unit->type_id];
    if (unit->core.position.z == 0) unit->core.position.z = info->spawnz;
    if (unit->max_hp <= 0) unit->max_hp = info->spawnhealth;
    if (unit->hp <= 0) unit->hp = unit->max_hp;
    if (unit->speed <= 0.0f) unit->speed = (float)info->speed;
    if (unit->radius <= 0.05f) {
        unit->radius = (float)info->radius / 32.0f;
        if (unit->radius < 0.32f) unit->radius = 0.32f;
        if (unit->radius > 0.90f) unit->radius = 0.90f;
    }
    if (unit->core.state_id <= 0) {
        const state_t *state = state_at(game_info, info->spawnstate);
        if (!state) return;
        unit->core.state_id = info->spawnstate;
        unit->core.tics = state->tics;
    }
    apply_state_visuals(game_info, &unit->core,
                        state_at(game_info, unit->core.state_id), false);
}


angle_t P_PointToAngle(float dx, float dy) {
    return angle_from_screen_vector(dx, dy);
}

static angle_t angle_from_map_vector(const level_t *map, float dx, float dy) {
    (void)map;
#if RTS_WORLD_Y_UP
    dy = -dy;
#endif
    return P_PointToAngle(dx, dy);
}

void P_AngleToVec(angle_t angle, float *dx, float *dy) {
    angle_to_screen_vector(angle, dx, dy);
}

static mobj_t *attack_target_in_range(const mobj_t *attacker) {
    if (!(attacker->traits & MF_ATTACK) || mobj_attack_damage(attacker) <= 0)
        return NULL;
    float range2 = mobj_attack_range(attacker) * mobj_attack_range(attacker);
    mobj_t *target = attacker->attack.target;
    if (target && !target->remove && target->hp > 0 &&
        P_VisibleTo(attacker, target) &&
        !(target->traits & (MF_NOBLOCKMAP | MF_MISSILE)) &&
        !P_IsAlly(attacker, target) &&
        fvec2_distance_squared(fixed3_xy_to_fvec2(target->core.position),
                               fixed3_xy_to_fvec2(attacker->core.position)) <= range2)
        return target;
    target = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *candidate = (mobj_t *)th;
        if (candidate == attacker || candidate->remove || candidate->hp <= 0 ||
            (candidate->traits & (MF_NOBLOCKMAP | MF_MISSILE)) ||
            P_IsAlly(attacker, candidate) ||
            !P_VisibleTo(attacker, candidate)) continue;
        float dist2 = fvec2_distance_squared(
            fixed3_xy_to_fvec2(candidate->core.position),
            fixed3_xy_to_fvec2(attacker->core.position));
        if (dist2 <= range2) { range2 = dist2; target = candidate; }
    }
    return target;
}

static void damage_mobj(mobj_t *target, int damage) {
    if (!target || target->remove || target->hp <= 0 || damage <= 0) return;
    target->hp -= damage;
    if (target->info && target->info->damage_action) target->info->damage_action(target);
    if (target->hp > 0) return;

    target->hp = 0;
    P_MobjSetSelected(target, false);
    target->traits &= ~(MF_SELECTABLE | MF_MOBILE |
                        MF_ATTACK | MF_HARVESTER);
    target->movement.flow_field = NULL;
    target->movement.order_arrived = false;
    target->harvest.target = -1;
    target->harvest.timer_ms = 0;
    target->harvest.phase = 0;
    target->harvest.cargo = 0;
    target->attack.target = NULL;
    target->attack.cooldown_left_ms = 0;
    target->core.momentum = fixed3_zero();
    if (gameinfo && target->type_id > 0 &&
        target->type_id < gameinfo->mobj_type_count) {
        int deathstate = gameinfo->mobjinfo[target->type_id].deathstate;
        P_SetMobjState(target, deathstate);
    } else {
        P_RemoveMobj(target);
    }
}

mobj_t *P_SpawnMissile(mobj_t *source, mobj_t *target, uint16_t type) {
    if (!source || !target || source->remove || target->remove || target->hp <= 0 ||
        !gameinfo || !gameinfo->mobjinfo || type == 0 || type >= gameinfo->mobj_type_count)
        return NULL;

    const mobjinfo_t *missile_info = &gameinfo->mobjinfo[type];
    fvec2_t direction = fvec2_sub(fixed3_xy_to_fvec2(target->core.position),
                                  fixed3_xy_to_fvec2(source->core.position));
    float distance = sqrtf(fvec2_length_squared(direction));
    if (missile_info->speed <= 0 || distance <= 0.0001f) return NULL;
    direction = fvec2_scale(direction, 1.0f / distance);

    mobj_t *missile = P_SpawnMobj(source->core.position, type);
    if (!missile) return NULL;
    missile->target = source; /* Doom stores the missile originator here. */
    missile->owner = source->owner;
    missile->team = source->team;
    missile->allegiance = source->allegiance;
    missile->core.angle = angle_from_map_vector(&level, direction.x, direction.y);
    missile->core.momentum = fixed3_planar_delta(
        fvec2_scale(direction, (float)missile_info->speed * FIXED_DT));

    /* Doom's P_CheckMissileSpawn advances half a tic and immediately explodes
     * a missile that starts inside a blocking line. */
    fvec2_t half_step = fvec2_scale(
        fixed3_xy_to_fvec2(missile->core.momentum), 0.5f);
    fvec2_t half_position = fvec2_add(
        fixed3_xy_to_fvec2(missile->core.position), half_step);
    if (level.width > 0 &&
        !P_CheckPosition(&level, missile, half_position.x, half_position.y)) {
        P_ExplodeMissile(missile);
        return missile;
    }
    return missile;
}

void P_ExplodeMissile(mobj_t *missile) {
    if (!missile || missile->remove) return;
    missile->core.momentum = fixed3_zero();
    if (!gameinfo || !gameinfo->mobjinfo || missile->type_id >= gameinfo->mobj_type_count) {
        P_RemoveMobj(missile);
        return;
    }
    if (!P_SetMobjState(missile, gameinfo->mobjinfo[missile->type_id].deathstate)) return;
    missile->traits &= ~MF_MISSILE;
}

bool P_Attack(mobj_t *attacker) {
    if (!attacker || (attacker->traits & MF_ATTACK) == 0) return false;
    mobj_t *target = attacker->attack.target;
    if (!target || target->remove || target->hp <= 0 ||
        (target->traits & (MF_NOBLOCKMAP | MF_MISSILE)) ||
        P_IsAlly(attacker, target) ||
        !P_VisibleTo(attacker, target)) {
        target = NULL;
        float best = mobj_attack_range(attacker) * mobj_attack_range(attacker);
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
            mobj_t *candidate = (mobj_t *)th;
            if (candidate == attacker || candidate->remove || candidate->hp <= 0 ||
                (candidate->traits & (MF_NOBLOCKMAP | MF_MISSILE)) ||
                P_IsAlly(attacker, candidate) ||
                !P_VisibleTo(attacker, candidate)) continue;
            float distance = fvec2_distance_squared(fixed3_xy_to_fvec2(candidate->core.position),
                                                    fixed3_xy_to_fvec2(attacker->core.position));
            if (distance <= best) { best = distance; target = candidate; }
        }
    }
    if (!target) return false;
    attacker->attack.target = target;
    const char *sprite_name = "(unknown)";
    if (gameinfo && attacker->core.sprite_id >= 0 &&
        attacker->core.sprite_id < gameinfo->sprite_count &&
        gameinfo->sprnames && gameinfo->sprnames[attacker->core.sprite_id]) {
        sprite_name = gameinfo->sprnames[attacker->core.sprite_id];
    }
    debug_effects_log("shoot fire unit_type=%u state=%d facing_code=%d dir_slot=%d sprite=%s frame=%d target=%d",
                      attacker->type_id, attacker->core.state_id,
                      angle_to_direction(attacker->core.angle, 32, ANG90, true),
                      0, sprite_name, attacker->core.frame, target->id);

    if (attacker->info && attacker->info->attack.projectile_type != 0) {
        mobj_t *missile = P_SpawnMissile(attacker, target,
                                         attacker->info->attack.projectile_type);
        if (!missile) return false;
        if (mobj_attack_cooldown_ms(attacker) > 0)
            attacker->attack.cooldown_left_ms = mobj_attack_cooldown_ms(attacker);
        debug_effects_log("missile launch source=%d type=%u target=%d speed=%d",
                          attacker->id, missile->type_id, target->id,
                          gameinfo->mobjinfo[missile->type_id].speed);
        return true;
    }

    damage_mobj(target, mobj_attack_damage(attacker));
    if (mobj_attack_cooldown_ms(attacker) > 0)
        attacker->attack.cooldown_left_ms = mobj_attack_cooldown_ms(attacker);
    debug_effects_log("state attack attacker_type=%u target=%d damage=%d hp=%d/%d",
                      attacker->type_id, target->id, mobj_attack_damage(attacker),
                      target->hp, target->max_hp);
    return true;
}

void A_Attack(mobj_t *unit) {
    (void)P_Attack(unit);
}

void A_Look(mobj_t *unit) {
    if (!unit || unit->hp <= 0 || !(unit->traits & MF_ATTACK) ||
        unit->attack.cooldown_left_ms > 0 || !gameinfo ||
        unit->type_id >= gameinfo->mobj_type_count) return;
    mobj_t *target = NULL;
    float best = mobj_attack_range(unit) * mobj_attack_range(unit);
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *candidate = (mobj_t *)th;
        if (candidate == unit || candidate->remove || candidate->hp <= 0 ||
            (candidate->traits & (MF_NOBLOCKMAP | MF_MISSILE)) ||
            P_IsAlly(unit, candidate)) continue;
        float distance = fvec2_distance_squared(fixed3_xy_to_fvec2(candidate->core.position),
                                                fixed3_xy_to_fvec2(unit->core.position));
        if (distance <= best) { best = distance; target = candidate; }
    }
    if (!target) return;
    unit->attack.target = target;
    fvec2_t delta = fvec2_sub(fixed3_xy_to_fvec2(target->core.position),
                            fixed3_xy_to_fvec2(unit->core.position));
    unit->core.angle = angle_from_map_vector(&level, delta.x, delta.y);
    int attack_state = gameinfo->mobjinfo[unit->type_id].missilestate;
    if (attack_state != gameinfo->null_state) P_SetMobjState(unit, attack_state);
}

void A_Chase(mobj_t *unit) {
    /* Movement runs in P_Ticker; state entry checks for an attack. */
    A_Look(unit);
}

static bool move_unit_if_walkable(const level_t *map, mobj_t *unit,
                                  fvec2_t displacement) {
    if (!unit) return false;
    fixed3_t momentum = fixed3_planar_delta(displacement);
    if (unit->traits & MF_FLY) {
        unit->core.momentum = momentum;
        unit->core.position = fixed3_add_planar(unit->core.position,
                                                   unit->core.momentum);
        return true;
    }
    fixed3_t candidate = fixed3_add_planar(unit->core.position, momentum);
    fvec2_t candidate_xy = fixed3_xy_to_fvec2(candidate);
    if (P_CheckPosition(map, unit, candidate_xy.x, candidate_xy.y)) {
        unit->core.momentum = momentum;
        unit->core.position = fixed3_add_planar(unit->core.position,
                                                   unit->core.momentum);
        return true;
    }
    momentum.y = 0;
    candidate = fixed3_add_planar(unit->core.position, momentum);
    candidate_xy = fixed3_xy_to_fvec2(candidate);
    if (momentum.x != 0 && P_CheckPosition(map, unit, candidate_xy.x, candidate_xy.y)) {
        unit->core.momentum = momentum;
        unit->core.position = fixed3_add_planar(unit->core.position,
                                                   unit->core.momentum);
        return true;
    }
    momentum = fixed3_planar_delta((fvec2_t){ 0.0f, displacement.y });
    candidate = fixed3_add_planar(unit->core.position, momentum);
    candidate_xy = fixed3_xy_to_fvec2(candidate);
    if (momentum.y != 0 && P_CheckPosition(map, unit, candidate_xy.x, candidate_xy.y)) {
        unit->core.momentum = momentum;
        unit->core.position = fixed3_add_planar(unit->core.position,
                                                   unit->core.momentum);
        return true;
    }
    unit->core.momentum = fixed3_zero();
    return false;
}

static bool missile_hits_mobj(const mobj_t *missile, const mobj_t *candidate,
                              fvec2_t start, fvec2_t end, float *along_out) {
    if (!missile || !candidate || candidate == missile || candidate == missile->target ||
        candidate->remove || candidate->hp <= 0 ||
        (candidate->traits & (MF_NOBLOCKMAP | MF_MISSILE)) ||
        P_IsAlly(missile, candidate)) return false;

    fvec2_t target_position = fixed3_xy_to_fvec2(candidate->core.position);
    fvec2_t path = fvec2_sub(end, start);
    float path_length2 = fvec2_length_squared(path);
    float along = 0.0f;
    if (path_length2 > 0.0001f) {
        fvec2_t to_target = fvec2_sub(target_position, start);
        along = (to_target.x * path.x + to_target.y * path.y) / path_length2;
        if (along < 0.0f) along = 0.0f;
        if (along > 1.0f) along = 1.0f;
    }
    fvec2_t closest = fvec2_add(start, fvec2_scale(path, along));
    float radius = P_MobjRadius(missile) + P_MobjRadius(candidate);
    if (fvec2_distance_squared(closest, target_position) > radius * radius) return false;
    if (along_out) *along_out = along;
    return true;
}

static mobj_t *missile_collision(const mobj_t *missile, fvec2_t start, fvec2_t end) {
    mobj_t *hit = NULL;
    float nearest = 2.0f;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *candidate = (mobj_t *)th;
        float along = 0.0f;
        if (missile_hits_mobj(missile, candidate, start, end, &along) && along < nearest) {
            nearest = along;
            hit = candidate;
        }
    }
    return hit;
}

static void tick_missile(mobj_t *missile) {
    if (!missile || missile->remove) return;

    fvec2_t start = fixed3_xy_to_fvec2(missile->core.position);
    fvec2_t step = fixed3_xy_to_fvec2(missile->core.momentum);
    fvec2_t end = fvec2_add(start, step);
    float distance = sqrtf(fvec2_length_squared(step));
    if (distance > 0.0001f) {
        if (level.width > 0 && !P_CheckPosition(&level, missile, end.x, end.y)) {
            P_ExplodeMissile(missile);
            return;
        }
        mobj_t *hit = missile_collision(missile, start, end);
        if (hit) {
            int damage = gameinfo->mobjinfo[missile->type_id].damage;
            damage_mobj(hit, damage);
            debug_effects_log("missile impact missile=%d target=%d damage=%d hp=%d/%d",
                              missile->id, hit->id, damage, hit->hp, hit->max_hp);
            P_ExplodeMissile(missile);
            return;
        }
        missile->core.position = fixed3_add_planar(missile->core.position,
                                                   missile->core.momentum);
    }
    P_TickMobjState(missile);
}

static bool unit_has_move_order(const mobj_t *unit) {
    return unit && (unit->movement.flow_field ||
                    ((unit->traits & MF_FLY) && unit->movement.order_id)) &&
           !unit->movement.order_arrived;
}

static bool final_goal_reaches_arrived_order_cluster(const mobj_t *unit,
                                                     float tx, float ty, float dist_to_goal) {
    if (unit->movement.order_id == 0) return false;
    float radius = P_MobjRadius(unit);

    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *other = (mobj_t *)th;
        if (other == unit) continue;
        if (other->remove || other->hp <= 0 || other->movement.order_id != unit->movement.order_id) {
            continue;
        }
        if (!other->movement.order_arrived) continue;

        float min_dist = radius + P_MobjRadius(other);
        fvec2_t other_position = fixed3_xy_to_fvec2(other->core.position);
        float goal_dist2 = fvec2_distance_squared(other_position,
                                                  (fvec2_t){ tx, ty });
        float unit_dist2 = fvec2_distance_squared(
            other_position, fixed3_xy_to_fvec2(unit->core.position));
        float contact_dist = min_dist + 0.20f;
        if (goal_dist2 < min_dist * min_dist &&
            dist_to_goal <= contact_dist) {
            return true;
        }
        if (unit_dist2 <= contact_dist * contact_dist) {
            return true;
        }
    }
    return false;
}

static float unit_harvest_interaction_radius_cells(const mobj_t *unit) {
    float radius = P_MobjRadius(unit) + 0.55f;
    if (radius < 0.75f) radius = 0.75f;
    if (radius > 1.10f) radius = 1.10f;
    return radius;
}

static bool update_unit_harvest(level_t *map,
                                mobj_t *unit, int dt_ms, const gameinfo_t *game_info) {
    if (!map || !unit || (unit->traits & MF_HARVESTER) == 0 ||
        unit->harvest.phase == HARVEST_PHASE_NONE || unit->harvest.target < 0) {
        return false;
    }
    if (unit->harvest.target >= map->resource_vent_count || !map->resource_vents) {
        unit->harvest.phase = HARVEST_PHASE_NONE;
        return false;
    }

    resourcevent_t *vent = &map->resource_vents[unit->harvest.target];
    if (unit->harvest.phase == HARVEST_PHASE_TO_BASE) {
        if (fvec2_distance_squared(unit->harvest.return_position,
                                   fixed3_xy_to_fvec2(unit->core.position)) > 1.0f) return false;
        int owner = unit->owner < 8 ? unit->owner : 0;
        int rtype = vent->resource_type < RTS_MAX_RESOURCES ? vent->resource_type : 0;
        map->player_resources[owner][rtype] += unit->harvest.cargo;
        unit->harvest.cargo = 0;
        if (!vent->active || vent->rate <= 0 || vent->amount <= 0) {
            unit->harvest.target = -1;
            unit->harvest.timer_ms = 0;
            unit->harvest.phase = HARVEST_PHASE_NONE;
            return false;
        }
        fvec2_t vent_center = fvec2_cell_center(vent->cell);
        if (!P_MoveUnitTo(map, unit, vent_center)) return false;
        unit->harvest.phase = HARVEST_PHASE_TO_MINE;
        return false;
    }
    if (unit->harvest.phase == HARVEST_PHASE_TURNING) {
        fvec2_t att_delta = fvec2_sub(
            vent->attachment, fixed3_xy_to_fvec2(unit->core.position));
        if (fvec2_length_squared(att_delta) > 0.000001f) {
            angle_t desired = angle_from_map_vector(map, att_delta.x, att_delta.y);
            if (angle_distance(desired, unit->core.angle) >= ANG45 / 8u) {
                unit->movement.turn_timer_ms -= dt_ms;
                if (unit->movement.turn_timer_ms <= 0) {
                    int32_t da = (int32_t)(desired - unit->core.angle);
                    unit->core.angle += da > 0 ? ANG45 / 4u : 0u - ANG45 / 4u;
                    unit->movement.turn_timer_ms = RTS_TURN_STEP_MS;
                }
                return false;
            }
            unit->core.angle = desired;
        }
        unit->movement.turn_timer_ms = 0;
        unit->harvest.phase = HARVEST_PHASE_MINING;
        if (game_info && mobj_harvest_state(unit) > 0 &&
            mobj_harvest_state(unit) < game_info->state_count) {
            P_SetMobjState(unit, mobj_harvest_state(unit));
        }
        return false;
    }
    if (!vent->active || vent->rate <= 0 || vent->amount <= 0) {
        if (mobj_harvest_capacity(unit) > 0 && unit->harvest.cargo > 0) {
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                    mobj_t *base = (mobj_t *)th;
                    if (base->remove) continue;
                if (!P_AreAllegiancesAllied(base->allegiance, unit->allegiance) ||
                    (base->traits & MF_RESOURCE_BASE) == 0 || base->hp <= 0) continue;
                fvec2_t base_position = fixed3_xy_to_fvec2(base->core.position);
                unit->harvest.return_position = base_position;
                if (P_MoveUnitTo(map, unit, base_position)) {
                    unit->harvest.return_position = unit->movement.goal;
                    unit->harvest.phase = HARVEST_PHASE_TO_BASE;
                    return false;
                }
            }
        }
        unit->harvest.target = -1;
        unit->harvest.timer_ms = 0;
        unit->harvest.phase = HARVEST_PHASE_NONE;
        return false;
    }

    fvec2_t attachment_delta = fvec2_sub(
        vent->attachment, fixed3_xy_to_fvec2(unit->core.position));
    float interaction_radius = unit_harvest_interaction_radius_cells(unit);
    if (unit_has_move_order(unit)) return false;
    if (fvec2_length_squared(attachment_delta) > interaction_radius * interaction_radius)
        return false;

    unit->movement.flow_field = NULL;
    unit->movement.order_arrived = true;
    unit->core.momentum = fixed3_zero();
    unit->attack.target = NULL;
    if (unit->harvest.phase != HARVEST_PHASE_MINING &&
        unit->harvest.phase != HARVEST_PHASE_TURNING) {
        unit->harvest.phase = HARVEST_PHASE_TURNING;
        unit->movement.turn_timer_ms = RTS_TURN_STEP_MS;
    }
    unit->harvest.timer_ms += dt_ms;
    while (unit->harvest.timer_ms >= RTS_HARVEST_INTERVAL_MS && vent->amount > 0) {
        unit->harvest.timer_ms -= RTS_HARVEST_INTERVAL_MS;
        int take = vent->rate;
        if (take > vent->amount) take = vent->amount;
        int owner = unit->owner < 8 ? unit->owner : 0;
        int rtype = vent->resource_type < RTS_MAX_RESOURCES ? vent->resource_type : 0;
        vent->amount -= take;
        if (mobj_harvest_capacity(unit) > 0)
            unit->harvest.cargo += take;
        else
            map->player_resources[owner][rtype] += take;
        if (mobj_harvest_capacity(unit) > 0 &&
            unit->harvest.cargo >= mobj_harvest_capacity(unit)) {
            bool sent_home = false;
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                    mobj_t *base = (mobj_t *)th;
                    if (base->remove) continue;
                if (!P_AreAllegiancesAllied(base->allegiance, unit->allegiance) ||
                    (base->traits & MF_RESOURCE_BASE) == 0 || base->hp <= 0) continue;
                fvec2_t base_position = fixed3_xy_to_fvec2(base->core.position);
                unit->harvest.return_position = base_position;
                sent_home = P_MoveUnitTo(map, unit, base_position);
                if (sent_home) {
                    unit->harvest.return_position = unit->movement.goal;
                }
                if (sent_home) break;
            }
            if (sent_home) {
                unit->harvest.phase = HARVEST_PHASE_TO_BASE;
                break;
            }
        }
        if (vent->amount <= 0) {
            vent->active = false;
            if (mobj_harvest_capacity(unit) > 0 && unit->harvest.cargo > 0) {
                unit->harvest.phase = HARVEST_PHASE_TO_BASE;
                for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                    mobj_t *base = (mobj_t *)th;
                    if (base->remove) continue;
                    if (!P_AreAllegiancesAllied(base->allegiance, unit->allegiance) ||
                        (base->traits & MF_RESOURCE_BASE) == 0 || base->hp <= 0) continue;
                    fvec2_t base_position = fixed3_xy_to_fvec2(base->core.position);
                    unit->harvest.return_position = base_position;
                    if (P_MoveUnitTo(map, unit, base_position)) {
                        unit->harvest.return_position = unit->movement.goal;
                        break;
                    }
                }
            } else {
                unit->harvest.target = -1;
                unit->harvest.timer_ms = 0;
                unit->harvest.phase = HARVEST_PHASE_NONE;
                if (game_info && mobj_harvest_state(unit) > 0)
                    P_SetMobjState(unit, game_info->mobjinfo[unit->type_id].spawnstate);
            }
            break;
        }
    }
    return true;
}

static void tick_actor(mobj_t *u) {
    level_t *map = &level;
    const gameinfo_t *game_info = gameinfo;
    float dt = FIXED_DT;
    int dt_ms = (int)lroundf(dt * 1000.0f);
    u->core.momentum = fixed3_zero();
    if (u->remove) return;
    if (u->core.state_id <= 0) P_InitMobj(game_info, u);
    P_TickMobjState(u);
    if (u->remove || u->hp <= 0) return;

    if (u->attack.cooldown_left_ms > 0) {
        u->attack.cooldown_left_ms -= dt_ms;
        if (u->attack.cooldown_left_ms < 0) u->attack.cooldown_left_ms = 0;
    }

    bool moving = unit_has_move_order(u);
    {
        const state_t *s = state_at(game_info, u->core.state_id);
        bool in_attack = s && s->group == 3;
        if (moving && !in_attack) {
            mobj_t *target = attack_target_in_range(u);
            if (target) {
                u->attack.target = target;
                fvec2_t target_delta = fvec2_sub(
                    fixed3_xy_to_fvec2(target->core.position),
                    fixed3_xy_to_fvec2(u->core.position));
                u->core.angle = angle_from_map_vector(map,
                                                 target_delta.x,
                                                 target_delta.y);
                u->movement.flow_field = NULL;
                u->movement.order_id = 0;
                u->movement.order_arrived = false;
                moving = false;
            }
        }
    }
    fvec2_t move_target = u->movement.goal;
    bool final = true;
    if (moving && !(u->traits & MF_FLY) && !P_FlowFieldTarget(
            map, u->movement.flow_field,
            fixed3_xy_to_fvec2(u->core.position), u->movement.goal,
            P_MobjRadius(u), &move_target, &final)) {
        u->movement.flow_field = NULL;
        u->movement.order_arrived = false;
        moving = false;
    }
    /* Turn-in-place before moving. */
    if (moving) {
        fvec2_t delta = fvec2_sub(
            move_target, fixed3_xy_to_fvec2(u->core.position));
        float dist = sqrtf(fvec2_length_squared(delta));
        if (dist >= 0.001f) {
            angle_t desired = angle_from_map_vector(map, delta.x, delta.y);
            if (angle_distance(desired, u->core.angle) >= ANG45 / 8u) {
                u->movement.turn_timer_ms -= dt_ms;
                if (u->movement.turn_timer_ms <= 0) {
                    int32_t delta = (int32_t)(desired - u->core.angle);
                    u->core.angle += delta > 0 ? ANG45 / 4u : 0u - ANG45 / 4u;
                    u->movement.turn_timer_ms = RTS_TURN_STEP_MS;
                }
                moving = false;
            } else {
                u->movement.turn_timer_ms = 0;
            }
        }
    }
    if (moving) {
        fvec2_t delta = fvec2_sub(
            move_target, fixed3_xy_to_fvec2(u->core.position));
        float dist = sqrtf(fvec2_length_squared(delta));
        if (final && !(u->traits & MF_FLY) && final_goal_reaches_arrived_order_cluster(
                u, move_target.x, move_target.y, dist)) {
            u->movement.goal = fixed3_xy_to_fvec2(u->core.position);
            u->movement.flow_field = NULL;
            u->movement.order_arrived = true;
            moving = false;
        } else {
            if (dist >= 0.001f)
                u->core.angle = angle_from_map_vector(map, delta.x, delta.y);
            float step = u->speed * dt;
            if (dist <= step || dist < 0.001f) {
                if (move_unit_if_walkable(map, u, delta)) {
                    if (final) {
                        u->movement.flow_field = NULL;
                        u->movement.order_arrived = true;
                        moving = false;
                    }
                } else {
                    u->movement.flow_field = NULL;
                    u->movement.order_arrived = false;
                    moving = false;
                }
            } else {
                fvec2_t displacement = fvec2_scale(delta, step / dist);
                if (!move_unit_if_walkable(map, u, displacement)) {
                    u->movement.flow_field = NULL;
                    u->movement.order_arrived = false;
                    moving = false;
                }
            }
        }
    }

    if (update_unit_harvest(map, u, dt_ms, game_info)) {
        moving = false;
        u->core.momentum = fixed3_zero();
    }

    if (game_info && (u->traits & MF_MOBILE) && u->type_id > 0 &&
        u->type_id < game_info->mobj_type_count) {
        const mobjinfo_t *mi = &game_info->mobjinfo[u->type_id];
        const state_t *state = state_at(game_info, u->core.state_id);
        int group = state ? state->group : 0;
        if (group != 3) {
            if (moving && group != 2) {
                P_SetMobjState(u, mi->seestate);
            } else if (!moving && group == 2 &&
                       !((u->traits & MF_FLY) && unit_has_move_order(u))) {
                P_SetMobjState(u, mi->spawnstate);
            } else {
                apply_state_visuals(game_info, &u->core,
                                    state_at(game_info, u->core.state_id), false);
            }
        }
    }
    if ((!gameinfo || !gameinfo->states) && u->attack.cooldown_left_ms <= 0)
        P_Attack(u);
}

void P_MobjThinker(mobj_t *mobj) {
    if (mobj->remove) { P_RemoveMobj(mobj); return; }
    if (mobj->traits & MF_MISSILE) {
        tick_missile(mobj);
        return;
    }
    tick_actor(mobj);
}
