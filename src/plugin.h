#ifndef OPEN_RTS_PLUGIN_H
#define OPEN_RTS_PLUGIN_H

#include "engine.h"

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
} RtsSubsystem;

typedef struct RtsPlugin {
    const char *id;
    const char *name;
    const char *version;
    const char *default_root;
    const char *default_map;
    const char *default_sprite;
    uint32_t subsystems;
    int cell_w;
    int cell_h;
    const RtsActorType *actor_types;
    int actor_type_count;
    uint16_t debug_enemy_type_id;
    bool (*load_map)(const char *map_path, GameMap *out);
    bool (*load_assets)(SDL_Renderer *renderer, const char *data_root, const GameMap *map,
                        const char *sprite_name, Tileset *tileset, SpriteSheet *unit_sprite);
    int (*load_initial_units)(const char *map_path, Unit *units, int max_units);
    bool (*load_runtime_sprites)(SDL_Renderer *renderer, const char *data_root,
                                 const GameMap *map, const Unit *units, int unit_count,
                                 SpriteCache *cache);
} RtsPlugin;

typedef const RtsPlugin *(*rts_plugin_entry_fn)(void);

int rts_plugin_count(void);
const RtsPlugin *rts_plugin_at(int index);
const RtsPlugin *rts_find_plugin(const char *id);
bool rts_plugin_load(const char *so_path);

#endif
