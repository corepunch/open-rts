#include "engine.h"
#include "info.h"
#include "rustfront.h"

/* Rustfront's three factions for the computer player (play/p_ai.c, g_ai.c). Product ids are
 * catalog ui_ids, which are the actor types (p_prod.c). Each plays to its strengths, as
 * StarCraft's races do: the same economy, a different army and a different tempo. */

/* Directorate: a C&C build. Power, a barracks and riflemen to hold the base, the refinery (it
 * brings a harvester) and a second harvester, the War Factory, two gun turrets, then armour;
 * Mastodons once it is rich. Attacks once it matches what it has seen of the enemy. */
static const AiStep directorate_opening[] = {
    {MT_POWER, 1}, {MT_BARRACKS, 1}, {MT_RIFLEMAN, 3}, {MT_REFINERY, 1}, {MT_RIFLEMAN, 5},
    {MT_FACTORY, 1}, {MT_HARVESTER, 2}, {MT_GUN_TURRET, 1}, {MT_TANK, 2}, {MT_ROCKET, 3},
    {MT_GUN_TURRET, 2}, {MT_APC, 1}, {MT_TANK, 4}, {MT_ARTILLERY, 1}, {MT_MASTODON, 1},
    {MT_SENTRY_TRUCK, 1}, {MT_PHANTOM, 1}, {MT_UPLINK, 1}, {MT_HAMMER, 1}, {MT_KESTREL, 2}, {MT_CRANE, 1},
};
/* Scrap Clans: a rush. Raiders and trikes early, gun trucks soon after, frequent waves that
 * trade freely; rocket rigs and crawlers to crack a base. */
static const AiStep clans_opening[] = {
    {MT_CL_GENERATOR, 1}, {MT_CL_CAMP, 1}, {MT_CL_RAIDER, 6}, {MT_CL_SMELTER, 1}, {MT_CL_TORCHER, 2},
    {MT_CL_YARD, 1}, {MT_CL_TRIKE, 3}, {MT_CL_HAULER, 2}, {MT_CL_TRUCK, 2}, {MT_CL_TOWER, 1},
    {MT_CL_RAIDER, 10}, {MT_CL_SAPPER, 2}, {MT_CL_CRUSHER, 1}, {MT_CL_CRAWLER, 2}, {MT_CL_RIG, 1},
    {MT_CL_DRILL, 1}, {MT_CL_SHOP, 1}, {MT_CL_WRECKER, 2}, {MT_CL_BUZZARD, 2},
};
/* Lumen Concord: a fortress. Beam towers early, few units, each one strong; attacks late and
 * only with a clear edge, and falls back before it is traded away. */
static const AiStep concord_opening[] = {
    {MT_LC_FUSION, 1}, {MT_LC_ACADEMY, 1}, {MT_LC_WARDEN, 2}, {MT_LC_EXTRACTOR, 1}, {MT_LC_TOWER, 1},
    {MT_LC_FOUNDRY, 1}, {MT_LC_HARVESTER, 2}, {MT_LC_GLAIVE, 1}, {MT_LC_LANCER, 2}, {MT_LC_TOWER, 2},
    {MT_LC_SKIMMER, 1}, {MT_LC_CANTOR, 1}, {MT_LC_GLAIVE, 2}, {MT_LC_STRIDER, 1}, {MT_LC_PRISM, 1},
    {MT_LC_SANCTUM, 1}, {MT_LC_AEGIS, 1}, {MT_LC_SERAPH, 2},
};
#define OPENING(steps) .opening = steps, .opening_count = (int)(sizeof(steps) / sizeof(*steps))

static const faction_t factions[RF_FACTION_COUNT] = {
    [RF_DIRECTORATE] = {.name = "Directorate", OPENING(directorate_opening),
        .wave_interval_ms = 75000, .wave_min_size = 6, .wave_max_size = 16,
        .doctrine = {.workers = 2, .defenses = 2, .army_cap = 28, .attack_ratio = 120, .retreat_ratio = 55,
                     .roster = {{MT_HARVESTER, 0}, {MT_GUN_TURRET, 0}, {MT_RIFLEMAN, 25}, {MT_ROCKET, 20},
                                {MT_TANK, 28}, {MT_APC, 7}, {MT_ARTILLERY, 8}, {MT_SCOUT, 4}, {MT_MASTODON, 8},
                                {MT_HAMMER, 6}, {MT_PHANTOM, 10}, {MT_KESTREL, 7}},
                     .roster_count = 12}},
    [RF_CLANS] = {.name = "Scrap Clans", OPENING(clans_opening),
        .wave_interval_ms = 60000, .wave_min_size = 14, .wave_max_size = 28,
        .doctrine = {.workers = 2, .defenses = 1, .army_cap = 36, .attack_ratio = 130, .retreat_ratio = 55,
                     .roster = {{MT_CL_HAULER, 0}, {MT_CL_TOWER, 0}, {MT_CL_RAIDER, 35}, {MT_CL_TORCHER, 15},
                                {MT_CL_TRIKE, 15}, {MT_CL_TRUCK, 15}, {MT_CL_CRAWLER, 12}, {MT_CL_RIG, 8},
                                {MT_CL_WRECKER, 8}, {MT_CL_SAPPER, 4}, {MT_CL_CRUSHER, 8}, {MT_CL_BUZZARD, 7}},
                     .roster_count = 12}},
    [RF_CONCORD] = {.name = "Lumen Concord", OPENING(concord_opening),
        .wave_interval_ms = 110000, .wave_min_size = 4, .wave_max_size = 10,
        .doctrine = {.workers = 2, .defenses = 2, .army_cap = 18, .attack_ratio = 140, .retreat_ratio = 60,
                     .roster = {{MT_LC_HARVESTER, 0}, {MT_LC_TOWER, 0}, {MT_LC_WARDEN, 25}, {MT_LC_LANCER, 20},
                                {MT_LC_SKIMMER, 10}, {MT_LC_GLAIVE, 30}, {MT_LC_PRISM, 15},
                                {MT_LC_AEGIS, 8}, {MT_LC_STRIDER, 12}, {MT_LC_SERAPH, 8}, {MT_LC_CANTOR, 4}},
                     .roster_count = 11}},
};

/* A side plays the faction of its headquarters, once it has one to build from. */
static int faction_of(const level_t *map, int owner) {
    (void)map;
    for (int f = 0; f < RF_FACTION_COUNT; ++f)
        if (G_ModelHasActorType(NULL, owner, rf_factions[f].role[RF_ROLE_HQ])) return f;
    return -1;
}

const ruleset_t g_ruleset = {
    .game = "rustfront",
    .policy = { .input = INPUT_LEFT_SELECT_ORDER, .sight = SIGHT_RADIAL, .memory = MEMORY_STRUCTURES },
    .factions = factions, .faction_count = RF_FACTION_COUNT, .faction_of = faction_of,
};
