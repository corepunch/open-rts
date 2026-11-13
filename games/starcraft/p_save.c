#include "sc_local.h"
#include <string.h>

typedef struct { sc_mission_t mission; int player; } sc_save_t;

/* The engine saves mobjs, research and resources. CHK bytes remain owned by
 * the loaded level; only the mission runtime and per-player trigger cursors
 * are restored here. These are engine saves, not retail .snx files. */
size_t G_SaveExtraSize(void) {
    const sc_mission_t *m = level.mission;
    return m ? sizeof(sc_save_t) + (size_t)m->trig_count * 8 * sizeof(sc_trig_t) : 0;
}

void G_SaveExtra(void *out) {
    const sc_mission_t *m = level.mission;
    if (!m) return;
    sc_save_t saved = {.mission = *m, .player = consoleplayer};
    saved.mission.file = (blob_t){0};
    saved.mission.trig = saved.mission.str = saved.mission.uprp = NULL;
    saved.mission.rt = NULL;
    saved.mission.terrain = NULL;
    memcpy(out, &saved, sizeof(saved));
    if (m->trig_count)
        memcpy((uint8_t *)out + sizeof(saved), m->rt,
               (size_t)m->trig_count * 8 * sizeof(sc_trig_t));
}

bool G_LoadExtra(const void *data, size_t size) {
    sc_mission_t *m = level.mission;
    sc_save_t record;
    if (!m || size != G_SaveExtraSize()) return false;
    memcpy(&record, data, sizeof(record));
    sc_mission_t saved = record.mission;
    if (record.player < 0 || record.player >= 8) return false;
    if (saved.map_mode > SC_MAP_MELEE || saved.research_rules_loaded > 1 ||
        saved.custom_slots_set > 1 ||
        saved.trig_count != m->trig_count || saved.trig_size != m->trig_size ||
        saved.str_size != m->str_size || saved.uprp_size != m->uprp_size ||
        saved.loc_count < 0 || saved.loc_count > SC_LOCATION_COUNT || saved.result < 0 || saved.result > 3 ||
        !memchr(saved.objectives, 0, sizeof(saved.objectives)) ||
        !memchr(saved.next_scenario, 0, sizeof(saved.next_scenario))) return false;
    for (int i = 0; i < 8; ++i) {
        if (saved.custom_slots_set &&
            (saved.custom_slot_kind[i] > SC_SLOT_CLOSED || saved.custom_slot_race[i] > 2)) return false;
        if (saved.player_result[i] > 3 || (saved.neutral_relations[i] & ~UINT32_C(0x7f800000))) return false;
        for (int tech = 0; tech < SC_TECHS; ++tech)
            if (saved.tech_available[i][tech] > 1) return false;
    }
    const uint8_t *runtime = (const uint8_t *)data + sizeof(record);
    for (int i = 0; i < m->trig_count * 8; ++i) {
        sc_trig_t trigger;
        memcpy(&trigger, runtime + (size_t)i * sizeof(trigger), sizeof(trigger));
        if (trigger.cursor < 0 || trigger.cursor > 64 || trigger.current < 0 ||
            trigger.current >= 8) return false;
    }
    saved.file = m->file;
    saved.trig = m->trig;
    saved.str = m->str;
    saved.uprp = m->uprp;
    saved.rt = m->rt;
    saved.terrain = m->terrain;
    /* A resync imports shared outcomes but not the sender's local UI state. */
    if (G_Resyncing()) {
        saved.result = m->result;
        saved.unit_speech_muted = m->unit_speech_muted;
    }
    *m = saved;
    sc_restore_map_session((sc_map_mode_t)m->map_mode,m->custom_slots_set,
                           m->custom_slot_kind,m->custom_slot_race);
    if (m->trig_count)
        memcpy(m->rt, runtime, (size_t)m->trig_count * 8 * sizeof(sc_trig_t));
    if (!netgame) consoleplayer = record.player;
    return true;
}
