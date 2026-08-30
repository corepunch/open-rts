#include "plugin.h"
#include "dr_types.h"

bool load_dark_map(const char *map_path, GameMap *out);
bool dark_reign_plugin_load_assets(SDL_Renderer *renderer, const char *data_root,
                                   const GameMap *map, const char *sprite_name,
                                   Tileset *tileset, SpriteSheet *unit_sprite);
int load_dark_reign_initial_units(const char *map_path, Unit *units, int max_units);
bool load_dark_reign_decoration_sprites(SDL_Renderer *renderer, const char *data_root,
                                        const GameMap *map, const Unit *units, int unit_count,
                                        SpriteCache *cache);

static const GameUiImage DARK_REIGN_UI_IMAGES[] = {
    { "graphics/INTFACE/IGI/TOPBTNS.BMP", {   0, 0, 126, 32 }, {   0,   0, 126,  32 } },
    { "graphics/INTFACE/IGI/TOPBITS.BMP", {   0, 0, 154, 32 }, { 126,   0, 154,  32 } },
    { "graphics/INTFACE/IGI/TOPBTNS.BMP", { 126, 0, 168, 32 }, { 280,   0, 168,  32 } },
    { "graphics/INTFACE/IGI/MFDBTNS.BMP", {   0, 0, 192, 64 }, { 448,   0, 192,  64 } },
    { "graphics/INTFACE/IGI/MFDBAC1.BMP", {   0, 0, 192,278 }, { 448,  64, 192, 278 } },
    { "graphics/INTFACE/IGI/BUBLDBIT.BMP",{   0, 0, 192, 28 }, { 448, 314, 192,  28 } },
    { "graphics/INTFACE/IGI/MINIMAP.BMP", {   0, 0, 140,138 }, { 448, 342, 140, 138 } },
    { "graphics/INTFACE/IGI/RESOBARS.BMP",{   0, 0,  52,104 }, { 588, 376,  52, 104 } },
};

static const GameUiDefinition DARK_REIGN_UI = {
    .logical_width = 640,
    .logical_height = 480,
    .world_viewport = { 0, 32, 448, 448 },
    .minimap = { 454, 348, 128, 126 },
    .command_grid = { 450, 66, 188, 246 },
    .command_columns = 3,
    .command_rows = 4,
    .resource_text = { 216, 5 },
    .resource_color = { 55, 242, 238, 255 },
    .images = DARK_REIGN_UI_IMAGES,
    .image_count = (int)(sizeof(DARK_REIGN_UI_IMAGES) / sizeof(DARK_REIGN_UI_IMAGES[0])),
};

static const ActorType DARK_REIGN_ACTOR_TYPES[] = {
    {
        .id = DR_ACTOR_FG_CONSTRUCTION_CREW,
        .name = "Construction Rig",
        .sprite_name = "ucfcnst0.spr",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                  RTS_TRAIT_RENDERABLE | RTS_TRAIT_ATTACK,
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
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                  RTS_TRAIT_RENDERABLE | RTS_TRAIT_HARVESTER,
        .speed = 4.5f,
        .max_hp = 750,
    },
    {
        .id = DR_ACTOR_FG_HEADQUARTERS_1,
        .name = "FG Headquarters 1",
        .sprite_name = "nfhqt1l0.spr",
        .shadow_name = "bfhqtsh0.spr",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 1200,
    },
};

static const GameInfo DARK_REIGN_GAME_INFO = {
    .direction_mode   = RTS_DIRECTION_DARK_REIGN_8,
    .selection_marker = { .style = SELECTION_STYLE_BRACKETS, .sprite = -1 },
};

static bool dark_reign_load_runtime_sprites(SDL_Renderer *renderer, const char *data_root,
                                            const GameMap *map, const Unit *units, int unit_count,
                                            SpriteCache *cache) {
    return load_dark_reign_decoration_sprites(renderer, data_root, map, units, unit_count, cache);
}

static const Plugin DARK_REIGN_PLUGIN = {
    .id             = "dark-reign",
    .name           = "Dark Reign",
    .version        = "0.1",
    .default_root   = "data/REIGN/dark",
    .default_map    = "scenario/FIXED/M01F/M01F.SCN",
    .default_sprite = "ucfcnst0.spr",
    .subsystems     = RTS_SUBSYSTEM_FILESYSTEM | RTS_SUBSYSTEM_GRAPHICS |
                      RTS_SUBSYSTEM_PALETTES   | RTS_SUBSYSTEM_TILESETS |
                      RTS_SUBSYSTEM_MAPS       | RTS_SUBSYSTEM_SPRITES  |
                      RTS_SUBSYSTEM_WORLD      | RTS_SUBSYSTEM_PLAYERS  |
                      RTS_SUBSYSTEM_ORDERS     | RTS_SUBSYSTEM_SIMULATION |
                      RTS_SUBSYSTEM_RENDERER   | RTS_SUBSYSTEM_UI,
    .cell_w            = 24,
    .cell_h            = 24,
    .game_info         = &DARK_REIGN_GAME_INFO,
    .actor_types       = DARK_REIGN_ACTOR_TYPES,
    .actor_type_count  = (int)(sizeof(DARK_REIGN_ACTOR_TYPES) / sizeof(DARK_REIGN_ACTOR_TYPES[0])),
    .debug_enemy_type_id = DR_ACTOR_FG_CONSTRUCTION_CREW,
    .capabilities        = {
        .map_format = MAP_FORMAT_DARK_REIGN_SCN,
        .capabilities = PLUGIN_CAP_RUNTIME_SPRITES | PLUGIN_CAP_SOFTWARE_RENDERER_SAFE,
        .data_capability = "dark-reign:data/REIGN/dark",
        .graphics_capability = "dark-reign:spr-ftg",
    },
    .ui                  = &DARK_REIGN_UI,
    .definition          = {
        .id             = "dark-reign",
        .name           = "Dark Reign",
        .version        = "0.1",
        .default_root   = "data/REIGN/dark",
        .default_map    = "scenario/FIXED/M01F/M01F.SCN",
        .default_sprite = "ucfcnst0.spr",
        .subsystems     = RTS_SUBSYSTEM_FILESYSTEM | RTS_SUBSYSTEM_GRAPHICS |
                          RTS_SUBSYSTEM_PALETTES   | RTS_SUBSYSTEM_TILESETS |
                          RTS_SUBSYSTEM_MAPS       | RTS_SUBSYSTEM_SPRITES  |
                          RTS_SUBSYSTEM_WORLD      | RTS_SUBSYSTEM_PLAYERS  |
                          RTS_SUBSYSTEM_ORDERS     | RTS_SUBSYSTEM_SIMULATION |
                          RTS_SUBSYSTEM_RENDERER   | RTS_SUBSYSTEM_UI,
        .cell_w            = 24,
        .cell_h            = 24,
        .game_info         = &DARK_REIGN_GAME_INFO,
        .actor_types       = DARK_REIGN_ACTOR_TYPES,
        .actor_type_count  = (int)(sizeof(DARK_REIGN_ACTOR_TYPES) / sizeof(DARK_REIGN_ACTOR_TYPES[0])),
        .debug_enemy_type_id = DR_ACTOR_FG_CONSTRUCTION_CREW,
        .capabilities        = {
            .map_format = MAP_FORMAT_DARK_REIGN_SCN,
            .capabilities = PLUGIN_CAP_RUNTIME_SPRITES | PLUGIN_CAP_SOFTWARE_RENDERER_SAFE,
            .data_capability = "dark-reign:data/REIGN/dark",
            .graphics_capability = "dark-reign:spr-ftg",
        },
        .ui                  = &DARK_REIGN_UI,
    },
    .loaders             = {
        .load_map            = load_dark_map,
        .load_assets         = dark_reign_plugin_load_assets,
        .load_initial_units  = load_dark_reign_initial_units,
        .load_runtime_sprites = dark_reign_load_runtime_sprites,
    },
    .load_map            = load_dark_map,
    .load_assets         = dark_reign_plugin_load_assets,
    .load_initial_units  = load_dark_reign_initial_units,
    .load_runtime_sprites = dark_reign_load_runtime_sprites,
};

const Plugin *open_rts_plugin_entry(void) { return &DARK_REIGN_PLUGIN; }

/* keep old name for any static-link usage */
const Plugin *open_rts_dark_reign_plugin(void) { return &DARK_REIGN_PLUGIN; }
