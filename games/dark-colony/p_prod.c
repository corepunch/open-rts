#define _DEFAULT_SOURCE
#include "game.h"
#include "g_game.h"
#include "dc_types.h"
#include "info.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static const StaticProductDefinition DARK_COLONY_HUMAN_PRODUCTS[] = {
    /* Buildings — all built from the Exco Center */
    {  0, 206, "Exo-Ctr",   2000, 129, RTS_PRODUCT_BUILDING, 16, 0, { 0 }, 0, { MT_EXCOPOD }, 1 },
    {  1,  80, "Barracks",  1000,  20, RTS_PRODUCT_BUILDING, 17, 0, { 0 }, 1, { MT_EXCOPOD }, 1 },
    {  2,  81, "Sci-Pod",   2000,  21, RTS_PRODUCT_BUILDING, 20, 0, { 0 }, 1, { MT_EXCOPOD }, 1 },
    {  3,  82, "Robo-Ftr",  2000,  22, RTS_PRODUCT_BUILDING, 18, 0, { 2, 1 }, 2, { MT_EXCOPOD }, 1 },
    {  6,  83, "Rsch-Bay",  3000,  23, RTS_PRODUCT_BUILDING, 22, 0, { 4 }, 1, { MT_EXCOPOD }, 1 },
    {  4,  85, "Sci-Pod+",  2000,  26, RTS_PRODUCT_BUILDING, 21, 0, { 2 }, 1, { MT_EXCOPOD }, 1 },
    {  5,  86, "Robo-Ftr+", 2000,  30, RTS_PRODUCT_BUILDING, 19, 0, { 3, 2 }, 2, { MT_EXCOPOD }, 1 },
    /* Exco Center units */
    {  7,  87, "Exploiter", 1500,   8, RTS_PRODUCT_UNIT,      6, 0, { 0 }, 1, { MT_EXCOPOD }, 1 },
    /* Barracks units */
    {  9,  89, "Trooper",    350,   6, RTS_PRODUCT_UNIT,      0, 0, { 1 }, 1, { MT_BRRKPOD }, 1 },
    { 29,  90, "Sentinel",   450,   5, RTS_PRODUCT_UNIT,     43, 0, { 1, 2 }, 2, { MT_BRRKPOD }, 1 },
    { 13,  94, "S.A.R.G.E", 1500,  12, RTS_PRODUCT_UNIT,      4, 0, { 1, 6 }, 2, { MT_BRRKPOD }, 1 },
    /* Robot Factory units */
    { 11,  91, "Reaper",     600,  11, RTS_PRODUCT_UNIT,      2, 0, { 3, 2 }, 2, { MT_ROBOPOD }, 1 },
    { 12,  93, "Barrager",  1000,   7, RTS_PRODUCT_UNIT,      3, 0, { 5, 4 }, 2, { MT_ROBOPOD2 }, 1 },
    { 10,  92, "Osprey IV",  600,   9, RTS_PRODUCT_UNIT,      5, 0, { 0, 3, 4 }, 3, { MT_ROBOPOD }, 1 },
    /* Upgraded Robot Factory units */
    {  8,  88, "Firestorm",  900,  10, RTS_PRODUCT_UNIT,      1, 0, { 5 }, 1, { MT_ROBOPOD2 }, 1 },
    { 83, 135, "Medi-craft", 900,  29, RTS_PRODUCT_UNIT,     49, 0, { 4, 3, 6 }, 3, { MT_ROBOPOD }, 1 },
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
    case 16: return MT_EXCOPOD;
    case 17: return MT_BRRKPOD;
    case 18: return MT_ROBOPOD;
    case 19: return MT_ROBOPOD2;
    case 20: return MT_SCNCPOD;
    case 21: return MT_SCNCPOD2;
    case 22: return MT_RSCHPOD;
    default: return 0;
    }
}

static uint16_t unit_actor_id_for_product_type(int product_type) {
    switch (product_type) {
    case 0: return MT_TROOPER;
    case 2: return MT_REAPER;
    case 3: return MT_THUNDERBOLT;
    case 4: return MT_CYBORG;
    case 5: return MT_SCOUT;
    case 6: return MT_EXPLOITER;
    case 14: return MT_SLUG;
    case  8: return MT_GREY;
    case 13: return MT_ORTU;
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
    (void)product;
    return 0; /* All DC buildings initialize through mobjinfo[].spawnstate. */
}

int G_ModelBuildingStateForProduct(const gameinfo_t *game_info,
                                  const StaticProductDefinition *product) {
    if (!game_info || !game_info->states || !game_info->sprnames || !product) return -1;
    switch (product->product_type) {
    case 16: return S_EXCOPOD_STND;
    case 17: return S_BRRKPOD_STND;
    case 19: return S_ROBOPOD2_BUILD1;
    case 20: return S_SCNCPOD_BUILD1;
    case 21: return S_SCNCPOD2_BUILD1;
    default: return -1;
    }
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

bool DC_ProductActorMatches(int actor, int required) {
    return actor == required || (actor == MT_SCNCPOD2 && required == MT_SCNCPOD) ||
        (actor == MT_ROBOPOD2 && required == MT_ROBOPOD);
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

bool G_ModelProductAvailableForUnits(mobj_t *const *units, int unit_count,
                                     const StaticProductDefinition *product) {
    if (!units || unit_count < 0 || !product) return false;
    for (int i = 0; i < product->prerequisite_count; ++i) {
        const StaticProductDefinition *prereq =
            product_by_row_id(product->prerequisites[i]);
        if (!prereq || prereq->product_class != RTS_PRODUCT_BUILDING) return false;
        uint16_t actor_id = G_ModelActorIdForProduct(prereq);
        bool found = false;
        for (int j = 0; j < unit_count; ++j) {
            if (P_MobjIsHidden(units[j]) || units[j]->owner != 0 || units[j]->remove ||
                units[j]->hp <= 0 ||
                (states[units[j]->core.state_id].group == 6 && !units[j]->production) ||
                !DC_ProductActorMatches(units[j]->type_id, actor_id)) continue;
            found = true;
            break;
        }
        if (!found) return false;
    }
    return true;
}

/* The retail release channel (+0x24) is independent of the building's
 * main channel (+0x14). An ordinary mobj owns its FIN state lifetime. */
void A_DC_ProductionReady(mobj_t *release) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *producer = (mobj_t *)th;
        if (!producer->remove && producer->id == release->producer_id &&
            producer->production && producer->production->release_active) {
            producer->production->release_ready = true;
            break;
        }
    }
}

bool G_ModelStartProductionRelease(RtsGameModel *model, mobj_t *producer,
                                   const StaticProductDefinition *product,
                                   uint16_t actor_id) {
    (void)model;
    if (!producer || !producer->production || !product ||
        producer->type_id != MT_BRRKPOD || product->product_class != RTS_PRODUCT_UNIT ||
        actor_id != MT_TROOPER) return false;
    mobj_t *release = P_SpawnMobj(producer->core.position, MT_PRODUCTION_RELEASE);
    if (!release) return false;
    release->producer_id = producer->id;
    release->core.render_offset = producer->core.render_offset;
    release->core.angle = producer->core.angle;
    release->owner = producer->owner;
    release->team = producer->team;
    producer->production->release_active = true;
    producer->production->release_ready = false;
    producer->production->time_left_ms = 0;
    return true;
}

bool G_ModelSpecialReleaseSpawnPoint(const RtsGameModel *model, const mobj_t *producer,
                                     const StaticProductDefinition *product,
                                     const mobj_t *new_unit,
                                     float *out_gx, float *out_gy) {
    (void)model;
    if (!producer || !product || !new_unit || !out_gx || !out_gy ||
        producer->type_id != MT_BRRKPOD || product->product_class != RTS_PRODUCT_UNIT ||
        new_unit->type_id != MT_TROOPER) return false;
    /* HUBU.FIN/TRSCBUILD0 frame 47 minus TRSC.FIN/TRSCSTAND8.
     * The latter is the ANG90 spawn facing. Tests check these native commands. */
    ivec2_t offset = ivec2_add(producer->core.render_offset,
                              ivec2_sub((ivec2_t){-143, 79}, (ivec2_t){-159, 0}));
    fvec2_t position = fvec2_add(fixed3_xy_to_fvec2(producer->core.position),
                                (fvec2_t){(float)offset.x / g_cell_w,
                                           -(float)offset.y / g_cell_h});
    *out_gx = position.x;
    *out_gy = position.y;
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
            snapshot->units[i].type_id >= MT_EXCOPOD) {
            selected_type = snapshot->units[i].type_id;
            break;
        }
    }
    if (selected_type == 0) selected_type = MT_EXCOPOD;

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

/* A city's modules share the authored FIN origin, with native slot positions. */
static bool dc_build_city_module(mobj_t *producer, const StaticProductDefinition *product,
                                 uint16_t actor_id) {
    int slot;
    switch (actor_id) {
    case MT_BRRKPOD: slot = 1; break;
    case MT_ROBOPOD: case MT_ROBOPOD2: slot = 2; break;
    case MT_SCNCPOD: case MT_SCNCPOD2: slot = 3; break;
    case MT_RSCHPOD: slot = 4; break;
    default: return false;
    }
    mobj_t *previous = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *obj = (mobj_t *)th;
        if (obj->team != producer->team || obj->remove || obj->hp <= 0) continue;
        if (DC_ProductActorMatches(obj->type_id, actor_id)) return false;
        if ((actor_id == MT_SCNCPOD2 && obj->type_id == MT_SCNCPOD) ||
            (actor_id == MT_ROBOPOD2 && obj->type_id == MT_ROBOPOD)) previous = obj;
    }
    ivec2_t offset = DC_CitySlotOffset(slot);
    /* Native slot pixels become 8.8 via *8, then 16.16 via *256. */
    ivec2_t delta = ivec2_scale(ivec2_sub(offset, DC_CitySlotOffset(0)), 8 * 256);
    fixed3_t position = fixed3_add(producer->core.position,
                                  (fixed3_t){delta.x, delta.y, 0});
    mobj_t *building = P_SpawnMobj(position, actor_id);
    if (!building) return false;
    building->owner = producer->owner;
    building->team = producer->team;
    building->allegiance = producer->allegiance;
    building->native_type_id = product->product_type;
    building->core.render_offset = (ivec2_t){-offset.x, offset.y + g_cell_h};
    int state = G_ModelBuildingStateForProduct(gameinfo, product);
    if (state > 0) P_SetMobjState(building, state);
    if (previous) P_RemoveMobj(previous);
    return true;
}

bool G_ModelEnqueueProduction(mobj_t *producer, const StaticProductDefinition *product,
                              uint16_t actor_id) {
    if (!producer || !product || actor_id == 0) return false;
    if (product->product_class == RTS_PRODUCT_BUILDING)
        return dc_build_city_module(producer, product, actor_id);
    production_t *production = P_EnsureMobjProduction(producer);
    if (!production) return false;
    if (production->queue_count > 0) {
        if (production->actor_id != actor_id ||
            production->product_type != product->product_type ||
            production->product_class != RTS_PRODUCT_UNIT ||
            production->queue_count >= RTS_MAX_PRODUCTION_QUEUE) {
            return false;
        }
        production->queue_count++;
        return true;
    }
    production->actor_id = actor_id;
    production->product_class = RTS_PRODUCT_UNIT;
    production->product_type = product->product_type;
    production->queue_count = 1;
    production->time_ms = G_ModelProductTrainingTimeMs(product);
    production->time_left_ms = production->time_ms;
    production->release_active = false;
    production->release_ready = false;
    return true;
}

static bool dc_product_uses_barracks_release(const mobj_t *producer,
                                             const StaticProductDefinition *product,
                                             uint16_t actor_id) {
    return producer && product && producer->type_id == MT_BRRKPOD &&
        product->product_type == 0 && actor_id == MT_TROOPER;
}

static void dc_clear_production(mobj_t *producer) {
    P_FreeMobjProduction(producer);
}

static void dc_advance_production_queue(mobj_t *producer) {
    if (!producer || !producer->production) return;
    production_t *production = producer->production;
    production->release_active = false;
    production->release_ready = false;
    production->queue_count--;
    if (production->queue_count > 0) {
        production->time_left_ms = production->time_ms;
    } else {
        dc_clear_production(producer);
    }
}

static bool dc_position_available_for_spawn(const level_t *map, mobj_t *const *units,
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
        const mobj_t *other = units[i];
        if (other->remove || other->hp <= 0) continue;
        float other_radius = other->radius > 0.05f ? other->radius : 0.42f;
        float min_dist = radius + other_radius;
        if (fvec2_distance_squared(fixed3_xy_to_fvec2(other->core.position),
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

static bool dc_find_spawn_position_near(const level_t *map, mobj_t *const *units,
                                        int unit_count, const mobj_t *producer,
                                        float radius, float *out_gx,
                                        float *out_gy) {
    if (!map || !units || !producer || !out_gx || !out_gy) return false;
    fvec2_t producer_position = fixed3_xy_to_fvec2(producer->core.position);
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

static void dc_order_barracks_exit_spacing(const level_t *map, mobj_t *const *units, int unit_count,
                                           int spawned_index, const mobj_t *producer,
                                           float exit_gx, float exit_gy) {
    if (!map || !units || !producer || spawned_index < 0 || spawned_index >= unit_count)
        return;
    bool saved[unit_count ? unit_count : 1];
    for (int i = 0; i < unit_count; ++i) {
        saved[i] = P_MobjIsSelected(units[i]);
        P_MobjSetSelected(units[i], false);
    }

    float crowd_radius = 2.75f;
    float crowd_radius_sq = crowd_radius * crowd_radius;
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *unit = units[i];
        if (unit->remove || unit->hp <= 0 || unit->owner != producer->owner ||
            (unit->traits & MF_MOBILE) == 0) {
            continue;
        }
        if (i == spawned_index ||
            fvec2_distance_squared(fixed3_xy_to_fvec2(unit->core.position),
                                   (fvec2_t){ exit_gx, exit_gy }) <= crowd_radius_sq) {
            P_MobjSetSelected(unit, true);
        }
    }

    fvec2_t delta = fvec2_sub((fvec2_t){ exit_gx, exit_gy },
                             fixed3_xy_to_fvec2(producer->core.position));
    float len = sqrtf(fvec2_length_squared(delta));
    if (len < 0.01f) {
        delta = (fvec2_t){ 0.0f, -1.0f };
        len = 1.0f;
    }
    fvec2_t goal = fvec2_add((fvec2_t){ exit_gx, exit_gy },
                            fvec2_scale(delta, 1.5f / len));
    P_MoveOrderAt(map, units, unit_count, goal);

    for (int i = 0; i < unit_count; ++i) {
        P_MobjSetSelected(units[i], saved[i]);
    }
}

static bool dc_spawn_finished_unit_product(const level_t *map,
                                           mobj_t *const *units, int *unit_count,
                                           int producer_index,
                                           uint16_t actor_id) {
    if (!map || !units || !unit_count || producer_index < 0 ||
        producer_index >= *unit_count || actor_id == 0) {
        return false;
    }
    const mobjtype_t *type = NULL;
    const mobjtype_t *types = (const mobjtype_t *)actor_types;
    for (int i = 0; types && i < num_actor_types; ++i) {
        if (types[i].id == actor_id) {
            type = &types[i];
            break;
        }
    }
    if (!type) return false;

    mobj_t *producer = units[producer_index];
    if (!producer->production) return false;
    mobj_t *new_unit = P_SpawnMobj(fixed3_zero(), actor_id);
    if (!new_unit) return false;
    new_unit->core.angle = ANG90;
    new_unit->owner = producer->owner;
    new_unit->team = producer->team;
    new_unit->allegiance = producer->allegiance;
    float radius = new_unit->radius > 0.05f ? new_unit->radius : 0.42f;
    float gx = 0.0f;
    float gy = 0.0f;
    const StaticProductDefinition *product =
        G_ModelProductByClassType(NULL, RTS_PRODUCT_UNIT, producer->production->product_type);
    bool use_barracks_release = dc_product_uses_barracks_release(producer, product, actor_id);
    if (use_barracks_release &&
        G_ModelSpecialReleaseSpawnPoint(NULL, producer, product, new_unit, &gx, &gy)) {
        if (!dc_position_walkable_for_spawn(map, gx, gy, radius)) {
            P_RemoveMobj(new_unit);
            return false;
        }
        /* The FIN exit is fixed; occupied exit cells are cleared below. */
    } else if (!dc_find_spawn_position_near(map, units, *unit_count, producer,
                                            radius, &gx, &gy)) {
        P_RemoveMobj(new_unit);
        return false;
    }
    new_unit->core.position = fixed3_from_fvec2((fvec2_t){ gx, gy }, 0);
    if (use_barracks_release) {
        mobjlist_t objects = P_ListMobjs();
        dc_order_barracks_exit_spacing(map, objects.items, objects.count, objects.count - 1, producer, gx, gy);
        P_FreeMobjList(&objects);
    }
    return true;
}

bool G_ModelUpdateProduction(level_t *map, mobj_t *const *units, int *unit_count,
                             float dt) {
    if (!map || !units || !unit_count || dt <= 0.0f) return false;
    bool spawned = false;
    int elapsed_ms = (int)(dt * 1000.0f + 0.5f);
    if (elapsed_ms <= 0) elapsed_ms = 1;
    for (int i = 0; i < *unit_count; ++i) {
        mobj_t *producer = units[i];
        production_t *production = producer->production;
        if (!production || production->queue_count <= 0) continue;
        if (producer->remove || producer->hp <= 0) {
            production->queue_count = 0;
            dc_clear_production(producer);
            continue;
        }
        if (production->release_active) {
            if (!production->release_ready) continue;
            uint16_t actor_id = production->actor_id;
            if (!dc_spawn_finished_unit_product(map, units, unit_count, i, actor_id)) {
                continue;
            }
            spawned = true;
            producer = units[i];
            dc_advance_production_queue(producer);
            continue;
        }
        production->time_left_ms -= elapsed_ms;
        while (production->queue_count > 0 && production->time_left_ms <= 0) {
            uint16_t actor_id = production->actor_id;
            const StaticProductDefinition *product =
                G_ModelProductByClassType(NULL, RTS_PRODUCT_UNIT, production->product_type);
            if (product && G_ModelStartProductionRelease(NULL, producer, product, actor_id)) {
                break;
            }
            if (!dc_spawn_finished_unit_product(map, units, unit_count, i, actor_id)) {
                production->time_left_ms = 250;
                break;
            }
            spawned = true;
            producer = units[i];
            dc_advance_production_queue(producer);
        }
    }
    return spawned;
}
