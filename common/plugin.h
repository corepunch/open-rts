#ifndef OPEN_RTS_PLUGIN_H
#define OPEN_RTS_PLUGIN_H

#include "engine.h"
#include "ui_definition.h"

typedef enum {
    RTS_SUBSYSTEM_FILESYSTEM = 1u << 0,
    RTS_SUBSYSTEM_GRAPHICS = 1u << 1,
    RTS_SUBSYSTEM_PALETTES = 1u << 2,
    RTS_SUBSYSTEM_TILESETS = 1u << 3,
    RTS_SUBSYSTEM_MAPS = 1u << 4,
    RTS_SUBSYSTEM_SPRITES = 1u << 5,
    RTS_SUBSYSTEM_WORLD = 1u << 6,
    RTS_SUBSYSTEM_PLAYERS = 1u << 7,
    RTS_SUBSYSTEM_ORDERS = 1u << 8,
    RTS_SUBSYSTEM_SIMULATION = 1u << 9,
    RTS_SUBSYSTEM_RENDERER = 1u << 10,
    RTS_SUBSYSTEM_UI = 1u << 11,
    RTS_SUBSYSTEM_AUDIO = 1u << 12,
    RTS_SUBSYSTEM_SCRIPTING = 1u << 13,
    RTS_SUBSYSTEM_NETWORKING = 1u << 14,
} Subsystem;

typedef enum {
    MAP_FORMAT_UNSPECIFIED = 0,
    MAP_FORMAT_DARK_REIGN_SCN,
    MAP_FORMAT_DARK_COLONY_MAP_MTG_OVH,
    MAP_FORMAT_KKND_LVL,
} MapFormat;

typedef enum {
    PLUGIN_CAP_STATIC_METADATA = 1u << 0,
    PLUGIN_CAP_RUNTIME_SPRITES = 1u << 1,
    PLUGIN_CAP_MISSION_SCRIPT = 1u << 2,
    PLUGIN_CAP_SOFTWARE_RENDERER_SAFE = 1u << 3,
} PluginCapability;

typedef struct {
    MapFormat map_format;
    uint32_t capabilities;
    const char *data_capability;
    const char *graphics_capability;
} PluginCapabilities;

typedef struct {
    const char *id;
    const char *name;
    const char *version;
    const char *default_root;
    const char *default_map;
    const char *default_sprite;
    uint32_t subsystems;
    int cell_w;
    int cell_h;
    const GameInfo *game_info;
    const ActorType *actor_types;
    int actor_type_count;
    uint16_t debug_enemy_type_id;
    PluginCapabilities capabilities;
    const GameUiDefinition *ui;
} GameDefinition;

typedef struct {
    bool (*load_map)(const char *map_path, GameMap *out);
    bool (*load_assets)(SDL_Renderer *renderer, const char *data_root, const GameMap *map,
                        const char *sprite_name, Tileset *tileset, SpriteSheet *unit_sprite);
    int (*load_initial_units)(const char *map_path, Unit *units, int max_units);
    bool (*load_runtime_sprites)(SDL_Renderer *renderer, const char *data_root,
                                 const GameMap *map, const Unit *units, int unit_count,
                                 SpriteCache *cache);
    bool (*load_font)(SDL_Renderer *renderer, const char *data_root, BitmapFont *font);
    void *(*load_mission)(const char *map_path);
    void (*update_mission)(void *mission, GameMap *map, Unit *units, int *unit_count,
                           VisualEffect *effects, int max_effects,
                           const GameInfo *game_info, HudText *hud, float dt);
    void (*destroy_mission)(void *mission);
} GameLoaders;

typedef struct Plugin {
    const char *id;
    const char *name;
    const char *version;
    const char *default_root;
    const char *default_map;
    const char *default_sprite;
    uint32_t subsystems;
    int cell_w;
    int cell_h;
    const GameInfo *game_info;
    const ActorType *actor_types;
    int actor_type_count;
    uint16_t debug_enemy_type_id;
    PluginCapabilities capabilities;
    const GameUiDefinition *ui;
    GameDefinition definition;
    GameLoaders loaders;
    bool (*load_map)(const char *map_path, GameMap *out);
    bool (*load_assets)(SDL_Renderer *renderer, const char *data_root, const GameMap *map,
                        const char *sprite_name, Tileset *tileset, SpriteSheet *unit_sprite);
    int (*load_initial_units)(const char *map_path, Unit *units, int max_units);
    bool (*load_runtime_sprites)(SDL_Renderer *renderer, const char *data_root,
                                 const GameMap *map, const Unit *units, int unit_count,
                                 SpriteCache *cache);
    bool (*load_font)(SDL_Renderer *renderer, const char *data_root, BitmapFont *font);
    void *(*load_mission)(const char *map_path);
    void (*update_mission)(void *mission, GameMap *map, Unit *units, int *unit_count,
                           VisualEffect *effects, int max_effects,
                           const GameInfo *game_info, HudText *hud, float dt);
    void (*destroy_mission)(void *mission);
} Plugin;

typedef const Plugin *(*plugin_entry_fn)(void);

int plugin_count(void);
const Plugin *plugin_at(int index);
const Plugin *find_plugin(const char *id);
bool plugin_load(const char *so_path);
const GameDefinition *plugin_definition(const Plugin *plugin);
const GameLoaders *plugin_loaders(const Plugin *plugin);

#endif
