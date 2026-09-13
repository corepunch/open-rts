#include "game.h"
#include "dr_types.h"
#include "info.h"

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
bool plugin_load_assets(SDL_Renderer *renderer, const char *data_root,
                                   const level_t *map, const char *sprite_name,
                                   tileset_t *tileset, spritesheet_t *unit_sprite);
int load_dark_reign_initial_units(const char *map_path);
bool load_dark_reign_decoration_sprites(SDL_Renderer *renderer, const char *data_root,
                                        const level_t *map, mobj_t *const *units, int unit_count,
                                        spritecache_t *cache);

static const uiimage_t DARK_REIGN_UI_IMAGES[] = {
    { "graphics/INTFACE/IGI/TOPBTNS.BMP", {   0, 0, 126, 32 }, {   0,   0, 126,  32 } }, /* 0 */
    { "graphics/INTFACE/IGI/TOPBITS.BMP", {   0, 0, 154, 32 }, { 126,   0, 154,  32 } }, /* 1 */
    { "graphics/INTFACE/IGI/TOPBTNS.BMP", { 126, 0, 168, 32 }, { 280,   0, 168,  32 } }, /* 2 */
    { "graphics/INTFACE/IGI/MFDBTNS.BMP", {   0, 0, 192, 64 }, { 448,   0, 192,  64 } }, /* 3 */
    { "graphics/INTFACE/IGI/MFDBAC1.BMP", {   0, 0, 192,278 }, { 448,  64, 192, 278 } }, /* 4 */
    { "graphics/INTFACE/IGI/BUBLDBIT.BMP",{   0, 0, 192, 28 }, { 448, 314, 192,  28 } }, /* 5 */
    { "graphics/INTFACE/IGI/MINIMAP.BMP", {   0, 0, 140,138 }, { 448, 342, 140, 138 } }, /* 6 */
    { "graphics/INTFACE/IGI/RESOBARS.BMP",{   0, 0,  52,104 }, { 588, 376,  52, 104 } }, /* 7 */
};

/* Categories: BUILD=all buildings, COMMS=infantry, MENU=vehicles.
   Each maps to one 64x32 cell in row 1 of MFDBTNS.BMP (image 3, 192x64). */
static const uicategory_t DARK_REIGN_UI_CATEGORIES[] = {
    { "BUILD",    { 448, 0, 64, 32 }, 3, {   0, 0, 64, 32 } },
    { "COMMS",    { 512, 0, 64, 32 }, 3, {  64, 0, 64, 32 } },
    { "Vehicles", { 576, 0, 64, 32 }, 3, { 128, 0, 64, 32 } },
};

/* ORDERS=Stop, PATHS=Move, SPECIAL=Attack — row 2 of MFDBTNS.BMP. */
static const uiaction_t DARK_REIGN_UI_ACTIONS[] = {
    { "Stop",   UI_STOP,   { 448, 32, 64, 32 }, 3, {   0, 32, 64, 32 }, 0 },
    { "Move",   UI_MOVE,   { 512, 32, 64, 32 }, 3, {  64, 32, 64, 32 }, 0 },
    { "Attack", UI_ATTACK, { 576, 32, 64, 32 }, 3, { 128, 32, 64, 32 }, 0 },
};

/* Products: categories 0-2 match DARK_REIGN_UI_CATEGORIES above.
   Sprites are menu icons from the FTG archive (loaded via G_LoadMenuSprite).
   Upgraded buildings (HQ2/3, Adv Barracks, etc.) stay in BUILD (cat 0) so
   the second row is free for action buttons and no category overlaps them. */
static const uiproduct_t DARK_REIGN_UI_PRODUCTS[] = {
    /* BUILD: all buildings and Construction Rig */
    { 10001, 0, "bfhqtmn0.spr" }, { 10002, 0, "bfhqtmn1.spr" }, { 10003, 0, "bfhqtmn2.spr" },
    { 10004, 0, "bfutfmn0.spr" }, { 10005, 0, "bfutfmn1.spr" },
    { 10006, 0, "bfvcymn0.spr" }, { 10007, 0, "bfvcymn1.spr" },
    { 10008, 0, "bfhspmn0.spr" }, { 10009, 0, "bfrepmn0.spr" },
    { 10010, 0, "bccammn0.spr" }, { 10011, 0, "bfrrmmn0.spr" },
    { 10012, 0, "bfaarmn0.spr" }, { 10013, 0, "bfgdtmn0.spr" }, { 10014, 0, "bfagtmn0.spr" },
    { 10015, 0, "bfphfmn0.spr" }, { 10016, 0, "bfphfmn1.spr" },
    { 10019, 0, "bclncmn0.spr" }, { 10020, 0, "bcpowmn0.spr" },
    { 10040, 0, "bcsbhmn0.spr" }, { 10041, 0, "bcsbvmn0.spr" }, { 10042, 0, "bcsbcmn0.spr" },
    {    11, 0, "ucfcnmn0.spr" },
    /* COMMS: infantry */
    {  9, 1, "ufradmn0.spr" }, { 10, 1, "ufmrcmn0.spr" }, {  8, 1, "ufsnpmn0.spr" },
    {  6, 1, "ufsctmn0.spr" }, {  7, 1, "ufmedmn0.spr" }, {  3, 1, "ufsabmn0.spr" },
    {  2, 1, "ufmecmn0.spr" }, {  5, 1, "ufmtrmn0.spr" }, {  4, 1, "ucinfmn0.spr" },
    /* Vehicles */
    {  1, 2, "ufspbmn0.spr" }, { 15, 2, "ufratmn0.spr" }, { 20, 2, "ufsktmn0.spr" },
    { 17, 2, "ufthnmn0.spr" }, { 21, 2, "ufphtmn0.spr" }, { 12, 2, "ufflkmn0.spr" },
    { 16, 2, "uftrtmn0.spr" }, { 19, 2, "uffarmn0.spr" }, { 23, 2, "ufskbmn0.spr" },
    { 24, 2, "ufoutmn0.spr" }, { 18, 2, "ufswvmn0.spr" }, { 30, 2, "ucwcomn0.spr" },
    { 13, 2, "ucfrgmn0.spr" }, { 14, 2, "uchfrmn0.spr" },
};

static const uidefinition_t DARK_REIGN_UI = {
    .logical_width = 640,
    .logical_height = 480,
    .world_viewport = { 0, 32, 448, 448 },
    .minimap = { 454, 348, 128, 126 },
    .command_grid = { 450, 66, 188, 246 },
    .command_columns = 3,
    .command_rows = 4,
    .icon_size = { 62, 61 },
    .resources = {
        [0] = { .text = { 216, 5 }, .color = { 55, 242, 238, 255 } },
    },
    .resource_count = 1,
    .images = DARK_REIGN_UI_IMAGES,
    .image_count = (int)(sizeof(DARK_REIGN_UI_IMAGES) / sizeof(DARK_REIGN_UI_IMAGES[0])),
    .products = DARK_REIGN_UI_PRODUCTS,
    .product_count = (int)(sizeof(DARK_REIGN_UI_PRODUCTS) / sizeof(DARK_REIGN_UI_PRODUCTS[0])),
    .categories = DARK_REIGN_UI_CATEGORIES,
    .category_count = (int)(sizeof(DARK_REIGN_UI_CATEGORIES) / sizeof(DARK_REIGN_UI_CATEGORIES[0])),
    .actions = DARK_REIGN_UI_ACTIONS,
    .action_count = (int)(sizeof(DARK_REIGN_UI_ACTIONS) / sizeof(DARK_REIGN_UI_ACTIONS[0])),
};

const uidefinition_t *const gameui = &DARK_REIGN_UI;

static const mobjtype_t DARK_REIGN_ACTOR_TYPES[] = {
    /* === Special / support units === */
    {
        .id = MT_FG_CONSTRUCTION_CREW,
        .name = "Construction Rig",
        .sprite_name = "ucfcnst0.spr",
        .shadow_name = "ucfcnsh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 5.5f,
        .max_hp = 100,
        .attack = { .range = 9.0f, .damage = 20, .cooldown_ms = 700 },
    },
    {
        .id = MT_FG_FREIGHTER,
        .name = "Freighter",
        .sprite_name = "ucfrgst0.spr",
        .shadow_name = "ucfrgst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER,
        .speed = 4.5f,
        .max_hp = 750,
        .harvest = { .capacity = 100 },
    },
    {   /* Laser-armed hover harvester */
        .id = MT_FG_HOVER_FREIGHTER,
        .name = "Hover Freighter",
        .sprite_name = "uchfrst0.spr",
        .shadow_name = "uchfrst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER | MF_ATTACK,
        .speed = 4.5f,
        .max_hp = 500,
        .attack = { .range = 4.0f, .damage = 11, .cooldown_ms = 267 },
        .harvest = { .capacity = 100 },
    },
    /* === Infantry === */
    {   /* LaserRifle: range 4, 267ms cd, 11 dmg */
        .id = MT_FG_RAIDER,
        .name = "Raider",
        .sprite_name = "ufradst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 5.0f,
        .max_hp = 100,
        .attack = { .range = 4.0f, .damage = 11, .cooldown_ms = 267 },
    },
    {   /* RailGun: range 5, 367ms cd, 11 dmg */
        .id = MT_FG_MERCENARY,
        .name = "Mercenary",
        .sprite_name = "ufmrcst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 4.8f,
        .max_hp = 125,
        .attack = { .range = 5.0f, .damage = 11, .cooldown_ms = 367 },
    },
    {   /* SniperRifle: range 8, 1667ms cd, 150 dmg */
        .id = MT_FG_SNIPER,
        .name = "Sniper",
        .sprite_name = "ufsnpst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 4.5f,
        .max_hp = 100,
        .attack = { .range = 8.0f, .damage = 150, .cooldown_ms = 1667 },
    },
    {   /* Recon only — no weapon */
        .id = MT_FG_SCOUT,
        .name = "Scout",
        .sprite_name = "ufsctst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 6.0f,
        .max_hp = 66,
    },
    {   /* MedicHeal — support, no offensive attack */
        .id = MT_FG_MEDIC,
        .name = "Field Medic",
        .sprite_name = "ufmedst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 4.8f,
        .max_hp = 66,
    },
    {   /* Sabotage ability — no ranged weapon */
        .id = MT_FG_SABOTEUR,
        .name = "Saboteur",
        .sprite_name = "ufsabst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 4.8f,
        .max_hp = 100,
    },
    {   /* MechanicRepair — support, no offensive attack */
        .id = MT_FG_MECHANIC,
        .name = "Mechanic",
        .sprite_name = "ufmecst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 4.8f,
        .max_hp = 66,
    },
    {   /* SuicideNuke: range 2, 1667ms, 180 dmg, large AoE */
        .id = MT_FG_MARTYR,
        .name = "Martyr",
        .sprite_name = "ufmtrst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 4.8f,
        .max_hp = 100,
        .attack = { .range = 2.0f, .damage = 180, .cooldown_ms = 1667 },
    },
    {   /* Infiltrate ability — no ranged weapon */
        .id = MT_FG_SPY,
        .name = "Infiltrator",
        .sprite_name = "ucinfst0.spr",
        .shadow_name = "ucmensh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 4.8f,
        .max_hp = 66,
    },
    /* === Vehicles === */
    {   /* DoubleRailGun: range 5, 433ms cd, 10 dmg */
        .id = MT_FG_SPYDER_BIKE,
        .name = "Spider Bike",
        .sprite_name = "ufspbst0.spr",
        .shadow_name = "ufspbsh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 6.5f,
        .max_hp = 133,
        .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 433 },
    },
    {   /* Rapid armored transport — no weapon */
        .id = MT_FG_IFV,
        .name = "RAT",
        .sprite_name = "ufratst0.spr",
        .shadow_name = "ufratst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 5.0f,
        .max_hp = 200,
    },
    {   /* SkirmishGun (dual): range 6, 667ms cd, 14 dmg */
        .id = MT_FG_MEDIUM_TANK,
        .name = "Skirmish Tank",
        .sprite_name = "ufsktst0.spr",
        .shadow_name = "ufsktst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 4.0f,
        .max_hp = 133,
        .attack = { .range = 6.0f, .damage = 14, .cooldown_ms = 667 },
    },
    {   /* TankHunterGun: range 3, 667ms cd, 60 dmg — high anti-armor */
        .id = MT_FG_TANK_HUNTER,
        .name = "Tank Hunter",
        .sprite_name = "ufthnst0.spr",
        .shadow_name = "ufthnst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 4.0f,
        .max_hp = 150,
        .attack = { .range = 3.0f, .damage = 60, .cooldown_ms = 667 },
    },
    {   /* PhaseTankCannon: range 6, 433ms cd, 30 dmg */
        .id = MT_FG_PHASE_TANK,
        .name = "Phase Tank",
        .sprite_name = "ufphtst0.spr",
        .shadow_name = "ufphtst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 4.0f,
        .max_hp = 166,
        .attack = { .range = 6.0f, .damage = 30, .cooldown_ms = 433 },
    },
    {   /* Chaff: range 8, 500ms cd, 8 dmg — anti-air */
        .id = MT_FG_MAD,
        .name = "Flak Jack",
        .sprite_name = "ufflkst0.spr",
        .shadow_name = "ufflksh0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 4.0f,
        .max_hp = 100,
        .attack = { .range = 8.0f, .damage = 8, .cooldown_ms = 500 },
    },
    {   /* TripleRailGun: range 8, 667ms cd, 24 dmg */
        .id = MT_FG_TRIPLE_RAIL_TANK,
        .name = "Triple Rail Tank",
        .sprite_name = "uftrtst0.spr",
        .shadow_name = "uftrtst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 3.5f,
        .max_hp = 200,
        .attack = { .range = 8.0f, .damage = 24, .cooldown_ms = 667 },
    },
    {   /* ArtilleryShell: range 45, 2667ms cd, 30 dmg, large AoE */
        .id = MT_FG_SPA,
        .name = "Hellstorm Artillery",
        .sprite_name = "uffarst0.spr",
        .shadow_name = "uffarst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 3.5f,
        .max_hp = 133,
        .attack = { .range = 45.0f, .damage = 30, .cooldown_ms = 2667 },
    },
    /* === Air units === */
    {   /* BkLaser: range 5, 233ms cd, 10 dmg */
        .id = MT_FG_SKY_BIKE,
        .name = "Sky Bike",
        .sprite_name = "ufskbst0.spr",
        .shadow_name = "ufskbst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 6.0f,
        .max_hp = 100,
        .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 233 },
    },
    {   /* OutriderMissile: range 5, 333ms cd, 20 dmg */
        .id = MT_FG_OUTRIDER,
        .name = "Outrider",
        .sprite_name = "ufoutst0.spr",
        .shadow_name = "ufoutst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 5.0f,
        .max_hp = 200,
        .attack = { .range = 5.0f, .damage = 20, .cooldown_ms = 333 },
    },
    /* === Experimental / special === */
    {   /* SeismicWave: range 24, slow cd, 17 dmg */
        .id = MT_FG_SHOCKWAVE,
        .name = "Shockwave",
        .sprite_name = "ufswvst0.spr",
        .shadow_name = "ufswvst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 3.5f,
        .max_hp = 166,
        .attack = { .range = 24.0f, .damage = 17, .cooldown_ms = 2000 },
    },
    {   /* Contaminator: range 1, 67ms cd, 5 dmg — targets buildings */
        .id = MT_FG_CONTAMINATOR,
        .name = "Water Contaminator",
        .sprite_name = "ucwcost0.spr",
        .shadow_name = "ucwcost0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 3.0f,
        .max_hp = 166,
        .attack = { .range = 1.0f, .damage = 5, .cooldown_ms = 67 },
    },
    {   /* Spawned from Phasing Facility — internal tunnel unit */
        .id = MT_FG_UNDERGROUND_TUNNEL,
        .name = "Phase Runner",
        .sprite_name = "ufphrst0.spr",
        .shadow_name = "ufphrst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 4.5f,
        .max_hp = 150,
    },
    {   /* Base relocation unit */
        .id = MT_FG_BASE_MOVER,
        .name = "Base Mover",
        .sprite_name = "ufbamst0.spr",
        .shadow_name = "ufbamst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 3.0f,
        .max_hp = 500,
    },
    /* === Buildings — passive === */
#define BUILDING(id_, name_, sprite_, shadow_, hp_) \
    { .id = (id_), .name = (name_), .sprite_name = (sprite_), .shadow_name = (shadow_), \
      .traits = MF_SELECTABLE | MF_RENDERABLE, .max_hp = (hp_) }
    {
        .id = MT_FG_HQ1,
        .name = "FG Headquarters 1",
        .sprite_name = "nfhqt1l0.spr",
        .shadow_name = "bfhqtsh0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE,
        .max_hp = 1200,
    },
    BUILDING(MT_FG_HQ2, "FG Headquarters 2", "nfhqt2l0.spr", "bfhqtsh0.spr", 2400),
    BUILDING(MT_FG_HQ3, "FG Headquarters 3", "nfhqt3l0.spr", "bfhqtsh0.spr", 3600),
    BUILDING(MT_FG_BARRACKS, "Barracks", "nfutf1l0.spr", "bfutfmn0.spr", 750),
    BUILDING(MT_FG_ADV_BARRACKS, "Advanced Barracks", "nfutf2l0.spr", "bfutfmn1.spr", 1500),
    BUILDING(MT_FG_VEHICLE_FACTORY, "Vehicle Factory", "nfvcy1l0.spr", "bfvcymn0.spr", 1000),
    BUILDING(MT_FG_ADV_VEHICLE_FACTORY, "Advanced Vehicle Factory", "nfvcy2l0.spr", "bfvcymn1.spr", 2000),
    BUILDING(MT_FG_HOVER_FACTORY, "Hovercraft Factory", "nfhsp1l0.spr", "bfhspmn0.spr", 600),
    BUILDING(MT_FG_REPAIR_BAY, "Repair Bay", "nfrep1l0.spr", "bfrepmn0.spr", 600),
    BUILDING(MT_FG_PHASE_FACTORY_1, "Phase Factory 1", "nfphf1l0.spr", "bfphfmn0.spr", 1000),
    BUILDING(MT_FG_PHASE_FACTORY_2, "Phase Factory 2", "nfphf2l0.spr", "bfphfmn1.spr", 2000),
    BUILDING(MT_FG_CAMERA_TOWER, "Camera Tower", "nccam1l0.spr", "bccammn0.spr", 150),
    BUILDING(MT_FG_LIFE_PLANT, "Life Plant", "nclnc1l0.spr", "bclncmn0.spr", 1300),
    BUILDING(MT_FG_POWER_PLANT, "Power Plant", "ncpow1l0.spr", "bcpowmn0.spr", 1450),
    BUILDING(MT_FG_REFINERY, "Refinery", "nfrrm1l0.spr", "bfrrmmn0.spr", 800),
    BUILDING(MT_FG_BRIDGE_H, "Small Horizontal Bridge", "ncsbh1l0.spr", "bcsbhmn0.spr", 400),
    BUILDING(MT_FG_BRIDGE_V, "Small Vertical Bridge", "ncsbv1l0.spr", "bcsbvmn0.spr", 400),
    BUILDING(MT_FG_BRIDGE_C, "Small Centre Bridge", "ncsbc1l0.spr", "bcsbcmn0.spr", 400),
#undef BUILDING
    /* === Buildings — combat (MF_ATTACK) === */
    {   /* GatLaser: range 5, 100ms cd, 10 dmg */
        .id = MT_FG_GUARD_TOWER,
        .name = "Guard Tower",
        .sprite_name = "nfgdt1l0.spr",
        .shadow_name = "bfgdtmn0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
        .max_hp = 400,
        .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 100 },
    },
    {   /* FixedLaserPlat: range 8, 333ms cd, 13 dmg */
        .id = MT_FG_ADV_GUARD_TOWER,
        .name = "Advanced Guard Tower",
        .sprite_name = "nfagt1l0.spr",
        .shadow_name = "bfagtmn0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
        .max_hp = 550,
        .attack = { .range = 8.0f, .damage = 13, .cooldown_ms = 333 },
    },
    {   /* FixedGroundToAirLaser: range 10, 467ms cd, 40 dmg — anti-air */
        .id = MT_FG_AA_SITE,
        .name = "Anti-Air Site",
        .sprite_name = "nfaar1l0.spr",
        .shadow_name = "bfaarmn0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
        .max_hp = 600,
        .attack = { .range = 10.0f, .damage = 40, .cooldown_ms = 467 },
    },
};


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

void G_InitGame(void) {
}

bool G_DoLoadLevel(const char *path, level_t *out) {
    return load_dark_map(path, out);
}

bool W_LoadAssets(SDL_Renderer *renderer, const char *root, const level_t *map,
                  const char *sprite, tileset_t *tileset, spritesheet_t *unit_sprite) {
    return plugin_load_assets(renderer, root, map, sprite, tileset, unit_sprite);
}

int P_LoadThings(const char *path) {
    return load_dark_reign_initial_units(path);
}

bool R_InitSprites(SDL_Renderer *renderer, const char *root, const level_t *map,
                          mobj_t *const *mobjs, int count, spritecache_t *cache) {
    return load_dark_reign_decoration_sprites(renderer, root, map,
                                              mobjs, count, cache);
}

bool HU_LoadFont(SDL_Renderer *renderer, const char *root, bitmapfont_t *font) {
    (void)renderer; (void)root; (void)font;
    return false;
}

void  G_MissionTicker(level_t *map, mobj_t *const *mobjs, int *count,
                      hudtext_t *hud, float dt) {
    (void)map; (void)mobjs; (void)count;
    (void)hud; (void)dt;
}

void *G_InitCustomUI(app_t *app, const char *data_root) {
    (void)app; (void)data_root;
    return NULL;
}

bool G_CustomUIResponder(void *ui, const app_t *app, level_t *map,
                         mobj_t *const *units, int unit_count, const SDL_Event *event) {
    (void)ui; (void)app; (void)map; (void)units; (void)unit_count; (void)event;
    return false;
}

void G_CustomUITicker(void *ui) {
    (void)ui;
}

void G_CustomUIDrawer(void *ui, app_t *app, const level_t *map,
                      mobj_t *const *units, int unit_count,
                      const spritecache_t *sprites, const hudtext_t *hud) {
    (void)ui; (void)app; (void)map; (void)units; (void)unit_count;
    (void)sprites; (void)hud;
}

bool G_UpdateProduction(void *ui, level_t *map, mobj_t *const *units, int *unit_count,
                        float dt) {
    (void)ui; (void)map; (void)units; (void)unit_count;
    (void)dt;
    return false;
}

void G_ShutdownCustomUI(void *ui) {
    (void)ui;
}

int G_WorldViewportWidth(const app_t *app) {
    if (!app) return 0;
    if (gameui && gameui->world_viewport.w > 0 && gameui->logical_width > 0)
        return gameui->world_viewport.w * app->win.w / gameui->logical_width;
    return app->win.w > 0 ? app->win.w : 1;
}
