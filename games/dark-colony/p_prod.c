#define _DEFAULT_SOURCE
#include "game.h"
#include "g_game.h"
#include "dc_types.h"
#include "info.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

enum {
    ACTOR_TROOPER = 1,
    ACTOR_EXCOPOD = 1000,
    ACTOR_BRRKPOD = 1001,
    ACTOR_ROBOPOD = 1002,
    ACTOR_ROBOPOD2 = 1003,
    ACTOR_SCNCPOD2 = 1005,
    PRODUCTION_BUILD_GROUP = 6,
    TRSCBUILD_FIRST_FRAME = 12,
};

static const StaticProductDefinition DARK_COLONY_HUMAN_PRODUCTS[] = {
    /* Buildings — all built from the Exco Center */
    {  0, 206, "Exo-Ctr",   2000, 129, RTS_PRODUCT_BUILDING, 16, 0, { 0 }, 0, { ACTOR_EXCOPOD }, 1 },
    {  1,  80, "Barracks",  1000,  20, RTS_PRODUCT_BUILDING, 17, 0, { 0 }, 1, { ACTOR_EXCOPOD }, 1 },
    {  2,  81, "Sci-Pod",   2000,  21, RTS_PRODUCT_BUILDING, 20, 0, { 0 }, 1, { ACTOR_EXCOPOD }, 1 },
    {  3,  82, "Robo-Ftr",  2000,  22, RTS_PRODUCT_BUILDING, 18, 0, { 2, 1 }, 2, { ACTOR_EXCOPOD }, 1 },
    {  6,  83, "Rsch-Bay",  3000,  23, RTS_PRODUCT_BUILDING, 22, 0, { 4 }, 1, { ACTOR_EXCOPOD }, 1 },
    {  4,  85, "Sci-Pod+",  2000,  26, RTS_PRODUCT_BUILDING, 21, 0, { 2 }, 1, { ACTOR_EXCOPOD }, 1 },
    {  5,  86, "Robo-Ftr+", 2000,  30, RTS_PRODUCT_BUILDING, 19, 0, { 3, 2 }, 2, { ACTOR_EXCOPOD }, 1 },
    /* Exco Center units */
    {  7,  87, "Exploiter", 1500,   8, RTS_PRODUCT_UNIT,      6, 0, { 0 }, 1, { ACTOR_EXCOPOD }, 1 },
    /* Barracks units */
    {  9,  89, "Trooper",    350,   6, RTS_PRODUCT_UNIT,      0, 0, { 1 }, 1, { ACTOR_BRRKPOD }, 1 },
    { 29,  90, "Sentinel",   450,   5, RTS_PRODUCT_UNIT,     43, 0, { 1, 2 }, 2, { ACTOR_BRRKPOD }, 1 },
    { 13,  94, "S.A.R.G.E", 1500,  12, RTS_PRODUCT_UNIT,      4, 0, { 1, 6 }, 2, { ACTOR_BRRKPOD }, 1 },
    /* Robot Factory units */
    { 11,  91, "Reaper",     600,  11, RTS_PRODUCT_UNIT,      2, 0, { 3, 2 }, 2, { ACTOR_ROBOPOD }, 1 },
    { 12,  93, "Barrager",  1000,   7, RTS_PRODUCT_UNIT,      3, 0, { 5, 4 }, 2, { ACTOR_ROBOPOD2 }, 1 },
    { 10,  92, "Osprey IV",  600,   9, RTS_PRODUCT_UNIT,      5, 0, { 3, 4 }, 2, { ACTOR_ROBOPOD }, 1 },
    /* Upgraded Robot Factory units */
    {  8,  88, "Firestorm",  900,  10, RTS_PRODUCT_UNIT,      1, 0, { 5 }, 1, { ACTOR_ROBOPOD2 }, 1 },
    { 83, 135, "Medi-craft", 900,  29, RTS_PRODUCT_UNIT,     49, 0, { 5, 6 }, 2, { ACTOR_ROBOPOD }, 1 },
};

static const StaticProductDefinition DARK_COLONY_ALIEN_PRODUCTS[] = {
    { 14, 0, "Slug",  350,  6, RTS_PRODUCT_UNIT, 0, 0, { 0 }, 1, { 0 }, 0 },
    {  8, 0, "Grey",  450,  5, RTS_PRODUCT_UNIT, 0, 0, { 0 }, 1, { 0 }, 0 },
    { 13, 0, "Ortu",  600, 11, RTS_PRODUCT_UNIT, 0, 0, { 0 }, 1, { 0 }, 0 },
};

static int product_count(void) {
    return (int)(sizeof(DARK_COLONY_HUMAN_PRODUCTS) /
                 sizeof(DARK_COLONY_HUMAN_PRODUCTS[0]));
}

static uint16_t actor_id_for_product_type(int product_type) {
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

static uint16_t unit_actor_id_for_product_type(int product_type) {
    switch (product_type) {
    case 0: return 1;
    case 2: return 4;
    case 3: return 5;
    case 4: return 6;
    case 5: return 7;
    case 6: return 3;
    case 14: return MT_DC_SLUG;
    case  8: return MT_DC_GREY;
    case 13: return MT_DC_ORTU;
    default: return 0;
    }
}

uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *product) {
    if (!product) return 0;
    if (product->product_class == RTS_PRODUCT_BUILDING)
        return actor_id_for_product_type(product->product_type);
    if (product->product_class == RTS_PRODUCT_UNIT)
        return unit_actor_id_for_product_type(product->product_type);
    return 0;
}

int G_ModelBuildingFrameForProduct(const StaticProductDefinition *product) {
    if (!product) return 0;
    switch (product->product_type) {
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

int G_ModelBuildingStateForProduct(const gameinfo_t *game_info,
                                  const StaticProductDefinition *product) {
    if (!game_info || !game_info->states || !game_info->sprnames || !product) return -1;
    const char *sprite_name = NULL;
    switch (product->product_type) {
    case 16:
    case 17:
        sprite_name = "HUBU";
        break;
    default:
        return -1;
    }
    int frame = G_ModelBuildingFrameForProduct(product);
    for (int i = 0; i < game_info->state_count; ++i) {
        const state_t *state = &game_info->states[i];
        if (state->sprite < 0 || state->sprite >= game_info->sprite_count) continue;
        if (state->frame != frame) continue;
        if (strcmp(game_info->sprnames[state->sprite], sprite_name) == 0)
            return i;
    }
    return -1;
}

int G_ModelProductTrainingTimeMs(const StaticProductDefinition *product) {
    if (!product || product->product_class != RTS_PRODUCT_UNIT) return 0;
    int ms = product->cost * 10;
    if (ms < 1000) ms = 1000;
    return ms;
}

int G_ModelAlienProducts(StaticProductDefinition *out, int max_products) {
    if (!out || max_products <= 0) return 0;
    int count = (int)(sizeof(DARK_COLONY_ALIEN_PRODUCTS) / sizeof(DARK_COLONY_ALIEN_PRODUCTS[0]));
    if (count > max_products) count = max_products;
    memcpy(out, DARK_COLONY_ALIEN_PRODUCTS, (size_t)count * sizeof(StaticProductDefinition));
    return count;
}

int G_ModelGetProducts(const RtsGameModel *model, int owner,
                       StaticProductDefinition *out, int max_products) {
    (void)model; (void)owner;
    if (!out || max_products <= 0) return 0;
    int count = product_count();
    if (count > max_products) count = max_products;
    memcpy(out, DARK_COLONY_HUMAN_PRODUCTS, (size_t)count * sizeof(StaticProductDefinition));
    return count;
}

const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *model, int ui_id) {
    (void)model;
    int count = product_count();
    for (int i = 0; i < count; ++i) {
        if (DARK_COLONY_HUMAN_PRODUCTS[i].ui_id == ui_id)
            return &DARK_COLONY_HUMAN_PRODUCTS[i];
    }
    return NULL;
}

const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *model,
                                                         int product_class,
                                                         int product_type) {
    (void)model;
    int count = product_count();
    for (int i = 0; i < count; ++i) {
        if ((int)DARK_COLONY_HUMAN_PRODUCTS[i].product_class == product_class &&
            DARK_COLONY_HUMAN_PRODUCTS[i].product_type == product_type)
            return &DARK_COLONY_HUMAN_PRODUCTS[i];
    }
    return NULL;
}

static const StaticProductDefinition *product_by_row_id(int row_id) {
    int count = product_count();
    for (int i = 0; i < count; ++i) {
        if (DARK_COLONY_HUMAN_PRODUCTS[i].row_id == row_id)
            return &DARK_COLONY_HUMAN_PRODUCTS[i];
    }
    return NULL;
}

bool G_ModelProductAvailable(const RtsGameModel *model, int owner,
                             const StaticProductDefinition *product) {
    if (!product) return false;
    for (int i = 0; i < product->prerequisite_count; ++i) {
        const StaticProductDefinition *prereq =
            product_by_row_id(product->prerequisites[i]);
        if (!prereq || prereq->product_class != RTS_PRODUCT_BUILDING) return false;
        uint16_t actor_id = G_ModelActorIdForProduct(prereq);
        if (!G_ModelHasActorType(model, owner, actor_id)) return false;
    }
    return true;
}

bool G_ModelProductAvailableForUnits(const mobj_t *units, int unit_count,
                                     const StaticProductDefinition *product) {
    if (!units || unit_count < 0 || !product) return false;
    for (int i = 0; i < product->prerequisite_count; ++i) {
        const StaticProductDefinition *prereq =
            product_by_row_id(product->prerequisites[i]);
        if (!prereq || prereq->product_class != RTS_PRODUCT_BUILDING) return false;
        uint16_t actor_id = G_ModelActorIdForProduct(prereq);
        bool found = false;
        for (int j = 0; j < unit_count; ++j) {
            if (units[j].hidden || units[j].owner != 0 || units[j].remove ||
                units[j].hp <= 0 || units[j].type_id != actor_id) continue;
            found = true;
            break;
        }
        if (!found) return false;
    }
    return true;
}

static const state_t *dc_model_state_at(const gameinfo_t *game_info, int state_id) {
    if (!game_info || !game_info->states || state_id < 0 || state_id >= game_info->state_count)
        return NULL;
    return &game_info->states[state_id];
}

static int dc_model_find_state_by_group_frame(const gameinfo_t *game_info, int group, int frame) {
    if (!game_info || !game_info->states) return -1;
    for (int i = 0; i < game_info->state_count; ++i) {
        const state_t *state = &game_info->states[i];
        if (state->misc1 != group || state->frame != frame) continue;
        return i;
    }
    return -1;
}

static int dc_model_state_chain_duration_ms(const gameinfo_t *game_info, int state_id, int group) {
    int tics = 0;
    int guard = 0;
    while (guard++ < (game_info ? game_info->state_count + 1 : 1)) {
        const state_t *state = dc_model_state_at(game_info, state_id);
        if (!state || state->misc1 != group) break;
        if (state->tics > 0) tics += state->tics;
        int next = state->nextstate;
        if (next == game_info->null_state || next == state_id) break;
        state_id = next;
    }
    if (tics <= 0) return 0;
    return (int)(tics * FIXED_DT * 1000.0f + 0.5f);
}

bool G_ModelStartProductionRelease(RtsGameModel *model, mobj_t *producer,
                                   const StaticProductDefinition *product,
                                   uint16_t actor_id) {
    (void)model;
    if (!producer || !product || !gameinfo) return false;
    if (producer->type_id != ACTOR_BRRKPOD ||
        product->product_class != RTS_PRODUCT_UNIT || product->product_type != 0 ||
        actor_id != ACTOR_TROOPER) {
        return false;
    }
    const gameinfo_t *game_info = gameinfo;
    int state_id = dc_model_find_state_by_group_frame(game_info, PRODUCTION_BUILD_GROUP,
                                                      TRSCBUILD_FIRST_FRAME);
    int duration_ms = dc_model_state_chain_duration_ms(game_info, state_id,
                                                       PRODUCTION_BUILD_GROUP);
    if (state_id <= 0 || duration_ms <= 0) return false;
    statecontext_t ctx = {
        .game_info = game_info,
    };
    if (!P_SetMobjState(&ctx, producer, state_id))
        return false;
    producer->production.release_active = true;
    producer->production.release_time_left_ms = duration_ms;
    producer->production.time_left_ms = 0;
    return true;
}

bool G_ModelSpecialReleaseSpawnPoint(const RtsGameModel *model, const mobj_t *producer,
                                     const StaticProductDefinition *product,
                                     const mobj_t *new_unit,
                                     float *out_gx, float *out_gy) {
    (void)model;
    if (!producer || !product || !new_unit || !out_gx || !out_gy || !gameinfo)
        return false;
    if (producer->type_id != ACTOR_BRRKPOD ||
        product->product_class != RTS_PRODUCT_UNIT || product->product_type != 0)
        return false;

    const gameinfo_t *game_info = gameinfo;
    const state_t *stand = dc_model_state_at(game_info, new_unit->core.state_id);
    if (!stand) return false;

    int stand_x = 0;
    int stand_y = 0;
    int release_state_id = dc_model_find_state_by_group_frame(game_info,
                                                              PRODUCTION_BUILD_GROUP,
                                                              TRSCBUILD_FIRST_FRAME);
    int release_x = 0;
    int release_y = 0;
    bool saw_release_trooper = false;
    int guard = 0;
    while (guard++ < game_info->state_count + 1) {
        const state_t *state = dc_model_state_at(game_info, release_state_id);
        if (!state || state->misc1 != PRODUCTION_BUILD_GROUP) break;
        int x = 0;
        int y = 0;
        if (state->sprite == stand->sprite && state->frame == stand->frame) {
            release_x = x;
            release_y = y;
            saw_release_trooper = true;
        }
        int next = state->nextstate;
        if (next == game_info->null_state || next == release_state_id) break;
        release_state_id = next;
    }
    if (!saw_release_trooper) return false;

    fvec2_t producer_position = fixedvec3_xy_to_fvec2(producer->core.position);
    *out_gx = producer_position.x + (float)(release_x - stand_x) / (float)CELL_W;
    *out_gy = producer_position.y - (float)(release_y - stand_y) / (float)CELL_H;
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

void G_ModelBuildUIScript(const RtsGameModel *model,
                          const RtsRenderSnapshot *snapshot,
                          char *dst, size_t dst_size) {
    if (!model || !snapshot || !dst || dst_size == 0) return;
    dst[0] = '\0';

    append_ui_script(dst, dst_size, "ui dark-colony 1\n");
    append_ui_script(dst, dst_size, "x 520 y 464 text \"P-7 %d\"\n",
                     snapshot->player_resources[0][0]);

    uint16_t selected_type = 0;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        if (snapshot->units[i].selected && snapshot->units[i].owner == 0 &&
            (snapshot->units[i].traits & RTS_RENDER_TRAIT_SELECTABLE) != 0 &&
            (snapshot->units[i].traits & RTS_RENDER_TRAIT_MOBILE) == 0 &&
            snapshot->units[i].type_id >= ACTOR_EXCOPOD) {
            selected_type = snapshot->units[i].type_id;
            break;
        }
    }
    if (selected_type == 0) selected_type = ACTOR_EXCOPOD;

    int slot = 0;
    int available_product_count = product_count();
    for (int i = 0; i < available_product_count; ++i) {
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
        bool available = G_ModelProductAvailable(model, 0, product);
        append_ui_script(dst, dst_size,
                         "x %d y %d btn %d enabled %d pic %d\n",
                         button_x, button_y, product->ui_id, available ? 1 : 0,
                         product->icon_frame);
        append_ui_script(dst, dst_size,
                         "x %d y %d text \"%s %d\"\n",
                         button_x + 8, button_y + 34, product->label, product->cost);
    }
}

typedef struct {
    int row_id;
    int desired_count;
} AiProductionGoal;

static const AiProductionGoal ai_production_goals[] = {
    { 1, 1 }, /* Barracks */
    { 2, 1 }, /* Sci-Pod */
    { 3, 1 }, /* Robo-Ftr */
    { 7, 2 }, /* Exploiters */
    { 9, 6 }, /* Troopers */
    { 11, 4 }, /* Reapers */
    { 10, 2 }, /* Osprey IV */
    { 13, 1 }, /* S.A.R.G.E. */
};

void G_ModelAIProduction(RtsGameModel *model, int elapsed_ms) {
    (void)elapsed_ms;
    if (!model) return;
    enum { AI_OWNER = 1 };

    for (size_t i = 0; i < sizeof(ai_production_goals) /
                         sizeof(ai_production_goals[0]); ++i) {
        const AiProductionGoal *goal = &ai_production_goals[i];
        const StaticProductDefinition *product = product_by_row_id(goal->row_id);
        if (!product) continue;
        if (!G_ModelProductAvailable(model, AI_OWNER, product)) continue;
        if (rts_game_model_player_resources(model, AI_OWNER, 0) < product->cost) continue;

        int producer_index = G_ModelFindProducerIndex(model, AI_OWNER, product);
        if (producer_index >= 0) {
            RtsGameCommand cmd = {
                .kind = RTS_GAME_COMMAND_BUILD_PRODUCT,
                .data.build_product = {
                    .producer_id = 0,
                    .producer_index = producer_index,
                    .ui_id = product->ui_id,
                },
            };
            if (rts_game_model_command(model, &cmd)) return;
        }
    }
}

/* ── interactive production simulation (raw mobj_t arrays, not RtsGameModel) ── */

bool G_ModelEnqueueProduction(mobj_t *producer, const StaticProductDefinition *product,
                              uint16_t actor_id) {
    if (!producer || !product || actor_id == 0) return false;
    if (producer->production.queue_count > 0) {
        if (producer->production.actor_id != actor_id ||
            producer->production.product_type != product->product_type ||
            producer->production.product_class != RTS_PRODUCT_UNIT ||
            producer->production.queue_count >= RTS_MAX_PRODUCTION_QUEUE) {
            return false;
        }
        producer->production.queue_count++;
        return true;
    }
    producer->production.actor_id = actor_id;
    producer->production.product_class = RTS_PRODUCT_UNIT;
    producer->production.product_type = product->product_type;
    producer->production.queue_count = 1;
    producer->production.time_ms = G_ModelProductTrainingTimeMs(product);
    producer->production.time_left_ms = producer->production.time_ms;
    producer->production.release_active = false;
    producer->production.release_time_left_ms = 0;
    return true;
}

static bool dc_product_uses_barracks_release(const mobj_t *producer,
                                             const StaticProductDefinition *product,
                                             uint16_t actor_id) {
    return producer && product && producer->type_id == ACTOR_BRRKPOD &&
        product->product_type == 0 && actor_id == ACTOR_TROOPER;
}

static bool dc_start_production_release(level_t *map,
                                        effect_t *effects, int max_effects,
                                        mobj_t *producer,
                                        const StaticProductDefinition *product,
                                        uint16_t actor_id) {
    if (!gameinfo || !producer || !product) return false;
    if (!dc_product_uses_barracks_release(producer, product, actor_id)) return false;
    int state_id = dc_model_find_state_by_group_frame(gameinfo, PRODUCTION_BUILD_GROUP,
                                                      TRSCBUILD_FIRST_FRAME);
    int duration_ms = dc_model_state_chain_duration_ms(gameinfo, state_id,
                                                       PRODUCTION_BUILD_GROUP);
    if (state_id <= 0 || duration_ms <= 0) return false;
    statecontext_t ctx = {
        .map = map,
        .effects = effects,
        .max_effects = max_effects,
        .game_info = gameinfo,
    };
    if (!P_SetMobjState(&ctx, producer, state_id)) return false;
    producer->production.release_active = true;
    producer->production.release_time_left_ms = duration_ms;
    producer->production.time_left_ms = 0;
    return true;
}

static void dc_clear_production(mobj_t *producer) {
    if (!producer) return;
    producer->production.actor_id = 0;
    producer->production.product_class = 0;
    producer->production.product_type = 0;
    producer->production.time_ms = 0;
    producer->production.time_left_ms = 0;
    producer->production.release_active = false;
    producer->production.release_time_left_ms = 0;
}

static void dc_advance_production_queue(mobj_t *producer) {
    if (!producer) return;
    producer->production.release_active = false;
    producer->production.release_time_left_ms = 0;
    producer->production.queue_count--;
    if (producer->production.queue_count > 0) {
        producer->production.time_left_ms = producer->production.time_ms;
    } else {
        dc_clear_production(producer);
    }
}

static bool dc_position_available_for_spawn(const level_t *map, const mobj_t *units,
                                            int unit_count, float gx, float gy,
                                            float radius) {
    if (!map || !units) return false;
    if (radius < 0.32f) radius = 0.32f;
    if (gx - radius < 0.0f || gy - radius < 0.0f ||
        gx + radius > (float)map->width || gy + radius > (float)map->height) {
        return false;
    }
    int min_x = (int)floorf(gx - radius);
    int max_x = (int)floorf(gx + radius);
    int min_y = (int)floorf(gy - radius);
    int max_y = (int)floorf(gy + radius);
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            if (!L_IsWalkable(map, x, y)) return false;
        }
    }
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *other = &units[i];
        if (other->remove || other->hp <= 0) continue;
        float other_radius = other->radius > 0.05f ? other->radius : 0.42f;
        float min_dist = radius + other_radius;
        if (fvec2_distance_squared(fixedvec3_xy_to_fvec2(other->core.position),
                                   (fvec2_t){ gx, gy }) <
            min_dist * min_dist) return false;
    }
    return true;
}

static bool dc_position_walkable_for_spawn(const level_t *map, float gx, float gy,
                                           float radius) {
    if (!map) return false;
    if (radius < 0.32f) radius = 0.32f;
    if (gx - radius < 0.0f || gy - radius < 0.0f ||
        gx + radius > (float)map->width || gy + radius > (float)map->height) {
        return false;
    }
    int min_x = (int)floorf(gx - radius);
    int max_x = (int)floorf(gx + radius);
    int min_y = (int)floorf(gy - radius);
    int max_y = (int)floorf(gy + radius);
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            if (!L_IsWalkable(map, x, y)) return false;
        }
    }
    return true;
}

static bool dc_find_spawn_position_near(const level_t *map, const mobj_t *units,
                                        int unit_count, const mobj_t *producer,
                                        float radius, float *out_gx,
                                        float *out_gy) {
    if (!map || !units || !producer || !out_gx || !out_gy) return false;
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
            float gx = (float)x + 0.5f;
            float gy = (float)y + 0.5f;
            if (!dc_position_available_for_spawn(map, units, unit_count, gx, gy, radius)) continue;
            *out_gx = gx;
            *out_gy = gy;
            return true;
        }
        for (int dy = -dist; dy <= dist; ++dy) {
            for (int dx = -dist; dx <= dist; ++dx) {
                if (dx != -dist && dx != dist && dy != -dist && dy != dist) continue;
                float gx = (float)(origin_x + dx) + 0.5f;
                float gy = (float)(origin_y + dy) + 0.5f;
                if (!dc_position_available_for_spawn(map, units, unit_count, gx, gy, radius)) continue;
                *out_gx = gx;
                *out_gy = gy;
                return true;
            }
        }
    }
    return false;
}

static void dc_order_barracks_exit_spacing(const level_t *map, mobj_t *units, int unit_count,
                                           int spawned_index, const mobj_t *producer,
                                           float exit_gx, float exit_gy) {
    if (!map || !units || !producer || spawned_index < 0 || spawned_index >= unit_count)
        return;
    bool saved[MAXMOBJS];
    for (int i = 0; i < unit_count; ++i) {
        saved[i] = units[i].selected;
        units[i].selected = false;
    }

    float crowd_radius = 2.75f;
    float crowd_radius_sq = crowd_radius * crowd_radius;
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *unit = &units[i];
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
    P_MoveOrderAt(map, units, unit_count, goal);

    for (int i = 0; i < unit_count; ++i) {
        units[i].selected = saved[i];
    }
}

static bool dc_spawn_finished_unit_product(const level_t *map,
                                           mobj_t *units, int *unit_count,
                                           int producer_index,
                                           uint16_t actor_id) {
    if (!map || !units || !unit_count || producer_index < 0 ||
        producer_index >= *unit_count || *unit_count >= MAXMOBJS || actor_id == 0) {
        return false;
    }
    const actortype_t *type = NULL;
    const actortype_t *types = (const actortype_t *)mobjinfo;
    for (int i = 0; types && i < num_mobjinfo; ++i) {
        if (types[i].id == actor_id) {
            type = &types[i];
            break;
        }
    }
    if (!type) return false;

    mobj_t new_unit;
    memset(&new_unit, 0, sizeof(new_unit));
    new_unit.type_id = actor_id;
    new_unit.owner = 0;
    new_unit.core.sprite_id = -1;
    new_unit.attack.target = -1;
    new_unit.harvest.target = -1;
    new_unit.traits = type->traits;
    new_unit.harvest.capacity = type->harvest.capacity;
    new_unit.speed = type->speed;
    new_unit.max_hp = type->max_hp;
    new_unit.hp = type->max_hp;
    new_unit.attack.range = type->attack.range;
    new_unit.attack.damage = type->attack.damage;
    new_unit.attack.cooldown_ms = type->attack.cooldown_ms;
    new_unit.attack.anim_ms = type->attack.anim_ms;
    new_unit.death.anim_ms = type->death.anim_ms;
    new_unit.harvest.state_id = type->harvest.state_id;
    new_unit.muzzle_flash_ms = type->muzzle_flash_ms;
    new_unit.core.render_intensity = 16;
    if (type->sprite_name)
        snprintf(new_unit.core.sprite_name, sizeof(new_unit.core.sprite_name), "%s", type->sprite_name);
    if (type->shadow_name)
        snprintf(new_unit.shadow_name, sizeof(new_unit.shadow_name), "%s", type->shadow_name);
    new_unit.muzzle_flash_sprite = type->muzzle_flash_sprite;
    new_unit.hit_effect_sprite = type->hit_effect_sprite;
    if (type->muzzle_flash_name)
        snprintf(new_unit.muzzle_flash_name, sizeof(new_unit.muzzle_flash_name), "%s", type->muzzle_flash_name);
    if (type->hit_effect_name)
        snprintf(new_unit.hit_effect_name, sizeof(new_unit.hit_effect_name), "%s", type->hit_effect_name);
    new_unit.death_effect_action = type->death_effect_action;
    P_SpawnMobj(gameinfo, &new_unit);

    float radius = new_unit.radius > 0.05f ? new_unit.radius : 0.42f;
    float gx = 0.0f;
    float gy = 0.0f;
    mobj_t *producer = &units[producer_index];
    const StaticProductDefinition *product =
        G_ModelProductByClassType(NULL, RTS_PRODUCT_UNIT, producer->production.product_type);
    bool use_barracks_release = dc_product_uses_barracks_release(producer, product, actor_id);
    if (use_barracks_release &&
        G_ModelSpecialReleaseSpawnPoint(NULL, producer, product, &new_unit, &gx, &gy) &&
        dc_position_walkable_for_spawn(map, gx, gy, radius)) {
        /* The release FIN places the visual handoff; occupied exit cells are cleared below. */
    } else if (!dc_find_spawn_position_near(map, units, *unit_count, producer,
                                            radius, &gx, &gy)) {
        return false;
    }
    new_unit.core.position = fixedvec3_from_fvec2((fvec2_t){ gx, gy }, 0);
    int spawned_index = *unit_count;
    units[(*unit_count)++] = new_unit;
    if (use_barracks_release)
        dc_order_barracks_exit_spacing(map, units, *unit_count, spawned_index, producer, gx, gy);
    return true;
}

bool G_ModelUpdateProduction(level_t *map, mobj_t *units, int *unit_count,
                             effect_t *effects, int max_effects, float dt) {
    if (!map || !units || !unit_count || dt <= 0.0f) return false;
    bool spawned = false;
    int elapsed_ms = (int)(dt * 1000.0f + 0.5f);
    if (elapsed_ms <= 0) elapsed_ms = 1;
    for (int i = 0; i < *unit_count; ++i) {
        mobj_t *producer = &units[i];
        if (producer->production.queue_count <= 0) continue;
        if (producer->remove || producer->hp <= 0) {
            producer->production.queue_count = 0;
            dc_clear_production(producer);
            continue;
        }
        if (producer->production.release_active) {
            producer->production.release_time_left_ms -= elapsed_ms;
            if (producer->production.release_time_left_ms > 0) continue;
            uint16_t actor_id = producer->production.actor_id;
            if (!dc_spawn_finished_unit_product(map, units, unit_count, i, actor_id)) {
                producer->production.release_time_left_ms = 250;
                continue;
            }
            spawned = true;
            producer = &units[i];
            dc_advance_production_queue(producer);
            continue;
        }
        producer->production.time_left_ms -= elapsed_ms;
        while (producer->production.queue_count > 0 &&
               producer->production.time_left_ms <= 0) {
            uint16_t actor_id = producer->production.actor_id;
            const StaticProductDefinition *product =
                G_ModelProductByClassType(NULL, RTS_PRODUCT_UNIT, producer->production.product_type);
            if (product && dc_start_production_release(map, effects, max_effects,
                                                       producer, product, actor_id)) {
                break;
            }
            if (!dc_spawn_finished_unit_product(map, units, unit_count, i, actor_id)) {
                producer->production.time_left_ms = 250;
                break;
            }
            spawned = true;
            producer = &units[i];
            dc_advance_production_queue(producer);
        }
    }
    return spawned;
}
