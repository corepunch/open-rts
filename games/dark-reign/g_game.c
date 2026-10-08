#include "engine.h"
#include "dark-reign.h"
#include "info.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

/* Native AIP values recovered from the shipped FG AIP files.  These are
 * configuration inputs for the generic AI layer, not executable-specific
 * behavior hidden in the renderer or map loader. */
const ai_profile_t g_dark_reign_ai_profiles[] = {
    {
        .name = "easy",
        .recompute_strategy_period = 200,
        .ground_unit_threat = 1, .threat_priority = 1, .distance_priority = -2,
        .defend_buildings_priority = 500, .attack_enemy_base_priority = 78,
        .exploration_priority = 600, .perimeter_priority = 5000,
        .resource_priority = 400, .danger_priority = 100,
        .min_matching_force_ratio = 1.0, .max_matching_force_ratio = 2.0,
        .min_building_defense_force = 40, .max_building_defense_force = 100,
        .min_exploration_force = 1, .max_exploration_force = 100,
        .min_perimeter_force = 40, .max_perimeter_force = 70,
        .min_resource_force = 100, .max_resource_force = 300,
        .repair_buildings = false,
    },
    {
        .name = "medium",
        .recompute_strategy_period = 50,
        .ground_unit_threat = 10, .threat_priority = 10, .distance_priority = -2,
        .defend_buildings_priority = 500, .attack_enemy_base_priority = 78,
        .exploration_priority = 600, .perimeter_priority = 100,
        .resource_priority = 400, .danger_priority = 1000,
        .min_matching_force_ratio = 2.0, .max_matching_force_ratio = 7.0,
        .min_building_defense_force = 40, .max_building_defense_force = 100,
        .min_exploration_force = 1, .max_exploration_force = 200,
        .min_perimeter_force = 40, .max_perimeter_force = 70,
        .min_resource_force = 300, .max_resource_force = 1000,
        .repair_buildings = true,
    },
    {
        .name = "defensive",
        .recompute_strategy_period = 50,
        .ground_unit_threat = 200, .threat_priority = 1, .distance_priority = -1,
        .defend_buildings_priority = 700, .attack_enemy_base_priority = 0,
        .exploration_priority = 500, .perimeter_priority = 1000,
        .resource_priority = 700, .danger_priority = 1000,
        .min_matching_force_ratio = 2.0, .max_matching_force_ratio = 7.0,
        .min_building_defense_force = 140, .max_building_defense_force = 280,
        .min_exploration_force = 70, .max_exploration_force = 70,
        .min_perimeter_force = 340, .max_perimeter_force = 650,
        .min_resource_force = 70, .max_resource_force = 140,
        .repair_buildings = true,
    },
    {
        .name = "aggressive",
        .recompute_strategy_period = 40,
        .ground_unit_threat = 1, .threat_priority = 1, .distance_priority = -2,
        .defend_buildings_priority = 500, .attack_enemy_base_priority = 78,
        .exploration_priority = 600, .perimeter_priority = 5000,
        .resource_priority = 400, .danger_priority = 100,
        .min_matching_force_ratio = 1.0, .max_matching_force_ratio = 2.0,
        .min_building_defense_force = 40, .max_building_defense_force = 100,
        .min_exploration_force = 1, .max_exploration_force = 100,
        .min_perimeter_force = 40, .max_perimeter_force = 70,
        .min_resource_force = 100, .max_resource_force = 300,
        .repair_buildings = false,
    },
};

const int g_dark_reign_ai_profile_count =
    (int)(sizeof(g_dark_reign_ai_profiles) / sizeof(g_dark_reign_ai_profiles[0]));

bool load_dark_map(const char *map_path, level_t *out);
bool plugin_load_assets(const char *data_root, const level_t *map,
                        const char *sprite_name, tileset_t *tileset,
                        spritesheet_t *unit_sprite);
int load_dark_reign_initial_units(const char *map_path);
bool load_dark_reign_decoration_sprites(const char *data_root, const level_t *map,
                                        mobj_t *const *units, int unit_count,
                                        spritecache_t *cache);

/* UNITS.TXT SetPhysics speed: tile-step progress per tic, step done at 100 (0x004c00c0, 0x004c1267). */
#define DR_SPEED(maxspeed) ((float)(maxspeed) * RTS_TICRATE / 100.0f)

static const mobjtype_t DARK_REIGN_ACTOR_TYPES[] = {
    /* === Special / support units === */
    {
        .id = MT_FG_CONSTRUCTION_CREW,
        .name = "Construction Rig",
        .sprite_name = "ucfcnst0.spr",
        .shadow_name = "ucfcnsh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = DR_SPEED(6),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 100,
    },
    {
        .id = MT_FG_FREIGHTER,
        .name = "Freighter",
        .sprite_name = "ucfrgst0.spr",
        .shadow_name = "ucfrgst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER,
        .speed = DR_SPEED(10),
        .move_class = DR_MOVE_WHEEL,
        .max_hp = 750,
        .turn_step = (UINT32_C(0x1000000) * 10 / 360) << 8,
        .harvest = { .state_id = S_UCFRGST0_HARVEST1, .unload_state_id = S_UCFRGST0_HARVEST1,
                     .dock_angle = ANG90 + ANG45,
                     .resources = { {750, 270, 270}, {50, 10, 25} } },
    },
    {   /* Laser-armed hover harvester */
        .id = MT_FG_HOVER_FREIGHTER,
        .name = "Hover Freighter",
        .sprite_name = "uchfrst0.spr",
        .shadow_name = "uchfrst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER | MF_ATTACK,
        .speed = DR_SPEED(16),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 500,
        .turn_step = (UINT32_C(0x1000000) * 10 / 360) << 8,
        .attack = { .range = 4.0f, .damage = 11, .cooldown_ms = 267 },
        .harvest = { .state_id = S_UCHFRST0_HARVEST1, .unload_state_id = S_UCHFRST0_HARVEST1,
                     .dock_angle = ANG90 + ANG45,
                     .resources = { {750, 270, 270}, {50, 10, 25} } },
    },
    /* === Infantry === */
    {   /* LaserRifle: range 4, 267ms cd, 11 dmg */
        .id = MT_FG_RAIDER,
        .name = "Raider",
        .sprite_name = "ufradst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_HUMAN,
        .speed = DR_SPEED(8),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 100,
        .attack = { .range = 4.0f, .damage = 11, .cooldown_ms = 267 },
    },
    {   /* RailGun: range 5, 367ms cd, 11 dmg */
        .id = MT_FG_MERCENARY,
        .name = "Mercenary",
        .sprite_name = "ufmrcst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_HUMAN,
        .speed = DR_SPEED(8),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 125,
        .attack = { .range = 5.0f, .damage = 11, .cooldown_ms = 367 },
    },
    {   /* SniperRifle: range 8, 1667ms cd, 150 dmg */
        .id = MT_FG_SNIPER,
        .name = "Sniper",
        .sprite_name = "ufsnpst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_HUMAN | MF_NOAUTOTARGET,
        .speed = DR_SPEED(12),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 100,
        .attack = { .range = 8.0f, .damage = 150, .cooldown_ms = 1667 },
    },
    {   /* Recon only — no weapon */
        .id = MT_FG_SCOUT,
        .name = "Scout",
        .sprite_name = "ufsctst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HUMAN,
        .speed = DR_SPEED(12),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 66,
    },
    {   /* MedicHeal — support, no offensive attack */
        .id = MT_FG_MEDIC,
        .name = "Field Medic",
        .sprite_name = "ufmedst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HEAL | MF_HUMAN,
        .speed = DR_SPEED(8),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 66,
        .attack = { .range = 1, .damage = -20, .cooldown_ms = 334 },
    },
    {   /* Sabotage ability — no ranged weapon */
        .id = MT_FG_SABOTEUR,
        .name = "Saboteur",
        .sprite_name = "ufsabst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HUMAN,
        .speed = DR_SPEED(12),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 100,
    },
    {   /* MechanicRepair — support, no offensive attack */
        .id = MT_FG_MECHANIC,
        .name = "Mechanic",
        .sprite_name = "ufmecst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_REPAIR | MF_HUMAN,
        .speed = DR_SPEED(12),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 66,
        .attack = { .range = 1, .damage = -5, .cooldown_ms = 334 },
    },
    {   /* SuicideNuke: range 2, 1667ms, 180 dmg, large AoE */
        .id = MT_FG_MARTYR,
        .name = "Martyr",
        .sprite_name = "ufmtrst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_HUMAN,
        .speed = DR_SPEED(16),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 100,
        .attack = { .range = 2.0f, .damage = 180, .cooldown_ms = 1667 },
    },
    {   /* Infiltrate ability — no ranged weapon */
        .id = MT_FG_SPY,
        .name = "Infiltrator",
        .sprite_name = "ucinfst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HUMAN | MF_NOAUTOTARGET,
        .speed = DR_SPEED(12),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 66,
    },
    /* === Vehicles === */
    {   /* DoubleRailGun: range 5, 433ms cd, 10 dmg */
        .id = MT_FG_SPYDER_BIKE,
        .name = "Spider Bike",
        .sprite_name = "ufspbst0.spr",
        .shadow_name = "ufspbsh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(24),
        .move_class = DR_MOVE_WHEELA,
        .max_hp = 133,
        .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 433 },
    },
    {   /* Rapid armored transport — no weapon */
        .id = MT_FG_IFV,
        .name = "RAT",
        .sprite_name = "ufratst0.spr",
        .shadow_name = "ufratst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = DR_SPEED(18),
        .move_class = DR_MOVE_WHEELF,
        .max_hp = 200,
    },
    {   /* SkirmishGun (dual): range 6, 667ms cd, 14 dmg */
        .id = MT_FG_MEDIUM_TANK,
        .name = "Skirmish Tank",
        .sprite_name = "ufsktst0.spr",
        .shadow_name = "ufsktst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(16),
        .move_class = DR_MOVE_TRACK,
        .max_hp = 133,
        .attack = { .range = 6.0f, .damage = 14, .cooldown_ms = 667 },
    },
    {   /* TankHunterGun: range 3, 667ms cd, 60 dmg — high anti-armor */
        .id = MT_FG_TANK_HUNTER,
        .name = "Tank Hunter",
        .sprite_name = "ufthnst0.spr",
        .shadow_name = "ufthnst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(20),
        .move_class = DR_MOVE_TRACK,
        .max_hp = 150,
        .attack = { .range = 3.0f, .damage = 60, .cooldown_ms = 667 },
    },
    {   /* PhaseTankCannon: range 6, 433ms cd, 30 dmg */
        .id = MT_FG_PHASE_TANK,
        .name = "Phase Tank",
        .sprite_name = "ufphtst0.spr",
        .shadow_name = "ufphtst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(12),
        .move_class = DR_MOVE_TRACK,
        .max_hp = 166,
        .attack = { .range = 6.0f, .damage = 30, .cooldown_ms = 433 },
    },
    {   /* Chaff: range 8, 500ms cd, 8 dmg — anti-air */
        .id = MT_FG_MAD,
        .name = "Flak Jack",
        .sprite_name = "ufflkst0.spr",
        .shadow_name = "ufflksh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(12),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 100,
        .attack = { .range = 8.0f, .damage = 8, .cooldown_ms = 500 },
    },
    {   /* TripleRailGun: range 8, 667ms cd, 24 dmg */
        .id = MT_FG_TRIPLE_RAIL_TANK,
        .name = "Triple Rail Tank",
        .sprite_name = "uftrtst0.spr",
        .shadow_name = "uftrtst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(12),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 200,
        .attack = { .range = 8.0f, .damage = 24, .cooldown_ms = 667 },
    },
    {   /* ArtilleryShell: range 45, 2667ms cd, 30 dmg, large AoE */
        .id = MT_FG_SPA,
        .name = "Hellstorm Artillery",
        .sprite_name = "uffarst0.spr",
        .shadow_name = "uffarst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(8),
        .move_class = DR_MOVE_TRACK,
        .max_hp = 133,
        .attack = { .range = 45.0f, .damage = 30, .cooldown_ms = 2667 },
    },
    /* === Air units === */
    {   /* BkLaser: range 5, 233ms cd, 10 dmg */
        .id = MT_FG_SKY_BIKE,
        .name = "Sky Bike",
        .sprite_name = "ufskbst0.spr",
        .shadow_name = "ufskbst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_FLY | MF_NOAUTOTARGET,
        .speed = DR_SPEED(28),
        .move_class = DR_MOVE_FLYING,
        .max_hp = 100,
        .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 233 },
    },
    {   /* OutriderMissile: range 5, 333ms cd, 20 dmg */
        .id = MT_FG_OUTRIDER,
        .name = "Outrider",
        .sprite_name = "ufoutst0.spr",
        .shadow_name = "ufoutst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_FLY | MF_NOAUTOTARGET,
        .speed = DR_SPEED(24),
        .move_class = DR_MOVE_FLYING,
        .max_hp = 200,
        .attack = { .range = 5.0f, .damage = 20, .cooldown_ms = 333 },
    },
    /* === Experimental / special === */
    {   /* SeismicWave: range 24, slow cd, 17 dmg */
        .id = MT_FG_SHOCKWAVE,
        .name = "Shockwave",
        .sprite_name = "ufswvst0.spr",
        .shadow_name = "ufswvst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_NOAUTOTARGET,
        .speed = DR_SPEED(8),
        .move_class = DR_MOVE_WHEEL,
        .max_hp = 166,
        .attack = { .range = 24.0f, .damage = 17, .cooldown_ms = 2000 },
    },
    {   /* Contaminator: range 1, 67ms cd, 5 dmg — targets buildings */
        .id = MT_FG_CONTAMINATOR,
        .name = "Water Contaminator",
        .sprite_name = "ucwcost0.spr",
        .shadow_name = "ucwcost0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_NOAUTOTARGET,
        .speed = DR_SPEED(4),
        .move_class = DR_MOVE_TRACK,
        .max_hp = 166,
        .attack = { .range = 1.0f, .damage = 5, .cooldown_ms = 67 },
    },
    {   /* Spawned from Phasing Facility — internal tunnel unit */
        .id = MT_FG_UNDERGROUND_TUNNEL,
        .name = "Phase Runner",
        .sprite_name = "ufphrst0.spr",
        .shadow_name = "ufphrst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = DR_SPEED(8),
        .move_class = DR_MOVE_FLYING,
        .max_hp = 150,
    },
    {   /* Base relocation unit */
        .id = MT_FG_BASE_MOVER,
        .name = "Base Mover",
        .sprite_name = "ufbamst0.spr",
        .shadow_name = "ufbamst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = DR_SPEED(6),
        .move_class = DR_MOVE_TRACK,
        .max_hp = 500,
    },
    /* === Imperium infantry (WEAPON.TXT range/damage, firedelay*33ms) === */
    {   /* LaserRifle: range 4, 267ms cd, 11 dmg */
        .id = MT_IMP_GUARDIAN,
        .name = "Guardian",
        .sprite_name = "uigrdst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_HUMAN,
        .speed = DR_SPEED(8),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 100,
        .attack = { .range = 4.0f, .damage = 11, .cooldown_ms = 267 },
    },
    {   /* PlasmaRifle: range 4, 267ms cd, 18 dmg */
        .id = MT_IMP_BION,
        .name = "Bion",
        .sprite_name = "uibonst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_HUMAN,
        .speed = DR_SPEED(6),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 150,
        .attack = { .range = 4.0f, .damage = 18, .cooldown_ms = 267 },
    },
    {   /* PolyAcid: range 5, 667ms cd, 15 dmg */
        .id = MT_IMP_EXTERMINATOR,
        .name = "Exterminator",
        .sprite_name = "uiextst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_HUMAN,
        .speed = DR_SPEED(16),
        .move_class = DR_MOVE_HOVERS,
        .max_hp = 75,
        .attack = { .range = 5.0f, .damage = 15, .cooldown_ms = 667 },
    },
    /* === Imperium vehicles === */
    {   /* LaserCannon: range 6, 433ms cd, 10 dmg */
        .id = MT_IMP_SCOUT_TANK,
        .name = "Scout Tank",
        .sprite_name = "uisttst0.spr",
        .shadow_name = "uisttst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(20),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 150,
        .attack = { .range = 6.0f, .damage = 10, .cooldown_ms = 433 },
    },
    {   /* LaserRifle: range 4, 267ms cd, 11 dmg */
        .id = MT_IMP_ASSAULT_VEHICLE,
        .name = "Invader Troop Transport",
        .sprite_name = "uiittst0.spr",
        .shadow_name = "uiittst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(20),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 150,
        .attack = { .range = 4.0f, .damage = 11, .cooldown_ms = 267 },
    },
    {   /* PlasmaCannon: range 5, 500ms cd, 19 dmg */
        .id = MT_IMP_PLASMA_TANK,
        .name = "Plasma Tank",
        .sprite_name = "uipltst0.spr",
        .shadow_name = "uipltst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(16),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 250,
        .attack = { .range = 5.0f, .damage = 19, .cooldown_ms = 500 },
    },
    {   /* AmperAmp has no offense strength — support unit */
        .id = MT_IMP_AMPER,
        .name = "Amper",
        .sprite_name = "uiampst0.spr",
        .shadow_name = "uiampsh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = DR_SPEED(8),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 66,
    },
    {   /* GroundToAirLaser: range 8, 667ms cd, 48 dmg — anti-air */
        .id = MT_IMP_MAD,
        .name = "Imperium MAD",
        .sprite_name = "uimadst0.spr",
        .shadow_name = "uimadst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(16),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 150,
        .attack = { .range = 8.0f, .damage = 48, .cooldown_ms = 667 },
    },
    {   /* Recon only — no weapon in UNITS.TXT */
        .id = MT_IMP_RECON_SAUCER,
        .name = "Recon Drone",
        .sprite_name = "uirdrst0.spr",
        .shadow_name = "uirdrst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_FLY,
        .speed = DR_SPEED(16),
        .move_class = DR_MOVE_FLYING,
        .max_hp = 66,
    },
    {   /* Shredder: melee (Attr 0 0), 10 dmg, 500ms fallback cd */
        .id = MT_IMP_SHREDDER,
        .name = "Shredder",
        .sprite_name = "uishrst0.spr",
        .shadow_name = "uishrst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(20),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 100,
        .attack = { .range = 2.0f, .damage = 10, .cooldown_ms = 500 },
    },
    {   /* Ability transport — no ranged weapon */
        .id = MT_IMP_HOSTAGE_TAKER,
        .name = "Hostage Taker",
        .sprite_name = "uihosst0.spr",
        .shadow_name = "uihosst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = DR_SPEED(20),
        .move_class = DR_MOVE_WHEEL,
        .max_hp = 450,
    },
    {   /* TachyonCannon: range 8, 667ms cd, 30 dmg */
        .id = MT_IMP_TACHYON_TANK,
        .name = "Tachyon Tank",
        .sprite_name = "uitctst0.spr",
        .shadow_name = "uitctst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(12),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 410,
        .attack = { .range = 8.0f, .damage = 30, .cooldown_ms = 667 },
    },
    {   /* IMPArtilleryShell: range 45, 2667ms cd, 30 dmg */
        .id = MT_IMP_SCARAB,
        .name = "S.C.A.R.A.B.",
        .sprite_name = "uiiarst0.spr",
        .shadow_name = "uiiarst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(6),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 133,
        .attack = { .range = 45.0f, .damage = 30, .cooldown_ms = 2667 },
    },
    {   /* CycloneCannon: range 6, 333ms cd, 24 dmg */
        .id = MT_IMP_CYCLONE,
        .name = "Cyclone",
        .sprite_name = "uicycst0.spr",
        .shadow_name = "uicycst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_FLY | MF_NOAUTOTARGET,
        .speed = DR_SPEED(24),
        .move_class = DR_MOVE_FLYING,
        .max_hp = 150,
        .attack = { .range = 6.0f, .damage = 24, .cooldown_ms = 333 },
    },
    {   /* FortressCannon: range 7, 667ms cd, 650 dmg */
        .id = MT_IMP_SKY_FORTRESS,
        .name = "Sky Fortress",
        .sprite_name = "uiskyst0.spr",
        .shadow_name = "uiskyst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_FLY | MF_NOAUTOTARGET,
        .speed = DR_SPEED(10),
        .move_class = DR_MOVE_FLYING,
        .max_hp = 266,
        .attack = { .range = 7.0f, .damage = 650, .cooldown_ms = 667 },
    },
    /* === Imperium shared-sprite units (same bodies as FG) === */
    {
        .id = MT_IMP_CONSTRUCTION_CREW,
        .name = "Imperium Construction Rig",
        .sprite_name = "ucfcnst0.spr",
        .shadow_name = "ucfcnsh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = DR_SPEED(6),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 100,
    },
    {
        .id = MT_IMP_GROUND_TRANSPORTER,
        .name = "Imperium Freighter",
        .sprite_name = "ucfrgst0.spr",
        .shadow_name = "ucfrgst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER,
        .speed = DR_SPEED(10),
        .move_class = DR_MOVE_WHEEL,
        .max_hp = 750,
        .turn_step = (UINT32_C(0x1000000) * 10 / 360) << 8,
        .harvest = { .state_id = S_UCFRGST0_IMP_HARVEST1, .unload_state_id = S_UCFRGST0_IMP_HARVEST1,
                     .dock_angle = ANG90 + ANG45,
                     .resources = { {750, 270, 270}, {50, 10, 25} } },
    },
    {
        .id = MT_IMP_HOVER_TRANSPORTER,
        .name = "Imperium Hover Freighter",
        .sprite_name = "uchfrst0.spr",
        .shadow_name = "uchfrst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER | MF_ATTACK,
        .speed = DR_SPEED(16),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 500,
        .turn_step = (UINT32_C(0x1000000) * 10 / 360) << 8,
        .attack = { .range = 4.0f, .damage = 11, .cooldown_ms = 267 },
        .harvest = { .state_id = S_UCHFRST0_IMP_HARVEST1, .unload_state_id = S_UCHFRST0_IMP_HARVEST1,
                     .dock_angle = ANG90 + ANG45,
                     .resources = { {750, 270, 270}, {50, 10, 25} } },
    },
    {
        .id = MT_IMP_SPY,
        .name = "Imperium Infiltrator",
        .sprite_name = "ucinfst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HUMAN | MF_NOAUTOTARGET,
        .speed = DR_SPEED(12),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 66,
    },
    {   /* SuicideNuke: range 2, 1667ms, 180 dmg */
        .id = MT_IMP_SUICIDE_ZOMBIE,
        .name = "Suicide Zombie",
        .sprite_name = "ufmtrst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_HUMAN,
        .speed = DR_SPEED(6),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 100,
        .attack = { .range = 2.0f, .damage = 180, .cooldown_ms = 1667 },
    },
    {   /* Contaminator: range 1, 67ms cd, 5 dmg */
        .id = MT_IMP_CONTAMINATOR,
        .name = "Imperium Contaminator",
        .sprite_name = "ucwcost0.spr",
        .shadow_name = "ucwcost0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_NOAUTOTARGET,
        .speed = DR_SPEED(4),
        .move_class = DR_MOVE_TRACK,
        .max_hp = 166,
        .attack = { .range = 1.0f, .damage = 5, .cooldown_ms = 67 },
    },
    /* === Imperium decoy mobile units === */
    {
        .id = MT_IMP_ASSAULT_VEHICLE_DECOY,
        .name = "Imperium Assault Vehicle Decoy",
        .sprite_name = "uiittst0.spr",
        .shadow_name = "uiittst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = DR_SPEED(20),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 75,
    },
    {
        .id = MT_IMP_PLASMA_TANK_DECOY,
        .name = "Imperium Plasma Tank Decoy",
        .sprite_name = "uipltst0.spr",
        .shadow_name = "uipltst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = DR_SPEED(16),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 75,
    },
    {
        .id = MT_IMP_TACHYON_TANK_DECOY,
        .name = "Imperium Tachyon Tank Decoy",
        .sprite_name = "uitctst0.spr",
        .shadow_name = "uitctst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = DR_SPEED(16),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 100,
    },
    {
        .id = MT_IMP_MAD_DECOY,
        .name = "Imperium MAD Decoy",
        .sprite_name = "uimadst0.spr",
        .shadow_name = "uimadst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = DR_SPEED(16),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 100,
    },
    {   /* Shredder decoy with melee attack */
        .id = MT_IMP_SHREDDER_DECOY,
        .name = "Imperium Shredder Decoy",
        .sprite_name = "uishrst0.spr",
        .shadow_name = "uishrst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = DR_SPEED(20),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 100,
        .attack = { .range = 2.0f, .damage = 50, .cooldown_ms = 500 },
    },
    {
        .id = MT_IMP_SPA_DECOY,
        .name = "Imperium S.C.A.R.A.B. Decoy",
        .sprite_name = "uiiarst0.spr",
        .shadow_name = "uiiarst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = DR_SPEED(6),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 100,
    },
    /* === Civilians and neutral units === */
    {   /* Unarmed pedestrian */
        .id = MT_CIV_MALE,
        .name = "Civilian",
        .sprite_name = "uocvmst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HUMAN,
        .speed = DR_SPEED(10),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 30,
    },
    {   /* CivilianPistol: range 2, 667ms cd, 2 dmg */
        .id = MT_CIV_ROWDY,
        .name = "Rowdy Civilian",
        .sprite_name = "uorcmst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_HUMAN,
        .speed = DR_SPEED(8),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 66,
        .attack = { .range = 2.0f, .damage = 2, .cooldown_ms = 667 },
    },
    {   /* Spy/agent — no weapon */
        .id = MT_CIV_SPY,
        .name = "Civilian Spy",
        .sprite_name = "uocspst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HUMAN,
        .speed = DR_SPEED(12),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 66,
    },
    {   /* Unarmed pedestrian */
        .id = MT_CIV_PRISONER,
        .name = "Prisoner",
        .sprite_name = "uocvmst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HUMAN,
        .speed = DR_SPEED(10),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 30,
    },
    {   /* Radec weapon: range 7, 99ms cd, 170 dmg */
        .id = MT_CIV_JEBRAD,
        .name = "Jeb Radec",
        .sprite_name = "uorcmst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK | MF_HUMAN,
        .speed = DR_SPEED(12),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 500,
        .attack = { .range = 7.0f, .damage = 170, .cooldown_ms = 99 },
    },
    {   /* MedicHeal support — no offensive attack */
        .id = MT_CIV_KAROCH,
        .name = "Karoch",
        .sprite_name = "uocspst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HEAL | MF_HUMAN,
        .speed = DR_SPEED(8),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 250,
        .attack = { .range = 1, .damage = -20, .cooldown_ms = 334 },
    },
    {   /* Unarmed pedestrian */
        .id = MT_CIV_COLONEL,
        .name = "Colonel Martel",
        .sprite_name = "uocvmst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HUMAN,
        .speed = DR_SPEED(10),
        .move_class = DR_MOVE_FOOT,
        .max_hp = 100,
    },
    {   /* Transport — no weapon */
        .id = MT_CIV_WHEEL,
        .name = "Civilian Convoy",
        .sprite_name = "uowtrst0.spr",
        .shadow_name = "uowtrst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = DR_SPEED(6),
        .move_class = DR_MOVE_WHEEL,
        .max_hp = 150,
    },
    {   /* Hover transport — no weapon */
        .id = MT_CIV_HOVER,
        .name = "Desiccator Transport",
        .sprite_name = "uohtrst0.spr",
        .shadow_name = "uohtrst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = DR_SPEED(10),
        .move_class = DR_MOVE_HOVER,
        .max_hp = 100,
    },
    /* === Buildings — passive === */
#define BUILDING(id_, name_, sprite_, shadow_, hp_, w_, h_, mask_) \
    { .id = (id_), .name = (name_), .sprite_name = (sprite_), .shadow_name = (shadow_), \
      .traits = MF_SELECTABLE | MF_RENDERABLE, .max_hp = (hp_), \
      .footprint = {w_, h_}, .foundation = mask_, .corner_anchor = true }
    {
        .id = MT_FG_HQ1,
        .name = "FG Headquarters 1",
        .sprite_name = "nfhqt1l0.spr",
        .footprint = {5, 6}, .foundation = "          xxxxxxxxxx=xx=x =x==", .corner_anchor = true,
        .shadow_name = "bfhqtsh0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 1200,
        .sight = {8,8,false},
    },
    BUILDING(MT_FG_HQ2, "FG Headquarters 2", "nfhqt2l0.spr", "bfhqtsh0.spr", 2400, 5, 6, "          xxxxxxxxxx=xx=x =x=="),
    BUILDING(MT_FG_HQ3, "FG Headquarters 3", "nfhqt3l0.spr", "bfhqtsh0.spr", 3600, 5, 6, "          xxxxxxxxxx=xx=x =x=="),
    BUILDING(MT_FG_BARRACKS, "Barracks", "nfutf1l0.spr", "bfutfmn0.spr", 750, 5, 5, " === xxxx=xx=xxxxx==xxx=="),
    BUILDING(MT_FG_ADV_BARRACKS, "Advanced Barracks", "nfutf2l0.spr", "bfutfmn1.spr", 1500, 5, 5, " === xxxx=xx=xxxxx==xxx=="),
    BUILDING(MT_FG_VEHICLE_FACTORY, "Vehicle Factory", "nfvcy1l0.spr", "bfvcymn0.spr", 1000, 6, 5, " =xx= xxxxx=xx=xxxx=xxx==xxx= "),
    BUILDING(MT_FG_ADV_VEHICLE_FACTORY, "Advanced Vehicle Factory", "nfvcy2l0.spr", "bfvcymn1.spr", 2000, 6, 5, " =xx= xxxxx=xx=xxxx=xxx==xxx= "),
    BUILDING(MT_FG_HOVER_FACTORY, "Hovercraft Factory", "nfhsp1l0.spr", "bfhspmn0.spr", 600, 5, 4, "  xxx=xxxx=xx====== "),
    BUILDING(MT_FG_REPAIR_BAY, "Repair Bay", "nfrep1l0.spr", "bfrepmn0.spr", 600, 5, 4, "======xx===x=x= =xx "),
    BUILDING(MT_FG_PHASE_FACTORY_1, "Phase Factory 1", "nfphf1l0.spr", "bfphfmn0.spr", 1000, 5, 4, " === =xxx==x=x= xx=="),
    BUILDING(MT_FG_PHASE_FACTORY_2, "Phase Factory 2", "nfphf2l0.spr", "bfphfmn1.spr", 2000, 5, 4, " === =xxx==x=x= xx=="),
    BUILDING(MT_FG_CAMERA_TOWER, "Camera Tower", "nccam1l0.spr", "bccammn0.spr", 150, 1, 2, "xx"),
    { .id = MT_FG_LIFE_PLANT, .name = "Water Launch Pad", .sprite_name = "nclnc1l0.spr",
      .footprint = {5, 4}, .foundation = "      =xx=xxx===xx= ", .corner_anchor = true,
      .shadow_name = "bclncsh0.spr",
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE,
      .max_hp = 1300, .sight = {8,8,false} },
    { .id = MT_FG_POWER_PLANT, .name = "Taelon Power Generator", .sprite_name = "ncpow1l0.spr",
      .footprint = {4, 5}, .foundation = "     == =xxx==x=====", .corner_anchor = true,
      .shadow_name = "bcpowsh0.spr",
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE,
      .max_hp = 1450, .sight = {8,8,false} },
    BUILDING(MT_FG_REFINERY, "Refinery", "nfrrm1l0.spr", "bfrrmmn0.spr", 800, 3, 4, "   xxxxxx x "),
    BUILDING(MT_FG_BRIDGE_H, "Small Horizontal Bridge", "ncsbh1l0.spr", "bcsbhmn0.spr", 400, 3, 5, "   xxx======xxx"),
    BUILDING(MT_FG_BRIDGE_V, "Small Vertical Bridge", "ncsbv1l0.spr", "bcsbvmn0.spr", 400, 4, 3, "x==xx==xx==x"),
    BUILDING(MT_FG_BRIDGE_C, "Small Centre Bridge", "ncsbc1l0.spr", "bcsbcmn0.spr", 400, 4, 4, "x==x========x==x"),
    /* === Buildings — combat (MF_ATTACK) === */
    {   /* GatLaser: range 5, 100ms cd, 10 dmg */
        .id = MT_FG_GUARD_TOWER,
        .name = "Guard Tower",
        .sprite_name = "nfgdt1l0.spr",
        .footprint = {2, 2}, .foundation = "xxxx", .corner_anchor = true,
        .shadow_name = "bfgdtmn0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
        .max_hp = 400,
        .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 100 },
    },
    {   /* FixedLaserPlat: range 8, 333ms cd, 13 dmg */
        .id = MT_FG_ADV_GUARD_TOWER,
        .name = "Advanced Guard Tower",
        .sprite_name = "nfagt1l0.spr",
        .footprint = {3, 3}, .foundation = "xxxxxxxxx", .corner_anchor = true,
        .shadow_name = "bfagtmn0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
        .max_hp = 550,
        .attack = { .range = 8.0f, .damage = 13, .cooldown_ms = 333 },
    },
    {   /* FixedGroundToAirLaser: range 10, 467ms cd, 40 dmg — anti-air */
        .id = MT_FG_AA_SITE,
        .name = "Anti-Air Site",
        .sprite_name = "nfaar1l0.spr",
        .footprint = {2, 2}, .foundation = "xxxx", .corner_anchor = true,
        .shadow_name = "bfaarmn0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
        .max_hp = 600,
        .attack = { .range = 10.0f, .damage = 40, .cooldown_ms = 467 },
    },
    /* === FG decoy buildings === */
    BUILDING(MT_FG_HQ1_DECOY, "FG Headquarters 1 Decoy", "nfhqt1l0.spr", "bfhqtsh0.spr", 200, 5, 6, "          xxxxxxxxxx=xx=x =x=="),
    BUILDING(MT_FG_HQ2_DECOY, "FG Headquarters 2 Decoy", "nfhqt2l0.spr", "bfhqtsh1.spr", 200, 5, 6, "          xxxxxxxxxx=xx=x =x=="),
    BUILDING(MT_FG_HQ3_DECOY, "FG Headquarters 3 Decoy", "nfhqt3l0.spr", "bfhqtsh2.spr", 200, 5, 6, "          xxxxxxxxxx=xx=x =x=="),
    BUILDING(MT_FG_VEHICLE_FACTORY_1_DECOY, "FG Assembly Plant Decoy", "nfvcy1l0.spr", "bfvcysh0.spr", 200, 6, 5, " =xx= xxxxx=xx=xxxx=xxx==xxx= "),
    BUILDING(MT_FG_VEHICLE_FACTORY_2_DECOY, "FG Advanced Assembly Plant Decoy", "nfvcy2l0.spr", "bfvcysh1.spr", 200, 6, 5, " =xx= xxxxx=xx=xxxx=xxx==xxx= "),
    BUILDING(MT_FG_PHASE_FACTORY_1_DECOY, "FG Phasing Facility Decoy", "nfphf1l0.spr", "bfphfsh0.spr", 200, 5, 4, " === =xxx==x=x= xx=="),
    BUILDING(MT_FG_PHASE_FACTORY_2_DECOY, "FG Advanced Phasing Facility Decoy", "nfphf2l0.spr", "bfphfsh1.spr", 200, 5, 4, " === =xxx==x=x= xx=="),
    BUILDING(MT_FG_BARRACKS_DECOY, "FG Barracks Decoy", "nfutf1l0.spr", "bfutfsh0.spr", 200, 5, 5, " === xxxx=xx=xxxxx==xxx=="),
    BUILDING(MT_FG_ADV_BARRACKS_DECOY, "FG Advanced Barracks Decoy", "nfutf2l0.spr", "bfutfsh1.spr", 200, 5, 5, " === xxxx=xx=xxxxx==xxx=="),
    BUILDING(MT_FG_HOVER_FACTORY_DECOY, "FG Hover Factory Decoy", "nfhsp1l0.spr", "bfhspsh0.spr", 200, 5, 4, "  xxx=xxxx=xx====== "),
    BUILDING(MT_FG_REPAIR_BAY_DECOY, "FG Repair Bay Decoy", "nfrep1l0.spr", "bfrepsh0.spr", 200, 5, 4, "======xx===x=x= =xx "),
    BUILDING(MT_FG_REFINERY_DECOY, "FG Refinery Decoy", "nfrrm1l0.spr", "bfrrmsh0.spr", 200, 3, 4, "   xxxxxx x "),
    BUILDING(MT_FG_POWER_PLANT_DECOY, "FG Power Plant Decoy", "ncpow1l0.spr", "bcpowsh0.spr", 200, 4, 5, "     == =xxx==x====="),
    /* === Imperium buildings — passive (BUILD.TXT hitpoints) === */
    {
        .id = MT_IMP_HQ1,
        .name = "Imperium Headquarters 1",
        .sprite_name = "nihqt1l0.spr",
        .footprint = {5, 6}, .foundation = "          =x=x==xxxxxx=xxx=xxx", .corner_anchor = true,
        .shadow_name = "bihqtsh0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 1440,
        .sight = {8,8,false},
    },
    BUILDING(MT_IMP_HQ2, "Imperium Headquarters 2", "nihqt2l0.spr", "bihqtsh1.spr", 2880, 5, 6, "          =x=x==xxxxxx=xxx=xxx"),
    BUILDING(MT_IMP_HQ3, "Imperium Headquarters 3", "nihqt3l0.spr", "bihqtsh2.spr", 4330, 5, 6, "          =x=x==xxxxxx=xxx=xxx"),
    BUILDING(MT_IMP_BARRACKS, "Imperium Barracks", "niutf1l0.spr", "biutfsh0.spr", 900, 5, 5, " =x= =xxxxxx=xxxxx====xx="),
    BUILDING(MT_IMP_ADV_BARRACKS, "Imperium Advanced Barracks", "niutf2l0.spr", "biutfsh1.spr", 1800, 5, 5, " =x= =xxxxxx=xxxxx====xx="),
    BUILDING(MT_IMP_VEHICLE_FACTORY, "Imperium Assembly Plant", "nivcy1l0.spr", "bivcysh0.spr", 1200, 5, 5, "======xxxxxx=xxx=xxx=xx=="),
    BUILDING(MT_IMP_ADV_VEHICLE_FACTORY, "Imperium Advanced Assembly Plant", "nivcy2l0.spr", "bivcysh1.spr", 2400, 5, 5, "======xxxxxx=xxx=xxx=xx=="),
    BUILDING(MT_IMP_HOVER_FACTORY, "Imperium Hover Factory", "nihsp1l0.spr", "bihspsh0.spr", 720, 4, 3, "xxx x=xx=xxx"),
    BUILDING(MT_IMP_REPAIR_BAY, "Imperium Repair Bay", "nirep1l0.spr", "birepsh0.spr", 720, 4, 4, "xxx=xx=xx=xx=xx="),
    BUILDING(MT_IMP_TACHYON_PLANT, "Tachyon Plant", "nitgt1l0.spr", "bitgtsh0.spr", 1000, 3, 2, "xxxx=x"),
    BUILDING(MT_IMP_REFINERY, "Imperium Refinery", "nirrm1l0.spr", "birrmsh0.spr", 960, 3, 3, "===xxxxxx"),
    BUILDING(MT_IMP_RIFT_CREATOR, "Rift Creator", "nitrc1l0.spr", "bitrcsh0.spr", 1000, 4, 4, "=== =xx xxxxxxxx"),
    BUILDING(MT_IMP_CAMERA_TOWER, "Imperium Camera Tower", "nccam1l0.spr", "bccammn0.spr", 150, 1, 2, "xx"),
    { .id = MT_IMP_LIFE_PLANT, .name = "Imperium Water Launch Pad", .sprite_name = "nclnc1l0.spr",
      .footprint = {5, 4}, .foundation = "      =xx=xxx===xx= ", .corner_anchor = true,
      .shadow_name = "bclncsh0.spr",
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE,
      .max_hp = 1300, .sight = {8,8,false} },
    { .id = MT_IMP_POWER_PLANT, .name = "Imperium Power Generator", .sprite_name = "ncpow1l0.spr",
      .footprint = {4, 5}, .foundation = "     == =xxx==x=====", .corner_anchor = true,
      .shadow_name = "bcpowsh0.spr",
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE,
      .max_hp = 1450, .sight = {8,8,false} },
    /* === Imperium buildings — combat (MF_ATTACK) === */
    {   /* GatPlasma: range 5, 100ms cd, 10 dmg */
        .id = MT_IMP_GUARD_TOWER,
        .name = "Imperium Guard Tower",
        .sprite_name = "nigdt1l0.spr",
        .footprint = {2, 2}, .foundation = "xxxx", .corner_anchor = true,
        .shadow_name = "bigdtsh0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
        .max_hp = 400,
        .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 100 },
    },
    {   /* NeutronAss: range 8, 1067ms cd, 180 dmg */
        .id = MT_IMP_ADV_GUARD_TOWER,
        .name = "Imperium Advanced Guard Tower",
        .sprite_name = "niagt1l0.spr",
        .footprint = {3, 3}, .foundation = "xxxxxxxxx", .corner_anchor = true,
        .shadow_name = "biagtsh0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
        .max_hp = 550,
        .attack = { .range = 8.0f, .damage = 180, .cooldown_ms = 1067 },
    },
    {   /* IMPFixedGroundToAirLaser: range 10, 467ms cd, 14 dmg — anti-air */
        .id = MT_IMP_AA_SITE,
        .name = "Imperium Anti-Air Site",
        .sprite_name = "niaar1l0.spr",
        .footprint = {3, 3}, .foundation = "xx=xxx=xx", .corner_anchor = true,
        .shadow_name = "biaarsh0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
        .max_hp = 720,
        .attack = { .range = 10.0f, .damage = 14, .cooldown_ms = 467 },
    },
    /* === Imperium bridges === */
    BUILDING(MT_IMP_BRIDGE_H, "Imperium Small Horizontal Bridge", "ncsbh1l0.spr", "bcsbhsh0.spr", 400, 3, 5, "   xxx======xxx"),
    BUILDING(MT_IMP_BRIDGE_V, "Imperium Small Vertical Bridge", "ncsbv1l0.spr", "bcsbvsh0.spr", 400, 4, 3, "x==xx==xx==x"),
    BUILDING(MT_IMP_BRIDGE_C, "Imperium Small Centre Bridge", "ncsbc1l0.spr", "bcsbcsh0.spr", 400, 4, 4, "x==x========x==x"),
    /* === Imperium walls === */
    BUILDING(MT_IMP_WALL_1, "Small Wall 1", "ncswl1l0.spr", "ncswlsh0.spr", 100, 2, 3, "  xxxx"),
    BUILDING(MT_IMP_WALL_2, "Small Wall 2", "ncswm1l0.spr", "ncswmsh0.spr", 100, 2, 3, "  xxxx"),
    BUILDING(MT_IMP_LARGE_WALL_1, "Large Wall 1", "ncbwl1l0.spr", "ncbwlsh0.spr", 400, 5, 4, "      xx   xxx   xx "),
    BUILDING(MT_IMP_LARGE_WALL_2, "Large Wall 2", "ncbwm1l0.spr", "ncbwmsh0.spr", 400, 5, 4, "       xx  xxx  xx  "),
    /* === Civilian and general buildings === */
    BUILDING(MT_CIV_ENTERTAINMENT, "Civilian Entertainment", "nocen1l0.spr", "bocensh0.spr", 1200, 5, 4, " =xx xxxxxxxxxxxxxxx"),
    BUILDING(MT_WATER_EXTRACTOR, "Water Extractor", "ncwel1l0.spr", "ncwel1l0.spr", 500, 3, 3, "========="),
    BUILDING(MT_TAELON_EXTRACTOR, "Taelon Extractor", "ncmin1l0.spr", "ncmin1l0.spr", 600, 3, 3, "========="),
    BUILDING(MT_IMP_WATER_RESEARCH, "Imperium Water Research", "nowat1l0.spr", "towatsh0.spr", 1200, 5, 4, " xxx xxxxxxxxxxx=xx "),
    BUILDING(MT_IMP_HOVER_RESEARCH, "Imperium Hover Research", "nohov1l0.spr", "tohovsh0.spr", 1200, 6, 5, "       xxxxxxxxxxxxxxxx  xxx  "),
    BUILDING(MT_IMP_DESICATOR_RESEARCH, "Imperium Desicator Research", "nodes1l0.spr", "todessh0.spr", 1200, 6, 5, "       xxxxxxxxxxxxxxxx  xxx  "),
    BUILDING(MT_IMP_GENETIC_RESEARCH, "Imperium Genetic Research", "nomdr1l0.spr", "tomdrsh0.spr", 1200, 4, 4, " xxxxxxxxxxxxxxx"),
    BUILDING(MT_CIV_SHELTER, "Civilian Public Shelter", "noshl1l0.spr", "toshlsh0.spr", 600, 2, 2, "xxxx"),
    BUILDING(MT_CIV_SUB_TRANSIT, "Civilian SubTransit", "nosub1l0.spr", "tosubsh0.spr", 600, 3, 2, "xx xxx"),
    BUILDING(MT_CIV_TRANSIT_CENTRE, "Civilian Transit Centre", "notcn1l0.spr", "totcnsh0.spr", 1200, 4, 4, "  x xxxxxxxx  x "),
    BUILDING(MT_FG_TREATY_HALL, "FG Treaty Hall", "notyh1l0.spr", "totyhsh0.spr", 1200, 6, 6, "        x    xxxx  xxxx  xxxx  xxx  "),
    BUILDING(MT_TOGRAN_LANDING_VESSEL, "Togran Landing Vessel", "nothq1l0.spr", "tothqsh0.spr", 1200, 6, 6, "        xx   xxx  xxxxxxxxxxxx xxxx "),
    BUILDING(MT_TOGRAN_MONOLITH, "Togran Monolith", "nomlt1l0.spr", "tomltsh0.spr", 92000, 3, 3, "    x  x "),
    BUILDING(MT_TOGRAN_LABORATORY, "Togran Laboratory", "notdr1l0.spr", "notdr1l0.spr", 90000, 3, 3, "====x===="),
    BUILDING(MT_RENDEZVOUS_POINT, "Rendezvous Point", "norvp1l0.spr", "torvpsh0.spr", 1000, 3, 3, "xxxxxxxxx"),
    BUILDING(MT_FG_PLANETARY_DEFENSE, "FG Orbital Defense Matrix", "nopld1l0.spr", "topldsh0.spr", 2500, 5, 7, "                xx  xxxxxxxxxx xxx "),
    BUILDING(MT_CIV_COMMERCIAL, "Civilian Commercial", "nocbs1l0.spr", "bocbssh0.spr", 1200, 4, 4, "    xxxxxxxxxxxx"),
    BUILDING(MT_CIV_FACTORY, "Civilian Factory", "nowar1l0.spr", "bowarsh0.spr", 1200, 4, 3, "xxx=xxxxxxx="),
    BUILDING(MT_IMP_PRISON, "Imperium Prison", "nopri1l0.spr", "toprish0.spr", 2500, 6, 5, "   xx  xxxx xxxxxxxxxxxx xxx= "),
    BUILDING(MT_CIV_RURAL, "Civilian Rural", "nochm4l0.spr", "bochmsh0.spr", 1200, 5, 4, "xxxx xxxxxxxxxx xxxx"),
    BUILDING(MT_CIV_GRAIN_FARM, "Civilian Grain Farm", "nofrm1l0.spr", "nofrm1l0.spr", 600, 5, 4, " xxxxxxxxxxxxxx xxxx"),
    BUILDING(MT_CIV_HYDRO_FARM, "Civilian Hydro Farm", "nofrm1l1.spr", "nofrm1l1.spr", 600, 5, 3, " xxx xxxxx xxx "),
    BUILDING(MT_CIV_FARMHOUSE, "Civilian Farmhouse", "nofrm1l2.spr", "nofrm1l2.spr", 600, 5, 5, "     xxxxxxxxxxxxxxxxxxxx"),
    /* === Civilian bridges === */
    BUILDING(MT_CIVILIAN_BRIDGE, "Civilian Bridge", "nobrd1l0.spr", "tobrdsh0.spr", 4000, 6, 5, "      xxxxxx============xxxxxx"),
    BUILDING(MT_CIVILIAN_VERTICAL_BRIDGE, "Civilian Vertical Bridge", "nobrd1l1.spr", "tobrdsh1.spr", 4000, 4, 6, "x==xx==xx==xx==xx==xx==x"),
    /* === Togran bridges === */
    BUILDING(MT_TOGRAN_BRIDGE_H, "Togran Small Horizontal Bridge", "ncsbh1l0.spr", "bcsbhsh0.spr", 400, 3, 5, "   xxx======xxx"),
    BUILDING(MT_TOGRAN_BRIDGE_V, "Togran Small Vertical Bridge", "ncsbv1l0.spr", "bcsbvsh0.spr", 400, 4, 3, "x==xx==xx==x"),
    BUILDING(MT_TOGRAN_BRIDGE_C, "Togran Small Centre Bridge", "ncsbc1l0.spr", "bcsbcsh0.spr", 400, 4, 4, "x==x========x==x"),
};
#undef BUILDING


/* ── game identity (Doom-style externs) ─────────────────────────────────── */

const char *const g_game_id            = "dark-reign";
const char *const g_game_name          = "Dark Reign";
const char *const g_game_default_root  = "data/REIGN/dark";
const char *const g_game_default_map   = "scenario/FIXED/M01F/M01F.SCN";
const char *const g_game_default_sprite = "ucfcnst0.spr";
const int g_cell_w = 24;
const int g_cell_h = 24;
const uint16_t g_debug_enemy_type = MT_FG_CONSTRUCTION_CREW;
const gameinfo_t *gameinfo = &game_info;
const mobjtype_t *const actor_types =
    (const mobjtype_t *)DARK_REIGN_ACTOR_TYPES;
const int num_actor_types =
    (int)(sizeof(DARK_REIGN_ACTOR_TYPES) / sizeof(DARK_REIGN_ACTOR_TYPES[0]));

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

static char requested_map[128];
static dr_skirmish_t requested, current;
static bool current_valid;

void DR_RequestSkirmish(const char *map, const dr_skirmish_t *setup) {
    snprintf(requested_map, sizeof(requested_map), "%s", M_FileName(map));
    requested = *setup;
}

/* Taken by the level that matches the requested map; any other level, such
 * as a campaign mission, plays its scenario as authored. */
bool DR_TakeSkirmish(const char *map, dr_skirmish_t *setup) {
    current_valid = requested_map[0] && !strcasecmp(M_FileName(map), requested_map);
    if (current_valid) current = requested;
    if (current_valid && setup) *setup = current;
    requested_map[0] = '\0';
    return current_valid;
}

const dr_skirmish_t *DR_LevelSkirmish(void) {
    return current_valid ? &current : NULL;
}

/* ── FOGTILE.FOG — native fog of war tiles ─────────────────────────────── */

#define DR_FOG_TILES 81
#define DR_FOG_W     24
#define DR_FOG_H     24

static uint8_t dr_fogtiles[DR_FOG_TILES][DR_FOG_H][DR_FOG_W];
static bool dr_fogtiles_loaded;

static bool load_fogtiles(const char *root) {
    if (dr_fogtiles_loaded) return true;
    char path[256];
    M_PathJoin(path, sizeof(path), root, "graphics/FOGTILE.FOG");
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint32_t hdr[3];
    if (fread(hdr, 4, 3, f) != 3 || hdr[0] != 0x54474f46u) { fclose(f); return false; }
    uint32_t sizes[DR_FOG_TILES];
    if (fread(sizes, 4, DR_FOG_TILES, f) != DR_FOG_TILES) { fclose(f); return false; }
    for (int i = 0; i < DR_FOG_TILES; ++i) {
        uint8_t buf[512];
        if (sizes[i] > sizeof(buf)) { fclose(f); return false; }
        if (fread(buf, 1, sizes[i], f) != sizes[i]) { fclose(f); return false; }
        const uint8_t *p = buf;
        for (int y = 0; y < DR_FOG_H; ++y) {
            int x = 0;
            for (;;) {
                uint8_t b = *p++;
                int type = b >> 6;
                int len = b & 0x3f;
                if (type == 3) break;
                for (int j = 0; j < len && x < DR_FOG_W; ++j)
                    dr_fogtiles[i][y][x++] = (uint8_t)type;
            }
            while (x < DR_FOG_W) dr_fogtiles[i][y][x++] = 0;
        }
    }
    fclose(f);
    dr_fogtiles_loaded = true;
    return true;
}

enum { DR_FOGST_VISIBLE, DR_FOGST_EXPLORED, DR_FOGST_UNEXPLORED };

static int dr_cell_fog(const level_t *map, int x, int y) {
    if (!map->sight.cells) return DR_FOGST_VISIBLE;
    if (!L_Contains(map, x, y)) return DR_FOGST_UNEXPLORED;
    uint32_t bits = map->sight.cells[L_Index(map, x, y)];
    if (!(bits & SIGHT_EXPLORED)) return DR_FOGST_UNEXPLORED;
    return (bits & map->sight.allies[consoleplayer]) ? DR_FOGST_VISIBLE : DR_FOGST_EXPLORED;
}

static void dr_draw_fog(app_t *app, const level_t *map, const tileset_t *tileset) {
    (void)tileset;
    if (!map->sight.cells || !screens[0].pixels) return;
    if (!dr_fogtiles_loaded) load_fogtiles(g_game_default_root);
    if (!dr_fogtiles_loaded) return;
    int cell_w = app->cell.w > 0 ? app->cell.w : 24;
    int cell_h = app->cell.h > 0 ? app->cell.h : 24;
    irect_t view = G_WorldViewport(app);
    int origin = view.x < 0 ? 0 : view.x;
    if (origin > screens[0].w) origin = screens[0].w;
    int width = view.w;
    if (width > screens[0].w - origin) width = screens[0].w - origin;
    if (width < 0) width = 0;
    int scr_h = app->win.h < screens[0].h ? app->win.h : screens[0].h;
    uint8_t black = V_NearestIndex(0xff000000u);

    for (int gy = 0; gy < map->height; ++gy) {
        for (int gx = 0; gx < map->width; ++gx) {
            float sx, sy;
            R_GridToScreen(app, (float)gx, (float)gy, &sx, &sy);
            int dx = (int)sx, dy = (int)sy;
            if (dx + cell_w <= origin || dx >= origin + width ||
                dy + cell_h <= 0 || dy >= scr_h) continue;

            int c  = dr_cell_fog(map, gx, gy);
            int n  = gy > 0 ? dr_cell_fog(map, gx, gy - 1) : DR_FOGST_UNEXPLORED;
            int s  = gy < map->height - 1 ? dr_cell_fog(map, gx, gy + 1) : DR_FOGST_UNEXPLORED;
            int w  = gx > 0 ? dr_cell_fog(map, gx - 1, gy) : DR_FOGST_UNEXPLORED;
            int e  = gx < map->width - 1 ? dr_cell_fog(map, gx + 1, gy) : DR_FOGST_UNEXPLORED;
            int nw = (gx > 0 && gy > 0) ? dr_cell_fog(map, gx - 1, gy - 1) : DR_FOGST_UNEXPLORED;
            int ne = (gx < map->width - 1 && gy > 0) ? dr_cell_fog(map, gx + 1, gy - 1) : DR_FOGST_UNEXPLORED;
            int sw_c = (gx > 0 && gy < map->height - 1) ? dr_cell_fog(map, gx - 1, gy + 1) : DR_FOGST_UNEXPLORED;
            int se_c = (gx < map->width - 1 && gy < map->height - 1) ? dr_cell_fog(map, gx + 1, gy + 1) : DR_FOGST_UNEXPLORED;

            /* Quadrant states: max of this cell and its two edge + one corner neighbor. */
            int q_nw = c; if (n > q_nw) q_nw = n; if (w > q_nw) q_nw = w; if (nw > q_nw) q_nw = nw;
            int q_ne = c; if (n > q_ne) q_ne = n; if (e > q_ne) q_ne = e; if (ne > q_ne) q_ne = ne;
            int q_sw = c; if (s > q_sw) q_sw = s; if (w > q_sw) q_sw = w; if (sw_c > q_sw) q_sw = sw_c;
            int q_se = c; if (s > q_se) q_se = s; if (e > q_se) q_se = e; if (se_c > q_se) q_se = se_c;

            int tile_idx = q_sw + q_se * 3 + q_nw * 9 + q_ne * 27;
            if (tile_idx == 0) continue;

            const uint8_t (*tile)[DR_FOG_W] = dr_fogtiles[tile_idx];
            for (int py = 0; py < cell_h; ++py) {
                int screen_y = dy + py;
                if (screen_y < 0 || screen_y >= scr_h) continue;
                uint8_t *row = screens[0].pixels + (size_t)screen_y * screens[0].w;
                int ty = py * DR_FOG_H / cell_h;
                for (int px = 0; px < cell_w; ++px) {
                    int screen_x = dx + px;
                    if (screen_x < origin || screen_x >= origin + width) continue;
                    int tx = px * DR_FOG_W / cell_w;
                    uint8_t t = tile[ty][tx];
                    if (t == 2)
                        row[screen_x] = black;
                    else if (t == 1 && ((screen_x ^ screen_y) & 1))
                        row[screen_x] = black;
                }
            }
        }
    }
}

static gameinfo_t dr_runtime_info;
static state_t dr_runtime_states[NUMSTATES];

void G_InitGame(void) {
    static bool initialized;
    if (initialized) return;
    memcpy(dr_runtime_states, states, sizeof(dr_runtime_states));
    dr_runtime_info = game_info;
    dr_runtime_info.states = dr_runtime_states;
    dr_runtime_info.draw_fog = dr_draw_fog;
    gameinfo = &dr_runtime_info;
    initialized = true;
}

bool G_DoLoadLevel(const char *path, level_t *out) {
    return load_dark_map(path, out);
}

bool W_LoadAssets(const char *root, const level_t *map, const char *sprite,
                  tileset_t *tileset, spritesheet_t *unit_sprite) {
    return plugin_load_assets(root, map, sprite, tileset, unit_sprite);
}

int P_LoadThings(const char *path) {
    return load_dark_reign_initial_units(path);
}

bool R_InitSprites(const char *root, const level_t *map, mobj_t *const *mobjs,
                   int count, spritecache_t *cache) {
    return load_dark_reign_decoration_sprites(root, map, mobjs, count, cache);
}

bool HU_LoadFont(const char *root, bitmapfont_t *font) {
    (void)root; (void)font;
    return false;
}

void  G_MissionTicker(level_t *map, mobj_t *const *mobjs, int *count,
                      hudtext_t *hud, float dt) {
    (void)map; (void)mobjs; (void)count;
    (void)hud; (void)dt;
}

bool G_UpdateProduction(level_t *map, mobj_t *const *units, int *unit_count, float dt) {
    (void)map; (void)units; (void)unit_count; (void)dt;
    return false;
}

/* Under the top bar, left of the MFD column. */
irect_t G_WorldViewport(const app_t *app) {
    return (irect_t){0, 32 * app->win.h / 480, 448 * app->win.w / 640, 448 * app->win.h / 480};
}
