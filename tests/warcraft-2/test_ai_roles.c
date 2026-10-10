#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II ruleset", #c)

/* The ruleset's per-actor roles agree with the unit stats: exactly the
 * buildings that give food are supply, and the sides' doctrines are the
 * human and orc twins. */
int main(void) {
    G_InitGame();
    CHECK(g_ruleset.actors && g_ruleset.actor_count == W2_TYPE_COUNT + 1);
    for (int type = 1; type <= W2_TYPE_COUNT; ++type) {
        AiUnitInfo info;
        P_AiUnitInfo(NULL, (uint16_t)type, &info);
        CHECK(((info.roles & AI_ROLE_SUPPLY) != 0) == (mobjinfo[type].w2.food.supply > 0));
    }
    AiUnitInfo footman, peasant, farm;
    P_AiUnitInfo(NULL, MT_FOOTMAN, &footman);
    P_AiUnitInfo(NULL, MT_PEASANT, &peasant);
    P_AiUnitInfo(NULL, MT_FARM, &farm);
    CHECK((footman.roles & AI_ROLE_FIGHTER) && (peasant.roles & AI_ROLE_WORKER));
    CHECK((farm.roles & AI_ROLE_SUPPLY) && !(farm.roles & AI_ROLE_FIGHTER));
    CHECK(g_ruleset.faction_count == 2 && g_ruleset.factions[0].name[0] == 'H' &&
          g_ruleset.factions[1].name[0] == 'O');
    /* Orcs open earlier and bolder than humans. */
    const faction_t *human = &g_ruleset.factions[0], *orc = &g_ruleset.factions[1];
    CHECK(human->opening_count == orc->opening_count);
    CHECK(orc->wave_interval_ms < human->wave_interval_ms && orc->doctrine.attack_ratio < human->doctrine.attack_ratio);
    puts("PASS: Warcraft II roles come from the ruleset and match the stats");
    return 0;
}
