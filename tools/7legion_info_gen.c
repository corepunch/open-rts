/* Generate 7th Legion's Doom-style state and mobjinfo tables.
   The catalog is intentionally limited to the native BIM actors used by the
   current playable vertical slice. */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "info_gen.h"

typedef struct {
    const char *type;
    const char *sprite;
    const char *asset;
    const char *doomednum;
    int health;
    int speed;
    int radius;
    int height;
    int mass;
    int damage;
    const char *flags;
} info_entry_t;

static void write_info_states(FILE *file, const info_entry_t *entry) {
    char sprite[64], stand[96], fire[96];
    snprintf(sprite, sizeof(sprite), "SPR_%s", entry->sprite);
    snprintf(stand, sizeof(stand), "S_%s_STND", entry->sprite);
    snprintf(fire, sizeof(fire), "S_%s_FIRE", entry->sprite);
    write_state(file, stand, sprite, 0, 1, entry->damage ? 1 : -1, NULL,
                entry->damage ? "A_Look" : "NULL", stand, 0);
    if (entry->damage)
        write_state(file, fire, sprite, 0, 1, 1, NULL, "A_Attack", stand, 3);
}

/* Call after write_info_c: its rows name the states of the enum. */
static bool write_info_h(const char *path, const char *source,
                         const info_entry_t *entries, int count) {
    FILE *file = fopen(path, "w");
    if (!file) return false;
    fprintf(file,
        "/* Generated from %s. Do not edit by hand. */\n"
        "#ifndef __INFO__\n#define __INFO__\n\n#include \"engine.h\"\n\n"
        "typedef struct mobjinfo_s {\n"
        "    int doomednum;\n    int spawnstate;\n    int spawnhealth;\n"
        "    int seestate;\n    int seesound;\n    int reactiontime;\n"
        "    int attacksound;\n    int painstate;\n    int painchance;\n"
        "    int painsound;\n    int meleestate;\n    int missilestate;\n"
        "    int deathstate;\n    int xdeathstate;\n    int deathsound;\n"
        "    int speed;\n    int radius;\n    int height;\n    int mass;\n"
        "    int damage;\n    int activesound;\n    int flags;\n"
        "    int raisestate;\n    fixed_t spawnz;\n} mobjinfo_t;\n\n",
        source);
    fprintf(file, "typedef enum {\n");
    for (int i = 0; i < count; ++i) fprintf(file, "    SPR_%s,\n", entries[i].sprite);
    fprintf(file, "    NUMSPRITES\n} spritenum_t;\n\n");
    write_statenum(file);
    fprintf(file, "\nenum {\n    MT_NULL,\n");
    for (int i = 0; i < count; ++i) fprintf(file, "    MT_%s,\n", entries[i].type);
    fprintf(file,
        "    NUMMOBJTYPES,\n};\n\n"
        "extern const char *const sprnames[NUMSPRITES];\n"
        "extern const state_t states[NUMSTATES];\n"
        "extern const mobjinfo_t mobjinfo[NUMMOBJTYPES];\n"
        "extern const gameinfo_t game_info;\n\n#endif\n");
    return fclose(file) == 0;
}

static bool write_info_c(const char *path, const char *source, const char *extra_include,
                         const char *selection_style, bool lowercase_assets,
                         const info_entry_t *entries, int count) {
    FILE *file = fopen(path, "w");
    if (!file) return false;
    fprintf(file, "/* Generated from %s. Do not edit by hand. */\n#include \"engine.h\"\n", source);
    if (extra_include) fprintf(file, "#include \"%s\"\n", extra_include);
    fprintf(file, "#include \"info.h\"\n\nconst char *const sprnames[NUMSPRITES] = {\n");
    for (int i = 0; i < count; ++i) {
        fprintf(file, "    \"");
        for (const char *p = entries[i].asset; *p; ++p)
            fputc(lowercase_assets ? tolower((unsigned char)*p) : *p, file);
        fprintf(file, "\",\n");
    }
    fprintf(file, "};\n\n");
    write_states_begin(file);
    for (int i = 0; i < count; ++i) write_info_states(file, &entries[i]);
    fprintf(file, "};\n\nconst mobjinfo_t mobjinfo[NUMMOBJTYPES] = {\n    { 0 },\n");
    for (int i = 0; i < count; ++i) {
        const info_entry_t *entry = &entries[i];
        fprintf(file,
            "    { // MT_%s\n"
            "        .doomednum = %s, .spawnstate = S_%s_STND, .spawnhealth = %d,\n",
            entry->type, entry->doomednum, entry->sprite, entry->health);
        if (entry->speed)
            fprintf(file, "        .seestate = S_%s_STND, .speed = %d,\n",
                    entry->sprite, entry->speed);
        if (entry->damage)
            fprintf(file, "        .missilestate = S_%s_FIRE, .damage = %d,\n",
                    entry->sprite, entry->damage);
        fprintf(file, "        .deathstate = S_NULL, .xdeathstate = S_NULL,\n");
        if (entry->radius || entry->height || entry->mass)
            fprintf(file, "        .radius = %d, .height = %d, .mass = %d,\n",
                    entry->radius, entry->height, entry->mass);
        fprintf(file, "        .flags = %s,\n    },\n", entry->flags);
    }
    fprintf(file,
        "};\n\nconst gameinfo_t game_info = {\n"
        "    sprnames, NUMSPRITES, states, NUMSTATES, mobjinfo, NUMMOBJTYPES,\n"
        "    S_NULL, RTS_STATE_COORDS_GROUND_OFFSET,\n"
        "    { .style = %s },\n    NULL,\n"
        "    .draw_overlays = NULL,\n"
        "};\n", selection_style);
    return fclose(file) == 0;
}

#define MOBILE(type, sprite, asset, id, hp, speed, damage, extra) \
    { type, sprite, asset, id, hp, speed, 16, 32, 100, damage, \
      "MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE" extra }

static const info_entry_t entries[] = {
    MOBILE("TROOPER", "LTROOP", "GFX/LTROOP.BIM", "1", 100, 4, 15, "|MF_ATTACK"),
    MOBILE("SLAVE", "SLAVEN1", "GFX/SLAVEN1.BIM", "2", 60, 4, 0, "|MF_HARVESTER"),
    MOBILE("SPIDER_MECH", "SPIDER", "GFX/SPIDER.BIM", "3", 300, 3, 35, "|MF_ATTACK"),
    MOBILE("TANK", "TANKBASE", "GFX/TANKBASE.BIM", "4", 500, 5, 50, "|MF_ATTACK"),
    MOBILE("ROCK_MECH", "ROCKMECH", "GFX/ROCKMECH.BIM", "5", 800, 3, 70, "|MF_ATTACK"),
    MOBILE("TRUCK", "TRUCK", "GFX/TRUCK.BIM", "6", 200, 5, 0, "|MF_HARVESTER"),
    MOBILE("MOBILE_BASE", "MOBBASE", "GFX/MOBBASE.BIM", "7", 1000, 3, 0, ""),
#define SL_BUILDING(native, type, asset, label, hp, cost, ticks, w, h) \
    {#type, #type, asset, "1000 + " #native, hp, 0, 0, 0, 0, 0, \
     "MF_SELECTABLE|MF_RENDERABLE"},
#include "../games/7legion/buildings.inc"
#undef SL_BUILDING
};

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: 7legion_info_gen <7legion-root> <info.h> <info.c>\n");
        return 1;
    }
    int count = (int)(sizeof(entries) / sizeof(*entries));
    for (int i = 0; i < count; ++i) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", argv[1],
                 entries[i].speed ? entries[i].asset : "legion.exe");
        if (access(path, R_OK) != 0) {
            fprintf(stderr, "7legion_info_gen: missing native asset %s: %s\n", path, strerror(errno));
            return 1;
        }
    }
    if (!write_info_c(argv[3], "the verified 7th Legion BIM actor catalog", NULL,
                      "SELECTION_STYLE_DEFAULT", false, entries, count) ||
        !write_info_h(argv[2], "the verified 7th Legion BIM actor catalog", entries, count)) {
        fprintf(stderr, "7legion_info_gen: cannot write output: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
