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


static int state_facing_slot(const state_t *state, int facing_code) {
    if (!state || state->facings <= 0) return -1;
    int wrap = 16;
    for (int i = 0; i < state->facings && i < RTS_MAX_STATE_FACINGS; ++i) {
        if (state->direction_codes[i] > 7 || facing_code > 7) {
            wrap = 16;
            break;
        }
        wrap = 8;
    }
    int best = 0;
    int best_delta = 1000;
    for (int i = 0; i < state->facings && i < RTS_MAX_STATE_FACINGS; ++i) {
        int code = state->direction_codes[i];
        int delta = abs(code - facing_code);
        if (delta > wrap / 2) delta = wrap - delta;
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    return best;
}

static void resolve_state_frame(const state_t *state, int facing_code,
                                    int *frame_out, uint32_t *flags_out,
                                    int *offset_x_out, int *offset_y_out,
                                    int *remap_out, int *intensity_out) {
    int frame = state ? state->frame : 0;
    uint32_t flags = state ? state->flags : 0;
    int offset_x = 0;
    int offset_y = 0;
    int remap = 0;
    int intensity = 16;
    if (state && state->facings > 0) {
        int best = state_facing_slot(state, facing_code);
        if (best < 0) best = 0;
        frame = state->facing_frames[best];
        flags = state->facing_flags[best];
        offset_x = state->offset_x[best];
        offset_y = state->offset_y[best];
        remap = state->remap[best];
        intensity = state->intensity[best];
    } else if (state) {
        remap = state->remap[0];
        intensity = state->intensity[0] ? state->intensity[0] : 16;
    }
    if (frame_out) *frame_out = frame;
    if (flags_out) *flags_out = flags;
    if (offset_x_out) *offset_x_out = offset_x;
    if (offset_y_out) *offset_y_out = offset_y;
    if (remap_out) *remap_out = remap;
    if (intensity_out) *intensity_out = intensity;
}

static void apply_state_visuals(const gameinfo_t *game_info, mobjcore_t *mobj,
                                const state_t *state, bool apply_offsets) {
    if (!game_info || !mobj || !state) return;
    mobj->sprite_id = state->sprite;
    resolve_state_frame(state, mobj->facing_code, &mobj->frame, &mobj->render_flags,
                        apply_offsets ? &mobj->render_offset_x : NULL,
                        apply_offsets ? &mobj->render_offset_y : NULL,
                        &mobj->render_remap, &mobj->render_intensity);
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
            unit->state_id = game_info->null_state;
            unit->tics = 0;
            unit->remove = true;
            return false;
        }
        const state_t *state = &game_info->states[state_id];
        unit->state_id = state_id;
        unit->tics = state->tics;
        apply_state_visuals(game_info, &unit->core, state, false);
        debug_effects_log("state unit type=%u state=%d sprite=%d frame=%d tics=%d",
                          unit->type_id, unit->state_id, unit->sprite_id, unit->frame, unit->tics);
        if (state->misc1 == 3) {
            int dir_slot = state_facing_slot(state, unit->facing_code);
            const char *sprite_name = "(unknown)";
            if (unit->sprite_id >= 0 && unit->sprite_id < game_info->sprite_count &&
                game_info->sprnames && game_info->sprnames[unit->sprite_id]) {
                sprite_name = game_info->sprnames[unit->sprite_id];
            }
            debug_effects_log("shoot state unit_type=%u state=%d facing_code=%d dir_slot=%d sprite=%s frame=%d",
                              unit->type_id, unit->state_id, unit->facing_code,
                              dir_slot, sprite_name, unit->frame);
        }
        if (state->action) state->action(ctx, unit);
        if (unit->remove || unit->state_id != state_id) return !unit->remove;
        if (unit->tics != 0) return true;
        state_id = state->nextstate;
    }
    unit->remove = true;
    return false;
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
        effect->state_id = state_id;
        effect->tics = state->tics;
        apply_state_visuals(game_info, &effect->core, state, true);
        if (effect->tics != 0) return true;
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
    if (unit->render_intensity == 0) unit->render_intensity = 16;
    const mobjinfo_t *info = &game_info->mobjinfo[unit->type_id];
    if (unit->max_hp <= 0) unit->max_hp = info->spawnhealth;
    if (unit->hp <= 0) unit->hp = unit->max_hp;
    if (unit->speed <= 0.0f) unit->speed = (float)info->speed;
    if (unit->radius <= 0.05f) {
        unit->radius = (float)info->radius / 32.0f;
        if (unit->radius < 0.32f) unit->radius = 0.32f;
        if (unit->radius > 0.90f) unit->radius = 0.90f;
    }
    if (unit->state_id <= 0) {
        statecontext_t ctx = { .game_info = game_info };
        P_SetMobjState(&ctx, unit, info->spawnstate);
    }
}


static int compass16_direction_code_from_vector(float dx, float dy) {
    if (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) return 0;
    return facing_to_index(facing_from_vector(dx, dy), &compass16_facing_scheme) * 2;
}

static int dark_colony8_direction_code_from_vector(float dx, float dy) {
    if (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) return 0;
    return facing_to_index(facing_from_vector(dx, dy), &dc8_facing_scheme);
}

static int dark_colony16_direction_code_from_vector(float dx, float dy) {
    if (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) return 0;
    return facing_to_index(facing_from_vector(dx, dy), &dc16_facing_scheme);
}

static int dark_reign16_direction_code_from_vector(float dx, float dy) {
    if (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) return 0;
    return facing_to_index(facing_from_vector(dx, dy), &dr16_facing_scheme);
}

int P_PointToAngle(const gameinfo_t *game_info, float dx, float dy) {
    if (game_info && game_info->direction_mode == RTS_DIRECTION_DARK_COLONY_16)
        return dark_colony16_direction_code_from_vector(dx, dy);
    if (game_info && game_info->direction_mode == RTS_DIRECTION_DARK_COLONY_8)
        return dark_colony8_direction_code_from_vector(dx, dy);
    if (game_info && game_info->direction_mode == RTS_DIRECTION_DARK_REIGN_8)
        return dark_reign16_direction_code_from_vector(dx, dy);
    return compass16_direction_code_from_vector(dx, dy);
}

static int direction_code_from_map_vector(const level_t *map, const gameinfo_t *game_info,
                                          float dx, float dy) {
    if (map && map->bottom_up_coordinates) dy = -dy;
    if (map && map->direction_mode == RTS_DIRECTION_DARK_REIGN_8)
        return dark_reign16_direction_code_from_vector(dx, dy);
    return P_PointToAngle(game_info, dx, dy);
}

void P_AngleToVec(const gameinfo_t *game_info, int code, float *dx, float *dy) {
    if (!dx || !dy) return;
    if (game_info && game_info->direction_mode == RTS_DIRECTION_DARK_COLONY_16) {
        float angle = (float)code * 0.39269908169872414f;
        *dx = sinf(angle);
        *dy = cosf(angle);
        return;
    }
    if (game_info && game_info->direction_mode == RTS_DIRECTION_DARK_COLONY_8) {
        static const float dirs[8][2] = {
            {  0.0f,  1.0f },
            {  0.70710678f,  0.70710678f },
            {  1.0f,  0.0f },
            {  0.70710678f, -0.70710678f },
            {  0.0f, -1.0f },
            { -0.70710678f, -0.70710678f },
            { -1.0f,  0.0f },
            { -0.70710678f,  0.70710678f },
        };
        int dir = code % 8;
        if (dir < 0) dir += 8;
        *dx = dirs[dir][0];
        *dy = dirs[dir][1];
        return;
    }
    float angle = -(float)code * 0.39269908169872414f;
    *dx = cosf(angle);
    *dy = -sinf(angle);
}

static void direction_vector_from_map_code(const level_t *map, const gameinfo_t *game_info,
                                           int code, float *dx, float *dy) {
    if (map && map->direction_mode == RTS_DIRECTION_DARK_REIGN_8) {
        float angle = (float)code * 0.39269908169872414f;
        *dx = sinf(angle);
        *dy = -cosf(angle);
        return;
    }
    P_AngleToVec(game_info, code, dx, dy);
    if (map && map->bottom_up_coordinates) *dy = -*dy;
}

static bool unit_has_attack_target_in_range(const mobj_t *attacker, const mobj_t *units, int unit_count,
                                            int *target_index_out) {
    if (target_index_out) *target_index_out = -1;
    if (!attacker || !units || unit_count <= 0 ||
        (attacker->traits & MF_ATTACK) == 0 ||
        attacker->attack_damage <= 0 || attacker->attack_range <= 0.0f) {
        return false;
    }

    int preferred = attacker->attack_target;
    if (preferred >= 0 && preferred < unit_count) {
        const mobj_t *target = &units[preferred];
        if (!target->remove && target->hp > 0 && target->owner != attacker->owner) {
            float dx = target->gx - attacker->gx;
            float dy = target->gy - attacker->gy;
            if (dx * dx + dy * dy <= attacker->attack_range * attacker->attack_range) {
                if (target_index_out) *target_index_out = preferred;
                return true;
            }
        }
    }

    int best = -1;
    float best_dist2 = attacker->attack_range * attacker->attack_range;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *candidate = &units[i];
        if (candidate == attacker || candidate->remove || candidate->hp <= 0 ||
            candidate->owner == attacker->owner) {
            continue;
        }
        float dx = candidate->gx - attacker->gx;
        float dy = candidate->gy - attacker->gy;
        float dist2 = dx * dx + dy * dy;
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
                                const char *sprite_name, const char *sequence_name,
                                float gx, float gy, int facing_code, int duration_ms,
                                int frame_ms, bool add_decoration_on_finish,
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
        effect->gx = gx;
        effect->gy = gy;
        effect->facing_code = facing_code;
        effect->render_intensity = 16;
        effect->duration_ms = duration_ms > 0 ? duration_ms : 120;
        effect->frame_ms = frame_ms > 0 ? frame_ms : 90;
        effect->decoration_frame_index = decoration_frame_index;
        effect->add_decoration_on_finish = add_decoration_on_finish;
        snprintf(effect->sprite_name, sizeof(effect->sprite_name), "%s", sprite_name);
        if (sequence_name && sequence_name[0] != '\0') {
            snprintf(effect->sequence_name, sizeof(effect->sequence_name), "%s", sequence_name);
        }
        debug_effects_log("spawn slot=%d sprite=%s sequence=%s pos=%.2f,%.2f facing=%d duration=%d frame_ms=%d corpse=%d",
                          i, effect->sprite_name,
                          effect->sequence_name[0] ? effect->sequence_name : "(none)",
                          effect->gx, effect->gy, effect->facing_code,
                          effect->duration_ms, effect->frame_ms,
                          effect->add_decoration_on_finish ? 1 : 0);
        return true;
    }
    debug_effects_log("spawn failed no free slot sprite=%s sequence=%s",
                      sprite_name, sequence_name ? sequence_name : "(none)");
    return false;
}

bool P_SpawnEffect(statecontext_t *ctx, int state_id, float gx, float gy, int facing_code) {
    if (!ctx || !ctx->effects || ctx->max_effects <= 0 || !ctx->game_info) return false;
    for (int i = 0; i < ctx->max_effects; ++i) {
        effect_t *effect = &ctx->effects[i];
        if (effect->active) continue;
        memset(effect, 0, sizeof(*effect));
        effect->active = true;
        effect->use_state = true;
        effect->gx = gx;
        effect->gy = gy;
        effect->facing_code = facing_code;
        effect->render_intensity = 16;
        bool ok = set_effect_state(ctx->game_info, effect, state_id);
        debug_effects_log("spawn state effect slot=%d state=%d ok=%d sprite=%s frame=%d",
                          i, state_id, ok ? 1 : 0, effect->sprite_name, effect->frame);
        return ok;
    }
    debug_effects_log("spawn state effect failed no free slot state=%d", state_id);
    return false;
}

static const char *death_sequence_name_for_unit(const mobj_t *unit) {
    (void)unit;
    return "die";
}

static void add_effect_finish_decoration(level_t *map, const effect_t *effect) {
    if (!map || !effect || !effect->add_decoration_on_finish ||
        effect->sprite_name[0] == '\0' || map->decoration_count >= MAX_DECORATIONS) {
        return;
    }
    mapdecoration_t *decorations = realloc(map->decorations,
                                         (size_t)(map->decoration_count + 1) * sizeof(mapdecoration_t));
    if (!decorations) return;
    map->decorations = decorations;
    mapdecoration_t *dec = &map->decorations[map->decoration_count++];
    memset(dec, 0, sizeof(*dec));
    dec->gx = (int)floorf(effect->gx);
    dec->gy = (int)floorf(effect->gy);
    dec->footprint_w = 1;
    dec->footprint_h = 1;
    dec->center_anchor = true;
    dec->frame_index = effect->decoration_frame_index;
    dec->facing_code = effect->facing_code;
    snprintf(dec->sprite_name, sizeof(dec->sprite_name), "%s", effect->sprite_name);
    snprintf(dec->sequence_name, sizeof(dec->sequence_name), "%s", effect->sequence_name);
    debug_effects_log("corpse decoration sprite=%s sequence=%s grid=%d,%d facing=%d frame_index=%d count=%d",
                      dec->sprite_name, dec->sequence_name, dec->gx, dec->gy,
                      dec->facing_code, dec->frame_index, map->decoration_count);
}

bool P_AddCorpse(statecontext_t *ctx, const mobj_t *unit) {
    if (!ctx || !ctx->map || !unit || unit->sprite_name[0] == '\0' ||
        ctx->map->decoration_count >= MAX_DECORATIONS) {
        return false;
    }
    mapdecoration_t *decorations = realloc(ctx->map->decorations,
                                         (size_t)(ctx->map->decoration_count + 1) * sizeof(mapdecoration_t));
    if (!decorations) return false;
    ctx->map->decorations = decorations;
    mapdecoration_t *dec = &ctx->map->decorations[ctx->map->decoration_count++];
    memset(dec, 0, sizeof(*dec));
    dec->gx = (int)floorf(unit->gx);
    dec->gy = (int)floorf(unit->gy);
    dec->footprint_w = 1;
    dec->footprint_h = 1;
    dec->center_anchor = true;
    dec->frame_index = unit->frame;
    dec->facing_code = unit->facing_code;
    dec->render_flags = unit->render_flags;
    snprintf(dec->sprite_name, sizeof(dec->sprite_name), "%s", unit->sprite_name);
    debug_effects_log("corpse unit sprite=%s frame=%d flags=%u grid=%d,%d count=%d",
                      dec->sprite_name, dec->frame_index, dec->render_flags,
                      dec->gx, dec->gy, ctx->map->decoration_count);
    return true;
}

bool P_Attack(statecontext_t *ctx, mobj_t *attacker) {
    if (!ctx || !attacker || !ctx->mobjs || !ctx->mobj_count) return false;
    int count = *ctx->mobj_count;
    int target_index = attacker->attack_target;
    if (target_index < 0 || target_index >= count || ctx->mobjs[target_index].hp <= 0 ||
        ctx->mobjs[target_index].owner == attacker->owner) {
        target_index = -1;
        float best_dist2 = attacker->attack_range * attacker->attack_range;
        for (int i = 0; i < count; ++i) {
            mobj_t *candidate = &ctx->mobjs[i];
            if (candidate == attacker || candidate->hp <= 0 || candidate->owner == attacker->owner)
                continue;
            float dx = candidate->gx - attacker->gx;
            float dy = candidate->gy - attacker->gy;
            float dist2 = dx * dx + dy * dy;
            if (dist2 <= best_dist2) {
                best_dist2 = dist2;
                target_index = i;
            }
        }
    }
    if (target_index < 0) return false;

    mobj_t *target = &ctx->mobjs[target_index];
    const state_t *attack_state = state_at(ctx->game_info, attacker->state_id);
    int dir_slot = state_facing_slot(attack_state, attacker->facing_code);
    const char *sprite_name = "(unknown)";
    if (ctx->game_info && attacker->sprite_id >= 0 &&
        attacker->sprite_id < ctx->game_info->sprite_count &&
        ctx->game_info->sprnames && ctx->game_info->sprnames[attacker->sprite_id]) {
        sprite_name = ctx->game_info->sprnames[attacker->sprite_id];
    }
    debug_effects_log("shoot fire unit_type=%u state=%d facing_code=%d dir_slot=%d sprite=%s frame=%d target=%d",
                      attacker->type_id, attacker->state_id, attacker->facing_code,
                      dir_slot, sprite_name, attacker->frame, target_index);
    target->hp -= attacker->attack_damage;
    if (attacker->attack_cooldown_ms > 0)
        attacker->attack_cooldown_left_ms = attacker->attack_cooldown_ms;
    debug_effects_log("state attack attacker_type=%u target=%d damage=%d hp=%d/%d",
                      attacker->type_id, target_index, attacker->attack_damage,
                      target->hp, target->max_hp);
    if (target->hp <= 0) {
        target->hp = 0;
        target->selected = false;
        target->traits &= ~(MF_SELECTABLE | MF_MOBILE |
                            MF_ATTACK | MF_HARVESTER);
        target->path_len = 0;
        target->path_index = 0;
        target->move_order_arrived = false;
        target->harvest_target = -1;
        target->harvest_timer_ms = 0;
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

void P_UpdateEffects(level_t *map, effect_t *effects, int max_effects,
                           const gameinfo_t *game_info, float dt) {
    if (!effects || max_effects <= 0) return;
    int dt_ms = (int)lroundf(dt * 1000.0f);
    for (int i = 0; i < max_effects; ++i) {
        effect_t *effect = &effects[i];
        if (!effect->active) continue;
        if (effect->use_state && game_info) {
            if (effect->tics > 0) effect->tics--;
            if (effect->tics == 0) {
                const state_t *state = state_at(game_info, effect->state_id);
                int next = state ? state->nextstate : game_info->null_state;
                set_effect_state(game_info, effect, next);
            }
            continue;
        }
        effect->age_ms += dt_ms;
        if (effect->age_ms < effect->duration_ms) continue;
        debug_effects_log("finish sprite=%s sequence=%s age=%d duration=%d corpse=%d",
                          effect->sprite_name,
                          effect->sequence_name[0] ? effect->sequence_name : "(none)",
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
                float dx = b->gx - a->gx;
                float dy = b->gy - a->gy;
                float dist2 = dx * dx + dy * dy;
                if (dist2 >= min_dist * min_dist) continue;
                float dist = sqrtf(dist2);
                if (dist < 0.0001f) {
                    float angle = (float)((i * 37 + j * 17) % 360) * 0.01745329252f;
                    dx = cosf(angle);
                    dy = sinf(angle);
                    dist = 1.0f;
                }
                float push = (min_dist - dist) * 0.5f;
                float nx = dx / dist;
                float ny = dy / dist;
                float ax = a->gx - nx * push;
                float ay = a->gy - ny * push;
                float bx = b->gx + nx * push;
                float by = b->gy + ny * push;
                if (P_CheckPosition(map, a, ax, ay)) {
                    a->gx = ax;
                    a->gy = ay;
                    P_ClampToLevel(map, a);
                }
                if (P_CheckPosition(map, b, bx, by)) {
                    b->gx = bx;
                    b->gy = by;
                    P_ClampToLevel(map, b);
                }
            }
        }
    }
}

static bool move_unit_if_walkable(const level_t *map, mobj_t *unit, float gx, float gy) {
    if (!unit) return false;
    if (P_CheckPosition(map, unit, gx, gy)) {
        unit->gx = gx;
        unit->gy = gy;
        return true;
    }
    if (P_CheckPosition(map, unit, gx, unit->gy)) {
        unit->gx = gx;
        return true;
    }
    if (P_CheckPosition(map, unit, unit->gx, gy)) {
        unit->gy = gy;
        return true;
    }
    return false;
}

static bool unit_is_following_path(const mobj_t *unit) {
    return unit && unit->path_index > 0 && unit->path_index < unit->path_len;
}

static bool final_goal_reaches_arrived_order_cluster(const mobj_t *units, int count, int self_index,
                                                     float tx, float ty, float dist_to_goal) {
    if (!units || self_index < 0 || self_index >= count) return false;
    const mobj_t *unit = &units[self_index];
    if (unit->move_order_id == 0) return false;
    float radius = P_MobjRadius(unit);

    for (int i = 0; i < count; ++i) {
        if (i == self_index) continue;
        const mobj_t *other = &units[i];
        if (other->remove || other->hp <= 0 || other->move_order_id != unit->move_order_id) {
            continue;
        }
        if (!other->move_order_arrived) continue;

        float min_dist = radius + P_MobjRadius(other);
        float goal_dx = other->gx - tx;
        float goal_dy = other->gy - ty;
        float unit_dx = other->gx - unit->gx;
        float unit_dy = other->gy - unit->gy;
        float contact_dist = min_dist + 0.20f;
        if (goal_dx * goal_dx + goal_dy * goal_dy < min_dist * min_dist &&
            dist_to_goal <= contact_dist) {
            return true;
        }
        if (unit_dx * unit_dx + unit_dy * unit_dy <= contact_dist * contact_dist) {
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

static bool update_unit_harvest(level_t *map, mobj_t *unit, int dt_ms,
                                const gameinfo_t *game_info) {
    if (!map || !unit || (unit->traits & MF_HARVESTER) == 0 ||
        unit->harvest_target < 0) {
        return false;
    }
    if (unit->harvest_target >= map->resource_vent_count || !map->resource_vents) {
        unit->harvest_target = -1;
        unit->harvest_timer_ms = 0;
        return false;
    }

    resourcevent_t *vent = &map->resource_vents[unit->harvest_target];
    if (!vent->active || vent->rate <= 0 || vent->amount <= 0) {
        unit->harvest_target = -1;
        unit->harvest_timer_ms = 0;
        return false;
    }

    float dx = vent->attach_gx - unit->gx;
    float dy = vent->attach_gy - unit->gy;
    float interaction_radius = unit_harvest_interaction_radius_cells(unit);
    if (unit_is_following_path(unit) && !unit->move_order_arrived) return false;
    if (dx * dx + dy * dy > interaction_radius * interaction_radius) return false;

    unit->path_len = 0;
    unit->path_index = 0;
    unit->move_order_arrived = true;
    unit->attack_target = -1;
    if (game_info && unit->harvest_state_id > 0 &&
        unit->harvest_state_id < game_info->state_count) {
        const state_t *state = state_at(game_info, unit->state_id);
        if (!state || state->misc1 != 5) {
            unit->facing_code = (vent->attach_gx < unit->gx) ? 14 : 2;
            statecontext_t ctx = { .map = map, .game_info = game_info };
            P_SetMobjState(&ctx, unit, unit->harvest_state_id);
        }
    }
    unit->harvest_timer_ms += dt_ms;
    while (unit->harvest_timer_ms >= RTS_HARVEST_INTERVAL_MS && vent->amount > 0) {
        unit->harvest_timer_ms -= RTS_HARVEST_INTERVAL_MS;
        int take = vent->rate;
        if (take > vent->amount) take = vent->amount;
        vent->amount -= take;
        int owner = unit->owner < 8 ? unit->owner : 0;
        int rtype = vent->resource_type < RTS_MAX_RESOURCES ? vent->resource_type : 0;
        map->player_resources[owner][rtype] += take;
        if (vent->amount <= 0) {
            vent->active = false;
            unit->harvest_target = -1;
            unit->harvest_timer_ms = 0;
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
            if (u->remove) continue;
            if (u->state_id <= 0) P_SpawnMobj(game_info, u);
            if (u->tics > 0) {
                u->tics--;
                if (u->tics == 0) {
                    const state_t *state = state_at(game_info, u->state_id);
                    int next = state ? state->nextstate : game_info->null_state;
                    P_SetMobjState(&ctx, u, next);
                }
            }
            if (u->remove || u->hp <= 0) continue;

            if (u->attack_cooldown_left_ms > 0) {
                u->attack_cooldown_left_ms -= dt_ms;
                if (u->attack_cooldown_left_ms < 0) u->attack_cooldown_left_ms = 0;
            }

            bool moving = unit_is_following_path(u);
            {
                const state_t *s = state_at(game_info, u->state_id);
                bool in_attack = s && s->misc1 == 3;
                if (moving && !in_attack) {
                    int stop_target = -1;
                    if (unit_has_attack_target_in_range(u, units, count, &stop_target)) {
                        u->attack_target = stop_target;
                        mobj_t *target = &units[stop_target];
                        u->facing_code = direction_code_from_map_vector(map, game_info,
                                                                         target->gx - u->gx,
                                                                         target->gy - u->gy);
                        u->path_len = 0;
                        u->path_index = 0;
                        u->move_order_arrived = false;
                        moving = false;
                    }
                }
            }
            /* Turn-in-place before moving. */
            if (moving) {
                cell_t c = u->path[u->path_index];
                bool final = u->path_index == u->path_len - 1;
                float tx = final ? u->move_goal_gx : (float)c.x + 0.5f;
                float ty = final ? u->move_goal_gy : (float)c.y + 0.5f;
                float dx = tx - u->gx;
                float dy = ty - u->gy;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist >= 0.001f) {
                    int desired = direction_code_from_map_vector(map, game_info, dx, dy);
                    if (desired != u->facing_code) {
                        u->turn_timer_ms -= dt_ms;
                        if (u->turn_timer_ms <= 0) {
                            int delta = desired - u->facing_code;
                            int turn_step = map && map->direction_mode == RTS_DIRECTION_DARK_REIGN_8 ? 1 : 2;
                            if (delta > 8) delta -= 16;
                            if (delta < -8) delta += 16;
                            u->facing_code = delta > 0 ?
                                (u->facing_code + turn_step) % 16 :
                                (u->facing_code - turn_step + 16) % 16;
                            u->turn_timer_ms = RTS_TURN_STEP_MS;
                        }
                        moving = false;
                    } else {
                        u->turn_timer_ms = 0;
                    }
                }
            }
            if (moving) {
                cell_t c = u->path[u->path_index];
                bool final = u->path_index == u->path_len - 1;
                float tx = final ? u->move_goal_gx : (float)c.x + 0.5f;
                float ty = final ? u->move_goal_gy : (float)c.y + 0.5f;
                float dx = tx - u->gx;
                float dy = ty - u->gy;
                float dist = sqrtf(dx * dx + dy * dy);
                if (final && final_goal_reaches_arrived_order_cluster(units, count, i, tx, ty, dist)) {
                    u->move_goal_gx = u->gx;
                    u->move_goal_gy = u->gy;
                    u->path_len = 0;
                    u->path_index = 0;
                    u->move_order_arrived = true;
                    moving = false;
                } else {
                    if (dist >= 0.001f)
                        u->facing_code = direction_code_from_map_vector(map, game_info, dx, dy);
                    float step = u->speed * dt;
                    if (dist <= step || dist < 0.001f) {
                        if (move_unit_if_walkable(map, u, tx, ty)) {
                            u->path_index++;
                            if (u->path_index >= u->path_len) {
                                u->path_len = 0;
                                u->path_index = 0;
                                u->move_order_arrived = final;
                                moving = false;
                            }
                        } else {
                            u->path_len = 0;
                            u->path_index = 0;
                            u->move_order_arrived = false;
                            moving = false;
                        }
                    } else {
                        float nx = u->gx + dx / dist * step;
                        float ny = u->gy + dy / dist * step;
                        if (!move_unit_if_walkable(map, u, nx, ny)) {
                            u->path_len = 0;
                            u->path_index = 0;
                            u->move_order_arrived = false;
                            moving = false;
                        }
                    }
                }
            }

            if (update_unit_harvest(map, u, dt_ms, game_info)) {
                moving = false;
            }

            if (u->type_id > 0 && u->type_id < game_info->mobj_type_count) {
                const mobjinfo_t *mi = &game_info->mobjinfo[u->type_id];
                const state_t *state = state_at(game_info, u->state_id);
                int group = state ? state->misc1 : 0;
                if (group != 3) {
                    if (moving && group != 2) {
                        P_SetMobjState(&ctx, u, mi->seestate);
                    } else if (!moving && group == 2) {
                        P_SetMobjState(&ctx, u, mi->spawnstate);
                    } else {
                        apply_state_visuals(game_info, &u->core,
                                            state_at(game_info, u->state_id), false);
                    }
                }
            }
        }

        separate_units(map, units, count);

        for (int i = 0; i < count; ++i) {
            mobj_t *attacker = &units[i];
            if (attacker->remove || attacker->hp <= 0 ||
                (attacker->traits & MF_ATTACK) == 0 ||
                attacker->attack_damage <= 0 || attacker->attack_range <= 0.0f ||
                attacker->type_id <= 0 || attacker->type_id >= game_info->mobj_type_count) {
                continue;
            }
            const mobjinfo_t *mi = &game_info->mobjinfo[attacker->type_id];
            const state_t *state = state_at(game_info, attacker->state_id);
            if (state && state->misc1 == 3) continue;

            int target_index = -1;
            float best_dist2 = attacker->attack_range * attacker->attack_range;
            for (int j = 0; j < count; ++j) {
                if (i == j || units[j].remove || units[j].hp <= 0 ||
                    units[j].owner == attacker->owner) {
                    continue;
                }
                float dx = units[j].gx - attacker->gx;
                float dy = units[j].gy - attacker->gy;
                float dist2 = dx * dx + dy * dy;
                if (dist2 <= best_dist2) {
                    best_dist2 = dist2;
                    target_index = j;
                }
            }
            attacker->attack_target = target_index;
            if (target_index < 0) continue;

            mobj_t *target = &units[target_index];
            attacker->facing_code = direction_code_from_map_vector(map, game_info,
                                                                   target->gx - attacker->gx,
                                                                   target->gy - attacker->gy);
            if (attacker->attack_cooldown_left_ms > 0 ||
                mi->missilestate == game_info->null_state) {
                continue;
            }
            P_SetMobjState(&ctx, attacker, mi->missilestate);
        }

        int write = 0;
        for (int read = 0; read < count; ++read) {
            if (units[read].remove) continue;
            if (write != read) units[write] = units[read];
            write++;
        }
        if (write != count) debug_effects_log("state compacted units before=%d after=%d", count, write);
        *unit_count = write;
        return;
    }

    if (!units || !unit_count || *unit_count <= 0) return;
    int count = *unit_count;
    int dt_ms = (int)lroundf(dt * 1000.0f);
    for (int i = 0; i < count; ++i) {
        mobj_t *u = &units[i];
        if (u->hp <= 0) {
            continue;
        }
        if (u->attack_cooldown_left_ms > 0) {
            u->attack_cooldown_left_ms -= dt_ms;
            if (u->attack_cooldown_left_ms < 0) u->attack_cooldown_left_ms = 0;
        }
        if (u->attack_anim_left_ms > 0) {
            u->attack_anim_left_ms -= dt_ms;
            if (u->attack_anim_left_ms < 0) u->attack_anim_left_ms = 0;
        }
        if (unit_is_following_path(u) && u->attack_anim_left_ms <= 0) {
            int stop_target = -1;
            if (unit_has_attack_target_in_range(u, units, count, &stop_target)) {
                u->attack_target = stop_target;
                mobj_t *target = &units[stop_target];
                u->facing_code = direction_code_from_map_vector(map, NULL,
                                                                target->gx - u->gx,
                                                                target->gy - u->gy);
                u->path_len = 0;
                u->path_index = 0;
                u->move_order_arrived = false;
            }
        }
        if (!unit_is_following_path(u)) {
            update_unit_harvest(map, u, dt_ms, NULL);
            continue;
        }
        cell_t c = u->path[u->path_index];
        bool final = u->path_index == u->path_len - 1;
        float tx = final ? u->move_goal_gx : (float)c.x + 0.5f;
        float ty = final ? u->move_goal_gy : (float)c.y + 0.5f;
        float dx = tx - u->gx;
        float dy = ty - u->gy;
        float dist = sqrtf(dx * dx + dy * dy);
        if (final && final_goal_reaches_arrived_order_cluster(units, count, i, tx, ty, dist)) {
            u->move_goal_gx = u->gx;
            u->move_goal_gy = u->gy;
            u->path_len = 0;
            u->path_index = 0;
            u->move_order_arrived = true;
            (void)update_unit_harvest(map, u, dt_ms, NULL);
            continue;
        }
        if (dist >= 0.001f) u->facing_code = direction_code_from_map_vector(map, NULL, dx, dy);
        float step = u->speed * dt;
        if (dist <= step || dist < 0.001f) {
            if (move_unit_if_walkable(map, u, tx, ty)) {
                u->path_index++;
                if (u->path_index >= u->path_len) {
                    u->path_len = 0;
                    u->path_index = 0;
                    u->move_order_arrived = final;
                }
            } else {
                u->path_len = 0;
                u->path_index = 0;
                u->move_order_arrived = false;
            }
        } else {
            float nx = u->gx + dx / dist * step;
            float ny = u->gy + dy / dist * step;
            if (!move_unit_if_walkable(map, u, nx, ny)) {
                u->path_len = 0;
                u->path_index = 0;
                u->move_order_arrived = false;
            }
        }
        (void)update_unit_harvest(map, u, dt_ms, NULL);
    }

    separate_units(map, units, count);

    for (int i = 0; i < count; ++i) {
        mobj_t *attacker = &units[i];
        if (attacker->hp <= 0 || (attacker->traits & MF_ATTACK) == 0 ||
            attacker->attack_damage <= 0 || attacker->attack_range <= 0.0f) {
            continue;
        }

        int target_index = -1;
        float best_dist2 = attacker->attack_range * attacker->attack_range;
        for (int j = 0; j < count; ++j) {
            if (i == j || units[j].hp <= 0 || units[j].owner == attacker->owner) continue;
            float dx = units[j].gx - attacker->gx;
            float dy = units[j].gy - attacker->gy;
            float dist2 = dx * dx + dy * dy;
            if (dist2 <= best_dist2) {
                best_dist2 = dist2;
                target_index = j;
            }
        }
        attacker->attack_target = target_index;
        if (target_index < 0) continue;

        mobj_t *target = &units[target_index];
        attacker->facing_code = direction_code_from_map_vector(map, NULL,
                                                               target->gx - attacker->gx,
                                                               target->gy - attacker->gy);
        if (attacker->attack_cooldown_left_ms > 0) continue;

        if (attacker->muzzle_flash_name[0] != '\0') {
            float vx = 0.0f, vy = 0.0f;
            direction_vector_from_map_code(map, NULL, attacker->facing_code, &vx, &vy);
            bool spawned = spawn_visual_effect(effects, max_effects, attacker->muzzle_flash_name, "flash",
                                               attacker->gx + vx * 0.42f, attacker->gy + vy * 0.42f,
                                               attacker->facing_code,
                                               attacker->muzzle_flash_ms > 0 ? attacker->muzzle_flash_ms : 120,
                                               40, false, 0);
            debug_effects_log("attack muzzle attacker=%d target=%d spawned=%d sprite=%s",
                              i, target_index, spawned ? 1 : 0, attacker->muzzle_flash_name);
        }
        target->hp -= attacker->attack_damage;
        debug_effects_log("attack damage attacker=%d target=%d damage=%d hp=%d/%d target_sprite=%s",
                          i, target_index, attacker->attack_damage, target->hp,
                          target->max_hp, target->sprite_name);
        if (target->hit_effect_name[0] != '\0') {
            spawn_visual_effect(effects, max_effects, target->hit_effect_name, NULL,
                                target->gx, target->gy, target->facing_code,
                                400, 50, false, 0);
        }
        if (target->hp <= 0) {
            target->hp = 0;
            target->selected = false;
            target->traits &= ~(MF_SELECTABLE | MF_MOBILE |
                                MF_ATTACK | MF_HARVESTER);
            target->path_len = 0;
            target->path_index = 0;
            target->move_order_arrived = false;
            target->attack_target = -1;
            target->harvest_target = -1;
            target->harvest_timer_ms = 0;
            target->attack_cooldown_left_ms = 0;
            target->attack_anim_left_ms = 0;
            target->death_started = true;
            if (target->death_anim_ms <= 0) {
                target->death_anim_ms = 900;
            }
            target->death_anim_left_ms = target->death_anim_ms;
            bool spawned = spawn_visual_effect(effects, max_effects, target->sprite_name,
                                               death_sequence_name_for_unit(target),
                                               target->gx, target->gy, target->facing_code,
                                               target->death_anim_ms, 90, true, -1);
            debug_effects_log("death target=%d spawned=%d sprite=%s sequence=%s facing=%d duration=%d",
                              target_index, spawned ? 1 : 0, target->sprite_name,
                              death_sequence_name_for_unit(target), target->facing_code,
                              target->death_anim_ms);
        }
        attacker->attack_cooldown_left_ms = attacker->attack_cooldown_ms > 0 ?
            attacker->attack_cooldown_ms : 500;
        attacker->attack_anim_left_ms = attacker->attack_anim_ms > 0 ?
            attacker->attack_anim_ms : attacker->attack_cooldown_left_ms;
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
