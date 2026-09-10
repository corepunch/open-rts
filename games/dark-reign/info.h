/* Generated from retail DEFTXT and the OpenDR sprite catalog. Do not edit by hand. */
#ifndef __INFO__
#define __INFO__

#include "actor.h"

typedef struct mobjinfo_s {
    int doomednum;
    int spawnstate;
    int spawnhealth;
    int seestate;
    int seesound;
    int reactiontime;
    int attacksound;
    int painstate;
    int painchance;
    int painsound;
    int meleestate;
    int missilestate;
    int deathstate;
    int xdeathstate;
    int deathsound;
    int speed;
    int radius;
    int height;
    int mass;
    int damage;
    int activesound;
    int flags;
    int raisestate;
    fixed_t spawnz;
} mobjinfo_t;

typedef enum {
    SPR_UCFCNST0,
    SPR_UCFRGST0,
    SPR_UCHFRST0,
    SPR_UFRADST0,
    SPR_UFMRCST0,
    SPR_UFSNPST0,
    SPR_UFSCTST0,
    SPR_UFMEDST0,
    SPR_UFSABST0,
    SPR_UFMECST0,
    SPR_UFMTRST0,
    SPR_UCINFST0,
    SPR_UFSPBST0,
    SPR_UFRATST0,
    SPR_UFSKTST0,
    SPR_UFTHNST0,
    SPR_UFPHTST0,
    SPR_UFFLKST0,
    SPR_UFTRTST0,
    SPR_UFFARST0,
    SPR_UFSKBST0,
    SPR_UFOUTST0,
    SPR_UFSWVST0,
    SPR_UCWCOST0,
    SPR_UFPHRST0,
    SPR_UFBAMST0,
    SPR_NFHQT1L0,
    SPR_NFHQT2L0,
    SPR_NFHQT3L0,
    SPR_NFUTF1L0,
    SPR_NFUTF2L0,
    SPR_NFVCY1L0,
    SPR_NFVCY2L0,
    SPR_NFHSP1L0,
    SPR_NFREP1L0,
    SPR_NFPHF1L0,
    SPR_NFPHF2L0,
    SPR_NCCAM1L0,
    SPR_NCLNC1L0,
    SPR_NCPOW1L0,
    SPR_NFRRM1L0,
    SPR_NCSBH1L0,
    SPR_NCSBV1L0,
    SPR_NCSBC1L0,
    SPR_NFGDT1L0,
    SPR_NFAGT1L0,
    SPR_NFAAR1L0,
    NUMSPRITES
} spritenum_t;

typedef enum {
    S_NULL = 0,
    S_UCFCNST0_STND,
    S_UCFRGST0_STND,
    S_UCHFRST0_STND,
    S_UFRADST0_STND,
    S_UFMRCST0_STND,
    S_UFSNPST0_STND,
    S_UFSCTST0_STND,
    S_UFMEDST0_STND,
    S_UFSABST0_STND,
    S_UFMECST0_STND,
    S_UFMTRST0_STND,
    S_UCINFST0_STND,
    S_UFSPBST0_STND,
    S_UFRATST0_STND,
    S_UFSKTST0_STND,
    S_UFTHNST0_STND,
    S_UFPHTST0_STND,
    S_UFFLKST0_STND,
    S_UFTRTST0_STND,
    S_UFFARST0_STND,
    S_UFSKBST0_STND,
    S_UFOUTST0_STND,
    S_UFSWVST0_STND,
    S_UCWCOST0_STND,
    S_UFPHRST0_STND,
    S_UFBAMST0_STND,
    S_NFHQT1L0_STND,
    S_NFHQT2L0_STND,
    S_NFHQT3L0_STND,
    S_NFUTF1L0_STND,
    S_NFUTF2L0_STND,
    S_NFVCY1L0_STND,
    S_NFVCY2L0_STND,
    S_NFHSP1L0_STND,
    S_NFREP1L0_STND,
    S_NFPHF1L0_STND,
    S_NFPHF2L0_STND,
    S_NCCAM1L0_STND,
    S_NCLNC1L0_STND,
    S_NCPOW1L0_STND,
    S_NFRRM1L0_STND,
    S_NCSBH1L0_STND,
    S_NCSBV1L0_STND,
    S_NCSBC1L0_STND,
    S_NFGDT1L0_STND,
    S_NFAGT1L0_STND,
    S_NFAAR1L0_STND,
    NUMSTATES
} statenum_t;

enum {
    MT_NULL,
    MT_FG_CONSTRUCTION_CREW,
    MT_FG_FREIGHTER,
    MT_FG_HOVER_FREIGHTER,
    MT_FG_RAIDER,
    MT_FG_MERCENARY,
    MT_FG_SNIPER,
    MT_FG_SCOUT,
    MT_FG_MEDIC,
    MT_FG_SABOTEUR,
    MT_FG_MECHANIC,
    MT_FG_MARTYR,
    MT_FG_SPY,
    MT_FG_SPYDER_BIKE,
    MT_FG_IFV,
    MT_FG_MEDIUM_TANK,
    MT_FG_TANK_HUNTER,
    MT_FG_PHASE_TANK,
    MT_FG_MAD,
    MT_FG_TRIPLE_RAIL_TANK,
    MT_FG_SPA,
    MT_FG_SKY_BIKE,
    MT_FG_OUTRIDER,
    MT_FG_SHOCKWAVE,
    MT_FG_CONTAMINATOR,
    MT_FG_UNDERGROUND_TUNNEL,
    MT_FG_BASE_MOVER,
    MT_FG_HQ1,
    MT_FG_HQ2,
    MT_FG_HQ3,
    MT_FG_BARRACKS,
    MT_FG_ADV_BARRACKS,
    MT_FG_VEHICLE_FACTORY,
    MT_FG_ADV_VEHICLE_FACTORY,
    MT_FG_HOVER_FACTORY,
    MT_FG_REPAIR_BAY,
    MT_FG_PHASE_FACTORY_1,
    MT_FG_PHASE_FACTORY_2,
    MT_FG_CAMERA_TOWER,
    MT_FG_LIFE_PLANT,
    MT_FG_POWER_PLANT,
    MT_FG_REFINERY,
    MT_FG_BRIDGE_H,
    MT_FG_BRIDGE_V,
    MT_FG_BRIDGE_C,
    MT_FG_GUARD_TOWER,
    MT_FG_ADV_GUARD_TOWER,
    MT_FG_AA_SITE,
    NUMMOBJTYPES,
};

extern const char *const sprnames[NUMSPRITES];
extern const state_t states[NUMSTATES];
extern const mobjinfo_t mobjinfo[NUMMOBJTYPES];
extern const gameinfo_t game_info;

#endif
