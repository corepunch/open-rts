#include "plugin.h"

bool load_dark_colony_map(const char *map_path, GameMap *out);
bool dark_colony_plugin_load_assets(SDL_Renderer *renderer, const char *data_root,
                                    const GameMap *map, const char *sprite_name,
                                    Tileset *tileset, SpriteSheet *unit_sprite);
int load_dark_colony_initial_units(const char *map_path, Unit *units, int max_units);
bool load_dark_colony_unit_sprites(SDL_Renderer *renderer, const char *data_root,
                                   const Unit *units, int unit_count, SpriteCache *cache);

enum {
    DC_ACTOR_TROOPER = 1,
    DC_ACTOR_GREY = 2,
    DC_ACTOR_EXPLOITER = 3,
};

static const RtsActorType DARK_COLONY_ACTOR_TYPES[] = {
    {
        .id = DC_ACTOR_TROOPER,
        .name = "Trooper",
        .sprite_name = "SPRITES/TRSC.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                  RTS_TRAIT_RENDERABLE | RTS_TRAIT_ATTACK,
        .speed = 5.5f,
        .max_hp = 100,
    },
    {
        .id = DC_ACTOR_GREY,
        .name = "Grey",
        .sprite_name = "SPRITES/GRAY.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                  RTS_TRAIT_RENDERABLE | RTS_TRAIT_ATTACK,
        .speed = 5.5f,
        .max_hp = 100,
    },
    {
        .id = DC_ACTOR_EXPLOITER,
        .name = "Exploiter",
        .sprite_name = "SPRITES/EXPL.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                  RTS_TRAIT_RENDERABLE | RTS_TRAIT_ATTACK,
        .speed = 5.5f,
        .max_hp = 120,
    },
};

static bool dark_colony_load_runtime_sprites(SDL_Renderer *renderer, const char *data_root,
                                             const GameMap *map, const Unit *units, int unit_count,
                                             SpriteCache *cache) {
    (void)map;
    return load_dark_colony_unit_sprites(renderer, data_root, units, unit_count, cache);
}

const RtsPlugin *open_rts_dark_colony_plugin(void) {
    static const RtsPlugin plugin = {
        .id = "dark-colony",
        .name = "Dark Colony",
        .version = "0.1",
        .default_root = "data/DCOLONY",
        .default_map = "SCENARIO/MPLAYER/D2PLAY01.MAP",
        .default_sprite = "SPRITES/TROOPER1.SPR",
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
        .cell_w = 32,
        .cell_h = 32,
        .actor_types = DARK_COLONY_ACTOR_TYPES,
        .actor_type_count = (int)(sizeof(DARK_COLONY_ACTOR_TYPES) / sizeof(DARK_COLONY_ACTOR_TYPES[0])),
        .debug_enemy_type_id = DC_ACTOR_GREY,
        .load_map = load_dark_colony_map,
        .load_assets = dark_colony_plugin_load_assets,
        .load_initial_units = load_dark_colony_initial_units,
        .load_runtime_sprites = dark_colony_load_runtime_sprites,
    };
    return &plugin;
}
