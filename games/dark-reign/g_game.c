#include "game.h"
#include "dr_types.h"

bool load_dark_map(const char *map_path, level_t *out);
bool dark_reign_plugin_load_assets(SDL_Renderer *renderer, const char *data_root,
                                   const level_t *map, const char *sprite_name,
                                   tileset_t *tileset, spritesheet_t *unit_sprite);
int load_dark_reign_initial_units(const char *map_path, mobj_t *units, int max_units);
bool load_dark_reign_decoration_sprites(SDL_Renderer *renderer, const char *data_root,
                                        const level_t *map, const mobj_t *units, int unit_count,
                                        spritecache_t *cache);

static const uiimage_t DARK_REIGN_UI_IMAGES[] = {
    { "graphics/INTFACE/IGI/TOPBTNS.BMP", {   0, 0, 126, 32 }, {   0,   0, 126,  32 } },
    { "graphics/INTFACE/IGI/TOPBITS.BMP", {   0, 0, 154, 32 }, { 126,   0, 154,  32 } },
    { "graphics/INTFACE/IGI/TOPBTNS.BMP", { 126, 0, 168, 32 }, { 280,   0, 168,  32 } },
    { "graphics/INTFACE/IGI/MFDBTNS.BMP", {   0, 0, 192, 64 }, { 448,   0, 192,  64 } },
    { "graphics/INTFACE/IGI/MFDBAC1.BMP", {   0, 0, 192,278 }, { 448,  64, 192, 278 } },
    { "graphics/INTFACE/IGI/BUBLDBIT.BMP",{   0, 0, 192, 28 }, { 448, 314, 192,  28 } },
    { "graphics/INTFACE/IGI/MINIMAP.BMP", {   0, 0, 140,138 }, { 448, 342, 140, 138 } },
    { "graphics/INTFACE/IGI/RESOBARS.BMP",{   0, 0,  52,104 }, { 588, 376,  52, 104 } },
};

static const uidefinition_t DARK_REIGN_UI = {
    .logical_width = 640,
    .logical_height = 480,
    .world_viewport = { 0, 32, 448, 448 },
    .minimap = { 454, 348, 128, 126 },
    .command_grid = { 450, 66, 188, 246 },
    .command_columns = 3,
    .command_rows = 4,
    .resources = {
        [0] = { .text = { 216, 5 }, .color = { 55, 242, 238, 255 } },
    },
    .resource_count = 1,
    .images = DARK_REIGN_UI_IMAGES,
    .image_count = (int)(sizeof(DARK_REIGN_UI_IMAGES) / sizeof(DARK_REIGN_UI_IMAGES[0])),
};

static const actortype_t DARK_REIGN_ACTOR_TYPES[] = {
    /* === Special / support units === */
    {
        .id = DR_ACTOR_FG_CONSTRUCTION_CREW,
        .name = "Construction Rig",
        .sprite_name = "ucfcnst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 5.5f,
        .max_hp = 100,
        .attack = { .range = 9.0f, .damage = 20, .cooldown_ms = 700, .anim_ms = 400 },
    },
    {
        .id = DR_ACTOR_FG_GROUND_TRANSPORTER,
        .name = "Freighter",
        .sprite_name = "ucfrgst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER,
        .speed = 4.5f,
        .max_hp = 750,
        .harvest = { .capacity = 100 },
    },
    {   /* Laser-armed hover harvester */
        .id = DR_ACTOR_FG_HOVER_TRANSPORTER,
        .name = "Hover Freighter",
        .sprite_name = "uchfrst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER | MF_ATTACK,
        .speed = 4.5f,
        .max_hp = 500,
        .attack = { .range = 4.0f, .damage = 11, .cooldown_ms = 267, .anim_ms = 150 },
        .harvest = { .capacity = 100 },
    },
    /* === Infantry === */
    {   /* LaserRifle: range 4, 267ms cd, 11 dmg */
        .id = DR_ACTOR_FG_RAIDER,
        .name = "Raider",
        .sprite_name = "ufradst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 5.0f,
        .max_hp = 100,
        .attack = { .range = 4.0f, .damage = 11, .cooldown_ms = 267, .anim_ms = 150 },
    },
    {   /* RailGun: range 5, 367ms cd, 11 dmg */
        .id = DR_ACTOR_FG_MERCENARY,
        .name = "Mercenary",
        .sprite_name = "ufmrcst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 4.8f,
        .max_hp = 125,
        .attack = { .range = 5.0f, .damage = 11, .cooldown_ms = 367, .anim_ms = 200 },
    },
    {   /* SniperRifle: range 8, 1667ms cd, 150 dmg */
        .id = DR_ACTOR_FG_SNIPER,
        .name = "Sniper",
        .sprite_name = "ufsnpst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 4.5f,
        .max_hp = 100,
        .attack = { .range = 8.0f, .damage = 150, .cooldown_ms = 1667, .anim_ms = 500 },
    },
    {   /* Recon only — no weapon */
        .id = DR_ACTOR_FG_SCOUT,
        .name = "Scout",
        .sprite_name = "ufsctst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 6.0f,
        .max_hp = 66,
    },
    {   /* MedicHeal — support, no offensive attack */
        .id = DR_ACTOR_FG_MEDIC,
        .name = "Field Medic",
        .sprite_name = "ufmedst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 4.8f,
        .max_hp = 66,
    },
    {   /* Sabotage ability — no ranged weapon */
        .id = DR_ACTOR_FG_SABOTEUR,
        .name = "Saboteur",
        .sprite_name = "ufsabst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 4.8f,
        .max_hp = 100,
    },
    {   /* MechanicRepair — support, no offensive attack */
        .id = DR_ACTOR_FG_MECHANIC,
        .name = "Mechanic",
        .sprite_name = "ufmecst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 4.8f,
        .max_hp = 66,
    },
    {   /* SuicideNuke: range 2, 1667ms, 180 dmg, large AoE */
        .id = DR_ACTOR_FG_SUICIDE_NUKER,
        .name = "Martyr",
        .sprite_name = "ufmtrst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 4.8f,
        .max_hp = 100,
        .attack = { .range = 2.0f, .damage = 180, .cooldown_ms = 1667, .anim_ms = 500 },
    },
    {   /* Infiltrate ability — no ranged weapon */
        .id = DR_ACTOR_FG_SPY,
        .name = "Infiltrator",
        .sprite_name = "ucinfst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 4.8f,
        .max_hp = 66,
    },
    /* === Vehicles === */
    {   /* DoubleRailGun: range 5, 433ms cd, 10 dmg */
        .id = DR_ACTOR_FG_SPYDER_BIKE,
        .name = "Spider Bike",
        .sprite_name = "ufspbst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 6.5f,
        .max_hp = 133,
        .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 433, .anim_ms = 200 },
    },
    {   /* Rapid armored transport — no weapon */
        .id = DR_ACTOR_FG_IFV,
        .name = "RAT",
        .sprite_name = "ufratst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 5.0f,
        .max_hp = 200,
    },
    {   /* SkirmishGun (dual): range 6, 667ms cd, 14 dmg */
        .id = DR_ACTOR_FG_MEDIUM_TANK,
        .name = "Skirmish Tank",
        .sprite_name = "ufsktst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 4.0f,
        .max_hp = 133,
        .attack = { .range = 6.0f, .damage = 14, .cooldown_ms = 667, .anim_ms = 300 },
    },
    {   /* TankHunterGun: range 3, 667ms cd, 60 dmg — high anti-armor */
        .id = DR_ACTOR_FG_TANK_HUNTER,
        .name = "Tank Hunter",
        .sprite_name = "ufthnst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 4.0f,
        .max_hp = 150,
        .attack = { .range = 3.0f, .damage = 60, .cooldown_ms = 667, .anim_ms = 300 },
    },
    {   /* PhaseTankCannon: range 6, 433ms cd, 30 dmg */
        .id = DR_ACTOR_FG_PHASE_TANK,
        .name = "Phase Tank",
        .sprite_name = "ufphtst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 4.0f,
        .max_hp = 166,
        .attack = { .range = 6.0f, .damage = 30, .cooldown_ms = 433, .anim_ms = 200 },
    },
    {   /* Chaff: range 8, 500ms cd, 8 dmg — anti-air */
        .id = DR_ACTOR_FG_MAD,
        .name = "Flak Jack",
        .sprite_name = "ufflkst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 4.0f,
        .max_hp = 100,
        .attack = { .range = 8.0f, .damage = 8, .cooldown_ms = 500, .anim_ms = 200 },
    },
    {   /* TripleRailGun: range 8, 667ms cd, 24 dmg */
        .id = DR_ACTOR_FG_TRIPLE_RAIL_TANK,
        .name = "Triple Rail Tank",
        .sprite_name = "uftrtst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 3.5f,
        .max_hp = 200,
        .attack = { .range = 8.0f, .damage = 24, .cooldown_ms = 667, .anim_ms = 300 },
    },
    {   /* ArtilleryShell: range 45, 2667ms cd, 30 dmg, large AoE */
        .id = DR_ACTOR_FG_SPA,
        .name = "Hellstorm Artillery",
        .sprite_name = "uffarst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 3.5f,
        .max_hp = 133,
        .attack = { .range = 45.0f, .damage = 30, .cooldown_ms = 2667, .anim_ms = 800 },
    },
    /* === Air units === */
    {   /* BkLaser: range 5, 233ms cd, 10 dmg */
        .id = DR_ACTOR_FG_SKY_BIKE,
        .name = "Sky Bike",
        .sprite_name = "ufskbst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 6.0f,
        .max_hp = 100,
        .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 233, .anim_ms = 100 },
    },
    {   /* OutriderMissile: range 5, 333ms cd, 20 dmg */
        .id = DR_ACTOR_FG_OUTRIDER,
        .name = "Outrider",
        .sprite_name = "ufoutst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 5.0f,
        .max_hp = 200,
        .attack = { .range = 5.0f, .damage = 20, .cooldown_ms = 333, .anim_ms = 150 },
    },
    /* === Experimental / special === */
    {   /* SeismicWave: range 24, slow cd, 17 dmg */
        .id = DR_ACTOR_FG_SHOCKWAVE,
        .name = "Shockwave",
        .sprite_name = "ufswvst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 3.5f,
        .max_hp = 166,
        .attack = { .range = 24.0f, .damage = 17, .cooldown_ms = 2000, .anim_ms = 1000 },
    },
    {   /* Contaminator: range 1, 67ms cd, 5 dmg — targets buildings */
        .id = DR_ACTOR_FG_CONTAMINATOR,
        .name = "Water Contaminator",
        .sprite_name = "ucwcost0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
        .speed = 3.0f,
        .max_hp = 166,
        .attack = { .range = 1.0f, .damage = 5, .cooldown_ms = 67, .anim_ms = 50 },
    },
    {   /* Spawned from Phasing Facility — internal tunnel unit */
        .id = DR_ACTOR_FG_UNDERGROUND_TUNNEL,
        .name = "Phase Runner",
        .sprite_name = "ucphrst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 4.5f,
        .max_hp = 150,
    },
    {   /* Base relocation unit */
        .id = DR_ACTOR_FG_BASE_MOVER,
        .name = "Base Mover",
        .sprite_name = "ucbmvst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE,
        .speed = 3.0f,
        .max_hp = 500,
    },
    /* === Buildings — passive === */
#define DR_BUILDING(id_, name_, sprite_, shadow_, hp_) \
    { .id = (id_), .name = (name_), .sprite_name = (sprite_), .shadow_name = (shadow_), \
      .traits = MF_SELECTABLE | MF_RENDERABLE, .max_hp = (hp_) }
    {
        .id = DR_ACTOR_FG_HEADQUARTERS_1,
        .name = "FG Headquarters 1",
        .sprite_name = "nfhqt1l0.spr",
        .shadow_name = "bfhqtsh0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE,
        .max_hp = 1200,
    },
    DR_BUILDING(DR_ACTOR_FG_HEADQUARTERS_2, "FG Headquarters 2", "nfhqt2l0.spr", "bfhqtsh0.spr", 2400),
    DR_BUILDING(DR_ACTOR_FG_HEADQUARTERS_3, "FG Headquarters 3", "nfhqt3l0.spr", "bfhqtsh0.spr", 3600),
    DR_BUILDING(DR_ACTOR_FG_TRAINING_FACILITY_1, "Barracks", "nfutf1l0.spr", "bfutfmn0.spr", 750),
    DR_BUILDING(DR_ACTOR_FG_TRAINING_FACILITY_2, "Advanced Barracks", "nfutf2l0.spr", "bfutfmn1.spr", 1500),
    DR_BUILDING(DR_ACTOR_FG_VEHICLE_FACTORY_1, "Vehicle Factory", "nfvcy1l0.spr", "bfvcymn0.spr", 1000),
    DR_BUILDING(DR_ACTOR_FG_VEHICLE_FACTORY_2, "Advanced Vehicle Factory", "nfvcy2l0.spr", "bfvcymn1.spr", 2000),
    DR_BUILDING(DR_ACTOR_FG_HOVER_FACTORY, "Hovercraft Factory", "nfhsp1l0.spr", "bfhspmn0.spr", 600),
    DR_BUILDING(DR_ACTOR_FG_REPAIR_BAY, "Repair Bay", "nfrep1l0.spr", "bfrepmn0.spr", 600),
    DR_BUILDING(DR_ACTOR_FG_PHASE_FACTORY_1, "Phase Factory 1", "nfphf1l0.spr", "bfphfmn0.spr", 1000),
    DR_BUILDING(DR_ACTOR_FG_PHASE_FACTORY_2, "Phase Factory 2", "nfphf2l0.spr", "bfphfmn1.spr", 2000),
    DR_BUILDING(DR_ACTOR_FG_CAMERA_TOWER, "Camera Tower", "nccam1l0.spr", "bccammn0.spr", 150),
    DR_BUILDING(DR_ACTOR_FG_LIFE_PLANT, "Life Plant", "nclnc1l0.spr", "bclncmn0.spr", 1300),
    DR_BUILDING(DR_ACTOR_FG_POWER_PLANT, "Power Plant", "ncpow1l0.spr", "bcpowmn0.spr", 1450),
    DR_BUILDING(DR_ACTOR_FG_REFINERY, "Refinery", "nfrrm1l0.spr", "bfrrmmn0.spr", 800),
    DR_BUILDING(DR_ACTOR_FG_SMALL_HORIZONTAL_BRIDGE, "Small Horizontal Bridge", "ncsbh1l0.spr", "bcsbhmn0.spr", 400),
    DR_BUILDING(DR_ACTOR_FG_SMALL_VERTICAL_BRIDGE, "Small Vertical Bridge", "ncsbv1l0.spr", "bcsbvmn0.spr", 400),
    DR_BUILDING(DR_ACTOR_FG_SMALL_CENTRE_BRIDGE, "Small Centre Bridge", "ncsbc1l0.spr", "bcsbcmn0.spr", 400),
#undef DR_BUILDING
    /* === Buildings — combat (MF_ATTACK) === */
    {   /* GatLaser: range 5, 100ms cd, 10 dmg */
        .id = DR_ACTOR_FG_GUARD_TOWER,
        .name = "Guard Tower",
        .sprite_name = "nfgdt1l0.spr",
        .shadow_name = "bfgdtmn0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
        .max_hp = 400,
        .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 100, .anim_ms = 60 },
    },
    {   /* FixedLaserPlat: range 8, 333ms cd, 13 dmg */
        .id = DR_ACTOR_FG_ADVANCED_GUARD_TOWER,
        .name = "Advanced Guard Tower",
        .sprite_name = "nfagt1l0.spr",
        .shadow_name = "bfagtmn0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
        .max_hp = 550,
        .attack = { .range = 8.0f, .damage = 13, .cooldown_ms = 333, .anim_ms = 150 },
    },
    {   /* FixedGroundToAirLaser: range 10, 467ms cd, 40 dmg — anti-air */
        .id = DR_ACTOR_FG_AA_SITE,
        .name = "Anti-Air Site",
        .sprite_name = "nfaar1l0.spr",
        .shadow_name = "bfaarmn0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
        .max_hp = 600,
        .attack = { .range = 10.0f, .damage = 40, .cooldown_ms = 467, .anim_ms = 200 },
    },
};

static const gameinfo_t DARK_REIGN_GAME_INFO = {
    .direction_mode   = RTS_DIRECTION_DARK_REIGN_8,
    .selection_marker = { .style = SELECTION_STYLE_BRACKETS, .sprite = -1 },
};

/* ── game identity (Doom-style externs) ─────────────────────────────────── */

const char *const g_game_id            = "dark-reign";
const char *const g_game_name          = "Dark Reign";
const char *const g_game_default_root  = "data/REIGN/dark";
const char *const g_game_default_map   = "scenario/FIXED/M01F/M01F.SCN";
const char *const g_game_default_sprite = "ucfcnst0.spr";
const int g_cell_w = 24;
const int g_cell_h = 24;
const uint16_t g_debug_enemy_type = DR_ACTOR_FG_CONSTRUCTION_CREW;
const gameinfo_t *const gameinfo = &DARK_REIGN_GAME_INFO;
const actortype_t *const mobjinfo =
    (const actortype_t *)DARK_REIGN_ACTOR_TYPES;
const int num_mobjinfo =
    (int)(sizeof(DARK_REIGN_ACTOR_TYPES) / sizeof(DARK_REIGN_ACTOR_TYPES[0]));
const uidefinition_t *const gameui = &DARK_REIGN_UI;

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

bool G_DoLoadLevel(const char *path, level_t *out) {
    return load_dark_map(path, out);
}

bool W_LoadAssets(SDL_Renderer *renderer, const char *root, const level_t *map,
                  const char *sprite, tileset_t *tileset, spritesheet_t *unit_sprite) {
    return dark_reign_plugin_load_assets(renderer, root, map, sprite, tileset, unit_sprite);
}

int P_LoadThings(const char *path, mobj_t *mobjs, int max) {
    return load_dark_reign_initial_units(path, (mobj_t *)mobjs, max);
}

bool R_InitSprites(SDL_Renderer *renderer, const char *root, const level_t *map,
                          const mobj_t *mobjs, int count, spritecache_t *cache) {
    return load_dark_reign_decoration_sprites(renderer, root, map,
                                              (const mobj_t *)mobjs, count, cache);
}

bool HU_LoadFont(SDL_Renderer *renderer, const char *root, bitmapfont_t *font) {
    (void)renderer; (void)root; (void)font;
    return false;
}

void *G_LoadMission(const char *path) { (void)path; return NULL; }
void  G_MissionTicker(void *m, level_t *map, mobj_t *mobjs, int *count,
                      effect_t *effects, int max_effects, hudtext_t *hud, float dt) {
    (void)m; (void)map; (void)mobjs; (void)count;
    (void)effects; (void)max_effects; (void)hud; (void)dt;
}
void  G_FreeMission(void *m) { (void)m; }
