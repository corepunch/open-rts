/* Authored gameplay tables; FIN animation extraction lives in tools/dc_info_conv/. */
#include "engine.h"
#include "info.h"

const char *const sprnames[NUMSPRITES] = {
    "DROP3", "DROP4", "ACAR", "ACOM", "AIRD", "ALBU", "ALIEN1", "ARTILER2",
    "ARTY", "ATRIL", "ATTACK2", "AVII", "BARR", "BBIT", "BEAC", "BEES",
    "BEON", "BIGC", "BITS", "BLAH", "BLAM", "BLAZ", "BLOO", "BOIG", "BRIT",
    "BUILDNG", "CAMM", "CENT", "CHAA", "CHAB", "CHOA", "CHOB", "CHOC", "CHOD",
    "CLOC", "CLOD", "CRYO", "CURS", "CYBORG", "DCSS", "DCUK", "DCUT", "DISH",
    "DOTT", "DROA", "DROP", "DSTY", "DUTS", "EGG", "ENCA", "ENCB", "ENCC",
    "ENCD", "ENCE", "ENCF", "ENGI", "EXPL", "FACT", "FETU", "FILL", "FIRA",
    "FIRB", "FIRE", "FLUCTION", "FRIEGHT", "FUEL", "GASY", "GLAT", "GLIT",
    "GLOT", "GRAY", "GRND", "GRUB", "HCAR", "HCOM", "HITA", "HITB", "HITC",
    "HITD", "HITE", "HITF", "HITG", "HITH", "HITT", "HUBU", "HYYK", "IT",
    "KNOBE", "LEFT", "LENS", "LEVEL", "LLLL", "LUNA", "LUNY", "MAKT",
    "MATATRAC", "MISA", "MISB", "MISC", "MISD", "MISE", "MISF", "MISG",
    "MISH", "MORE", "MSLS", "MUZA", "MWIA", "MWIB", "MWIC", "MWID", "NETA",
    "NETB", "NETC", "NETD", "NETE", "NUKE", "ORTU", "PCFO", "PLASMA", "POPP",
    "POPPPAEN", "PORT", "PSYC", "PUFF", "PUSB", "REAP", "RNAT", "SALA",
    "SALY", "SARG", "SAUC", "SCGM", "SCOU", "SCOUT1", "SCUT", "SCYT", "SERA",
    "SERB", "SERC", "SERD", "SERE", "SHOK", "SHORTCIT", "SHRI", "SIDE",
    "SLOM", "SLUG", "SMAE", "SMAY", "SMOA", "SMOK", "SMSP", "SONIC", "SPAC",
    "SPAK", "SPAR", "SPED", "SPID", "SPIKE", "SPON", "SPOT", "SPUC", "SRCH",
    "SSSS", "TEKT", "TEKTARA", "TIMEMIS", "TONG", "TORT", "TOWR", "TOXX",
    "TROOPER1", "TROOPER2", "TRSC", "TRUK", "TURR", "VCAL", "VCEA", "VENT",
    "VENT2", "WATC", "WEATH", "WINA", "WINB", "WINC", "WIND", "WINE", "WINF",
    "XENO", "YABA", "ZISP", "BURN", "BURN2",
};

const state_t states[NUMSTATES] = {
    { 0, 0, -1, NULL, S_NULL, 0 },
    #include "animate/ALBU.inc"
    #include "animate/ATRIL.inc"
    #include "animate/BARR.inc"
    #include "animate/BEAC.inc"
    #include "animate/BLOO.inc"
    #include "animate/BURN.inc"
    #include "animate/BURN2.inc"
    #include "animate/CENT.inc"
    #include "animate/DISH.inc"
    #include "animate/DOTT.inc"
    #include "animate/DROA.inc"
    #include "animate/DROP.inc"
    #include "animate/DROP3.inc"
    #include "animate/DROP4.inc"
    #include "animate/EXPL.inc"
    #include "animate/FILL.inc"
    #include "animate/FUEL.inc"
    #include "animate/GRAY.inc"
    #include "animate/HUBU.inc"
    #include "animate/HYYK.inc"
    #include "animate/ORTU.inc"
    #include "animate/REAP.inc"
    #include "animate/SALA.inc"
    #include "animate/SARG.inc"
    #include "animate/SCGM.inc"
    #include "animate/SCYT.inc"
    #include "animate/SHRI.inc"
    #include "animate/SLUG.inc"
    #include "animate/TONG.inc"
    #include "animate/TOWR.inc"
    #include "animate/TRSC.inc"
    #include "animate/TURR.inc"
    #include "animate/VENT.inc"
    #include "animate/WATC.inc"
    #include "animate/XENO.inc"
};

const dc_building_sequence_t dc_building_sequences[MT_RSCHPOD - MT_EXCOPOD + 1][3] = {
    { { "EXCOPODSCRCH0", S_EXCOPODSCRCH0_170, S_EXCOPODSCRCH0_185 }, { "EXCOPODBURN0", S_EXCOPODBURN0_186, S_EXCOPODBURN0_201 }, { "EXCOPODDIE0", S_EXCOPODDIE0_191, S_EXCOPODDIE0_219 } },
    { { "BRRKPODSCRCH0", S_BRRKPODSCRCH0_301, S_BRRKPODSCRCH0_320 }, { "BRRKPODBURN0", S_BRRKPODBURN0_57, S_BRRKPODBURN0_76 }, { "BRRKPODDIE0", S_BRRKPODDIE0_112, S_BRRKPODDIE0_146 } },
    { { "ROBOPODSCRCH0", S_ROBOPODSCRCH0_88, S_ROBOPODSCRCH0_107 }, { "ROBOPODBURN0", S_ROBOPODBURN0_386, S_ROBOPODBURN0_405 }, { "ROBOPODDIE0", S_ROBOPODDIE0_147, S_ROBOPODDIE0_181 } },
    { { "ROBOPOD2SCRCH0", S_ROBOPOD2SCRCH0_0, S_ROBOPOD2SCRCH0_19 }, { "ROBOPOD2BURN0", S_ROBOPOD2BURN0_108, S_ROBOPOD2BURN0_127 }, { "ROBOPOD2DIE0", S_ROBOPOD2DIE0_217, S_ROBOPOD2DIE0_251 } },
    { { "SCNCPODSCRCH0", S_SCNCPODSCRCH0_289, S_SCNCPODSCRCH0_304 }, { "SCNCPODBURN0", S_SCNCPODBURN0_72, S_SCNCPODBURN0_87 }, { "SCNCPODDIE0", S_SCNCPODDIE0_321, S_SCNCPODDIE0_352 } },
    { { "SCNCPOD2SCRCH0", S_SCNCPOD2SCRCH0_408, S_SCNCPOD2SCRCH0_423 }, { "SCNCPOD2BURN0", S_SCNCPOD2BURN0_466, S_SCNCPOD2BURN0_481 }, { "SCNCPOD2DIE0", S_SCNCPOD2DIE0_353, S_SCNCPOD2DIE0_385 } },
    { { "RSCHPODSCRCH0", S_RSCHPODSCRCH0_128, S_RSCHPODSCRCH0_148 }, { "RSCHPODBURN0", S_RSCHPODBURN0_149, S_RSCHPODBURN0_169 }, { "RSCHPODDIE0", S_RSCHPODDIE0_182, S_RSCHPODDIE0_216 } },
};

const mobjinfo_t mobjinfo[NUMMOBJTYPES] = {
    { // MT_NULL
        0,
    },
    { // MT_TROOPER
        .doomednum = 1,
        .spawnstate = S_TRSC_STND,
        .spawnhealth = 800,
        .seestate = S_TRSC_RUN1,
        .missilestate = S_TRSC_ATK1,
        .deathstate = S_TRSC_DIE1,
        .xdeathstate = S_TRSC_DIE1,
        .speed = 5,
        .radius = 16,
        .height = 32,
        .mass = 100,
        .damage = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_ATTACK,
    },
    { // MT_GREY
        .doomednum = 2,
        .spawnstate = S_GRAY_STND,
        .spawnhealth = 800,
        .seestate = S_GRAY_RUN1,
        .missilestate = S_GRAY_ATK1,
        .deathstate = S_GRAY_DIE1,
        .xdeathstate = S_GRAY_DIE1,
        .speed = 5,
        .radius = 16,
        .height = 32,
        .mass = 100,
        .damage = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_ATTACK,
    },
    { // MT_EXPLOITER
        .doomednum = 3,
        .spawnstate = S_EXPL_STND,
        .spawnhealth = 800,
        .seestate = S_EXPL_RUN1,
        .deathstate = S_EXPL_DIE1,
        .xdeathstate = S_EXPL_DIE1,
        .speed = 5,
        .radius = 16,
        .height = 32,
        .mass = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_HARVESTER,
    },
    { // MT_REAPER
        .doomednum = 2,
        .spawnstate = S_REAP_STND,
        .spawnhealth = 800,
        .seestate = S_REAP_RUN1,
        .missilestate = S_REAP_ATK1,
        .deathstate = S_REAP_DIE_SELECT,
        .xdeathstate = S_REAP_DIE_SELECT,
        .speed = 6,
        .radius = 16,
        .height = 32,
        .mass = 100,
        .damage = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_ATTACK,
    },
    { // MT_THUNDERBOLT
        .doomednum = 3,
        .spawnstate = S_BARR_STND,
        .spawnhealth = 400,
        .seestate = S_BARR_RUN1,
        .deathstate = S_BARR_DIE1,
        .xdeathstate = S_BARR_DIE1,
        .speed = 3,
        .radius = 16,
        .height = 32,
        .mass = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE,
    },
    { // MT_CYBORG
        .doomednum = 4,
        .spawnstate = S_SARG_STND,
        .spawnhealth = 800,
        .seestate = S_SARG_RUN1,
        .deathstate = S_SARG_DIE1,
        .xdeathstate = S_SARG_DIE1,
        .speed = 9,
        .radius = 16,
        .height = 32,
        .mass = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE,
    },
    { // MT_SCOUT
        .doomednum = 5,
        .spawnstate = S_SCGM_STND,
        .spawnhealth = 800,
        .seestate = S_SCGM_RUN1,
        .deathstate = S_SCGM_DIE1,
        .xdeathstate = S_SCGM_DIE1,
        .speed = 9,
        .radius = 16,
        .height = 32,
        .mass = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE,
    },
    { // MT_ORTU
        .spawnstate = S_ORTU_STND,
        .spawnhealth = 800,
        .seestate = S_ORTU_RUN1,
        .deathstate = S_ORTU_DIE1,
        .xdeathstate = S_ORTU_DIE1,
        .speed = 5,
        .radius = 16,
        .height = 32,
        .mass = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_ATTACK,
    },
    { // MT_SLUG
        .spawnstate = S_SLUG_STND,
        .spawnhealth = 800,
        .seestate = S_SLUG_RUN1,
        .deathstate = S_SLUG_DIE1,
        .xdeathstate = S_SLUG_DIE1,
        .speed = 5,
        .radius = 16,
        .height = 32,
        .mass = 100,
        .flags = MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE|MF_HARVESTER,
    },
    { // MT_MOBILE_TOWER
        .spawnstate = S_TURR_STND,
        .spawnhealth = 800,
        .missilestate = S_TURR_FIRE,
        .deathstate = S_TURR_DIE1,
        .xdeathstate = S_TURR_DIE1,
        .radius = 16,
        .height = 32,
        .mass = 100,
        .flags = MF_SELECTABLE|MF_RENDERABLE|MF_ATTACK,
    },
    { // MT_DROP_LINK
        .spawnstate = S_CENT_STND1,
        .spawnhealth = 800,
        .radius = 16,
        .height = 32,
        .mass = 100,
        .flags = MF_RENDERABLE,
    },
    { // MT_ALIEN_COM
        .spawnstate = S_TONG_STND1,
        .spawnhealth = 800,
        .deathstate = S_TONG_DIE1,
        .xdeathstate = S_TONG_DIE1,
        .radius = 16,
        .height = 32,
        .mass = 100,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_VISION_SIGHT
        .spawnstate = S_DOTT_STND,
        .spawnhealth = 300,
        .radius = 16,
        .height = 32,
        .mass = 100,
        .flags = MF_RENDERABLE,
    },
    { // MT_DROPSHIP
        .spawnstate = S_DROP_UNLOAD1,
        .seestate = S_DROP_MOVE1,
        .spawnhealth = 800,
        .speed = 4,
        .radius = 16,
        .height = 32,
        .mass = 100,
        .flags = MF_RENDERABLE|MF_MOBILE|MF_FLY,
        .spawnz = 50 * FIXED_ONE / 32, /* Requested 50 px altitude; DC cells are 32 px. */
    },
    { // MT_BLOOD
        .spawnstate = S_NULL,
        .flags = MF_RENDERABLE|MF_NOBLOCKMAP,
    },
    { // MT_VENT
        .spawnstate = S_VENT_EXHAUSTED,
        .flags = MF_RENDERABLE|MF_NOBLOCKMAP,
    },
    { // MT_BEACON
        .spawnstate = S_BEAC_STAND1,
        .spawnhealth = 800,
        .flags = MF_RENDERABLE|MF_NOBLOCKMAP,
    },
    { // MT_EXCOPOD
        .doomednum = 16,
        .spawnstate = S_EXCOPOD_STND,
        .deathstate = S_EXCOPODDIE0_191,
        .spawnhealth = 4800,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_BRRKPOD
        .doomednum = 17,
        .spawnstate = S_BRRKPOD_STND,
        .deathstate = S_BRRKPODDIE0_112,
        .spawnhealth = 2400,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_ROBOPOD
        .doomednum = 18,
        .spawnstate = S_ROBOPOD_STND1,
        .deathstate = S_ROBOPODDIE0_147,
        .spawnhealth = 2400,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_ROBOPOD2
        .doomednum = 19,
        .spawnstate = S_ROBOPOD2_STND1,
        .deathstate = S_ROBOPOD2DIE0_217,
        .spawnhealth = 3600,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_SCNCPOD
        .doomednum = 20,
        .spawnstate = S_SCNCPOD_STND1,
        .deathstate = S_SCNCPODDIE0_321,
        .spawnhealth = 2400,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_SCNCPOD2
        .doomednum = 21,
        .spawnstate = S_SCNCPOD2_STND1,
        .deathstate = S_SCNCPOD2DIE0_353,
        .spawnhealth = 3600,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_RSCHPOD
        .doomednum = 22,
        .spawnstate = S_RSCHPOD_STND1,
        .deathstate = S_RSCHPODDIE0_182,
        .spawnhealth = 3600,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_ALIEN_MINDHIVE
        .doomednum = 28,
        .spawnstate = S_ALIEN_MINDHIVE_STND,
        .spawnhealth = 4800,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_ALIEN_WARHIVE
        .doomednum = 29,
        .spawnstate = S_ALIEN_WARHIVE_STND,
        .spawnhealth = 2400,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_ALIEN_BRDRHIVE
        .doomednum = 30,
        .spawnstate = S_ALIEN_BRDRHIVE_STND,
        .spawnhealth = 2400,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_ALIEN_BRDRHIVE2
        .doomednum = 31,
        .spawnstate = S_ALIEN_BRDRHIVE2_STND,
        .spawnhealth = 3600,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_ALIEN_MINDHIVE2
        .doomednum = 32,
        .spawnstate = S_ALIEN_MINDHIVE2_STND,
        .spawnhealth = 2400,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_ALIEN_MINDHIVE3
        .doomednum = 33,
        .spawnstate = S_ALIEN_MINDHIVE3_STND,
        .spawnhealth = 3600,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_ALIEN_RSCHIVE
        .doomednum = 34,
        .spawnstate = S_ALIEN_RSCHIVE_STND,
        .spawnhealth = 3600,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_COMMS_DISH
        .doomednum = 86,
        .spawnstate = S_DISH_STND1,
        .spawnhealth = 1200,
        .flags = MF_SELECTABLE|MF_RENDERABLE,
    },
    { // MT_CITY_TOWER
        .doomednum = 81,
        .spawnstate = S_TOWR_STND,
        .spawnhealth = 1600,
        .flags = MF_RENDERABLE,
    },
    { // MT_PRODUCTION_RELEASE
        .spawnstate = S_BRRKPOD_BUILD_TRSC1,
        .flags = MF_RENDERABLE|MF_NOBLOCKMAP,
    },
};

const gameinfo_t game_info = {
    sprnames,
    NUMSPRITES,
    states,
    NUMSTATES,
    mobjinfo,
    NUMMOBJTYPES,
    S_NULL,
    RTS_STATE_COORDS_FIN_TOP_LEFT,
    { .style = SELECTION_STYLE_SPRITE, .image = "INTRFACE/CLIENT.SPR",
      .healthy_frame = 0, .wounded_frame = 1, .critical_frame = 3, .top_offset_y = -3 },
    NULL,
};
