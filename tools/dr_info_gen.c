/* Generate Dark Reign's Doom-style state and mobjinfo tables.
   Animation data derived from OpenDR sequences YAML at revision
   98079a904746440433795fe7f21c4b35eb6b3959.  Sprite names validated
   against retail DEFTXT. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

/* Animation sequence from OpenDR sequences YAML.
 * run_start/shoot_start/idle_start are logical RSPR animation steps
 * (OpenDR Start / Facings).  -1 means the sequence is absent. */
typedef struct {
    int facings;
    int run_start,   run_len,   run_tick;
    int shoot_start, shoot_len, shoot_tick;
    int stand_start;
    int idle_start,  idle_len;
} dr_anim_t;

typedef struct {
    const char *type;
    const char *sprite;
    const char *asset;
    const char *doomednum;
    int health, speed, radius, height, mass, damage;
    const char *flags;
    dr_anim_t anim;
} dr_entry_t;

/* ANIM(facings, run_start,run_len,run_tick,
 *            shoot_start,shoot_len,shoot_tick,
 *            stand_start, idle_start,idle_len) */
#define ANIM(f,rs,rl,rt, ss,sl,st, stnd, is,il) \
    { (f), (rs),(rl),(rt), (ss),(sl),(st), (stnd), (is),(il) }

/* Buildings get a trivial static anim: no walk/shoot, stand at frame 0. */
#define BLDANIM { 1, -1,0,0, -1,0,0, 0, -1,0 }

#define MOBILE(type, sprite, actor, hp, speed, damage, extra, anim_data) \
    { type, sprite, sprite ".spr", actor, hp, speed, 16, 32, 100, damage, \
      "MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE" extra, anim_data }
#define BUILDING(type, sprite, actor, hp, damage, extra) \
    { type, sprite, sprite ".spr", actor, hp, 0, 0, 0, 0, damage, \
      "MF_SELECTABLE|MF_RENDERABLE" extra, BLDANIM }

/* OpenDR Tick → engine tics.  Tick is approximately 1/100 s; engine runs at
 * ~15 tics/s, so divide by 15 and clamp to at least 2 tics per frame. */
static int tics_from_tick(int tick) {
    if (tick <= 0) return 3;
    int t = tick / 15;
    return t > 1 ? t : 2;
}

static bool has_attack_flag(const dr_entry_t *entry) {
    return strstr(entry->flags, "MF_ATTACK") != NULL;
}

static bool has_fire_state(const dr_entry_t *entry) {
    return has_attack_flag(entry) && entry->damage > 0 &&
           entry->anim.shoot_start < 0;
}

/* ------------------------------------------------------------------ entries */
/* Animation data sourced from OpenDR sequences/units.yaml and
 * sequences/structures.yaml, pinned revision 98079a9.
 *
 * Formula: logical_step = OpenDR_Start / Facings.
 * shoot_start is set to -1 when the shoot cycle is identical to the run
 * cycle (same frames); in that case missilestate falls back to seestate. */

static const dr_entry_t entries[] = {
    /* --- Freedom Guard infantry --- */
    MOBILE("FG_CONSTRUCTION_CREW", "UCFCNST0", "ACTOR_FG_CONSTRUCTION_CREW",
           100, 6, 20, "|MF_ATTACK",
           /* run: Start=0,F=8,L=6,T=60  stand:Start=48/F=6  no shoot/idle */
           ANIM(8,  0,6,60,  -1,0,0,  6,  -1,0)),

    MOBILE("FG_FREIGHTER", "UCFRGST0", "ACTOR_FG_GROUND_TRANSPORTER",
           750, 5, 0, "|MF_HARVESTER",
           /* run: Start=0,F=16,L=3,T=100  stand at step 0 (Stride=3) */
           ANIM(16, 0,3,100, -1,0,0,  0,  -1,0)),

    MOBILE("FG_HOVER_FREIGHTER", "UCHFRST0", "ACTOR_FG_HOVER_TRANSPORTER",
           500, 5, 11, "|MF_HARVESTER|MF_ATTACK",
           /* single-frame body (F=16,L=1) */
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    MOBILE("FG_RAIDER", "UFRADST0", "ACTOR_FG_RAIDER",
           100, 5, 11, "|MF_ATTACK",
           /* run:0..5  shoot:6..7  idle:8..14  stand:15 */
           ANIM(16, 0,6,25,  6,2,25,  15,  8,7)),

    MOBILE("FG_MERCENARY", "UFMRCST0", "ACTOR_FG_MERCENARY",
           125, 5, 11, "|MF_ATTACK",
           /* run:0..5  shoot:6..7  idle:8..12  stand:18 */
           ANIM(16, 0,6,25,  6,2,25,  18,  8,5)),

    MOBILE("FG_SNIPER", "UFSNPST0", "ACTOR_FG_SNIPER",
           100, 5, 150, "|MF_ATTACK",
           /* run:0..5  shoot:9..12 (Start=144/16)  idle/stand:30 (Start=480/16) */
           ANIM(16, 0,6,100, 9,4,100, 30,  30,1)),

    MOBILE("FG_SCOUT", "UFSCTST0", "ACTOR_FG_SCOUT",
           66, 6, 0, "",
           /* run:0..7  shoot:8..10 (Start=64/8)  idle/stand:19 (Start=152/8) */
           ANIM(8,  0,8,100, 8,3,100, 19,  19,1)),

    MOBILE("FG_MEDIC", "UFMEDST0", "ACTOR_FG_MEDIC",
           66, 5, 0, "",
           /* run:0..5  shoot:6..15 (Start=48/8,L=10)  idle:16..21  stand:22 */
           ANIM(8,  0,6,100, 6,10,100, 22,  16,6)),

    MOBILE("FG_SABOTEUR", "UFSABST0", "ACTOR_FG_SABOTEUR",
           100, 5, 0, "",
           /* no OpenDR entry; approximate from mechanic (same infantry class) */
           ANIM(8,  0,8,100, -1,0,0,  8,  -1,0)),

    MOBILE("FG_MECHANIC", "UFMECST0", "ACTOR_FG_MECHANIC",
           66, 5, 0, "",
           /* run:0..7  shoot:8..15 (Start=64/8)  stand:24 (Start=192/8) */
           ANIM(8,  0,8,100, 8,8,100, 24,  -1,0)),

    MOBILE("FG_MARTYR", "UFMTRST0", "ACTOR_FG_SUICIDE_NUKER",
           100, 5, 180, "|MF_ATTACK",
           /* run:0..5  shoot:6..21 (Start=48/8,L=16)  idle:22..25  stand:25 */
           ANIM(8,  0,6,100, 6,16,100, 25,  22,4)),

    MOBILE("FG_SPY", "UCINFST0", "ACTOR_FG_SPY",
           66, 5, 0, "",
           /* run:0..7  no shoot  idle/stand:8..10 (Start=64/8,L=3) */
           ANIM(8,  0,8,100, -1,0,0,  8,  8,3)),

    /* --- Freedom Guard vehicles --- */
    MOBILE("FG_SPYDER_BIKE", "UFSPBST0", "ACTOR_FG_SPYDER_BIKE",
           133, 7, 10, "|MF_ATTACK",
           /* run:0..2  shoot:3 (Start=48/16)  stand:4 (Start=64/16) */
           ANIM(16, 0,3,100, 3,1,100,  4,  -1,0)),

    MOBILE("FG_IFV", "UFRATST0", "ACTOR_FG_IFV",
           200, 5, 0, "",
           /* run:0..2  no shoot (transport)  stand at step 0 */
           ANIM(16, 0,3,100, -1,0,0,  0,  -1,0)),

    MOBILE("FG_MEDIUM_TANK", "UFSKTST0", "ACTOR_FG_MEDIUM_TANK",
           133, 4, 14, "|MF_ATTACK",
           /* run/shoot identical 0..2; shoot=-1 so missilestate=seestate */
           ANIM(16, 0,3,100, -1,0,0,  0,  -1,0)),

    MOBILE("FG_TANK_HUNTER", "UFTHNST0", "ACTOR_FG_TANK_HUNTER",
           150, 4, 60, "|MF_ATTACK",
           /* run:0..2  shoot:3..6 (Start=48/16,L=4)  stand at step 0 */
           ANIM(16, 0,3,100, 3,4,100,  0,  -1,0)),

    MOBILE("FG_PHASE_TANK", "UFPHTST0", "ACTOR_FG_PHASE_TANK",
           166, 4, 30, "|MF_ATTACK",
           /* run:0..2  shoot:3 (Start=48/16,L=1)  stand at step 0 */
           ANIM(16, 0,3,100, 3,1,100,  0,  -1,0)),

    MOBILE("FG_MAD", "UFFLKST0", "ACTOR_FG_MAD",
           100, 4, 8, "|MF_ATTACK",
           /* 8 facings: run:0..7  shoot:8..10 (Start=64/8)  stand at step 0 */
           ANIM(8,  0,8,50,  8,3,100,  0,  -1,0)),

    MOBILE("FG_TRIPLE_RAIL_TANK", "UFTRTST0", "ACTOR_FG_TRIPLE_RAIL_TANK",
           200, 4, 24, "|MF_ATTACK",
           /* single-frame body; turret is a separate overlay sprite */
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    MOBILE("FG_SPA", "UFFARST0", "ACTOR_FG_SPA",
           133, 4, 30, "|MF_ATTACK",
           /* run:0..2  shoot:3..4 (Start=48/16,L=2)  stand at step 0 */
           ANIM(16, 0,3,100, 3,2,100,  0,  -1,0)),

    MOBILE("FG_SKY_BIKE", "UFSKBST0", "ACTOR_FG_SKY_BIKE",
           100, 6, 10, "|MF_ATTACK|MF_FLY",
           /* single-frame body */
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    MOBILE("FG_OUTRIDER", "UFOUTST0", "ACTOR_FG_OUTRIDER",
           200, 5, 20, "|MF_ATTACK|MF_FLY",
           /* single-frame body */
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    MOBILE("FG_SHOCKWAVE", "UFSWVST0", "ACTOR_FG_SHOCKWAVE",
           166, 4, 17, "|MF_ATTACK",
           /* run/shoot identical 0..2; shoot=-1 so missilestate=seestate */
           ANIM(16, 0,3,100, -1,0,0,  0,  -1,0)),

    MOBILE("FG_CONTAMINATOR", "UCWCOST0", "ACTOR_FG_CONTAMINATOR",
           166, 3, 5, "|MF_ATTACK",
           /* run/shoot identical 0..3; shoot=-1 so missilestate=seestate */
           ANIM(16, 0,4,100, -1,0,0,  0,  -1,0)),

    MOBILE("FG_UNDERGROUND_TUNNEL", "UFPHRST0", "ACTOR_FG_UNDERGROUND_TUNNEL",
           150, 5, 0, "",
           /* run:0..2  no shoot  stand at step 0 */
           ANIM(16, 0,3,100, -1,0,0,  0,  -1,0)),

    MOBILE("FG_BASE_MOVER", "UFBAMST0", "ACTOR_FG_BASE_MOVER",
           500, 3, 0, "",
           /* no OpenDR data; conservative single-frame default */
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    /* --- Freedom Guard buildings --- */
    BUILDING("FG_HQ1",               "NFHQT1L0", "ACTOR_FG_HEADQUARTERS_1",      1200,  0, "|MF_RESOURCE_BASE"),
    BUILDING("FG_HQ2",               "NFHQT2L0", "ACTOR_FG_HEADQUARTERS_2",       2400,  0, ""),
    BUILDING("FG_HQ3",               "NFHQT3L0", "ACTOR_FG_HEADQUARTERS_3",       3600,  0, ""),
    BUILDING("FG_BARRACKS",          "NFUTF1L0", "ACTOR_FG_TRAINING_FACILITY_1",   750,  0, ""),
    BUILDING("FG_ADV_BARRACKS",      "NFUTF2L0", "ACTOR_FG_TRAINING_FACILITY_2",  1500,  0, ""),
    BUILDING("FG_VEHICLE_FACTORY",   "NFVCY1L0", "ACTOR_FG_VEHICLE_FACTORY_1",    1000,  0, ""),
    BUILDING("FG_ADV_VEHICLE_FACTORY","NFVCY2L0","ACTOR_FG_VEHICLE_FACTORY_2",    2000,  0, ""),
    BUILDING("FG_HOVER_FACTORY",     "NFHSP1L0", "ACTOR_FG_HOVER_FACTORY",         600,  0, ""),
    BUILDING("FG_REPAIR_BAY",        "NFREP1L0", "ACTOR_FG_REPAIR_BAY",            600,  0, ""),
    BUILDING("FG_PHASE_FACTORY_1",   "NFPHF1L0", "ACTOR_FG_PHASE_FACTORY_1",      1000,  0, ""),
    BUILDING("FG_PHASE_FACTORY_2",   "NFPHF2L0", "ACTOR_FG_PHASE_FACTORY_2",      2000,  0, ""),
    BUILDING("FG_CAMERA_TOWER",      "NCCAM1L0", "ACTOR_FG_CAMERA_TOWER",          150,  0, ""),
    BUILDING("FG_LIFE_PLANT",        "NCLNC1L0", "ACTOR_FG_LIFE_PLANT",           1300,  0, ""),
    BUILDING("FG_POWER_PLANT",       "NCPOW1L0", "ACTOR_FG_POWER_PLANT",          1450,  0, ""),
    BUILDING("FG_REFINERY",          "NFRRM1L0", "ACTOR_FG_REFINERY",              800,  0, ""),
    BUILDING("FG_BRIDGE_H",          "NCSBH1L0", "ACTOR_FG_SMALL_HORIZONTAL_BRIDGE",400, 0, ""),
    BUILDING("FG_BRIDGE_V",          "NCSBV1L0", "ACTOR_FG_SMALL_VERTICAL_BRIDGE", 400,  0, ""),
    BUILDING("FG_BRIDGE_C",          "NCSBC1L0", "ACTOR_FG_SMALL_CENTRE_BRIDGE",   400,  0, ""),
    BUILDING("FG_GUARD_TOWER",       "NFGDT1L0", "ACTOR_FG_GUARD_TOWER",           400, 10, "|MF_ATTACK"),
    BUILDING("FG_ADV_GUARD_TOWER",   "NFAGT1L0", "ACTOR_FG_ADVANCED_GUARD_TOWER",  550, 13, "|MF_ATTACK"),
    BUILDING("FG_AA_SITE",           "NFAAR1L0", "ACTOR_FG_AA_SITE",               600, 40, "|MF_ATTACK"),
};

/* ------------------------------------------------------------------ writers */

static bool write_dr_info_h(const char *path, const dr_entry_t *entries, int count) {
    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f,
        "/* Generated from retail DEFTXT and the OpenDR sprite catalog. Do not edit by hand. */\n"
        "#ifndef __INFO__\n#define __INFO__\n\n#include \"actor.h\"\n\n"
        "typedef struct mobjinfo_s {\n"
        "    int doomednum;\n    int spawnstate;\n    int spawnhealth;\n"
        "    int seestate;\n    int seesound;\n    int reactiontime;\n"
        "    int attacksound;\n    int painstate;\n    int painchance;\n"
        "    int painsound;\n    int meleestate;\n    int missilestate;\n"
        "    int deathstate;\n    int xdeathstate;\n    int deathsound;\n"
        "    int speed;\n    int radius;\n    int height;\n    int mass;\n"
        "    int damage;\n    int activesound;\n    int flags;\n"
        "    int raisestate;\n    fixed_t spawnz;\n} mobjinfo_t;\n\n");

    /* spritenum_t */
    fprintf(f, "typedef enum {\n");
    for (int i = 0; i < count; ++i) fprintf(f, "    SPR_%s,\n", entries[i].sprite);
    fprintf(f, "    NUMSPRITES\n} spritenum_t;\n\n");

    /* statenum_t: STND + RUN* + SHOOT* + IDLE* per entry, then FIRE states. */
    fprintf(f, "typedef enum {\n    S_NULL = 0,\n");
    for (int i = 0; i < count; ++i) {
        const dr_entry_t *e = &entries[i];
        const dr_anim_t  *a = &e->anim;
        fprintf(f, "    S_%s_STND,\n", e->sprite);
        if (a->run_start >= 0 && a->run_len > 0)
            for (int s = 0; s < a->run_len; ++s)
                fprintf(f, "    S_%s_RUN%d,\n", e->sprite, s + 1);
        if (a->shoot_start >= 0 && a->shoot_len > 0)
            for (int s = 0; s < a->shoot_len; ++s)
                fprintf(f, "    S_%s_SHOOT%d,\n", e->sprite, s + 1);
        if (a->idle_start >= 0 && a->idle_len > 0)
            for (int s = 0; s < a->idle_len; ++s)
                fprintf(f, "    S_%s_IDLE%d,\n", e->sprite, s + 1);
    }
    for (int i = 0; i < count; ++i)
        if (has_fire_state(&entries[i]))
            fprintf(f, "    S_%s_FIRE,\n", entries[i].sprite);
    fprintf(f, "    NUMSTATES\n} statenum_t;\n\n");

    /* MT_ enum */
    fprintf(f, "enum {\n    MT_NULL,\n");
    for (int i = 0; i < count; ++i) fprintf(f, "    MT_%s,\n", entries[i].type);
    fprintf(f, "    NUMMOBJTYPES,\n};\n\n");

    fprintf(f,
        "extern const char *const sprnames[NUMSPRITES];\n"
        "extern const state_t states[NUMSTATES];\n"
        "extern const mobjinfo_t mobjinfo[NUMMOBJTYPES];\n"
        "extern const gameinfo_t game_info;\n\n#endif\n");

    return fclose(f) == 0;
}

static bool write_dr_info_c(const char *path, const dr_entry_t *entries, int count) {
    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f,
        "/* Generated from retail DEFTXT and the OpenDR sprite catalog. Do not edit by hand. */\n"
        "#include \"engine.h\"\n#include \"dr_types.h\"\n#include \"info.h\"\n\n");

    /* sprnames */
    fprintf(f, "const char *const sprnames[NUMSPRITES] = {\n");
    for (int i = 0; i < count; ++i) {
        fprintf(f, "    \"");
        for (const char *p = entries[i].asset; *p; ++p)
            fputc(tolower((unsigned char)*p), f);
        fprintf(f, "\",\n");
    }
    fprintf(f, "};\n\n");

    /* states */
    fprintf(f, "const state_t states[NUMSTATES] = {\n"
               "    { 0, 0, -1, NULL, S_NULL, 0 },\n");
    for (int i = 0; i < count; ++i) {
        const dr_entry_t *e = &entries[i];
        const dr_anim_t  *a = &e->anim;
        bool has_run   = a->run_start >= 0 && a->run_len > 0;
        bool has_shoot = a->shoot_start >= 0 && a->shoot_len > 0;
        bool has_idle  = a->idle_start >= 0 && a->idle_len > 0;
        bool attacking = has_attack_flag(e);

        /* Attacking actors periodically scan while standing. */
        fprintf(f, "    { SPR_%s, %d, %d, %s, S_%s_STND, 0 },\n",
                e->sprite, a->stand_start, attacking ? 20 : -1,
                attacking ? "A_Look" : "NULL", e->sprite);

        /* RUN states: cycle run_start .. run_start+run_len-1, then wrap */
        if (has_run) {
            int tics = tics_from_tick(a->run_tick);
            for (int s = 0; s < a->run_len; ++s) {
                if (s < a->run_len - 1)
                    fprintf(f, "    { SPR_%s, %d, %d, %s, S_%s_RUN%d, 2 },\n",
                            e->sprite, a->run_start + s, tics,
                            s == 0 && attacking ? "A_Chase" : "NULL",
                            e->sprite, s + 2);
                else
                    fprintf(f, "    { SPR_%s, %d, %d, %s, S_%s_RUN1, 2 },\n",
                            e->sprite, a->run_start + s, tics,
                            s == 0 && attacking ? "A_Chase" : "NULL",
                            e->sprite);
            }
        }

        /* SHOOT states: play once, return to STND */
        if (has_shoot) {
            int tics = tics_from_tick(a->shoot_tick);
            for (int s = 0; s < a->shoot_len; ++s) {
                if (s < a->shoot_len - 1)
                    fprintf(f, "    { SPR_%s, %d, %d, %s, S_%s_SHOOT%d, 3 },\n",
                            e->sprite, a->shoot_start + s, tics,
                            s == 0 && attacking ? "A_Attack" : "NULL",
                            e->sprite, s + 2);
                else
                    fprintf(f, "    { SPR_%s, %d, %d, %s, S_%s_STND, 3 },\n",
                            e->sprite, a->shoot_start + s, tics,
                            s == 0 && attacking ? "A_Attack" : "NULL",
                            e->sprite);
            }
        }

        /* IDLE states: cycle idle_start .. idle_start+idle_len-1, then wrap */
        if (has_idle) {
            for (int s = 0; s < a->idle_len; ++s) {
                if (s < a->idle_len - 1)
                    fprintf(f, "    { SPR_%s, %d, 10, NULL, S_%s_IDLE%d, 0 },\n",
                            e->sprite, a->idle_start + s, e->sprite, s + 2);
                else
                    fprintf(f, "    { SPR_%s, %d, 10, NULL, S_%s_IDLE1, 0 },\n",
                            e->sprite, a->idle_start + s, e->sprite);
            }
        }
    }
    for (int i = 0; i < count; ++i) {
        const dr_entry_t *e = &entries[i];
        if (has_fire_state(e))
            fprintf(f, "    { SPR_%s, %d, 1, A_Attack, S_%s_STND, 3 },\n",
                    e->sprite, e->anim.stand_start, e->sprite);
    }
    fprintf(f, "};\n\n");

    /* mobjinfo */
    fprintf(f, "const mobjinfo_t mobjinfo[NUMMOBJTYPES] = {\n    { 0 },\n");
    for (int i = 0; i < count; ++i) {
        const dr_entry_t *e = &entries[i];
        const dr_anim_t  *a = &e->anim;
        bool has_run   = a->run_start >= 0 && a->run_len > 0;
        bool has_shoot = a->shoot_start >= 0 && a->shoot_len > 0;

        fprintf(f, "    { // MT_%s\n"
                   "        .doomednum = %s, .spawnstate = S_%s_STND, .spawnhealth = %d,\n",
                e->type, e->doomednum, e->sprite, e->health);

        if (e->speed) {
            if (has_run)
                fprintf(f, "        .seestate = S_%s_RUN1, .speed = %d,\n",
                        e->sprite, e->speed);
            else
                fprintf(f, "        .seestate = S_%s_STND, .speed = %d,\n",
                        e->sprite, e->speed);
        }

        if (e->damage) {
            if (has_shoot)
                fprintf(f, "        .missilestate = S_%s_SHOOT1, .damage = %d,\n",
                        e->sprite, e->damage);
            else if (has_fire_state(e))
                fprintf(f, "        .missilestate = S_%s_FIRE, .damage = %d,\n",
                        e->sprite, e->damage);
            else if (has_run)
                /* shoot cycle same as run or absent; use run animation */
                fprintf(f, "        .missilestate = S_%s_RUN1, .damage = %d,\n",
                        e->sprite, e->damage);
            else
                fprintf(f, "        .missilestate = S_%s_STND, .damage = %d,\n",
                        e->sprite, e->damage);
        }

        fprintf(f, "        .deathstate = S_NULL, .xdeathstate = S_NULL,\n");
        if (e->radius || e->height || e->mass)
            fprintf(f, "        .radius = %d, .height = %d, .mass = %d,\n",
                    e->radius, e->height, e->mass);
        fprintf(f, "        .flags = %s,\n    },\n", e->flags);
    }
    fprintf(f, "};\n\n"
               "const gameinfo_t game_info = {\n"
               "    sprnames, NUMSPRITES, states, NUMSTATES, mobjinfo, NUMMOBJTYPES,\n"
               "    S_NULL, RTS_STATE_COORDS_GROUND_OFFSET,\n"
               "    { .style = SELECTION_STYLE_BRACKETS },\n    NULL,\n"
               "    .right_click_orders = false,\n};\n");

    return fclose(f) == 0;
}

/* ------------------------------------------------------------------ main */

static char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *text = malloc((size_t)size + 1);
    if (!text || fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text);
        fclose(file);
        return NULL;
    }
    text[size] = '\0';
    fclose(file);
    return text;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: dr_info_gen <dark-reign-root> <info.h> <info.c>\n");
        return 1;
    }

    /* Validate every sprite name against the retail DEFTXT files. */
    char path[1024];
    snprintf(path, sizeof(path), "%s/deftxt/UNITS.TXT", argv[1]);
    char *units = read_text(path);
    snprintf(path, sizeof(path), "%s/deftxt/BUILD.TXT", argv[1]);
    char *buildings = read_text(path);
    if (!units || !buildings) {
        fprintf(stderr, "dr_info_gen: cannot read retail DEFTXT under %s: %s\n",
                argv[1], strerror(errno));
        free(units);
        free(buildings);
        return 1;
    }

    int count = (int)(sizeof(entries) / sizeof(*entries));
    for (int i = 0; i < count; ++i) {
        char lower[32];
        snprintf(lower, sizeof(lower), "%s", entries[i].asset);
        for (char *p = lower; *p; ++p)
            if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
        if (!strstr(units, lower) && !strstr(buildings, lower)) {
            fprintf(stderr, "dr_info_gen: native definitions do not reference %s\n", lower);
            free(units);
            free(buildings);
            return 1;
        }
    }
    free(units);
    free(buildings);

    if (!write_dr_info_h(argv[2], entries, count) ||
        !write_dr_info_c(argv[3], entries, count)) {
        fprintf(stderr, "dr_info_gen: cannot write output: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
