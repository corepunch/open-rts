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
    int makers[RTS_MODEL_MAX_PRODUCT_PREREQUISITES];
    int maker_count;
} StaticProductDefinition;

static bool dark_colony_product_uses_barracks_release(const Unit *producer,
                                                      const StaticProductDefinition *product,
                                                      uint16_t actor_id);

enum {
    DC_ACTOR_TROOPER = 1,
    DC_ACTOR_EXCOPOD = 1000,
    DC_ACTOR_BRRKPOD = 1001,
    DC_ACTOR_ROBOPOD = 1002,
    DC_ACTOR_ROBOPOD2 = 1003,
    DC_ACTOR_SCNCPOD2 = 1005,
    DC_PRODUCTION_BUILD_GROUP = 6,
    DC_TRSCBUILD_FIRST_FRAME = 12,
};

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
    /* Buildings — all built from the Exco Center */
    {  0, 206, "Exo-Ctr",   2000, 129, RTS_PRODUCT_BUILDING, 16, 0, { 0 }, 0, { DC_ACTOR_EXCOPOD }, 1 },
    {  1,  80, "Barracks",  1000,  20, RTS_PRODUCT_BUILDING, 17, 0, { 0 }, 1, { DC_ACTOR_EXCOPOD }, 1 },
    {  2,  81, "Sci-Pod",   2000,  21, RTS_PRODUCT_BUILDING, 20, 0, { 0 }, 1, { DC_ACTOR_EXCOPOD }, 1 },
    {  3,  82, "Robo-Ftr",  2000,  22, RTS_PRODUCT_BUILDING, 18, 0, { 2, 1 }, 2, { DC_ACTOR_EXCOPOD }, 1 },
    {  6,  83, "Rsch-Bay",  3000,  23, RTS_PRODUCT_BUILDING, 22, 0, { 4 }, 1, { DC_ACTOR_EXCOPOD }, 1 },
    {  4,  85, "Sci-Pod+",  2000,  26, RTS_PRODUCT_BUILDING, 21, 0, { 2 }, 1, { DC_ACTOR_EXCOPOD }, 1 },
    {  5,  86, "Robo-Ftr+", 2000,  30, RTS_PRODUCT_BUILDING, 19, 0, { 3, 2 }, 2, { DC_ACTOR_EXCOPOD }, 1 },
    /* Exco Center units */
    {  7,  87, "Exploiter", 1500,   8, RTS_PRODUCT_UNIT,      6, 0, { 0 }, 1, { DC_ACTOR_EXCOPOD }, 1 },
    /* Barracks units */
    {  9,  89, "Trooper",    350,   6, RTS_PRODUCT_UNIT,      0, 0, { 1 }, 1, { DC_ACTOR_BRRKPOD }, 1 },
    { 29,  90, "Sentinel",   450,   5, RTS_PRODUCT_UNIT,     43, 0, { 1, 2 }, 2, { DC_ACTOR_BRRKPOD }, 1 },
    { 13,  94, "S.A.R.G.E", 1500,  12, RTS_PRODUCT_UNIT,      4, 0, { 1, 6 }, 2, { DC_ACTOR_BRRKPOD }, 1 },
    /* Robot Factory units */
    { 11,  91, "Reaper",     600,  11, RTS_PRODUCT_UNIT,      2, 0, { 3, 2 }, 2, { DC_ACTOR_ROBOPOD }, 1 },
    { 12,  93, "Barrager",  1000,   7, RTS_PRODUCT_UNIT,      3, 0, { 3, 4 }, 2, { DC_ACTOR_ROBOPOD }, 1 },
    { 10,  92, "Osprey IV",  600,   9, RTS_PRODUCT_UNIT,      5, 0, { 3, 4 }, 2, { DC_ACTOR_ROBOPOD }, 1 },
    /* Upgraded Robot Factory units */
    {  8,  88, "Firestorm",  900,  10, RTS_PRODUCT_UNIT,      1, 0, { 5 }, 1, { DC_ACTOR_ROBOPOD2 }, 1 },
    { 83, 135, "Medi-craft", 900,  29, RTS_PRODUCT_UNIT,     49, 0, { 5, 6 }, 2, { DC_ACTOR_ROBOPOD2 }, 1 },
};

static int dark_colony_product_count(void) {
    return (int)(sizeof(DARK_COLONY_HUMAN_PRODUCTS) /
                 sizeof(DARK_COLONY_HUMAN_PRODUCTS[0]));
}

static const StaticProductDefinition DARK_REIGN_FG_PRODUCTS[] = {
    {
        10001, 10001, "FG HQ 1", 750, 0, RTS_PRODUCT_BUILDING, 10001, 0,
        { 0 }, 0, { 11 }, 1,
    },
    {
        11, 11, "Construction Rig", 300, 0, RTS_PRODUCT_UNIT, 11, 0,
        { 10001 }, 1, { 10001, 10002, 10003 }, 3,
    },
};

static int dark_reign_product_count(void) {
    return (int)(sizeof(DARK_REIGN_FG_PRODUCTS) /
                 sizeof(DARK_REIGN_FG_PRODUCTS[0]));
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

static int dark_colony_building_frame_for_product_type(int product_type) {
    switch (product_type) {
    case 16: return 0; /* HUBU.FIN EXCOPODSTAND0 */
    case 17: return 4; /* HUBU.FIN BRRKPODSTAND0 */
    case 18: return 1;
    case 19: return 1;
    case 20: return 2;
    case 21: return 2;
    case 22: return 4;
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

static int dark_colony_building_state_for_product_type(const GameInfo *game_info,
                                                       int product_type) {
    if (!game_info || !game_info->states || !game_info->sprnames) return -1;
    const char *sprite_name = NULL;
    switch (product_type) {
    case 16:
    case 17:
        sprite_name = "SPRITES/HUBU.SPR";
        break;
    default:
        return -1;
    }
    int frame = dark_colony_building_frame_for_product_type(product_type);
    for (int i = 0; i < game_info->state_count; ++i) {
        const State *state = &game_info->states[i];
        if (state->sprite < 0 || state->sprite >= game_info->sprite_count) continue;
        if (state->facings != 1 || state->facing_frames[0] != frame) continue;
        if (strcmp(game_info->sprnames[state->sprite], sprite_name) == 0)
            return i;
    }
    return -1;
}

static const State *model_state_at(const GameInfo *game_info, int state_id) {
    if (!game_info || !game_info->states || state_id < 0 || state_id >= game_info->state_count)
        return NULL;
    return &game_info->states[state_id];
}

static int model_find_state_by_group_frame(const GameInfo *game_info, int group, int frame) {
    if (!game_info || !game_info->states) return -1;
    for (int i = 0; i < game_info->state_count; ++i) {
        const State *state = &game_info->states[i];
        if (state->misc1 != group || state->facings != 1) continue;
        if (state->facing_frames[0] == frame) return i;
    }
    return -1;
}

static int model_state_chain_duration_ms(const GameInfo *game_info, int state_id, int group) {
    int tics = 0;
    int guard = 0;
    while (guard++ < (game_info ? game_info->state_count + 1 : 1)) {
        const State *state = model_state_at(game_info, state_id);
        if (!state || state->misc1 != group) break;
        if (state->tics > 0) tics += state->tics;
        int next = state->nextstate;
        if (next == game_info->null_state || next == state_id) break;
        state_id = next;
    }
    if (tics <= 0) return 0;
    return (int)(tics * FIXED_DT * 1000.0f + 0.5f);
}

static int product_training_time_ms(const StaticProductDefinition *product) {
    if (!product) return 0;
    if (product->product_class != RTS_PRODUCT_UNIT) return 0;
    int ms = product->cost * 10;
    if (ms < 1000) ms = 1000;
    return ms;
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

static const StaticProductDefinition *dark_reign_product_by_ui_id(int ui_id) {
    int count = dark_reign_product_count();
    for (int i = 0; i < count; ++i) {
        if (DARK_REIGN_FG_PRODUCTS[i].ui_id == ui_id)
            return &DARK_REIGN_FG_PRODUCTS[i];
    }
    return NULL;
}

static const StaticProductDefinition *product_by_ui_id_for_model(const RtsGameModel *model,
                                                                 int ui_id) {
    if (!model || !model->plugin || !model->plugin->id) return NULL;
    if (strcmp(model->plugin->id, "dark-colony") == 0)
        return dark_colony_product_by_ui_id(ui_id);
    if (strcmp(model->plugin->id, "dark-reign") == 0)
        return dark_reign_product_by_ui_id(ui_id);
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

static uint16_t dark_reign_actor_id_for_requirement(int requirement_id) {
    switch (requirement_id) {
    case 11: return 11;
    case 10001: return 10001;
    default: return 0;
    }
}

static uint16_t dark_reign_actor_id_for_product(const StaticProductDefinition *product) {
    return product ? dark_reign_actor_id_for_requirement(product->product_type) : 0;
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

static bool model_has_dark_reign_requirement(const RtsGameModel *model, int requirement_id) {
    return model_has_player_actor_type(model, dark_reign_actor_id_for_requirement(requirement_id));
}

static bool product_is_available(const RtsGameModel *model, const StaticProductDefinition *product) {
    if (!product) return false;
    bool dark_reign = model && model->plugin && model->plugin->id &&
        strcmp(model->plugin->id, "dark-reign") == 0;
    if (dark_reign) {
        for (int i = 0; i < product->prerequisite_count; ++i) {
            if (!model_has_dark_reign_requirement(model, product->prerequisites[i]))
                return false;
        }
        if (product->maker_count <= 0) return true;
        for (int i = 0; i < product->maker_count; ++i) {
            if (model_has_dark_reign_requirement(model, product->makers[i]))
                return true;
        }
        return false;
    }
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

static int find_product_producer_index(const RtsGameModel *model,
                                       const StaticProductDefinition *product) {
    if (!model || !product) return -1;
    if (model->plugin && model->plugin->id &&
        strcmp(model->plugin->id, "dark-reign") == 0) {
        for (int i = 0; i < product->maker_count; ++i) {
            int index = find_player_actor_index(
                model, dark_reign_actor_id_for_requirement(product->makers[i]));
            if (index >= 0) return index;
        }
        return -1;
    }
    /* dark-colony: makers[] holds the actor type ID of the building that builds this */
    for (int i = 0; i < product->maker_count; ++i) {
        int index = find_player_actor_index(model, (uint16_t)product->makers[i]);
        if (index >= 0) return index;
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
            if (!map_walkable(&model->map, x, y)) return false;
        }
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

static bool state_offset_for_facing(const State *state, bool overlay, int facing_code,
                                    int *out_x, int *out_y) {
    if (!state || !out_x || !out_y) return false;
    int facings = overlay ? state->overlay_facings : state->facings;
    if (facings <= 0) return false;
    int best = 0;
    int best_delta = 0x7fffffff;
    for (int i = 0; i < facings && i < RTS_MAX_STATE_FACINGS; ++i) {
        int code = overlay ? state->overlay_direction_codes[i] : state->direction_codes[i];
        int delta = abs(code - facing_code);
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
        if (delta == 0) break;
    }
    *out_x = overlay ? state->overlay_offset_x[best] : state->offset_x[best];
    *out_y = overlay ? state->overlay_offset_y[best] : state->offset_y[best];
    return true;
}

static bool dark_colony_barracks_release_spawn_point(const RtsGameModel *model,
                                                     const Unit *producer,
                                                     const Unit *new_unit,
                                                     float *out_gx,
                                                     float *out_gy) {
    if (!model || !producer || !new_unit || !out_gx || !out_gy ||
        !model->plugin || !model->plugin->game_info) {
        return false;
    }
    const GameInfo *game_info = model->plugin->game_info;
    const State *stand = model_state_at(game_info, new_unit->state_id);
    if (!stand) return false;

    int stand_x = 0;
    int stand_y = 0;
    if (!state_offset_for_facing(stand, false, new_unit->facing_code, &stand_x, &stand_y))
        return false;

    int release_state_id = model_find_state_by_group_frame(game_info,
                                                           DC_PRODUCTION_BUILD_GROUP,
                                                           DC_TRSCBUILD_FIRST_FRAME);
    int release_x = 0;
    int release_y = 0;
    bool saw_release_trooper = false;
    int guard = 0;
    while (guard++ < game_info->state_count + 1) {
        const State *state = model_state_at(game_info, release_state_id);
        if (!state || state->misc1 != DC_PRODUCTION_BUILD_GROUP) break;
        int x = 0;
        int y = 0;
        if (state->sprite == stand->sprite &&
            state_offset_for_facing(state, false, new_unit->facing_code, &x, &y)) {
            release_x = x;
            release_y = y;
            saw_release_trooper = true;
        }
        if (state->overlay_sprite == stand->sprite &&
            state_offset_for_facing(state, true, new_unit->facing_code, &x, &y)) {
            release_x = x;
            release_y = y;
            saw_release_trooper = true;
        }
        int next = state->nextstate;
        if (next == game_info->null_state || next == release_state_id) break;
        release_state_id = next;
    }
    if (!saw_release_trooper) return false;

    *out_gx = producer->gx + (float)(release_x - stand_x) / (float)CELL_W;
    *out_gy = producer->gy - (float)(release_y - stand_y) / (float)CELL_H;
    return true;
}

static void order_barracks_exit_spacing(RtsGameModel *model, int spawned_index,
                                        const Unit *producer, float exit_gx,
                                        float exit_gy) {
    if (!model || !producer || spawned_index < 0 || spawned_index >= model->unit_count)
        return;
    bool saved[MAX_UNITS];
    for (int i = 0; i < model->unit_count; ++i) {
        saved[i] = model->units[i].selected;
        model->units[i].selected = false;
    }

    float crowd_radius = 2.75f;
    float crowd_radius_sq = crowd_radius * crowd_radius;
    for (int i = 0; i < model->unit_count; ++i) {
        Unit *unit = &model->units[i];
        if (unit->remove || unit->hp <= 0 || unit->owner != producer->owner ||
            (unit->traits & T_MOBILE) == 0) {
            continue;
        }
        float dx = unit->gx - exit_gx;
        float dy = unit->gy - exit_gy;
        if (i == spawned_index || dx * dx + dy * dy <= crowd_radius_sq) {
            unit->selected = true;
        }
    }

    float dx = exit_gx - producer->gx;
    float dy = exit_gy - producer->gy;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) {
        dx = 0.0f;
        dy = -1.0f;
        len = 1.0f;
    }
    float goal_gx = exit_gx + dx / len * 1.5f;
    float goal_gy = exit_gy + dy / len * 1.5f;
    issue_move_order_at(&model->map, model->units, model->unit_count, goal_gx, goal_gy);

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
    if (!model->plugin || !model->plugin->id) return false;
    if (model->unit_count >= MAX_UNITS) return false;

    bool dark_reign = strcmp(model->plugin->id, "dark-reign") == 0;
    uint16_t actor_id = dark_reign ? dark_reign_actor_id_for_product(product) :
        dark_colony_actor_id_for_product(product);
    const ActorType *actor_type = plugin_actor_type_by_id(model->plugin, actor_id);
    if (actor_id == 0 || !actor_type) return false;

    Unit new_unit;
    memset(&new_unit, 0, sizeof(new_unit));
    new_unit.type_id = actor_id;
    new_unit.owner = 0;
    new_unit.sprite_id = -1;
    new_unit.attack_target = -1;
    new_unit.harvest_target = -1;
    new_unit.frame = !dark_reign && product->product_class == RTS_PRODUCT_BUILDING ?
        dark_colony_building_frame_for_product_type(product->product_type) : 0;
    apply_actor_type_defaults(&new_unit, actor_type);
    apply_mobjinfo_defaults(model->plugin ? model->plugin->game_info : NULL, &new_unit);
    if (!dark_reign && product->product_class == RTS_PRODUCT_BUILDING) {
        int state_id = dark_colony_building_state_for_product_type(model->plugin->game_info,
                                                                   product->product_type);
        if (state_id > 0) {
            StateContext ctx = { .game_info = model->plugin->game_info };
            set_unit_state(&ctx, &new_unit, state_id);
        }
    }
    if (dark_reign && product->product_class == RTS_PRODUCT_BUILDING)
        new_unit.radius = 1.2f;
    float radius = new_unit.radius > 0.05f ? new_unit.radius : 0.42f;

    float gx = 0.0f;
    float gy = 0.0f;
    Unit *producer = &model->units[producer_index];
    bool use_barracks_release = dark_colony_product_uses_barracks_release(
        producer, product, actor_id);
    if (use_barracks_release &&
        dark_colony_barracks_release_spawn_point(model, producer, &new_unit, &gx, &gy) &&
        model_position_walkable_only(model, gx, gy, radius)) {
        /* The release FIN places the visual handoff; occupied exit cells are cleared below. */
    } else if (!find_spawn_position_near(model, producer, radius, &gx, &gy)) {
        return false;
    }

    new_unit.gx = gx;
    new_unit.gy = gy;
    int spawned_index = model->unit_count;
    model->units[model->unit_count++] = new_unit;
    if (use_barracks_release)
        order_barracks_exit_spacing(model, spawned_index, producer, gx, gy);
    return true;
}

static bool enqueue_model_unit_product(RtsGameModel *model,
                                       const StaticProductDefinition *product,
                                       int producer_index,
                                       uint16_t actor_id) {
    if (!model || !product || producer_index < 0 || producer_index >= model->unit_count)
        return false;
    Unit *producer = &model->units[producer_index];
    if (producer->production_queue_count > 0) {
        if (producer->production_actor_id != actor_id ||
            producer->production_product_type != product->product_type ||
            producer->production_product_class != (uint8_t)product->product_class ||
            producer->production_queue_count >= RTS_MAX_PRODUCTION_QUEUE) {
            return false;
        }
        producer->production_queue_count++;
        return true;
    }

    producer->production_actor_id = actor_id;
    producer->production_product_class = (uint8_t)product->product_class;
    producer->production_product_type = product->product_type;
    producer->production_queue_count = 1;
    producer->production_time_ms = product_training_time_ms(product);
    producer->production_time_left_ms = producer->production_time_ms;
    producer->production_release_active = false;
    producer->production_release_time_left_ms = 0;
    return true;
}

static bool dark_colony_product_uses_barracks_release(const Unit *producer,
                                                      const StaticProductDefinition *product,
                                                      uint16_t actor_id) {
    return producer && product && producer->type_id == DC_ACTOR_BRRKPOD &&
        product->product_class == RTS_PRODUCT_UNIT && product->product_type == 0 &&
        actor_id == DC_ACTOR_TROOPER;
}

static bool start_model_production_release(RtsGameModel *model, Unit *producer,
                                           const StaticProductDefinition *product,
                                           uint16_t actor_id) {
    if (!model || !producer || !product || !model->plugin ||
        !model->plugin->game_info) {
        return false;
    }
    if (!dark_colony_product_uses_barracks_release(producer, product, actor_id))
        return false;
    const GameInfo *game_info = model->plugin->game_info;
    int state_id = model_find_state_by_group_frame(game_info, DC_PRODUCTION_BUILD_GROUP,
                                                   DC_TRSCBUILD_FIRST_FRAME);
    int duration_ms = model_state_chain_duration_ms(game_info, state_id,
                                                    DC_PRODUCTION_BUILD_GROUP);
    if (state_id <= 0 || duration_ms <= 0) return false;
    StateContext ctx = {
        .map = &model->map,
        .effects = model->effects,
        .max_effects = MAX_VISUAL_EFFECTS,
        .game_info = game_info,
    };
    if (!spawn_state_effect(&ctx, state_id, producer->gx, producer->gy, 0))
        return false;
    producer->production_release_active = true;
    producer->production_release_time_left_ms = duration_ms;
    producer->production_time_left_ms = 0;
    return true;
}

static void clear_model_production(Unit *producer) {
    if (!producer) return;
    producer->production_actor_id = 0;
    producer->production_product_class = 0;
    producer->production_product_type = 0;
    producer->production_time_ms = 0;
    producer->production_time_left_ms = 0;
    producer->production_release_active = false;
    producer->production_release_time_left_ms = 0;
}

static void advance_model_production_queue(Unit *producer) {
    if (!producer) return;
    producer->production_release_active = false;
    producer->production_release_time_left_ms = 0;
    producer->production_queue_count--;
    if (producer->production_queue_count > 0) {
        producer->production_time_left_ms = producer->production_time_ms;
    } else {
        clear_model_production(producer);
    }
}

static bool create_model_product(RtsGameModel *model,
                                 const StaticProductDefinition *product) {
    if (!model || !product) return false;
    if (!model->plugin || !model->plugin->id) return false;
    if (!product_is_available(model, product)) return false;
    if (model->map.player_resources[0] < product->cost) return false;

    bool dark_reign = strcmp(model->plugin->id, "dark-reign") == 0;
    bool dark_colony = strcmp(model->plugin->id, "dark-colony") == 0;
    uint16_t actor_id = dark_reign ? dark_reign_actor_id_for_product(product) :
        dark_colony_actor_id_for_product(product);
    if (actor_id == 0 || !plugin_actor_type_by_id(model->plugin, actor_id)) return false;

    int producer_index = find_product_producer_index(model, product);
    if (producer_index < 0) return false;

    if (dark_colony && product->product_class == RTS_PRODUCT_UNIT) {
        if (!enqueue_model_unit_product(model, product, producer_index, actor_id)) return false;
        model->map.player_resources[0] -= product->cost;
        return true;
    }

    if (!spawn_finished_model_product(model, product, producer_index)) return false;
    model->map.player_resources[0] -= product->cost;
    return true;
}

static const StaticProductDefinition *product_by_class_type_for_model(
    const RtsGameModel *model, int product_class, int product_type) {
    if (!model || !model->plugin || !model->plugin->id) return NULL;
    const StaticProductDefinition *source = NULL;
    int source_count = 0;
    if (strcmp(model->plugin->id, "dark-colony") == 0) {
        source = DARK_COLONY_HUMAN_PRODUCTS;
        source_count = dark_colony_product_count();
    } else if (strcmp(model->plugin->id, "dark-reign") == 0) {
        source = DARK_REIGN_FG_PRODUCTS;
        source_count = dark_reign_product_count();
    }
    for (int i = 0; i < source_count; ++i) {
        if ((int)source[i].product_class == product_class &&
            source[i].product_type == product_type) {
            return &source[i];
        }
    }
    return NULL;
}

static void update_model_production(RtsGameModel *model, float dt) {
    if (!model || dt <= 0.0f) return;
    int elapsed_ms = (int)(dt * 1000.0f + 0.5f);
    if (elapsed_ms <= 0) elapsed_ms = 1;
    for (int i = 0; i < model->unit_count; ++i) {
        Unit *producer = &model->units[i];
        if (producer->production_queue_count <= 0) continue;
        if (producer->remove || producer->hp <= 0) {
            producer->production_queue_count = 0;
            clear_model_production(producer);
            continue;
        }
        if (producer->production_release_active) {
            producer->production_release_time_left_ms -= elapsed_ms;
            if (producer->production_release_time_left_ms > 0) continue;
            const StaticProductDefinition *product = product_by_class_type_for_model(
                model, producer->production_product_class,
                producer->production_product_type);
            if (!product || !spawn_finished_model_product(model, product, i)) {
                producer->production_release_time_left_ms = 250;
                continue;
            }
            producer = &model->units[i];
            advance_model_production_queue(producer);
            continue;
        }
        producer->production_time_left_ms -= elapsed_ms;
        while (producer->production_queue_count > 0 &&
               producer->production_time_left_ms <= 0) {
            const StaticProductDefinition *product = product_by_class_type_for_model(
                model, producer->production_product_class,
                producer->production_product_type);
            if (!product) {
                producer->production_time_left_ms = 250;
                break;
            }
            if (start_model_production_release(model, producer, product,
                                               producer->production_actor_id)) {
                break;
            }
            if (!spawn_finished_model_product(model, product, i)) {
                producer->production_time_left_ms = 250;
                break;
            }
            producer = &model->units[i];
            advance_model_production_queue(producer);
        }
    }
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

    /* Find the selected player building. Mobile units (harvesters, infantry) don't
       show a build panel — only stationary buildings do. */
    uint16_t selected_type = 0;
    const Unit *selected_building = NULL;
    for (int i = 0; i < model->unit_count; ++i) {
        const Unit *u = &model->units[i];
        if (u->selected && u->owner == 0 && u->hp > 0 && !u->remove &&
            (u->traits & T_SELECTABLE) != 0 &&
            (u->traits & T_MOBILE) == 0) {
            selected_type = u->type_id;
            selected_building = u;
            break;
        }
    }
    if (selected_type == 0) return;

    /* Show production queue progress for the selected building. */
    if (selected_building && selected_building->production_queue_count > 0) {
        const StaticProductDefinition *producing = product_by_class_type_for_model(
            model, selected_building->production_product_class,
            selected_building->production_product_type);
        int pct = 0;
        if (producing && selected_building->production_time_ms > 0) {
            int elapsed = selected_building->production_time_ms -
                          selected_building->production_time_left_ms;
            pct = elapsed * 100 / selected_building->production_time_ms;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
        }
        append_ui_script(dst, dst_size, "x 516 y 76 progress %d queue %d label \"%s\"\n",
                         pct, selected_building->production_queue_count,
                         producing ? producing->label : "");
    }

    /* Emit build buttons for products this building can produce. */
    int slot = 0;
    int source_count = dark_colony_product_count();
    for (int i = 0; i < source_count; ++i) {
        const StaticProductDefinition *product = &DARK_COLONY_HUMAN_PRODUCTS[i];
        bool this_maker = false;
        for (int m = 0; m < product->maker_count; ++m) {
            if (product->makers[m] == (int)selected_type) {
                this_maker = true;
                break;
            }
        }
        if (!this_maker) continue;

        int col = slot % 3;
        int row = slot / 3;
        slot++;
        int button_x = 516 + col * 36;
        int button_y = 92 + row * 42;
        bool available = product_is_available(model, product) &&
                         model->map.player_resources[0] >= product->cost;
        append_ui_script(dst, dst_size,
                         "x %d y %d btn %d enabled %d pic %d\n",
                         button_x, button_y, product->ui_id, available ? 1 : 0,
                         product->icon_frame);
        append_ui_script(dst, dst_size,
                         "x %d y %d text \"%s %d\"\n",
                         button_x + 8, button_y + 34, product->label, product->cost);
    }
}

static void build_dark_reign_ui_script(const RtsGameModel *model, char *dst, size_t dst_size) {
    if (!model || !dst || dst_size == 0) return;
    dst[0] = '\0';
    append_ui_script(dst, dst_size, "ui dark-reign 1\n");
    append_ui_script(dst, dst_size, "x 520 y 464 text \"Credits %d\"\n",
                     model->map.player_resources[0]);

    int source_count = dark_reign_product_count();
    for (int i = 0; i < source_count; ++i) {
        const StaticProductDefinition *product = &DARK_REIGN_FG_PRODUCTS[i];
        int button_x = 516 + (i % 3) * 36;
        int button_y = 92 + (i / 3) * 42;
        bool available = product_is_available(model, product);
        append_ui_script(dst, dst_size,
                         "x %d y %d btn %d enabled %d pic %d\n",
                         button_x, button_y, product->ui_id, available ? 1 : 0,
                         product->icon_frame);
        append_ui_script(dst, dst_size,
                         "x %d y %d text \"%s %d\"\n",
                         button_x + 8, button_y + 34, product->label, product->cost);
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
    update_model_production(model, dt);
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
                (unit->traits & T_SELECTABLE) != 0;
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
        return create_model_product(
            model, product_by_ui_id_for_model(model, command->data.activate_ui_button.ui_id));
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
        dst->facing_code = src->facing_code;
        dst->state_id = src->state_id;
        dst->render_flags = src->render_flags;
        dst->render_remap = src->render_remap;
        dst->render_intensity = src->render_intensity;
        dst->render_offset_x = src->render_offset_x;
        dst->render_offset_y = src->render_offset_y;
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
        dst->has_sprite_pivot = src->has_sprite_pivot;
        dst->sprite_pivot_x = src->sprite_pivot_x;
        dst->sprite_pivot_y = src->sprite_pivot_y;
        dst->frame_index = src->frame_index;
        dst->frame2_index = src->frame2_index;
        dst->frame3_index = src->frame3_index;
        dst->facing_code = src->facing_code;
        dst->render_flags = src->render_flags;
        dst->render2_flags = src->render2_flags;
        dst->render3_flags = src->render3_flags;
        snprintf(dst->sprite_name, sizeof(dst->sprite_name), "%s", src->sprite_name);
        snprintf(dst->sprite2_name, sizeof(dst->sprite2_name), "%s", src->sprite2_name);
        snprintf(dst->sprite3_name, sizeof(dst->sprite3_name), "%s", src->sprite3_name);
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
    } else if (model->plugin && model->plugin->id && strcmp(model->plugin->id, "dark-reign") == 0) {
        build_dark_reign_ui_script(model, out->ui_script, sizeof(out->ui_script));
    }
    return true;
}

const char *rts_game_model_last_error(const RtsGameModel *model) {
    return model && model->error[0] ? model->error : "";
}

int rts_game_model_products(const RtsGameModel *model, RtsProductDefinition *out, int max_products) {
    if (!model || !out || max_products <= 0) return 0;
    if (!model->plugin || !model->plugin->id) return 0;

    const StaticProductDefinition *source = NULL;
    int source_count = 0;
    if (strcmp(model->plugin->id, "dark-colony") == 0) {
        source = DARK_COLONY_HUMAN_PRODUCTS;
        source_count = dark_colony_product_count();
    } else if (strcmp(model->plugin->id, "dark-reign") == 0) {
        source = DARK_REIGN_FG_PRODUCTS;
        source_count = dark_reign_product_count();
    } else {
        return 0;
    }
    int count = source_count < max_products ? source_count : max_products;
    for (int i = 0; i < count; ++i) {
        const StaticProductDefinition *src = &source[i];
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
