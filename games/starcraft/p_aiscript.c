#include "sc_local.h"
#include <stdio.h>
#include <string.h>

/* Run AI Script (15) and At Location (16) reach the retail gate 0x489eb0: it
 * checks the in-game player type, searches the loaded aiscript.bin for the
 * id, and only then starts a script thread (0x489f20). A gated-out owner and
 * an absent id do nothing at all.
 *
 * aiscript.bin keeps the original layout: a table of 16-byte entries (id,
 * file offset, string, flags) ended by a zero id, the bytecode, a second copy
 * of the table and four bytes. A script runs to the next higher start, or to
 * the copy for the last one. The utility scripts the campaigns run are one
 * command followed by stop (0x24) or fatal_error (0x28); the interpreter
 * 0x48a0b0 would stop there too, so running the command when the action
 * fires is the same sequence. Every longer script needs the AI region or
 * attack-wave planner state this engine does not hold, so it reports instead
 * of half-running. */

static blob_t bin;
static char bin_root[1024];
static bool bin_tried;

void sc_aiscript_root(const char *root) {
    const char *want = root && *root ? root : g_game_default_root;
    if (bin_root[0] && !strcmp(bin_root, want)) return;
    W_FreeFile(&bin);
    bin = (blob_t){0};
    bin_tried = false;
    snprintf(bin_root, sizeof(bin_root), "%s", want);
}

static const blob_t *aiscript_bin(void) {
    if (!bin_tried) {
        bin_tried = true;
        if (!bin_root[0]) snprintf(bin_root, sizeof(bin_root), "%s", g_game_default_root);
        sc_read(bin_root, "scripts/aiscript.bin", &bin);
    }
    return bin.size ? &bin : NULL;
}

/* The bytes of one script, or NULL when the id is absent. */
static const uint8_t *script_bytes(uint32_t id, uint32_t *size) {
    const blob_t *b = aiscript_bin();
    if (!b || b->size < 32) return NULL;
    unsigned n = 0;
    while ((n + 1) * 16 <= b->size / 2 && read_u32_le(b->bytes + n * 16)) n++;
    uint32_t limit = (uint32_t)b->size - n * 16 - 4;
    if (!n || limit < n * 16 || memcmp(b->bytes + limit, b->bytes, n * 16)) return NULL;
    uint32_t start = 0;
    for (unsigned i = 0; i < n; i++) if (read_u32_le(b->bytes + i * 16) == id) {
        start = read_u32_le(b->bytes + i * 16 + 4);
        break;
    }
    if (!start || start >= limit) return NULL;
    uint32_t end = limit;
    for (unsigned i = 0; i < n; i++) {
        uint32_t next = read_u32_le(b->bytes + i * 16 + 4);
        if (next > start && next < end) end = next;
    }
    *size = end - start;
    return b->bytes + start;
}

/* True when the script is one command the engine can carry out followed by
 * stop or fatal_error. send_suicide (0x1f) is the only one of them with an
 * operand byte. */
static bool one_command(const uint8_t *code, uint32_t len, unsigned *op) {
    if (!len) return false;
    *op = code[0];
    uint32_t at = 1;
    if (*op == 0x1f) {
        if (len < 3) return false;
        at = 2;
    } else if (*op != 0x20 && *op != 0x21 && *op != 0x25 && *op != 0x26 &&
               *op != 0x29 && *op != 0x2a && *op != 0x2f) return false;
    return at < len && (code[at] == 0x24 || code[at] == 0x28);
}

/* player_enemy (0x483100) and player_ally (0x483170) walk the location and
 * write one direction of the relation table; send_suicide, move_dt,
 * value_area and clear_combatdata need planner state the engine does not
 * have. */
static bool run_one(sc_mission_t *m, int owner, uint32_t location, unsigned op) {
    switch (op) {
    case 0x20: sc_ai_relation(m, owner, location, false); break;
    case 0x21: sc_ai_relation(m, owner, location, true); break;
    case 0x25: sc_ai_switch_rescue(m, owner); break;
    case 0x29: sc_ai_enter_bunker(m, owner, location); break;
    default: return false;
    }
    return true;
}

void sc_ai_script_start(sc_mission_t *m, int owner, uint32_t id, uint32_t location) {
    if (!m || owner < 0 || owner >= 8) return;
    int kind = m->owners[owner];
    /* Type 1 (Computer) may run any id; type 7 (Neutral) only Rscu and
     * EnBk; every other type, human slots included, does nothing. */
    bool may = kind == 5 || (kind == 7 && (id == UINT32_C(0x75637352) || /* Rscu */
                                           id == UINT32_C(0x6b426e45))); /* EnBk */
    if (!may) return;
    uint32_t len = 0;
    const uint8_t *code = script_bytes(id, &len);
    if (!code) { sc_ai_script_fallback(m, id); return; }
    sc_ai_script_t *script = &m->ai_script[owner];
    script->id = id;
    script->location = location;
    script->runs++;
    /* The shared planner follows the thread only for a Computer owner; a
     * Neutral one that Rscu converts must not start building. */
    if (kind == 5) m->ai_on[owner] = true;
    unsigned op = 0;
    if (!one_command(code, len, &op) || !run_one(m, owner, location, op))
        sc_ai_script_fallback(m, id);
}
