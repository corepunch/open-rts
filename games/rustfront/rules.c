#include "engine.h"
#include "info.h"

/* Rustfront's one faction, for the computer player (play/p_ai.c, g_ai.c). Product ids are
 * catalog ui_ids, which are the actor types (p_prod.c).
 *
 * The opening is a C&C build: power, a barracks and a few riflemen to hold the base, the
 * refinery (it brings a harvester) and a second harvester, the War Factory, two gun turrets,
 * then a first armoured group. After it the doctrine keeps two harvesters and two turrets per
 * base, masses a mixed army and attacks once it matches what it has seen of the enemy. */
static const AiStep opening[] = {
    {MT_POWER, 1}, {MT_BARRACKS, 1}, {MT_RIFLEMAN, 3}, {MT_REFINERY, 1}, {MT_RIFLEMAN, 5},
    {MT_FACTORY, 1}, {MT_HARVESTER, 2}, {MT_GUN_TURRET, 1}, {MT_TANK, 2}, {MT_ROCKET, 3},
    {MT_GUN_TURRET, 2}, {MT_APC, 1}, {MT_TANK, 4}, {MT_ARTILLERY, 1},
};

static const faction_t factions[1] = {
    [0] = {.name = "Rustfront",
           .opening = opening, .opening_count = (int)(sizeof(opening) / sizeof(*opening)),
           .wave_interval_ms = 75000, .wave_min_size = 6, .wave_max_size = 16,
           .doctrine = {.workers = 2, .defenses = 2, .army_cap = 28, .attack_ratio = 100, .retreat_ratio = 40,
                        .roster = {{MT_HARVESTER, 0}, {MT_GUN_TURRET, 0}, {MT_RIFLEMAN, 30}, {MT_ROCKET, 25},
                                   {MT_TANK, 30}, {MT_APC, 8}, {MT_ARTILLERY, 10}, {MT_SCOUT, 5}},
                        .roster_count = 8}},
};

/* A side plays the faction once it has a Command Center to build from. */
static int faction_of(const level_t *map, int owner) {
    (void)map;
    return G_ModelHasActorType(NULL, owner, MT_HQ) ? 0 : -1;
}

const ruleset_t g_ruleset = {
    .game = "rustfront",
    .policy = { .input = INPUT_LEFT_SELECT_ORDER, .sight = SIGHT_OPEN },
    .factions = factions, .faction_count = 1, .faction_of = faction_of,
};
