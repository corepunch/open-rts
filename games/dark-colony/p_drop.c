#include "p_mission.h"
#include "p_reinforce.h"
#include "dc_facing.h"
#include "info.h"
#include "w_drop.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


enum { DROPSHIP_FLIGHT_TICS = 50 };

typedef struct {
    mobj_t mobj;
    bool active;
    ivec2_t origin;
    DropshipPayload payload[DROPSHIP_MAX_PAYLOAD_TYPES];
    int payload_count;
    int payload_index;
    int released_count;
    bool release_pending;
    int phase_duration_ms;
    int effect_slots[DROPSHIP_MAX_PARTS];
} DropshipRuntime;

static const ivec2_t drop_formation[] = {
    { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 },
    { 0, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 },
    { 1, 1 }, { -2, 0 }, { 2, 0 }, { 0, -2 },
};

typedef struct {
    level_t *map;
    mobj_t *units;
    int *unit_count;
    const gameinfo_t *game_info;
} DropshipUpdateContext;

struct DropshipSystem {
    DropshipRuntime dropships[8];
    DropshipAnimations dropship_animations;
};

static DropshipRuntime *dropship_runtime(DropshipSystem *ships, const mobj_t *mobj) {
    if (!ships || !mobj) return NULL;
    for (int i = 0; i < (int)(sizeof(ships->dropships) / sizeof(ships->dropships[0])); ++i) {
        if (&ships->dropships[i].mobj == mobj) return &ships->dropships[i];
    }
    return NULL;
}

static int spawn_dropship_part(effect_t *effects, int max_effects,
                               fixed3_t position, int duration_ms) {
    for (int i = 0; i < max_effects; ++i) {
        if (effects[i].active) continue;
        effect_t *effect = &effects[i];
        memset(effect, 0, sizeof(*effect));
        effect->active = true;
        effect->fin_placement = true;
        effect->core.position = position;
        effect->duration_ms = duration_ms;
        effect->frame_ms = duration_ms + 1;
        return i;
    }
    return -1;
}

static void clear_dropship_parts(DropshipRuntime *runtime,
                                 effect_t *effects, int max_effects) {
    for (int i = 0; i < DROPSHIP_MAX_PARTS; ++i) {
        int slot = runtime->effect_slots[i];
        if (slot >= 0 && slot < max_effects) memset(&effects[slot], 0, sizeof(effects[slot]));
        runtime->effect_slots[i] = -1;
    }
}

static int dropship_frame_at(const DropshipAnimation *animation, int elapsed_ms) {
    if (animation->duration_ms > 0) elapsed_ms %= animation->duration_ms;
    int frame_end_ms = 0;
    for (int i = 0; i < animation->frame_count; ++i) {
        frame_end_ms += animation->frames[i].duration_ms;
        if (elapsed_ms < frame_end_ms) return i;
    }
    return animation->frame_count - 1;
}

static void sync_dropship_parts(DropshipRuntime *runtime,
                                const DropshipAnimation *animation,
                                effect_t *effects, int max_effects,
                                int elapsed_ms) {
    if (!animation->valid || animation->frame_count <= 0) return;
    mobj_t *ship = &runtime->mobj;
    const DropshipFrame *frame =
        &animation->frames[dropship_frame_at(animation, elapsed_ms)];
    int runtime_part = 0;
    for (int part_index = 0;
         part_index < frame->part_count && runtime_part < DROPSHIP_MAX_PARTS;
         ++part_index) {
        const DropshipPart *part = &frame->parts[part_index];
        int slot = runtime->effect_slots[runtime_part];
        if (slot < 0 || slot >= max_effects || !effects[slot].active) {
            slot = spawn_dropship_part(
                effects, max_effects, ship->core.position, runtime->phase_duration_ms + 1);
            runtime->effect_slots[runtime_part] = slot;
        }
        if (slot >= 0) {
            effect_t *effect = &effects[slot];
            /* Slots persist across phase transitions now, so refresh the
             * lifetime every tick or P_UpdateEffects reaps them once age_ms
             * exceeds the duration_ms captured when the slot was first spawned. */
            effect->age_ms = 0;
            effect->duration_ms = runtime->phase_duration_ms + 1;
            effect->core.position = ship->core.position;
            effect->core.frame = part->sprite_frame;
            effect->core.render_offset = part->offset;
            /* Palette remap is authored per animation label in DROP.FIN, not per team;
             * force the ship's own team color so it doesn't flip between phases. */
            effect->core.render_remap = ship->team;
            effect->core.render_intensity = part->render_intensity;
            effect->core.render_flags = (uint32_t)part->flags;
            effect->render_selector = part->render_selector;
            snprintf(effect->core.sprite_name, sizeof(effect->core.sprite_name),
                     "%s", part->sprite_name);
        }
        runtime_part++;
    }
    for (int i = runtime_part; i < DROPSHIP_MAX_PARTS; ++i) {
        int slot = runtime->effect_slots[i];
        if (slot >= 0 && slot < max_effects) memset(&effects[slot], 0, sizeof(effects[slot]));
        runtime->effect_slots[i] = -1;
    }
}

static const DropshipAnimation *dropship_animation(
    const DropshipSystem *ships, int state_id) {
    return state_id == S_DROPSHIP_UNLOAD ? &ships->dropship_animations.unload :
                                               &ships->dropship_animations.move;
}

static DropshipRuntime *reserve_dropship(
    DropshipSystem *ships, level_t *map, effect_t *effects, int max_effects,
    int team, int gx, int gy) {
    if (!ships || !effects || max_effects <= 0) return NULL;

    DropshipRuntime *runtime = NULL;
    for (int i = 0; i < (int)(sizeof(ships->dropships) / sizeof(ships->dropships[0])); ++i) {
        if (!ships->dropships[i].active) { runtime = &ships->dropships[i]; break; }
    }
    if (!runtime) return NULL;

    memset(runtime, 0, sizeof(*runtime));
    mobj_t *ship = &runtime->mobj;
    runtime->active = true;
    ship->team = team;
    runtime->origin = (ivec2_t){ gx, gy };
    runtime->phase_duration_ms = (DROPSHIP_FLIGHT_TICS * 1000 + 15) / 30;
    ship->movement.goal = fvec2_cell_center(runtime->origin);
    runtime->release_pending = true;
    for (int i = 0; i < DROPSHIP_MAX_PARTS; ++i) runtime->effect_slots[i] = -1;
    ship->type_id = MT_DROP_LINK;
    const mobjinfo_t *info = &game_info.mobjinfo[ship->type_id];
    ship->traits = (uint32_t)info->flags;
    ship->speed = (float)info->speed;
    ship->core.position = fixed3_from_fvec2(
        fvec2_cell_center((ivec2_t){ gx - 1, gy - 1 }), info->spawnz);
    ship->core.angle = dc_direction_to_angle(6);
    statecontext_t state_context = {
        .map = map, .mobjs = NULL, .mobj_count = NULL,
        .effects = effects, .max_effects = max_effects, .game_info = &game_info,
    };
    P_SetMobjState(&state_context, ship, info->spawnstate);
    sync_dropship_parts(
        runtime, &ships->dropship_animations.move, effects, max_effects, 0);

    return runtime;
}

static bool dropship_cell_occupied(const mobj_t *units, int unit_count,
                                   ivec2_t cell) {
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].remove || units[i].hp <= 0) continue;
        fvec2_t position = fixed3_xy_to_fvec2(units[i].core.position);
        if ((int)floorf(position.x) == cell.x &&
            (int)floorf(position.y) == cell.y) {
            return true;
        }
    }
    return false;
}

static fvec2_t dropship_drop_position(const level_t *map, const mobj_t *units,
                                      int unit_count, ivec2_t origin, int slot) {
    int formation_count = (int)(sizeof(drop_formation) /
                                sizeof(drop_formation[0]));
    for (int attempt = 0; attempt < formation_count; ++attempt) {
        ivec2_t offset = drop_formation[(slot + attempt) % formation_count];
        ivec2_t cell = { origin.x + offset.x, origin.y + offset.y };
        if ((!map || L_IsWalkable(map, cell.x, cell.y)) &&
            !dropship_cell_occupied(units, unit_count, cell)) {
            return fvec2_cell_center(cell);
        }
    }
    return fvec2_cell_center(origin);
}

static void set_dropship_duration(DropshipRuntime *runtime, int duration_ms) {
    runtime->phase_duration_ms = duration_ms > 0 ? duration_ms : 1;
}

static void dropship_unload_done(DropshipUpdateContext *context,
                                 DropshipRuntime *runtime) {
    mobj_t *ship = &runtime->mobj;
    fvec2_t center = fixed3_xy_to_fvec2(ship->core.position);
    if (runtime->release_pending && runtime->payload_index < runtime->payload_count) {
        DropshipPayload *payload = &runtime->payload[runtime->payload_index];
        ivec2_t release_cell = {
            (int)floorf(center.x), (int)floorf(center.y)
        };
        DC_SpawnReinforcement(context->map, context->units, context->unit_count, ship->team,
                          release_cell.x, release_cell.y,
                  payload->type, context->game_info);
        runtime->released_count++;
        payload->count--;
        if (payload->count <= 0) runtime->payload_index++;
        runtime->release_pending = false;
    }

    if (runtime->payload_index < runtime->payload_count) {
        ship->movement.goal = dropship_drop_position(
            context->map, context->units, *context->unit_count,
            runtime->origin, runtime->released_count);
        runtime->release_pending = true;
        set_dropship_duration(runtime, (DROPSHIP_FLIGHT_TICS * 1000 + 15) / 30);
        return;
    }

    ship->movement.goal = fvec2_cell_center((ivec2_t){
        runtime->origin.x - 1,
        runtime->origin.y - 1,
    });
    set_dropship_duration(runtime, (DROPSHIP_FLIGHT_TICS * 1000 + 15) / 30);
}

static void tick_dropship_state(DropshipSystem *ships, DropshipRuntime *runtime,
                            level_t *map, mobj_t *units, int *unit_count,
                            effect_t *effects, int max_effects,
                            const gameinfo_t *game_info, float dt) {
    statecontext_t state_context = {
        .map = map, .mobjs = units, .mobj_count = unit_count,
        .effects = effects, .max_effects = max_effects, .game_info = game_info,
    };
    mobj_t *ship = &runtime->mobj;
    int state_before = ship->core.state_id;
    bool moving = state_before == S_DROPSHIP_APPROACH ||
                  state_before == S_DROPSHIP_REPOSITION ||
                  state_before == S_DROPSHIP_DEPART;
    if (moving) {
        P_MoveMobjToward(NULL, ship, dt);
    }
    P_TickMobjState(&state_context, ship);
    int state_after = ship->core.state_id;

    if (state_before == S_DROPSHIP_UNLOAD && state_after != S_DROPSHIP_UNLOAD) {
        DropshipUpdateContext update = {
            .map = map, .units = units,
            .unit_count = unit_count, .game_info = game_info,
        };
        dropship_unload_done(&update, runtime);
        int next_state = S_DROPSHIP_DEPART;
        if (runtime->payload_index < runtime->payload_count) {
            fvec2_t delta = fvec2_sub(
                ship->movement.goal, fixed3_xy_to_fvec2(ship->core.position));
            next_state = fvec2_length_squared(delta) > 0.001f * 0.001f ?
                S_DROPSHIP_REPOSITION : S_DROPSHIP_UNLOAD;
        }
        P_SetMobjState(&state_context, ship, next_state);
    } else if (state_before == S_DROPSHIP_REPOSITION &&
               state_after != S_DROPSHIP_REPOSITION) {
        runtime->release_pending = true;
        P_SetMobjState(&state_context, ship, S_DROPSHIP_UNLOAD);
    } else if (state_before == S_DROPSHIP_DEPART &&
               state_after != S_DROPSHIP_DEPART) {
        clear_dropship_parts(runtime, effects, max_effects);
        runtime->active = false;
        ship->remove = true;
    }

    int elapsed_ms = runtime->phase_duration_ms -
                     ship->core.tics * 1000 / DROPSHIP_FLIGHT_TICS;
    if (elapsed_ms < 0) elapsed_ms = 0;
    sync_dropship_parts(runtime, dropship_animation(ships, ship->core.state_id),
                         effects, max_effects, elapsed_ms);
}

static void start_dropship_flight(statecontext_t *ctx, mobj_t *unit) {
    DropshipSystem *ships = ctx && ctx->map && ctx->map->mission ? ((Mission *)ctx->map->mission)->dropships : NULL;
    DropshipRuntime *runtime = dropship_runtime(ships, unit);
    if (!runtime) return;
    set_dropship_duration(runtime, (DROPSHIP_FLIGHT_TICS * 1000 + 15) / 30);
    unit->core.tics = DROPSHIP_FLIGHT_TICS;
    fvec2_t delta = fvec2_sub(
        unit->movement.goal, fixed3_xy_to_fvec2(unit->core.position));
    float dist = sqrtf(fvec2_length_squared(delta));
    if (dist > 0.001f && runtime->phase_duration_ms > 0)
        unit->speed = dist / ((float)runtime->phase_duration_ms / 1000.0f);
}

void A_DC_DropshipApproach(statecontext_t *ctx, mobj_t *unit) {
    start_dropship_flight(ctx, unit);
}

void A_DC_DropshipUnload(statecontext_t *ctx, mobj_t *unit) {
    DropshipSystem *ships = ctx && ctx->map && ctx->map->mission ? ((Mission *)ctx->map->mission)->dropships : NULL;
    DropshipRuntime *runtime = dropship_runtime(ships, unit);
    if (!runtime) return;
    set_dropship_duration(runtime, ships->dropship_animations.unload.duration_ms);
    unit->core.tics = (runtime->phase_duration_ms * 30 + 999) / 1000;
    if (unit->core.tics < 1) unit->core.tics = 1;
    if (ctx) sync_dropship_parts(runtime, &ships->dropship_animations.unload,
                                 ctx->effects, ctx->max_effects, 0);
}

void A_DC_DropshipReposition(statecontext_t *ctx, mobj_t *unit) {
    start_dropship_flight(ctx, unit);
}

void A_DC_DropshipDepart(statecontext_t *ctx, mobj_t *unit) {
    start_dropship_flight(ctx, unit);
}

DropshipSystem *DC_LoadDropships(const char *map_path) {
    DropshipSystem *ships = calloc(1, sizeof(*ships));
    if (ships) DC_LoadDropshipAnimations(map_path, &ships->dropship_animations);
    return ships;
}

void DC_FreeDropships(DropshipSystem *ships) { free(ships); }

bool DC_StartDropship(level_t *map, effect_t *effects, int max_effects,
                      int team, ivec2_t origin,
                      const DropshipPayload *payload, int payload_count) {
    Mission *owner = map ? map->mission : NULL;
    if (!owner || !payload || payload_count <= 0 ||
        payload_count > DROPSHIP_MAX_PAYLOAD_TYPES) return false;
    DropshipRuntime *ship = reserve_dropship(owner->dropships, map, effects,
                                              max_effects, team, origin.x, origin.y);
    if (!ship) return false;
    memcpy(ship->payload, payload, (size_t)payload_count * sizeof(*payload));
    ship->payload_count = payload_count;
    return true;
}

void DC_UpdateDropships(level_t *map, mobj_t *units, int *unit_count,
                        effect_t *effects, int max_effects,
                        const gameinfo_t *game_info, float dt) {
    Mission *owner = map ? map->mission : NULL;
    DropshipSystem *ships = owner ? owner->dropships : NULL;
    if (!ships) return;
    for (int i = 0; i < (int)(sizeof(ships->dropships) / sizeof(ships->dropships[0])); ++i) {
        DropshipRuntime *runtime = &ships->dropships[i];
        if (!runtime->active) continue;
        tick_dropship_state(ships, runtime, map, units, unit_count,
                                    effects, max_effects,
                                    game_info, dt);
    }
}

