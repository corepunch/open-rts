#include "engine.h"

/* Z has no economy to plan: territories pay out as time, not money. The
 * computer player and its doctrine are not decoded yet. */
static int faction_of(const level_t *map, int owner) {
    (void)map; (void)owner;
    return -1;
}

const ruleset_t g_ruleset = {
    .game = "z",
    .policy = { .input = INPUT_LEFT_SELECT_ORDER, .sight = SIGHT_OPEN },
    .faction_of = faction_of,
};
