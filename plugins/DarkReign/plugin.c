#include "plugin.h"

bool load_dark_map(const char *map_path, GameMap *out);
bool dark_reign_plugin_load_assets(SDL_Renderer *renderer, const char *data_root,
                                   const GameMap *map, const char *sprite_name,
                                   Tileset *tileset, SpriteSheet *unit_sprite);
int load_dark_reign_initial_units(const char *map_path, Unit *units, int max_units);
bool load_dark_reign_decoration_sprites(SDL_Renderer *renderer, const char *data_root,
                                        const GameMap *map, const Unit *units, int unit_count,
                                        SpriteCache *cache);

enum {
    DR_ACTOR_CONSTRUCTION_RIG = 1,
};

static const RtsActorType DARK_REIGN_ACTOR_TYPES[] = {
    {
        .id = DR_ACTOR_CONSTRUCTION_RIG,
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
};

static bool dark_reign_load_runtime_sprites(SDL_Renderer *renderer, const char *data_root,
                                            const GameMap *map, const Unit *units, int unit_count,
                                            SpriteCache *cache) {
    return load_dark_reign_decoration_sprites(renderer, data_root, map, units, unit_count, cache);
}

const RtsPlugin *open_rts_dark_reign_plugin(void) {
    static const RtsPlugin plugin = {
        .id = "dark-reign",
        .name = "Dark Reign",
        .version = "0.1",
        .default_root = "data/REIGN/dark",
        .default_map = "scenario/MULTI/2NIC/2NIC.MAP",
        .default_sprite = "ucfcnst0.spr",
        .subsystems = RTS_SUBSYSTEM_FILESYSTEM |
                      RTS_SUBSYSTEM_GRAPHICS |
                      RTS_SUBSYSTEM_PALETTES |
                      RTS_SUBSYSTEM_TILESETS |
                      RTS_SUBSYSTEM_MAPS |
                      RTS_SUBSYSTEM_SPRITES |
                      RTS_SUBSYSTEM_WORLD |
                      RTS_SUBSYSTEM_PLAYERS |
                      RTS_SUBSYSTEM_ORDERS |
                      RTS_SUBSYSTEM_SIMULATION |
                      RTS_SUBSYSTEM_RENDERER |
                      RTS_SUBSYSTEM_UI,
        .cell_w = 24,
        .cell_h = 24,
        .actor_types = DARK_REIGN_ACTOR_TYPES,
        .actor_type_count = (int)(sizeof(DARK_REIGN_ACTOR_TYPES) / sizeof(DARK_REIGN_ACTOR_TYPES[0])),
        .debug_enemy_type_id = DR_ACTOR_CONSTRUCTION_RIG,
        .load_map = load_dark_map,
        .load_assets = dark_reign_plugin_load_assets,
        .load_initial_units = load_dark_reign_initial_units,
        .load_runtime_sprites = dark_reign_load_runtime_sprites,
    };
    return &plugin;
}
