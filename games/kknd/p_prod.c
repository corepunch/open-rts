#define _DEFAULT_SOURCE
#include "game.h"
#include "g_game.h"
#include "info.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/*
 * KKnD Survivor production table.
 * Outpost produces infantry, Machine Shop produces vehicles.
 * Drill Rig is the resource base (not a producer).
 */
static const StaticProductDefinition KKND_PRODUCTS[] = {
    /* Infantry — produced at Outpost */
    { 1, 1, "Rifleman",     150, 0, RTS_PRODUCT_UNIT, MT_SURV_RIFLEMAN,     0, {0}, 0, {MT_SURV_OUTPOST}, 1 },
    { 2, 2, "Flamer",       200, 0, RTS_PRODUCT_UNIT, MT_SURV_FLAMER,       0, {0}, 0, {MT_SURV_OUTPOST}, 1 },
    { 3, 3, "RPG Launcher", 350, 0, RTS_PRODUCT_UNIT, MT_SURV_RPG_LAUNCHER, 0, {0}, 0, {MT_SURV_OUTPOST}, 1 },
    { 4, 4, "Sniper",       400, 0, RTS_PRODUCT_UNIT, MT_SURV_SNIPER,       0, {0}, 0, {MT_SURV_OUTPOST}, 1 },
    /* Vehicles — produced at Machine Shop */
    { 10, 10, "Dirt Bike",    300, 0, RTS_PRODUCT_UNIT, MT_SURV_DIRT_BIKE,      0, {0}, 0, {MT_SURV_MACHINE_SHOP}, 1 },
    { 11, 11, "4x4 Pickup",   500, 0, RTS_PRODUCT_UNIT, MT_SURV_4X4_PICKUP,     0, {0}, 0, {MT_SURV_MACHINE_SHOP}, 1 },
    { 12, 12, "Anaconda Tank", 900, 0, RTS_PRODUCT_UNIT, MT_SURV_ANACONDA_TANK,  0, {0}, 0, {MT_SURV_MACHINE_SHOP}, 1 },
    { 13, 13, "Autocannon",    700, 0, RTS_PRODUCT_UNIT, MT_SURV_AUTOCANNON_TANK,0, {0}, 0, {MT_SURV_MACHINE_SHOP}, 1 },
    { 14, 14, "Oil Tanker",   400, 0, RTS_PRODUCT_UNIT, MT_SURV_OIL_TANKER,     0, {0}, 0, {MT_SURV_MACHINE_SHOP}, 1 },
    /* Buildings — produced at Drill Rig */
    { 20, 20, "Outpost",      600, 0, RTS_PRODUCT_BUILDING, MT_SURV_OUTPOST,     0, {0}, 0, {MT_SURV_DRILLRIG}, 1 },
    { 21, 21, "Machine Shop",  800, 0, RTS_PRODUCT_BUILDING, MT_SURV_MACHINE_SHOP,0, {0}, 0, {MT_SURV_DRILLRIG}, 1 },
    { 22, 22, "Guard Tower",  500, 0, RTS_PRODUCT_BUILDING, MT_SURV_GUARD_TOWER, 0, {0}, 0, {MT_SURV_DRILLRIG}, 1 },
    /* === Mutant faction (AI) === */
    /* Mutant infantry — produced at Clan Hall */
    { 101, 101, "Berserker",   150, 0, RTS_PRODUCT_UNIT, MT_MUTE_BERSERKER,   1, {0}, 0, {MT_MUTE_CLANHALL}, 1 },
    { 102, 102, "Pyromaniac",  200, 0, RTS_PRODUCT_UNIT, MT_MUTE_PYROMANIAC,  1, {0}, 0, {MT_MUTE_CLANHALL}, 1 },
    { 103, 103, "Shotgunner",  250, 0, RTS_PRODUCT_UNIT, MT_MUTE_SHOTGUNNER,  1, {0}, 0, {MT_MUTE_CLANHALL}, 1 },
    { 104, 104, "Bazooka",     350, 0, RTS_PRODUCT_UNIT, MT_MUTE_BAZOOKA,     1, {0}, 0, {MT_MUTE_CLANHALL}, 1 },
    /* Mutant vehicles — produced at Beast Enclosure */
    { 110, 110, "Dire Wolf",    300, 0, RTS_PRODUCT_UNIT, MT_MUTE_DIRE_WOLF,       1, {0}, 0, {MT_MUTE_BEAST_ENCLOSURE}, 1 },
    { 111, 111, "Monster Truck",500, 0, RTS_PRODUCT_UNIT, MT_MUTE_MONSTER_TRUCK,   1, {0}, 0, {MT_MUTE_BEAST_ENCLOSURE}, 1 },
    { 112, 112, "Giant Scorpion",800, 0, RTS_PRODUCT_UNIT, MT_MUTE_GIANT_SCORPION, 1, {0}, 0, {MT_MUTE_BEAST_ENCLOSURE}, 1 },
    { 113, 113, "War Mastadont",1200, 0, RTS_PRODUCT_UNIT, MT_MUTE_WAR_MASTADONT,  1, {0}, 0, {MT_MUTE_BEAST_ENCLOSURE}, 1 },
    { 114, 114, "Oil Tanker",   400, 0, RTS_PRODUCT_UNIT, MT_MUTE_OIL_TANKER,     1, {0}, 0, {MT_MUTE_BEAST_ENCLOSURE}, 1 },
    /* Mutant buildings — produced at Drill Rig */
    { 120, 120, "Clan Hall",    600, 0, RTS_PRODUCT_BUILDING, MT_MUTE_CLANHALL,         1, {0}, 0, {MT_MUTE_DRILLRIG}, 1 },
    { 121, 121, "Beast Enclosure",800, 0, RTS_PRODUCT_BUILDING, MT_MUTE_BEAST_ENCLOSURE, 1, {0}, 0, {MT_MUTE_DRILLRIG}, 1 },
    { 122, 122, "Machinegun Nest",500, 0, RTS_PRODUCT_BUILDING, MT_MUTE_MACHINEGUN_NEST,1, {0}, 0, {MT_MUTE_DRILLRIG}, 1 },
};

static int product_count(void) {
    return (int)(sizeof(KKND_PRODUCTS) / sizeof(KKND_PRODUCTS[0]));
}

int G_ModelGetProducts(const RtsGameModel *model, int owner,
                       StaticProductDefinition *out, int max_products) {
    (void)model; (void)owner;
    if (!out || max_products <= 0) return 0;
    int count = product_count();
    if (count > max_products) count = max_products;
    memcpy(out, KKND_PRODUCTS, (size_t)count * sizeof(StaticProductDefinition));
    return count;
}

const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *model, int ui_id) {
    (void)model;
    for (int i = 0; i < product_count(); ++i)
        if (KKND_PRODUCTS[i].ui_id == ui_id) return &KKND_PRODUCTS[i];
    return NULL;
}

const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *model,
                                                         int product_class,
                                                         int product_type) {
    (void)model;
    for (int i = 0; i < product_count(); ++i)
        if ((int)KKND_PRODUCTS[i].product_class == product_class &&
            KKND_PRODUCTS[i].product_type == product_type)
            return &KKND_PRODUCTS[i];
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
    return product->cost * 8;
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
    append_ui_script(dst, dst_size, "ui kknd 1\n");
    append_ui_script(dst, dst_size, "x 400 y 3 text \"Oil %d\"\n",
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
        const StaticProductDefinition *product = &KKND_PRODUCTS[i];
        bool is_maker = false;
        for (int m = 0; m < product->maker_count; ++m)
            if (product->makers[m] == (int)producer_type) { is_maker = true; break; }
        if (!is_maker) continue;
        int by = 40 + button_index * 48;
        button_index++;
        bool available = G_ModelProductAvailable(model, 0, product) &&
                         snapshot->player_resources[0][0] >= product->cost;
        append_ui_script(dst, dst_size, "x 593 y %d btn %d enabled %d pic %d\n",
                         by, product->ui_id, available ? 1 : 0, product->icon_frame);
        append_ui_script(dst, dst_size, "x 593 y %d text \"%s %d\"\n",
                         by + 36, product->label, product->cost);
    }
}

/* Engine skirmish goals for either faction; makers determine eligibility. */
static const productiongoal_t kk_ai_goals[] = {
    { 21, 1 }, { 121, 1 },   /* Vehicle production */
    { 14, 1 }, { 114, 1 },   /* Oil tanker */
    { 1, 2 }, { 101, 2 },    /* Infantry */
    { 2, 1 }, { 102, 1 },    /* Flame infantry */
    { 3, 1 }, { 104, 1 },    /* Rockets */
    { 10, 1 }, { 110, 1 },   /* Fast vehicle */
    { 12, 1 }, { 113, 1 },   /* Heavy vehicle */
};

void G_ModelAIProduction(RtsGameModel *model, int elapsed_ms) {
    (void)model; (void)elapsed_ms;
    G_ProductionGoals(kk_ai_goals, sizeof(kk_ai_goals) / sizeof(*kk_ai_goals));
}
