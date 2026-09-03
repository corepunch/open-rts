#define _DEFAULT_SOURCE
#include "g_game.h"

#include "engine.h"
#include "game.h"
#include "p_ai.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct RtsGameModel {
    level_t map;
    mobj_t units[MAXMOBJS];
    int unit_count;
    effect_t effects[MAX_VISUAL_EFFECTS];
    hudtext_t hud;
    void *mission;
    bool loaded;
    char error[256];
    uint64_t tick;
    uint32_t next_unit_id;
    RtsGameEvent events[256];
    int event_head;
    int event_count;
    AiContext ai;
};

static void model_set_error(RtsGameModel *model, const char *fmt, ...) {
    if (!model) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(model->error, sizeof(model->error), fmt, args);
    va_end(args);
}

static void model_emit_event(void *user, int type, const mobj_t *subject,
                             const mobj_t *target, int product_class, int product_type) {
    RtsGameModel *model = user;
    if (!model || model->event_count >= (int)(sizeof(model->events) / sizeof(model->events[0]))) return;
    int slot = (model->event_head + model->event_count) %
        (int)(sizeof(model->events) / sizeof(model->events[0]));
    RtsGameEvent *event = &model->events[slot];
    memset(event, 0, sizeof(*event));
    event->type = (RtsGameEventType)type;
    event->tick = model->tick;
    event->subject_id = subject ? subject->id : 0;
    event->target_id = target ? target->id : 0;
    event->subject_type_id = subject ? subject->type_id : 0;
    event->target_type_id = target ? target->type_id : 0;
    event->product_class = product_class;
    event->product_type = product_type;
    event->position = subject ? fixedvec3_xy_to_fvec2(subject->core.position) :
        (fvec2_t){ 0.0f, 0.0f };
    model->event_count++;
}

static void model_emit_build_completion(RtsGameModel *model, const mobj_t *unit,
                                        const mobj_t *producer,
                                        const StaticProductDefinition *product) {
    if (!model || !unit || !product) return;
    model_emit_event(model, RTS_GAME_EVENT_BUILD_FINISHED, unit, producer,
                     product->product_class, product->product_type);
    model_emit_event(model,
                     product->product_class == RTS_PRODUCT_UNIT ?
                         RTS_GAME_EVENT_UNIT_BUILT : RTS_GAME_EVENT_BUILDING_BUILT,
                     unit, producer, product->product_class, product->product_type);
}

static const actortype_t *plugin_actor_type_by_id(uint16_t type_id) {
    for (int i = 0; i < num_mobjinfo; ++i) {
        if (mobjinfo[i].id == type_id) return (const actortype_t *)&mobjinfo[i];
    }
    return NULL;
}

static const actortype_t *plugin_actor_type_for_unit(const mobj_t *unit) {
    const actortype_t *type = plugin_actor_type_by_id(unit ? unit->type_id : 0);
    if (type) return type;
    if (!unit) return NULL;
    for (int i = 0; i < num_mobjinfo; ++i) {
        const char *sprite = mobjinfo[i].sprite_name;
        if (sprite && sprite[0] != '\0' && strcasecmp(sprite, unit->core.sprite_name) == 0)
            return (const actortype_t *)&mobjinfo[i];
    }
    return num_mobjinfo > 0 ? (const actortype_t *)&mobjinfo[0] : NULL;
}

static void apply_actor_type_defaults(mobj_t *unit, const actortype_t *type) {
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
    if (unit->core.sprite_name[0] == '\0' && type->sprite_name) {
        snprintf(unit->core.sprite_name, sizeof(unit->core.sprite_name), "%s", type->sprite_name);
    }
    if (unit->shadow_name[0] == '\0' && type->shadow_name) {
        snprintf(unit->shadow_name, sizeof(unit->shadow_name), "%s", type->shadow_name);
    }
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

static void apply_plugin_actor_defaults(RtsGameModel *model) {
    if (!model) return;
    mobj_t *units = model->units;
    int unit_count = model->unit_count;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].id == 0) units[i].id = ++model->next_unit_id;
        apply_actor_type_defaults(&units[i], plugin_actor_type_for_unit(&units[i]));
        P_SpawnMobj(gameinfo, &units[i]);
    }
}

bool G_ModelHasActorType(const RtsGameModel *model, int owner, uint16_t actor_id) {
    if (!model || actor_id == 0) return false;
    for (int i = 0; i < model->unit_count; ++i) {
        if (model->units[i].owner == owner && !model->units[i].remove &&
            model->units[i].hp > 0 && model->units[i].type_id == actor_id)
            return true;
    }
    return false;
}

int G_ModelFindProducerIndex(const RtsGameModel *model, int owner,
                             const StaticProductDefinition *product) {
    if (!model || !product) return -1;
    for (int i = 0; i < product->maker_count; ++i) {
        for (int j = 0; j < model->unit_count; ++j) {
            const mobj_t *unit = &model->units[j];
            if (unit->owner == owner && !unit->remove && unit->hp > 0 &&
                unit->type_id == (uint16_t)product->makers[i])
                return j;
        }
    }
    return -1;
}

static bool model_position_available(const RtsGameModel *model, float gx, float gy,
                                     float radius) {
    if (!model) return false;
    if (radius < 0.32f) radius = 0.32f;
    if (gx - radius < 0.0f || gy - radius < 0.0f ||
        gx + radius > (float)model->map.width || gy + radius > (float)model->map.height) {
        return false;
    }

    int min_x = (int)floorf(gx - radius);
    int max_x = (int)floorf(gx + radius);
    int min_y = (int)floorf(gy - radius);
    int max_y = (int)floorf(gy + radius);
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            if (!L_IsWalkable(&model->map, x, y)) return false;
        }
    }

    for (int i = 0; i < model->unit_count; ++i) {
        const mobj_t *other = &model->units[i];
        if (other->remove || other->hp <= 0) continue;
        float other_radius = other->radius > 0.05f ? other->radius : 0.42f;
        float min_dist = radius + other_radius;
        if (fvec2_distance_squared(fixedvec3_xy_to_fvec2(other->core.position),
                                   (fvec2_t){ gx, gy }) <
            min_dist * min_dist) return false;
    }
    return true;
}

static bool model_position_walkable_only(const RtsGameModel *model, float gx, float gy,
                                         float radius) {
    if (!model) return false;
    if (radius < 0.32f) radius = 0.32f;
    if (gx - radius < 0.0f || gy - radius < 0.0f ||
        gx + radius > (float)model->map.width || gy + radius > (float)model->map.height) {
        return false;
    }

    int min_x = (int)floorf(gx - radius);
    int max_x = (int)floorf(gx + radius);
    int min_y = (int)floorf(gy - radius);
    int max_y = (int)floorf(gy + radius);
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            if (!L_IsWalkable(&model->map, x, y)) return false;
        }
    }
    return true;
}

static bool find_spawn_position_near(const RtsGameModel *model, const mobj_t *producer,
                                     float radius, float *out_gx, float *out_gy) {
    if (!model || !producer || !out_gx || !out_gy) return false;
    fvec2_t producer_position = fixedvec3_xy_to_fvec2(producer->core.position);
    int origin_x = (int)floorf(producer_position.x);
    int origin_y = (int)floorf(producer_position.y);
    static const int preferred[][2] = {
        { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 },
        { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 },
    };
    int preferred_count = (int)(sizeof(preferred) / sizeof(preferred[0]));
    for (int dist = 1; dist <= 8; ++dist) {
        for (int i = 0; i < preferred_count; ++i) {
            int x = origin_x + preferred[i][0] * dist;
            int y = origin_y + preferred[i][1] * dist;
            float candidate_gx = (float)x + 0.5f;
            float candidate_gy = (float)y + 0.5f;
            if (!model_position_available(model, candidate_gx, candidate_gy, radius)) continue;
            *out_gx = candidate_gx;
            *out_gy = candidate_gy;
            return true;
        }
        for (int dy = -dist; dy <= dist; ++dy) {
            for (int dx = -dist; dx <= dist; ++dx) {
                if (dx != -dist && dx != dist && dy != -dist && dy != dist) continue;
                float candidate_gx = (float)(origin_x + dx) + 0.5f;
                float candidate_gy = (float)(origin_y + dy) + 0.5f;
                if (!model_position_available(model, candidate_gx, candidate_gy, radius)) continue;
                *out_gx = candidate_gx;
                *out_gy = candidate_gy;
                return true;
            }
        }
    }
    return false;
}

static void order_barracks_exit_spacing(RtsGameModel *model, int spawned_index,
                                        const mobj_t *producer, float exit_gx,
                                        float exit_gy) {
    if (!model || !producer || spawned_index < 0 || spawned_index >= model->unit_count)
        return;
    bool saved[MAXMOBJS];
    for (int i = 0; i < model->unit_count; ++i) {
        saved[i] = model->units[i].selected;
        model->units[i].selected = false;
    }

    float crowd_radius = 2.75f;
    float crowd_radius_sq = crowd_radius * crowd_radius;
    for (int i = 0; i < model->unit_count; ++i) {
        mobj_t *unit = &model->units[i];
        if (unit->remove || unit->hp <= 0 || unit->owner != producer->owner ||
            (unit->traits & MF_MOBILE) == 0) {
            continue;
        }
        if (i == spawned_index ||
            fvec2_distance_squared(fixedvec3_xy_to_fvec2(unit->core.position),
                                   (fvec2_t){ exit_gx, exit_gy }) <= crowd_radius_sq) {
            unit->selected = true;
        }
    }

    fvec2_t delta = fvec2_sub((fvec2_t){ exit_gx, exit_gy },
                             fixedvec3_xy_to_fvec2(producer->core.position));
    float len = sqrtf(fvec2_length_squared(delta));
    if (len < 0.01f) {
        delta = (fvec2_t){ 0.0f, -1.0f };
        len = 1.0f;
    }
    fvec2_t goal = fvec2_add((fvec2_t){ exit_gx, exit_gy },
                            fvec2_scale(delta, 1.5f / len));
    P_MoveOrderAt(&model->map, model->units, model->unit_count, goal);
    for (int i = 0; i < model->unit_count; ++i) {
        model->units[i].selected = saved[i];
    }
}

static bool spawn_finished_model_product(RtsGameModel *model,
                                         const StaticProductDefinition *product,
                                         int producer_index) {
    if (!model || !product || producer_index < 0 ||
        producer_index >= model->unit_count) {
        return false;
    }
    if (model->unit_count >= MAXMOBJS) return false;

    uint16_t actor_id = G_ModelActorIdForProduct(product);
    const actortype_t *actor_type = plugin_actor_type_by_id(actor_id);
    if (actor_id == 0 || !actor_type) return false;

    mobj_t new_unit;
    memset(&new_unit, 0, sizeof(new_unit));
    new_unit.type_id = actor_id;
    new_unit.id = ++model->next_unit_id;
    new_unit.owner = model->units[producer_index].owner;
    new_unit.core.sprite_id = -1;
    new_unit.core.angle = ANG90;
    new_unit.attack.target = -1;
    new_unit.harvest.target = -1;
    new_unit.core.frame = G_ModelBuildingFrameForProduct(product);
    apply_actor_type_defaults(&new_unit, actor_type);
    P_SpawnMobj(gameinfo, &new_unit);
    if (product->product_class == RTS_PRODUCT_BUILDING) {
        int state_id = G_ModelBuildingStateForProduct(gameinfo, product);
        if (state_id > 0) {
            statecontext_t ctx = { .game_info = gameinfo };
            P_SetMobjState(&ctx, &new_unit, state_id);
        }
        if ((actor_type->traits & MF_MOBILE) == 0)
            new_unit.radius = 1.2f;
    }
    float radius = new_unit.radius > 0.05f ? new_unit.radius : 0.42f;

    float gx = 0.0f;
    float gy = 0.0f;
    mobj_t *producer = &model->units[producer_index];
    bool use_special_release = false;
    if (G_ModelSpecialReleaseSpawnPoint(model, producer, product, &new_unit, &gx, &gy) &&
        model_position_walkable_only(model, gx, gy, radius)) {
        use_special_release = true;
    } else if (!find_spawn_position_near(model, producer, radius, &gx, &gy)) {
        return false;
    }

    new_unit.core.position = fixedvec3_from_fvec2((fvec2_t){ gx, gy }, 0);
    int spawned_index = model->unit_count;
    model->units[model->unit_count++] = new_unit;
    model_emit_build_completion(model, &model->units[spawned_index], producer, product);
    if (use_special_release)
        order_barracks_exit_spacing(model, spawned_index, producer, gx, gy);
    return true;
}

static bool enqueue_model_unit_product(RtsGameModel *model,
                                       const StaticProductDefinition *product,
                                       int producer_index,
                                       uint16_t actor_id) {
    if (!model || !product || producer_index < 0 || producer_index >= model->unit_count)
        return false;
    mobj_t *producer = &model->units[producer_index];
    if (producer->production.queue_count > 0) {
        if (producer->production.actor_id != actor_id ||
            producer->production.product_type != product->product_type ||
            producer->production.product_class != (uint8_t)product->product_class ||
            producer->production.queue_count >= RTS_MAX_PRODUCTION_QUEUE) {
            return false;
        }
        producer->production.queue_count++;
        model_emit_event(model, RTS_GAME_EVENT_BUILD_QUEUED, producer, NULL,
                         product->product_class, product->product_type);
        return true;
    }

    producer->production.actor_id = actor_id;
    producer->production.product_class = (uint8_t)product->product_class;
    producer->production.product_type = product->product_type;
    producer->production.queue_count = 1;
    producer->production.time_ms = G_ModelProductTrainingTimeMs(product);
    producer->production.time_left_ms = producer->production.time_ms;
    producer->production.release_active = false;
    producer->production.release_time_left_ms = 0;
    producer->production.blocked = false;
    model_emit_event(model, RTS_GAME_EVENT_BUILD_QUEUED, producer, NULL,
                     product->product_class, product->product_type);
    model_emit_event(model, RTS_GAME_EVENT_BUILD_STARTED, producer, NULL,
                     product->product_class, product->product_type);
    return true;
}

static void clear_model_production(mobj_t *producer) {
    if (!producer) return;
    producer->production.actor_id = 0;
    producer->production.product_class = 0;
    producer->production.product_type = 0;
    producer->production.time_ms = 0;
    producer->production.time_left_ms = 0;
    producer->production.release_active = false;
    producer->production.release_time_left_ms = 0;
}

static void advance_model_production_queue(mobj_t *producer) {
    if (!producer) return;
    producer->production.release_active = false;
    producer->production.release_time_left_ms = 0;
    producer->production.queue_count--;
    if (producer->production.queue_count > 0) {
        producer->production.time_left_ms = producer->production.time_ms;
    } else {
        clear_model_production(producer);
    }
}

static bool create_model_product_for_owner(RtsGameModel *model, int owner,
                                           const StaticProductDefinition *product) {
    if (!model || !product) return false;
    if (!G_ModelProductAvailable(model, owner, product)) return false;
    if (model->map.player_resources[owner][0] < product->cost) return false;

    uint16_t actor_id = G_ModelActorIdForProduct(product);
    if (actor_id == 0 || !plugin_actor_type_by_id(actor_id)) return false;

    int producer_index = G_ModelFindProducerIndex(model, owner, product);
    if (producer_index < 0) return false;

    if (G_ModelProductTrainingTimeMs(product) > 0) {
        if (!enqueue_model_unit_product(model, product, producer_index, actor_id)) return false;
        model->map.player_resources[owner][0] -= product->cost;
        return true;
    }

    model_emit_event(model, RTS_GAME_EVENT_BUILD_QUEUED, &model->units[producer_index], NULL,
                     product->product_class, product->product_type);
    if (!spawn_finished_model_product(model, product, producer_index)) return false;
    model->map.player_resources[owner][0] -= product->cost;
    return true;
}

static bool create_model_product(RtsGameModel *model,
                                 const StaticProductDefinition *product) {
    return create_model_product_for_owner(model, 0, product);
}

static void update_model_production(RtsGameModel *model, float dt) {
    if (!model || dt <= 0.0f) return;
    int elapsed_ms = (int)(dt * 1000.0f + 0.5f);
    if (elapsed_ms <= 0) elapsed_ms = 1;
    for (int i = 0; i < model->unit_count; ++i) {
        mobj_t *producer = &model->units[i];
        if (producer->production.queue_count <= 0) continue;
        if (producer->remove || producer->hp <= 0) {
            if (!producer->production.blocked)
                model_emit_event(model, RTS_GAME_EVENT_BUILD_BLOCKED, producer, NULL,
                                 producer->production.product_class,
                                 producer->production.product_type);
            producer->production.blocked = true;
            producer->production.queue_count = 0;
            clear_model_production(producer);
            continue;
        }
        if (producer->production.release_active) {
            producer->production.release_time_left_ms -= elapsed_ms;
            if (producer->production.release_time_left_ms > 0) continue;
            const StaticProductDefinition *product = G_ModelProductByClassType(
                model, producer->production.product_class,
                producer->production.product_type);
            if (!product || !spawn_finished_model_product(model, product, i)) {
                if (!producer->production.blocked)
                    model_emit_event(model, RTS_GAME_EVENT_BUILD_BLOCKED, producer, NULL,
                                     producer->production.product_class,
                                     producer->production.product_type);
                producer->production.blocked = true;
                producer->production.release_time_left_ms = 250;
                continue;
            }
            producer = &model->units[i];
            advance_model_production_queue(producer);
            continue;
        }
        producer->production.time_left_ms -= elapsed_ms;
        while (producer->production.queue_count > 0 &&
               producer->production.time_left_ms <= 0) {
            const StaticProductDefinition *product = G_ModelProductByClassType(
                model, producer->production.product_class,
                producer->production.product_type);
            if (!product) {
                producer->production.time_left_ms = 250;
                break;
            }
            if (G_ModelStartProductionRelease(model, producer, product,
                                              producer->production.actor_id)) {
                break;
            }
            if (!spawn_finished_model_product(model, product, i)) {
                if (!producer->production.blocked)
                    model_emit_event(model, RTS_GAME_EVENT_BUILD_BLOCKED, producer, NULL,
                                     producer->production.product_class,
                                     producer->production.product_type);
                producer->production.blocked = true;
                producer->production.time_left_ms = 250;
                break;
            }
            producer = &model->units[i];
            advance_model_production_queue(producer);
        }
    }
}

static void destroy_model_map(level_t *map) {
    if (!map) return;
    free(map->tile_ids);
    for (int i = 0; i < MAX_TILE_OVERLAYS; ++i) free(map->tile_overlays[i]);
    for (int i = 0; i < MAX_TILE_OVERLAYS + 1; ++i) free(map->tile_transforms[i]);
    free(map->blocked);
    free(map->cell_colors);
    free(map->decorations);
    free(map->resource_vents);
    free(map->extras);
    memset(map, 0, sizeof(*map));
}

static bool build_map_path(char *out, size_t out_size, const char *data_root, const char *map_path) {
    if (!out || out_size == 0 || !data_root || !map_path) return false;
    if (map_path[0] == '/') {
        snprintf(out, out_size, "%s", map_path);
    } else {
        M_PathJoin(out, out_size, data_root, map_path);
    }
    return true;
}

RtsGameModel *rts_game_model_create(void) {
    return calloc(1, sizeof(RtsGameModel));
}

void rts_game_model_destroy(RtsGameModel *model) {
    if (!model) return;
    if (model->mission) G_FreeMission(model->mission);
    destroy_model_map(&model->map);
    free(model);
}

bool rts_game_model_load(RtsGameModel *model, const RtsGameModelConfig *config) {
    if (!model || !config) return false;
    G_InitGame();
    const char *data_root = config->data_root && config->data_root[0] ?
        config->data_root : g_game_default_root;
    const char *map_rel_or_abs = config->map_path && config->map_path[0] ?
        config->map_path : g_game_default_map;

    char map_path[1024];
    if (!build_map_path(map_path, sizeof(map_path), data_root, map_rel_or_abs)) {
        model_set_error(model, "invalid data root or map path");
        return false;
    }

    if (model->loaded) {
        if (model->mission) G_FreeMission(model->mission);
        model->mission = NULL;
        destroy_model_map(&model->map);
        memset(model->units, 0, sizeof(model->units));
        memset(model->effects, 0, sizeof(model->effects));
        memset(&model->hud, 0, sizeof(model->hud));
        model->unit_count = 0;
        model->tick = 0;
        model->next_unit_id = 0;
        model->event_head = 0;
        model->event_count = 0;
        model->loaded = false;
    }

    if (!G_DoLoadLevel(map_path, &model->map)) {
        model_set_error(model, "failed to load map '%s'", map_path);
        return false;
    }
    model->unit_count = P_LoadThings(map_path, (mobj_t *)model->units, MAXMOBJS);
    apply_plugin_actor_defaults(model);
    model->mission = G_LoadMission(map_path);
    P_AiInit(&model->ai);
    model->loaded = true;
    model->error[0] = '\0';
    return true;
}

bool rts_game_model_tick(RtsGameModel *model, float dt) {
    if (!model || !model->loaded) return false;
    if (dt <= 0.0f) return true;
    uint32_t old_ids[MAXMOBJS];
    uint16_t old_types[MAXMOBJS];
    int old_hp[MAXMOBJS];
    int old_state[MAXMOBJS];
    int old_targets[MAXMOBJS];
    bool old_arrived[MAXMOBJS];
    int old_count = model->unit_count;
    for (int i = 0; i < old_count; ++i) {
        old_ids[i] = model->units[i].id;
        old_types[i] = model->units[i].type_id;
        old_hp[i] = model->units[i].hp;
        old_state[i] = model->units[i].core.state_id;
        old_targets[i] = model->units[i].attack.target;
        old_arrived[i] = model->units[i].movement.order_arrived;
    }
    model->tick++;
    P_Ticker(&model->map, model->units, &model->unit_count, model->effects,
                 MAX_VISUAL_EFFECTS, gameinfo, dt);
    P_AiTick(&model->ai, &model->map, model->units, model->unit_count,
             gameinfo, (int)(dt * 1000.0f));
    if (model->mission) {
        G_MissionTicker(model->mission, &model->map, (mobj_t *)model->units,
                        &model->unit_count, model->effects,
                        MAX_VISUAL_EFFECTS, &model->hud, dt);
    }
    G_ModelAIProduction(model, (int)(dt * 1000.0f));
    update_model_production(model, dt);
    P_UpdateEffects(&model->map, model->effects, MAX_VISUAL_EFFECTS,
                          gameinfo, dt);
    HU_Ticker(&model->hud, dt);
    for (int i = 0; i < old_count; ++i) {
        int now = -1;
        for (int j = 0; j < model->unit_count; ++j)
            if (model->units[j].id == old_ids[i]) { now = j; break; }
        if (now < 0 && old_hp[i] > 0) {
            RtsGameEvent event = { .type = RTS_GAME_EVENT_UNIT_DIED,
                                   .tick = model->tick, .subject_id = old_ids[i],
                                   .subject_type_id = old_types[i] };
            if (model->event_count < (int)(sizeof(model->events) / sizeof(model->events[0]))) {
                int slot = (model->event_head + model->event_count) %
                    (int)(sizeof(model->events) / sizeof(model->events[0]));
                model->events[slot] = event;
                model->event_count++;
            }
            continue;
        }
        if (now < 0) continue;
        mobj_t *unit = &model->units[now];
        if (old_hp[i] > 0 && unit->hp <= 0)
            model_emit_event(model, RTS_GAME_EVENT_UNIT_DIED, unit, NULL, 0, 0);
        if (!old_arrived[i] && unit->movement.order_arrived)
            model_emit_event(model, RTS_GAME_EVENT_UNIT_ARRIVED, unit, NULL, 0, 0);
        const state_t *old_s = (gameinfo && gameinfo->states && old_state[i] >= 0 && old_state[i] < gameinfo->state_count) ?
            &gameinfo->states[old_state[i]] : NULL;
        const state_t *new_s = (gameinfo && gameinfo->states && unit->core.state_id >= 0 && unit->core.state_id < gameinfo->state_count) ?
            &gameinfo->states[unit->core.state_id] : NULL;
        if ((!old_s || old_s->misc1 != 3) && new_s && new_s->misc1 == 3) {
            const mobj_t *target = NULL;
            if (unit->attack.target >= 0 && unit->attack.target < model->unit_count)
                target = &model->units[unit->attack.target];
            else if (old_targets[i] >= 0 && old_targets[i] < old_count)
                for (int j = 0; j < model->unit_count; ++j)
                    if (model->units[j].id == old_ids[old_targets[i]]) { target = &model->units[j]; break; }
            model_emit_event(model, RTS_GAME_EVENT_ATTACK_STARTED, unit, target, 0, 0);
        }
    }
    return true;
}

bool rts_game_model_command(RtsGameModel *model, const RtsGameCommand *command) {
    if (!model || !model->loaded || !command) return false;
    switch (command->kind) {
    case RTS_GAME_COMMAND_NONE:
        return true;
    case RTS_GAME_COMMAND_SELECT_ALL_PLAYER_UNITS:
        for (int i = 0; i < model->unit_count; ++i) {
            mobj_t *unit = &model->units[i];
            unit->selected = unit->owner == 0 && unit->hp > 0 &&
                (unit->traits & MF_SELECTABLE) != 0;
        }
        return true;
    case RTS_GAME_COMMAND_SELECT_UNIT_INDEX:
        if (command->data.select_unit_index.unit_index < 0 ||
            command->data.select_unit_index.unit_index >= model->unit_count) {
            return false;
        }
        if (!command->data.select_unit_index.additive) {
            for (int i = 0; i < model->unit_count; ++i) model->units[i].selected = false;
        }
        model->units[command->data.select_unit_index.unit_index].selected = true;
        return true;
    case RTS_GAME_COMMAND_MOVE_SELECTED: {
        P_MoveOrderAt(&model->map, model->units, model->unit_count,
                      command->data.move_selected.target);
        for (int i = 0; i < model->unit_count; ++i)
            if (model->units[i].selected && model->units[i].movement.order_arrived)
                model_emit_event(model, RTS_GAME_EVENT_UNIT_ARRIVED, &model->units[i], NULL, 0, 0);
        return true;
    }
    case RTS_GAME_COMMAND_HARVEST_SELECTED:
        return P_HarvestOrderAt(&model->map, model->units, model->unit_count,
                                command->data.harvest_selected.target);
    case RTS_GAME_COMMAND_ATTACK_UNIT: {
        int target = command->data.attack_unit.target_index;
        if (command->data.attack_unit.target_id != 0) {
            target = -1;
            for (int i = 0; i < model->unit_count; ++i)
                if (model->units[i].id == command->data.attack_unit.target_id) { target = i; break; }
        }
        if (target < 0 || target >= model->unit_count || model->units[target].hp <= 0) return false;
        for (int i = 0; i < model->unit_count; ++i) {
            mobj_t *unit = &model->units[i];
            if (unit->selected && unit->owner == 0 && (unit->traits & MF_ATTACK))
                unit->attack.target = target;
        }
        P_MoveOrderAt(&model->map, model->units, model->unit_count,
                      fixedvec3_xy_to_fvec2(model->units[target].core.position));
        return true;
    }
    case RTS_GAME_COMMAND_BUILD_PRODUCT: {
        int producer = command->data.build_product.producer_index;
        if (command->data.build_product.producer_id != 0) {
            producer = -1;
            for (int i = 0; i < model->unit_count; ++i)
                if (model->units[i].id == command->data.build_product.producer_id) { producer = i; break; }
        }
        if (producer < 0 || producer >= model->unit_count) return false;
        const StaticProductDefinition *product = G_ModelProductByUIId(model, command->data.build_product.ui_id);
        if (!product || !G_ModelProductAvailable(model, 0, product) ||
            model->map.player_resources[0][0] < product->cost ||
            G_ModelFindProducerIndex(model, 0, product) != producer) return false;
        uint16_t actor_id = G_ModelActorIdForProduct(product);
        bool queued = G_ModelProductTrainingTimeMs(product) > 0;
        if (!queued)
            model_emit_event(model, RTS_GAME_EVENT_BUILD_QUEUED, &model->units[producer], NULL,
                             product->product_class, product->product_type);
        bool ok = queued ? enqueue_model_unit_product(model, product, producer, actor_id) :
            spawn_finished_model_product(model, product, producer);
        if (ok) model->map.player_resources[0][0] -= product->cost;
        return ok;
    }
    case RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON:
        return create_model_product(
            model, G_ModelProductByUIId(model, command->data.activate_ui_button.ui_id));
    default:
        return false;
    }
}

bool rts_game_model_poll_event(RtsGameModel *model, RtsGameEvent *out) {
    if (!model || !out || model->event_count <= 0) return false;
    *out = model->events[model->event_head];
    model->event_head = (model->event_head + 1) %
        (int)(sizeof(model->events) / sizeof(model->events[0]));
    model->event_count--;
    return true;
}

bool rts_game_model_snapshot(const RtsGameModel *model, RtsRenderSnapshot *out) {
    if (!model || !model->loaded || !out) return false;
    memset(out, 0, sizeof(*out));
    out->map_width = model->map.width;
    out->map_height = model->map.height;
    out->decoration_count = model->map.decoration_count;
    if (out->decoration_count > RTS_MODEL_MAX_SNAPSHOT_DECORATIONS)
        out->decoration_count = RTS_MODEL_MAX_SNAPSHOT_DECORATIONS;
    out->resource_vent_count = model->map.resource_vent_count;
    for (int i = 0; i < RTS_MODEL_MAX_PLAYERS; ++i) {
        for (int r = 0; r < RTS_MODEL_MAX_RESOURCES; ++r)
            out->player_resources[i][r] = model->map.player_resources[i][r];
    }
    out->unit_count = model->unit_count;
    if (out->unit_count > RTS_MODEL_MAX_SNAPSHOT_UNITS)
        out->unit_count = RTS_MODEL_MAX_SNAPSHOT_UNITS;
    for (int i = 0; i < out->unit_count; ++i) {
        const mobj_t *src = &model->units[i];
        RtsRenderUnit *dst = &out->units[i];
        dst->position = fixedvec3_xy_to_fvec2(src->core.position);
        dst->move_goal = src->movement.goal;
        dst->type_id = src->type_id;
        dst->owner = src->owner;
        dst->traits = src->traits;
        dst->hp = src->hp;
        dst->max_hp = src->max_hp;
        dst->frame = src->core.frame;
        dst->facing_code = angle_to_direction(src->core.angle, 32, ANG90, true);
        dst->state_id = src->core.state_id;
        dst->id = src->id;
        dst->render_flags = src->core.render_flags;
        dst->render_remap = src->core.render_remap;
        dst->render_intensity = src->core.render_intensity;
        dst->render_offset = src->core.render_offset;
        dst->selected = src->selected;
        dst->has_move_order = src->movement.order_id != 0;
        dst->harvest_target = src->harvest.target;
        snprintf(dst->sprite_name, sizeof(dst->sprite_name), "%s", src->core.sprite_name);
        snprintf(dst->shadow_name, sizeof(dst->shadow_name), "%s", src->shadow_name);
    }
    for (int i = 0; i < out->decoration_count; ++i) {
        const mapdecoration_t *src = &model->map.decorations[i];
        RtsRenderDecoration *dst = &out->decorations[i];
        dst->cell = src->cell;
        dst->footprint = src->footprint;
        dst->hidden = src->hidden;
        dst->center_anchor = src->center_anchor;
        dst->has_sprite_pivot = src->has_sprite_pivot;
        dst->sprite_pivot = src->sprite_pivot;
        dst->frame_interval_ms = src->frame_interval_ms;
        dst->frame_index = src->frame_index;
        dst->frame2_index = src->frame2_index;
        dst->frame3_index = src->frame3_index;
        dst->facing_code = angle_to_direction(src->angle, 32, ANG90, true);
        dst->render_remap = src->render_remap;
        dst->render_flags = src->render_flags;
        dst->render_selector = src->render_selector;
        dst->render2_flags = src->render2_flags;
        dst->render2_selector = src->render2_selector;
        dst->render3_flags = src->render3_flags;
        dst->render3_selector = src->render3_selector;
        snprintf(dst->sprite_name, sizeof(dst->sprite_name), "%s", src->sprite_name);
        snprintf(dst->sprite2_name, sizeof(dst->sprite2_name), "%s", src->sprite2_name);
        snprintf(dst->sprite3_name, sizeof(dst->sprite3_name), "%s", src->sprite3_name);
        snprintf(dst->shadow_name, sizeof(dst->shadow_name), "%s", src->shadow_name);
        snprintf(dst->sequence_name, sizeof(dst->sequence_name), "%s", src->sequence_name);
    }
    for (int i = 0; i < MAX_VISUAL_EFFECTS && out->effect_count < RTS_MODEL_MAX_SNAPSHOT_EFFECTS; ++i) {
        const effect_t *src = &model->effects[i];
        if (!src->active) continue;
        RtsRenderEffect *dst = &out->effects[out->effect_count++];
        dst->active = src->active;
        dst->position = fixedvec3_xy_to_fvec2(src->core.position);
        dst->frame = src->core.frame;
        dst->render_flags = src->core.render_flags;
        dst->render_remap = src->core.render_remap;
        dst->render_intensity = src->core.render_intensity;
        dst->render_selector = src->render_selector;
        dst->ground_light = src->ground_light;
        dst->light_radius = src->light_radius;
        snprintf(dst->sprite_name, sizeof(dst->sprite_name), "%s", src->core.sprite_name);
        snprintf(dst->sequence_name, sizeof(dst->sequence_name), "%s", src->sequence_name);
    }
    G_ModelBuildUIScript(model, out, out->ui_script, sizeof(out->ui_script));
    return true;
}

const char *rts_game_model_last_error(const RtsGameModel *model) {
    return model && model->error[0] ? model->error : "";
}

int rts_game_model_player_resources(const RtsGameModel *model, int player, int resource_type) {
    if (!model || player < 0 || player >= 8 || resource_type < 0 || resource_type >= RTS_MAX_RESOURCES)
        return 0;
    return model->map.player_resources[player][resource_type];
}

int rts_game_model_products(const RtsGameModel *model, RtsProductDefinition *out, int max_products) {
    if (!model || !out || max_products <= 0) return 0;
    StaticProductDefinition static_defs[64];
    int static_count = G_ModelGetProducts(model, 0, static_defs, 64);
    int count = static_count < max_products ? static_count : max_products;
    for (int i = 0; i < count; ++i) {
        const StaticProductDefinition *src = &static_defs[i];
        RtsProductDefinition *dst = &out[i];
        memset(dst, 0, sizeof(*dst));
        dst->ui_id = src->ui_id;
        snprintf(dst->label, sizeof(dst->label), "%s", src->label);
        dst->cost = src->cost;
        dst->icon_frame = src->icon_frame;
        dst->product_class = src->product_class;
        dst->product_type = src->product_type;
        dst->faction = src->faction;
        dst->prerequisite_count = src->prerequisite_count;
        for (int j = 0; j < src->prerequisite_count && j < RTS_MODEL_MAX_PRODUCT_PREREQUISITES; ++j) {
            dst->prerequisites[j] = src->prerequisites[j];
        }
        dst->available = G_ModelProductAvailable(model, 0, src);
    }
    return count;
}

