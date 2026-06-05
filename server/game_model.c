#define _DEFAULT_SOURCE
#include "game_model.h"

#include "engine.h"
#include "plugin.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct RtsGameModel {
    const Plugin *plugin;
    GameMap map;
    Unit units[MAX_UNITS];
    int unit_count;
    VisualEffect effects[MAX_VISUAL_EFFECTS];
    HudText hud;
    void *mission;
    bool loaded;
    char error[256];
};

static void model_set_error(RtsGameModel *model, const char *fmt, ...) {
    if (!model) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(model->error, sizeof(model->error), fmt, args);
    va_end(args);
}

static void load_plugin_by_id(const char *game_id) {
    char lib_path[1024];
    const char *extensions[] = { ".dylib", ".so", NULL };
    for (int i = 0; extensions[i]; ++i) {
        snprintf(lib_path, sizeof(lib_path), "build/libs/%s%s", game_id, extensions[i]);
        if (plugin_load(lib_path)) return;
    }
}

static void destroy_model_map(GameMap *map) {
    if (!map) return;
    free(map->tile_ids);
    for (int i = 0; i < MAX_TILE_OVERLAYS; ++i) free(map->tile_overlays[i]);
    for (int i = 0; i < MAX_TILE_OVERLAYS + 1; ++i) free(map->tile_flip_flags[i]);
    free(map->blocked);
    free(map->cell_colors);
    free(map->decorations);
    free(map->resource_vents);
    free(map->extras);
    memset(map, 0, sizeof(*map));
}

static const ActorType *plugin_actor_type_by_id(const Plugin *plugin, uint16_t type_id) {
    if (!plugin || !plugin->actor_types) return NULL;
    for (int i = 0; i < plugin->actor_type_count; ++i) {
        if (plugin->actor_types[i].id == type_id) return &plugin->actor_types[i];
    }
    return NULL;
}

static const ActorType *plugin_actor_type_for_unit(const Plugin *plugin, const Unit *unit) {
    const ActorType *type = plugin_actor_type_by_id(plugin, unit ? unit->type_id : 0);
    if (type) return type;
    if (!plugin || !plugin->actor_types || !unit) return NULL;
    for (int i = 0; i < plugin->actor_type_count; ++i) {
        const char *sprite = plugin->actor_types[i].sprite_name;
        if (sprite && sprite[0] != '\0' && strcasecmp(sprite, unit->sprite_name) == 0) {
            return &plugin->actor_types[i];
        }
    }
    return plugin->actor_type_count > 0 ? &plugin->actor_types[0] : NULL;
}

static void apply_actor_type_defaults(Unit *unit, const ActorType *type) {
    if (!unit || !type) return;
    unit->type_id = type->id;
    unit->traits = type->traits;
    if (unit->speed <= 0.0f) unit->speed = type->speed;
    if (unit->max_hp <= 0) unit->max_hp = type->max_hp;
    if (unit->hp <= 0) unit->hp = unit->max_hp;
    if (unit->attack_range <= 0.0f) unit->attack_range = type->attack_range;
    if (unit->attack_damage <= 0) unit->attack_damage = type->attack_damage;
    if (unit->attack_cooldown_ms <= 0) unit->attack_cooldown_ms = type->attack_cooldown_ms;
    if (unit->attack_anim_ms <= 0) unit->attack_anim_ms = type->attack_anim_ms;
    if (unit->death_anim_ms <= 0) unit->death_anim_ms = type->death_anim_ms;
    if (unit->harvest_state_id <= 0) unit->harvest_state_id = type->harvest_state_id;
    if (unit->muzzle_flash_ms <= 0) unit->muzzle_flash_ms = type->muzzle_flash_ms;
    if (unit->attack_target <= 0) unit->attack_target = -1;
    if (unit->harvest_target == 0) unit->harvest_target = -1;
    if (unit->sprite_name[0] == '\0' && type->sprite_name) {
        snprintf(unit->sprite_name, sizeof(unit->sprite_name), "%s", type->sprite_name);
    }
    if (unit->shadow_name[0] == '\0' && type->shadow_name) {
        snprintf(unit->shadow_name, sizeof(unit->shadow_name), "%s", type->shadow_name);
    }
    if (unit->muzzle_flash_name[0] == '\0' && type->muzzle_flash_name) {
        snprintf(unit->muzzle_flash_name, sizeof(unit->muzzle_flash_name), "%s", type->muzzle_flash_name);
    }
}

static void apply_plugin_actor_defaults(const Plugin *plugin, Unit *units, int unit_count) {
    for (int i = 0; i < unit_count; ++i) {
        apply_actor_type_defaults(&units[i], plugin_actor_type_for_unit(plugin, &units[i]));
        apply_mobjinfo_defaults(plugin ? plugin->game_info : NULL, &units[i]);
    }
}

static bool build_map_path(char *out, size_t out_size, const char *data_root, const char *map_path) {
    if (!out || out_size == 0 || !data_root || !map_path) return false;
    if (map_path[0] == '/') {
        snprintf(out, out_size, "%s", map_path);
    } else {
        path_join(out, out_size, data_root, map_path);
    }
    return true;
}

RtsGameModel *rts_game_model_create(void) {
    return calloc(1, sizeof(RtsGameModel));
}

void rts_game_model_destroy(RtsGameModel *model) {
    if (!model) return;
    if (model->mission && model->plugin && model->plugin->destroy_mission) {
        model->plugin->destroy_mission(model->mission);
    }
    destroy_model_map(&model->map);
    free(model);
}

bool rts_game_model_load(RtsGameModel *model, const RtsGameModelConfig *config) {
    if (!model || !config) return false;
    const char *game_id = config->game_id && config->game_id[0] ? config->game_id : "dark-colony";
    if (!find_plugin(game_id)) load_plugin_by_id(game_id);
    const Plugin *plugin = find_plugin(game_id);
    if (!plugin) {
        model_set_error(model, "unknown game '%s'", game_id);
        return false;
    }
    const char *data_root = config->data_root && config->data_root[0] ?
        config->data_root : plugin->default_root;
    const char *map_rel_or_abs = config->map_path && config->map_path[0] ?
        config->map_path : plugin->default_map;

    char map_path[1024];
    if (!build_map_path(map_path, sizeof(map_path), data_root, map_rel_or_abs)) {
        model_set_error(model, "invalid data root or map path");
        return false;
    }

    if (model->loaded) {
        if (model->mission && model->plugin && model->plugin->destroy_mission) {
            model->plugin->destroy_mission(model->mission);
        }
        model->mission = NULL;
        destroy_model_map(&model->map);
        memset(model->units, 0, sizeof(model->units));
        memset(model->effects, 0, sizeof(model->effects));
        memset(&model->hud, 0, sizeof(model->hud));
        model->unit_count = 0;
        model->loaded = false;
    }

    if (!plugin->load_map || !plugin->load_map(map_path, &model->map)) {
        model_set_error(model, "failed to load map '%s'", map_path);
        return false;
    }
    model->unit_count = plugin->load_initial_units ?
        plugin->load_initial_units(map_path, model->units, MAX_UNITS) : 0;
    apply_plugin_actor_defaults(plugin, model->units, model->unit_count);
    model->mission = plugin->load_mission ? plugin->load_mission(map_path) : NULL;
    model->plugin = plugin;
    model->loaded = true;
    model->error[0] = '\0';
    return true;
}

bool rts_game_model_tick(RtsGameModel *model, float dt) {
    if (!model || !model->loaded) return false;
    if (dt <= 0.0f) return true;
    update_units(&model->map, model->units, &model->unit_count, model->effects,
                 MAX_VISUAL_EFFECTS, model->plugin ? model->plugin->game_info : NULL, dt);
    if (model->mission && model->plugin && model->plugin->update_mission) {
        model->plugin->update_mission(model->mission, &model->map, model->units,
                                      &model->unit_count, model->effects,
                                      MAX_VISUAL_EFFECTS, model->plugin->game_info,
                                      &model->hud, dt);
    }
    update_visual_effects(&model->map, model->effects, MAX_VISUAL_EFFECTS,
                          model->plugin ? model->plugin->game_info : NULL, dt);
    hud_text_update(&model->hud, dt);
    return true;
}

bool rts_game_model_command(RtsGameModel *model, const RtsGameCommand *command) {
    if (!model || !model->loaded || !command) return false;
    switch (command->kind) {
    case RTS_GAME_COMMAND_NONE:
        return true;
    case RTS_GAME_COMMAND_SELECT_ALL_PLAYER_UNITS:
        for (int i = 0; i < model->unit_count; ++i) {
            Unit *unit = &model->units[i];
            unit->selected = unit->owner == 0 && unit->hp > 0 &&
                (unit->traits & RTS_TRAIT_SELECTABLE) != 0;
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
        Cell goal = {
            (int)command->data.move_selected.gx,
            (int)command->data.move_selected.gy,
        };
        issue_move_order(&model->map, model->units, model->unit_count, goal);
        return true;
    }
    default:
        return false;
    }
}

bool rts_game_model_snapshot(const RtsGameModel *model, RtsRenderSnapshot *out) {
    if (!model || !model->loaded || !out) return false;
    memset(out, 0, sizeof(*out));
    out->map_width = model->map.width;
    out->map_height = model->map.height;
    out->decoration_count = model->map.decoration_count;
    out->resource_vent_count = model->map.resource_vent_count;
    for (int i = 0; i < RTS_MODEL_MAX_PLAYERS; ++i) {
        out->player_resources[i] = model->map.player_resources[i];
    }
    out->unit_count = model->unit_count;
    if (out->unit_count > RTS_MODEL_MAX_SNAPSHOT_UNITS)
        out->unit_count = RTS_MODEL_MAX_SNAPSHOT_UNITS;
    for (int i = 0; i < out->unit_count; ++i) {
        const Unit *src = &model->units[i];
        RtsRenderUnit *dst = &out->units[i];
        dst->gx = src->gx;
        dst->gy = src->gy;
        dst->move_goal_gx = src->move_goal_gx;
        dst->move_goal_gy = src->move_goal_gy;
        dst->type_id = src->type_id;
        dst->owner = src->owner;
        dst->traits = src->traits;
        dst->hp = src->hp;
        dst->max_hp = src->max_hp;
        dst->frame = src->frame;
        dst->render_flags = src->render_flags;
        dst->selected = src->selected;
        dst->has_move_order = src->move_order_id != 0;
        snprintf(dst->sprite_name, sizeof(dst->sprite_name), "%s", src->sprite_name);
        snprintf(dst->shadow_name, sizeof(dst->shadow_name), "%s", src->shadow_name);
    }
    for (int i = 0; i < MAX_VISUAL_EFFECTS && out->effect_count < RTS_MODEL_MAX_SNAPSHOT_EFFECTS; ++i) {
        const VisualEffect *src = &model->effects[i];
        if (!src->active) continue;
        RtsRenderEffect *dst = &out->effects[out->effect_count++];
        dst->active = src->active;
        dst->gx = src->gx;
        dst->gy = src->gy;
        dst->frame = src->frame;
        dst->render_flags = src->render_flags;
        snprintf(dst->sprite_name, sizeof(dst->sprite_name), "%s", src->sprite_name);
        snprintf(dst->sequence_name, sizeof(dst->sequence_name), "%s", src->sequence_name);
    }
    return true;
}

const char *rts_game_model_last_error(const RtsGameModel *model) {
    return model && model->error[0] ? model->error : "";
}
