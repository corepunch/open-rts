#include "engine.h"
#include "info.h"
#include "rustfront.h"

/* Rustfront's three factions. Every faction fills the same roles with its own types, so the
 * rules that care about a role (a Command Center builds, a refinery takes ore and comes with a
 * harvester, infantry keep a lookout) work for all three:
 *
 *   Directorate   painted industrial army: balanced, armour-heavy, the Mastodon at the top.
 *   Scrap Clans   scavengers: cheap, fast and many; flamethrowers, gun trucks, rocket rigs.
 *   Lumen Concord a high-technology order: few, expensive and strong; hover craft and beams.
 *
 * A side plays the faction of its headquarters (or, before it has one, of any unit it owns). */

const rf_faction_t rf_factions[RF_FACTION_COUNT] = {
    [RF_DIRECTORATE] = {"Directorate", MT_HQ, MT_MASTODON,
                        {MT_HQ, MT_POWER, MT_REFINERY, MT_BARRACKS, MT_FACTORY, MT_GUN_TURRET, MT_HARVESTER}},
    [RF_CLANS] = {"Scrap Clans", MT_CL_HALL, MT_CL_TORCHER,
                  {MT_CL_HALL, MT_CL_GENERATOR, MT_CL_SMELTER, MT_CL_CAMP, MT_CL_YARD, MT_CL_TOWER, MT_CL_HAULER}},
    [RF_CONCORD] = {"Lumen Concord", MT_LC_SPIRE, MT_LC_LANCER,
                    {MT_LC_SPIRE, MT_LC_FUSION, MT_LC_EXTRACTOR, MT_LC_ACADEMY, MT_LC_FOUNDRY, MT_LC_TOWER,
                     MT_LC_HARVESTER}},
};

int RF_FactionOfType(uint16_t type) {
    for (int f = RF_FACTION_COUNT - 1; f >= 0; --f)
        if (type >= rf_factions[f].first && type <= rf_factions[f].last) return f;
    return -1;
}

uint16_t RF_Role(int faction, rf_role_t role) {
    return faction >= 0 && faction < RF_FACTION_COUNT && role < RF_ROLE_COUNT ? rf_factions[faction].role[role] : 0;
}

bool RF_IsRole(uint16_t type, rf_role_t role) {
    int f = RF_FactionOfType(type);
    return f >= 0 && type && rf_factions[f].role[role] == type;
}

bool RF_IsInfantry(uint16_t type) {
    return type == MT_RIFLEMAN || type == MT_ROCKET || type == MT_CL_RAIDER || type == MT_CL_TORCHER ||
           type == MT_LC_WARDEN || type == MT_LC_LANCER;
}

int RF_OwnerFaction(int owner) {
    int any = -1;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *unit = (const mobj_t *)th;
        if (th->function != P_MobjThinker || unit->owner != owner || unit->remove || unit->hp <= 0) continue;
        int f = RF_FactionOfType(unit->type_id);
        if (f < 0) continue;
        if (RF_IsRole(unit->type_id, RF_ROLE_HQ)) return f;
        if (any < 0) any = f;
    }
    return any;
}

bool RF_OwnerHasRole(int owner, rf_role_t role) {
    for (int f = 0; f < RF_FACTION_COUNT; ++f)
        if (G_ModelHasActorType(NULL, owner, rf_factions[f].role[role])) return true;
    return false;
}
