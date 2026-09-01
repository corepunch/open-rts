#define _DEFAULT_SOURCE
#include "game.h"
#include "g_game.h"
#include "dr_types.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static const StaticProductDefinition DARK_REIGN_FG_PRODUCTS[] = {
#define DR_BUILD(ui_, label_, cost_, type_, p0_, p1_) \
    { (ui_), (ui_), (label_), (cost_), 0, RTS_PRODUCT_BUILDING, (type_), 0, \
      { (p0_), (p1_) }, ((p1_) == 0 ? ((p0_) == 0 ? 0 : 1) : 2), { 11 }, 1 }
#define DR_UNIT(ui_, label_, cost_, type_, p0_, p1_, m0_, m1_) \
    { (ui_), (ui_), (label_), (cost_), 0, RTS_PRODUCT_UNIT, (type_), 0, \
      { (p0_), (p1_) }, ((p1_) == 0 ? ((p0_) == 0 ? 0 : 1) : 2), \
      { (m0_), (m1_) }, ((m1_) == 0 ? 1 : 2) }
    DR_BUILD(10001, "FG HQ 1", 750, 10001, 0, 0),
    DR_BUILD(10002, "FG HQ 2", 1000, 10002, 10004, 10006),
    DR_BUILD(10003, "FG HQ 3", 1250, 10003, 10005, 10007),
    DR_BUILD(10004, "Barracks", 1500, 10004, 10001, 0),
    DR_BUILD(10005, "Advanced Barracks", 750, 10005, 10002, 0),
    DR_BUILD(10006, "Vehicle Factory", 2200, 10006, 10001, 0),
    DR_BUILD(10007, "Advanced Vehicle Factory", 2500, 10007, 10002, 0),
    DR_BUILD(10008, "Hover Factory", 500, 10008, 10004, 0),
    DR_BUILD(10009, "Repair Bay", 800, 10009, 10006, 0),
    DR_BUILD(10010, "Camera Tower", 200, 10010, 10002, 0),
    DR_BUILD(10011, "Refinery", 1000, 10011, 10003, 0),
    DR_BUILD(10012, "Anti-Air Site", 1000, 10012, 10002, 0),
    DR_BUILD(10013, "Guard Tower", 500, 10013, 10001, 0),
    DR_BUILD(10014, "Advanced Guard Tower", 1700, 10014, 10002, 0),
    DR_BUILD(10015, "Phase Factory 1", 1200, 10015, 10001, 0),
    DR_BUILD(10016, "Phase Factory 2", 1200, 10016, 10002, 0),
    DR_BUILD(10019, "Life Plant", 2500, 10019, 0, 0),
    DR_BUILD(10020, "Power Plant", 2000, 10020, 0, 0),
    DR_BUILD(10040, "Small Horizontal Bridge", 100, 10040, 0, 0),
    DR_BUILD(10041, "Small Vertical Bridge", 100, 10041, 0, 0),
    DR_BUILD(10042, "Small Centre Bridge", 150, 10042, 0, 0),
    { 11, 11, "Construction Rig", 300, 0, RTS_PRODUCT_UNIT, 11, 0,
      { 10001 }, 1, { 10001, 10002, 10003 }, 3 },
    DR_UNIT(9, "Raider", 150, 9, 10004, 0, 10004, 10005),
    DR_UNIT(10, "Mercenary", 300, 10, 10004, 0, 10004, 10005),
    DR_UNIT(8, "Sniper", 700, 8, 10005, 0, 10004, 10005),
    DR_UNIT(6, "Scout", 300, 6, 10004, 0, 10004, 10005),
    DR_UNIT(7, "Medic", 500, 7, 10004, 10009, 10004, 10005),
    DR_UNIT(3, "Saboteur", 800, 3, 10005, 0, 10004, 10005),
    DR_UNIT(2, "Mechanic", 500, 2, 10004, 10009, 10004, 10005),
    DR_UNIT(5, "Martyr", 600, 5, 10004, 0, 10004, 10005),
    DR_UNIT(4, "Spy", 1000, 4, 10005, 0, 10004, 10005),
    DR_UNIT(1, "Spider Bike", 500, 1, 10006, 0, 10006, 10007),
    DR_UNIT(15, "RAT", 450, 15, 10006, 0, 10006, 10007),
    DR_UNIT(20, "Skirmish Tank", 600, 20, 10006, 0, 10006, 10007),
    DR_UNIT(17, "Tank Hunter", 700, 17, 10006, 0, 10006, 10007),
    DR_UNIT(21, "Phase Tank", 600, 21, 10006, 10015, 10006, 10007),
    DR_UNIT(12, "Flak Jack", 500, 12, 10002, 10006, 10006, 10007),
    DR_UNIT(16, "Triple Rail Tank", 1300, 16, 10007, 0, 10006, 10007),
    DR_UNIT(19, "Hellstorm Artillery", 1100, 19, 10007, 0, 10006, 10007),
    DR_UNIT(23, "Sky Bike", 800, 23, 10006, 10011, 10006, 10007),
    DR_UNIT(24, "Outrider", 1400, 24, 10006, 10011, 10006, 10007),
    DR_UNIT(18, "Shockwave", 4000, 18, 10006, 10003, 10006, 10007),
    DR_UNIT(30, "Water Contaminator", 10000, 30, 10007, 10003, 10006, 10007),
    DR_UNIT(13, "Freighter", 1000, 13, 10006, 0, 10006, 10007),
    DR_UNIT(14, "Hover Freighter", 1500, 14, 10007, 0, 10006, 10007),
#undef DR_BUILD
#undef DR_UNIT
};

static int dark_reign_product_count(void) {
    return (int)(sizeof(DARK_REIGN_FG_PRODUCTS) /
                 sizeof(DARK_REIGN_FG_PRODUCTS[0]));
}

static uint16_t dark_reign_actor_id_for_requirement(int requirement_id) {
    if (requirement_id == 11 ||
        (requirement_id >= 10001 && requirement_id <= 10020))
        return (uint16_t)requirement_id;
    return 0;
}

uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *product) {
    return product ? dark_reign_actor_id_for_requirement(product->product_type) : 0;
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
    static const struct { int type; int seconds; } times[] = {
        {10001,22},{10002,30},{10003,37},{10004,45},{10005,23},
        {10006,66},{10007,75},{10008,15},{10009,24},{10010,6},
        {10011,30},{10012,30},{10013,22},{10014,51},{10015,36},{10016,36},
        {10019,37},{10020,30},{10040,3},{10041,3},{10042,5},
        {11,9},{9,5},{10,9},{8,21},{6,9},{7,15},{3,24},{2,15},
        {5,18},{4,30},{1,15},{15,14},{20,18},{17,21},{21,18},
        {12,15},{16,39},{19,33},{23,24},{24,42},{18,120},{30,150},
        {13,30},{14,45},
    };
    for (size_t i = 0; i < sizeof(times) / sizeof(times[0]); ++i)
        if (times[i].type == product->product_type) return times[i].seconds * 1000;
    return 0;
}

int G_ModelGetProducts(const RtsGameModel *model, int owner,
                       StaticProductDefinition *out, int max_products) {
    (void)model; (void)owner;
    if (!out || max_products <= 0) return 0;
    int count = dark_reign_product_count();
    if (count > max_products) count = max_products;
    memcpy(out, DARK_REIGN_FG_PRODUCTS, (size_t)count * sizeof(StaticProductDefinition));
    return count;
}

const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *model, int ui_id) {
    (void)model;
    int count = dark_reign_product_count();
    for (int i = 0; i < count; ++i) {
        if (DARK_REIGN_FG_PRODUCTS[i].ui_id == ui_id)
            return &DARK_REIGN_FG_PRODUCTS[i];
    }
    return NULL;
}

const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *model,
                                                         int product_class,
                                                         int product_type) {
    (void)model;
    int count = dark_reign_product_count();
    for (int i = 0; i < count; ++i) {
        if ((int)DARK_REIGN_FG_PRODUCTS[i].product_class == product_class &&
            DARK_REIGN_FG_PRODUCTS[i].product_type == product_type)
            return &DARK_REIGN_FG_PRODUCTS[i];
    }
    return NULL;
}

bool G_ModelProductAvailable(const RtsGameModel *model, int owner,
                             const StaticProductDefinition *product) {
    if (!product) return false;
    for (int i = 0; i < product->prerequisite_count; ++i) {
        if (!G_ModelHasActorType(model, owner,
                                 dark_reign_actor_id_for_requirement(product->prerequisites[i])))
            return false;
    }
    if (product->maker_count <= 0) return true;
    for (int i = 0; i < product->maker_count; ++i) {
        if (G_ModelHasActorType(model, owner,
                                dark_reign_actor_id_for_requirement(product->makers[i])))
            return true;
    }
    return false;
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

    append_ui_script(dst, dst_size, "ui dark-reign 1\n");
    append_ui_script(dst, dst_size, "x 216 y 5 text \"Taelon %d\"\n",
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
    int product_count = dark_reign_product_count();
    for (int i = 0; i < product_count; ++i) {
        const StaticProductDefinition *product = &DARK_REIGN_FG_PRODUCTS[i];
        bool is_maker = false;
        for (int m = 0; m < product->maker_count; ++m) {
            if (product->makers[m] == (int)producer_type) {
                is_maker = true;
                break;
            }
        }
        if (!is_maker) continue;

        int col = button_index % 3;
        int row = button_index / 3;
        int button_x = 516 + col * 36;
        int button_y = 92 + row * 42;
        button_index++;
        bool available = G_ModelProductAvailable(model, 0, product) &&
                         snapshot->player_resources[0][0] >= product->cost;
        append_ui_script(dst, dst_size,
                         "x %d y %d btn %d enabled %d pic %d\n",
                         button_x, button_y, product->ui_id, available ? 1 : 0,
                         product->icon_frame);
        append_ui_script(dst, dst_size,
                         "x %d y %d text \"%s %d\"\n",
                         button_x + 8, button_y + 34, product->label, product->cost);
    }
}

void G_ModelAIProduction(RtsGameModel *model, int elapsed_ms) {
    (void)model; (void)elapsed_ms;
}
