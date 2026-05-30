#include "plugin.h"
#include "info.h"

bool load_dark_colony_map(const char *map_path, GameMap *out);
bool dark_colony_plugin_load_assets(SDL_Renderer *renderer, const char *data_root,
                                    const GameMap *map, const char *sprite_name,
                                    Tileset *tileset, SpriteSheet *unit_sprite);
int load_dark_colony_initial_units(const char *map_path, Unit *units, int max_units);
bool load_dark_colony_unit_sprites(SDL_Renderer *renderer, const char *data_root,
                                   const Unit *units, int unit_count, SpriteCache *cache);

enum {
    DC_ACTOR_TROOPER  = 1,
    DC_ACTOR_GREY     = 2,
    DC_ACTOR_EXPLOITER = 3,
};

void A_DC_MuzzleFlash(RtsStateContext *ctx, Unit *unit) {
    if (!ctx || !unit) return;
    float vx = 0.0f, vy = 0.0f;
    rts_direction_vector_from_code(ctx->game_info, unit->facing_code, &vx, &vy);
    rts_spawn_state_effect(ctx, S_DC_MUZA1,
                           unit->gx + vx * 0.42f,
                           unit->gy + vy * 0.42f,
                           unit->facing_code);
}

void A_DC_Attack(RtsStateContext *ctx, Unit *unit) {
    rts_unit_fire_attack(ctx, unit);
}

void A_DC_Fall(RtsStateContext *ctx, Unit *unit) {
    (void)ctx;
    if (!unit) return;
    unit->selected = false;
    unit->traits &= ~(RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE | RTS_TRAIT_ATTACK);
    unit->path_len = 0;
    unit->path_index = 0;
    unit->attack_target = -1;
    unit->attack_cooldown_left_ms = 0;
    unit->attack_anim_left_ms = 0;
    unit->death_started = true;
}

void A_DC_Corpse(RtsStateContext *ctx, Unit *unit) {
    if (!unit) return;
    rts_unit_add_corpse_decoration(ctx, unit);
    unit->remove = true;
}

static const RtsActorType DARK_COLONY_ACTOR_TYPES[] = {
    {
        .id = DC_ACTOR_TROOPER,
        .name = "Trooper",
        .sprite_name = "SPRITES/TRSC.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                  RTS_TRAIT_RENDERABLE | RTS_TRAIT_ATTACK,
        .speed = 5.5f,
        .max_hp = 800,
        .attack_range = 4.0f,
        .attack_damage = 100,
        .attack_cooldown_ms = 500,
        .attack_anim_ms = 210,
    },
    {
        .id = DC_ACTOR_GREY,
        .name = "Grey",
        .sprite_name = "SPRITES/GRAY.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                  RTS_TRAIT_RENDERABLE | RTS_TRAIT_ATTACK,
        .speed = 5.5f,
        .max_hp = 800,
        .attack_range = 4.0f,
        .attack_damage = 100,
        .attack_cooldown_ms = 500,
        .attack_anim_ms = 210,
    },
    {
        .id = DC_ACTOR_EXPLOITER,
        .name = "Exploiter",
        .sprite_name = "SPRITES/EXPL.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE | RTS_TRAIT_RENDERABLE,
        .speed = 5.5f,
        .max_hp = 800,
    },
};

static bool dark_colony_load_runtime_sprites(SDL_Renderer *renderer, const char *data_root,
                                             const GameMap *map, const Unit *units, int unit_count,
                                             SpriteCache *cache) {
    (void)map;
    return load_dark_colony_unit_sprites(renderer, data_root, units, unit_count, cache);
}

static const RtsPlugin DARK_COLONY_PLUGIN = {
    .id             = "dark-colony",
    .name           = "Dark Colony",
    .version        = "0.1",
    .default_root   = "data/DCOLONY",
    .default_map    = "SCENARIO/MPLAYER/D2PLAY01.MAP",
    .default_sprite = "SPRITES/TROOPER1.SPR",
    .subsystems     = RTS_SUBSYSTEM_FILESYSTEM | RTS_SUBSYSTEM_GRAPHICS |
                      RTS_SUBSYSTEM_PALETTES   | RTS_SUBSYSTEM_TILESETS |
                      RTS_SUBSYSTEM_MAPS       | RTS_SUBSYSTEM_SPRITES  |
                      RTS_SUBSYSTEM_WORLD      | RTS_SUBSYSTEM_PLAYERS  |
                      RTS_SUBSYSTEM_ORDERS     | RTS_SUBSYSTEM_SIMULATION |
                      RTS_SUBSYSTEM_RENDERER   | RTS_SUBSYSTEM_UI,
    .cell_w            = 32,
    .cell_h            = 32,
    .game_info         = &dark_colony_game_info,
    .actor_types       = DARK_COLONY_ACTOR_TYPES,
    .actor_type_count  = (int)(sizeof(DARK_COLONY_ACTOR_TYPES) / sizeof(DARK_COLONY_ACTOR_TYPES[0])),
    .debug_enemy_type_id = DC_ACTOR_GREY,
    .load_map            = load_dark_colony_map,
    .load_assets         = dark_colony_plugin_load_assets,
    .load_initial_units  = load_dark_colony_initial_units,
    .load_runtime_sprites = dark_colony_load_runtime_sprites,
};

const RtsPlugin *open_rts_plugin_entry(void) { return &DARK_COLONY_PLUGIN; }

/* keep old name for any static-link usage */
const RtsPlugin *open_rts_dark_colony_plugin(void) { return &DARK_COLONY_PLUGIN; }
