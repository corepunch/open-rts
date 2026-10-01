#include "dark-colony.h"
#include "engine.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>

static char requested_map[128];
static dc_skirmish_t requested_setup;

void DC_RequestSkirmish(const char *map, const dc_skirmish_t *setup) {
    snprintf(requested_map, sizeof(requested_map), "%s", M_FileName(map));
    requested_setup = *setup;
}

bool DC_TakeSkirmish(const char *map, dc_skirmish_t *setup) {
    bool matches = requested_map[0] && !strcasecmp(M_FileName(map), requested_map);
    if (matches) *setup = requested_setup;
    requested_map[0] = '\0';
    return matches;
}
