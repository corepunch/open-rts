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
typedef struct {
    const char *cfg_name;
    const char *symbol;
    int mobd;
    int unit_stats_id;
} mobd_entry_t;

static const mobd_entry_t MOBD_TABLE[] = {
    /* Survivor infantry */
    { "UNIT_SURV_INFANTRY",       "SURV_RIFLEMAN",         34,  0  },
    { "UNIT_SURV_FLAMER",         "SURV_FLAMER",           25,  2  },
    { "UNIT_SURV_SWAT",           "SURV_SWAT",             76,  4  },
    { "UNIT_SURV_SAPPER",         "SURV_SAPPER",           63,  6  },
    { "UNIT_SURV_SABOTEUR",       "SURV_SABOTEUR",         62,  10 },
    { "UNIT_SURV_TECHNICIAN",     "SURV_TECHNICIAN",       78,  12 },
    { "UNIT_SURV_ROCKETLAUNCHER", "SURV_RPG_LAUNCHER",     59,  14 },
    { "UNIT_SURV_SNIPER",         "SURV_SNIPER",           71,  16 },
    /* Mutant infantry */
    { "UNIT_MUTE_BERSERKER",      "MUTE_BERSERKER",        5,   1  },
    { "UNIT_MUTE_PYRO",           "MUTE_PYROMANIAC",       55,  3  },
    { "UNIT_MUTE_SHOTGUNNER",     "MUTE_SHOTGUNNER",       68,  5  },
    { "UNIT_MUTE_RIOTER",         "MUTE_RIOTER",           58,  7  },
    { "UNIT_MUTE_VANDAL",         "MUTE_VANDAL",           81,  11 },
    { "UNIT_MUTE_TECHNICIAN",     "MUTE_MEKANIK",          41,  13 },
    { "UNIT_MUTE_ROCKETLAUNCHER", "MUTE_BAZOOKA",          60,  15 },
    { "UNIT_MUTE_CRAZYHARRY",     "MUTE_CRAZY_HARRY",      31,  17 },
    /* Survivor vehicles */
    { "UNIT_SURV_BIKE",           "SURV_DIRT_BIKE",        7,   26 },
    { "UNIT_SURV_PICKUP",         "SURV_4X4_PICKUP",       54,  28 },
    { "UNIT_SURV_ATV",            "SURV_ATV",              1,   30 },
    { "UNIT_SURV_FLAMEATV",       "SURV_ATV_FLAMETHROWER", 24,  32 },
    { "UNIT_SURV_ANACONDA",       "SURV_ANACONDA_TANK",    77,  34 },
    { "UNIT_SURV_BARAGECRAFT",    "SURV_BARRAGE_CRAFT",    2,   36 },
    { "UNIT_SURV_CANNONTANK",     "SURV_AUTOCANNON_TANK",  11,  38 },
    { "UNIT_SURV_DERRICK",        "SURV_MOBILE_DERRICK",   65,  21 },
    { "UNIT_SURV_TANKER",         "SURV_OIL_TANKER",       73,  23 },
    { "UNIT_SURV_MOBILE_BASE",    "SURV_MOBILE_OUTPOST",   53,  40 },
    /* Mutant vehicles */
    { "UNIT_MUTE_WOLF",           "MUTE_DIRE_WOLF",        19,  27 },
    { "UNIT_MUTE_SIDECAR",        "MUTE_BIKE_SIDECAR",     70,  29 },
    { "UNIT_MUTE_MONTRUCK",       "MUTE_MONSTER_TRUCK",    47,  31 },
    { "UNIT_MUTE_SCORPION",       "MUTE_GIANT_SCORPION",   64,  33 },
    { "UNIT_MUTE_MASTODON",       "MUTE_WAR_MASTADONT",    38,  35 },
    { "UNIT_MUTE_BEETLE",         "MUTE_GIANT_BEETLE",     4,   37 },
    { "UNIT_MUTE_CRAB",           "MUTE_MISSILE_CRAB",     16,  39 },
    { "UNIT_MUTE_DERRICK",        "MUTE_MOBILE_DERRICK",   39,  22 },
    { "UNIT_MUTE_TANKER",         "MUTE_OIL_TANKER",       48,  24 },
    { "UNIT_MUTE_MOBILE_BASE",    "MUTE_CLANHALL_WAGON",   14,  41 },
    /* Survivor buildings */
    { "UNIT_SURV_DRILLRIG",       "SURV_DRILLRIG",         75,  46 },
    { "UNIT_SURV_POWERPLANT",     "SURV_POWER_STATION",    74,  48 },
    { "UNIT_SURV_OUTPOST",        "SURV_OUTPOST",          52,  58 },
    { "UNIT_SURV_MACHINESHOP",    "SURV_MACHINE_SHOP",     37,  60 },
    { "UNIT_SURV_REPAIRBAY",      "SURV_REPAIR_BAY",       56,  63 },
    { "UNIT_SURV_RESEARCHLAB",    "SURV_RESEARCH_LAB",     57,  65 },
    /* Mutant buildings */
    { "UNIT_MUTE_DRILLRIG",       "MUTE_DRILLRIG",         50,  47 },
    { "UNIT_MUTE_POWERPLANT",     "MUTE_POWER_STATION",    49,  49 },
    { "UNIT_MUTE_CLANHALL",       "MUTE_CLANHALL",         13,  59 },
    { "UNIT_MUTE_BLACKSMITH",     "MUTE_BLACKSMITH",       8,   61 },
    { "UNIT_MUTE_BEASTENCLOSURE", "MUTE_BEAST_ENCLOSURE",  3,   62 },
    { "UNIT_MUTE_MENAGERIE",      "MUTE_MENAGERIE",        42,  64 },
    { "UNIT_MUTE_ALCHEMYHALL",    "MUTE_ALCHEMY_HALL",     0,   66 },
    /* Survivor towers */
    { "UNIT_SURV_GUARDTOWER",     "SURV_GUARD_TOWER",      67,  52 },
    { "UNIT_SURV_MISSILEBATTERY", "SURV_MISSILE_BATTERY",  44,  56 },
    { "UNIT_SURV_CANNONTOWER",    "SURV_CANNON_TOWER",     12,  54 },
    /* Mutant towers */
    { "UNIT_MUTE_MACHGUNNEST",    "MUTE_MACHINEGUN_NEST",  43,  53 },
    { "UNIT_MUTE_GRAPESHOT",      "MUTE_GRAPESHOT_TOWER",  29,  55 },
    { "UNIT_MUTE_ROTARYCANNON",   "MUTE_ROTARY_CANNON",    61,  57 },
    /* Air */
    { "UNIT_SURV_BOMBER",         "SURV_BOMBER",           83,  44 },
    { "UNIT_MUTE_WASP",           "MUTE_WASP",             82,  43 },
    { NULL, NULL, 0, 0 }
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

        const mobd_entry_t *me = lookup_mobd(cols[0]);
        if (!me) continue;
        unit_t *u = &g_units[me - MOBD_TABLE];
        memset(u, 0, sizeof(*u));
        strncpy(u->name, cols[0], sizeof(u->name) - 1);
        for (char *q = u->name; *q; ++q) *q = (char)toupper((unsigned char)*q);

        u->hitpts = (ncols > 3) ? atoi(cols[3]) : 0;
        u->speed  = (ncols > 4) ? atoi(cols[4]) : 0;
        u->i_dmg  = (ncols > 11) ? atoi(cols[11]) : 0;

        u->mobd         = me->mobd;
        u->unit_stats_id = me->unit_stats_id;

        snprintf(u->spr_suffix,  sizeof(u->spr_suffix),  "%s", me->symbol);
        snprintf(u->mt_name,     sizeof(u->mt_name),      "MT_%s", me->symbol);
        snprintf(u->spr_name,    sizeof(u->spr_name),     "SPR_%s", me->symbol);
        snprintf(u->state_name,  sizeof(u->state_name),   "S_%s_STND", me->symbol);

        classify_unit(u);
    }
    fclose(f);
    while (MOBD_TABLE[g_unit_count].cfg_name) {
        if (!g_units[g_unit_count].name[0]) {
            fprintf(stderr, "kknd_info_gen: missing %s in %s\n",
                    MOBD_TABLE[g_unit_count].cfg_name, path);
            return 0;
        }
        ++g_unit_count;
    }
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

static void unit_size(const unit_t *u, int *radius, int *height, int *mass) {
    int id = u->unit_stats_id;
    if (u->is_flyer) {
        *radius = 20; *height = 16; *mass = 200;
    } else if (!u->is_mobile) {
        *radius = *height = *mass = 0;
    } else if (id <= 17) {
        *radius = 12; *height = 24; *mass = 100;
    } else if (id == 21 || id == 22) {
        *radius = 24; *height = 32; *mass = 800;
    } else if (id == 23 || id == 24 || (id >= 36 && id <= 39)) {
        *radius = 24; *height = 32; *mass = 600;
    } else if (id == 26 || id == 27) {
        *radius = 16; *height = 24; *mass = 200;
    } else if (id == 28 || id == 29) {
        *radius = 16; *height = 24; *mass = 300;
    } else if (id >= 30 && id <= 33) {
        *radius = 20; *height = 28; *mass = 400;
    } else if (id == 34 || id == 35) {
        *radius = 24; *height = 32; *mass = 500;
    } else {
        *radius = 32; *height = 40; *mass = 1000;
    }
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

        int radius, height, mass;
        unit_size(u, &radius, &height, &mass);

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
            fprintf(f, "        .speed = %d, .radius = %d, .height = %d, .mass = %d,\n",
                    u->speed, radius, height, mass);
        else if (radius)
            fprintf(f, "        .radius = %d, .height = %d, .mass = %d,\n",
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
