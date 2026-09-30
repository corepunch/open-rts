#define _DEFAULT_SOURCE
#include "d_net.h"
#include "p_local.h"
#include "game.h"
#include "g_game.h"
#include "dc_types.h"
#include "info.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static const StaticProductDefinition DARK_COLONY_PRODUCTS[] = {
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
    { 11,  91, "Reaper",     600,  11, RTS_PRODUCT_UNIT,      2, 0, { 3, 2 }, 2, { MT_ROBOPOD, MT_ROBOPOD2 }, 2 },
    { 12,  93, "Barrager",  1000,   7, RTS_PRODUCT_UNIT,      3, 0, { 5, 4 }, 2, { MT_ROBOPOD2 }, 1 },
    { 10,  92, "Osprey IV",  600,   9, RTS_PRODUCT_UNIT,      5, 0, { 0, 3, 4 }, 3, { MT_ROBOPOD, MT_ROBOPOD2 }, 2 },
    /* Upgraded Robot Factory units */
    {  8,  88, "Firestorm",  900,  10, RTS_PRODUCT_UNIT,      1, 0, { 5 }, 1, { MT_ROBOPOD2 }, 1 },
    { 83, 135, "Medi-craft", 900,  29, RTS_PRODUCT_UNIT,     49, 0, { 4, 3, 6 }, 3, { MT_ROBOPOD, MT_ROBOPOD2 }, 2 },
    /* Alien rows: DEPEND.TXT owns cost/dependencies; MAINE owns IDs/icons. */
    { 14, 205, "Mind-Hive", 2000, 130, RTS_PRODUCT_BUILDING, 28, 1, { 0 }, 0, { MT_ALIEN_MINDHIVE }, 1 },
    { 15,  41, "War. Fold", 1000,  24, RTS_PRODUCT_BUILDING, 29, 1, { 14 }, 1, { MT_ALIEN_MINDHIVE }, 1 },
    { 16,  42, "Breed-Pod", 2000, 114, RTS_PRODUCT_BUILDING, 32, 1, { 14 }, 1, { MT_ALIEN_MINDHIVE }, 1 },
    { 17,  43, "Gene-Sac",  2000,  25, RTS_PRODUCT_BUILDING, 30, 1, { 16, 15 }, 2, { MT_ALIEN_MINDHIVE }, 1 },
    { 20,  44, "Neur-Hive", 3000,  46, RTS_PRODUCT_BUILDING, 34, 1, { 18 }, 1, { MT_ALIEN_MINDHIVE }, 1 },
    { 18,  97, "Pod-Upgrd", 2000, 115, RTS_PRODUCT_BUILDING, 33, 1, { 16 }, 1, { MT_ALIEN_MINDHIVE }, 1 },
    { 19,  98, "Gene-Upgrd",2000,  39, RTS_PRODUCT_BUILDING, 31, 1, { 17, 14 }, 2, { MT_ALIEN_MINDHIVE }, 1 },
    { 21,  46, "Brozaar",   1500,  15, RTS_PRODUCT_UNIT, 14, 1, { 14 }, 1, { MT_ALIEN_MINDHIVE }, 1 },
    { 23,  48, "Gray",       350,  13, RTS_PRODUCT_UNIT,  8, 1, { 15 }, 1, { MT_ALIEN_WARHIVE }, 1 },
    { 28,  71, "Slom",       450, 116, RTS_PRODUCT_UNIT, 44, 1, { 15, 16 }, 2, { MT_ALIEN_WARHIVE }, 1 },
    { 27,  52, "Gorrem",    1500,  19, RTS_PRODUCT_UNIT, 12, 1, { 15, 20 }, 2, { MT_ALIEN_WARHIVE }, 1 },
    { 25,  50, "Sy-Demon",   600,  18, RTS_PRODUCT_UNIT, 10, 1, { 17, 16 }, 2, { MT_ALIEN_BRDRHIVE, MT_ALIEN_BRDRHIVE2 }, 2 },
    { 26,  51, "Atril",     1000,  14, RTS_PRODUCT_UNIT, 11, 1, { 19, 18 }, 2, { MT_ALIEN_BRDRHIVE2 }, 1 },
    { 24,  49, "Ortu",       600,  16, RTS_PRODUCT_UNIT, 13, 1, { 14, 17, 18 }, 3, { MT_ALIEN_BRDRHIVE, MT_ALIEN_BRDRHIVE2 }, 2 },
    { 22,  47, "Xenowort",   900,  17, RTS_PRODUCT_UNIT,  9, 1, { 19 }, 1, { MT_ALIEN_BRDRHIVE2 }, 1 },
    { 84, 134, "Zisp",       900,  36, RTS_PRODUCT_UNIT, 50, 1, { 18, 15, 20 }, 3, { MT_ALIEN_BRDRHIVE, MT_ALIEN_BRDRHIVE2 }, 2 },
    /* Native research rows: six weapon/armor pairs per race. */
    {30,56,"Alien weapon +1",1000,84,RTS_PRODUCT_UPGRADE,30,1,{15,16},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {31,100,"Alien weapon +2",2000,37,RTS_PRODUCT_UPGRADE,31,1,{30,18},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {32,99,"Alien armor +1",1000,55,RTS_PRODUCT_UPGRADE,32,1,{15,16},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {33,68,"Alien armor +2",2000,97,RTS_PRODUCT_UPGRADE,33,1,{32,18},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {37,55,"Alien weapon +1",1000,87,RTS_PRODUCT_UPGRADE,37,1,{19,16},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {38,77,"Alien weapon +2",2000,42,RTS_PRODUCT_UPGRADE,38,1,{37,18},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {39,66,"Alien armor +1",1000,59,RTS_PRODUCT_UPGRADE,39,1,{19,16},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {40,67,"Alien armor +2",2000,101,RTS_PRODUCT_UPGRADE,40,1,{39,18},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {41,58,"Alien weapon +1",1000,88,RTS_PRODUCT_UPGRADE,41,1,{17,16},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {42,103,"Alien weapon +2",2000,43,RTS_PRODUCT_UPGRADE,42,1,{41,18},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {43,72,"Alien armor +1",1000,60,RTS_PRODUCT_UPGRADE,43,1,{17,16},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {44,104,"Alien armor +2",2000,102,RTS_PRODUCT_UPGRADE,44,1,{43,18},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {45,57,"Alien weapon +1",1000,86,RTS_PRODUCT_UPGRADE,45,1,{17,18},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {46,101,"Alien weapon +2",2000,41,RTS_PRODUCT_UPGRADE,46,1,{45},1,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {47,69,"Alien armor +1",1000,58,RTS_PRODUCT_UPGRADE,47,1,{17,18},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {48,102,"Alien armor +2",2000,100,RTS_PRODUCT_UPGRADE,48,1,{47},1,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {49,59,"Alien weapon +1",1000,85,RTS_PRODUCT_UPGRADE,49,1,{18,19},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {50,105,"Alien weapon +2",2000,38,RTS_PRODUCT_UPGRADE,50,1,{49},1,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {51,73,"Alien armor +1",1000,56,RTS_PRODUCT_UPGRADE,51,1,{18,19},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {52,106,"Alien armor +2",2000,98,RTS_PRODUCT_UPGRADE,52,1,{51},1,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {53,60,"Alien weapon +1",1000,89,RTS_PRODUCT_UPGRADE,53,1,{20,16},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {54,78,"Alien weapon +2",2000,45,RTS_PRODUCT_UPGRADE,54,1,{53,18},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {55,74,"Alien armor +1",1000,61,RTS_PRODUCT_UPGRADE,55,1,{20,16},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {56,107,"Alien armor +2",2000,103,RTS_PRODUCT_UPGRADE,56,1,{55,18},2,{MT_ALIEN_MINDHIVE2,MT_ALIEN_MINDHIVE3},2},
    {59,110,"Human weapon +1",1000,47,RTS_PRODUCT_UPGRADE,59,0,{1,2},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {60,111,"Human weapon +2",2000,27,RTS_PRODUCT_UPGRADE,60,0,{59,4},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {61,112,"Human armor +1",1000,48,RTS_PRODUCT_UPGRADE,61,0,{1,2},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {62,113,"Human armor +2",2000,90,RTS_PRODUCT_UPGRADE,62,0,{61,4},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {63,114,"Human weapon +1",1000,80,RTS_PRODUCT_UPGRADE,63,0,{3,4},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {64,115,"Human weapon +2",2000,31,RTS_PRODUCT_UPGRADE,64,0,{63},1,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {65,116,"Human armor +1",1000,51,RTS_PRODUCT_UPGRADE,65,0,{3,4},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {66,117,"Human armor +2",2000,93,RTS_PRODUCT_UPGRADE,66,0,{65},1,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {67,118,"Human weapon +1",1000,82,RTS_PRODUCT_UPGRADE,67,0,{3,2},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {68,119,"Human weapon +2",2000,33,RTS_PRODUCT_UPGRADE,68,0,{67,4},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {69,120,"Human armor +1",1000,53,RTS_PRODUCT_UPGRADE,69,0,{3,2},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {70,121,"Human armor +2",2000,95,RTS_PRODUCT_UPGRADE,70,0,{69,4},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {71,122,"Human weapon +1",1000,81,RTS_PRODUCT_UPGRADE,71,0,{2,5},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {72,123,"Human weapon +2",2000,32,RTS_PRODUCT_UPGRADE,72,0,{71,4},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {73,124,"Human armor +1",1000,52,RTS_PRODUCT_UPGRADE,73,0,{2,5},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {74,125,"Human armor +2",2000,94,RTS_PRODUCT_UPGRADE,74,0,{73,4},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {75,126,"Human weapon +1",1000,64,RTS_PRODUCT_UPGRADE,75,0,{5,4},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {76,127,"Human weapon +2",2000,28,RTS_PRODUCT_UPGRADE,76,0,{75},1,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {77,128,"Human armor +1",1000,49,RTS_PRODUCT_UPGRADE,77,0,{5,4},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {78,129,"Human armor +2",2000,91,RTS_PRODUCT_UPGRADE,78,0,{77},1,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {79,130,"Human weapon +1",1000,83,RTS_PRODUCT_UPGRADE,79,0,{2,6},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {80,131,"Human weapon +2",2000,35,RTS_PRODUCT_UPGRADE,80,0,{4,79},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {81,132,"Human armor +1",1000,54,RTS_PRODUCT_UPGRADE,81,0,{2,6},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
    {82,133,"Human armor +2",2000,96,RTS_PRODUCT_UPGRADE,82,0,{4,81},2,{MT_SCNCPOD,MT_SCNCPOD2},2},
};

static uint8_t *upgrade_value(int owner, const StaticProductDefinition *product, int *tier) {
    if (owner < 0 || owner >= 8 || !product || product->product_class != RTS_PRODUCT_UPGRADE) return NULL;
    static const struct { int row, type; } groups[] = {
        {30,8},{37,42},{41,10},{45,13},{49,11},{53,12},
        {59,0},{63,5},{67,2},{71,41},{75,3},{79,4}
    };
    for (unsigned i=0; i<sizeof(groups)/sizeof(*groups); ++i) {
        int offset=product->row_id-groups[i].row;
        if (offset<0 || offset>=4) continue;
        *tier=1+offset%2;
        return offset<2 ? &level.upgrades[groups[i].type][owner].weapon :
                          &level.upgrades[groups[i].type][owner].armor;
    }
    return NULL;
}

static int product_count(void) {
    return (int)(sizeof(DARK_COLONY_PRODUCTS) /
                 sizeof(DARK_COLONY_PRODUCTS[0]));
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
    case 28: return MT_ALIEN_MINDHIVE;
    case 29: return MT_ALIEN_WARHIVE;
    case 30: return MT_ALIEN_BRDRHIVE;
    case 31: return MT_ALIEN_BRDRHIVE2;
    case 32: return MT_ALIEN_MINDHIVE2;
    case 33: return MT_ALIEN_MINDHIVE3;
    case 34: return MT_ALIEN_RSCHIVE;
    default: return 0;
    }
}

static uint16_t unit_actor_id_for_product_type(int product_type) {
    switch (product_type) {
    case 0: return MT_TROOPER;
    case 1: return MT_TURRET_CARRIER;
    case 2: return MT_REAPER;
    case 3: return MT_THUNDERBOLT;
    case 4: return MT_CYBORG;
    case 5: return MT_SCOUT;
    case 6: return MT_EXPLOITER;
    case 43: return MT_SENTINEL;
    case 49: return MT_MEDI_CRAFT;
    case 14: return MT_SLUG;
    case  8: return MT_GREY;
    case 13: return MT_ORTU;
    case 9: return MT_XENOWORT;
    case 10: return MT_SY_DEMON;
    case 11: return MT_ATRIL;
    case 12: return MT_GORREM;
    case 44: return MT_SLOM;
    case 50: return MT_ZISP;
    default: return 0;
    }
}

uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *product) {
    if (!product) return 0;
    if (product->product_class == RTS_PRODUCT_UPGRADE) return product->makers[0];
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
    case 16: return S_EXCOPOD_BUILD1;
    case 17: return S_BRRKPOD_BUILD1;
    case 18: return S_ROBOPOD_BUILD1;
    case 19: return S_ROBOPOD2_BUILD1;
    case 20: return S_SCNCPOD_BUILD1;
    case 21: return S_SCNCPOD2_BUILD1;
    case 22: return S_RSCHPOD_BUILD1;
    case 28: return S_BIOHIV_BUILD1;
    case 29: return S_WARHIVE_BUILD1;
    case 30: return S_BRDRHIV_BUILD1;
    case 31: return S_BRDRHIV2_BUILD1;
    case 32: return S_MINDHIV_BUILD1;
    case 33: return S_MNDHIV2_BUILD1;
    case 34: return S_RSCHIV_BUILD1;
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
    int count = 0;
    for (int i = 0; i < product_count() && count < max_products; ++i)
        if (DARK_COLONY_PRODUCTS[i].faction == 1) out[count++] = DARK_COLONY_PRODUCTS[i];
    return count;
}

int G_ModelGetProducts(const RtsGameModel *model, int owner,
                       StaticProductDefinition *out, int max_products) {
    (void)model;
    if (!out || max_products <= 0) return 0;
    int count = 0, race = DC_PlayerRace(owner);
    for (int i = 0; i < product_count() && count < max_products; ++i)
        if (DARK_COLONY_PRODUCTS[i].faction == race) out[count++] = DARK_COLONY_PRODUCTS[i];
    return count;
}

const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *model, int ui_id) {
    (void)model;
    int count = product_count();
    for (int i = 0; i < count; ++i) {
        if (DARK_COLONY_PRODUCTS[i].ui_id == ui_id)
            return &DARK_COLONY_PRODUCTS[i];
    }
    return NULL;
}

const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *model,
                                                         int product_class,
                                                         int product_type) {
    (void)model;
    int count = product_count();
    for (int i = 0; i < count; ++i) {
        if ((int)DARK_COLONY_PRODUCTS[i].product_class == product_class &&
            DARK_COLONY_PRODUCTS[i].product_type == product_type)
            return &DARK_COLONY_PRODUCTS[i];
    }
    return NULL;
}

static const StaticProductDefinition *product_by_row_id(int row_id) {
    int count = product_count();
    for (int i = 0; i < count; ++i) {
        if (DARK_COLONY_PRODUCTS[i].row_id == row_id)
            return &DARK_COLONY_PRODUCTS[i];
    }
    return NULL;
}

bool DC_ProductActorMatches(int actor, int required) {
    return actor == required || (actor == MT_SCNCPOD2 && required == MT_SCNCPOD) ||
        (actor == MT_ROBOPOD2 && required == MT_ROBOPOD) ||
        (actor == MT_ALIEN_MINDHIVE3 && required == MT_ALIEN_MINDHIVE2) ||
        (actor == MT_ALIEN_BRDRHIVE2 && required == MT_ALIEN_BRDRHIVE);
}

static bool dc_unit_is_ready(const mobj_t *unit, int owner) {
    if (!unit || P_MobjIsHidden(unit) || unit->owner != owner || unit->remove || unit->hp <= 0)
        return false;
    if (gameinfo && gameinfo->states && unit->core.state_id >= 0 &&
        unit->core.state_id < gameinfo->state_count &&
        gameinfo->states[unit->core.state_id].group == 6) return false;
    return true;
}

bool G_ModelProductAvailable(const RtsGameModel *model, int owner,
                             const StaticProductDefinition *product) {
    (void)model;
    if (!product) return false;
    if (product->product_class == RTS_PRODUCT_UPGRADE) {
        int tier;
        uint8_t *value=upgrade_value(owner,product,&tier);
        if (!value || *value+1!=tier) return false;
    }
    for (int i = 0; i < product->prerequisite_count; ++i) {
        const StaticProductDefinition *prereq =
            product_by_row_id(product->prerequisites[i]);
        if (!prereq) return false;
        if (prereq->product_class == RTS_PRODUCT_UPGRADE) {
            int tier;
            uint8_t *value=upgrade_value(owner,prereq,&tier);
            if (!value || *value<tier) return false;
            continue;
        }
        if (prereq->product_class != RTS_PRODUCT_BUILDING) return false;
        uint16_t actor_id = G_ModelActorIdForProduct(prereq);
        bool found = false;
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
            const mobj_t *unit = (mobj_t *)th;
            if (dc_unit_is_ready(unit, owner) && DC_ProductActorMatches(unit->type_id, actor_id)) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool G_ModelProductAvailableForUnits(mobj_t *const *units, int unit_count,
                                     const StaticProductDefinition *product) {
    if (!units || unit_count < 0 || !product) return false;
    if (product->product_class == RTS_PRODUCT_UPGRADE)
        return G_ModelProductAvailable(NULL,consoleplayer,product);
    for (int i = 0; i < product->prerequisite_count; ++i) {
        const StaticProductDefinition *prereq =
            product_by_row_id(product->prerequisites[i]);
        if (!prereq || prereq->product_class != RTS_PRODUCT_BUILDING) return false;
        uint16_t actor_id = G_ModelActorIdForProduct(prereq);
        bool found = false;
        for (int j = 0; j < unit_count; ++j) {
            if (!dc_unit_is_ready(units[j], consoleplayer) ||
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
                     snapshot->player_resources[consoleplayer][0]);

    uint16_t selected_type = 0;
    for (int i = 0; i < snapshot->unit_count; ++i) {
        if (snapshot->units[i].selected && snapshot->units[i].owner == consoleplayer &&
            (snapshot->units[i].traits & RTS_RENDER_TRAIT_SELECTABLE) != 0 &&
            (snapshot->units[i].traits & RTS_RENDER_TRAIT_MOBILE) == 0 &&
            snapshot->units[i].type_id >= MT_EXCOPOD) {
            selected_type = snapshot->units[i].type_id;
            break;
        }
    }
    if (selected_type == 0)
        selected_type = DC_PlayerRace(consoleplayer) ? MT_ALIEN_MINDHIVE : MT_EXCOPOD;

    int slot = 0;
    int available_product_count = product_count();
    for (int i = 0; i < available_product_count; ++i) {
        const StaticProductDefinition *product = &DARK_COLONY_PRODUCTS[i];
        if (product->faction != DC_PlayerRace(consoleplayer)) continue;
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
        bool available = G_ModelProductAvailable(model, consoleplayer, product);
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
    if (D_PlayerIsHuman(AI_OWNER)) return;

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
    case MT_BRRKPOD: case MT_ALIEN_WARHIVE: slot = 1; break;
    case MT_ROBOPOD: case MT_ROBOPOD2:
    case MT_ALIEN_BRDRHIVE: case MT_ALIEN_BRDRHIVE2: slot = 2; break;
    case MT_SCNCPOD: case MT_SCNCPOD2:
    case MT_ALIEN_MINDHIVE2: case MT_ALIEN_MINDHIVE3: slot = 3; break;
    case MT_RSCHPOD: case MT_ALIEN_RSCHIVE: slot = 4; break;
    default: return false;
    }
    mobj_t *previous = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *obj = (mobj_t *)th;
        if (obj->team != producer->team || obj->remove || obj->hp <= 0) continue;
        if (DC_ProductActorMatches(obj->type_id, actor_id)) return false;
        if ((actor_id == MT_SCNCPOD2 && obj->type_id == MT_SCNCPOD) ||
            (actor_id == MT_ROBOPOD2 && obj->type_id == MT_ROBOPOD) ||
            (actor_id == MT_ALIEN_MINDHIVE3 && obj->type_id == MT_ALIEN_MINDHIVE2) ||
            (actor_id == MT_ALIEN_BRDRHIVE2 && obj->type_id == MT_ALIEN_BRDRHIVE)) previous = obj;
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
    building->core.render_offset = (ivec2_t){-offset.x, offset.y};
    int state = G_ModelBuildingStateForProduct(gameinfo, product);
    if (state > 0) P_SetMobjState(building, state);
    if (previous) P_RemoveMobj(previous);
    return true;
}

bool G_ModelEnqueueProduction(mobj_t *producer, const StaticProductDefinition *product,
                              uint16_t actor_id) {
    if (!producer || !product || actor_id == 0) return false;
    if (product->product_class == RTS_PRODUCT_UPGRADE) {
        int tier;
        uint8_t *value=upgrade_value(producer->owner,product,&tier);
        if (!value || *value+1!=tier) return false;
        *value=tier;
        return true;
    }
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

static bool build_product(mobj_t *producer, const StaticProductDefinition *product, bool paid) {
    if (!producer || !product || producer->remove || producer->hp <= 0 ||
        producer->owner >= RTS_MODEL_MAX_PLAYERS ||
        !G_ModelProductAvailable(NULL, producer->owner, product) ||
        (!paid && level.player_resources[producer->owner][0] < product->cost)) return false;
    for (int i = 0; i < product->maker_count; ++i) {
        if (!DC_ProductActorMatches(producer->type_id, product->makers[i])) continue;
        if (!G_ModelEnqueueProduction(producer, product, G_ModelActorIdForProduct(product))) return false;
        if (!paid) level.player_resources[producer->owner][0] -= product->cost;
        return true;
    }
    return false;
}

bool G_PlayerBuildProduct(mobj_t *producer, const StaticProductDefinition *product) {
    return build_product(producer, product, false);
}

void DC_SelectPurchase(int owner, int ui_id, bool refund) {
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui_id);
    if (owner < 0 || owner >= 8 || !product || product->faction != DC_PlayerRace(owner) ||
        product->row_id < 0 || product->row_id >= 110) return;
    uint8_t *quantity = &level.purchases[owner][product->row_id].selected;
    if (refund) {
        if (*quantity) { --*quantity; level.player_resources[owner][0] += product->cost; }
        return;
    }
    /* 0x430075..0x4300bc: fifty units, one building/research purchase. */
    if (*quantity + level.purchases[owner][product->row_id].queued >=
            (product->product_class == RTS_PRODUCT_UNIT ? 50 : 1) ||
        level.player_resources[owner][0] < product->cost ||
        !G_ModelProductAvailable(NULL, owner, product)) return;
    if (product->product_class == RTS_PRODUCT_BUILDING) {
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            const mobj_t *actor = (mobj_t *)th;
            if (!actor->remove && actor->hp > 0 && actor->owner == owner &&
                DC_ProductActorMatches(actor->type_id, G_ModelActorIdForProduct(product))) return;
        }
    }
    ++*quantity;
    level.player_resources[owner][0] -= product->cost;
}

void DC_SubmitPurchases(int owner) {
    if (owner < 0 || owner >= 8) return;
    for (int row = 0; row < 110; ++row) {
        unsigned quantity = level.purchases[owner][row].selected;
        if (quantity + level.purchases[owner][row].queued > 50) continue;
        level.purchases[owner][row].queued += quantity;
        level.purchases[owner][row].selected = 0;
    }
    DC_RunPurchases();
}

void DC_RunPurchases(void) {
    /* 0x434c64 walks DEPEND row order, independent of the visible tab. */
    for (int owner = 0; owner < 8; ++owner)
    for (int row = 0; row < 110; ++row) {
        const StaticProductDefinition *product = product_by_row_id(row);
        uint8_t *quantity = &level.purchases[owner][row].queued;
        if (!product) continue;
        while (*quantity) {
            mobj_t *producer = G_FindProducer(owner, product);
            if (!producer || !build_product(producer, product, true)) break;
            --*quantity;
        }
    }
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
    mobj_t *crowd[unit_count ? unit_count : 1];
    int count = 0;

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
            crowd[count++] = unit;
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
    P_MoveUnitsAt(map, crowd, count, goal);
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
        /* Match the model path: FIN owns the point, terrain gates its cell. */
        if (!L_IsWalkable(map, (int)floorf(gx), (int)floorf(gy))) {
            P_RemoveMobj(new_unit);
            return false;
        }
        /* The FIN exit is fixed; occupied exit cells are cleared below. */
    } else if (!dc_find_spawn_position_near(map, units, *unit_count, producer,
                                            radius, &gx, &gy)) {
        P_RemoveMobj(new_unit);
        return false;
    }
    new_unit->core.position = fixed3_with_xy(new_unit->core.position,
                                             (fvec2_t){ gx, gy });
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
    DC_RunPurchases();
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

bool G_ModelProducerHasTech(const mobj_t *producer, const StaticProductDefinition *product) {
    (void)producer; (void)product;
    return true;
}

int G_ModelRadarLevel(int owner) {
    (void)owner;
    return 2;
}
