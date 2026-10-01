#ifndef __INFO_GEN__
#define __INFO_GEN__

#include <stdbool.h>
#include <stdio.h>

/* Names of the rows written so far; statenum_t is emitted from them, so the
 * enum can never disagree with the table. Write info.c before info.h. */
enum { STATE_GEN_MAX = 4096 };
static char state_gen_names[STATE_GEN_MAX][96];
static int state_gen_count;

/* Writes one states[] row: a run of `count` frames sharing an action.
 * `frame_tics` lists each frame's duration and becomes a TICS(...) list when
 * they differ; NULL gives every frame `tics`. */
static void write_state(FILE *f, const char *name, const char *sprite, int frame,
                        int count, int tics, const int *frame_tics,
                        const char *action, const char *next, int group) {
    bool uneven = false;
    if (frame_tics) {
        tics = frame_tics[0];
        for (int i = 1; i < count; ++i) uneven |= frame_tics[i] != tics;
    }
    fprintf(f, "    [%s] = { %s, %d, %d, %d, %s, %s, %d, ",
            name, sprite, frame, count, uneven ? 0 : tics, action, next, group);
    if (uneven) {
        fprintf(f, "TICS(");
        for (int i = 0; i < count; ++i) fprintf(f, "%s%d", i ? "," : "", frame_tics[i]);
        fprintf(f, ")");
    } else {
        fprintf(f, "NULL");
    }
    fprintf(f, " },\n");
    if (state_gen_count < STATE_GEN_MAX)
        snprintf(state_gen_names[state_gen_count++], sizeof(*state_gen_names), "%s", name);
}

static void write_statenum(FILE *f) {
    fprintf(f, "typedef enum {\n    S_NULL = 0,\n");
    for (int i = 0; i < state_gen_count; ++i) fprintf(f, "    %s,\n", state_gen_names[i]);
    fprintf(f, "    NUMSTATES\n} statenum_t;\n");
}

/* Opens states[]; rows hold sprite, first frame, frames, tics, action, next
 * state and group. */
static void write_states_begin(FILE *f) {
    fprintf(f, "const state_t states[NUMSTATES] = {\n"
               "    { 0, 0, 0, -1, NULL, S_NULL, 0, NULL },\n");
}

#endif
