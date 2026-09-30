#define _DEFAULT_SOURCE
#include "game.h"
#include "dc_facing.h"
#include "engine.h"
#include "info.h"
#include "gamestat.h"
#include "dc_types.h"
#include "sb_bar.h"
#include "w_spr.h"
#include "p_mission.h"
#include "p_blood.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "p_weapon.h"

/* DC.EXE (0x4117fc/0x411a30) moves an object speed/256 of a cell once per
 * 66 ms world tick; the simulation stores cells per second. */
#define DC_SPEED(raw) ((float)(raw) * (1000.0f / 66.0f) / 256.0f)

bool load_dark_colony_map(const char *map_path, level_t *out);
int load_dark_colony_initial_units(void);
extern bool load_dark_colony_tileset(const char *path, tileset_t *out);

/* DC.EXE 0x4758b0: height = flight_ticks * table[index] / 64 in 8.8 cells. */
static const fixed_t artillery_arc[] = {
    25*4,150*4,280*4,390*4,480*4,550*4,600*4,630*4,640*4,
    630*4,600*4,550*4,480*4,390*4,280*4,150*4,25*4
};
static const uint8_t artillery_blast[] = {
    10,25,50,25,10, 25,50,75,50,25, 50,75,100,75,50,
    25,50,75,50,25, 10,25,50,25,10
};
static const uint8_t mine_blast[] = {
    5,10,15,20,15,10,5, 10,15,30,50,30,15,10,
    15,30,75,90,75,30,15, 20,50,90,100,90,50,20,
    15,30,75,90,75,30,15, 10,15,30,50,30,15,10,
    5,10,15,20,15,10,5
};
/* MBULLET.TXT classes 3 and 6, converted to the native 8.8 multipliers. */
static const uint16_t artillery_damage[] = {256,89,0,64,179,460,38,128,0,25};
static const uint16_t mine_damage[] = {419,64,0,64,128,332,33,5,0,5};
static const uint16_t turret_damage[] = {256,84,128,84,168,204,84,128,0,84};
static const uint16_t bomb_damage[] = {17,64,0,20,64,115,12,64,0,12};
static const uint8_t bomb_blast[] = {0,0,0,0,100,0,0,0,0};
/* DC.EXE 0x4758f4; flight mode 2 advances four entries each native tick. */
static const int8_t rocket_weave[] = {
    0,19,38,55,70,83,92,98,99,98,92,83,70,55,38,19,
    0,-19,-38,-55,-70,-83,-92,-98,-99,-98,-92,-83,-70,-55,-38,-19
};

const mobjtype_t DARK_COLONY_ACTOR_TYPES[] = {
    {
        .id = MT_TROOPER,
        .defense = {256,204,170},
        .sight = { 7, 4, false },
        .native_type_id = 0,
        .damage_action = A_DC_Damage,
        .name = "Trooper",
        .sprite_name = "SPRITES/TRSC.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        /* GAMESTAT.TXT speed is 8.8 map units per native world tick. */
        .speed = DC_SPEED(25),
        .max_hp = 800,
        .attack = { .range = 4, .damage = 100, .upgrade_damage = {125,150}, .cooldown_ms = 15 * 66 },
    },
    { .id = MT_BEACON, .native_type_id = 84,
      .sight = { 8, 5, false },
      .name = "Beacon", .sprite_name = "SPRITES/BEAC.SPR", .max_hp = 800,
      .traits = MF_RENDERABLE | MF_NOBLOCKMAP },
    { .id = MT_VENT, .native_type_id = OBJECT_TYPE_PETRA7_VENT,
      .name = "Petra-7 vent", .sprite_name = "SPRITES/VENT.SPR",
      .traits = MF_RENDERABLE | MF_NOBLOCKMAP },
    { .id = MT_PRODUCTION_RELEASE, .name = "Production release",
      .traits = MF_RENDERABLE | MF_NOBLOCKMAP },
    { .id = MT_BLOOD, .name = "Blood", .traits = MF_RENDERABLE | MF_NOBLOCKMAP },
    {
        .id = MT_GREY,
        .defense = {256,204,170},
        .sight = { 4, 7, false },
        .native_type_id = 8,
        .damage_action = A_DC_Damage,
        .name = "Grey",
        .sprite_name = "SPRITES/GRAY.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = DC_SPEED(25),
        .max_hp = 800,
        .attack = { .range = 4, .damage = 100, .upgrade_damage = {125,150}, .cooldown_ms = 15 * 66 },
    },
    {
        .id = MT_EXPLOITER,
        .armor_class = 5,
        .sight = { 6, 4, false },
        .native_type_id = 6,
        .damage_action = A_DC_Damage,
        .name = "Exploiter",
        .sprite_name = "SPRITES/EXPL.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_HARVESTER,
        /* The gameplay tuning uses the documented heavy-harvester rate. */
        .speed = 3.5f,
        .max_hp = 800,
        .harvest = { .state_id = S_EXPL_DEPLOY1 },
    },
    {
        .id = MT_REAPER,
        .defense = {256,204,170},
        .sight = { 7, 4, false },
        .native_type_id = 2,
        .damage_action = A_DC_Damage,
        .name = "Reaper",
        .armor_class = 1,
        .sprite_name = "SPRITES/REAP.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = DC_SPEED(30),
        .max_hp = 800,
        .attack = { .range = 2, .damage = 100, .upgrade_damage = {125,150}, .cooldown_ms = 15 * 66 },
    },
    {
        .id = MT_THUNDERBOLT,
        .turn_step = (uint64_t)5 * (1u << 24) * 1000 / (66 * RTS_TICRATE),
        .defense = {256,204,170},
        .sight = { 7, 4, false },
        .native_type_id = 3,
        .damage_action = A_DC_Damage,
        .name = "Barrager",
        .armor_class = 3,
        .sprite_name = "SPRITES/BARR.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = DC_SPEED(15),
        .max_hp = 400,
        .attack = { .range = 12.0f, .damage = 250, .cooldown_ms = 75 * 66,
                    .projectile_type = MT_CANNONBALL },
    },
    {
        .id = MT_CYBORG,
        .defense = {256,204,170},
        .sight = { 10, 10, false },
        .native_type_id = 4,
        .damage_action = A_DC_Damage,
        .name = "Cyborg",
        .armor_class = 4,
        .sprite_name = "SPRITES/SARG.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_DETECTOR |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = DC_SPEED(45),
        .max_hp = 1200,
        .attack = { .range = 8, .damage = 200, .upgrade_damage = {250,250}, .cooldown_ms = 15 * 66 },
    },
    {
        .id = MT_SCOUT,
        .turn_step = (uint64_t)10 * (1u << 24) * 1000 / (66 * RTS_TICRATE),
        .defense = {256,204,170},
        .sight = { 8, 8, true },
        .native_type_id = 5,
        .damage_action = A_DC_Damage,
        .name = "Osprey",
        .armor_class = 2,
        .sprite_name = "SPRITES/SCGM.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_FLY |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = DC_SPEED(47),
        .max_hp = 800,
        .attack = { .range = 2, .damage = 100, .cooldown_ms = 10 * 66,
                    .projectile_type = MT_SCOUT_BOMB, .shots = 3, .reload_ms = 30 * 66 },
    },
    {
        .id = MT_EXCOPOD,
        .sight = { 9, 6, true },
        .native_type_id = 16,
        .armor_class = 9,
        .damage_action = A_DC_Damage,
        .name = "Exco Center",
        .sprite_name = "SPRITES/HUBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 4800,
    },
    {
        .id = MT_BRRKPOD,
        .sight = { 9, 6, true },
        .native_type_id = 17,
        .armor_class = 9,
        .damage_action = A_DC_Damage,
        .name = "Barracks",
        .sprite_name = "SPRITES/HUBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_ROBOPOD,
        .sight = { 9, 6, true },
        .native_type_id = 18,
        .armor_class = 9,
        .damage_action = A_DC_Damage,
        .name = "Robot Factory",
        .sprite_name = "SPRITES/HUBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_ROBOPOD2,
        .sight = { 9, 6, true },
        .native_type_id = 19,
        .armor_class = 9,
        .damage_action = A_DC_Damage,
        .name = "Robot Factory II",
        .sprite_name = "SPRITES/HUBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_SCNCPOD,
        .sight = { 9, 6, true },
        .native_type_id = 20,
        .armor_class = 9,
        .damage_action = A_DC_Damage,
        .name = "Science Pod",
        .sprite_name = "SPRITES/HUBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_SCNCPOD2,
        .sight = { 9, 6, true },
        .native_type_id = 21,
        .armor_class = 9,
        .damage_action = A_DC_Damage,
        .name = "Science Pod II",
        .sprite_name = "SPRITES/HUBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_RSCHPOD,
        .sight = { 9, 6, true },
        .native_type_id = 22,
        .armor_class = 9,
        .damage_action = A_DC_Damage,
        .name = "Research Pod",
        .sprite_name = "SPRITES/HUBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_ALIEN_MINDHIVE,
        .sight = { 6, 9, true },
        .native_type_id = 28,
        .armor_class = 9,
        .damage_action = A_DC_Damage,
        .name = "Mind Hive",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 4800,
    },
    {
        .id = MT_ALIEN_WARHIVE,
        .sight = { 6, 9, true },
        .native_type_id = 29,
        .armor_class = 9,
        .damage_action = A_DC_Damage,
        .name = "Warrior Hive",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_ALIEN_BRDRHIVE,
        .sight = { 6, 9, true },
        .native_type_id = 30,
        .armor_class = 9,
        .damage_action = A_DC_Damage,
        .name = "Breeder Hive",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_ALIEN_BRDRHIVE2,
        .sight = { 6, 9, true },
        .native_type_id = 31,
        .armor_class = 9,
        .damage_action = A_DC_Damage,
        .name = "Breeder Hive II",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_ALIEN_MINDHIVE2,
        .sight = { 6, 9, true },
        .native_type_id = 32,
        .armor_class = 9,
        .damage_action = A_DC_Damage,
        .name = "Mind Hive II",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_ALIEN_MINDHIVE3,
        .sight = { 6, 9, true },
        .native_type_id = 33,
        .armor_class = 9,
        .damage_action = A_DC_Damage,
        .name = "Mind Hive III",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_ALIEN_RSCHIVE,
        .sight = { 6, 9, true },
        .native_type_id = 34,
        .armor_class = 9,
        .damage_action = A_DC_Damage,
        .name = "Research Hive",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_COMMS_DISH,
        .sight = { 8, 5, false },
        .native_type_id = 86,
        .damage_action = A_DC_Damage,
        .name = "Communication Dish",
        .sprite_name = "SPRITES/DISH.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 1200,
    },
    {
        .id = MT_CITY_TOWER,
        .sight = { 9, 6, true },
        .native_type_id = 81,
        .damage_action = A_DC_Damage,
        .name = "City Tower",
        .sprite_name = "SPRITES/TOWR.SPR",
        .traits = MF_RENDERABLE,
        .max_hp = 1600,
    },
    {
        .id = MT_ORTU,
        .turn_step = (uint64_t)10 * (1u << 24) * 1000 / (66 * RTS_TICRATE),
        .defense = {256,204,170},
        .sight = { 8, 8, true },
        .native_type_id = 13,
        .damage_action = A_DC_Damage,
        .name = "Saucer Scout",
        .armor_class = 2,
        .sprite_name = "SPRITES/ORTU.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_FLY |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = DC_SPEED(47),
        .max_hp = 800,
        .attack = { .range = 2, .damage = 100, .cooldown_ms = 10 * 66,
                    .projectile_type = MT_SCOUT_BOMB, .shots = 3, .reload_ms = 30 * 66 },
    },
    {
        .id = MT_SLUG,
        .armor_class = 5,
        .sight = { 4, 6, false },
        .native_type_id = 14,
        .damage_action = A_DC_Damage,
        .name = "Alien Worker",
        .sprite_name = "SPRITES/SLUG.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_HARVESTER,
        .speed = DC_SPEED(40),
        .max_hp = 800,
        .harvest = { .state_id = S_SLUG_DEPLOY1 },
    },
    {
        .id = MT_MOBILE_TOWER,
        .sight = { 9, 5, false },
        .native_type_id = 41,
        .damage_action = A_DC_Damage,
        .name = "Mobile Tower",
        .sprite_name = "SPRITES/TURR.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK | MF_TURRET,
        .turn_step = (uint64_t)5 * (1u << 24) * 1000 / (66 * RTS_TICRATE),
        .armor_class = 6,
        .max_hp = 800,
        .attack = { .range = 6.0f, .damage = 100, .cooldown_ms = 15 * 66,
                    .projectile_type = MT_TOWER_ROCKET },
    },
    {
        .id = MT_DROPSHIP,
        .armor_class = 2,
        .sight = { 8, 8, true },
        .native_type_id = 92,
        .damage_action = A_DC_Damage,
        .name = "Dropship",
        .sprite_name = "SPRITES/DROP.SPR",
        .traits = MF_RENDERABLE | MF_MOBILE | MF_FLY,
        .speed = 4.0f,
        .max_hp = 800,
    },
    {
        .id = MT_DROP_LINK,
        .sight = { 8, 5, false },
        .native_type_id = 89,
        .damage_action = A_DC_Damage,
        .name = "Dropship Link",
        .sprite_name = "SPRITES/CENT.SPR",
        .traits = MF_RENDERABLE,
        .max_hp = 800,
    },
    {
        .id = MT_ALIEN_COM,
        .sight = { 8, 5, false },
        .native_type_id = 91,
        .damage_action = A_DC_Damage,
        .name = "Alien Com Tower",
        .sprite_name = "SPRITES/TONG.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 800,
    },
    {
        .id = MT_VISION_SIGHT,
        .sight = { 8, 8, true },
        .native_type_id = 94,
        .damage_action = A_DC_Damage,
        .name = "Vision Sight",
        .sprite_name = "SPRITES/DOTT.SPR",
        .traits = MF_RENDERABLE,
        .max_hp = 300,
    },
    {
        .id = MT_CANNONBALL,
        .name = "Cannonball",
        .sprite_name = "SPRITES/BARR.SPR",
        .traits = MF_RENDERABLE | MF_MISSILE,
        .max_hp = 1,
        .missile = { .step = 60 * FIXED_ONE / 256, .period_ms = 66,
                     .lifetime = (12 * 256 + 1024) / 60 + 1, .timed = true,
                     .arc = artillery_arc, .arc_count = 17 },
        .blast = { artillery_blast, 5, artillery_damage, 10 },
    },
    {
        .id = MT_SENTINEL,
        .defense = {256,213,182},
        .sight = { 6, 4, false },
        .native_type_id = 43,
        .damage_action = A_DC_Damage,
        .name = "Sentinel",
        .sprite_name = "SPRITES/ENGI.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_DETECTOR,
        .speed = DC_SPEED(30),
        .max_hp = 800,
        .armor_class = 5,
        .deploy = { S_ENGI_DEPLOY1, MT_HUMAN_MINE },
    },
    {
        .id = MT_MEDI_CRAFT,
        .turn_step = (uint64_t)10 * (1u << 24) * 1000 / (66 * RTS_TICRATE),
        .defense = {256,204,170},
        .sight = { 5, 3, true },
        .armor_class = 2,
        .native_type_id = 49,
        .damage_action = A_DC_Damage,
        .name = "Medi-craft",
        .sprite_name = "SPRITES/BEON.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_FLY,
        .speed = DC_SPEED(47),
        .max_hp = 400,
    },
    /* GAMESTAT.TXT native types 9..12, 44 and 50; authored C gameplay stats. */
    { .id = MT_XENOWORT, .native_type_id = 9, .sight = {4, 7, false},
      .damage_action = A_DC_Damage, .name = "Xenowort", .sprite_name = "SPRITES/XENO.SPR",
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
      .speed = DC_SPEED(15), .max_hp = 800,
      .deploy = { S_XENO_DEPLOY1, MT_XENO_TOWER } },
    { .id = MT_SY_DEMON, .native_type_id = 10, .sight = {4, 7, false},
      .defense = {256,204,170},
      .damage_action = A_DC_Damage, .name = "Sy-Demon", .sprite_name = "SPRITES/SCYT.SPR",
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = DC_SPEED(36), .max_hp = 800,
      .armor_class = 1,
      .attack = { .range = 1, .damage = 100, .upgrade_damage = {125,150}, .cooldown_ms = 15 * 66 } },
    { .id = MT_ATRIL, .native_type_id = 11, .sight = {4, 7, false},
      .turn_step = (uint64_t)5 * (1u << 24) * 1000 / (66 * RTS_TICRATE),
      .defense = {256,204,170},
      .damage_action = A_DC_Damage, .name = "Atril", .sprite_name = "SPRITES/ATRIL.SPR",
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = DC_SPEED(15), .max_hp = 400,
      .armor_class = 3,
      .attack = { .range = 12, .damage = 250, .cooldown_ms = 75 * 66,
                  .projectile_type = MT_PUS_BOMB } },
    { .id = MT_GORREM, .native_type_id = 12, .sight = {10, 10, false},
      .defense = {256,204,170},
      .damage_action = A_DC_Damage, .name = "Gorrem", .sprite_name = "SPRITES/PSYC.SPR",
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_DETECTOR,
      .speed = DC_SPEED(47), .max_hp = 800,
      .armor_class = 4,
      .attack = { .range = 9, .damage = 200, .upgrade_damage = {250,300}, .cooldown_ms = 30 * 66 } },
    { .id = MT_SLOM, .native_type_id = 44, .sight = {4, 6, false},
      .defense = {256,213,182},
      .damage_action = A_DC_Damage, .name = "Slom", .sprite_name = "SPRITES/SLOM.SPR",
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_DETECTOR,
      .speed = DC_SPEED(30), .max_hp = 800, .armor_class = 5,
      .deploy = { S_SLOM_DEPLOY1, MT_ALIEN_MINE } },
    { .id = MT_ZISP, .native_type_id = 50, .sight = {3, 5, true},
      .turn_step = (uint64_t)10 * (1u << 24) * 1000 / (66 * RTS_TICRATE),
      .defense = {256,204,170},
      .armor_class = 2,
      .damage_action = A_DC_Damage, .name = "Zisp", .sprite_name = "SPRITES/ZISP.SPR",
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_FLY,
      .speed = DC_SPEED(47), .max_hp = 400 },
    { .id = MT_SCOUT_BOMB, .name = "Scout bomb", .max_hp = 1,
      .traits = MF_MISSILE,
      .missile = { .step = 15 * FIXED_ONE / 256, .period_ms = 66,
                   .lifetime = (2 * 256 + 1024) / 15 + 1, .timed = true },
      .blast = { bomb_blast, 3, bomb_damage, 10 } },
    { .id = MT_PUS_BOMB, .name = "Atril bomb", .sprite_name = "SPRITES/ATRIL.SPR",
      .traits = MF_RENDERABLE | MF_MISSILE, .max_hp = 1,
      .missile = { .step = 60 * FIXED_ONE / 256, .period_ms = 66,
                   .lifetime = (12 * 256 + 1024) / 60 + 1, .timed = true,
                   .arc = artillery_arc, .arc_count = 17 },
      .blast = { artillery_blast, 5, artillery_damage, 10 } },
    { .id = MT_TOWER_ROCKET, .name = "Tower rocket", .sprite_name = "SPRITES/TURR.SPR",
      .traits = MF_RENDERABLE | MF_MISSILE, .max_hp = 1,
      .missile = { .step = 60 * FIXED_ONE / 256, .period_ms = 66,
                   .lifetime = (6 * 256 + 1024) / 60 + 1,
                   .weave = rocket_weave, .weave_count = 32, .weave_step = 4,
                   .trail_type = MT_ROCKET_SMOKE },
      .blast = { .damage_factors = turret_damage, .armor_classes = 10 } },
    { .id = MT_XENO_BOLT, .name = "Xenowort bolt", .sprite_name = "SPRITES/XENO.SPR",
      .traits = MF_RENDERABLE | MF_MISSILE, .max_hp = 1,
      .missile = { .step = 60 * FIXED_ONE / 256, .period_ms = 66,
                   .lifetime = (6 * 256 + 1024) / 60 + 1 },
      .blast = { .damage_factors = turret_damage, .armor_classes = 10 } },
    { .id = MT_MINE_BLAST, .name = "Mine charge",
      .traits = MF_MISSILE, .max_hp = 1,
      .missile = { .step = 60 * FIXED_ONE / 256, .period_ms = 66, .timed = true,
                   .lifetime = (256 + 1024) / 60 + 1 },
      .blast = { mine_blast, 7, mine_damage, 10 } },
    { .id = MT_TURRET_CARRIER, .native_type_id = 1, .name = "Firestorm",
      .sprite_name = "SPRITES/TURR.SPR", .sight = {7,4,false},
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_MOBILE,
      .speed = DC_SPEED(15), .max_hp = 800,
      .deploy = { S_TURR_DEPLOY1, MT_MOBILE_TOWER } },
    { .id = MT_XENO_TOWER, .native_type_id = 42, .name = "Deployed Xenowort",
      .sprite_name = "SPRITES/XENO.SPR", .sight = {5,9,false},
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK | MF_TURRET,
      .turn_step = (uint64_t)5 * (1u << 24) * 1000 / (66 * RTS_TICRATE),
      .max_hp = 800, .armor_class = 6,
      .attack = { .range = 6, .damage = 100, .cooldown_ms = 15 * 66,
                  .projectile_type = MT_XENO_BOLT } },
    { .id = MT_ROCKET_SMOKE, .name = "Rocket smoke", .sprite_name = "SPRITES/TURR.SPR",
      .traits = MF_RENDERABLE | MF_NOBLOCKMAP, .max_hp = 1 },
    { .id = MT_HUMAN_MINE, .native_type_id = 45, .name = "Deployed Sentinel",
      .defense = {256,213,182},
      .sprite_name = "SPRITES/ENGI.SPR", .sight = {6,4,false},
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK | MF_LANDMINE,
      .max_hp = 800, .armor_class = 7,
      .attack = { .range = 1, .damage = 1300, .cooldown_ms = 150 * 66,
                  .projectile_type = MT_MINE_BLAST, .health_cost = 300 } },
    { .id = MT_ALIEN_MINE, .native_type_id = 46, .name = "Deployed Slom",
      .defense = {256,213,182},
      .sprite_name = "SPRITES/ENGI.SPR", .sight = {4,6,false},
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK | MF_LANDMINE,
      .max_hp = 800, .armor_class = 7,
      .attack = { .range = 1, .damage = 1300, .cooldown_ms = 150 * 66,
                  .projectile_type = MT_MINE_BLAST, .health_cost = 300 } },
};

const mobjtype_t *actor_type_by_id(uint16_t type_id) {
    for (int i = 0; i < (int)(sizeof(DARK_COLONY_ACTOR_TYPES) / sizeof(DARK_COLONY_ACTOR_TYPES[0])); ++i) {
        if (DARK_COLONY_ACTOR_TYPES[i].id == type_id) return &DARK_COLONY_ACTOR_TYPES[i];
    }
    return NULL;
}

bool DC_LoadFont(const char *data_root, const char *name,
                 bitmapfont_t *font) {
    if (!data_root || !font) return false;
    memset(font, 0, sizeof(*font));
    for (int i = 0; i < 128; ++i) font->glyph_index[i] = -1;
    char path[1024];
    M_PathJoin(path, sizeof(path), data_root, name);
    if (!DC_LoadSpriteImage(path, &font->sprite)) return false;
    const int font_offset = 31;
    int max_w = 0, max_h = 0;
    for (int ch = font_offset; ch < 128; ++ch) {
        int frame = ch - font_offset;
        if (frame >= font->sprite.numlumps) break;
        font->glyph_index[ch] = frame;
        irect_t rect = font->sprite.cells[frame].rect;
        if (rect.w > max_w) max_w = rect.w;
        if (rect.h > max_h) max_h = rect.h;
    }
    font->draw_divisor = 1;
    font->native_origin = true;
    font->glyph_size = (isize2_t){max_w, max_h};
    font->line_h = font->glyph_size.h + 1;
    for (int ch = 0; ch < 128; ++ch) {
        /* 0x424446: every glyph, including space, advances max width + 1. */
        font->glyph_width[ch] = (uint8_t)(font->glyph_size.w + 1);
    }
    return true;
}

/* ── game identity (Doom-style externs) ─────────────────────────────────── */

const char *const g_game_id            = "dark-colony";
const char *const g_game_name          = "Dark Colony";
const char *const g_game_default_root  = "data/DCOLONY";
const char *const g_game_default_map   = "SCENARIO/HUMAN/HUMAN01.MAP";
const char *const g_game_default_sprite = "SPRITES/TROOPER1.SPR";
const int g_cell_w = 32;
const int g_cell_h = 32;
const uint16_t g_debug_enemy_type = MT_GREY;
static state_t runtime_states[NUMSTATES];
static gameinfo_t runtime_info;

const gameinfo_t *gameinfo = &runtime_info;
const mobjtype_t *const actor_types =
    (const mobjtype_t *)DARK_COLONY_ACTOR_TYPES;
const int num_actor_types =
    (int)(sizeof(DARK_COLONY_ACTOR_TYPES) / sizeof(DARK_COLONY_ACTOR_TYPES[0]));
const uidefinition_t *const gameui = NULL;

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

void G_InitGame(void) {
    static bool initialized;
    gameinfo = &runtime_info;
    if (initialized) return;

    memcpy(runtime_states, states, sizeof(runtime_states));
    runtime_info = game_info;
    runtime_info.states = runtime_states;
    initialized = true;
}

bool G_DoLoadLevel(const char *path, level_t *out) {
    if (!load_dark_colony_map(path, out)) return false;
    char root[1024];
    snprintf(root, sizeof(root), "%s", path);
    char *scenario = strstr(root, "/SCENARIO/");
    if (!scenario) scenario = strstr(root, "/scenario/");
    if (scenario) {
        *scenario = '\0';
        if (!DC_LoadWeapons(out, root)) { P_FreeLevel(out); return false; }
    }
    out->mission = load_mission(path);
    out->destroy_mission = destroy_mission;
    return true;
}

bool W_LoadAssets(SDL_Renderer *renderer, const char *root, const level_t *map,
                  const char *sprite, tileset_t *tileset, spritesheet_t *unit_sprite) {
    if (!load_render_tables(root, map->tileset_name)) {
        fprintf(stderr, "failed to load Dark Colony render tables for %s\n", map->tileset_name);
        return false;
    }
    char bts_path[1024];
    snprintf(bts_path, sizeof(bts_path), "%s/SCENARIO/%s.BTS", root, map->tileset_name);
    if (!load_dark_colony_tileset(bts_path, tileset)) return false;
    if (!R_UploadTileset(renderer, tileset))
        fprintf(stderr, "warning: Dark Colony tileset atlas was not uploaded\n");

    char sprite_path[1024];
    uint32_t sprite_palette[256] = { 0 };
    if (sprite[0] == '/') {
        snprintf(sprite_path, sizeof(sprite_path), "%s", sprite);
    } else {
        M_PathJoin(sprite_path, sizeof(sprite_path), root, sprite);
    }
    if (!load_dark_colony_sprite(sprite_path, unit_sprite, sprite_palette)) {
        fprintf(stderr, "failed to load %s\n", sprite_path);
        R_FreeTileset(tileset);
        return false;
    }
    return true;
}

int P_LoadThings(const char *path) {
    (void)path;
    return load_dark_colony_initial_units();
}

bool R_InitSprites(SDL_Renderer *renderer, const char *root, const level_t *map,
                          mobj_t *const *mobjs, int count, spritecache_t *cache) {
    (void)renderer;
    return load_dark_colony_unit_sprites(root, map, mobjs, count, cache);
}

bool HU_LoadFont(SDL_Renderer *renderer, const char *root, bitmapfont_t *font) {
    (void)renderer;
    return DC_LoadFont(root, "INTRFACE/MFONTO7.SPR", font);
}

void G_MissionTicker(level_t *map, mobj_t *const *mobjs, int *count,
                     hudtext_t *hud, float dt) {
    if (!map || !map->mission) return;
    update_mission(map, mobjs, count, hud, dt);
}

void *G_InitCustomUI(app_t *app, const char *data_root) {
    return DC_SB_Init(app, data_root);
}

bool G_CustomUIResponder(void *ui, app_t *app, level_t *map,
                         mobj_t *const *units, int unit_count, const SDL_Event *event) {
    return DC_SB_Responder(ui, app, map, units, unit_count, event);
}

void G_CustomUITicker(void *ui) {
    (void)ui;
}

void G_CustomUIDrawer(void *ui, app_t *app, const level_t *map,
                      mobj_t *const *units, int unit_count,
                      const spritecache_t *sprites, const hudtext_t *hud) {
    DC_SB_Drawer(ui, app, map, units, unit_count, sprites, hud);
}

bool G_UpdateProduction(void *ui, level_t *map, mobj_t *const *units, int *unit_count,
                        float dt) {
    if (!ui) return false;
    return G_ModelUpdateProduction(map, units, unit_count, dt);
}

void G_ShutdownCustomUI(void *ui) {
    DC_SB_Shutdown(ui);
}

int G_WorldViewportWidth(const app_t *app) {
    return DC_SB_WorldViewportWidth(app);
}

bool G_LoadMenuSprite(SDL_Renderer *renderer, const char *root,
                      const char *name, spritesheet_t *out) {
    (void)renderer; (void)root; (void)name; (void)out;
    return false;
}
