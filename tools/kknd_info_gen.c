/* Generate games/kknd/info.h and games/kknd/info.c from data/KKND/UNITS.CFG.
   MOBD sprite indices are read from a built-in table derived from the
   OpenKKnD reverse-engineering project (github.com/wdigger/OpenKKND).

   Usage:  kknd_info_gen  data/KKND/UNITS.CFG  games/kknd/info.h  games/kknd/info.c
*/
#define _DEFAULT_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ── MOBD index table ─────────────────────────────────────────────────────── */
/* Derived from wdigger/OpenKKND src/kknd.h MOBD_ID enum (hex values are
   MOBD member indices in LEVELS/640/SPRITES.LVL).                           */
typedef struct { const char *cfg_name; int mobd; int unit_stats_id; } mobd_entry_t;

static const mobd_entry_t MOBD_TABLE[] = {
    /* Survivor infantry */
    { "UNIT_SURV_INFANTRY",       34,  0  },   /* MOBD_SURV_RIFLEMAN       */
    { "UNIT_SURV_FLAMER",         25,  2  },   /* MOBD_SURV_FLAMER         */
    { "UNIT_SURV_SWAT",           76,  4  },   /* MOBD_SURV_SWAT           */
    { "UNIT_SURV_SAPPER",         63,  6  },   /* MOBD_SURV_SAPPER         */
    { "UNIT_SURV_SABOTEUR",       62,  10 },   /* MOBD_SURV_SABOTEUR       */
    { "UNIT_SURV_TECHNICIAN",     78,  12 },   /* MOBD_SURV_TECHNICIAN     */
    { "UNIT_SURV_ROCKETLAUNCHER", 59,  14 },   /* MOBD_SURV_RPG_LAUNCHER   */
    { "UNIT_SURV_SNIPER",         71,  16 },   /* MOBD_SURV_SNIPER         */
    /* Mutant infantry */
    { "UNIT_MUTE_BERSERKER",      5,   1  },   /* MOBD_MUTE_BERSERKER      */
    { "UNIT_MUTE_PYRO",           55,  3  },   /* MOBD_MUTE_PYROMANIAC     */
    { "UNIT_MUTE_SHOTGUNNER",     68,  5  },   /* MOBD_MUTE_SHOTGUNNER     */
    { "UNIT_MUTE_RIOTER",         58,  7  },   /* MOBD_MUTE_RIOTER         */
    { "UNIT_MUTE_VANDAL",         81,  11 },   /* MOBD_MUTE_VANDAL         */
    { "UNIT_MUTE_TECHNICIAN",     41,  13 },   /* MOBD_MUTE_MEKANIK        */
    { "UNIT_MUTE_ROCKETLAUNCHER", 60,  15 },   /* MOBD_MUTE_BAZOOKA        */
    { "UNIT_MUTE_CRAZYHARRY",     31,  17 },   /* MOBD_MUTE_CRAZY_HARRY    */
    /* Survivor vehicles */
    { "UNIT_SURV_BIKE",           7,   26 },   /* MOBD_SURV_DIRT_BIKE      */
    { "UNIT_SURV_PICKUP",         54,  28 },   /* MOBD_SURV_4X4_PICKUP     */
    { "UNIT_SURV_ATV",            1,   30 },   /* MOBD_SURV_ATV            */
    { "UNIT_SURV_FLAMEATV",       24,  32 },   /* MOBD_SURV_ATV_FLAMETHROWER */
    { "UNIT_SURV_ANACONDA",       77,  34 },   /* MOBD_SURV_ANACONDA_TANK  */
    { "UNIT_SURV_BARAGECRAFT",    2,   36 },   /* MOBD_SURV_BARRAGE_CRAFT  */
    { "UNIT_SURV_CANNONTANK",     11,  38 },   /* MOBD_SURV_AUTOCANNON_TANK */
    { "UNIT_SURV_DERRICK",        65,  21 },   /* MOBD_SURV_MOBILE_DERRICK */
    { "UNIT_SURV_TANKER",         73,  23 },   /* MOBD_SURV_OIL_TANKER     */
    { "UNIT_SURV_MOBILE_BASE",    53,  40 },   /* MOBD_SURV_MOBILE_OUTPOST */
    /* Mutant vehicles */
    { "UNIT_MUTE_WOLF",           19,  27 },   /* MOBD_MUTE_DIRE_WOLF      */
    { "UNIT_MUTE_SIDECAR",        70,  29 },   /* MOBD_MUTE_BIKE_SIDECAR   */
    { "UNIT_MUTE_MONTRUCK",       47,  31 },   /* MOBD_MUTE_MONSTER_TRUCK  */
    { "UNIT_MUTE_SCORPION",       64,  33 },   /* MOBD_MUTE_GIANT_SCORPION */
    { "UNIT_MUTE_MASTODON",       38,  35 },   /* MOBD_MUTE_WAR_MASTADONT  */
    { "UNIT_MUTE_BEETLE",         4,   37 },   /* MOBD_MUTE_GIANT_BEETLE   */
    { "UNIT_MUTE_CRAB",           16,  39 },   /* MOBD_MUTE_MISSILE_CRAB   */
    { "UNIT_MUTE_DERRICK",        39,  22 },   /* MOBD_MUTE_MOBILE_DERRICK */
    { "UNIT_MUTE_TANKER",         48,  24 },   /* MOBD_MUTE_OIL_TANKER     */
    { "UNIT_MUTE_MOBILE_BASE",    14,  41 },   /* MOBD_MUTE_CLANHALL_WAGON */
    /* Survivor buildings */
    { "UNIT_SURV_DRILLRIG",       75,  46 },   /* MOBD_SURV_DRILLRIG       */
    { "UNIT_SURV_POWERPLANT",     74,  48 },   /* MOBD_SURV_POWER_STATION  */
    { "UNIT_SURV_OUTPOST",        52,  58 },   /* MOBD_SURV_OUTPOST        */
    { "UNIT_SURV_MACHINESHOP",    37,  60 },   /* MOBD_SURV_MACHINE_SHOP   */
    { "UNIT_SURV_REPAIRBAY",      56,  63 },   /* MOBD_SURV_REPAIR_BAY     */
    { "UNIT_SURV_RESEARCHLAB",    57,  65 },   /* MOBD_SURV_RESEARCH_LAB   */
    /* Mutant buildings */
    { "UNIT_MUTE_DRILLRIG",       50,  47 },   /* MOBD_MUTE_DRILLRIG       */
    { "UNIT_MUTE_POWERPLANT",     49,  49 },   /* MOBD_MUTE_POWER_STATION  */
    { "UNIT_MUTE_CLANHALL",       13,  59 },   /* MOBD_MUTE_CLANHALL       */
    { "UNIT_MUTE_BLACKSMITH",     8,   61 },   /* MOBD_MUTE_BLACKSMITH     */
    { "UNIT_MUTE_BEASTENCLOSURE", 3,   62 },   /* MOBD_MUTE_BEAST_ENCLOSURE */
    { "UNIT_MUTE_MENAGERIE",      42,  64 },   /* MOBD_MUTE_MENAGERIE      */
    { "UNIT_MUTE_ALCHEMYHALL",    0,   66 },   /* MOBD_MUTE_ALCHEMY_HALL   */
    /* Survivor towers */
    { "UNIT_SURV_GUARDTOWER",     67,  52 },   /* MOBD_SURV_GUARD_TOWER    */
    { "UNIT_SURV_MISSILEBATTERY", 44,  56 },   /* MOBD_SURV_MISSILE_BATTERY */
    { "UNIT_SURV_CANNONTOWER",    12,  54 },   /* MOBD_SURV_CANNON_TOWER   */
    /* Mutant towers */
    { "UNIT_MUTE_MACHGUNNEST",    43,  53 },   /* MOBD_MUTE_MACHINEGUN_NEST */
    { "UNIT_MUTE_GRAPESHOT",      29,  55 },   /* MOBD_MUTE_GRAPESHOT_TOWER */
    { "UNIT_MUTE_ROTARYCANNON",   61,  57 },   /* MOBD_MUTE_ROTARY_CANNON  */
    /* Air */
    { "UNIT_SURV_BOMBER",         83,  44 },   /* MOBD_SURV_BOMBER         */
    { "UNIT_MUTE_WASP",           82,  43 },   /* MOBD_MUTE_WASP           */
    { NULL, 0, 0 }
};

static const mobd_entry_t *lookup_mobd(const char *cfg_name) {
    for (int i = 0; MOBD_TABLE[i].cfg_name; ++i)
        if (strcasecmp(MOBD_TABLE[i].cfg_name, cfg_name) == 0)
            return &MOBD_TABLE[i];
    return NULL;
}

/* ── UNITS.CFG parsing ────────────────────────────────────────────────────── */

/*  Column layout from UNITS.CFG:
    name, cost, prod, hitpts, speed, reload, reload2, volley, tspeed,
    range, acc, i-dmg, v-dmg, b-dmg
    Fields after hitpts may be absent for buildings.                         */

#define MAX_UNITS 256

typedef struct {
    char name[64];
    int  hitpts;
    int  speed;     /* in original pixels/s; 0 for buildings */
    int  i_dmg;     /* infantry damage; 0 if not a combat unit */
    int  is_mobile;
    int  is_combat;
    int  is_harvester;
    int  is_flyer;
    int  is_building;
    int  mobd;
    int  unit_stats_id;
    /* Derived short name for C identifiers: SURV_INFANTRY → SURV_RIFLEMAN etc. */
    char spr_suffix[48];   /* e.g. "SURV_RIFLEMAN" */
    char mt_name[64];      /* e.g. "MT_SURV_RIFLEMAN" */
    char spr_name[64];     /* e.g. "SPR_SURV_RIFLEMAN" */
    char state_name[64];   /* e.g. "S_SURV_RIFLEMAN_STND" */
} unit_t;

static unit_t g_units[MAX_UNITS];
static int    g_unit_count = 0;

/* Convert "UNIT_SURV_INFANTRY" → "SURV_INFANTRY" (strip "UNIT_" prefix). */
static void strip_unit_prefix(const char *cfg, char *out, size_t n) {
    if (strncasecmp(cfg, "UNIT_", 5) == 0) cfg += 5;
    strncpy(out, cfg, n - 1);
    out[n - 1] = 0;
    for (char *p = out; *p; ++p) *p = (char)toupper((unsigned char)*p);
}

/* Determine category from cfg_name. */
static void classify_unit(unit_t *u) {
    const char *n = u->name;
    u->is_mobile    = (u->speed > 0);
    u->is_combat    = (u->i_dmg > 0);
    u->is_flyer     = (strstr(n, "BOMBER") || strstr(n, "WASP")) ? 1 : 0;
    u->is_harvester = (strstr(n, "TANKER") || strstr(n, "DERRICK")) ? 1 : 0;
    u->is_building  = (!u->is_mobile) ? 1 : 0;

    /* Special cases */
    if (strstr(n, "MOBILE_BASE") || strstr(n, "OUTPOST") || strstr(n, "CLANHALL_WAGON"))
        u->is_harvester = 0;
    if (strstr(n, "DRILLRIG"))
        u->is_harvester = 0; /* drill rig is MF_RESOURCE_BASE, not harvester */
}

/* Parse UNITS.CFG, populating g_units. */
static int parse_units_cfg(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "kknd_info_gen: open %s: %s\n", path, strerror(errno));
        return 0;
    }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* Strip comments and leading whitespace. */
        char *sc = strchr(line, ';');
        if (sc) *sc = 0;
        char *p = line;
        while (*p && isspace((unsigned char)*p)) ++p;
        if (!*p) continue;
        if (strncmp(p, "UNIT_", 5) != 0) continue;

        /* Parse columns. */
        char cols[16][64];
        int ncols = 0;
        while (*p && ncols < 16) {
            while (*p && isspace((unsigned char)*p)) ++p;
            if (!*p) break;
            int j = 0;
            while (*p && !isspace((unsigned char)*p) && j < 63)
                cols[ncols][j++] = *p++;
            cols[ncols][j] = 0;
            ++ncols;
        }
        if (ncols < 1) continue;

        if (g_unit_count >= MAX_UNITS) {
            fprintf(stderr, "kknd_info_gen: too many units\n");
            break;
        }

        unit_t *u = &g_units[g_unit_count];
        memset(u, 0, sizeof(*u));
        strncpy(u->name, cols[0], sizeof(u->name) - 1);
        for (char *q = u->name; *q; ++q) *q = (char)toupper((unsigned char)*q);

        u->hitpts = (ncols > 3) ? atoi(cols[3]) : 0;
        u->speed  = (ncols > 4) ? atoi(cols[4]) : 0;
        u->i_dmg  = (ncols > 11) ? atoi(cols[11]) : 0;

        /* Look up MOBD index. */
        const mobd_entry_t *me = lookup_mobd(u->name);
        if (!me) {
            /* Not in our table — skip (mission-only units, etc.) */
            continue;
        }
        u->mobd         = me->mobd;
        u->unit_stats_id = me->unit_stats_id;

        /* Build C identifier suffixes. */
        char suffix[48];
        strip_unit_prefix(u->name, suffix, sizeof(suffix));
        snprintf(u->spr_suffix,  sizeof(u->spr_suffix),  "%s", suffix);
        snprintf(u->mt_name,     sizeof(u->mt_name),      "MT_%s", suffix);
        snprintf(u->spr_name,    sizeof(u->spr_name),     "SPR_%s", suffix);
        snprintf(u->state_name,  sizeof(u->state_name),   "S_%s_STND", suffix);

        classify_unit(u);
        ++g_unit_count;
    }
    fclose(f);
    return g_unit_count;
}

/* ── Write info.h ─────────────────────────────────────────────────────────── */

static void write_info_h(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "kknd_info_gen: open %s: %s\n", path, strerror(errno)); return; }

    fprintf(f,
        "/* Generated by tools/kknd_info_gen.c. Do not edit by hand.\n"
        "   Source data: data/KKND/UNITS.CFG + OpenKKnD MOBD_ID table. */\n"
        "#ifndef __INFO__\n"
        "#define __INFO__\n"
        "\n"
        "#include \"actor.h\"\n"
        "\n"
        "typedef struct mobjinfo_s {\n"
        "    int doomednum;\n"
        "    int spawnstate;\n"
        "    int spawnhealth;\n"
        "    int seestate;\n"
        "    int seesound;\n"
        "    int reactiontime;\n"
        "    int attacksound;\n"
        "    int painstate;\n"
        "    int painchance;\n"
        "    int painsound;\n"
        "    int meleestate;\n"
        "    int missilestate;\n"
        "    int deathstate;\n"
        "    int xdeathstate;\n"
        "    int deathsound;\n"
        "    int speed;\n"
        "    int radius;\n"
        "    int height;\n"
        "    int mass;\n"
        "    int damage;\n"
        "    int activesound;\n"
        "    int flags;\n"
        "    int raisestate;\n"
        "    fixed_t spawnz;\n"
        "} mobjinfo_t;\n"
        "\n");

    /* spritenum_t */
    fprintf(f, "typedef enum {\n");
    for (int i = 0; i < g_unit_count; ++i)
        fprintf(f, "    %s,\n", g_units[i].spr_name);
    fprintf(f, "    NUMSPRITES\n} spritenum_t;\n\n");

    /* statenum_t */
    fprintf(f, "typedef enum {\n    S_NULL = 0,\n");
    for (int i = 0; i < g_unit_count; ++i)
        fprintf(f, "    %s,\n", g_units[i].state_name);
    fprintf(f, "    NUMSTATES\n} statenum_t;\n\n");

    /* MT_ enum — doomednum = UNIT_STATS_* from OpenKKnD */
    fprintf(f,
        "/* doomednum values = UNIT_STATS_* indices from OpenKKnD UNIT_ID enum. */\n"
        "enum {\n"
        "    MT_NULL,\n");
    for (int i = 0; i < g_unit_count; ++i)
        fprintf(f, "    %s,  /* doomednum=%d */\n",
                g_units[i].mt_name, g_units[i].unit_stats_id);
    fprintf(f, "    NUMMOBJTYPES,\n};\n\n");

    /* externs */
    fprintf(f,
        "extern const char *const sprnames[NUMSPRITES];\n"
        "extern const state_t states[NUMSTATES];\n"
        "extern const mobjinfo_t mobjinfo[NUMMOBJTYPES];\n"
        "extern const gameinfo_t game_info;\n"
        "\n"
        "#endif\n");

    fclose(f);
}

/* ── Flags string ─────────────────────────────────────────────────────────── */

static void unit_flags(const unit_t *u, char *out, size_t n) {
    char buf[256] = "";
    if (!u->is_building) strcat(buf, "MF_SELECTABLE|");
    else                 strcat(buf, "MF_SELECTABLE|");   /* buildings too */
    if (u->is_mobile)  strcat(buf, "MF_MOBILE|");
    strcat(buf, "MF_RENDERABLE|");
    if (u->is_combat && !u->is_building) strcat(buf, "MF_ATTACK|");
    if (u->is_combat && u->is_building)  strcat(buf, "MF_ATTACK|");
    if (u->is_harvester)  strcat(buf, "MF_HARVESTER|");
    if (u->is_flyer)      strcat(buf, "MF_FLY|");
    /* Drill rigs are resource bases. */
    if (strstr(u->name, "DRILLRIG")) strcat(buf, "MF_RESOURCE_BASE|");
    /* Trim trailing pipe. */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '|') buf[len - 1] = 0;
    snprintf(out, n, "%s", buf);
}

/* ── Write info.c ─────────────────────────────────────────────────────────── */

static void write_info_c(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "kknd_info_gen: open %s: %s\n", path, strerror(errno)); return; }

    fprintf(f,
        "/* Generated by tools/kknd_info_gen.c. Do not edit by hand.\n"
        "   Source data: data/KKND/UNITS.CFG + OpenKKnD MOBD_ID table.\n"
        "   Sprite names are MOBD member indices into LEVELS/640/SPRITES.LVL. */\n"
        "#include \"engine.h\"\n"
        "#include \"info.h\"\n"
        "\n");

    /* sprnames[] */
    fprintf(f, "const char *const sprnames[NUMSPRITES] = {\n");
    for (int i = 0; i < g_unit_count; ++i)
        fprintf(f, "    \"%d\",  /* %s */\n", g_units[i].mobd, g_units[i].name);
    fprintf(f, "};\n\n");

    /* states[] */
    fprintf(f, "const state_t states[NUMSTATES] = {\n");
    fprintf(f, "    { 0, 0, -1, NULL, S_NULL, 0 },  /* S_NULL */\n");
    for (int i = 0; i < g_unit_count; ++i) {
        fprintf(f, "    { %s, 0, -1, NULL, %s, 0 },  /* %s */\n",
                g_units[i].spr_name, g_units[i].state_name, g_units[i].state_name);
    }
    fprintf(f, "};\n\n");

    /* mobjinfo[] */
    fprintf(f, "/* Stats from UNITS.CFG; doomednum = UNIT_STATS_* id. */\n");
    fprintf(f, "const mobjinfo_t mobjinfo[NUMMOBJTYPES] = {\n");
    fprintf(f, "    { // MT_NULL\n        0,\n    },\n");

    for (int i = 0; i < g_unit_count; ++i) {
        const unit_t *u = &g_units[i];
        char flags[256];
        unit_flags(u, flags, sizeof(flags));

        int radius = u->is_mobile ? (u->is_flyer ? 20 : (u->speed > 60 ? 12 : 16)) : 32;
        int height = u->is_mobile ? (u->is_flyer ? 16 : 24) : 40;
        int mass   = u->hitpts / 8;
        if (mass < 100) mass = 100;
        if (mass > 1000) mass = 1000;

        fprintf(f, "    { // %s  (%s)\n", g_units[i].mt_name, u->name);
        fprintf(f, "        .doomednum    = %d,\n", u->unit_stats_id);
        fprintf(f, "        .spawnstate   = %s,\n", u->state_name);
        fprintf(f, "        .spawnhealth  = %d,\n", u->hitpts);
        if (u->is_mobile)
            fprintf(f, "        .seestate     = %s,\n", u->state_name);
        if (u->is_combat)
            fprintf(f, "        .missilestate = %s,\n", u->state_name);
        fprintf(f, "        .deathstate   = S_NULL, .xdeathstate = S_NULL,\n");
        if (u->is_mobile)
            fprintf(f, "        .speed = %d, ", u->speed);
        else
            fprintf(f, "        ");
        fprintf(f, ".radius = %d, .height = %d, .mass = %d,\n",
                radius, height, mass);
        if (u->is_combat)
            fprintf(f, "        .damage = %d,\n", u->i_dmg);
        fprintf(f, "        .flags = %s,\n", flags);
        fprintf(f, "    },\n");
    }
    fprintf(f, "};\n\n");

    /* game_info */
    fprintf(f,
        "const gameinfo_t game_info = {\n"
        "    sprnames,\n"
        "    NUMSPRITES,\n"
        "    states,\n"
        "    NUMSTATES,\n"
        "    mobjinfo,\n"
        "    NUMMOBJTYPES,\n"
        "    S_NULL,\n"
        "    RTS_STATE_COORDS_GROUND_OFFSET,\n"
        "    { .style = SELECTION_STYLE_CIRCLE },\n"
        "    NULL,\n"
        "};\n");

    fclose(f);
}

/* ── Main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr,
            "kknd_info_gen: usage: kknd_info_gen <UNITS.CFG> <info.h> <info.c>\n"
            "  Generates games/kknd/info.h and info.c from KKnD unit configuration.\n");
        return 1;
    }

    const char *cfg_path   = argv[1];
    const char *info_h     = argv[2];
    const char *info_c     = argv[3];

    int n = parse_units_cfg(cfg_path);
    if (n == 0) {
        fprintf(stderr, "kknd_info_gen: no units parsed from %s\n", cfg_path);
        return 1;
    }
    fprintf(stderr, "kknd_info_gen: %d units parsed\n", n);

    write_info_h(info_h);
    fprintf(stderr, "kknd_info_gen: wrote %s\n", info_h);

    write_info_c(info_c);
    fprintf(stderr, "kknd_info_gen: wrote %s\n", info_c);

    return 0;
}
