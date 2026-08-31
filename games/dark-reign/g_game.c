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
    {
        .id = DR_ACTOR_FG_CONSTRUCTION_CREW,
        .name = "Construction Rig",
        .sprite_name = "ucfcnst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 5.5f,
        .max_hp = 200,
        .attack_range = 9.0f,
        .attack_damage = 20,
        .attack_cooldown_ms = 700,
        .attack_anim_ms = 400,
    },
    {
        .id = DR_ACTOR_FG_GROUND_TRANSPORTER,
        .name = "Freighter",
        .sprite_name = "ucfrgst0.spr",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_HARVESTER,
        .speed = 4.5f,
        .max_hp = 750,
    },
    {
        .id = DR_ACTOR_FG_HEADQUARTERS_1,
        .name = "FG Headquarters 1",
        .sprite_name = "nfhqt1l0.spr",
        .shadow_name = "bfhqtsh0.spr",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 1200,
    },
#define DR_MOBILE(id_, name_, sprite_, hp_, speed_) \
    { .id = (id_), .name = (name_), .sprite_name = (sprite_), \
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK, \
      .speed = (speed_), .max_hp = (hp_), .attack_range = 7.0f, \
      .attack_damage = 10, .attack_cooldown_ms = 900, .attack_anim_ms = 400 }
    DR_MOBILE(DR_ACTOR_FG_RAIDER, "Raider", "ufradst0.spr", 100, 5.0f),
    DR_MOBILE(DR_ACTOR_FG_MERCENARY, "Mercenary", "ufmrcst0.spr", 125, 4.8f),
    DR_MOBILE(DR_ACTOR_FG_SNIPER, "Sniper", "ufsnpst0.spr", 100, 4.5f),
    DR_MOBILE(DR_ACTOR_FG_SCOUT, "Scout", "ufsctst0.spr", 66, 6.0f),
    DR_MOBILE(DR_ACTOR_FG_MEDIC, "Medic", "ufmedst0.spr", 66, 4.8f),
    DR_MOBILE(DR_ACTOR_FG_SABOTEUR, "Saboteur", "ufsabst0.spr", 100, 4.8f),
    DR_MOBILE(DR_ACTOR_FG_MECHANIC, "Mechanic", "ufmecst0.spr", 66, 4.8f),
    DR_MOBILE(DR_ACTOR_FG_SUICIDE_NUKER, "Martyr", "ufmtrst0.spr", 100, 4.8f),
    DR_MOBILE(DR_ACTOR_FG_SPY, "Spy", "ucinfst0.spr", 66, 4.8f),
    DR_MOBILE(DR_ACTOR_FG_SPYDER_BIKE, "Spider Bike", "ufspbst0.spr", 133, 6.5f),
    DR_MOBILE(DR_ACTOR_FG_IFV, "RAT", "ufratst0.spr", 200, 5.0f),
    DR_MOBILE(DR_ACTOR_FG_MEDIUM_TANK, "Skirmish Tank", "ufsktst0.spr", 133, 4.0f),
    DR_MOBILE(DR_ACTOR_FG_TANK_HUNTER, "Tank Hunter", "ufthnst0.spr", 150, 4.0f),
    DR_MOBILE(DR_ACTOR_FG_PHASE_TANK, "Phase Tank", "ufphtst0.spr", 166, 4.0f),
    DR_MOBILE(DR_ACTOR_FG_MAD, "Flak Jack", "ufflkst0.spr", 100, 4.0f),
    DR_MOBILE(DR_ACTOR_FG_TRIPLE_RAIL_TANK, "Triple Rail Tank", "uftrtst0.spr", 200, 3.5f),
    DR_MOBILE(DR_ACTOR_FG_SPA, "Hellstorm Artillery", "uffarst0.spr", 133, 3.5f),
    DR_MOBILE(DR_ACTOR_FG_SKY_BIKE, "Sky Bike", "ufskbst0.spr", 100, 6.0f),
    DR_MOBILE(DR_ACTOR_FG_OUTRIDER, "Outrider", "ufoutst0.spr", 200, 5.0f),
    DR_MOBILE(DR_ACTOR_FG_SHOCKWAVE, "Shockwave", "ufswvst0.spr", 166, 3.5f),
    DR_MOBILE(DR_ACTOR_FG_CONTAMINATOR, "Water Contaminator", "ucwcost0.spr", 166, 3.0f),
    DR_MOBILE(DR_ACTOR_FG_HOVER_TRANSPORTER, "Hover Freighter", "uchfrst0.spr", 500, 4.5f),
#undef DR_MOBILE
#define DR_BUILDING(id_, name_, sprite_, shadow_, hp_) \
    { .id = (id_), .name = (name_), .sprite_name = (sprite_), .shadow_name = (shadow_), \
      .traits = MF_SELECTABLE | MF_RENDERABLE, .speed = 0.0f, .max_hp = (hp_) }
    DR_BUILDING(DR_ACTOR_FG_HEADQUARTERS_2, "FG Headquarters 2", "nfhqt2l0.spr", "bfhqtsh0.spr", 2400),
    DR_BUILDING(DR_ACTOR_FG_HEADQUARTERS_3, "FG Headquarters 3", "nfhqt3l0.spr", "bfhqtsh0.spr", 3600),
    DR_BUILDING(DR_ACTOR_FG_TRAINING_FACILITY_1, "Barracks", "nfutf1l0.spr", "bfutfmn0.spr", 750),
    DR_BUILDING(DR_ACTOR_FG_TRAINING_FACILITY_2, "Advanced Barracks", "nfutf2l0.spr", "bfutfmn1.spr", 1500),
    DR_BUILDING(DR_ACTOR_FG_VEHICLE_FACTORY_1, "Vehicle Factory", "nfvcy1l0.spr", "bfvcymn0.spr", 1000),
    DR_BUILDING(DR_ACTOR_FG_VEHICLE_FACTORY_2, "Advanced Vehicle Factory", "nfvcy2l0.spr", "bfvcymn1.spr", 2000),
    DR_BUILDING(DR_ACTOR_FG_HOVER_FACTORY, "Hovercraft Factory", "nfhsp1l0.spr", "bfhspmn0.spr", 600),
    DR_BUILDING(DR_ACTOR_FG_REPAIR_BAY, "Repair Bay", "nfrep1l0.spr", "bfrepmn0.spr", 600),
    DR_BUILDING(10015, "Phase Factory 1", "nfphf1l0.spr", "bfphfmn0.spr", 1000),
    DR_BUILDING(10016, "Phase Factory 2", "nfphf2l0.spr", "bfphfmn1.spr", 2000),
    DR_BUILDING(DR_ACTOR_FG_CAMERA_TOWER, "Camera Tower", "nccam1l0.spr", "bccammn0.spr", 150),
    DR_BUILDING(DR_ACTOR_FG_AA_SITE, "Anti-Air Site", "nfaar1l0.spr", "bfaarmn0.spr", 600),
    DR_BUILDING(DR_ACTOR_FG_GUARD_TOWER, "Guard Tower", "nfgdt1l0.spr", "bfgdtmn0.spr", 400),
    DR_BUILDING(DR_ACTOR_FG_ADVANCED_GUARD_TOWER, "Advanced Guard Tower", "nfagt1l0.spr", "bfagtmn0.spr", 550),
    DR_BUILDING(DR_ACTOR_FG_LIFE_PLANT, "Life Plant", "nclnc1l0.spr", "bclncmn0.spr", 1300),
    DR_BUILDING(DR_ACTOR_FG_POWER_PLANT, "Power Plant", "ncpow1l0.spr", "bcpowmn0.spr", 1450),
    DR_BUILDING(DR_ACTOR_FG_REFINERY, "Refinery", "nfrrm1l0.spr", "bfrrmmn0.spr", 800),
    DR_BUILDING(10040, "Small Horizontal Bridge", "ncsbh1l0.spr", "bcsbhmn0.spr", 400),
    DR_BUILDING(10041, "Small Vertical Bridge", "ncsbv1l0.spr", "bcsbvmn0.spr", 400),
    DR_BUILDING(10042, "Small Centre Bridge", "ncsbc1l0.spr", "bcsbcmn0.spr", 400),
#undef DR_BUILDING
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
