#include "engine.h"
#include "info.h"
#include "rustfront.h"

/* Fog of war (rules.c: SIGHT_RADIAL with MEMORY_STRUCTURES). Ground nobody of yours has seen is
 * black; ground seen before but out of sight now is dimmed and keeps the enemy structures as
 * last seen (play/p_sight.c, render/r_draw.c). Each type sees its own distance, in cells:
 * scouts far, soldiers and heavy guns near, so artillery needs a spotter to use its reach. */

static const struct { uint16_t type; int8_t cells; } SIGHT[] = {
    {MT_HQ, 8}, {MT_POWER, 5}, {MT_REFINERY, 5}, {MT_BARRACKS, 5}, {MT_FACTORY, 5}, {MT_GUN_TURRET, 7},
    {MT_SCOUT, 9}, {MT_TANK, 6}, {MT_ARTILLERY, 6}, {MT_HARVESTER, 4}, {MT_APC, 6},
    {MT_RIFLEMAN, 5}, {MT_ROCKET, 5}, {MT_MASTODON, 6},
    {MT_CL_HALL, 8}, {MT_CL_GENERATOR, 5}, {MT_CL_SMELTER, 5}, {MT_CL_CAMP, 5}, {MT_CL_YARD, 5},
    {MT_CL_TOWER, 6}, {MT_CL_TRIKE, 9}, {MT_CL_TRUCK, 7}, {MT_CL_CRAWLER, 6}, {MT_CL_RIG, 6},
    {MT_CL_HAULER, 4}, {MT_CL_RAIDER, 5}, {MT_CL_TORCHER, 4},
    {MT_LC_SPIRE, 9}, {MT_LC_FUSION, 5}, {MT_LC_EXTRACTOR, 5}, {MT_LC_ACADEMY, 5}, {MT_LC_FOUNDRY, 5},
    {MT_LC_TOWER, 8}, {MT_LC_SKIMMER, 10}, {MT_LC_GLAIVE, 7}, {MT_LC_PRISM, 7}, {MT_LC_HARVESTER, 4},
    {MT_LC_WARDEN, 6}, {MT_LC_LANCER, 6},
    /* The Uplink watches far round the base; the Hammer needs eyes forward. */
    {MT_UPLINK, 12}, {MT_HAMMER, 6}, {MT_CL_SHOP, 5}, {MT_CL_WRECKER, 6}, {MT_LC_SANCTUM, 6}, {MT_LC_AEGIS, 7},
    /* The special units: a gunship and the flyers see far; a mine sees nothing. */
    {MT_CRANE, 5}, {MT_PHANTOM, 6}, {MT_KESTREL, 8}, {MT_SENTRY_TRUCK, 5}, {MT_SENTRY, 7},
    {MT_CL_DRILL, 5}, {MT_CL_DERRICK, 4}, {MT_CL_SAPPER, 5}, {MT_CL_CRUSHER, 6}, {MT_CL_BUZZARD, 7},
    {MT_LC_STRIDER, 6}, {MT_LC_SERAPH, 8}, {MT_LC_CANTOR, 6},
};

int RF_SightCells(uint16_t type) {
    for (size_t i = 0; i < sizeof(SIGHT) / sizeof(*SIGHT); ++i)
        if (SIGHT[i].type == type) return SIGHT[i].cells;
    return 0;
}

/* A site sees only its own surroundings until it is finished. Effects and shots see nothing. */
bool G_SightRadius(const mobj_t *actor, int *radius) {
    if (actor->rf_boarded) { *radius = 0; return true; } /* a soldier inside an APC */
    int cells = RF_SightCells(actor->type_id);
    if (!cells) return *radius > 0;
    *radius = RF_IsSite(actor) ? 3 : cells;
    return true;
}

/* The engine's smooth fog: corners fade between cells, as in Dark Colony. */
void RF_DrawFog(app_t *app, const level_t *map, const tileset_t *tileset) {
    (void)tileset;
    R_DrawFog(app, map);
}
