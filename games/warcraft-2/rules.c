#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

/* Warcraft II's ruleset: the two sides' computer players. Wargus
 * land_attack.lua's opening in goal-ladder form: workers, farms and a
 * barracks, soldiers, a mill and smithy with the first research, a tower, the
 * keep and the cavalry building. The race doctrine runs the game from there.
 * Product ids are catalog ui_ids: 1..10 are units, then W2_UI_*. */

static const AiStep human_opening[] = {
    { W2_UI_TOWN_HALL, 1 }, { 3, 5 }, { W2_UI_FARM, 2 }, { W2_UI_HUMAN_BARRACKS, 1 },
    { 3, 8 }, { 1, 3 }, { W2_UI_ELVEN_LUMBER_MILL, 1 }, { 3, 12 }, { 5, 2 }, { W2_UI_HUMAN_BLACKSMITH, 1 },
    { W2_UI_SWORD1, 1 }, { W2_UI_HUMAN_WATCH_TOWER, 1 }, { 3, 15 }, { W2_UI_HUMAN_SHIELD1, 1 },
    { W2_UI_KEEP, 1 }, { W2_UI_STABLES, 1 }, { W2_UI_HUMAN_BARRACKS, 2 },
};
/* The orc twin of each human line: the next id, except the three upgrades. */
static const AiStep orc_opening[] = {
    { W2_UI_GREAT_HALL, 1 }, { 4, 5 }, { W2_UI_PIG_FARM, 2 }, { W2_UI_ORC_BARRACKS, 1 },
    { 4, 8 }, { 2, 3 }, { W2_UI_TROLL_LUMBER_MILL, 1 }, { 4, 12 }, { 6, 2 }, { W2_UI_ORC_BLACKSMITH, 1 },
    { W2_UI_AXE1, 1 }, { W2_UI_ORC_WATCH_TOWER, 1 }, { 4, 15 }, { W2_UI_ORC_SHIELD1, 1 },
    { W2_UI_STRONGHOLD, 1 }, { W2_UI_OGRE_MOUND, 1 }, { W2_UI_ORC_BARRACKS, 2 },
};
/* Wargus sea_attack.lua: a shipyard and oil before the first destroyers,
 * then the foundry that transports and battleships need. */
static const AiStep human_sea_opening[] = {
    { W2_UI_TOWN_HALL, 1 }, { 3, 5 }, { W2_UI_FARM, 2 }, { W2_UI_HUMAN_BARRACKS, 1 },
    { 3, 9 }, { W2_UI_ELVEN_LUMBER_MILL, 1 }, { 3, 12 }, { 1, 2 }, { W2_UI_HUMAN_SHIPYARD, 1 },
    { 200 + MT_HUMAN_OIL_TANKER, 1 }, { 200 + MT_HUMAN_OIL_PLATFORM, 1 }, { W2_UI_FARM, 3 }, { 3, 15 },
    { 200 + MT_HUMAN_OIL_TANKER, 2 }, { W2_UI_HUMAN_BLACKSMITH, 1 }, { W2_UI_HUMAN_FOUNDRY, 1 },
    { 200 + MT_HUMAN_TRANSPORT, 1 }, { W2_UI_KEEP, 1 }, { W2_UI_HUMAN_REFINERY, 1 },
};
static const AiStep orc_sea_opening[] = {
    { W2_UI_GREAT_HALL, 1 }, { 4, 5 }, { W2_UI_PIG_FARM, 2 }, { W2_UI_ORC_BARRACKS, 1 },
    { 4, 9 }, { W2_UI_TROLL_LUMBER_MILL, 1 }, { 4, 12 }, { 2, 2 }, { W2_UI_ORC_SHIPYARD, 1 },
    { 200 + MT_ORC_OIL_TANKER, 1 }, { 200 + MT_ORC_OIL_PLATFORM, 1 }, { W2_UI_PIG_FARM, 3 }, { 4, 15 },
    { 200 + MT_ORC_OIL_TANKER, 2 }, { W2_UI_ORC_BLACKSMITH, 1 }, { W2_UI_ORC_FOUNDRY, 1 },
    { 200 + MT_ORC_TRANSPORT, 1 }, { W2_UI_STRONGHOLD, 1 }, { W2_UI_ORC_REFINERY, 1 },
};
/* Wargus air_attack.lua: soldiers and towers at home, then the aviary
 * behind a castle. */
static const AiStep human_air_opening[] = {
    { W2_UI_TOWN_HALL, 1 }, { 3, 5 }, { W2_UI_FARM, 2 }, { W2_UI_HUMAN_BARRACKS, 1 },
    { 3, 9 }, { 1, 2 }, { W2_UI_ELVEN_LUMBER_MILL, 1 }, { 3, 12 }, { W2_UI_HUMAN_BLACKSMITH, 1 },
    { 3, 15 }, { W2_UI_KEEP, 1 }, { W2_UI_STABLES, 1 }, { W2_UI_CASTLE, 1 }, { W2_UI_GRYPHON_AVIARY, 1 },
};
static const AiStep orc_air_opening[] = {
    { W2_UI_GREAT_HALL, 1 }, { 4, 5 }, { W2_UI_PIG_FARM, 2 }, { W2_UI_ORC_BARRACKS, 1 },
    { 4, 9 }, { 2, 2 }, { W2_UI_TROLL_LUMBER_MILL, 1 }, { 4, 12 }, { W2_UI_ORC_BLACKSMITH, 1 },
    { 4, 15 }, { W2_UI_STRONGHOLD, 1 }, { W2_UI_OGRE_MOUND, 1 }, { W2_UI_FORTRESS, 1 }, { W2_UI_DRAGON_ROOST, 1 },
};

#define OPENING(steps) .opening = steps, .opening_count = (int)(sizeof(steps) / sizeof(*steps))

/* The two sides field near-mirror units, so the doctrines carry the
 * difference. Humans: ranged archers and knights that become healing
 * paladins, mages, towers, patient attacks that pull back to heal. Orcs:
 * grunts and bloodlusting ogres, death knights, earlier and bolder
 * attacks that fight it out. Farms feed four. Towers rise as watch
 * towers and are armed in place. */
/* A start location with nothing else of its player's on the map gets a hall
 * and one worker below it (Stratagus' one-worker start). Cells. */
static const startunit_t human_start[] = { { MT_TOWN_HALL, 1, {0, 0}, {0, 0} }, { MT_PEASANT, 1, {1, 4}, {0, 0} } };
static const startunit_t orc_start[] = { { MT_GREAT_HALL, 1, {0, 0}, {0, 0} }, { MT_PEON, 1, {1, 4}, {0, 0} } };
#define START(units) .start_units = units, .start_unit_count = (int)(sizeof(units) / sizeof(*units))

/* Sea and air scripts of the PUD's AIPL (variant_of): the same race with
 * its navy or its flyers weighted in; the sea rosters add the destroyers,
 * battleships and transports (ids a twin apart) and the soldiers they carry. */
#define VARIANT(steps, ...) { .opening = steps, .opening_count = (int)(sizeof(steps) / sizeof(*steps)), __VA_ARGS__ }
#define ROSTER(r) .roster = r, .roster_count = (int)(sizeof(r) / sizeof(*r))
static const AiChoice human_sea_roster[] = { {200 + MT_HUMAN_DESTROYER, 35}, {200 + MT_BATTLESHIP, 25},
    {200 + MT_HUMAN_TRANSPORT, 6}, {200 + MT_GRYPHON_RIDER, 15} };
static const AiChoice orc_sea_roster[] = { {200 + MT_ORC_DESTROYER, 35}, {200 + MT_BATTLESHIP + 1, 25},
    {200 + MT_ORC_TRANSPORT, 6}, {200 + MT_DRAGON, 15} };
static const AiChoice human_air_roster[] = { {200 + MT_GRYPHON_RIDER, 60} };
static const AiChoice orc_air_roster[] = { {200 + MT_DRAGON, 60} };
static const factionvariant_t human_variants[] = {
    [W2_VARIANT_SEA] = VARIANT(human_sea_opening, ROSTER(human_sea_roster)),
    [W2_VARIANT_AIR] = VARIANT(human_air_opening, ROSTER(human_air_roster), .defenses = 2),
};
static const factionvariant_t orc_variants[] = {
    [W2_VARIANT_SEA] = VARIANT(orc_sea_opening, ROSTER(orc_sea_roster)),
    [W2_VARIANT_AIR] = VARIANT(orc_air_opening, ROSTER(orc_air_roster), .defenses = 2),
};

static const faction_t factions[2] = {
    [0] = { .name = "Human", START(human_start), OPENING(human_opening),
        .wave_interval_ms = 60000, .wave_min_size = 6, .wave_max_size = 16,
        .variants = human_variants, .variant_count = 2,
        .doctrine = {
            .workers = 15, .supply_buffer = 3, .defenses = 1, .research = 50,
            .counter = 50, .attack_ratio = 120, .retreat_ratio = 55,
            .roster = { {3,0},{W2_UI_FARM,0},{1,40},{5,30},{9,25},{7,10},{200 + MT_MAGE,10},
                        {200 + MT_GRYPHON_RIDER,5},{200 + MT_HUMAN_GUARD_TOWER,0},
                        {200 + MT_HUMAN_CANNON_TOWER,0},{W2_UI_HUMAN_WATCH_TOWER,0} },
            .roster_count = 11 } },
    [1] = { .name = "Orc", START(orc_start), OPENING(orc_opening),
        .wave_interval_ms = 45000, .wave_min_size = 4, .wave_max_size = 16,
        .variants = orc_variants, .variant_count = 2,
        .doctrine = {
            .workers = 15, .supply_buffer = 3, .defenses = 1, .research = 40,
            .counter = 50, .attack_ratio = 85, .retreat_ratio = 35,
            .roster = { {4,0},{W2_UI_PIG_FARM,0},{2,45},{6,25},{10,30},{8,10},{200 + MT_DEATH_KNIGHT,8},
                        {200 + MT_DRAGON,5},{200 + MT_ORC_GUARD_TOWER,0},
                        {200 + MT_ORC_CANNON_TOWER,0},{W2_UI_ORC_WATCH_TOWER,0} },
            .roster_count = 11 } },
};

/* The PUD names each slot's side; a map without one tells by what the owner
 * already fields. */
static int faction_of(const level_t *map, int owner) {
    const w2_pud_t *pud = map ? map->native_data : NULL;
    if (pud && owner >= 0 && owner < 16) return pud->sides[owner] == 1;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *unit = (const mobj_t *)th;
        if (unit->owner != owner || unit->remove || unit->hp <= 0) continue;
        if (unit->type_id == MT_PEON || unit->type_id == MT_GREAT_HALL || unit->type_id == MT_GRUNT) return 1;
        if (unit->type_id == MT_PEASANT || unit->type_id == MT_TOWN_HALL || unit->type_id == MT_FOOTMAN) return 0;
    }
    return 0;
}

/* Which script plays this slot: a sea or air attack by the PUD's AIPL, or a
 * land attack; an island start without an enemy by land plays the sea game. */
static int variant_of(const level_t *map, int owner) {
    int script = W2_AiScript(map, owner);
    if (script == W2_AI_AIR) return W2_VARIANT_AIR;
    if (script == W2_AI_SEA || !W2_EnemyByLand(map, owner)) return W2_VARIANT_SEA;
    return -1;
}

/* What the catalog's building list cannot say: research goes one tier at a
 * time and the rest of a tree waits for its root. Tiers run through
 * REQ_UPGRADE on the line's id; Warcraft's spell and unit research needs a
 * keep or stronghold and the research it grows from. */
#define W2_UI(upgrade) ((upgrade) <= W2_UPGRADE_ORC_SHIELD2 ? (upgrade) + 10 : 100 + (upgrade))
#define TIER2(second, first) { W2_UI(second), { REQ_UPGRADE, (first), 1 } }
#define NEEDS_BUILDING(upgrade, type) { W2_UI(upgrade), { REQ_BUILDING, (type), 0 } }
#define NEEDS_UPGRADE(upgrade, root) { W2_UI(upgrade), { REQ_UPGRADE, (root), 1 } }
static const productreq_t requirements[] = {
    TIER2(W2_UPGRADE_SWORD2, W2_UPGRADE_SWORD1), TIER2(W2_UPGRADE_AXE2, W2_UPGRADE_AXE1),
    TIER2(W2_UPGRADE_ARROW2, W2_UPGRADE_ARROW1), TIER2(W2_UPGRADE_THROWING_AXE2, W2_UPGRADE_THROWING_AXE1),
    TIER2(W2_UPGRADE_HUMAN_SHIELD2, W2_UPGRADE_HUMAN_SHIELD1), TIER2(W2_UPGRADE_ORC_SHIELD2, W2_UPGRADE_ORC_SHIELD1),
    TIER2(W2_UPGRADE_BALLISTA2, W2_UPGRADE_BALLISTA1), TIER2(W2_UPGRADE_CATAPULT2, W2_UPGRADE_CATAPULT1),
    TIER2(W2_UPGRADE_HUMAN_CANNON2, W2_UPGRADE_HUMAN_CANNON1), TIER2(W2_UPGRADE_ORC_CANNON2, W2_UPGRADE_ORC_CANNON1),
    TIER2(W2_UPGRADE_HUMAN_SHIP_ARMOR2, W2_UPGRADE_HUMAN_SHIP_ARMOR1),
    TIER2(W2_UPGRADE_ORC_SHIP_ARMOR2, W2_UPGRADE_ORC_SHIP_ARMOR1),
    NEEDS_BUILDING(W2_UPGRADE_RANGER, MT_KEEP), NEEDS_BUILDING(W2_UPGRADE_LONGBOW, MT_KEEP),
    NEEDS_BUILDING(W2_UPGRADE_RANGER_SCOUTING, MT_KEEP), NEEDS_BUILDING(W2_UPGRADE_MARKSMANSHIP, MT_KEEP),
    NEEDS_UPGRADE(W2_UPGRADE_LONGBOW, W2_UPGRADE_RANGER), NEEDS_UPGRADE(W2_UPGRADE_RANGER_SCOUTING, W2_UPGRADE_RANGER),
    NEEDS_UPGRADE(W2_UPGRADE_MARKSMANSHIP, W2_UPGRADE_RANGER),
    NEEDS_BUILDING(W2_UPGRADE_BERSERKER, MT_STRONGHOLD), NEEDS_BUILDING(W2_UPGRADE_LIGHT_AXES, MT_STRONGHOLD),
    NEEDS_BUILDING(W2_UPGRADE_BERSERKER_SCOUTING, MT_STRONGHOLD), NEEDS_BUILDING(W2_UPGRADE_REGENERATION, MT_STRONGHOLD),
    NEEDS_UPGRADE(W2_UPGRADE_LIGHT_AXES, W2_UPGRADE_BERSERKER), NEEDS_UPGRADE(W2_UPGRADE_BERSERKER_SCOUTING, W2_UPGRADE_BERSERKER),
    NEEDS_UPGRADE(W2_UPGRADE_REGENERATION, W2_UPGRADE_BERSERKER),
    NEEDS_UPGRADE(W2_UPGRADE_HEALING, W2_UPGRADE_PALADIN), NEEDS_UPGRADE(W2_UPGRADE_EXORCISM, W2_UPGRADE_PALADIN),
    NEEDS_UPGRADE(W2_UPGRADE_BLOODLUST, W2_UPGRADE_OGRE_MAGE), NEEDS_UPGRADE(W2_UPGRADE_RUNES, W2_UPGRADE_OGRE_MAGE),
};
#undef W2_UI
#undef TIER2
#undef NEEDS_BUILDING
#undef NEEDS_UPGRADE

/* Gold is the catalog's cost; lumber and oil come from the stat tables. */
static void w2_product_costs(const StaticProductDefinition *product, int *out) {
    out[0] = product->cost;
    out[1] = W2_ProductLumber(product);
    out[2] = W2_ProductOil(product);
}

static int w2_upgrade_level(int owner, int upgrade) { return W2_UpgradeLevel(owner, W2_Upgrade(upgrade)); }

/* Farms and halls feed; the engine reads the rest from the actor. */
static const actorrole_t actors[W2_TYPE_COUNT + 1] = {
    [MT_FARM] = { .roles = AI_ROLE_SUPPLY }, [MT_PIG_FARM] = { .roles = AI_ROLE_SUPPLY },
    [MT_TOWN_HALL] = { .roles = AI_ROLE_SUPPLY }, [MT_GREAT_HALL] = { .roles = AI_ROLE_SUPPLY },
    [MT_KEEP] = { .roles = AI_ROLE_SUPPLY }, [MT_STRONGHOLD] = { .roles = AI_ROLE_SUPPLY },
    [MT_CASTLE] = { .roles = AI_ROLE_SUPPLY }, [MT_FORTRESS] = { .roles = AI_ROLE_SUPPLY },
    /* The site a guard or cannon tower is raised on. */
    [MT_HUMAN_WATCH_TOWER] = { .roles = AI_ROLE_DEFENSE }, [MT_ORC_WATCH_TOWER] = { .roles = AI_ROLE_DEFENSE },
};

/* Map overrides. The tables stay the authority: R_PatchSet remembers what a
 * patch replaced and the next level load puts it back. */
static bool patch_unit(const rulepatch_t *e) {
    if (e->row >= W2_TYPE_COUNT) return false;
    mobjinfo_t *unit = &mobjinfo[e->row + 1];
    int v = e->value;
    switch (e->field) {
    case W2_UNIT_SIGHT: return R_PatchSet(&unit->w2.sight, v);
    case W2_UNIT_HP: return R_PatchSet(&unit->spawnhealth, v);
    case W2_UNIT_BUILD_TIME: return R_PatchSet(&unit->w2.costs.time, v);
    case W2_UNIT_GOLD: return R_PatchSet(&unit->w2.costs.resources[0], v);
    case W2_UNIT_LUMBER: return R_PatchSet(&unit->w2.costs.resources[1], v);
    case W2_UNIT_OIL: return R_PatchSet(&unit->w2.costs.resources[2], v);
    case W2_UNIT_RANGE: return R_PatchSet(&unit->w2.attack_range, v);
    case W2_UNIT_ARMOR: return R_PatchSet(&unit->w2.armor, v);
    /* The most a hit does moves with its parts. */
    case W2_UNIT_BASIC:
        return R_PatchSet(&unit->damage, unit->damage + v - unit->w2.basic_damage) &&
               R_PatchSet(&unit->w2.basic_damage, v);
    case W2_UNIT_PIERCING:
        return R_PatchSet(&unit->damage, unit->damage + v - unit->w2.piercing_damage) &&
               R_PatchSet(&unit->w2.piercing_damage, v);
    case W2_UNIT_POINTS: return R_PatchSet(&unit->w2.points, v);
    }
    return false;
}

/* ALOW: what a map bans per player. Zero is allowed; a patch sets one. */
static int banned_unit[8][32], banned_upgrade[8][W2_UPGRADE_COUNT];

bool W2_Banned(int owner, const StaticProductDefinition *product) {
    if (!product || owner < 0 || owner >= 8) return false;
    if (product->product_class == RTS_PRODUCT_UPGRADE)
        return product->product_type > 0 && product->product_type < W2_UPGRADE_COUNT &&
               banned_upgrade[owner][product->product_type];
    int unit = product->product_type - 1; /* PUD unit ids start at the footman */
    return unit >= 0 && unit < 32 && banned_unit[owner][unit];
}

static bool patch_allow(const rulepatch_t *e) {
    int kind = e->field / 256, index = e->field % 256;
    if (e->row >= 8) return false;
    if (kind == W2_ALLOW_UNIT && index < 32) return R_PatchSet(&banned_unit[e->row][index], e->value);
    if (kind == W2_ALLOW_UPGRADE && index < W2_UPGRADE_COUNT) return R_PatchSet(&banned_upgrade[e->row][index], e->value);
    return false;
}

static bool patch_apply(const rulepatch_t *e) {
    if (e->table == W2_PATCH_ALLOW) return patch_allow(e);
    if (e->table == W2_PATCH_UNIT) return patch_unit(e);
    if (e->table != W2_PATCH_UPGRADE) return false;
    w2_upgrade_t *upgrade = W2_UpgradeRW(e->row);
    if (!upgrade) return false;
    switch (e->field) {
    case W2_UPGRADE_FIELD_TIME: return R_PatchSet(&upgrade->time, e->value);
    case W2_UPGRADE_FIELD_GOLD: return R_PatchSet(&upgrade->gold, e->value);
    case W2_UPGRADE_FIELD_LUMBER: return R_PatchSet(&upgrade->lumber, e->value);
    case W2_UPGRADE_FIELD_OIL: return R_PatchSet(&upgrade->oil, e->value);
    }
    return false;
}

/* Hit points, sight and damage reach the engine's actor types from the stats. */
static void patch_done(void) { w2_refill_actors(); w2_rebuild_product_costs(); }

static bool w2_prerequisite_met(int owner, int id) { return W2_OwnerHas(owner, (uint16_t)id); }

const ruleset_t g_ruleset = {
    .game = "warcraft-2",
    .policy = { .input = INPUT_RIGHT_CLICK_ORDERS, .sight = SIGHT_RADIAL, .select = SELECT_ANY,
                .f10 = F10_CONTROL_MENU, .turning = TURN_INSTANT },
    .factions = factions, .faction_count = 2, .faction_of = faction_of, .variant_of = variant_of,
    .actors = actors, .actor_count = W2_TYPE_COUNT + 1,
    .prerequisite_met = w2_prerequisite_met,
    .requirements = requirements, .requirement_count = (int)(sizeof(requirements) / sizeof(*requirements)),
    .upgrade_level = w2_upgrade_level, .product_costs = w2_product_costs,
    .patch_apply = patch_apply, .patch_done = patch_done,
};
