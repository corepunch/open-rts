#include "plugin.h"
#include "sl_types.h"

bool sl_load_map(const char *map_path, GameMap *out);
bool sl_load_assets(SDL_Renderer *renderer, const char *data_root, const GameMap *map,
                    const char *sprite_name, Tileset *tileset, SpriteSheet *unit_sprite);
int  sl_load_initial_units(const char *map_path, Unit *units, int max_units);

/* Unit types defined in 7th Legion based on sprites present in data/7LEGION/GFX/ */
static const ActorType SL_ACTOR_TYPES[] = {
    {
        .id          = 1,
        .name        = "Trooper",
        .sprite_name = "GFX/TROOP1W.BIM",
        .traits      = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                       RTS_TRAIT_RENDERABLE | RTS_TRAIT_ATTACK,
        .speed       = 4.0f,
        .max_hp      = 100,
        .attack_range    = 5.0f,
        .attack_damage   = 15,
        .attack_cooldown_ms = 800,
        .attack_anim_ms  = 400,
        .death_anim_ms   = 600,
    },
    {
        .id          = 2,
        .name        = "Slave",
        .sprite_name = "GFX/SLAVEN1.BIM",
        .traits      = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                       RTS_TRAIT_RENDERABLE | RTS_TRAIT_HARVESTER,
        .speed       = 3.5f,
        .max_hp      = 60,
    },
    {
        .id          = 3,
        .name        = "Spider Mech",
        .sprite_name = "GFX/SPIDER.BIM",
        .traits      = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                       RTS_TRAIT_RENDERABLE | RTS_TRAIT_ATTACK,
        .speed       = 3.0f,
        .max_hp      = 300,
        .attack_range    = 7.0f,
        .attack_damage   = 35,
        .attack_cooldown_ms = 1200,
        .attack_anim_ms  = 500,
        .death_anim_ms   = 800,
    },
    {
        .id          = 4,
        .name        = "Tank",
        .sprite_name = "GFX/TANKBASE.BIM",
        .traits      = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                       RTS_TRAIT_RENDERABLE | RTS_TRAIT_ATTACK,
        .speed       = 4.5f,
        .max_hp      = 500,
        .attack_range    = 8.0f,
        .attack_damage   = 50,
        .attack_cooldown_ms = 1500,
        .attack_anim_ms  = 600,
        .death_anim_ms   = 1000,
    },
    {
        .id          = 5,
        .name        = "Rock Mech",
        .sprite_name = "GFX/ROCKMECH.BIM",
        .traits      = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                       RTS_TRAIT_RENDERABLE | RTS_TRAIT_ATTACK,
        .speed       = 2.5f,
        .max_hp      = 800,
        .attack_range    = 6.0f,
        .attack_damage   = 70,
        .attack_cooldown_ms = 2000,
        .attack_anim_ms  = 700,
        .death_anim_ms   = 1200,
    },
    {
        .id          = 6,
        .name        = "Truck",
        .sprite_name = "GFX/TRUCK.BIM",
        .traits      = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                       RTS_TRAIT_RENDERABLE | RTS_TRAIT_HARVESTER,
        .speed       = 5.0f,
        .max_hp      = 200,
    },
};

static const GameInfo SL_GAME_INFO = {
    .direction_mode   = RTS_DIRECTION_DARK_REIGN_8,
    .selection_marker = { .style = SELECTION_STYLE_CIRCLE, .sprite = -1 },
};

static const Plugin SL_PLUGIN = {
    .id             = "7legion",
    .name           = "7th Legion",
    .version        = "0.1",
    .default_root   = "data/7LEGION",
    .default_map    = "",
    .default_sprite = "GFX/TROOP1W.BIM",
    .subsystems     = RTS_SUBSYSTEM_FILESYSTEM | RTS_SUBSYSTEM_GRAPHICS |
                      RTS_SUBSYSTEM_PALETTES   | RTS_SUBSYSTEM_TILESETS |
                      RTS_SUBSYSTEM_MAPS       | RTS_SUBSYSTEM_SPRITES  |
                      RTS_SUBSYSTEM_WORLD      | RTS_SUBSYSTEM_PLAYERS  |
                      RTS_SUBSYSTEM_ORDERS     | RTS_SUBSYSTEM_SIMULATION |
                      RTS_SUBSYSTEM_RENDERER   | RTS_SUBSYSTEM_UI,
    .cell_w         = SL_TILE_W,
    .cell_h         = SL_TILE_H,
    .game_info      = &SL_GAME_INFO,
    .actor_types    = SL_ACTOR_TYPES,
    .actor_type_count = (int)(sizeof(SL_ACTOR_TYPES) / sizeof(SL_ACTOR_TYPES[0])),
    .debug_enemy_type_id = 1,
    .capabilities   = {
        .map_format   = MAP_FORMAT_UNSPECIFIED,
        .capabilities = PLUGIN_CAP_SOFTWARE_RENDERER_SAFE,
        .data_capability     = "7legion:data/7LEGION",
        .graphics_capability = "7legion:bim-col",
    },
    .definition = {
        .id             = "7legion",
        .name           = "7th Legion",
        .version        = "0.1",
        .default_root   = "data/7LEGION",
        .default_map    = "",
        .default_sprite = "GFX/TROOP1W.BIM",
        .subsystems     = RTS_SUBSYSTEM_FILESYSTEM | RTS_SUBSYSTEM_GRAPHICS |
                          RTS_SUBSYSTEM_PALETTES   | RTS_SUBSYSTEM_TILESETS |
                          RTS_SUBSYSTEM_MAPS       | RTS_SUBSYSTEM_SPRITES  |
                          RTS_SUBSYSTEM_WORLD      | RTS_SUBSYSTEM_PLAYERS  |
                          RTS_SUBSYSTEM_ORDERS     | RTS_SUBSYSTEM_SIMULATION |
                          RTS_SUBSYSTEM_RENDERER   | RTS_SUBSYSTEM_UI,
        .cell_w         = SL_TILE_W,
        .cell_h         = SL_TILE_H,
        .game_info      = &SL_GAME_INFO,
        .actor_types    = SL_ACTOR_TYPES,
        .actor_type_count = (int)(sizeof(SL_ACTOR_TYPES) / sizeof(SL_ACTOR_TYPES[0])),
        .debug_enemy_type_id = 1,
        .capabilities   = {
            .map_format   = MAP_FORMAT_UNSPECIFIED,
            .capabilities = PLUGIN_CAP_SOFTWARE_RENDERER_SAFE,
            .data_capability     = "7legion:data/7LEGION",
            .graphics_capability = "7legion:bim-col",
        },
    },
    .loaders = {
        .load_map           = sl_load_map,
        .load_assets        = sl_load_assets,
        .load_initial_units = sl_load_initial_units,
    },
    .load_map           = sl_load_map,
    .load_assets        = sl_load_assets,
    .load_initial_units = sl_load_initial_units,
};

const Plugin *open_rts_plugin_entry(void) { return &SL_PLUGIN; }
