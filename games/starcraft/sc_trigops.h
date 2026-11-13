#ifndef SC_TRIGOPS_H
#define SC_TRIGOPS_H
#include <stdbool.h>
#include <stdint.h>

/* Every TRIG condition (byte 15) and action (byte 26) type is classified so
 * that none can run without saying what it does. Names follow PyMS
 * FileFormats/TRG.py. */
typedef enum {
    SC_TRIG_UNSUPPORTED, /* No effect; reported once per type. */
    SC_TRIG_SUPPORTED,   /* Executed. */
    SC_TRIG_STORED,      /* State is stored, saved and hashed; no presentation. */
    SC_TRIG_PARTIAL,     /* Some operands execute; the rest are reported. */
    SC_TRIG_NOOP         /* Defined to have no game effect. */
} sc_trig_status_t;

enum { SC_TRIG_ACTION_TYPES = 60, SC_TRIG_CONDITION_TYPES = 24 };

sc_trig_status_t sc_trigger_action_status(unsigned id);
sc_trig_status_t sc_trigger_condition_status(unsigned id);
const char *sc_trigger_action_name(unsigned id);
const char *sc_trigger_condition_name(unsigned id);

/* Leaderboard actions 17-21, 32-37 and 40. The latest leaderboard action
 * replaces the active one; values are recomputed every mission tick. */
enum {
    SC_LB_NONE, SC_LB_CONTROL, SC_LB_CONTROL_AT, SC_LB_RESOURCES, SC_LB_KILLS,
    SC_LB_POINTS, SC_LB_GREED
};
typedef struct {
    uint8_t kind;     /* SC_LB_*; zero when no leaderboard is active. */
    uint8_t goal;     /* Actions 33-37 and 40 carry a goal quantity. */
    uint8_t selector; /* Resource type or score type, when the kind has one. */
    uint16_t unit;
    uint32_t title;   /* STR index, zero for the default text. */
    uint32_t location, quantity;
    int64_t value[8];
} sc_leaderboard_t;

/* Run AI Script (15) and At Location (16): the last script a player ran. */
typedef struct {
    uint32_t id;       /* The four script characters, as stored in the CHK. */
    uint32_t location; /* Raw location field; zero for action 15. */
    uint32_t runs;
} sc_ai_script_t;

enum { SC_AI_FALLBACK_LOG = 64 };
#endif
