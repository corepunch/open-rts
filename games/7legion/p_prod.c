#define _DEFAULT_SOURCE
#include "game.h"
#include "g_game.h"
#include "info.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/*
 * 7th Legion production table.
 * Mobile Base (type 7) is the sole producer — it builds all units.
 * type ids match the actor table in g_game.c.
 */
static const StaticProductDefinition SL_PRODUCTS[] = {
    { 1, 1, "Trooper",     150, 0, RTS_PRODUCT_UNIT, 1, 0, {0}, 0, {7}, 1 },
    { 2, 2, "Slave",       100, 0, RTS_PRODUCT_UNIT, 2, 0, {0}, 0, {7}, 1 },
    { 3, 3, "Spider Mech", 600, 0, RTS_PRODUCT_UNIT, 3, 0, {0}, 0, {7}, 1 },
    { 4, 4, "Tank",        800, 0, RTS_PRODUCT_UNIT, 4, 0, {0}, 0, {7}, 1 },
    { 5, 5, "Rock Mech",  1200, 0, RTS_PRODUCT_UNIT, 5, 0, {0}, 0, {7}, 1 },
    { 6, 6, "Truck",       400, 0, RTS_PRODUCT_UNIT, 6, 0, {0}, 0, {7}, 1 },
};

static int product_count(void) {
    return (int)(sizeof(SL_PRODUCTS) / sizeof(SL_PRODUCTS[0]));
}

int G_ModelGetProducts(const RtsGameModel *model, int owner,
                       StaticProductDefinition *out, int max_products) {
    (void)model; (void)owner;
    if (!out || max_products <= 0) return 0;
    int count = product_count();
    if (count > max_products) count = max_products;
    memcpy(out, SL_PRODUCTS, (size_t)count * sizeof(StaticProductDefinition));
    return count;
}

const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *model, int ui_id) {
    (void)model;
    for (int i = 0; i < product_count(); ++i)
        if (SL_PRODUCTS[i].ui_id == ui_id) return &SL_PRODUCTS[i];
    return NULL;
}

const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *model,
                                                         int product_class,
                                                         int product_type) {
    (void)model;
    for (int i = 0; i < product_count(); ++i)
        if ((int)SL_PRODUCTS[i].product_class == product_class &&
            SL_PRODUCTS[i].product_type == product_type)
            return &SL_PRODUCTS[i];
    return NULL;
}

bool G_ModelProductAvailable(const RtsGameModel *model, int owner,
                             const StaticProductDefinition *product) {
    if (!product) return false;
    if (product->maker_count <= 0) return true;
    for (int i = 0; i < product->maker_count; ++i)
        if (G_ModelHasActorType(model, owner, (uint16_t)product->makers[i]))
            return true;
    return false;
}

uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *product) {
    return product ? (uint16_t)product->product_type : 0;
}

int G_ModelBuildingFrameForProduct(const StaticProductDefinition *product) {
    (void)product;
    return 0;
}

int G_ModelBuildingStateForProduct(const gameinfo_t *game_info,
                                  const StaticProductDefinition *product) {
    (void)game_info; (void)product;
    return -1;
}

int G_ModelProductTrainingTimeMs(const StaticProductDefinition *product) {
    if (!product) return 0;
    return product->cost * 10;
}

bool G_ModelStartProductionRelease(RtsGameModel *model, mobj_t *producer,
                                   const StaticProductDefinition *product,
                                   uint16_t actor_id) {
    (void)model; (void)producer; (void)product; (void)actor_id;
    return false;
}

bool G_ModelSpecialReleaseSpawnPoint(const RtsGameModel *model, const mobj_t *producer,
                                     const StaticProductDefinition *product,
                                     const mobj_t *new_unit,
                                     float *out_gx, float *out_gy) {
    (void)model; (void)producer; (void)product; (void)new_unit; (void)out_gx; (void)out_gy;
    return false;
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
    append_ui_script(dst, dst_size, "ui 7legion 1\n");
    append_ui_script(dst, dst_size, "x 630 y 3 text \"Credits %d\"\n",
                     snapshot->player_resources[0][0]);

    int selected_idx = -1;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        if (snapshot->units[i].selected && snapshot->units[i].owner == 0) {
            selected_idx = i;
            break;
        }
    }
    if (selected_idx < 0) return;

    uint16_t producer_type = snapshot->units[selected_idx].type_id;
    int button_index = 0;
    for (int i = 0; i < product_count(); ++i) {
        const StaticProductDefinition *product = &SL_PRODUCTS[i];
        bool is_maker = false;
        for (int m = 0; m < product->maker_count; ++m)
            if (product->makers[m] == (int)producer_type) { is_maker = true; break; }
        if (!is_maker) continue;
        int col = button_index % 3;
        int row = button_index / 3;
        int bx = 500 + col * 44;
        int by = 40 + row * 44;
        button_index++;
        bool available = G_ModelProductAvailable(model, 0, product) &&
                         snapshot->player_resources[0][0] >= product->cost;
        append_ui_script(dst, dst_size, "x %d y %d btn %d enabled %d pic %d\n",
                         bx, by, product->ui_id, available ? 1 : 0, product->icon_frame);
        append_ui_script(dst, dst_size, "x %d y %d text \"%s %d\"\n",
                         bx + 4, by + 34, product->label, product->cost);
    }
}

static const productiongoal_t sl_ai_goals[] = {
    { 2, 1 },  /* Slave */
    { 1, 3 },  /* Troopers */
    { 3, 1 },  /* Spider Mech */
    { 4, 1 },  /* Tank */
};

void G_ModelAIProduction(RtsGameModel *model, int elapsed_ms) {
    (void)model; (void)elapsed_ms;
    G_ProductionGoals(sl_ai_goals, sizeof(sl_ai_goals) / sizeof(*sl_ai_goals));
}
