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

/* Person and computer slots. Rescue and neutral owners are not sides. */
bool G_PlayerSide(int owner) {
    const w2_pud_t *pud = level.native_data;
    return pud && owner >= 0 && owner < 8 && (pud->owners[owner] == 4 || pud->owners[owner] == 5);
}

bool G_UnitHoldsSide(const mobj_t *unit) {
    return unit->type_id < (uint16_t)(W2_TYPE_COUNT + 1) &&
           !(mobjinfo[unit->type_id].w2.attributes & W2_NEUTRAL);
}

void G_ShowOutcome(outcome_t outcome) { W2_ShowResult(outcome == OUTCOME_VICTORY); }

void W2_VictoryReset(void) {
    w2_mission_t *mission = level.mission;
    if (mission) { mission->done = false; mission->countdown = 0; }
    level.outcome = OUTCOME_NONE;
}

/* Checked every 30 tics. Losing every unit ends a campaign mission as it
 * ends a custom map; a campaign's victory is its own objective. */
unsigned G_LevelObjectives(mobj_t *const *units, int count) {
    w2_mission_t *mission = level.mission;
    if (!mission || mission->done || menuactive || --mission->countdown > 0) return 0;
    mission->countdown = 30;
    if (!level.native_data) return 0;
    if (!mission->campaign.number) return ELIMINATE_ALL;
    /* Other campaign objectives (rescue, regions, named targets) must not
     * accidentally fall through to generic elimination. */
    if (mission->campaign.number == 1 && consoleplayer >= 0 && consoleplayer < 8) {
        int farms = 0, barracks = 0;
        for (int i = 0; i < count; ++i) {
            const mobj_t *unit = units[i];
            if (unit->owner != consoleplayer || unit->hp <= 0 || unit->remove ||
                W2_UnderConstruction(unit)) continue;
            farms += unit->type_id == (mission->campaign.orc ? MT_PIG_FARM : MT_FARM);
            barracks += unit->type_id == (mission->campaign.orc ? MT_ORC_BARRACKS : MT_HUMAN_BARRACKS);
        }
        if (farms >= 4 && barracks >= 1) G_EndLevel(OUTCOME_VICTORY);
    }
    return ELIMINATE_DEFEAT;
}

void W2_CheckVictory(mobj_t *const *units, int count) { G_OutcomeTicker(units, count); }
