#define _DEFAULT_SOURCE
#include "game_model.h"

#include "engine.h"
#include "plugin.h"

#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    int row_id;
    int ui_id;
    const char *label;
    int cost;
    int icon_frame;
    RtsProductClass product_class;
    int product_type;
    int faction;
    int prerequisites[RTS_MODEL_MAX_PRODUCT_PREREQUISITES];
    int prerequisite_count;
} StaticProductDefinition;

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

static const ActorType *plugin_actor_type_by_id(const Plugin *plugin, uint16_t type_id);
static void apply_actor_type_defaults(Unit *unit, const ActorType *type);

static const StaticProductDefinition DARK_COLONY_HUMAN_PRODUCTS[] = {
    {  0, 206, "Exo-Ctr",   2000, 129, RTS_PRODUCT_BUILDING, 16, 0, { 0 }, 0 },
    {  1,  80, "Barracks",  1000,  20, RTS_PRODUCT_BUILDING, 17, 0, { 0 }, 1 },
    {  2,  81, "Sci-Pod",   2000,  21, RTS_PRODUCT_BUILDING, 20, 0, { 0 }, 1 },
    {  3,  82, "Robo-Ftr",  2000,  22, RTS_PRODUCT_BUILDING, 18, 0, { 2, 1 }, 2 },
    {  6,  83, "Rsch-Bay",  3000,  23, RTS_PRODUCT_BUILDING, 22, 0, { 4 }, 1 },
    {  4,  85, "Sci-Pod+",  2000,  26, RTS_PRODUCT_BUILDING, 21, 0, { 2 }, 1 },
    {  5,  86, "Robo-Ftr+", 2000,  30, RTS_PRODUCT_BUILDING, 19, 0, { 3, 2 }, 2 },
    {  7,  87, "Exploiter", 1500,   8, RTS_PRODUCT_UNIT,      6, 0, { 0 }, 1 },
    {  9,  89, "Trooper",    350,   6, RTS_PRODUCT_UNIT,      0, 0, { 1 }, 1 },
    { 29,  90, "Sentinel",   450,   5, RTS_PRODUCT_UNIT,     43, 0, { 1, 2 }, 2 },
    { 10,  92, "Osprey IV",  600,   9, RTS_PRODUCT_UNIT,      5, 0, { 0, 3, 4 }, 3 },
    { 11,  91, "Reaper",     600,  11, RTS_PRODUCT_UNIT,      2, 0, { 3, 2 }, 2 },
    {  8,  88, "Firestorm",  900,  10, RTS_PRODUCT_UNIT,      1, 0, { 5 }, 1 },
    { 12,  93, "Barrager",  1000,   7, RTS_PRODUCT_UNIT,      3, 0, { 5, 4 }, 2 },
    { 13,  94, "S.A.R.G.E", 1500,  12, RTS_PRODUCT_UNIT,      4, 0, { 1, 6 }, 2 },
    { 83, 135, "Medi-craft", 900,  29, RTS_PRODUCT_UNIT,     49, 0, { 4, 3, 6 }, 3 },
};

static int dark_colony_product_count(void) {
    return (int)(sizeof(DARK_COLONY_HUMAN_PRODUCTS) /
                 sizeof(DARK_COLONY_HUMAN_PRODUCTS[0]));
}

static void model_set_error(RtsGameModel *model, const char *fmt, ...) {
    if (!model) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(model->error, sizeof(model->error), fmt, args);
    va_end(args);
}

static uint16_t dark_colony_actor_id_for_product_type(int product_type) {
    switch (product_type) {
    case 16: return 1000;
    case 17: return 1001;
    case 18: return 1002;
    case 19: return 1003;
    case 20: return 1004;
    case 21: return 1005;
    case 22: return 1006;
    default: return 0;
    }
}

static uint16_t dark_colony_unit_actor_id_for_product_type(int product_type) {
    switch (product_type) {
    case 0: return 1;
    case 2: return 4;
    case 3: return 5;
    case 4: return 6;
    case 5: return 7;
    case 6: return 3;
    default: return 0;
    }
}

static const StaticProductDefinition *dark_colony_product_by_row_id(int row_id) {
    int count = dark_colony_product_count();
    for (int i = 0; i < count; ++i) {
        if (DARK_COLONY_HUMAN_PRODUCTS[i].row_id == row_id)
            return &DARK_COLONY_HUMAN_PRODUCTS[i];
    }
    return NULL;
}

static const StaticProductDefinition *dark_colony_product_by_ui_id(int ui_id) {
    int count = dark_colony_product_count();
    for (int i = 0; i < count; ++i) {
        if (DARK_COLONY_HUMAN_PRODUCTS[i].ui_id == ui_id)
            return &DARK_COLONY_HUMAN_PRODUCTS[i];
    }
    return NULL;
}

static uint16_t dark_colony_actor_id_for_product(const StaticProductDefinition *product) {
    if (!product) return 0;
    if (product->product_class == RTS_PRODUCT_BUILDING)
        return dark_colony_actor_id_for_product_type(product->product_type);
    if (product->product_class == RTS_PRODUCT_UNIT)
        return dark_colony_unit_actor_id_for_product_type(product->product_type);
    return 0;
}

static bool model_has_player_actor_type(const RtsGameModel *model, uint16_t actor_id) {
    if (!model || actor_id == 0) return false;
    for (int i = 0; i < model->unit_count; ++i) {
        const Unit *unit = &model->units[i];
        if (unit->owner == 0 && !unit->remove && unit->hp > 0 && unit->type_id == actor_id)
            return true;
    }
    return false;
}

static bool model_has_player_product(const RtsGameModel *model,
                                     const StaticProductDefinition *product) {
    if (!product || product->product_class != RTS_PRODUCT_BUILDING) return false;
    return model_has_player_actor_type(model, dark_colony_actor_id_for_product(product));
}

static bool product_is_available(const RtsGameModel *model, const StaticProductDefinition *product) {
    if (!product) return false;
    for (int i = 0; i < product->prerequisite_count; ++i) {
        const StaticProductDefinition *prereq =
            dark_colony_product_by_row_id(product->prerequisites[i]);
        if (!model_has_player_product(model, prereq)) return false;
    }
    return true;
}

static int find_player_actor_index(const RtsGameModel *model, uint16_t actor_id) {
    if (!model || actor_id == 0) return -1;
    for (int i = 0; i < model->unit_count; ++i) {
        const Unit *unit = &model->units[i];
        if (unit->owner == 0 && !unit->remove && unit->hp > 0 && unit->type_id == actor_id)
            return i;
    }
    return -1;
}

static int find_player_product_index(const RtsGameModel *model,
                                     const StaticProductDefinition *product) {
    return find_player_actor_index(model, dark_colony_actor_id_for_product(product));
}

static int find_product_producer_index(const RtsGameModel *model,
                                       const StaticProductDefinition *product) {
    if (!model || !product) return -1;
    for (int i = 0; i < product->prerequisite_count; ++i) {
        const StaticProductDefinition *prereq =
            dark_colony_product_by_row_id(product->prerequisites[i]);
        int index = find_player_product_index(model, prereq);
        if (index >= 0) return index;
    }
    for (int i = 0; i < model->unit_count; ++i) {
        const Unit *unit = &model->units[i];
        if (unit->owner == 0 && !unit->remove && unit->hp > 0 &&
            (unit->traits & RTS_TRAIT_RENDERABLE) != 0 &&
            (unit->traits & RTS_TRAIT_MOBILE) == 0) {
            return i;
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
            if (!map_walkable(&model->map, x, y)) return false;
        }
    }

    for (int i = 0; i < model->unit_count; ++i) {
        const Unit *other = &model->units[i];
        if (other->remove || other->hp <= 0) continue;
        float other_radius = other->radius > 0.05f ? other->radius : 0.42f;
        float min_dist = radius + other_radius;
        float dx = other->gx - gx;
        float dy = other->gy - gy;
        if (dx * dx + dy * dy < min_dist * min_dist) return false;
    }
    return true;
}

static bool find_spawn_position_near(const RtsGameModel *model, const Unit *producer,
                                     float radius, float *out_gx, float *out_gy) {
    if (!model || !producer || !out_gx || !out_gy) return false;
    int origin_x = (int)floorf(producer->gx);
    int origin_y = (int)floorf(producer->gy);
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

static bool create_dark_colony_product(RtsGameModel *model,
                                       const StaticProductDefinition *product) {
    if (!model || !product) return false;
    if (!model->plugin || !model->plugin->id ||
        strcmp(model->plugin->id, "dark-colony") != 0) {
        return false;
    }
    if (!product_is_available(model, product)) return false;
    if (model->map.player_resources[0] < product->cost) return false;
    if (model->unit_count >= MAX_UNITS) return false;

    uint16_t actor_id = dark_colony_actor_id_for_product(product);
    const ActorType *actor_type = plugin_actor_type_by_id(model->plugin, actor_id);
    if (actor_id == 0 || !actor_type) return false;

    int producer_index = find_product_producer_index(model, product);
    if (producer_index < 0) return false;

    Unit new_unit;
    memset(&new_unit, 0, sizeof(new_unit));
    new_unit.type_id = actor_id;
    new_unit.owner = 0;
    new_unit.sprite_id = -1;
    new_unit.attack_target = -1;
    new_unit.harvest_target = -1;
    new_unit.frame = product->product_class == RTS_PRODUCT_BUILDING ?
        product->product_type - 16 : 0;
    apply_actor_type_defaults(&new_unit, actor_type);
    apply_mobjinfo_defaults(model->plugin ? model->plugin->game_info : NULL, &new_unit);
    float radius = new_unit.radius > 0.05f ? new_unit.radius : 0.42f;

    float gx = 0.0f;
    float gy = 0.0f;
    if (!find_spawn_position_near(model, &model->units[producer_index], radius, &gx, &gy))
        return false;

    new_unit.gx = gx;
    new_unit.gy = gy;
    model->map.player_resources[0] -= product->cost;
    model->units[model->unit_count++] = new_unit;
    return true;
}

static void append_ui_script(char *dst, size_t dst_size, const char *fmt, ...) {
    if (!dst || dst_size == 0) return;
    size_t len = strlen(dst);
    if (len >= dst_size - 1) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(dst + len, dst_size - len, fmt, args);
    va_end(args);
}

static void build_dark_colony_ui_script(const RtsGameModel *model, char *dst, size_t dst_size) {
    if (!model || !dst || dst_size == 0) return;
    dst[0] = '\0';
    append_ui_script(dst, dst_size, "ui dark-colony 1\n");
    append_ui_script(dst, dst_size, "x 520 y 464 text \"P-7 %d\"\n",
                     model->map.player_resources[0]);

    int source_count = dark_colony_product_count();
    for (int i = 0; i < source_count; ++i) {
        const StaticProductDefinition *product = &DARK_COLONY_HUMAN_PRODUCTS[i];
        int col = i % 3;
        int row = i / 3;
        int button_x = 516 + col * 36;
        int button_y = 92 + row * 42;
        int label_x = button_x + 8;
        int label_y = button_y + 34;
        bool available = product_is_available(model, product);
        append_ui_script(dst, dst_size,
                         "x %d y %d btn %d enabled %d pic %d\n",
                         button_x, button_y, product->ui_id, available ? 1 : 0,
                         product->icon_frame);
        append_ui_script(dst, dst_size,
                         "x %d y %d text \"%s %d\"\n",
                         label_x, label_y, product->label, product->cost);
    }
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
    if (unit->render_intensity == 0) unit->render_intensity = 16;
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
    case RTS_GAME_COMMAND_HARVEST_SELECTED:
        return issue_harvest_order_at(&model->map, model->units, model->unit_count,
                                      command->data.harvest_selected.gx,
                                      command->data.harvest_selected.gy);
    case RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON:
        return create_dark_colony_product(
            model, dark_colony_product_by_ui_id(command->data.activate_ui_button.ui_id));
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
    if (out->decoration_count > RTS_MODEL_MAX_SNAPSHOT_DECORATIONS)
        out->decoration_count = RTS_MODEL_MAX_SNAPSHOT_DECORATIONS;
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
        dst->state_id = src->state_id;
        dst->render_flags = src->render_flags;
        dst->render_remap = src->render_remap;
        dst->render_intensity = src->render_intensity;
        dst->selected = src->selected;
        dst->has_move_order = src->move_order_id != 0;
        dst->harvest_target = src->harvest_target;
        snprintf(dst->sprite_name, sizeof(dst->sprite_name), "%s", src->sprite_name);
        snprintf(dst->shadow_name, sizeof(dst->shadow_name), "%s", src->shadow_name);
    }
    for (int i = 0; i < out->decoration_count; ++i) {
        const MapDecoration *src = &model->map.decorations[i];
        RtsRenderDecoration *dst = &out->decorations[i];
        dst->gx = src->gx;
        dst->gy = src->gy;
        dst->footprint_w = src->footprint_w;
        dst->footprint_h = src->footprint_h;
        dst->center_anchor = src->center_anchor;
        dst->frame_index = src->frame_index;
        dst->frame2_index = src->frame2_index;
        dst->facing_code = src->facing_code;
        dst->render_flags = src->render_flags;
        dst->render2_flags = src->render2_flags;
        snprintf(dst->sprite_name, sizeof(dst->sprite_name), "%s", src->sprite_name);
        snprintf(dst->sprite2_name, sizeof(dst->sprite2_name), "%s", src->sprite2_name);
        snprintf(dst->shadow_name, sizeof(dst->shadow_name), "%s", src->shadow_name);
        snprintf(dst->sequence_name, sizeof(dst->sequence_name), "%s", src->sequence_name);
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
        dst->render_remap = src->render_remap;
        dst->render_intensity = src->render_intensity;
        snprintf(dst->sprite_name, sizeof(dst->sprite_name), "%s", src->sprite_name);
        snprintf(dst->sequence_name, sizeof(dst->sequence_name), "%s", src->sequence_name);
    }
    if (model->plugin && model->plugin->id && strcmp(model->plugin->id, "dark-colony") == 0) {
        build_dark_colony_ui_script(model, out->ui_script, sizeof(out->ui_script));
    }
    return true;
}

const char *rts_game_model_last_error(const RtsGameModel *model) {
    return model && model->error[0] ? model->error : "";
}

int rts_game_model_products(const RtsGameModel *model, RtsProductDefinition *out, int max_products) {
    if (!model || !out || max_products <= 0) return 0;
    if (!model->plugin || !model->plugin->id || strcmp(model->plugin->id, "dark-colony") != 0)
        return 0;

    int source_count = dark_colony_product_count();
    int count = source_count < max_products ? source_count : max_products;
    for (int i = 0; i < count; ++i) {
        const StaticProductDefinition *src = &DARK_COLONY_HUMAN_PRODUCTS[i];
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
        dst->available = product_is_available(model, src);
    }
    return count;
}
