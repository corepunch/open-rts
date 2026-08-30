#include "kknd.h"

static const ActorType KKND_ACTOR_TYPES[] = {
    {
        .id = 1,
        .name = "Survivor Infantry",
        .sprite_name = "LEVELS/640/SPRITES.LVL|Infantry.mobd",
        .traits = T_SELECTABLE | T_MOBILE |
                  T_RENDERABLE | T_ATTACK,
        .speed = 4.0f,
        .max_hp = 100,
        .attack_range = 4.0f,
        .attack_damage = 10,
        .attack_cooldown_ms = 650,
        .attack_anim_ms = 450,
    },
};

static const Plugin KKND_PLUGIN = {
    .id = "kknd",
    .name = "KKnD",
    .version = "0.1",
    .default_root = "data/KKND",
    .default_map = "LEVELS/640/SURV_01.LVL",
    .default_sprite = "LEVELS/640/SPRITES.LVL|Infantry.mobd",
    .subsystems = RTS_SUBSYSTEM_FILESYSTEM | RTS_SUBSYSTEM_GRAPHICS |
                  RTS_SUBSYSTEM_PALETTES | RTS_SUBSYSTEM_TILESETS |
                  RTS_SUBSYSTEM_MAPS | RTS_SUBSYSTEM_SPRITES |
                  RTS_SUBSYSTEM_WORLD | RTS_SUBSYSTEM_PLAYERS |
                  RTS_SUBSYSTEM_ORDERS | RTS_SUBSYSTEM_SIMULATION |
                  RTS_SUBSYSTEM_RENDERER,
    .cell_w = 32,
    .cell_h = 32,
    .actor_types = KKND_ACTOR_TYPES,
    .actor_type_count = (int)(sizeof(KKND_ACTOR_TYPES) / sizeof(KKND_ACTOR_TYPES[0])),
    .debug_enemy_type_id = 1,
    .capabilities = {
        .map_format = MAP_FORMAT_KKND_LVL,
        .capabilities = PLUGIN_CAP_SOFTWARE_RENDERER_SAFE,
        .data_capability = "kknd:data/KKND",
        .graphics_capability = "kknd:lvl-mapd-mobd",
    },
    .definition = {
        .id = "kknd",
        .name = "KKnD",
        .version = "0.1",
        .default_root = "data/KKND",
        .default_map = "LEVELS/640/SURV_01.LVL",
        .default_sprite = "LEVELS/640/SPRITES.LVL|Infantry.mobd",
        .subsystems = RTS_SUBSYSTEM_FILESYSTEM | RTS_SUBSYSTEM_GRAPHICS |
                      RTS_SUBSYSTEM_PALETTES | RTS_SUBSYSTEM_TILESETS |
                      RTS_SUBSYSTEM_MAPS | RTS_SUBSYSTEM_SPRITES |
                      RTS_SUBSYSTEM_WORLD | RTS_SUBSYSTEM_PLAYERS |
                      RTS_SUBSYSTEM_ORDERS | RTS_SUBSYSTEM_SIMULATION |
                      RTS_SUBSYSTEM_RENDERER,
        .cell_w = 32,
        .cell_h = 32,
        .actor_types = KKND_ACTOR_TYPES,
        .actor_type_count = (int)(sizeof(KKND_ACTOR_TYPES) / sizeof(KKND_ACTOR_TYPES[0])),
        .debug_enemy_type_id = 1,
        .capabilities = {
            .map_format = MAP_FORMAT_KKND_LVL,
            .capabilities = PLUGIN_CAP_SOFTWARE_RENDERER_SAFE,
            .data_capability = "kknd:data/KKND",
            .graphics_capability = "kknd:lvl-mapd-mobd",
        },
    },
    .loaders = {
        .load_map = load_kknd_map,
        .load_assets = kknd_load_assets,
    },
    .load_map = load_kknd_map,
    .load_assets = kknd_load_assets,
};

const Plugin *open_rts_plugin_entry(void) { return &KKND_PLUGIN; }
