#include "engine.h"
#include "info.h"

/* 7th Legion's ruleset: one faction: troopers and mechs around a tank core, no towers (the Wall
 * has no gun), no flyers to counter and no supply, so army_cap bounds the
 * army. Slaves and Trucks harvest, the Mobile
 * Base builds all, so a plan waits for one. Product ids are catalog ui_ids:
 * 1 Trooper, 2 Slave, 3 Spider Mech, 4 Tank, 5 Rock Mech, 6 Truck. */

static const AiStep legion_opening[] = {
    { 2, 2 }, { 1, 3 }, { 2, 3 }, { 3, 2 }, { 4, 2 }, { 6, 1 },
    { 1, 8 }, { 4, 4 }, { 5, 2 }, { 3, 4 }, { 1, 12 }, { 4, 6 }, { 5, 4 },
};

/* The mission script counts the troopers and bases; every start also gets a
 * slave to harvest, three cells below the start. */
static const startunit_t legion_start[] = { { 2, 1, {0, 3}, {0, 0} } };

static const faction_t factions[1] = {
    [0] = { .name = "Legion",
        .start_units = legion_start, .start_unit_count = 1,
        .opening = legion_opening, .opening_count = (int)(sizeof(legion_opening) / sizeof(*legion_opening)),
        .wave_interval_ms = 35000, .wave_min_size = 5, .wave_max_size = 14,
        .doctrine = { .workers = 4, .army_cap = 30, .attack_ratio = 100, .retreat_ratio = 45,
            .roster = { {2,0},{6,0},{1,35},{3,25},{4,25},{5,15} },
            .roster_count = 6 } },
};

static int faction_of(const level_t *map, int owner) {
    (void)map;
    return G_ModelHasActorType(NULL, owner, 7) ? 0 : -1; /* No Mobile Base yet. */
}

const ruleset_t g_ruleset = {
    .game = "7legion",
    .policy = { .input = INPUT_LEFT_SELECT_ORDER },
    .factions = factions, .faction_count = 1, .faction_of = faction_of,
};
