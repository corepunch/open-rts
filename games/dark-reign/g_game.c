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
