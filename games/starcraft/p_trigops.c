#include "sc_local.h"
#include <stdio.h>

/* Names are PyMS FileFormats/TRG.py's action and condition lists. Status is
 * what p_trig.c does today; see docs/SC_EXE_FINDINGS.md for the inventory of
 * the installed campaign CHKs against this table. */
typedef struct { const char *name; sc_trig_status_t status; } trig_op_t;
#define S SC_TRIG_SUPPORTED
#define U SC_TRIG_UNSUPPORTED
#define T SC_TRIG_STORED
#define P SC_TRIG_PARTIAL
#define N SC_TRIG_NOOP

static const trig_op_t actions[SC_TRIG_ACTION_TYPES] = {
    {"No Action", N}, {"Victory", S}, {"Defeat", S}, {"Preserve Trigger", S}, {"Wait", S},
    {"Pause Game", U}, {"Unpause Game", U}, {"Transmission", S}, {"Play WAV", S},
    {"Display Text Message", S}, {"Center View", S}, {"Create Unit with Properties", S},
    {"Set Mission Objectives", S}, {"Set Switch", S}, {"Set Countdown Timer", S},
    {"Run AI Script", P}, {"Run AI Script At Location", P},
    {"Leaderboard Control", T}, {"Leaderboard Control At Location", T},
    {"Leaderboard Resources", T}, {"Leaderboard Kills", T}, {"Leaderboard Points", T},
    {"Kill Unit", S}, {"Kill Unit At Location", S}, {"Remove Unit", S},
    {"Remove Unit At Location", S}, {"Set Resources", S}, {"Set Score", S},
    {"Minimap Ping", S}, {"Talking Portrait", T}, {"Mute Unit Speech", S},
    {"Unmute Unit Speech", S}, {"Leaderboard Computer Players", T},
    {"Leaderboard Goal Control", T}, {"Leaderboard Goal Control At Location", T},
    {"Leaderboard Goal Resources", T}, {"Leaderboard Goal Kills", T},
    {"Leaderboard Goal Points", T}, {"Move Location", S}, {"Move Unit", S},
    {"Leaderboard Greed", T}, {"Set Next Scenario", S}, {"Set Doodad State", U},
    {"Set Invincibility", S}, {"Create Unit", S}, {"Set Deaths", S}, {"Order", S},
    {"Comment", N}, {"Give Units to Player", S}, {"Modify Unit Hit Points", S},
    {"Modify Unit Energy", S}, {"Modify Unit Shield Points", S},
    {"Modify Unit Resource Amount", S}, {"Modify Unit Hangar Count", S},
    {"Pause Timer", S}, {"Unpause Timer", S}, {"Draw", S}, {"Set Alliance Status", S},
    {"Disable Debug Mode", U}, {"Enable Debug Mode", U},
};

static const trig_op_t conditions[SC_TRIG_CONDITION_TYPES] = {
    {"No Condition", N}, {"Countdown Timer", S}, {"Command", S}, {"Bring", S},
    {"Accumulate", S}, {"Kill", S}, {"Command the Most", S}, {"Commands the Most At", S},
    {"Most Kills", S}, {"Highest Score", S}, {"Most Resources", S}, {"Switch", S},
    {"Elapsed Time", S}, {"Mission Briefing", U}, {"Opponents", S}, {"Deaths", S},
    {"Command the Least", S}, {"Command the Least At", S}, {"Least Kills", S},
    {"Lowest Score", S}, {"Least Resources", S}, {"Score", S}, {"Always", S}, {"Never", S},
};

sc_trig_status_t sc_trigger_action_status(unsigned id) {
    return id < SC_TRIG_ACTION_TYPES ? actions[id].status : SC_TRIG_UNSUPPORTED;
}
sc_trig_status_t sc_trigger_condition_status(unsigned id) {
    return id < SC_TRIG_CONDITION_TYPES ? conditions[id].status : SC_TRIG_UNSUPPORTED;
}
const char *sc_trigger_action_name(unsigned id) {
    return id < SC_TRIG_ACTION_TYPES ? actions[id].name : "Unknown action";
}
const char *sc_trigger_condition_name(unsigned id) {
    return id < SC_TRIG_CONDITION_TYPES ? conditions[id].name : "Unknown condition";
}

/* One stderr line per type per mission. The bits are saved with the mission
 * so a loaded game does not repeat them; they are not part of the hash. */
static void report_once(sc_mission_t *m, bool action, unsigned id, const char *what) {
    uint8_t *bits = action ? m->warned_action : m->warned_condition;
    if (id > 255 || (bits[id >> 3] & (1u << (id & 7)))) return;
    bits[id >> 3] |= (uint8_t)(1u << (id & 7));
    m->trigger_warnings++;
    fprintf(stderr, "starcraft: trigger %s %u (%s) %s\n", action ? "action" : "condition",
            id, action ? sc_trigger_action_name(id) : sc_trigger_condition_name(id), what);
}

void sc_trigger_note(sc_mission_t *m, bool action, unsigned id) {
    sc_trig_status_t status = action ? sc_trigger_action_status(id) : sc_trigger_condition_status(id);
    if (status == SC_TRIG_UNSUPPORTED) report_once(m, action, id, "is not implemented and was skipped");
}

bool sc_trigger_reported(const sc_mission_t *m, bool action, unsigned id) {
    const uint8_t *bits = action ? m->warned_action : m->warned_condition;
    return id <= 255 && (bits[id >> 3] & (1u << (id & 7)));
}

/* Run AI Script keeps the script and location. Only the shared-vision
 * scripts have native behavior; every other script falls back to the shared
 * planner, reported once per script ID. */
void sc_ai_script_fallback(sc_mission_t *m, uint32_t id) {
    for (int i = 0; i < m->ai_fallback_count; i++) if (m->ai_fallback[i] == id) return;
    if (m->ai_fallback_count < SC_AI_FALLBACK_LOG) m->ai_fallback[m->ai_fallback_count++] = id;
    m->trigger_warnings++;
    fprintf(stderr, "starcraft: AI script '%c%c%c%c' has no native implementation; "
            "using the shared planner\n", (char)id, (char)(id >> 8), (char)(id >> 16),
            (char)(id >> 24));
}
