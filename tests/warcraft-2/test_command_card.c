#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

/* The command card follows the tech tree: each button appears when the
 * buildings and research it needs exist, and not before (Wargus
 * buttons.lua and upgrade.lua dependencies). */

#define CHECK(c) RTS_CHECK(c, "Warcraft II command card", #c)

static menu_t *menu;
static mobjlist_t units;
static fvec2_t home;
static bool orc;

/* The orc twin of a human type is the next id. */
static uint16_t side(uint16_t human) { return (uint16_t)(human + (orc ? 1 : 0)); }

static mobj_t *spawn(uint16_t type, float dx, float dy) {
    fvec2_t at = fvec2_add(home, (fvec2_t){dx, dy});
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(fixed2_from_fvec2(at), 0), type);
    if (!unit) return NULL;
    unit->owner = (uint8_t)consoleplayer;
    unit->team = (uint8_t)consoleplayer;
    unit->allegiance = ALLEGIANCE_PLAYER;
    return unit;
}

static void relist(void) {
    P_FreeMobjList(&units);
    units = P_ListMobjs();
    hudview.units = units.items;
    hudview.unit_count = units.count;
}

static void show(mobj_t *unit) {
    relist();
    for (int i = 0; i < units.count; ++i) P_MobjSetSelected(units.items[i], false);
    P_MobjSetSelected(unit, true);
    menu->refresh(menu);
}

static menuitem_t *button(const char *tip) {
    for (int i = 0; i < menu->numitems; ++i) {
        menuitem_t *it = &menu->items[i];
        if (it->kind == MI_BUTTON && it->visible && it->tooltip && !strcmp(it->tooltip, tip)) return it;
    }
    return NULL;
}

static const char *label(uint16_t type) { return mobjinfo[type].label; }

static bool has(const char *tip) { return button(tip) != NULL; }

/* Open a build page, look for a structure's button, close the page. */
static bool on_page(mobj_t *worker, const char *page, const char *tip) {
    show(worker);
    menuitem_t *open = button(page);
    if (!open) return false;
    open->routine(menu, open, MA_ACTIVATE);
    menu->refresh(menu);
    bool found = has(tip);
    menuitem_t *cancel = button("Cancel");
    if (cancel) cancel->routine(menu, cancel, MA_ACTIVATE);
    menu->refresh(menu);
    return found;
}

static bool basic(mobj_t *worker, const char *tip) { return on_page(worker, "Build basic structure", tip); }
static bool advanced(mobj_t *worker, const char *tip) { return on_page(worker, "Build advanced structure", tip); }

static void remove_unit(mobj_t *unit) {
    P_RemoveMobj(unit);
    P_RunThinkers();
    relist();
}

/* Clear the player's map units so every requirement comes from the test. */
static void clear_player(void) {
    relist();
    bool placed = false;
    for (int i = 0; i < units.count; ++i) {
        mobj_t *unit = units.items[i];
        if (unit->owner != consoleplayer) continue;
        if (!placed) home = fixed3_xy_to_fvec2(unit->core.position), placed = true;
        P_RemoveMobj(unit);
    }
    P_RunThinkers();
    level.w2_research[consoleplayer] = 0;
    for (int type = 0; type < RTS_MAX_UPGRADE_TYPES; ++type) level.upgrades[type][consoleplayer].weapon = level.upgrades[type][consoleplayer].armor = 0;
    relist();
}

/* A worker's card, then each build page opening as the town grows. */
static int test_worker(void) {
    clear_player();
    mobj_t *worker = spawn(side(MT_PEASANT), 0, 0);
    mobj_t *hall = spawn(side(MT_TOWN_HALL), 4, 0);
    CHECK(worker && hall);
    show(worker);
    CHECK(has("Move") && has("Stop") && has("Repair") && has("Harvest") && has("Build basic structure"));
    CHECK(!has("Return goods") && !has("Patrol") && !has("Stand ground"));
    CHECK(!has("Build advanced structure"));
    worker->harvest.cargo = 100;
    worker->harvest.resource_type = 0;
    show(worker);
    CHECK(has("Return goods"));
    worker->harvest.cargo = 0;

    const char *farm = orc ? "Build pig farm" : "Build farm";
    CHECK(basic(worker, farm) && basic(worker, "Build barracks") && basic(worker, "Build lumber mill"));
    CHECK(basic(worker, "Build blacksmith") && basic(worker, "Build tower"));
    CHECK(basic(worker, orc ? "Build great hall" : "Build town hall"));

    /* A lumber mill opens the advanced page with just the shipyard. */
    mobj_t *mill = spawn(side(MT_ELVEN_LUMBER_MILL), -4, 0);
    CHECK(mill);
    show(worker);
    CHECK(has("Build advanced structure"));
    CHECK(advanced(worker, "Build shipyard"));
    CHECK(!advanced(worker, "Build foundry") && !advanced(worker, "Build refinery"));
    CHECK(!advanced(worker, orc ? "Build ogre mound" : "Build stables"));
    CHECK(!advanced(worker, orc ? "Build alchemist" : "Build inventor"));
    CHECK(!advanced(worker, orc ? "Build temple" : "Build mage tower"));
    CHECK(!advanced(worker, orc ? "Build altar of storms" : "Build church"));
    CHECK(!advanced(worker, orc ? "Build dragon roost" : "Build gryphon aviary"));

    /* A shipyard adds the foundry and refinery. */
    mobj_t *yard = spawn(side(MT_HUMAN_SHIPYARD), 0, 4);
    CHECK(yard);
    CHECK(advanced(worker, "Build foundry") && advanced(worker, "Build refinery"));

    /* A keep (or stronghold) adds the inventor and the cavalry building. */
    CHECK(W2_TransformUnit(hall, side(MT_KEEP)));
    CHECK(advanced(worker, orc ? "Build ogre mound" : "Build stables"));
    CHECK(advanced(worker, orc ? "Build alchemist" : "Build inventor"));
    CHECK(!advanced(worker, orc ? "Build temple" : "Build mage tower"));

    /* A castle (or fortress) adds the rest; the keep's buildings stay. */
    CHECK(W2_TransformUnit(hall, side(MT_CASTLE)));
    CHECK(advanced(worker, orc ? "Build temple" : "Build mage tower"));
    CHECK(advanced(worker, orc ? "Build altar of storms" : "Build church"));
    CHECK(advanced(worker, orc ? "Build dragon roost" : "Build gryphon aviary"));
    CHECK(advanced(worker, orc ? "Build ogre mound" : "Build stables"));

    /* Without the mill the advanced page still opens for the castle. */
    remove_unit(mill);
    show(worker);
    CHECK(has("Build advanced structure"));
    return 0;
}

/* The construction order itself refuses what the card hides. */
static int test_construct_gate(void) {
    clear_player();
    mobj_t *worker = spawn(side(MT_PEASANT), 0, 0);
    CHECK(worker);
    int *stock = level.player_resources[consoleplayer];
    stock[0] = stock[1] = stock[2] = 100000;
    ivec2_t at = fvec2_cell(home), cell = {-1, -1};
    uint16_t roost = side(MT_GRYPHON_AVIARY);
    for (int dy = -6; dy <= 6 && cell.x < 0; ++dy)
        for (int dx = -6; dx <= 6 && cell.x < 0; ++dx)
            if (W2_CanPlace(roost, (ivec2_t){at.x + dx, at.y + dy}, worker)) cell = (ivec2_t){at.x + dx, at.y + dy};
    CHECK(cell.x >= 0);
    CHECK(!W2_SiteAllowed(consoleplayer, worker->type_id, roost));
    CHECK(!W2_ConstructOrder(worker, roost, cell));
    CHECK(worker->w2.build_phase == W2_BUILD_NONE);
    /* The other side's buildings are never this worker's to raise. */
    CHECK(!W2_SiteAllowed(consoleplayer, worker->type_id, orc ? MT_FARM : MT_PIG_FARM));
    CHECK(W2_SiteAllowed(consoleplayer, worker->type_id, side(MT_FARM)));
    mobj_t *castle = spawn(side(MT_CASTLE), 8, 0);
    CHECK(castle);
    CHECK(W2_SiteAllowed(consoleplayer, worker->type_id, roost));
    CHECK(W2_ConstructOrder(worker, roost, cell));
    W2_InterruptBuild(worker);
    return 0;
}

/* The barracks offers each soldier once its buildings stand. */
static int test_barracks(void) {
    clear_player();
    mobj_t *barracks = spawn(side(MT_HUMAN_BARRACKS), 0, 0);
    CHECK(barracks);
    const char *soldier = orc ? "Grunt" : "Footman", *archer = orc ? "Axethrower" : "Archer";
    const char *siege = orc ? "Catapult" : "Ballista", *rider = orc ? "Ogre" : "Knight";
    show(barracks);
    CHECK(has(soldier) && !has(archer) && !has(siege) && !has(rider));
    mobj_t *mill = spawn(side(MT_ELVEN_LUMBER_MILL), 4, 0);
    show(barracks);
    CHECK(mill && has(archer) && !has(siege));
    mobj_t *smith = spawn(side(MT_HUMAN_BLACKSMITH), -4, 0);
    show(barracks);
    CHECK(smith && has(siege) && !has(rider));
    mobj_t *stables = spawn(side(MT_STABLES), 0, 4);
    show(barracks);
    CHECK(stables && has(rider));
    remove_unit(smith);
    show(barracks);
    CHECK(!has(siege) && !has(rider) && has(archer));
    return 0;
}

/* The hall's upgrade appears with its prerequisites and hides while busy. */
static int test_hall(void) {
    clear_player();
    mobj_t *hall = spawn(side(MT_TOWN_HALL), 0, 0);
    CHECK(hall);
    const char *keep = orc ? "Upgrade to stronghold" : "Upgrade to keep";
    const char *castle = orc ? "Upgrade to fortress" : "Upgrade to castle";
    show(hall);
    CHECK(has(orc ? "Peon" : "Peasant") && !has(keep));
    mobj_t *barracks = spawn(side(MT_HUMAN_BARRACKS), 4, 0);
    show(hall);
    CHECK(barracks && has(keep));
    int *stock = level.player_resources[consoleplayer];
    stock[0] = stock[1] = stock[2] = 100000;
    CHECK(G_BuildOrder(hall, orc ? 4 : 3));
    show(hall);
    CHECK(!has(keep) && has("Cancel current production"));
    P_FreeMobjProduction(hall);
    CHECK(W2_TransformUnit(hall, side(MT_KEEP)));
    show(hall);
    CHECK(!has(keep) && !has(castle));
    spawn(side(MT_STABLES), -4, 0);
    spawn(side(MT_HUMAN_BLACKSMITH), 0, 4);
    show(hall);
    CHECK(!has(castle));
    spawn(side(MT_ELVEN_LUMBER_MILL), 4, 4);
    show(hall);
    CHECK(has(castle));
    CHECK(W2_TransformUnit(hall, side(MT_CASTLE)));
    show(hall);
    CHECK(!has(castle) && has(orc ? "Peon" : "Peasant"));
    return 0;
}

/* Research tiers and the elite research behind the keep. */
static int test_research(void) {
    clear_player();
    mobj_t *mill = spawn(side(MT_ELVEN_LUMBER_MILL), 0, 0);
    CHECK(mill);
    int arrow1 = orc ? W2_UPGRADE_THROWING_AXE1 : W2_UPGRADE_ARROW1;
    int arrow2 = orc ? W2_UPGRADE_THROWING_AXE2 : W2_UPGRADE_ARROW2;
    const char *arrows = orc ? "Upgrade throwing axe" : "Upgrade arrows";
    const char *elite = W2_Upgrade(orc ? W2_UPGRADE_BERSERKER : W2_UPGRADE_RANGER)->label;
    const char *scouting = W2_Upgrade(orc ? W2_UPGRADE_BERSERKER_SCOUTING : W2_UPGRADE_RANGER_SCOUTING)->label;
    show(mill);
    menuitem_t *tier = button(arrows);
    CHECK(tier && tier->look[MS_NORMAL].cell == W2_Upgrade(arrow1)->icon);
    CHECK(!has(elite) && !has(scouting));
    W2_ApplyUpgrade(consoleplayer, arrow1);
    show(mill);
    tier = button(arrows);
    CHECK(tier && tier->look[MS_NORMAL].cell == W2_Upgrade(arrow2)->icon);
    W2_ApplyUpgrade(consoleplayer, arrow2);
    show(mill);
    CHECK(!has(arrows));
    mobj_t *keep = spawn(side(MT_KEEP), 4, 0);
    show(mill);
    CHECK(keep && has(elite) && !has(scouting));
    W2_ApplyUpgrade(consoleplayer, orc ? W2_UPGRADE_BERSERKER : W2_UPGRADE_RANGER);
    show(mill);
    CHECK(!has(elite) && has(scouting));
    /* The barracks now trains the elite unit in the archer's place. */
    mobj_t *barracks = spawn(side(MT_HUMAN_BARRACKS), -4, 0);
    show(barracks);
    CHECK(barracks && has(label(side(MT_RANGER))) && !has(orc ? "Axethrower" : "Archer"));
    CHECK(button(label(side(MT_RANGER)))->look[MS_NORMAL].cell == (orc ? 7 : 6));
    /* And what it trains leaves as the elite unit. */
    spawn(side(MT_FARM), 0, -4);
    int *stock = level.player_resources[consoleplayer];
    stock[0] = stock[1] = stock[2] = 100000;
    CHECK(G_BuildOrder(barracks, orc ? 6 : 5));
    barracks->production->time_left_ms = 1;
    CHECK(G_ProductionTicker(RTS_TICK_MS));
    relist();
    int promoted = 0, plain = 0;
    for (int i = 0; i < units.count; ++i) {
        promoted += units.items[i]->type_id == side(MT_RANGER);
        plain += units.items[i]->type_id == side(MT_ARCHER) && units.items[i]->owner == consoleplayer;
    }
    CHECK(promoted == 1 && plain == 0);
    return 0;
}

/* Attack and Stop show the weapon and armor tier research has reached. */
static int test_tier_icons(void) {
    clear_player();
    mobj_t *soldier = spawn(side(MT_FOOTMAN), 0, 0), *archer = spawn(side(MT_ARCHER), 1, 0);
    CHECK(soldier && archer);
    show(soldier);
    CHECK(button("Attack")->look[MS_NORMAL].cell == (orc ? 119 : 116));
    CHECK(button("Stop")->look[MS_NORMAL].cell == (orc ? 167 : 164));
    W2_ApplyUpgrade(consoleplayer, orc ? W2_UPGRADE_AXE1 : W2_UPGRADE_SWORD1);
    W2_ApplyUpgrade(consoleplayer, orc ? W2_UPGRADE_ORC_SHIELD1 : W2_UPGRADE_HUMAN_SHIELD1);
    W2_ApplyUpgrade(consoleplayer, orc ? W2_UPGRADE_ORC_SHIELD2 : W2_UPGRADE_HUMAN_SHIELD2);
    show(soldier);
    CHECK(button("Attack")->look[MS_NORMAL].cell == (orc ? 120 : 117));
    CHECK(button("Stop")->look[MS_NORMAL].cell == (orc ? 169 : 166));
    show(archer);
    CHECK(button("Attack")->look[MS_NORMAL].cell == (orc ? 127 : 124));
    W2_ApplyUpgrade(consoleplayer, orc ? W2_UPGRADE_THROWING_AXE1 : W2_UPGRADE_ARROW1);
    show(archer);
    CHECK(button("Attack")->look[MS_NORMAL].cell == (orc ? 128 : 125));
    mobj_t *ship = spawn(side(MT_HUMAN_DESTROYER), 8, 8);
    show(ship);
    CHECK(button("Move")->look[MS_NORMAL].cell == (orc ? 156 : 157));
    CHECK(button("Attack")->look[MS_NORMAL].cell == (orc ? 147 : 144));
    CHECK(button("Stop")->look[MS_NORMAL].cell == (orc ? 150 : 153));
    return 0;
}

/* Casters show the spell page only for spells they can cast. */
static int test_spells(void) {
    clear_player();
    uint16_t caster_type = orc ? MT_DEATH_KNIGHT : MT_MAGE;
    mobj_t *caster = spawn(caster_type, 0, 0);
    CHECK(caster);
    w2_spell_id_t innate = orc ? W2_SPELL_DEATH_COIL : W2_SPELL_FIREBALL;
    w2_spell_id_t researched = orc ? W2_SPELL_HASTE : W2_SPELL_SLOW;
    show(caster);
    menuitem_t *spells = button("Spells");
    CHECK(spells && has("Patrol") && has("Stand ground") && has("Attack"));
    spells->routine(menu, spells, MA_ACTIVATE);
    menu->refresh(menu);
    CHECK(has(w2_spells[innate].label) && !has(w2_spells[researched].label));
    W2_ApplyUpgrade(consoleplayer, w2_spells[researched].research);
    menu->refresh(menu);
    CHECK(has(w2_spells[innate].label) && has(w2_spells[researched].label));
    menuitem_t *cancel = button("Cancel");
    CHECK(cancel);
    cancel->routine(menu, cancel, MA_ACTIVATE);
    /* Knights have nothing to cast until the paladin and its research. */
    mobj_t *knight = spawn(side(MT_KNIGHT), 2, 0);
    show(knight);
    CHECK(knight && !has("Spells"));
    W2_ApplyUpgrade(consoleplayer, orc ? W2_UPGRADE_OGRE_MAGE : W2_UPGRADE_PALADIN);
    CHECK(knight->type_id == side(MT_PALADIN));
    show(knight);
    CHECK(has("Spells"));
    return 0;
}

/* Towers arm in place; ships and transports carry their own commands. */
static int test_towers_and_ships(void) {
    clear_player();
    mobj_t *tower = spawn(side(MT_HUMAN_WATCH_TOWER), 0, 0);
    CHECK(tower);
    const char *guard = label(side(MT_HUMAN_GUARD_TOWER));
    const char *cannon = label(side(MT_HUMAN_CANNON_TOWER));
    show(tower);
    CHECK(!has(guard) && !has(cannon));
    spawn(side(MT_ELVEN_LUMBER_MILL), 4, 0);
    show(tower);
    CHECK(has(guard) && !has(cannon));
    spawn(side(MT_HUMAN_BLACKSMITH), -4, 0);
    show(tower);
    CHECK(has(guard) && has(cannon));

    mobj_t *yard = spawn(side(MT_HUMAN_SHIPYARD), 0, 4);
    const char *tanker = label(side(MT_HUMAN_OIL_TANKER));
    const char *transport = label(side(MT_HUMAN_TRANSPORT));
    show(yard);
    CHECK(has(tanker) && !has(transport));
    spawn(side(MT_HUMAN_FOUNDRY), 4, 4);
    show(yard);
    CHECK(has(transport));

    mobj_t *ship = spawn(side(MT_HUMAN_OIL_TANKER), 8, 8);
    show(ship);
    CHECK(has("Harvest oil") && has("Build oil platform") && !has("Return oil"));
    ship->harvest.cargo = 100;
    show(ship);
    CHECK(has("Return oil"));
    mobj_t *carrier = spawn(side(MT_HUMAN_TRANSPORT), 8, 10);
    show(carrier);
    CHECK(has("Unload") && !has("Board transport"));
    mobj_t *soldier = spawn(side(MT_FOOTMAN), 2, 2);
    show(soldier);
    CHECK(has("Board transport") && has("Patrol") && !has("Unload"));
    return 0;
}

static int run(bool orc_side) {
    orc = orc_side;
    G_InitGame();
    P_InitThinkers();
    CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level));
    CHECK(P_LoadThings(NULL) > 0);
    ((w2_pud_t *)level.native_data)->sides[consoleplayer] = orc ? 1 : 0;
    app_t app = {.win = {640, 480}, .cell = {32, 32}};
    V_AllocScreen(app.win.w, app.win.h);
    menu = G_InitHUD(&app, "data/WAR2");
    CHECK(menu && menu->refresh);
    units = (mobjlist_t){0};
    RTS_RUN(test_worker());
    RTS_RUN(test_construct_gate());
    RTS_RUN(test_barracks());
    RTS_RUN(test_hall());
    RTS_RUN(test_research());
    RTS_RUN(test_tier_icons());
    RTS_RUN(test_spells());
    RTS_RUN(test_towers_and_ships());
    G_ShutdownHUD();
    V_FreeScreen();
    P_FreeMobjList(&units);
    P_FreeLevel(&level);
    return 0;
}

int main(void) {
    RTS_RUN(run(false));
    RTS_RUN(run(true));
    puts("PASS: Warcraft II command card follows the tech tree: build pages, training, hall upgrades, research tiers, spells, towers and ships");
    return 0;
}
