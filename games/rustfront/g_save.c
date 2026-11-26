#include "engine.h"
#include "rustfront.h"

#include <string.h>

/* Rustfront's part of a saved game and of a network resync (game/g_save.c keeps the level's
 * blocking, sight, ore vents and every mobj): what else steers the game.
 *
 *   "RFS2"                  magic
 *   width, height           uint16 each, to refuse a save of another map
 *   difficulty[MAXPLAYERS]  each computer player's level (g_ai.c)
 *   terrain[width*height]   every cell's terrain: ore mined out turns to sand
 *   the units' block        each side's Rites and the Lumen Strikes on their way (g_units.c)
 *   the campaign's block    the running mission, if any (g_campaign.c)
 */

enum { HEADER = 4 + 2 + 2 + MAXPLAYERS };

static size_t cells(void) {
    return level.cell_terrain ? (size_t)level.width * level.height : 0;
}

size_t G_SaveExtraSize(void) {
    return HEADER + cells() + RF_UnitsSaveSize() + RF_CampaignSaveSize();
}

void G_SaveExtra(void *out) {
    uint8_t *p = out;
    memcpy(p, "RFS2", 4);
    p[4] = (uint8_t)level.width, p[5] = (uint8_t)(level.width >> 8);
    p[6] = (uint8_t)level.height, p[7] = (uint8_t)(level.height >> 8);
    for (int owner = 0; owner < MAXPLAYERS; ++owner) p[8 + owner] = (uint8_t)RF_AIDifficulty(owner);
    p += HEADER;
    if (cells()) memcpy(p, level.cell_terrain, cells());
    p += cells();
    RF_UnitsSave(p);
    p += RF_UnitsSaveSize();
    RF_CampaignSave(p);
}

bool G_LoadExtra(const void *data, size_t size) {
    const uint8_t *p = data;
    size_t units = RF_UnitsSaveSize();
    if (size < HEADER || memcmp(p, "RFS2", 4) || (p[4] | p[5] << 8) != level.width ||
        (p[6] | p[7] << 8) != level.height || size < HEADER + cells() + units)
        return false;
    if (!RF_UnitsLoad(p + HEADER + cells(), units)) return false;
    if (!RF_CampaignLoad(p + HEADER + cells() + units, size - HEADER - cells() - units)) return false;
    for (int owner = 0; owner < MAXPLAYERS; ++owner) RF_SetAIDifficulty(owner, p[8 + owner]);
    const uint8_t *terrain = p + HEADER;
    for (int y = 0; cells() && y < level.height; ++y)
        for (int x = 0; x < level.width; ++x) RF_SetCellTerrain(&level, x, y, terrain[y * level.width + x]);
    return true;
}
