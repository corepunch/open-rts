#define _DEFAULT_SOURCE
#include "p_local.h"

enum {
    RTS_HARVEST_INTERVAL_MS = 1000,
    RTS_TURN_STEP_MS = 75,
};

static const state_t *state_at(const gameinfo_t *game_info, int state_id) {
    if (!game_info || !game_info->states || state_id < 0 || state_id >= game_info->state_count)
        return NULL;
    return &game_info->states[state_id];
}

static void apply_state_visuals(const gameinfo_t *game_info, mobjcore_t *mobj,
                                const state_t *state, bool apply_offsets) {
    if (!game_info || !mobj || !state) return;
    mobj->sprite_id = state->sprite;
    mobj->frame = state->frame;
    mobj->render_flags = (uint32_t)state->misc2;
    mobj->render_remap = 0;
    mobj->render_intensity = 16;
    if (apply_offsets) mobj->render_offset = (ivec2_t){ 0, 0 };
    if (mobj->sprite_id >= 0 && mobj->sprite_id < game_info->sprite_count &&
        game_info->sprnames && game_info->sprnames[mobj->sprite_id]) {
        snprintf(mobj->sprite_name, sizeof(mobj->sprite_name), "%s",
                 game_info->sprnames[mobj->sprite_id]);
    }
}

bool P_SetMobjState(statecontext_t *ctx, mobj_t *unit, int state_id) {
    const gameinfo_t *game_info = ctx ? ctx->game_info : NULL;
    if (!game_info || !unit) return false;
    int guard = 0;
    while (guard++ < game_info->state_count + 1) {
        if (state_id == game_info->null_state || state_id < 0 ||
            state_id >= game_info->state_count) {
            unit->core.state_id = game_info->null_state;
            unit->core.tics = 0;
            unit->core.momentum = fixedvec3_zero();
            unit->remove = true;
            return false;
        }
        const state_t *state = &game_info->states[state_id];
        unit->core.state_id = state_id;
        unit->core.tics = state->tics;
        apply_state_visuals(game_info, &unit->core, state, false);
        debug_effects_log("state unit type=%u state=%d sprite=%d frame=%d tics=%d",
                          unit->type_id, unit->core.state_id, unit->core.sprite_id,
                          unit->core.frame, unit->core.tics);
        if (state->misc1 == 3) {
            const char *sprite_name = "(unknown)";
            if (unit->core.sprite_id >= 0 && unit->core.sprite_id < game_info->sprite_count &&
                game_info->sprnames && game_info->sprnames[unit->core.sprite_id]) {
                sprite_name = game_info->sprnames[unit->core.sprite_id];
            }
            debug_effects_log("shoot state unit_type=%u state=%d sprite=%s frame=%d",
                              unit->type_id, unit->core.state_id,
                              sprite_name, unit->core.frame);
        }
        if (state->action) state->action(ctx, unit);
        if (unit->remove || unit->core.state_id != state_id) return !unit->remove;
        if (unit->core.tics != 0) return true;
        state_id = state->nextstate;
    }
    unit->remove = true;
    return false;
}

bool P_TickMobjState(statecontext_t *ctx, mobj_t *unit) {
    if (!ctx || !unit || unit->remove) return false;
    if (unit->core.state_id <= 0) return false;
    if (unit->core.tics > 0) unit->core.tics--;
    if (unit->core.tics != 0) return true;
    const state_t *state = state_at(ctx->game_info, unit->core.state_id);
    return P_SetMobjState(ctx, unit,
                          state ? state->nextstate : ctx->game_info->null_state);
}

void P_ApplyActorTypeDefaults(mobj_t *unit, const actortype_t *type) {
    if (!unit || !type) return;
    unit->type_id = type->id;
    unit->traits = type->traits;
    unit->harvest.capacity = type->harvest.capacity;
    if (unit->speed <= 0.0f) unit->speed = type->speed;
    if (unit->max_hp <= 0) unit->max_hp = type->max_hp;
    if (unit->hp <= 0) unit->hp = unit->max_hp;
    if (unit->attack.range <= 0.0f) unit->attack.range = type->attack.range;
    if (unit->attack.damage <= 0) unit->attack.damage = type->attack.damage;
    if (unit->attack.cooldown_ms <= 0) unit->attack.cooldown_ms = type->attack.cooldown_ms;
    if (unit->attack.anim_ms <= 0) unit->attack.anim_ms = type->attack.anim_ms;
    if (unit->death.anim_ms <= 0) unit->death.anim_ms = type->death.anim_ms;
    if (unit->harvest.state_id <= 0) unit->harvest.state_id = type->harvest.state_id;
    if (unit->muzzle_flash_ms <= 0) unit->muzzle_flash_ms = type->muzzle_flash_ms;
    if (unit->core.render_intensity == 0) unit->core.render_intensity = 16;
    if (unit->attack.target <= 0) unit->attack.target = -1;
    if (unit->harvest.target == 0) unit->harvest.target = -1;
    if (unit->core.sprite_name[0] == '\0' && type->sprite_name)
        snprintf(unit->core.sprite_name, sizeof(unit->core.sprite_name), "%s", type->sprite_name);
    if (unit->shadow_name[0] == '\0' && type->shadow_name)
        snprintf(unit->shadow_name, sizeof(unit->shadow_name), "%s", type->shadow_name);
    if (unit->muzzle_flash_sprite < 0)
        unit->muzzle_flash_sprite = type->muzzle_flash_sprite;
    if (unit->hit_effect_sprite < 0)
        unit->hit_effect_sprite = type->hit_effect_sprite;
    if (unit->muzzle_flash_name[0] == '\0' && type->muzzle_flash_name)
        snprintf(unit->muzzle_flash_name, sizeof(unit->muzzle_flash_name),
                 "%s", type->muzzle_flash_name);
    if (unit->hit_effect_name[0] == '\0' && type->hit_effect_name)
        snprintf(unit->hit_effect_name, sizeof(unit->hit_effect_name),
                 "%s", type->hit_effect_name);
    if (!unit->death_effect_action)
        unit->death_effect_action = type->death_effect_action;
}

static bool set_effect_state(const gameinfo_t *game_info, effect_t *effect,
                                 int state_id) {
    if (!game_info || !effect) return false;
    int guard = 0;
    while (guard++ < game_info->state_count + 1) {
        if (state_id == game_info->null_state || state_id < 0 ||
            state_id >= game_info->state_count) {
            memset(effect, 0, sizeof(*effect));
            return false;
        }
        const state_t *state = &game_info->states[state_id];
        effect->core.state_id = state_id;
        effect->core.tics = state->tics;
        apply_state_visuals(game_info, &effect->core, state, true);
        if (effect->core.tics != 0) return true;
        state_id = state->nextstate;
    }
    memset(effect, 0, sizeof(*effect));
    return false;
}

void P_SpawnMobj(const gameinfo_t *game_info, mobj_t *unit) {
    if (!game_info || !unit || !game_info->mobjinfo ||
        unit->type_id <= 0 || unit->type_id >= game_info->mobj_type_count) {
        return;
    }
    if (unit->core.render_intensity == 0) unit->core.render_intensity = 16;
    const mobjinfo_t *info = &game_info->mobjinfo[unit->type_id];
    if (unit->max_hp <= 0) unit->max_hp = info->spawnhealth;
    if (unit->hp <= 0) unit->hp = unit->max_hp;
    if (unit->speed <= 0.0f) unit->speed = (float)info->speed;
    if (unit->radius <= 0.05f) {
        unit->radius = (float)info->radius / 32.0f;
        if (unit->radius < 0.32f) unit->radius = 0.32f;
        if (unit->radius > 0.90f) unit->radius = 0.90f;
    }
    if (unit->core.state_id <= 0) {
        statecontext_t ctx = { .game_info = game_info };
        P_SetMobjState(&ctx, unit, info->spawnstate);
    } else {
        apply_state_visuals(game_info, &unit->core,
                            state_at(game_info, unit->core.state_id), false);
    }
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

static bool unit_has_attack_target_in_range(const mobj_t *attacker, const mobj_t *units, int unit_count,
                                            int *target_index_out) {
    if (target_index_out) *target_index_out = -1;
    if (!attacker || !units || unit_count <= 0 ||
        (attacker->traits & MF_ATTACK) == 0 ||
        attacker->attack.damage <= 0 || attacker->attack.range <= 0.0f) {
        return false;
    }

    int preferred = attacker->attack.target;
    if (preferred >= 0 && preferred < unit_count) {
        const mobj_t *target = &units[preferred];
        if (!target->remove && target->hp > 0 && !P_IsAlly(attacker, target)) {
            if (fvec2_distance_squared(fixedvec3_xy_to_fvec2(target->core.position),
                                       fixedvec3_xy_to_fvec2(attacker->core.position)) <=
                attacker->attack.range * attacker->attack.range) {
                if (target_index_out) *target_index_out = preferred;
                return true;
            }
        }
    }

    int best = -1;
    float best_dist2 = attacker->attack.range * attacker->attack.range;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *candidate = &units[i];
        if (candidate == attacker || candidate->remove || candidate->hp <= 0 ||
            P_IsAlly(attacker, candidate)) {
            continue;
        }
        float dist2 = fvec2_distance_squared(
            fixedvec3_xy_to_fvec2(candidate->core.position),
            fixedvec3_xy_to_fvec2(attacker->core.position));
        if (dist2 <= best_dist2) {
            best_dist2 = dist2;
            best = i;
        }
    }
    if (best < 0) return false;
    if (target_index_out) *target_index_out = best;
    return true;
}

static bool spawn_visual_effect(effect_t *effects, int max_effects,
                                const char *sprite_name,
                                fixedvec3_t position, angle_t angle, int duration_ms,
                                int frame_ms, bool fin_placement,
                                bool add_decoration_on_finish,
                                int decoration_frame_index) {
    if (!effects || max_effects <= 0 || !sprite_name || sprite_name[0] == '\0') {
        debug_effects_log("spawn skipped sprite=%s max=%d", sprite_name ? sprite_name : "(null)", max_effects);
        return false;
    }
    for (int i = 0; i < max_effects; ++i) {
        effect_t *effect = &effects[i];
        if (effect->active) continue;
        memset(effect, 0, sizeof(*effect));
        effect->active = true;
        effect->core.position = position;
        effect->core.angle = angle;
        effect->core.render_intensity = 16;
        effect->fin_placement = fin_placement;
        effect->duration_ms = duration_ms > 0 ? duration_ms : 120;
        effect->frame_ms = frame_ms > 0 ? frame_ms : 90;
        effect->decoration_frame_index = decoration_frame_index;
        effect->add_decoration_on_finish = add_decoration_on_finish;
        snprintf(effect->core.sprite_name, sizeof(effect->core.sprite_name), "%s", sprite_name);
        fvec2_t position_xy = fixedvec3_xy_to_fvec2(position);
        debug_effects_log("spawn slot=%d sprite=%s pos=%.2f,%.2f facing=%d duration=%d frame_ms=%d corpse=%d",
                          i, effect->core.sprite_name,
                          position_xy.x, position_xy.y,
                          angle_to_direction(effect->core.angle, 32, ANG90, true),
                          effect->duration_ms, effect->frame_ms,
                          effect->add_decoration_on_finish ? 1 : 0);
        return true;
    }
    debug_effects_log("spawn failed no free slot sprite=%s", sprite_name);
    return false;
}

static bool spawn_ground_light(effect_t *effects, int max_effects,
                               fixedvec3_t position, int duration_ms, int radius) {
    if (!effects || max_effects <= 0) return false;
    for (int i = 0; i < max_effects; ++i) {
        effect_t *effect = &effects[i];
        if (effect->active) continue;
        memset(effect, 0, sizeof(*effect));
        effect->active = true;
        effect->ground_light = true;
        effect->core.position = position;
        effect->duration_ms = duration_ms > 0 ? duration_ms : 120;
        effect->light_radius = radius > 0 ? radius : 28;
        return true;
    }
    return false;
}

bool P_SpawnEffect(statecontext_t *ctx, int state_id, fixedvec3_t position, angle_t angle) {
    if (!ctx || !ctx->effects || ctx->max_effects <= 0 || !ctx->game_info) return false;
    for (int i = 0; i < ctx->max_effects; ++i) {
        effect_t *effect = &ctx->effects[i];
        if (effect->active) continue;
        memset(effect, 0, sizeof(*effect));
        effect->active = true;
        effect->use_state = true;
        effect->core.position = position;
        effect->core.angle = angle;
        effect->core.render_intensity = 16;
        bool ok = set_effect_state(ctx->game_info, effect, state_id);
        debug_effects_log("spawn state effect slot=%d state=%d ok=%d sprite=%s frame=%d",
                          i, state_id, ok ? 1 : 0, effect->core.sprite_name, effect->core.frame);
        return ok;
    }
    debug_effects_log("spawn state effect failed no free slot state=%d", state_id);
    return false;
}

static void add_effect_finish_decoration(level_t *map, const effect_t *effect) {
    if (!map || !effect || !effect->add_decoration_on_finish ||
        effect->core.sprite_name[0] == '\0' || map->decoration_count >= MAX_DECORATIONS) {
        return;
    }
    mapdecoration_t *decorations = realloc(map->decorations,
                                         (size_t)(map->decoration_count + 1) * sizeof(mapdecoration_t));
    if (!decorations) return;
    map->decorations = decorations;
    mapdecoration_t *dec = &map->decorations[map->decoration_count++];
    memset(dec, 0, sizeof(*dec));
    fvec2_t position = fixedvec3_xy_to_fvec2(effect->core.position);
    dec->cell = (ivec2_t){ (int)floorf(position.x), (int)floorf(position.y) };
    dec->footprint = (isize2_t){ 1, 1 };
    dec->center_anchor = true;
    dec->frame_index = effect->decoration_frame_index;
    dec->angle = effect->core.angle;
    snprintf(dec->sprite_name, sizeof(dec->sprite_name), "%s", effect->core.sprite_name);
    debug_effects_log("corpse decoration sprite=%s grid=%d,%d facing=%d frame_index=%d count=%d",
                      dec->sprite_name, dec->cell.x, dec->cell.y,
                      angle_to_direction(dec->angle, 32, ANG90, true),
                      dec->frame_index, map->decoration_count);
}

bool P_AddCorpse(statecontext_t *ctx, const mobj_t *unit) {
    if (!ctx || !ctx->map || !unit || unit->core.sprite_name[0] == '\0' ||
        ctx->map->decoration_count >= MAX_DECORATIONS) {
        return false;
    }
    mapdecoration_t *decorations = realloc(ctx->map->decorations,
                                         (size_t)(ctx->map->decoration_count + 1) * sizeof(mapdecoration_t));
    if (!decorations) return false;
    ctx->map->decorations = decorations;
    mapdecoration_t *dec = &ctx->map->decorations[ctx->map->decoration_count++];
    memset(dec, 0, sizeof(*dec));
    fvec2_t position = fixedvec3_xy_to_fvec2(unit->core.position);
    dec->cell = (ivec2_t){ (int)floorf(position.x), (int)floorf(position.y) };
    dec->footprint = (isize2_t){ 1, 1 };
    dec->center_anchor = true;
    dec->frame_index = unit->core.frame;
    dec->angle = unit->core.angle;
    dec->render_flags = unit->core.render_flags;
    snprintf(dec->sprite_name, sizeof(dec->sprite_name), "%s", unit->core.sprite_name);
    debug_effects_log("corpse unit sprite=%s frame=%d flags=%u grid=%d,%d count=%d",
                      dec->sprite_name, dec->frame_index, dec->render_flags,
                      dec->cell.x, dec->cell.y, ctx->map->decoration_count);
    return true;
}

bool P_Attack(statecontext_t *ctx, mobj_t *attacker) {
    if (!ctx || !attacker || !ctx->mobjs || !ctx->mobj_count) return false;
    int count = *ctx->mobj_count;
    int target_index = attacker->attack.target;
    if (target_index < 0 || target_index >= count || ctx->mobjs[target_index].hp <= 0 ||
        P_IsAlly(attacker, &ctx->mobjs[target_index])) {
        target_index = -1;
        float best_dist2 = attacker->attack.range * attacker->attack.range;
        for (int i = 0; i < count; ++i) {
            mobj_t *candidate = &ctx->mobjs[i];
            if (candidate == attacker || candidate->hp <= 0 || P_IsAlly(attacker, candidate))
                continue;
            float dist2 = fvec2_distance_squared(
                fixedvec3_xy_to_fvec2(candidate->core.position),
                fixedvec3_xy_to_fvec2(attacker->core.position));
            if (dist2 <= best_dist2) {
                best_dist2 = dist2;
                target_index = i;
            }
        }
    }
    if (target_index < 0) return false;

    mobj_t *target = &ctx->mobjs[target_index];
    const char *sprite_name = "(unknown)";
    if (ctx->game_info && attacker->core.sprite_id >= 0 &&
        attacker->core.sprite_id < ctx->game_info->sprite_count &&
        ctx->game_info->sprnames && ctx->game_info->sprnames[attacker->core.sprite_id]) {
        sprite_name = ctx->game_info->sprnames[attacker->core.sprite_id];
    }
    debug_effects_log("shoot fire unit_type=%u state=%d facing_code=%d dir_slot=%d sprite=%s frame=%d target=%d",
                      attacker->type_id, attacker->core.state_id,
                      angle_to_direction(attacker->core.angle, 32, ANG90, true),
                      0, sprite_name, attacker->core.frame, target_index);
    target->hp -= attacker->attack.damage;
    if (attacker->attack.cooldown_ms > 0)
        attacker->attack.cooldown_left_ms = attacker->attack.cooldown_ms;
    /* A_DC_MuzzleFlash (fired on the attack state's frame) draws the flash sprite;
     * pair it with a ground light and, on the target, a hit/blood effect. Hidden
     * (not-yet-revealed) units must not leak a visible light or blood splash. */
    if (!attacker->hidden && attacker->muzzle_flash_name[0] != '\0') {
        int flash_ms = attacker->muzzle_flash_ms > 0 ? attacker->muzzle_flash_ms : 120;
        spawn_ground_light(ctx->effects, ctx->max_effects, attacker->core.position, flash_ms, 30);
    }
    if (!target->hidden && target->hit_effect_name[0] != '\0') {
        spawn_visual_effect(ctx->effects, ctx->max_effects, target->hit_effect_name,
                            target->core.position, target->core.angle, 400, 50, false, false, 0);
    }
    debug_effects_log("state attack attacker_type=%u target=%d damage=%d hp=%d/%d",
                      attacker->type_id, target_index, attacker->attack.damage,
                      target->hp, target->max_hp);
    if (target->hp <= 0) {
        target->hp = 0;
        target->selected = false;
        target->traits &= ~(MF_SELECTABLE | MF_MOBILE |
                            MF_ATTACK | MF_HARVESTER);
        target->movement.path_len = 0;
        target->movement.path_index = 0;
        target->movement.order_arrived = false;
        target->harvest.target = -1;
        target->harvest.timer_ms = 0;
        target->core.momentum = fixedvec3_zero();
        if (ctx->game_info && target->type_id > 0 &&
            target->type_id < ctx->game_info->mobj_type_count) {
            int deathstate = ctx->game_info->mobjinfo[target->type_id].deathstate;
            P_SetMobjState(ctx, target, deathstate);
        } else {
            target->remove = true;
        }
    }
    return true;
}

void A_Attack(statecontext_t *ctx, mobj_t *unit) {
    (void)P_Attack(ctx, unit);
}

void A_Walk(statecontext_t *ctx, mobj_t *unit) {
    if (!ctx || !unit || !ctx->mobjs || !ctx->mobj_count ||
        unit->hp <= 0 || (unit->traits & MF_ATTACK) == 0 ||
        unit->attack.range <= 0.0f || unit->attack.cooldown_left_ms > 0 ||
        !ctx->game_info || unit->type_id <= 0 ||
        unit->type_id >= ctx->game_info->mobj_type_count) {
        return;
    }
    int target = -1;
    float best_dist2 = unit->attack.range * unit->attack.range;
    for (int i = 0; i < *ctx->mobj_count; ++i) {
        mobj_t *candidate = &ctx->mobjs[i];
        if (candidate == unit || candidate->remove || candidate->hp <= 0 ||
            P_IsAlly(unit, candidate)) continue;
        float dist2 = fvec2_distance_squared(
            fixedvec3_xy_to_fvec2(candidate->core.position),
            fixedvec3_xy_to_fvec2(unit->core.position));
        if (dist2 <= best_dist2) {
            best_dist2 = dist2;
            target = i;
        }
    }
    if (target < 0) return;
    unit->attack.target = target;
    fvec2_t delta = fvec2_sub(
        fixedvec3_xy_to_fvec2(ctx->mobjs[target].core.position),
        fixedvec3_xy_to_fvec2(unit->core.position));
    unit->core.angle = angle_from_map_vector(ctx->map, delta.x, delta.y);
    int attack_state = ctx->game_info->mobjinfo[unit->type_id].missilestate;
    if (attack_state != ctx->game_info->null_state)
        P_SetMobjState(ctx, unit, attack_state);
}

void P_UpdateEffects(level_t *map, effect_t *effects, int max_effects,
                           const gameinfo_t *game_info, float dt) {
    if (!effects || max_effects <= 0) return;
    int dt_ms = (int)lroundf(dt * 1000.0f);
    for (int i = 0; i < max_effects; ++i) {
        effect_t *effect = &effects[i];
        if (!effect->active) continue;
        if (effect->use_state && game_info) {
            if (effect->core.tics > 0) effect->core.tics--;
            if (effect->core.tics == 0) {
                const state_t *state = state_at(game_info, effect->core.state_id);
                int next = state ? state->nextstate : game_info->null_state;
                set_effect_state(game_info, effect, next);
            }
            continue;
        }
        effect->age_ms += dt_ms;
        if (effect->age_ms < effect->duration_ms) continue;
        debug_effects_log("finish sprite=%s age=%d duration=%d corpse=%d",
                  effect->core.sprite_name,
                          effect->age_ms, effect->duration_ms,
                          effect->add_decoration_on_finish ? 1 : 0);
        add_effect_finish_decoration(map, effect);
        memset(effect, 0, sizeof(*effect));
    }
}

static void separate_units(const level_t *map, mobj_t *units, int count) {
    if (!units || count <= 1) return;
    for (int iter = 0; iter < 3; ++iter) {
        for (int i = 0; i < count; ++i) {
            mobj_t *a = &units[i];
            if (a->remove || a->hp <= 0 || (a->traits & MF_MOBILE) == 0) continue;
            for (int j = i + 1; j < count; ++j) {
                mobj_t *b = &units[j];
                if (b->remove || b->hp <= 0 || (b->traits & MF_MOBILE) == 0) continue;
                float min_dist = P_MobjRadius(a) + P_MobjRadius(b);
                fvec2_t a_position = fixedvec3_xy_to_fvec2(a->core.position);
                fvec2_t b_position = fixedvec3_xy_to_fvec2(b->core.position);
                fvec2_t delta = fvec2_sub(b_position, a_position);
                float dist2 = fvec2_length_squared(delta);
                if (dist2 >= min_dist * min_dist) continue;
                float dist = sqrtf(dist2);
                if (dist < 0.0001f) {
                    float angle = (float)((i * 37 + j * 17) % 360) * 0.01745329252f;
                    delta = (fvec2_t){ cosf(angle), sinf(angle) };
                    dist = 1.0f;
                }
                float push = (min_dist - dist) * 0.5f;
                fvec2_t separation = fvec2_scale(delta, push / dist);
                fvec2_t separated_a = fvec2_sub(a_position, separation);
                fvec2_t separated_b = fvec2_add(b_position, separation);
                if (P_CheckPosition(map, a, separated_a.x, separated_a.y)) {
                    fixedvec3_t before = a->core.position;
                    a->core.position = fixedvec3_with_xy(a->core.position, separated_a);
                    P_ClampToLevel(map, a);
                    a->core.momentum = fixedvec3_add(
                        a->core.momentum,
                        fixedvec3_planar_displacement(before, a->core.position));
                }
                if (P_CheckPosition(map, b, separated_b.x, separated_b.y)) {
                    fixedvec3_t before = b->core.position;
                    b->core.position = fixedvec3_with_xy(b->core.position, separated_b);
                    P_ClampToLevel(map, b);
                    b->core.momentum = fixedvec3_add(
                        b->core.momentum,
                        fixedvec3_planar_displacement(before, b->core.position));
                }
            }
        }
    }
}

static bool move_unit_if_walkable(const level_t *map, mobj_t *unit,
                                  fvec2_t displacement) {
    if (!unit) return false;
    fixedvec3_t momentum = fixedvec3_planar_delta(displacement);
    if (unit->traits & MF_FLY) {
        unit->core.momentum = momentum;
        unit->core.position = fixedvec3_add_planar(unit->core.position,
                                                   unit->core.momentum);
        return true;
    }
    fixedvec3_t candidate = fixedvec3_add_planar(unit->core.position, momentum);
    fvec2_t candidate_xy = fixedvec3_xy_to_fvec2(candidate);
    if (P_CheckPosition(map, unit, candidate_xy.x, candidate_xy.y)) {
        unit->core.momentum = momentum;
        unit->core.position = fixedvec3_add_planar(unit->core.position,
                                                   unit->core.momentum);
        return true;
    }
    momentum.y = 0;
    candidate = fixedvec3_add_planar(unit->core.position, momentum);
    candidate_xy = fixedvec3_xy_to_fvec2(candidate);
    if (momentum.x != 0 && P_CheckPosition(map, unit, candidate_xy.x, candidate_xy.y)) {
        unit->core.momentum = momentum;
        unit->core.position = fixedvec3_add_planar(unit->core.position,
                                                   unit->core.momentum);
        return true;
    }
    momentum = fixedvec3_planar_delta((fvec2_t){ 0.0f, displacement.y });
    candidate = fixedvec3_add_planar(unit->core.position, momentum);
    candidate_xy = fixedvec3_xy_to_fvec2(candidate);
    if (momentum.y != 0 && P_CheckPosition(map, unit, candidate_xy.x, candidate_xy.y)) {
        unit->core.momentum = momentum;
        unit->core.position = fixedvec3_add_planar(unit->core.position,
                                                   unit->core.momentum);
        return true;
    }
    unit->core.momentum = fixedvec3_zero();
    return false;
}

bool A_Move(mobj_t *unit, float dt) {
    if (!unit) return true;
    fvec2_t pos = fixedvec3_xy_to_fvec2(unit->core.position);
    fvec2_t delta = fvec2_sub(unit->movement.goal, pos);
    float dist = sqrtf(delta.x * delta.x + delta.y * delta.y);
    if (dist < 0.001f) return true;
    float step = unit->speed * dt;
    fvec2_t displacement;
    if (dist <= step)
        displacement = delta;
    else
        displacement = fvec2_scale(delta, step / dist);
    if (!move_unit_if_walkable(NULL, unit, displacement)) {
        unit->movement.path_len = 0;
        unit->movement.path_index = 0;
        unit->movement.order_arrived = false;
        return false;
    }
    return dist <= step;
}

static bool unit_is_following_path(const mobj_t *unit) {
    return unit && unit->movement.path_index > 0 && unit->movement.path_index < unit->movement.path_len;
}

static bool final_goal_reaches_arrived_order_cluster(const mobj_t *units, int count, int self_index,
                                                     float tx, float ty, float dist_to_goal) {
    if (!units || self_index < 0 || self_index >= count) return false;
    const mobj_t *unit = &units[self_index];
    if (unit->movement.order_id == 0) return false;
    float radius = P_MobjRadius(unit);

    for (int i = 0; i < count; ++i) {
        if (i == self_index) continue;
        const mobj_t *other = &units[i];
        if (other->remove || other->hp <= 0 || other->movement.order_id != unit->movement.order_id) {
            continue;
        }
        if (!other->movement.order_arrived) continue;

        float min_dist = radius + P_MobjRadius(other);
        fvec2_t other_position = fixedvec3_xy_to_fvec2(other->core.position);
        float goal_dist2 = fvec2_distance_squared(other_position,
                                                  (fvec2_t){ tx, ty });
        float unit_dist2 = fvec2_distance_squared(
            other_position, fixedvec3_xy_to_fvec2(unit->core.position));
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

static void update_resource_vent_smoke(level_t *map, const mobj_t *units, int unit_count) {
    if (!map || !map->resource_vents) return;
    for (int vent_index = 0; vent_index < map->resource_vent_count; ++vent_index) {
        resourcevent_t *vent = &map->resource_vents[vent_index];
        if (vent->smoke_decoration_index < 0 ||
            vent->smoke_decoration_index >= map->decoration_count) {
            continue;
        }
        bool attached = false;
        for (int unit_index = 0; unit_index < unit_count; ++unit_index) {
            const mobj_t *unit = &units[unit_index];
            if (!unit->remove && unit->hp > 0 &&
                unit->harvest.target == vent_index &&
                unit->harvest.phase == HARVEST_PHASE_MINING) {
                attached = true;
                break;
            }
        }
        map->decorations[vent->smoke_decoration_index].hidden = !vent->active || attached;
    }
}

static void deactivate_resource_vent(level_t *map, resourcevent_t *vent) {
    if (!map || !vent) return;
    vent->active = false;
    if (vent->decoration_index >= 0 && vent->decoration_index < map->decoration_count)
        map->decorations[vent->decoration_index].sprite_name[0] = '\0';
}

static bool update_unit_harvest(level_t *map, mobj_t *units, int unit_count,
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
                                   fixedvec3_xy_to_fvec2(unit->core.position)) > 1.0f) return false;
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
            vent->attachment, fixedvec3_xy_to_fvec2(unit->core.position));
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
        if (game_info && unit->harvest.state_id > 0 &&
            unit->harvest.state_id < game_info->state_count) {
            statecontext_t ctx = { .map = map, .game_info = game_info };
            P_SetMobjState(&ctx, unit, unit->harvest.state_id);
        }
        return false;
    }
    if (!vent->active || vent->rate <= 0 || vent->amount <= 0) {
        if (unit->harvest.capacity > 0 && unit->harvest.cargo > 0) {
            for (int i = 0; i < unit_count; ++i) {
                if (!P_AreAllegiancesAllied(units[i].allegiance, unit->allegiance) ||
                    (units[i].traits & MF_RESOURCE_BASE) == 0 || units[i].hp <= 0) continue;
                fvec2_t base_position = fixedvec3_xy_to_fvec2(units[i].core.position);
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
        vent->attachment, fixedvec3_xy_to_fvec2(unit->core.position));
    float interaction_radius = unit_harvest_interaction_radius_cells(unit);
    if (unit_is_following_path(unit) && !unit->movement.order_arrived) return false;
    if (fvec2_length_squared(attachment_delta) > interaction_radius * interaction_radius)
        return false;

    unit->movement.path_len = 0;
    unit->movement.path_index = 0;
    unit->movement.order_arrived = true;
    unit->core.momentum = fixedvec3_zero();
    unit->attack.target = -1;
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
        if (unit->harvest.capacity > 0)
            unit->harvest.cargo += take;
        else
            map->player_resources[owner][rtype] += take;
        if (unit->harvest.capacity > 0 && unit->harvest.cargo >= unit->harvest.capacity) {
            bool sent_home = false;
            for (int i = 0; i < unit_count; ++i) {
                if (!P_AreAllegiancesAllied(units[i].allegiance, unit->allegiance) ||
                    (units[i].traits & MF_RESOURCE_BASE) == 0 || units[i].hp <= 0) continue;
                fvec2_t base_position = fixedvec3_xy_to_fvec2(units[i].core.position);
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
            deactivate_resource_vent(map, vent);
            if (unit->harvest.capacity > 0 && unit->harvest.cargo > 0) {
                unit->harvest.phase = HARVEST_PHASE_TO_BASE;
                for (int i = 0; i < unit_count; ++i) {
                    if (!P_AreAllegiancesAllied(units[i].allegiance, unit->allegiance) ||
                        (units[i].traits & MF_RESOURCE_BASE) == 0 || units[i].hp <= 0) continue;
                    fvec2_t base_position = fixedvec3_xy_to_fvec2(units[i].core.position);
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
                if (game_info && unit->harvest.state_id > 0)
                    P_SetMobjState(&(statecontext_t){ .map = map, .game_info = game_info },
                                   unit, game_info->mobjinfo[unit->type_id].spawnstate);
            }
            break;
        }
    }
    return true;
}

void P_Ticker(level_t *map, mobj_t *units, int *unit_count, effect_t *effects,
                  int max_effects, const gameinfo_t *game_info, float dt) {
    if (game_info) {
        if (!units || !unit_count || *unit_count <= 0) return;
        int count = *unit_count;
        int dt_ms = (int)lroundf(dt * 1000.0f);
        statecontext_t ctx = {
            .map = map,
            .mobjs = units,
            .mobj_count = unit_count,
            .effects = effects,
            .max_effects = max_effects,
            .game_info = game_info,
        };

        for (int i = 0; i < count; ++i) {
            mobj_t *u = &units[i];
            u->core.momentum = fixedvec3_zero();
            if (u->remove) continue;
            if (u->core.state_id <= 0) P_SpawnMobj(game_info, u);
            P_TickMobjState(&ctx, u);
            if (u->remove || u->hp <= 0) continue;

            if (u->attack.cooldown_left_ms > 0) {
                u->attack.cooldown_left_ms -= dt_ms;
                if (u->attack.cooldown_left_ms < 0) u->attack.cooldown_left_ms = 0;
            }

            bool moving = unit_is_following_path(u);
            {
                const state_t *s = state_at(game_info, u->core.state_id);
                bool in_attack = s && s->misc1 == 3;
                if (moving && !in_attack) {
                    int stop_target = -1;
                    if (unit_has_attack_target_in_range(u, units, count, &stop_target)) {
                        u->attack.target = stop_target;
                        mobj_t *target = &units[stop_target];
                        fvec2_t target_delta = fvec2_sub(
                            fixedvec3_xy_to_fvec2(target->core.position),
                            fixedvec3_xy_to_fvec2(u->core.position));
                        u->core.angle = angle_from_map_vector(map,
                                                         target_delta.x,
                                                         target_delta.y);
                        u->movement.path_len = 0;
                        u->movement.path_index = 0;
                        u->movement.order_arrived = false;
                        moving = false;
                    }
                }
            }
            /* Turn-in-place before moving. */
            if (moving) {
                cell_t c = u->movement.path[u->movement.path_index];
                bool final = u->movement.path_index == u->movement.path_len - 1;
                fvec2_t target = final ? u->movement.goal :
                    fvec2_cell_center((ivec2_t){ c.x, c.y });
                fvec2_t delta = fvec2_sub(
                    target, fixedvec3_xy_to_fvec2(u->core.position));
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
                cell_t c = u->movement.path[u->movement.path_index];
                bool final = u->movement.path_index == u->movement.path_len - 1;
                fvec2_t target = final ? u->movement.goal :
                    fvec2_cell_center((ivec2_t){ c.x, c.y });
                fvec2_t delta = fvec2_sub(
                    target, fixedvec3_xy_to_fvec2(u->core.position));
                float dist = sqrtf(fvec2_length_squared(delta));
                if (final && final_goal_reaches_arrived_order_cluster(
                        units, count, i, target.x, target.y, dist)) {
                    u->movement.goal = fixedvec3_xy_to_fvec2(u->core.position);
                    u->movement.path_len = 0;
                    u->movement.path_index = 0;
                    u->movement.order_arrived = true;
                    moving = false;
                } else {
                    if (dist >= 0.001f)
                        u->core.angle = angle_from_map_vector(map, delta.x, delta.y);
                    float step = u->speed * dt;
                    if (dist <= step || dist < 0.001f) {
                        if (move_unit_if_walkable(map, u, delta)) {
                            u->movement.path_index++;
                            if (u->movement.path_index >= u->movement.path_len) {
                                u->movement.path_len = 0;
                                u->movement.path_index = 0;
                                u->movement.order_arrived = final;
                                moving = false;
                            }
                        } else {
                            u->movement.path_len = 0;
                            u->movement.path_index = 0;
                            u->movement.order_arrived = false;
                            moving = false;
                        }
                    } else {
                        fvec2_t displacement = fvec2_scale(delta, step / dist);
                        if (!move_unit_if_walkable(map, u, displacement)) {
                            u->movement.path_len = 0;
                            u->movement.path_index = 0;
                            u->movement.order_arrived = false;
                            moving = false;
                        }
                    }
                }
            }

            if (update_unit_harvest(map, units, count, u, dt_ms, game_info)) {
                moving = false;
                u->core.momentum = fixedvec3_zero();
            }

            if (u->type_id > 0 && u->type_id < game_info->mobj_type_count) {
                const mobjinfo_t *mi = &game_info->mobjinfo[u->type_id];
                const state_t *state = state_at(game_info, u->core.state_id);
                int group = state ? state->misc1 : 0;
                if (group != 3) {
                    if (moving && group != 2) {
                        P_SetMobjState(&ctx, u, mi->seestate);
                    } else if (!moving && group == 2) {
                        P_SetMobjState(&ctx, u, mi->spawnstate);
                    } else {
                        apply_state_visuals(game_info, &u->core,
                                            state_at(game_info, u->core.state_id), false);
                    }
                }
            }
        }

        separate_units(map, units, count);

        int write = 0;
        for (int read = 0; read < count; ++read) {
            if (units[read].remove) continue;
            if (write != read) units[write] = units[read];
            write++;
        }
        if (write != count) debug_effects_log("state compacted units before=%d after=%d", count, write);
        *unit_count = write;
        update_resource_vent_smoke(map, units, write);
        return;
    }

    if (!units || !unit_count || *unit_count <= 0) return;
    int count = *unit_count;
    int dt_ms = (int)lroundf(dt * 1000.0f);
    for (int i = 0; i < count; ++i) {
        mobj_t *u = &units[i];
        u->core.momentum = fixedvec3_zero();
        if (u->hp <= 0) {
            continue;
        }
        if (u->attack.cooldown_left_ms > 0) {
            u->attack.cooldown_left_ms -= dt_ms;
            if (u->attack.cooldown_left_ms < 0) u->attack.cooldown_left_ms = 0;
        }
        if (u->attack.anim_left_ms > 0) {
            u->attack.anim_left_ms -= dt_ms;
            if (u->attack.anim_left_ms < 0) u->attack.anim_left_ms = 0;
        }
        if (unit_is_following_path(u) && u->attack.anim_left_ms <= 0) {
            int stop_target = -1;
            if (unit_has_attack_target_in_range(u, units, count, &stop_target)) {
                u->attack.target = stop_target;
                mobj_t *target = &units[stop_target];
                fvec2_t target_delta = fvec2_sub(
                    fixedvec3_xy_to_fvec2(target->core.position),
                    fixedvec3_xy_to_fvec2(u->core.position));
                u->core.angle = angle_from_map_vector(map,
                                            target_delta.x, target_delta.y);
                u->movement.path_len = 0;
                u->movement.path_index = 0;
                u->movement.order_arrived = false;
            }
        }
        if (!unit_is_following_path(u)) {
            update_unit_harvest(map, units, count, u, dt_ms, NULL);
            continue;
        }
        cell_t c = u->movement.path[u->movement.path_index];
        bool final = u->movement.path_index == u->movement.path_len - 1;
        fvec2_t target = final ? u->movement.goal :
            fvec2_cell_center((ivec2_t){ c.x, c.y });
        fvec2_t delta = fvec2_sub(
            target, fixedvec3_xy_to_fvec2(u->core.position));
        float dist = sqrtf(fvec2_length_squared(delta));
        if (final && final_goal_reaches_arrived_order_cluster(
                units, count, i, target.x, target.y, dist)) {
            u->movement.goal = fixedvec3_xy_to_fvec2(u->core.position);
            u->movement.path_len = 0;
            u->movement.path_index = 0;
            u->movement.order_arrived = true;
            (void)update_unit_harvest(map, units, count, u, dt_ms, NULL);
            continue;
        }
        if (dist >= 0.001f)
            u->core.angle = angle_from_map_vector(map, delta.x, delta.y);
        float step = u->speed * dt;
        if (dist <= step || dist < 0.001f) {
            if (move_unit_if_walkable(map, u, delta)) {
                u->movement.path_index++;
                if (u->movement.path_index >= u->movement.path_len) {
                    u->movement.path_len = 0;
                    u->movement.path_index = 0;
                    u->movement.order_arrived = final;
                }
            } else {
                u->movement.path_len = 0;
                u->movement.path_index = 0;
                u->movement.order_arrived = false;
            }
        } else {
            fvec2_t displacement = fvec2_scale(delta, step / dist);
            if (!move_unit_if_walkable(map, u, displacement)) {
                u->movement.path_len = 0;
                u->movement.path_index = 0;
                u->movement.order_arrived = false;
            }
        }
        (void)update_unit_harvest(map, units, count, u, dt_ms, NULL);
    }

    separate_units(map, units, count);

    for (int i = 0; i < count; ++i) {
        mobj_t *attacker = &units[i];
        if (attacker->hp <= 0 || (attacker->traits & MF_ATTACK) == 0 ||
            attacker->attack.damage <= 0 || attacker->attack.range <= 0.0f) {
            continue;
        }

        int target_index = -1;
        float best_dist2 = attacker->attack.range * attacker->attack.range;
        for (int j = 0; j < count; ++j) {
            if (i == j || units[j].hp <= 0 || P_IsAlly(attacker, &units[j])) continue;
            float dist2 = fvec2_distance_squared(
                fixedvec3_xy_to_fvec2(units[j].core.position),
                fixedvec3_xy_to_fvec2(attacker->core.position));
            if (dist2 <= best_dist2) {
                best_dist2 = dist2;
                target_index = j;
            }
        }
        attacker->attack.target = target_index;
        if (target_index < 0) continue;

        mobj_t *target = &units[target_index];
        fvec2_t target_delta = fvec2_sub(
            fixedvec3_xy_to_fvec2(target->core.position),
            fixedvec3_xy_to_fvec2(attacker->core.position));
        attacker->core.angle = angle_from_map_vector(map,
                     target_delta.x, target_delta.y);
        if (attacker->attack.cooldown_left_ms > 0) continue;

        /* A hidden attacker (pre-placed native object outside FOW) must not leak its
         * position via a visible muzzle flash or ground light before it is revealed. */
        if (!attacker->hidden && attacker->muzzle_flash_name[0] != '\0') {
            int flash_ms = attacker->muzzle_flash_ms > 0 ? attacker->muzzle_flash_ms : 120;
            bool light_spawned = spawn_ground_light(effects, max_effects,
                                                    attacker->core.position,
                                                    flash_ms, 30);
            bool spawned = false;
            int muzzle_state = game_info && attacker->type_id > 0 &&
                attacker->type_id < game_info->mobj_type_count ?
                game_info->mobjinfo[attacker->type_id].muzzleflash : game_info->null_state;
            if (muzzle_state != game_info->null_state) {
                statecontext_t effect_ctx = {
                    .map = map,
                    .mobjs = units,
                    .mobj_count = &count,
                    .effects = effects,
                    .max_effects = max_effects,
                    .game_info = game_info,
                };
                spawned = P_SpawnEffect(&effect_ctx, muzzle_state,
                                        attacker->core.position, attacker->core.angle);
            }
            if (!spawned) {
                spawned = spawn_visual_effect(effects, max_effects,
                                              attacker->muzzle_flash_name,
                                              attacker->core.position,
                                              attacker->core.angle,
                                              flash_ms, 40, false, false, 0);
            }
            debug_effects_log("attack muzzle attacker=%d target=%d spawned=%d sprite=%s",
                              i, target_index, spawned ? 1 : 0, attacker->muzzle_flash_name);
            debug_effects_log("attack ground-light attacker=%d spawned=%d", i,
                              light_spawned ? 1 : 0);
        }
        target->hp -= attacker->attack.damage;
        debug_effects_log("attack damage attacker=%d target=%d damage=%d hp=%d/%d target_sprite=%s",
                          i, target_index, attacker->attack.damage, target->hp,
                          target->max_hp, target->core.sprite_name);
        if (target->hit_effect_name[0] != '\0' && !target->hidden) {
            spawn_visual_effect(effects, max_effects, target->hit_effect_name,
                                target->core.position,
                                target->core.angle,
                                400, 50, false, false, 0);
        }
        if (target->hp <= 0) {
            target->hp = 0;
            target->selected = false;
            target->traits &= ~(MF_SELECTABLE | MF_MOBILE |
                                MF_ATTACK | MF_HARVESTER);
            target->movement.path_len = 0;
            target->movement.path_index = 0;
            target->movement.order_arrived = false;
            target->attack.target = -1;
            target->harvest.target = -1;
            target->harvest.timer_ms = 0;
            target->attack.cooldown_left_ms = 0;
            target->attack.anim_left_ms = 0;
            target->core.momentum = fixedvec3_zero();
            target->death_started = true;
            if (target->death_effect_action && !target->hidden) {
                statecontext_t death_ctx = {
                    .map = map,
                    .mobjs = units,
                    .mobj_count = &count,
                    .effects = effects,
                    .max_effects = max_effects,
                    .game_info = game_info,
                };
                target->death_effect_action(&death_ctx, target);
            }
            if (target->death.anim_ms <= 0) {
                target->death.anim_ms = 900;
            }
            target->death.anim_left_ms = target->death.anim_ms;
            bool spawned = !target->hidden && spawn_visual_effect(effects, max_effects,
                                               target->core.sprite_name,
                                               target->core.position,
                                               target->core.angle,
                                               target->death.anim_ms, 90, true, true, -1);
            debug_effects_log("death target=%d spawned=%d sprite=%s facing=%d duration=%d",
                              target_index, spawned ? 1 : 0, target->core.sprite_name,
                              target->core.angle, target->death.anim_ms);
        }
        attacker->attack.cooldown_left_ms = attacker->attack.cooldown_ms > 0 ?
            attacker->attack.cooldown_ms : 500;
        attacker->attack.anim_left_ms = attacker->attack.anim_ms > 0 ?
            attacker->attack.anim_ms : attacker->attack.cooldown_left_ms;
    }

    int write = 0;
    for (int read = 0; read < count; ++read) {
        if (units[read].hp <= 0) continue;
        if (write != read) units[write] = units[read];
        write++;
    }
    if (write != count) {
        debug_effects_log("compacted units before=%d after=%d", count, write);
    }
    *unit_count = write;
}
