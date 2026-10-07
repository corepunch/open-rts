#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"
#include <stdlib.h>

/* Campaign objectives are distinct from skirmish elimination. The first
 * human/orc objective is confirmed by STRDAT 53 and Wargus level01*_c.sms. */
static w2_campaign_t next_campaign;

void W2_SetCampaign(int number, bool orc) { next_campaign = (w2_campaign_t){number, orc}; }

bool w2_init_mission(level_t *map) {
    w2_mission_t *mission = calloc(1, sizeof(*mission));
    if (!mission) return false;
    mission->campaign = next_campaign;
    next_campaign = (w2_campaign_t){0};
    map->mission = mission;
    map->destroy_mission = free;
    return true;
}

static bool alive(int owner, mobj_t *const *units, int count) {
    for (int i = 0; i < count; ++i) {
        const mobj_t *unit = units[i];
        if (unit->owner != owner || unit->hp <= 0 || unit->remove) continue;
        if (unit->type_id >= (uint16_t)(W2_TYPE_COUNT + 1)) continue;
        if (!(mobjinfo[unit->type_id].w2.attributes & W2_NEUTRAL)) return true;
    }
    return false;
}

void W2_VictoryReset(void) {
    w2_mission_t *mission = level.mission;
    if (mission) { mission->done = false; mission->countdown = 0; }
}

void W2_CheckVictory(mobj_t *const *units, int count) {
    w2_mission_t *mission = level.mission;
    if (!mission || mission->done || netgame || menuactive || --mission->countdown > 0) return;
    mission->countdown = 30;
    const w2_pud_t *pud = level.native_data;
    if (!pud || consoleplayer < 0 || consoleplayer >= 8) return;
    bool survived = alive(consoleplayer, units, count);
    if (!survived) { mission->done = true; W2_ShowResult(false); return; }
    if (mission->campaign.number) {
        /* Other campaign objectives (rescue, regions, named targets) must
         * not accidentally fall through to generic elimination. */
        if (mission->campaign.number != 1) return;
        int farms = 0, barracks = 0;
        for (int i = 0; i < count; ++i) {
            const mobj_t *unit = units[i];
            if (unit->owner != consoleplayer || unit->hp <= 0 || unit->remove ||
                W2_UnderConstruction(unit)) continue;
            farms += unit->type_id == (mission->campaign.orc ? MT_PIG_FARM : MT_FARM);
            barracks += unit->type_id == (mission->campaign.orc ? MT_ORC_BARRACKS : MT_HUMAN_BARRACKS);
        }
        if (farms < 4 || barracks < 1) return;
        mission->done = true;
        W2_ShowResult(true);
        return;
    }
    int opponents = 0;
    bool opponent_left = false;
    for (int i = 0; i < 8; ++i) {
        if (i == consoleplayer || (pud->owners[i] != 4 && pud->owners[i] != 5)) continue;
        ++opponents;
        opponent_left = opponent_left || alive(i, units, count);
    }
    if (!opponents) return;
    if (opponent_left) return;
    mission->done = true;
    W2_ShowResult(true);
}
