/* Every script in the installed aiscript.bin (original StarCraft layout: a
 * table of 16-byte entries, id / file offset / string / flags, ended by a zero
 * id, then the bytecode) and every script the 35 installed campaign CHKs run
 * through Run AI Script (15) and Run AI Script At Location (16). Run with
 * --table to print the markdown table kept in docs/SC_EXE_FINDINGS.md. */
#include "t_local.h"
#include "sc_local.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(c) RTS_CHECK(c, "StarCraft AI script inventory", #c)

enum { MAX_SCRIPTS = 128, LAST_OPCODE = 0x4f, BW_LAST_OPCODE = 0x71 };
/* Operand bytes of each opcode (PyMS AIBIN parameter lists: byte 1, word,
 * address, unit, upgrade and technology 2, dword 4). 255: debug, an address
 * and a zero-ended string. Opcodes above 0x4f are Brood War additions the
 * retail jump table at 0x48af5c (80 entries) does not have. */
static const uint8_t operands[BW_LAST_OPCODE + 1] = {
    2, 4, 2, 0, 0, 3, 4, 4, 3, 3, 3, 0, 3, 0, 0, 0,
    0, 0, 0, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 1,
    0, 0, 1, 0, 0, 0, 0, 255, 0, 0, 0, 0, 0, 1, 2, 0,
    3, 3, 0, 0, 0, 0, 0, 2, 0, 6, 3, 1, 2, 3, 3, 2,
    2, 0, 2, 1, 2, 3, 3, 0, 2, 3, 2, 3, 3, 0, 3, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const char *const names[LAST_OPCODE + 1] = {
    "goto", "notowns_jump", "wait", "start_town", "start_areatown", "expand", "build", "upgrade",
    "tech", "wait_build", "wait_buildstart", "attack_clear", "attack_add", "attack_prepare",
    "attack_do", "wait_secure", "capt_expand", "build_bunkers", "wait_bunkers", "defensebuild_gg",
    "defensebuild_ag", "defensebuild_ga", "defensebuild_aa", "defenseuse_gg", "defenseuse_ag",
    "defenseuse_ga", "defenseuse_aa", "defenseclear_gg", "defenseclear_ag", "defenseclear_ga",
    "defenseclear_aa", "send_suicide", "player_enemy", "player_ally", "default_min",
    "defaultbuild_off", "stop", "switch_rescue", "move_dt", "debug", "fatal_error", "enter_bunker",
    "value_area", "transports_off", "check_transports", "nuke_rate", "max_force", "clear_combatdata",
    "random_jump", "time_jump", "farms_notiming", "farms_timing", "build_turrets", "wait_turrets",
    "default_build", "harass_factor", "start_campaign", "race_jump", "region_size", "get_oldpeons",
    "groundmap_jump", "place_guard", "wait_force", "guard_resources", "call", "return",
    "eval_harass", "creep", "panic", "player_need", "do_morph", "wait_upgrades", "multirun", "rush",
    "scout_with", "define_max", "train", "target_expansion", "wait_train", "set_attacks",
};

typedef struct {
    uint32_t id, offset, end;
    int opcodes, first, bw, bad, uses, kinds[16];
    int last;
} script_t;
static script_t scripts[MAX_SCRIPTS];
static int script_count;
static blob_t bin;

static unsigned u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

static bool load_table(void) {
    if (!sc_read("data/STARCRAFT", "scripts/aiscript.bin", &bin)) return false;
    for (script_count = 0; (size_t)(script_count + 1) * 16 <= bin.size && u32(bin.bytes + script_count * 16);
         script_count++) {
        if (script_count >= MAX_SCRIPTS) return false;
        scripts[script_count].id = u32(bin.bytes + script_count * 16);
        scripts[script_count].offset = u32(bin.bytes + script_count * 16 + 4);
    }
    /* A script runs to the next higher start in the file (PyMS AIBIN); the last
     * one stops where the second copy of the table at the end of the file
     * begins. Zero bytes after the last command are padding. */
    uint32_t limit = (uint32_t)bin.size - (uint32_t)script_count * 16 - 4;
    CHECK(limit > 0 && !memcmp(bin.bytes + limit, bin.bytes, (size_t)script_count * 16));
    for (int i = 0; i < script_count; i++) {
        uint32_t end = limit;
        for (int j = 0; j < script_count; j++)
            if (scripts[j].offset > scripts[i].offset && scripts[j].offset < end) end = scripts[j].offset;
        scripts[i].end = end;
        uint32_t at = scripts[i].offset;
        scripts[i].first = at < bin.size ? bin.bytes[at] : -1;
        while (at < end) {
            uint32_t pad = at;
            while (pad < end && !bin.bytes[pad]) pad++;
            if (pad == end && at > scripts[i].offset) { at = end; break; }
            unsigned op = bin.bytes[at++];
            if (op > BW_LAST_OPCODE) { scripts[i].bad++; break; }
            if (op > LAST_OPCODE) scripts[i].bw++;
            scripts[i].opcodes++;
            scripts[i].last = (int)op;
            if (operands[op] == 255) {
                at += 2;
                while (at < end && bin.bytes[at]) at++;
                at++;
            } else at += operands[op];
        }
        if (at != end) scripts[i].bad++;
    }
    return true;
}

/* Players a TRIG record runs for: its direct slots, All Players and forces
 * (the selector bytes 2372-2399). Kinds are the CHK OWNR types. */
static int owner_kinds(const uint8_t *owners, const uint8_t *force, const uint8_t *mask, int *kinds) {
    int players = 0;
    for (int p = 0; p < 8; p++) {
        bool run = mask[p] == 1 || mask[17] == 1;
        for (int f = 0; f < 4; f++) run = run || (mask[18 + f] == 1 && force[p] == f);
        if (!run || owners[p] == 0 || owners[p] == 8) continue;
        kinds[owners[p]]++;
        players++;
    }
    return players;
}

static unsigned action_uses[2];
static int scan_chk(const char *file) {
    blob_t chk;
    if (!W_ReadFile(file, &chk)) return 0;
    const uint8_t *owners = NULL, *force = NULL, *trig = NULL;
    size_t trig_size = 0;
    for (size_t at = 0; at + 8 <= chk.size;) {
        unsigned n = u32(chk.bytes + at + 4);
        if (at + 8 + n > chk.size) break;
        if (!memcmp(chk.bytes + at, "OWNR", 4) && n >= 12) owners = chk.bytes + at + 8;
        if (!memcmp(chk.bytes + at, "FORC", 4) && n >= 8) force = chk.bytes + at + 8;
        if (!memcmp(chk.bytes + at, "TRIG", 4)) { trig = chk.bytes + at + 8; trig_size = n; }
        at += 8 + n;
    }
    uint8_t no_force[8] = {0};
    for (size_t off = 0; owners && trig && off + 2400 <= trig_size; off += 2400) {
        const uint8_t *t = trig + off;
        for (int a = 0; a < 64 && t[320 + a * 32 + 26]; a++) {
            const uint8_t *r = t + 320 + a * 32;
            if (r[26] != 15 && r[26] != 16) continue;
            if (r[27] & 2) continue; /* a disabled action */
            uint32_t id = u32(r + 20);
            int *kinds = NULL;
            script_t *s = NULL;
            for (int i = 0; i < script_count; i++) if (scripts[i].id == id) s = &scripts[i];
            CHECK(s);
            kinds = s->kinds;
            s->uses++;
            action_uses[r[26] - 15]++;
            owner_kinds(owners, force ? force : no_force, t + 2372, kinds);
        }
    }
    W_FreeFile(&chk);
    return 1;
}

static int scan_campaigns(void) {
    static const char *races[] = {"terran", "zerg", "protoss"};
    static const char *folders[] = {"tutorial", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12"};
    int files = 0;
    for (int r = 0; r < 3; r++) for (int i = 0; i < 13; i++) {
        char file[256];
        if (i == 0) snprintf(file, sizeof(file), "data/STARCRAFT/install/campaign/%s/tutorial/staredit/scenario.chk", races[r]);
        else snprintf(file, sizeof(file), "data/STARCRAFT/install/campaign/%s/%s%s/staredit/scenario.chk", races[r], races[r], folders[i]);
        files += scan_chk(file);
    }
    return files;
}

static const char *class_of(const script_t *s) {
    if (s->opcodes <= 4 && s->first != 0x38) return "single-opcode utility";
    if (s->first == 0x38) return "campaign build script";
    if (s->first == 3 || s->first == 4) return "expansion";
    return "melee or defence script";
}

static script_t *find(const char *name) {
    uint32_t id = (unsigned char)name[0] | (unsigned char)name[1] << 8 | (unsigned char)name[2] << 16 | (unsigned)(unsigned char)name[3] << 24;
    for (int i = 0; i < script_count; i++) if (scripts[i].id == id) return &scripts[i];
    return NULL;
}

static void print_table(void) {
    printf("| ID | Uses | Opcodes | First opcode | Owner types (OWNR runs) | Class |\n| --- | ---: | ---: | --- | --- | --- |\n");
    for (int i = 0; i < script_count; i++) {
        const script_t *s = &scripts[i];
        char owner[96] = "";
        static const char *kind[] = {"", "", "", "Rescue", "Unused", "Computer", "Human", "Neutral"};
        for (int k = 3; k <= 7; k++) if (s->kinds[k])
            snprintf(owner + strlen(owner), sizeof(owner) - strlen(owner), "%s%s %d", *owner ? ", " : "", kind[k], s->kinds[k]);
        printf("| %c%c%c%c | %d | %d | %s | %s | %s |\n", (char)s->id, (char)(s->id >> 8), (char)(s->id >> 16),
               (char)(s->id >> 24), s->uses, s->opcodes, s->first >= 0 && s->first <= LAST_OPCODE ? names[s->first] : "?",
               *owner ? owner : "-", class_of(s));
    }
}

int main(int argc, char **argv) {
    CHECK(load_table());
    CHECK(script_count == 83);
    for (int i = 0; i < script_count; i++) CHECK(!scripts[i].bad && !scripts[i].bw);
    CHECK(scan_campaigns() == 35);
    CHECK(action_uses[0] == 9 && action_uses[1] == 89);
    if (argc > 1 && !strcmp(argv[1], "--table")) { print_table(); return 0; }
    /* The nine short scripts are one command and stop; the campaigns use
     * each as recorded in docs/SC_EXE_FINDINGS.md. */
    static const struct { const char *id; int uses, first; } utility[] = {
        {"Suic", 5, 31}, {"SuiR", 2, 31}, {"Rscu", 2, 37}, {"Enmy", 0, 32}, {"Ally", 0, 33},
        {"MvTe", 6, 38}, {"ClrC", 1, 47}, {"VluA", 13, 42}, {"EnBk", 6, 41},
    };
    int utility_uses = 0;
    for (size_t i = 0; i < sizeof(utility) / sizeof(utility[0]); i++) {
        const script_t *s = find(utility[i].id);
        CHECK(s && s->first == utility[i].first && s->last == 36);
        CHECK(s->uses == utility[i].uses);
        CHECK(!strcmp(class_of(s), "single-opcode utility"));
        utility_uses += s->uses;
    }
    CHECK(utility_uses == 35);
    int build_uses = 0, other = 0;
    for (int i = 0; i < script_count; i++) {
        if (!strcmp(class_of(&scripts[i]), "single-opcode utility")) continue;
        build_uses += scripts[i].uses;
        other++;
    }
    CHECK(other == 74 && build_uses == 63);
    /* Retail trigger owners of the utility scripts that the fixtures use. */
    CHECK(find("Rscu")->kinds[7] == 2 && find("EnBk")->kinds[5] > 0);
    W_FreeFile(&bin);
    printf("StarCraft AI script inventory: ok\n");
    return 0;
}
