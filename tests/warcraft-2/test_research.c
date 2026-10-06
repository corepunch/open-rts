#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <stdlib.h>

#define CHECK(c) RTS_CHECK(c, "Warcraft II research", #c)

static void fixture(void) {
    P_FreeLevel(&level);
    P_InitThinkers();
    G_InitGame();
    consoleplayer = 0;
    level.width = 32;
    level.height = 24;
    int cells = level.width * level.height;
    level.tile_ids = calloc((size_t)cells, sizeof(*level.tile_ids));
    level.blocked = calloc((size_t)cells, 1);
    level.cell_solid = calloc((size_t)cells, 1);
    level.cell_terrain = calloc((size_t)cells, 1);
    level.speeds = calloc(1, sizeof(*level.speeds));
    level.speeds->class_count = 2;
    level.speeds->terrain[1][0] = 100;
}

static mobj_t *spawn(int type, int x, int y, int owner) {
    isize2_t foot = mobjinfo[type].w2.footprint;
    bool structure = (mobjinfo[type].w2.flags & W2_STRUCTURE) != 0;
    fvec2_t at = structure ? (fvec2_t){x + foot.w * 0.5f, y + foot.h * 0.5f} : (fvec2_t){x + 0.5f, y + 0.5f};
    mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2(at, 0), (uint16_t)type);
    assert(unit);
    unit->owner = (uint8_t)owner;
    unit->team = (uint8_t)owner;
    unit->allegiance = owner == 0 ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
    if (structure) w2_mark_footprint(x, y, foot);
    return unit;
}

static const StaticProductDefinition *product(int ui) { return G_ModelProductByUIId(NULL, ui); }
static bool available(int owner, int ui) { return G_ModelProductAvailable(NULL, owner, product(ui)); }

static bool order(mobj_t *producer, int ui) {
    ticcmd_t cmd = {.order = TC_BUILD, .product = ui, .count = 1, .units = {producer->id}};
    G_RunTiccmd(producer->owner, &cmd);
    return producer->production && producer->production->queue_count > 0;
}

/* Run the production clock until the predicate holds, at most `seconds`. */
static void produce(int seconds, bool (*done)(void *), void *user) {
    for (int i = 0; i < seconds * RTS_TICRATE + RTS_TICRATE && !done(user); ++i) {
        P_Ticker();
        G_UpdateProduction(&level, NULL, NULL, FIXED_DT);
    }
}

static bool idle(void *user) { return !((mobj_t *)user)->production; }

/* Optional audit against the pinned Wargus upgrade.lua rows. */
static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *data = length > 0 ? calloc((size_t)length + 1, 1) : NULL;
    if (data && fread(data, 1, (size_t)length, file) != (size_t)length) { free(data); data = NULL; }
    fclose(file);
    return data;
}

static int audit_reference(void) {
    char *human = read_file("reference/wargus/scripts/human/upgrade.lua");
    char *orc = read_file("reference/wargus/scripts/orc/upgrade.lua");
    if (!human || !orc) {
        free(human); free(orc);
        puts("SKIP: reference/wargus not present, research costs not audited");
        return 0;
    }
    int audited = 0;
    for (int id = 1; id < W2_UPGRADE_COUNT; ++id) {
        const w2_upgrade_t *upgrade = W2_Upgrade(id);
        char key[64];
        snprintf(key, sizeof(key), "{\"%s\",", upgrade->name);
        const char *row = strstr(human, key);
        if (!row) row = strstr(orc, key);
        CHECK(row);
        const char *costs = strchr(row, '{');
        costs = costs ? strchr(costs + 1, '{') : NULL;
        int time = 0, gold = 0, lumber = 0, oil = 0;
        CHECK(costs && sscanf(costs, "{ %d , %d , %d , %d", &time, &gold, &lumber, &oil) == 4);
        CHECK(time == upgrade->time && gold == upgrade->gold && lumber == upgrade->lumber && oil == upgrade->oil);
        /* The modifier row names the stat and the first unit it applies to. */
        char modifier[96];
        snprintf(modifier, sizeof(modifier), "DefineModifier(\"%s\"", upgrade->name);
        const char *mod = strstr(human, modifier);
        if (!mod) mod = strstr(orc, modifier);
        CHECK(mod);
        const char *key_stat = upgrade->armor ? "{\"Armor\", " : "{\"PiercingDamage\", ";
        const char *stat = strstr(mod, key_stat);
        CHECK(stat && atoi(stat + strlen(key_stat)) == upgrade->bonus);
        char apply[96];
        snprintf(apply, sizeof(apply), "{\"apply-to\", \"unit-%s\"}", mobjinfo[upgrade->units[0]].name);
        const char *applied = strstr(mod, apply);
        CHECK(applied && applied < strstr(mod, ")\n"));
        ++audited;
    }
    CHECK(audited == W2_UPGRADE_COUNT - 1);
    free(human);
    free(orc);
    return 0;
}

static int test_catalog(void) {
    fixture();
    StaticProductDefinition products[64];
    CHECK(G_ModelGetProducts(NULL, 0, products, 64) == W2_UI_COUNT - 1);
    int research = 0, halls = 0;
    for (int i = 0; i < W2_UI_COUNT - 1; ++i) {
        const StaticProductDefinition *p = &products[i];
        CHECK(p->ui_id == i + 1 && product(p->ui_id) == G_ModelProductByClassType(NULL, p->product_class, p->product_type));
        if (p->product_class == RTS_PRODUCT_UPGRADE) {
            const w2_upgrade_t *upgrade = W2_Upgrade(p->product_type);
            CHECK(upgrade && p->cost == upgrade->gold && W2_ProductLumber(p) == upgrade->lumber &&
                  W2_ProductOil(p) == upgrade->oil && p->icon_frame == upgrade->icon);
            CHECK(G_ModelProductTrainingTimeMs(p) == upgrade->time * 1000);
            CHECK(p->maker_count == 1 && p->makers[0] == upgrade->maker);
            CHECK(G_ModelActorIdForProduct(p) == upgrade->units[0] && upgrade->units[0]);
            CHECK(upgrade->tier == 1 || W2_Upgrade(p->product_type - 1)->tier == upgrade->tier - 1);
            ++research;
        } else if (p->product_class == RTS_PRODUCT_BUILDING) {
            const w2_cost_t *cost = &mobjinfo[p->product_type].w2.costs;
            CHECK(p->cost == cost->resources[0] && W2_ProductLumber(p) == cost->resources[1] &&
                  W2_ProductOil(p) == cost->resources[2] && G_ModelProductTrainingTimeMs(p) == cost->time * 1000);
            CHECK(G_ModelActorIdForProduct(p) == p->product_type && p->maker_count == 1);
            CHECK(mobjinfo[p->product_type].w2.footprint.w == mobjinfo[p->makers[0]].w2.footprint.w);
            ++halls;
        }
    }
    CHECK(research == W2_UPGRADE_COUNT - 1 && halls == 4);
    CHECK(product(W2_UI_KEEP)->cost == 2000 && W2_ProductLumber(product(W2_UI_KEEP)) == 1000 &&
          W2_ProductOil(product(W2_UI_KEEP)) == 200);
    CHECK(product(W2_UI_CASTLE)->cost == 2500 && W2_ProductOil(product(W2_UI_CASTLE)) == 500);
    CHECK(W2_Upgrade(W2_UPGRADE_SWORD1)->gold == 800 && W2_Upgrade(W2_UPGRADE_SWORD2)->gold == 2400);
    return audit_reference();
}

static int test_research(void) {
    fixture();
    mobj_t *smith = spawn(MT_HUMAN_BLACKSMITH, 6, 6, 0);
    mobj_t *footman = spawn(MT_FOOTMAN, 3, 3, 0);
    mobj_t *enemy = spawn(MT_FOOTMAN, 20, 20, 1);
    int *stock = level.player_resources[0];
    stock[0] = 10000; stock[1] = 1000; stock[2] = 0;
    CHECK(available(0, W2_UI_SWORD1) && !available(0, W2_UI_SWORD2));
    CHECK(available(0, W2_UI_HUMAN_SHIELD1) && !available(0, W2_UI_ARROW1) && !available(0, W2_UI_AXE1));
    CHECK(!available(1, W2_UI_SWORD1)); /* Player 1 has no blacksmith. */
    CHECK(G_FindProducer(0, product(W2_UI_SWORD1)) == smith && !G_FindProducer(1, product(W2_UI_SWORD1)));
    CHECK(!G_FindProducer(0, product(W2_UI_ARROW1)));
    CHECK(!G_PlayerBuildProduct(footman, product(W2_UI_SWORD1)));
    CHECK(!G_PlayerBuildProduct(smith, product(W2_UI_SWORD2)));
    stock[0] = 799;
    CHECK(!G_PlayerBuildProduct(smith, product(W2_UI_SWORD1)) && stock[0] == 799 && !smith->production);
    stock[0] = 10000;
    stock[1] = 299;
    CHECK(!G_PlayerBuildProduct(smith, product(W2_UI_HUMAN_SHIELD1)) && stock[0] == 10000 && stock[1] == 299);
    stock[1] = 1000;
    CHECK(order(smith, W2_UI_SWORD1));
    CHECK(stock[0] == 9200 && stock[1] == 1000);
    CHECK(smith->production->product_class == RTS_PRODUCT_UPGRADE &&
          smith->production->product_type == W2_UPGRADE_SWORD1 && smith->production->time_ms == 200000);
    /* One research at a time, and the same one cannot be queued twice. */
    CHECK(!G_PlayerBuildProduct(smith, product(W2_UI_HUMAN_SHIELD1)) && stock[0] == 9200);
    CHECK(!G_PlayerBuildProduct(smith, product(W2_UI_SWORD1)) && smith->production->queue_count == 1);
    CHECK(!available(0, W2_UI_SWORD2));
    produce(150, idle, smith);
    CHECK(smith->production && level.upgrades[MT_FOOTMAN][0].weapon == 0);
    produce(60, idle, smith);
    CHECK(!smith->production);
    CHECK(level.upgrades[MT_FOOTMAN][0].weapon == 1 && level.upgrades[MT_KNIGHT][0].weapon == 1 &&
          level.upgrades[MT_PALADIN][0].weapon == 1);
    CHECK(level.upgrades[MT_FOOTMAN][1].weapon == 0 && level.upgrades[MT_ARCHER][0].weapon == 0);
    CHECK(level.upgrades[MT_FOOTMAN][0].armor == 0);
    CHECK(W2_PiercingDamage(footman) == 5 && W2_PiercingDamage(enemy) == 3);
    CHECK(W2_UpgradeLevel(0, W2_Upgrade(W2_UPGRADE_SWORD1)) == 1);
    CHECK(!available(0, W2_UI_SWORD1) && available(0, W2_UI_SWORD2));
    CHECK(!G_PlayerBuildProduct(smith, product(W2_UI_SWORD1)) && stock[0] == 9200);
    CHECK(order(smith, W2_UI_HUMAN_SHIELD1) && stock[0] == 8900 && stock[1] == 700);
    produce(250, idle, smith);
    CHECK(level.upgrades[MT_FOOTMAN][0].armor == 1 && W2_Armor(footman) == 4 && W2_Armor(enemy) == 2);
    CHECK(order(smith, W2_UI_SWORD2) && stock[0] == 6500);
    produce(300, idle, smith);
    CHECK(level.upgrades[MT_FOOTMAN][0].weapon == 2 && W2_PiercingDamage(footman) == 7);
    CHECK(!available(0, W2_UI_SWORD1) && !available(0, W2_UI_SWORD2));
    /* The orc line is separate and priced in lumber too. */
    mobj_t *orc_smith = spawn(MT_ORC_BLACKSMITH, 20, 6, 1);
    mobj_t *grunt = spawn(MT_GRUNT, 20, 10, 1);
    level.player_resources[1][0] = 500;
    level.player_resources[1][1] = 99;
    CHECK(available(1, W2_UI_AXE1) && !available(1, W2_UI_SWORD1));
    CHECK(!order(orc_smith, W2_UI_AXE1) && level.player_resources[1][0] == 500);
    level.player_resources[1][1] = 100;
    CHECK(order(orc_smith, W2_UI_AXE1) && level.player_resources[1][0] == 0 && level.player_resources[1][1] == 0);
    produce(210, idle, orc_smith);
    CHECK(level.upgrades[MT_GRUNT][1].weapon == 1 && W2_PiercingDamage(grunt) == 5);
    CHECK(level.upgrades[MT_GRUNT][0].weapon == 0 && level.upgrades[MT_FOOTMAN][1].weapon == 0);
    /* Arrows need the lumber mill, and a destroyed researcher loses the order. */
    mobj_t *mill = spawn(MT_ELVEN_LUMBER_MILL, 12, 12, 0);
    CHECK(available(0, W2_UI_ARROW1) && order(mill, W2_UI_ARROW1) && stock[0] == 6200 && stock[1] == 400);
    P_DamageMobj(mill, enemy, mill->hp);
    G_ProductionTicker(FIXED_DT);
    CHECK(!mill->production && level.upgrades[MT_ARCHER][0].weapon == 0);
    return 0;
}

static int test_hall_upgrades(void) {
    fixture();
    mobj_t *hall = spawn(MT_TOWN_HALL, 4, 4, 0);
    mobj_t *peasant = spawn(MT_PEASANT, 10, 10, 0);
    uint32_t hall_id = hall->id;
    int *stock = level.player_resources[0];
    stock[0] = 2000; stock[1] = 1000; stock[2] = 200;
    CHECK(!available(0, W2_UI_KEEP) && !available(0, W2_UI_CASTLE) && !available(0, W2_UI_STRONGHOLD));
    mobj_t *barracks = spawn(MT_HUMAN_BARRACKS, 12, 4, 0);
    CHECK(available(0, W2_UI_KEEP) && !available(0, W2_UI_CASTLE));
    CHECK(G_FindProducer(0, product(W2_UI_KEEP)) == hall);
    stock[2] = 199;
    CHECK(!order(hall, W2_UI_KEEP) && stock[0] == 2000 && stock[1] == 1000);
    stock[2] = 200;
    /* A training hall finishes that first; an upgrading hall trains nothing. */
    stock[0] = 2400;
    CHECK(order(hall, 3) && stock[0] == 2000);
    CHECK(!G_PlayerBuildProduct(hall, product(W2_UI_KEEP)) && stock[0] == 2000);
    produce(50, idle, hall);
    CHECK(!hall->production && stock[0] == 2000);
    hall->hp = 600;
    CHECK(order(hall, W2_UI_KEEP) && stock[0] == 0 && stock[1] == 0 && stock[2] == 0);
    CHECK(hall->production->product_class == RTS_PRODUCT_BUILDING && hall->production->time_ms == 200000);
    stock[0] = 400;
    CHECK(!G_PlayerBuildProduct(hall, product(3)) && stock[0] == 400);
    produce(150, idle, hall);
    CHECK(hall->type_id == MT_TOWN_HALL && hall->production);
    produce(60, idle, hall);
    CHECK(P_MobjById(hall_id) == hall && !hall->production);
    CHECK(hall->type_id == MT_KEEP && hall->info == &actor_types[MT_KEEP - 1]);
    CHECK(hall->max_hp == 1400 && hall->hp == 700);
    CHECK(!strcmp(hall->core.sprite_name, "keep") && hall->core.state_id == mobjinfo[MT_KEEP].spawnstate);
    CHECK(hall->core.position.x == fixed3_from_fvec2((fvec2_t){6, 6}, 0).x);
    CHECK((hall->traits & (MF_SELECTABLE | MF_RESOURCE_BASE)) == (MF_SELECTABLE | MF_RESOURCE_BASE));
    CHECK(level.cell_solid[L_Index(&level, 4, 4)] && level.cell_solid[L_Index(&level, 7, 7)]);
    CHECK(G_ModelHasActorType(NULL, 0, MT_KEEP) && !G_ModelHasActorType(NULL, 0, MT_TOWN_HALL));
    CHECK(W2_ResourceIncome(0, 0) == 110);
    CHECK(G_FindProducer(0, product(3)) == hall); /* Still trains peasants. */
    CHECK(!available(0, W2_UI_KEEP) && !available(0, W2_UI_CASTLE));
    /* The peasant returns gold to the keep, with its bonus. */
    peasant->harvest.cargo = 100;
    peasant->harvest.resource_type = 0;
    CHECK(W2_ReturnGoods(peasant) && peasant->harvest.base == hall);
    for (int i = 0; i < 1500 && stock[0] == 400; ++i) P_Ticker();
    CHECK(stock[0] == 510);
    spawn(MT_STABLES, 12, 12, 0);
    spawn(MT_HUMAN_BLACKSMITH, 16, 12, 0);
    CHECK(!available(0, W2_UI_CASTLE));
    spawn(MT_ELVEN_LUMBER_MILL, 20, 12, 0);
    CHECK(available(0, W2_UI_CASTLE));
    stock[0] = 2500; stock[1] = 1200; stock[2] = 500;
    CHECK(order(hall, W2_UI_CASTLE) && stock[0] == 0 && stock[1] == 0 && stock[2] == 0);
    produce(210, idle, hall);
    CHECK(hall->type_id == MT_CASTLE && hall->max_hp == 1600 && hall->hp == 800);
    CHECK(W2_ResourceIncome(0, 0) == 120 && !available(0, W2_UI_CASTLE));
    (void)barracks;
    /* The orc line mirrors it. */
    mobj_t *great_hall = spawn(MT_GREAT_HALL, 24, 4, 1);
    spawn(MT_ORC_BARRACKS, 24, 10, 1);
    level.player_resources[1][0] = 2000; level.player_resources[1][1] = 1000; level.player_resources[1][2] = 200;
    CHECK(available(1, W2_UI_STRONGHOLD) && !available(1, W2_UI_KEEP));
    CHECK(order(great_hall, W2_UI_STRONGHOLD));
    produce(210, idle, great_hall);
    CHECK(great_hall->type_id == MT_STRONGHOLD && W2_ResourceIncome(1, 0) == 110);
    CHECK(!available(1, W2_UI_FORTRESS));
    return 0;
}

int main(void) {
    RTS_RUN(test_catalog());
    RTS_RUN(test_research());
    RTS_RUN(test_hall_upgrades());
    P_FreeLevel(&level);
    puts("PASS: pinned Wargus research costs, tiers, prerequisites, owner isolation, and in-place hall upgrades");
    return 0;
}
