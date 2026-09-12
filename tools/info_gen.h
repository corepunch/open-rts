#ifndef __INFO_GEN__
#define __INFO_GEN__

#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>

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

static bool write_info_h(const char *path, const char *source,
                         const info_entry_t *entries, int count) {
    FILE *file = fopen(path, "w");
    if (!file) return false;
    fprintf(file,
        "/* Generated from %s. Do not edit by hand. */\n"
        "#ifndef __INFO__\n#define __INFO__\n\n#include \"actor.h\"\n\n"
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
    fprintf(file, "    NUMSPRITES\n} spritenum_t;\n\ntypedef enum {\n    S_NULL = 0,\n");
    for (int i = 0; i < count; ++i) {
        fprintf(file, "    S_%s_STND,\n", entries[i].sprite);
        if (entries[i].damage) fprintf(file, "    S_%s_FIRE,\n", entries[i].sprite);
    }
    fprintf(file, "    NUMSTATES\n} statenum_t;\n\nenum {\n    MT_NULL,\n");
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
    fprintf(file, "};\n\nconst state_t states[NUMSTATES] = {\n"
                  "    { 0, 0, -1, NULL, S_NULL, 0 },\n");
    for (int i = 0; i < count; ++i) {
        const info_entry_t *entry = &entries[i];
        /* One-tic polling preserves the authored engine cooldown without
           inventing a native BIM attack animation or another firing delay. */
        fprintf(file, "    { SPR_%s, 0, %d, %s, S_%s_STND, 0 },\n",
                entry->sprite, entry->damage ? 1 : -1,
                entry->damage ? "A_Look" : "NULL", entry->sprite);
        if (entry->damage)
            fprintf(file, "    { SPR_%s, 0, 1, A_Attack, S_%s_STND, 3 },\n",
                    entry->sprite, entry->sprite);
    }
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
        "    { .style = %s },\n    NULL,\n};\n", selection_style);
    return fclose(file) == 0;
}

#endif
