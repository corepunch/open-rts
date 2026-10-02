#include "engine.h"
#include "dark-reign.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static char requested_map[128];
static dr_skirmish_t requested, current;
static bool current_valid;

void DR_RequestSkirmish(const char *map, const dr_skirmish_t *setup) {
    snprintf(requested_map, sizeof(requested_map), "%s", M_FileName(map));
    requested = *setup;
}

/* Taken by the level that matches the requested map; any other level, such
 * as a campaign mission, plays its scenario as authored. */
bool DR_TakeSkirmish(const char *map, dr_skirmish_t *setup) {
    current_valid = requested_map[0] && !strcasecmp(M_FileName(map), requested_map);
    if (current_valid) current = requested;
    if (current_valid && setup) *setup = current;
    requested_map[0] = '\0';
    return current_valid;
}

const dr_skirmish_t *DR_LevelSkirmish(void) {
    return current_valid ? &current : NULL;
}
