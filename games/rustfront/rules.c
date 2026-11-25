#include "engine.h"

/* No factions, economy or computer player yet: the skirmish map is a sandbox. */
static int faction_of(const level_t *map, int owner) {
    (void)map; (void)owner;
    return -1;
}

const ruleset_t g_ruleset = {
    .game = "rustfront",
    .policy = { .input = INPUT_LEFT_SELECT_ORDER, .sight = SIGHT_OPEN },
    .faction_of = faction_of,
};
