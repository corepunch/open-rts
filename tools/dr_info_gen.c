/* Generate per-sprite .inc files and the statenum_t enum for Dark Reign's
   info.h, plus the complete info.c (sprnames, states with #include, mobjinfo,
   game_info).

   Reads the entries table and writes:
     <animate-dir>/<SPRITE>.inc  — one per sprite, designated-initializer rows
     <info.h>                    — updates only the statenum_t enum between markers
     <info.c>                    — complete file with #include per sprite

   Animation data derived from OpenDR sequences YAML at revision
   98079a904746440433795fe7f21c4b35eb6b3959.  Sprite names validated
   against retail DEFTXT.

   Usage:
     dr_info_gen <dark-reign-root> <info.h> <info.c> <animate-dir>
*/
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/stat.h>

/* Animation sequence from OpenDR sequences YAML.
 * run_start/shoot_start/idle_start are logical RSPR animation steps
 * (OpenDR Start / Facings).  -1 means the sequence is absent. */
typedef struct {
    int facings;
    int run_start,   run_len,   run_tick;
    int shoot_start, shoot_len, shoot_tick;
    int stand_start;
    int idle_start,  idle_len;
    int harvest_start, harvest_len, harvest_tick;
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
 *            stand_start, idle_start,idle_len).  Harvest is absent. */
#define ANIM(f,rs,rl,rt, ss,sl,st, stnd, is,il) \
    { (f), (rs),(rl),(rt), (ss),(sl),(st), (stnd), (is),(il), -1, 0, 0 }

/* Same fields plus harvest_start, harvest_len, harvest_tick.
 * harvest_start is the logical RSPR step (OpenDR Start / Facings). */
#define HARVEST_ANIM(f,rs,rl,rt, ss,sl,st, stnd, is,il, hs,hl,ht) \
    { (f), (rs),(rl),(rt), (ss),(sl),(st), (stnd), (is),(il), (hs),(hl),(ht) }

/* Buildings get a trivial static anim: no walk/shoot, stand at frame 0. */
#define BLDANIM { 1, -1,0,0, -1,0,0, 0, -1,0, -1,0,0 }

#define MOBILE(type, sprite, actor, hp, speed, damage, extra, anim_data) \
    { type, sprite, sprite ".spr", actor, hp, speed, 16, 32, 100, damage, \
      "MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE" extra, anim_data }
/* Shared-sprite variant: distinct state/sprite IDs, same retail asset.
 * Used when Imperium reuses a Freedom Guard body (e.g. ucfcnst0.spr). */
#define MOBILE_ASSET(type, sprite, asset, actor, hp, speed, damage, extra, anim_data) \
    { type, sprite, asset, actor, hp, speed, 16, 32, 100, damage, \
      "MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE" extra, anim_data }
#define BUILDING(type, sprite, actor, hp, damage, extra) \
    { type, sprite, sprite ".spr", actor, hp, 0, 0, 0, 0, damage, \
      "MF_SELECTABLE|MF_RENDERABLE" extra, BLDANIM }
#define BUILDING_ASSET(type, sprite, asset, actor, hp, damage, extra) \
    { type, sprite, asset, actor, hp, 0, 0, 0, 0, damage, \
      "MF_SELECTABLE|MF_RENDERABLE" extra, BLDANIM }

/* OpenDR Tick -> engine tics.  Tick is approximately 1/100 s; engine runs at
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

static int mkdir_p(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1;
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
           /* run: Start=0,F=16,L=3,T=100  stand at step 0 (Stride=3)
            * harvest: RSPR sect 1 anims 3..17, 16 facings (OpenDR Start=48) */
           HARVEST_ANIM(16, 0,3,100, -1,0,0,  0,  -1,0,  3,15,100)),

    MOBILE("FG_HOVER_FREIGHTER", "UCHFRST0", "ACTOR_FG_HOVER_TRANSPORTER",
           500, 5, 11, "|MF_HARVESTER|MF_ATTACK",
           /* run: single-frame body (F=16,L=1)
            * harvest: RSPR sect 1 anims 1..15, 16 facings */
           HARVEST_ANIM(16, 0,1,100, -1,0,0,  0,  -1,0,  1,15,100)),

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

    /* --- Imperium infantry (OpenDR sequences/units.yaml) --- */
    MOBILE("IMP_GUARDIAN", "UIGRDST0", "ACTOR_IMP_STRIKE_MARINE",
           100, 5, 11, "|MF_ATTACK",
           /* run:0..5  shoot:6..7 (Start=48/8)  stand/idle:8 (Start=64/8) */
           ANIM(8,  0,6,100,  6,2,100,  8,  8,1)),

    MOBILE("IMP_BION", "UIBONST0", "ACTOR_IMP_FIRE_SUPPORT_MARINE",
           150, 5, 18, "|MF_ATTACK",
           /* run:0..7  shoot:8..10 (Start=64/8)  idle:11..18  stand:25 */
           ANIM(8,  0,8,100,  8,3,100,  25,  11,8)),

    MOBILE("IMP_EXTERMINATOR", "UIEXTST0", "ACTOR_IMP_HOVER_MARINE",
           75, 5, 15, "|MF_ATTACK",
           /* run single-frame  sync with shoot Start=16/16 */
           ANIM(16, 0,1,100,  1,2,100,  0,  0,1)),

    /* --- Imperium vehicles --- */
    MOBILE("IMP_SCOUT_TANK", "UISTTST0", "ACTOR_IMP_SCOUT_TANK",
           150, 6, 10, "|MF_ATTACK",
           /* single-frame body; shoot identical to run */
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    MOBILE("IMP_ASSAULT_VEHICLE", "UIITTST0", "ACTOR_IMP_ASSAULT_VEHICLE",
           150, 5, 11, "|MF_ATTACK",
           /* single-frame body with separate turret overlay */
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    MOBILE("IMP_PLASMA_TANK", "UIPLTST0", "ACTOR_IMP_PLASMA_TANK",
           250, 4, 19, "|MF_ATTACK",
           /* single-frame body with separate turret overlay */
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    MOBILE("IMP_AMPER", "UIAMPST0", "ACTOR_IMP_AMPER",
           66, 5, 0, "",
           /* run:0..5  shoot:6..8  idle:9..11  stand:15 (Start=120/8) */
           ANIM(8,  0,6,100,  6,3,100,  15,  9,3)),

    MOBILE("IMP_MAD", "UIMADST0", "ACTOR_IMP_MAD",
           150, 4, 48, "|MF_ATTACK",
           /* single-frame body */
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    MOBILE("IMP_RECON_SAUCER", "UIRDRST0", "ACTOR_IMP_RECON_SAUCER",
           66, 6, 0, "|MF_FLY",
           /* recon only -- shoot strip is sensor sweep, not a weapon */
           ANIM(8,  0,1,100, -1,0,0,  0,  -1,0)),

    MOBILE("IMP_SHREDDER", "UISHRST0", "ACTOR_IMP_SHREDDER",
           100, 5, 10, "|MF_ATTACK",
           /* run/shoot identical 0..3; shoot=-1 so missilestate=seestate */
           ANIM(16, 0,4,100, -1,0,0,  0,  -1,0)),

    MOBILE("IMP_HOSTAGE_TAKER", "UIHOSST0", "ACTOR_IMP_HOSTAGE_TAKER",
           450, 4, 0, "",
           /* transport/ability unit -- no ranged weapon */
           ANIM(16, 0,3,100, -1,0,0,  0,  -1,0)),

    MOBILE("IMP_TACHYON_TANK", "UITCTST0", "ACTOR_IMP_TACHYON_TANK",
           410, 4, 30, "|MF_ATTACK",
           /* single-frame body */
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    MOBILE("IMP_SCARAB", "UIIARST0", "ACTOR_IMP_SPA",
           133, 4, 30, "|MF_ATTACK",
           /* run single-frame  shoot:1..3 (Start=16/16) */
           ANIM(16, 0,1,100,  1,3,100,  0,  -1,0)),

    MOBILE("IMP_CYCLONE", "UICYCST0", "ACTOR_IMP_VTOL",
           150, 6, 24, "|MF_ATTACK|MF_FLY",
           /* run single-frame  shoot:1 (Start=16/16) */
           ANIM(16, 0,1,100,  1,1,100,  0,  -1,0)),

    MOBILE("IMP_SKY_FORTRESS", "UISKYST0", "ACTOR_IMP_SKY_FORTRESS",
           266, 4, 650, "|MF_ATTACK|MF_FLY",
           /* run single-frame  shoot:0..2 */
           ANIM(8,  0,1,100,  0,3,100,  0,  -1,0)),

    /* --- Imperium shared-sprite units (retail reuses FG bodies) --- */
    MOBILE_ASSET("IMP_CONSTRUCTION_CREW", "UCFNST0_IMP", "ucfcnst0.spr",
           "ACTOR_IMP_CONSTRUCTION_CREW",
           100, 6, 20, "|MF_ATTACK",
           ANIM(8,  0,6,60,  -1,0,0,  6,  -1,0)),

    MOBILE_ASSET("IMP_GROUND_TRANSPORTER", "UCFRGST0_IMP", "ucfrgst0.spr",
           "ACTOR_IMP_GROUND_TRANSPORTER",
           750, 5, 0, "|MF_HARVESTER",
           HARVEST_ANIM(16, 0,3,100, -1,0,0,  0,  -1,0,  3,15,100)),

    MOBILE_ASSET("IMP_HOVER_TRANSPORTER", "UCHFRST0_IMP", "uchfrst0.spr",
           "ACTOR_IMP_HOVER_TRANSPORTER",
           500, 5, 11, "|MF_HARVESTER|MF_ATTACK",
           HARVEST_ANIM(16, 0,1,100, -1,0,0,  0,  -1,0,  1,15,100)),

    MOBILE_ASSET("IMP_SPY", "UCINFST0_IMP", "ucinfst0.spr",
           "ACTOR_IMP_SPY",
           66, 5, 0, "",
           ANIM(8,  0,8,100, -1,0,0,  8,  8,3)),

    MOBILE_ASSET("IMP_SUICIDE_ZOMBIE", "UFMTRST0_IMP", "ufmtrst0.spr",
           "ACTOR_IMP_SUICIDE_ZOMBIE",
           100, 5, 180, "|MF_ATTACK",
           ANIM(8,  0,6,100,  6,16,100,  25,  22,4)),

    MOBILE_ASSET("IMP_CONTAMINATOR", "UCWCOST0_IMP", "ucwcost0.spr",
           "ACTOR_IMP_CONTAMINATOR",
           166, 3, 5, "|MF_ATTACK",
           ANIM(16, 0,4,100, -1,0,0,  0,  -1,0)),

    /* --- Imperium decoy mobile units (share sprites with parent IMP units) --- */
    MOBILE_ASSET("IMP_ASSAULT_VEHICLE_DECOY", "UIITTST0_D", "uiittst0.spr",
           "ACTOR_IMP_ASSAULT_VEHICLE_DECOY",
           75, 5, 0, "",
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    MOBILE_ASSET("IMP_PLASMA_TANK_DECOY", "UIPLTST0_D", "uipltst0.spr",
           "ACTOR_IMP_PLASMA_TANK_DECOY",
           75, 4, 0, "",
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    MOBILE_ASSET("IMP_TACHYON_TANK_DECOY", "UITCTST0_D", "uitctst0.spr",
           "ACTOR_IMP_TACHYON_TANK_DECOY",
           100, 4, 0, "",
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    MOBILE_ASSET("IMP_MAD_DECOY", "UIMADST0_D", "uimadst0.spr",
           "ACTOR_IMP_MAD_DECOY",
           100, 4, 0, "",
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    MOBILE_ASSET("IMP_SHREDDER_DECOY", "UISHRST0_D", "uishrst0.spr",
           "ACTOR_IMP_SHREDDER_DECOY",
           100, 5, 50, "|MF_ATTACK",
           /* run/shoot identical 0..3; shoot=-1 so missilestate=seestate */
           ANIM(16, 0,4,100, -1,0,0,  0,  -1,0)),

    MOBILE_ASSET("IMP_SPA_DECOY", "UIIARST0_D", "uiiarst0.spr",
           "ACTOR_IMP_SPA_DECOY",
           100, 4, 0, "",
           ANIM(16, 0,1,100, -1,0,0,  0,  -1,0)),

    /* --- Civilians and neutral units (UNITS.TXT SetType values) --- */
    MOBILE("CIV_MALE", "UOCVMST0", "ACTOR_CIV_MALE",
           30, 6, 0, "",
           /* run:0..5  stand:22 */
           ANIM(8,  0,6,40,  -1,0,0, 22,  -1,0)),

    MOBILE("CIV_ROWDY", "UORCMST0", "ACTOR_CIV_ROWDY",
           66, 5, 2, "|MF_ATTACK",
           /* run:0..7  shoot:8..10 (Start=64/8)  stand:23 */
           ANIM(8,  0,8,40,  8,3,60, 23,  -1,0)),

    MOBILE("CIV_SPY", "UOCSPST0", "ACTOR_CIV_SPY",
           66, 8, 0, "",
           /* run:0..3  stand:0 (single-frame) */
           ANIM(8,  0,4,40,  -1,0,0,  0,  -1,0)),

    /* Civilians that share sprites need unique sprite names for distinct states. */
    MOBILE_ASSET("CIV_PRISONER", "UOCVMST0_CPR", "uocvmst0.spr",
           "ACTOR_CIV_PRISONER",
           30, 6, 0, "",
           /* same animation as male civilian */
           ANIM(8,  0,6,40,  -1,0,0, 22,  -1,0)),

    MOBILE_ASSET("CIV_JEBRAD", "UORCMST0_CJR", "uorcmst0.spr",
           "ACTOR_CIV_JEB_RAD",
           500, 8, 170, "|MF_ATTACK",
           /* same animation as rowdy, Radec weapon: range 7, 99ms cd, 170 dmg */
           ANIM(8,  0,8,40,  8,3,60, 23,  -1,0)),

    MOBILE_ASSET("CIV_KAROCH", "UOCSPST0_CK", "uocspst0.spr",
           "ACTOR_CIV_KAROCH",
           250, 5, 0, "",
           /* same animation as civ spy, MedicHeal support */
           ANIM(8,  0,4,40,  -1,0,0,  0,  -1,0)),

    MOBILE_ASSET("CIV_COLONEL", "UOCVMST0_CCM", "uocvmst0.spr",
           "ACTOR_CIV_COLONEL_MARTEL",
           100, 6, 0, "",
           /* same animation as male civilian */
           ANIM(8,  0,6,40,  -1,0,0, 22,  -1,0)),

    MOBILE("CIV_WHEEL", "UOWTRST0", "ACTOR_CIV_WHEEL_TRANSPORTER",
           150, 4, 0, "",
           /* run:0..2 (16 facings, L=3, T=40) */
           ANIM(16, 0,3,40,  -1,0,0,  0,  -1,0)),

    MOBILE("CIV_HOVER", "UOHTRST0", "ACTOR_CIV_HOVER_TRANSPORTER",
           100, 7, 0, "",
           /* single-frame body (16 facings, L=1) */
           ANIM(16, 0,1,40,  -1,0,0,  0,  -1,0)),

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
    BUILDING("FG_LIFE_PLANT",        "NCLNC1L0", "ACTOR_FG_LIFE_PLANT",           1300,  0, "|MF_RESOURCE_BASE"),
    BUILDING("FG_POWER_PLANT",       "NCPOW1L0", "ACTOR_FG_POWER_PLANT",          1450,  0, ""),
    BUILDING("FG_REFINERY",          "NFRRM1L0", "ACTOR_FG_REFINERY",              800,  0, ""),
    BUILDING("FG_BRIDGE_H",          "NCSBH1L0", "ACTOR_FG_SMALL_HORIZONTAL_BRIDGE",400, 0, ""),
    BUILDING("FG_BRIDGE_V",          "NCSBV1L0", "ACTOR_FG_SMALL_VERTICAL_BRIDGE", 400,  0, ""),
    BUILDING("FG_BRIDGE_C",          "NCSBC1L0", "ACTOR_FG_SMALL_CENTRE_BRIDGE",   400,  0, ""),
    BUILDING("FG_GUARD_TOWER",       "NFGDT1L0", "ACTOR_FG_GUARD_TOWER",           400, 10, "|MF_ATTACK"),
    BUILDING("FG_ADV_GUARD_TOWER",   "NFAGT1L0", "ACTOR_FG_ADVANCED_GUARD_TOWER",  550, 13, "|MF_ATTACK"),
    BUILDING("FG_AA_SITE",           "NFAAR1L0", "ACTOR_FG_AA_SITE",               600, 40, "|MF_ATTACK"),

    /* --- Imperium buildings (retail BUILD.TXT hitpoints) --- */
    BUILDING("IMP_HQ1",               "NIHQT1L0", "ACTOR_IMP_HEADQUARTERS_1",      1440,  0, "|MF_RESOURCE_BASE"),
    BUILDING("IMP_HQ2",               "NIHQT2L0", "ACTOR_IMP_HEADQUARTERS_2",      2880,  0, ""),
    BUILDING("IMP_HQ3",               "NIHQT3L0", "ACTOR_IMP_HEADQUARTERS_3",      4330,  0, ""),
    BUILDING("IMP_BARRACKS",          "NIUTF1L0", "ACTOR_IMP_TRAINING_FACILITY_1",  900,  0, ""),
    BUILDING("IMP_ADV_BARRACKS",      "NIUTF2L0", "ACTOR_IMP_TRAINING_FACILITY_2", 1800,  0, ""),
    BUILDING("IMP_VEHICLE_FACTORY",   "NIVCY1L0", "ACTOR_IMP_VEHICLE_FACTORY_1",   1200,  0, ""),
    BUILDING("IMP_ADV_VEHICLE_FACTORY","NIVCY2L0","ACTOR_IMP_VEHICLE_FACTORY_2",   2400,  0, ""),
    BUILDING("IMP_HOVER_FACTORY",     "NIHSP1L0", "ACTOR_IMP_HOVER_FACTORY",        720,  0, ""),
    BUILDING("IMP_REPAIR_BAY",        "NIREP1L0", "ACTOR_IMP_REPAIR_BAY",           720,  0, ""),
    BUILDING("IMP_TACHYON_PLANT",     "NITGT1L0", "ACTOR_IMP_TACHYON_PLANT",       1000,  0, ""),
    BUILDING("IMP_REFINERY",          "NIRRM1L0", "ACTOR_IMP_REFINERY",             960,  0, ""),
    BUILDING("IMP_RIFT_CREATOR",      "NITRC1L0", "ACTOR_IMP_RIFT_CREATOR",        1000,  0, ""),
    BUILDING("IMP_GUARD_TOWER",       "NIGDT1L0", "ACTOR_IMP_GUARD_TOWER",          400, 10, "|MF_ATTACK"),
    BUILDING("IMP_ADV_GUARD_TOWER",   "NIAGT1L0", "ACTOR_IMP_ADVANCED_GUARD_TOWER", 550,180, "|MF_ATTACK"),
    BUILDING("IMP_AA_SITE",           "NIAAR1L0", "ACTOR_IMP_AA_SITE",              720, 14, "|MF_ATTACK"),
    BUILDING_ASSET("IMP_CAMERA_TOWER", "NCCAM1L0_IMP", "nccam1l0.spr",
           "ACTOR_IMP_CAMERA_TOWER", 150, 0, ""),
    BUILDING_ASSET("IMP_LIFE_PLANT", "NCLNC1L0_IMP", "nclnc1l0.spr",
           "ACTOR_IMP_LIFE_PLANT", 1300, 0, "|MF_RESOURCE_BASE"),
    BUILDING_ASSET("IMP_POWER_PLANT", "NCPOW1L0_IMP", "ncpow1l0.spr",
           "ACTOR_IMP_POWER_PLANT", 1450, 0, ""),

    /* --- Imperium bridges (share sprites with FG bridges) --- */
    BUILDING_ASSET("IMP_BRIDGE_H", "NCSBH1L0_IMP", "ncsbh1l0.spr",
           "ACTOR_IMP_SMALL_HORIZONTAL_BRIDGE", 400, 0, ""),
    BUILDING_ASSET("IMP_BRIDGE_V", "NCSBV1L0_IMP", "ncsbv1l0.spr",
           "ACTOR_IMP_SMALL_VERTICAL_BRIDGE", 400, 0, ""),
    BUILDING_ASSET("IMP_BRIDGE_C", "NCSBC1L0_IMP", "ncsbc1l0.spr",
           "ACTOR_IMP_SMALL_CENTRE_BRIDGE", 400, 0, ""),

    /* --- Imperium walls --- */
    BUILDING("IMP_WALL_1", "NCSWL1L0", "ACTOR_IMP_SMALL_WALL_1", 100, 0, ""),
    BUILDING("IMP_WALL_2", "NCSWM1L0", "ACTOR_IMP_SMALL_WALL_2", 100, 0, ""),
    BUILDING("IMP_LARGE_WALL_1", "NCBWL1L0", "ACTOR_IMP_LARGE_WALL_1", 400, 0, ""),
    BUILDING("IMP_LARGE_WALL_2", "NCBWM1L0", "ACTOR_IMP_LARGE_WALL_2", 400, 0, ""),

    /* --- FG decoy buildings (share sprites with parent FG buildings) --- */
    BUILDING_ASSET("FG_VEHICLE_FACTORY_1_DECOY", "NFVCY1L0_D", "nfvcy1l0.spr",
           "ACTOR_FG_VEHICLE_FACTORY_1_DECOY", 200, 0, ""),
    BUILDING_ASSET("FG_VEHICLE_FACTORY_2_DECOY", "NFVCY2L0_D", "nfvcy2l0.spr",
           "ACTOR_FG_VEHICLE_FACTORY_2_DECOY", 200, 0, ""),
    BUILDING_ASSET("FG_PHASE_FACTORY_1_DECOY", "NFPHF1L0_D", "nfphf1l0.spr",
           "ACTOR_FG_PHASE_FACTORY_1_DECOY", 200, 0, ""),
    BUILDING_ASSET("FG_PHASE_FACTORY_2_DECOY", "NFPHF2L0_D", "nfphf2l0.spr",
           "ACTOR_FG_PHASE_FACTORY_2_DECOY", 200, 0, ""),
    BUILDING_ASSET("FG_HQ1_DECOY", "NFHQT1L0_D", "nfhqt1l0.spr",
           "ACTOR_FG_HEADQUARTERS_1_DECOY", 200, 0, ""),
    BUILDING_ASSET("FG_HQ2_DECOY", "NFHQT2L0_D", "nfhqt2l0.spr",
           "ACTOR_FG_HEADQUARTERS_2_DECOY", 200, 0, ""),
    BUILDING_ASSET("FG_HQ3_DECOY", "NFHQT3L0_D", "nfhqt3l0.spr",
           "ACTOR_FG_HEADQUARTERS_3_DECOY", 200, 0, ""),
    BUILDING_ASSET("FG_ADV_BARRACKS_DECOY", "NFUTF2L0_D", "nfutf2l0.spr",
           "ACTOR_FG_TRAINING_FACILITY_2_DECOY", 200, 0, ""),
    BUILDING_ASSET("FG_HOVER_FACTORY_DECOY", "NFHSP1L0_D", "nfhsp1l0.spr",
           "ACTOR_FG_HOVER_FACTORY_DECOY", 200, 0, ""),
    BUILDING_ASSET("FG_REPAIR_BAY_DECOY", "NFREP1L0_D", "nfrep1l0.spr",
           "ACTOR_FG_REPAIR_BAY_DECOY", 200, 0, ""),
    BUILDING_ASSET("FG_REFINERY_DECOY", "NFRRM1L0_D", "nfrrm1l0.spr",
           "ACTOR_FG_REFINERY_DECOY", 200, 0, ""),
    BUILDING_ASSET("FG_POWER_PLANT_DECOY", "NCPOW1L0_D", "ncpow1l0.spr",
           "ACTOR_FG_POWER_PLANT_DECOY", 200, 0, ""),
    BUILDING_ASSET("FG_BARRACKS_DECOY", "NFUTF1L0_D", "nfutf1l0.spr",
           "ACTOR_FG_TRAINING_FACILITY_1_DECOY", 200, 0, ""),

    /* --- Civilian and general buildings --- */
    BUILDING("CIV_ENTERTAINMENT", "NOCEN1L0", "ACTOR_CIV_ENTERTAINMENT", 1200, 0, ""),
    BUILDING("WATER_EXTRACTOR", "NCWEL1L0", "ACTOR_WATER_EXTRACTOR", 500, 0, ""),
    BUILDING("TAELON_EXTRACTOR", "NCMIN1L0", "ACTOR_TAELON_EXTRACTOR", 600, 0, ""),
    BUILDING("IMP_WATER_RESEARCH", "NOWAT1L0", "ACTOR_IMP_WATER_RESEARCH", 1200, 0, ""),
    BUILDING("IMP_HOVER_RESEARCH", "NOHOV1L0", "ACTOR_IMP_HOVER_RESEARCH", 1200, 0, ""),
    BUILDING("IMP_DESICATOR_RESEARCH", "NODES1L0", "ACTOR_IMP_DESICATOR_RESEARCH", 1200, 0, ""),
    BUILDING("IMP_GENETIC_RESEARCH", "NOMDR1L0", "ACTOR_IMP_GENETIC_RESEARCH", 1200, 0, ""),
    BUILDING("CIV_SHELTER", "NOSHL1L0", "ACTOR_CIV_SHELTER", 600, 0, ""),
    BUILDING("CIV_SUB_TRANSIT", "NOSUB1L0", "ACTOR_CIV_SUB_TRANSIT", 600, 0, ""),
    BUILDING("CIV_TRANSIT_CENTRE", "NOTCN1L0", "ACTOR_CIV_TRANSIT_CENTRE", 1200, 0, ""),
    BUILDING("FG_TREATY_HALL", "NOTYH1L0", "ACTOR_FG_TREATY_HALL", 1200, 0, ""),
    BUILDING("TOGRAN_LANDING_VESSEL", "NOTHQ1L0", "ACTOR_TOGRAN_LANDING_VESSEL", 1200, 0, ""),
    BUILDING("TOGRAN_MONOLITH", "NOMLT1L0", "ACTOR_TOGRAN_MONOLITH", 92000, 0, ""),
    BUILDING("TOGRAN_LABORATORY", "NOTDR1L0", "ACTOR_TOGRAN_LABORATORY", 90000, 0, ""),
    BUILDING("RENDEZVOUS_POINT", "NORVP1L0", "ACTOR_RENDEZVOUS_POINT", 1000, 0, ""),
    BUILDING("FG_PLANETARY_DEFENSE", "NOPLD1L0", "ACTOR_FG_PLANETARY_DEFENSE", 2500, 0, ""),
    BUILDING("CIV_COMMERCIAL", "NOCBS1L0", "ACTOR_CIV_COMMERCIAL", 1200, 0, ""),
    BUILDING("CIV_FACTORY", "NOWAR1L0", "ACTOR_CIV_FACTORY", 1200, 0, ""),
    BUILDING("IMP_PRISON", "NOPRI1L0", "ACTOR_IMP_PRISON", 2500, 0, ""),
    BUILDING("CIV_RURAL", "NOCHM4L0", "ACTOR_CIV_RURAL", 1200, 0, ""),
    BUILDING("CIV_GRAIN_FARM", "NOFRM1L0", "ACTOR_CIV_GRAIN_FARM", 600, 0, ""),
    BUILDING("CIV_HYDRO_FARM", "NOFRM1L1", "ACTOR_CIV_HYDRO_FARM", 600, 0, ""),
    BUILDING("CIV_FARMHOUSE", "NOFRM1L2", "ACTOR_CIV_FARMHOUSE", 600, 0, ""),

    /* --- Civilian bridges --- */
    BUILDING("CIVILIAN_BRIDGE", "NOBRD1L0", "ACTOR_CIVILIAN_BRIDGE", 4000, 0, ""),
    BUILDING("CIVILIAN_VERTICAL_BRIDGE", "NOBRD1L1", "ACTOR_CIVILIAN_VERTICAL_BRIDGE", 4000, 0, ""),

    /* --- Togran bridges (share sprites with FG/IMP bridges) --- */
    BUILDING_ASSET("TOGRAN_BRIDGE_H", "NCSBH1L0_T", "ncsbh1l0.spr",
           "ACTOR_TOGRAN_SMALL_HORIZONTAL_BRIDGE", 400, 0, ""),
    BUILDING_ASSET("TOGRAN_BRIDGE_V", "NCSBV1L0_T", "ncsbv1l0.spr",
           "ACTOR_TOGRAN_SMALL_VERTICAL_BRIDGE", 400, 0, ""),
    BUILDING_ASSET("TOGRAN_BRIDGE_C", "NCSBC1L0_T", "ncsbc1l0.spr",
           "ACTOR_TOGRAN_SMALL_CENTRE_BRIDGE", 400, 0, ""),
};

/* ------------------------------------------------------------------ writers */

/* Write one sprite's states to <animate-dir>/<SPRITE>.inc using C99
 * designated initializers so include order does not matter. */
static bool write_inc(const char *animate_dir, const char *sprite,
                      const dr_entry_t *e) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.inc", animate_dir, sprite);

    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "/* Generated by tools/dr_info_gen. Do not edit. */\n");

    const dr_anim_t *a = &e->anim;
    bool attacking = has_attack_flag(e);

    /* STND */
    fprintf(f, "    [S_%s_STND] = { SPR_%s, %d, %d, %s, S_%s_STND, 0 },\n",
            sprite, sprite, a->stand_start, attacking ? 20 : -1,
            attacking ? "A_Look" : "NULL", sprite);

    /* RUN */
    if (a->run_start >= 0 && a->run_len > 0) {
        int tics = tics_from_tick(a->run_tick);
        for (int s = 0; s < a->run_len; ++s) {
            int next = s < a->run_len - 1 ? s + 2 : 1;
            fprintf(f, "    [S_%s_RUN%d] = { SPR_%s, %d, %d, %s, S_%s_RUN%d, 2 },\n",
                    sprite, s + 1, sprite, a->run_start + s, tics,
                    s == 0 && attacking ? "A_Chase" : "NULL",
                    sprite, next);
        }
    }

    /* SHOOT */
    if (a->shoot_start >= 0 && a->shoot_len > 0) {
        int tics = tics_from_tick(a->shoot_tick);
        for (int s = 0; s < a->shoot_len; ++s) {
            if (s < a->shoot_len - 1)
                fprintf(f, "    [S_%s_SHOOT%d] = { SPR_%s, %d, %d, %s, S_%s_SHOOT%d, 3 },\n",
                        sprite, s + 1, sprite, a->shoot_start + s, tics,
                        s == 0 && attacking ? "A_Attack" : "NULL",
                        sprite, s + 2);
            else
                fprintf(f, "    [S_%s_SHOOT%d] = { SPR_%s, %d, %d, %s, S_%s_STND, 3 },\n",
                        sprite, s + 1, sprite, a->shoot_start + s, tics,
                        s == 0 && attacking ? "A_Attack" : "NULL",
                        sprite);
        }
    }

    /* IDLE */
    if (a->idle_start >= 0 && a->idle_len > 0) {
        for (int s = 0; s < a->idle_len; ++s) {
            if (s < a->idle_len - 1)
                fprintf(f, "    [S_%s_IDLE%d] = { SPR_%s, %d, 10, NULL, S_%s_IDLE%d, 0 },\n",
                        sprite, s + 1, sprite, a->idle_start + s, sprite, s + 2);
            else
                fprintf(f, "    [S_%s_IDLE%d] = { SPR_%s, %d, 10, NULL, S_%s_IDLE1, 0 },\n",
                        sprite, s + 1, sprite, a->idle_start + s, sprite);
        }
    }

    /* HARVEST */
    if (a->harvest_start >= 0 && a->harvest_len > 0) {
        int tics = tics_from_tick(a->harvest_tick);
        for (int s = 0; s < a->harvest_len; ++s) {
            int next = s < a->harvest_len - 1 ? s + 2 : 1;
            fprintf(f, "    [S_%s_HARVEST%d] = { SPR_%s, %d, %d, NULL, S_%s_HARVEST%d, 5 },\n",
                    sprite, s + 1, sprite, a->harvest_start + s, tics,
                    sprite, next);
        }
    }

    /* FIRE: single attack state for attacking actors without a native shoot
     * cycle. Plays the stand frame once, fires through A_Attack, and returns
     * to STND. Without this row the S_*_FIRE slot stays zero-filled and
     * entering it removes the unit. */
    if (has_fire_state(e))
        fprintf(f, "    [S_%s_FIRE] = { SPR_%s, %d, 1, A_Attack, S_%s_STND, 3 },\n",
                sprite, sprite, a->stand_start, sprite);

    fclose(f);
    return true;
}

static bool is_first_sprite(const dr_entry_t *entries, int index) {
    for (int i = 0; i < index; ++i)
        if (strcmp(entries[i].sprite, entries[index].sprite) == 0)
            return false;
    return true;
}

/* Emit statenum_t entries for one entry to the given stream. */
static void emit_statenum(FILE *f, const dr_entry_t *e) {
    const dr_anim_t *a = &e->anim;

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
    if (a->harvest_start >= 0 && a->harvest_len > 0)
        for (int s = 0; s < a->harvest_len; ++s)
            fprintf(f, "    S_%s_HARVEST%d,\n", e->sprite, s + 1);
}

/* Emit the S_*_FIRE entry for an entry that needs one. */
static void emit_fire_statenum(FILE *f, const dr_entry_t *e) {
    fprintf(f, "    S_%s_FIRE,\n", e->sprite);
}

static void write_statenum(FILE *f, const dr_entry_t *entries, int count) {
    fprintf(f, "typedef enum {\n    S_NULL = 0,\n");
    for (int i = 0; i < count; ++i)
        emit_statenum(f, &entries[i]);
    for (int i = 0; i < count; ++i)
        if (has_fire_state(&entries[i]))
            emit_fire_statenum(f, &entries[i]);
    fprintf(f, "    NUMSTATES\n} statenum_t;\n");
}

/* Write the statenum_t enum section to info.h between marker comments.
 * If the file doesn't exist, creates the full template. */
static bool write_info_h_enum(const char *info_h_path, const dr_entry_t *entries,
                              int count) {
    const char *BEGIN = "/* BEGIN_GENERATED_STATENUM */";
    const char *END   = "/* END_GENERATED_STATENUM */";

    /* Read the existing file if present. */
    char *old = NULL;
    long size = 0;
    FILE *rf = fopen(info_h_path, "r");
    if (rf) {
        fseek(rf, 0, SEEK_END);
        size = ftell(rf);
        rewind(rf);
        old = malloc((size_t)size + 1);
        if (old) {
            if (fread(old, 1, (size_t)size, rf) != (size_t)size) {
                free(old); old = NULL;
            } else {
                old[size] = '\0';
            }
        }
        fclose(rf);
    }

    char *begin_pos = old ? strstr(old, BEGIN) : NULL;
    char *end_pos   = begin_pos ? strstr(begin_pos, END) : NULL;

    FILE *out = fopen(info_h_path, "w");
    if (!out) { free(old); return false; }

    if (begin_pos && end_pos) {
        /* Update between markers in existing file. */
        fwrite(old, 1, (size_t)(begin_pos - old), out);

        fprintf(out, "%s\n", BEGIN);
        write_statenum(out, entries, count);
        fprintf(out, "%s\n", END);

        const char *after = end_pos + strlen(END);
        if (*after == '\n') ++after;
        fwrite(after, 1, (size_t)(old + size - after), out);
    } else {
        /* Create the full info.h from scratch. */
        fprintf(out,
            "/* Generated from retail DEFTXT and the OpenDR sprite catalog. Do not edit by hand. */\n"
            "#ifndef __INFO__\n#define __INFO__\n\n"
            "#include \"actor.h\"\n\n"
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
        fprintf(out, "typedef enum {\n");
        for (int i = 0; i < count; ++i)
            fprintf(out, "    SPR_%s,\n", entries[i].sprite);
        fprintf(out, "    NUMSPRITES\n} spritenum_t;\n\n");

        /* statenum_t with markers */
        fprintf(out, "%s\n", BEGIN);
        write_statenum(out, entries, count);
        fprintf(out, "%s\n\n", END);

        /* MT_ enum */
        fprintf(out, "enum {\n    MT_NULL,\n");
        for (int i = 0; i < count; ++i)
            fprintf(out, "    MT_%s,\n", entries[i].type);
        fprintf(out, "    NUMMOBJTYPES,\n};\n\n");

        fprintf(out,
            "extern const char *const sprnames[NUMSPRITES];\n"
            "extern const state_t states[NUMSTATES];\n"
            "extern const mobjinfo_t mobjinfo[NUMMOBJTYPES];\n"
            "extern const gameinfo_t game_info;\n\n#endif\n");
    }

    free(old);
    return fclose(out) == 0;
}

/* Write the complete info.c file with #include per sprite. */
static bool write_info_c(const char *path, const dr_entry_t *entries, int count) {
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

    /* states with #include per sprite */
    fprintf(f, "const state_t states[NUMSTATES] = {\n"
               "    { 0, 0, -1, NULL, S_NULL, 0 },\n");

    /* Collect unique sprites in order of first appearance. */
    for (int i = 0; i < count; ++i) {
        if (is_first_sprite(entries, i))
            fprintf(f, "    #include \"animate/%s.inc\"\n", entries[i].sprite);
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
    if (argc != 5) {
        fprintf(stderr, "usage: dr_info_gen <dark-reign-root> <info.h> <info.c> <animate-dir>\n");
        return 1;
    }

    const char *dr_root    = argv[1];
    const char *info_h     = argv[2];
    const char *info_c     = argv[3];
    const char *animate_dir = argv[4];

    /* Validate every sprite name against the retail DEFTXT files. */
    char path[1024];
    snprintf(path, sizeof(path), "%s/deftxt/UNITS.TXT", dr_root);
    char *units = read_text(path);
    snprintf(path, sizeof(path), "%s/deftxt/BUILD.TXT", dr_root);
    char *buildings = read_text(path);
    if (!units || !buildings) {
        fprintf(stderr, "dr_info_gen: cannot read retail DEFTXT under %s: %s\n",
                dr_root, strerror(errno));
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

    /* Create the animate directory. */
    if (mkdir_p(animate_dir) < 0 && errno != EEXIST) {
        fprintf(stderr, "dr_info_gen: mkdir %s: %s\n", animate_dir, strerror(errno));
        return 1;
    }

    /* Write per-sprite .inc files. */
    for (int i = 0; i < count; ++i) {
        if (!is_first_sprite(entries, i)) continue;
        if (!write_inc(animate_dir, entries[i].sprite, &entries[i])) {
            fprintf(stderr, "dr_info_gen: cannot write %s/%s.inc: %s\n",
                    animate_dir, entries[i].sprite, strerror(errno));
            return 1;
        }
    }
    fprintf(stderr, "dr_info_gen: wrote .inc files to %s\n", animate_dir);

    /* Update the statenum_t enum in info.h. */
    if (!write_info_h_enum(info_h, entries, count)) {
        fprintf(stderr, "dr_info_gen: cannot update %s: %s\n", info_h, strerror(errno));
        return 1;
    }
    fprintf(stderr, "dr_info_gen: updated statenum_t in %s\n", info_h);

    /* Write info.c. */
    if (!write_info_c(info_c, entries, count)) {
        fprintf(stderr, "dr_info_gen: cannot write %s: %s\n", info_c, strerror(errno));
        return 1;
    }
    fprintf(stderr, "dr_info_gen: wrote %s\n", info_c);

    return 0;
}
