#include "plugin.h"
#include "info.h"
#include "dc_types.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool load_dark_colony_map(const char *map_path, GameMap *out);
bool dark_colony_plugin_load_assets(SDL_Renderer *renderer, const char *data_root,
                                    const GameMap *map, const char *sprite_name,
                                    Tileset *tileset, SpriteSheet *unit_sprite);
int load_dark_colony_initial_units(const char *map_path, Unit *units, int max_units);
bool load_dark_colony_unit_sprites(SDL_Renderer *renderer, const char *data_root,
                                   const GameMap *map, const Unit *units, int unit_count,
                                   SpriteCache *cache);
bool load_dark_colony_sprite(SDL_Renderer *renderer, const char *path, SpriteSheet *out,
                             uint32_t palette_out[256]);

void A_DC_MuzzleFlash(StateContext *ctx, Unit *unit) {
    if (!ctx || !unit) return;
    int muzzle_state = 0;
    if (ctx->game_info && unit->type_id > 0 &&
        unit->type_id < ctx->game_info->mobj_type_count) {
        muzzle_state = ctx->game_info->mobjinfo[unit->type_id].muzzleflash;
    }
    spawn_state_effect(ctx, muzzle_state, unit->gx, unit->gy, unit->facing_code);
}

void A_DC_Attack(StateContext *ctx, Unit *unit) {
    unit_fire_attack(ctx, unit);
}

void A_DC_TrooperAttackStart(StateContext *ctx, Unit *unit) {
    if (!ctx || !unit) return;
    set_unit_state(ctx, unit, (rand() & 1) ? S_DC_TRSC_ATKB1 : S_DC_TRSC_ATK1);
}

void A_DC_Fall(StateContext *ctx, Unit *unit) {
    (void)ctx;
    if (!unit) return;
    unit->selected = false;
    unit->traits &= ~(RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                      RTS_TRAIT_ATTACK | RTS_TRAIT_HARVESTER);
    unit->path_len = 0;
    unit->path_index = 0;
    unit->attack_target = -1;
    unit->harvest_target = -1;
    unit->harvest_timer_ms = 0;
    unit->attack_cooldown_left_ms = 0;
    unit->attack_anim_left_ms = 0;
    unit->death_started = true;
}

static int reaper_death_effect_state_for_facing(int facing_code) {
    int code = facing_code & 15;
    for (int distance = 0; distance <= 8; ++distance) {
        for (int sign = -1; sign <= 1; sign += 2) {
            if (distance == 0 && sign > 0) continue;
            int candidate = (code + sign * distance) & 15;
            int suffix = (16 - candidate) & 15;
            if (suffix == 14) return S_DC_REAP_DIEA14_FX1;
            if (suffix == 6) return S_DC_REAP_DIEA6_FX1;
            if (suffix == 10 || suffix == 2) return S_NULL;
        }
    }
    return S_NULL;
}

void A_DC_ReaperDeath(StateContext *ctx, Unit *unit) {
    if (ctx && unit) {
        int fx_state = reaper_death_effect_state_for_facing(unit->facing_code);
        if (fx_state != S_NULL) {
            spawn_state_effect(ctx, fx_state, unit->gx, unit->gy, unit->facing_code);
        }
    }
    A_DC_Fall(ctx, unit);
}

void A_DC_Corpse(StateContext *ctx, Unit *unit) {
    if (!unit) return;
    unit_add_corpse_decoration(ctx, unit);
    unit->remove = true;
}

static const ActorType DARK_COLONY_ACTOR_TYPES[] = {
    {
        .id = MT_DC_TROOPER,
        .name = "Trooper",
        .sprite_name = "SPRITES/TRSC.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                  RTS_TRAIT_RENDERABLE | RTS_TRAIT_ATTACK,
        .speed = 5.0f,
        .max_hp = 800,
        .attack_range = 4.0f,
        .attack_damage = 100,
        .attack_cooldown_ms = 500,
        .attack_anim_ms = 210,
        .muzzle_flash_name = "SPRITES/MUZA.SPR",
        .muzzle_flash_ms = 120,
        .hit_effect_name = "SPRITES/BLOO.SPR",
    },
    {
        .id = MT_DC_GREY,
        .name = "Grey",
        .sprite_name = "SPRITES/GRAY.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                  RTS_TRAIT_RENDERABLE | RTS_TRAIT_ATTACK,
        .speed = 5.0f,
        .max_hp = 800,
        .attack_range = 4.0f,
        .attack_damage = 100,
        .attack_cooldown_ms = 500,
        .attack_anim_ms = 210,
        .muzzle_flash_name = "SPRITES/MUZA.SPR",
        .muzzle_flash_ms = 120,
        .hit_effect_name = "SPRITES/BLOO.SPR",
    },
    {
        .id = MT_DC_EXPLOITER,
        .name = "Exploiter",
        .sprite_name = "SPRITES/EXPL.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                  RTS_TRAIT_RENDERABLE | RTS_TRAIT_HARVESTER,
        .speed = 8.0f,
        .max_hp = 800,
        .harvest_state_id = S_DC_EXPL_DEPLOY1,
    },
    {
        .id = MT_DC_REAPER,
        .name = "Mech",
        .sprite_name = "SPRITES/REAP.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                  RTS_TRAIT_RENDERABLE | RTS_TRAIT_ATTACK,
        .speed = 6.0f,
        .max_hp = 800,
        .attack_range = 4.0f,
        .attack_damage = 100,
        .attack_cooldown_ms = 500,
        .attack_anim_ms = 210,
        .muzzle_flash_name = "SPRITES/BLAZ.SPR",
        .muzzle_flash_ms = 120,
        .hit_effect_name = "SPRITES/BLOO.SPR",
    },
    {
        .id = MT_DC_THUNDERBOLT,
        .name = "Thunderbolt",
        .sprite_name = "SPRITES/BARR.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE | RTS_TRAIT_RENDERABLE,
        .speed = 3.0f,
        .max_hp = 400,
    },
    {
        .id = MT_DC_CYBORG,
        .name = "Cyborg",
        .sprite_name = "SPRITES/SARG.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE | RTS_TRAIT_RENDERABLE,
        .speed = 9.0f,
        .max_hp = 800,
    },
    {
        .id = MT_DC_SCOUT,
        .name = "Scout",
        .sprite_name = "SPRITES/SCGM.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE | RTS_TRAIT_RENDERABLE,
        .speed = 9.4f,
        .max_hp = 800,
    },
    {
        .id = MT_DC_EXCOPOD,
        .name = "Exco Center",
        .sprite_name = "SPRITES/HUBU.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 4800,
    },
    {
        .id = MT_DC_BRRKPOD,
        .name = "Barracks",
        .sprite_name = "SPRITES/HUBU.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_ROBOPOD,
        .name = "Robot Factory",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_ROBOPOD2,
        .name = "Robot Factory II",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_DC_SCNCPOD,
        .name = "Science Pod",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_SCNCPOD2,
        .name = "Science Pod II",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_DC_RSCHPOD,
        .name = "Research Pod",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_DC_ALIEN_MINDHIVE,
        .name = "Mind Hive",
        .sprite_name = "SPRITES/ALIEN1.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 4800,
    },
    {
        .id = MT_DC_ALIEN_WARHIVE,
        .name = "Warrior Hive",
        .sprite_name = "SPRITES/ALIEN1.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_ALIEN_BRDRHIVE,
        .name = "Breeder Hive",
        .sprite_name = "SPRITES/ALIEN1.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_ALIEN_BRDRHIVE2,
        .name = "Breeder Hive II",
        .sprite_name = "SPRITES/ALIEN1.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_DC_ALIEN_MINDHIVE2,
        .name = "Mind Hive II",
        .sprite_name = "SPRITES/ALIEN1.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_ALIEN_MINDHIVE3,
        .name = "Mind Hive III",
        .sprite_name = "SPRITES/ALIEN1.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_DC_ALIEN_RSCHIVE,
        .name = "Research Hive",
        .sprite_name = "SPRITES/ALIEN1.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_DC_COMMS_DISH,
        .name = "Communication Dish",
        .sprite_name = "SPRITES/DISH.SPR",
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_RENDERABLE,
        .max_hp = 1200,
    },
    {
        .id = MT_DC_CITY_TOWER,
        .name = "City Tower",
        .sprite_name = "SPRITES/TOWR.SPR",
        .traits = RTS_TRAIT_RENDERABLE,
        .max_hp = 1600,
    },
};

static const ActorType *dark_colony_actor_type_by_id(uint16_t type_id) {
    for (int i = 0; i < (int)(sizeof(DARK_COLONY_ACTOR_TYPES) / sizeof(DARK_COLONY_ACTOR_TYPES[0])); ++i) {
        if (DARK_COLONY_ACTOR_TYPES[i].id == type_id) return &DARK_COLONY_ACTOR_TYPES[i];
    }
    return NULL;
}

static void dark_colony_apply_actor_type_defaults(Unit *unit, const ActorType *type) {
    if (!unit || !type) return;
    unit->type_id = type->id;
    unit->traits = type->traits;
    if (unit->speed <= 0.0f) unit->speed = type->speed;
    if (unit->max_hp <= 0) unit->max_hp = type->max_hp;
    if (unit->hp <= 0) unit->hp = unit->max_hp;
    if (unit->attack_range <= 0.0f) unit->attack_range = type->attack_range;
    if (unit->attack_damage <= 0) unit->attack_damage = type->attack_damage;
    if (unit->attack_cooldown_ms <= 0) unit->attack_cooldown_ms = type->attack_cooldown_ms;
    if (unit->attack_anim_ms <= 0) unit->attack_anim_ms = type->attack_anim_ms;
    if (unit->death_anim_ms <= 0) unit->death_anim_ms = type->death_anim_ms;
    if (unit->harvest_state_id <= 0) unit->harvest_state_id = type->harvest_state_id;
    if (unit->muzzle_flash_ms <= 0) unit->muzzle_flash_ms = type->muzzle_flash_ms;
    if (unit->render_intensity == 0) unit->render_intensity = 16;
    if (unit->attack_target <= 0) unit->attack_target = -1;
    if (unit->harvest_target == 0) unit->harvest_target = -1;
    if (unit->sprite_name[0] == '\0' && type->sprite_name)
        snprintf(unit->sprite_name, sizeof(unit->sprite_name), "%s", type->sprite_name);
    if (unit->shadow_name[0] == '\0' && type->shadow_name)
        snprintf(unit->shadow_name, sizeof(unit->shadow_name), "%s", type->shadow_name);
    if (unit->muzzle_flash_name[0] == '\0' && type->muzzle_flash_name)
        snprintf(unit->muzzle_flash_name, sizeof(unit->muzzle_flash_name), "%s", type->muzzle_flash_name);
    if (unit->hit_effect_name[0] == '\0' && type->hit_effect_name)
        snprintf(unit->hit_effect_name, sizeof(unit->hit_effect_name), "%s", type->hit_effect_name);
}

static void replace_extension(char *dst, size_t dst_size, const char *path, const char *ext) {
    snprintf(dst, dst_size, "%s", path);
    char *dot = strrchr(dst, '.');
    char *slash = strrchr(dst, '/');
    if (dot && (!slash || dot > slash)) {
        snprintf(dot, dst_size - (size_t)(dot - dst), "%s", ext);
    } else {
        strncat(dst, ext, dst_size - strlen(dst) - 1);
    }
}

static char *dark_colony_load_text(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fclose(fp);
    Blob blob;
    if (!load_blob(path, &blob)) return NULL;
    char *text = malloc(blob.size + 1);
    if (!text) {
        free_blob(&blob);
        return NULL;
    }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';
    free_blob(&blob);
    return text;
}

static void trim_copy(char *dst, size_t dst_size, const char *src) {
    while (*src && isspace((unsigned char)*src)) src++;
    size_t len = strlen(src);
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

typedef enum {
    DC_SCRIPT_CMD_NONE,
    DC_SCRIPT_CMD_MSG,
    DC_SCRIPT_CMD_REINFORCE,
    DC_SCRIPT_CMD_REINFORCE2,
    DC_SCRIPT_CMD_NEWTYPE,
} DarkColonyScriptCommandType;

enum {
    DC_SCRIPT_COUNTER_MS = 1000,
};

typedef struct {
    DarkColonyScriptCommandType type;
    int a[8];
} DarkColonyScriptCommand;

typedef struct {
    int id;
    bool trip;
    bool fired;
    int c_gt;
    bool requires_player_near;
    int trigger_x;
    int trigger_y;
    DarkColonyScriptCommand commands[32];
    int command_count;
} DarkColonyScriptBlock;

typedef struct {
    int id;
    char text[256];
} DarkColonyScriptMessage;

typedef struct {
    bool active;
    int spawn_ms;
    int team;
    int x;
    int y;
    int type;
    int index;
} DarkColonyPendingSpawn;

typedef struct {
    bool active;
    float center_gx;
    float center_gy;
    float radius;
    float angle;        /* radians, advances each frame */
    float speed;        /* radians per second */
    int duration_ms;
    int elapsed_ms;
    int effect_slot;    /* index into effects array, -1 if untracked */
} DarkColonyDropship;

typedef struct {
    DarkColonyScriptMessage messages[64];
    int message_count;
    DarkColonyScriptBlock blocks[64];
    int block_count;
    DarkColonyPendingSpawn pending_spawns[128];
    DarkColonyDropship dropships[8];
    int elapsed_ms;
} DarkColonyMission;

static const char *dark_colony_script_message(const DarkColonyMission *mission, int id) {
    if (!mission) return NULL;
    for (int i = 0; i < mission->message_count; ++i)
        if (mission->messages[i].id == id) return mission->messages[i].text;
    return NULL;
}

static uint16_t dark_colony_script_unit_type(int team, int type) {
    if (team != 0) {
        if (type == 0 || (type >= 69 && type <= 76)) return MT_DC_GREY;
        return MT_DC_GREY;
    }
    if (type == 0 || (type >= 69 && type <= 72)) return MT_DC_TROOPER;
    switch (type) {
        case 2: return MT_DC_REAPER;
        case 3: return MT_DC_THUNDERBOLT;
        case 4: return MT_DC_CYBORG;
        case 5: return MT_DC_SCOUT;
        case 6: return MT_DC_EXPLOITER;
        default: return MT_DC_TROOPER;
    }
}

static int dark_colony_script_y_to_map(const GameMap *map, int y) {
    if (!map || map->height <= 0) return y;
    int map_y = map->height - 1 - y;
    if (map_y < 0) map_y = 0;
    if (map_y >= map->height) map_y = map->height - 1;
    return map_y;
}

static bool dark_colony_player_near(const GameMap *map, const Unit *units,
                                    int unit_count, int gx, int gy) {
    int map_y = dark_colony_script_y_to_map(map, gy);
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].owner != 0 || units[i].remove || units[i].hp <= 0) continue;
        float dx = units[i].gx - ((float)gx + 0.5f);
        float dy = units[i].gy - ((float)map_y + 0.5f);
        if (dx * dx + dy * dy <= 16.0f) return true;
    }
    return false;
}

static int dark_colony_spawn_dropship_effect(VisualEffect *effects, int max_effects,
                                             float gx, float gy, int duration_ms) {
    for (int i = 0; i < max_effects; ++i) {
        if (effects[i].active) continue;
        VisualEffect *effect = &effects[i];
        memset(effect, 0, sizeof(*effect));
        effect->active = true;
        effect->gx = gx;
        effect->gy = gy;
        effect->duration_ms = duration_ms;
        effect->frame_ms = duration_ms + 1;
        effect->render_intensity = 16;
        effect->screen_offset_y = -75;
        snprintf(effect->sprite_name, sizeof(effect->sprite_name), "SPRITES/DROP.SPR");
        return i;
    }
    return -1;
}

static void dark_colony_spawn_drop_effect(DarkColonyMission *mission,
                                          VisualEffect *effects, int max_effects,
                                          int gx, int gy, int duration_ms) {
    if (!mission || !effects || max_effects <= 0) return;
    int actual_duration = duration_ms > 1400 ? duration_ms : 1400;
    float cx = (float)gx + 0.5f;
    float cy = (float)gy + 0.5f;

    /* Find a free dropship slot */
    DarkColonyDropship *ship = NULL;
    for (int i = 0; i < (int)(sizeof(mission->dropships) / sizeof(mission->dropships[0])); ++i) {
        if (!mission->dropships[i].active) { ship = &mission->dropships[i]; break; }
    }
    if (!ship) return;

    /* Start the dropship on the orbit at a random-ish angle offset by slot index */
    float start_angle = 0.0f;
    for (int i = 0; i < (int)(sizeof(mission->dropships) / sizeof(mission->dropships[0])); ++i) {
        if (&mission->dropships[i] == ship) { start_angle = (float)i * 1.05f; break; }
    }

    ship->active      = true;
    ship->center_gx   = cx;
    ship->center_gy   = cy;
    ship->radius      = 0.0f;
    ship->angle       = start_angle;
    ship->speed       = 0.0f;
    ship->duration_ms = actual_duration;
    ship->elapsed_ms  = 0;

    ship->effect_slot = dark_colony_spawn_dropship_effect(effects, max_effects,
                                                          cx, cy, actual_duration);

    if (getenv("OPEN_RTS_DEBUG_SCRIPT")) {
        fprintf(stderr, "Dark Colony dropship effect at %d,%d duration=%d\n",
                gx, gy, actual_duration);
    }
}

static void dark_colony_spawn_script_unit(const GameMap *map, Unit *units, int *unit_count, int team,
                                          int gx, int gy, int type, int index,
                                          const GameInfo *game_info) {
    if (!units || !unit_count || *unit_count >= MAX_UNITS) return;
    Unit *unit = &units[*unit_count];
    memset(unit, 0, sizeof(*unit));
    int offset_x = index % 2;
    int offset_y = index / 2;
    int spawn_x = gx + offset_x;
    int spawn_y = gy - offset_y;
    if (map) {
        if (spawn_x < 0) spawn_x = 0;
        if (spawn_x >= map->width) spawn_x = map->width - 1;
        if (spawn_y < 0) spawn_y = 0;
        if (spawn_y >= map->height) spawn_y = map->height - 1;
    }
    unit->gx = (float)spawn_x + 0.5f;
    unit->gy = (float)spawn_y + 0.5f;
    unit->owner = team == 0 ? 0 : 1;
    if (unit->owner == 0) {
        bool has_selected_player = false;
        for (int i = 0; i < *unit_count; ++i) {
            if (units[i].owner == 0 && units[i].selected) {
                has_selected_player = true;
                break;
            }
        }
        unit->selected = !has_selected_player;
    }
    unit->facing_code = unit->owner == 0 ? 6 : 14;
    uint16_t type_id = dark_colony_script_unit_type(team, type);
    const ActorType *actor = dark_colony_actor_type_by_id(type_id);
    dark_colony_apply_actor_type_defaults(unit, actor);
    apply_mobjinfo_defaults(game_info, unit);
    (*unit_count)++;
}

static void dark_colony_queue_pending_spawn(DarkColonyMission *mission, int spawn_ms,
                                            int team, int x, int y, int type, int index) {
    if (!mission) return;
    for (int i = 0; i < (int)(sizeof(mission->pending_spawns) / sizeof(mission->pending_spawns[0])); ++i) {
        DarkColonyPendingSpawn *spawn = &mission->pending_spawns[i];
        if (spawn->active) continue;
        memset(spawn, 0, sizeof(*spawn));
        spawn->active = true;
        spawn->spawn_ms = spawn_ms;
        spawn->team = team;
        spawn->x = x;
        spawn->y = y;
        spawn->type = type;
        spawn->index = index;
        if (getenv("OPEN_RTS_DEBUG_SCRIPT")) {
            fprintf(stderr, "Dark Colony dropship queued team=%d type=%d index=%d at %d,%d ms=%d\n",
                    team, type, index, x, y, spawn_ms);
        }
        return;
    }
}

static void dark_colony_update_pending_spawns(DarkColonyMission *mission, GameMap *map,
                                              Unit *units, int *unit_count,
                                              const GameInfo *game_info) {
    if (!mission || !units || !unit_count) return;
    for (int i = 0; i < (int)(sizeof(mission->pending_spawns) / sizeof(mission->pending_spawns[0])); ++i) {
        DarkColonyPendingSpawn *spawn = &mission->pending_spawns[i];
        if (!spawn->active || mission->elapsed_ms < spawn->spawn_ms) continue;
        if (getenv("OPEN_RTS_DEBUG_SCRIPT")) {
            fprintf(stderr, "Dark Colony dropship spawned team=%d type=%d index=%d at %d,%d ms=%d\n",
                    spawn->team, spawn->type, spawn->index, spawn->x, spawn->y, mission->elapsed_ms);
        }
        dark_colony_spawn_script_unit(map, units, unit_count, spawn->team, spawn->x, spawn->y,
                                      spawn->type, spawn->index, game_info);
        memset(spawn, 0, sizeof(*spawn));
    }
}

static void dark_colony_execute_script_block(DarkColonyMission *mission, DarkColonyScriptBlock *block,
                                             GameMap *map, Unit *units, int *unit_count,
                                             VisualEffect *effects, int max_effects,
                                             const GameInfo *game_info, HudText *hud) {
    if (!mission || !block) return;
    int drop_sequence = 0;
    for (int i = 0; i < block->command_count; ++i) {
        DarkColonyScriptCommand *cmd = &block->commands[i];
        if (cmd->type == DC_SCRIPT_CMD_MSG) {
            const char *message = dark_colony_script_message(mission, cmd->a[0]);
            if (message) hud_text_push(hud, message, 6500);
        } else if (cmd->type == DC_SCRIPT_CMD_REINFORCE ||
                   cmd->type == DC_SCRIPT_CMD_REINFORCE2) {
            int team = cmd->a[0], x = cmd->a[1], y = dark_colony_script_y_to_map(map, cmd->a[2]);
            int count = cmd->a[3] > 0 ? cmd->a[3] : 1;
            int type = cmd->a[4];
            if (cmd->type == DC_SCRIPT_CMD_REINFORCE && cmd->a[5]) {
                int drop_count = 0;
                for (int j = i; j < block->command_count; ++j) {
                    DarkColonyScriptCommand *drop_cmd = &block->commands[j];
                    if (drop_cmd->type != DC_SCRIPT_CMD_REINFORCE) break;
                    if (j != i && drop_cmd->a[5]) break;
                    drop_count += drop_cmd->a[3] > 0 ? drop_cmd->a[3] : 1;
                }
                int drop_duration = 420 + (drop_count > 0 ? drop_count - 1 : 0) * 280 + 700;
                dark_colony_spawn_drop_effect(mission, effects, max_effects, x, y, drop_duration);
                drop_sequence = 0;
            }
            for (int n = 0; n < count; ++n) {
                if (cmd->type == DC_SCRIPT_CMD_REINFORCE) {
                    int index = drop_sequence++;
                    int spawn_ms = mission->elapsed_ms + 420 + index * 280;
                    dark_colony_queue_pending_spawn(mission, spawn_ms, team, x, y, type, index);
                } else {
                    dark_colony_spawn_script_unit(map, units, unit_count, team, x, y, type, n, game_info);
                }
            }
        }
    }
    block->fired = true;
}

static void dark_colony_parse_messages(DarkColonyMission *mission, const char *path) {
    char *text = dark_colony_load_text(path);
    if (!text) return;
    DarkColonyScriptMessage *current = NULL;
    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char nl = *next; *next++ = '\0';
            if (nl == '\r' && *next == '\n') next++;
        }
        char token[256];
        trim_copy(token, sizeof(token), line);
        int id = 0;
        if (sscanf(token, "text %d", &id) == 1) {
            if (mission->message_count < (int)(sizeof(mission->messages) / sizeof(mission->messages[0]))) {
                current = &mission->messages[mission->message_count++];
                memset(current, 0, sizeof(*current));
                current->id = id;
            }
        } else if (current && token[0] != '\0') {
            size_t len = strlen(current->text);
            snprintf(current->text + len, sizeof(current->text) - len, "%s%s",
                     len > 0 ? " " : "", token);
        }
        line = next;
    }
    free(text);
}

static void dark_colony_script_add_command(DarkColonyScriptBlock *block,
                                           DarkColonyScriptCommand command) {
    if (!block || block->command_count >= (int)(sizeof(block->commands) / sizeof(block->commands[0])))
        return;
    block->commands[block->command_count++] = command;
}

static int dark_colony_parse_command_ints(const char *token, const char *keyword,
                                          int *out, int max_out) {
    if (!token || !keyword || !out || max_out <= 0) return -1;
    size_t keyword_len = strlen(keyword);
    if (strncmp(token, keyword, keyword_len) != 0) return -1;
    if (token[keyword_len] != '\0' && !isspace((unsigned char)token[keyword_len])) return -1;

    const char *p = token + keyword_len;
    int count = 0;
    while (*p && count < max_out) {
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;
        char *end = NULL;
        long value = strtol(p, &end, 10);
        if (end == p) break;
        out[count++] = (int)value;
        p = end;
    }
    return count;
}

static void dark_colony_add_reinforce_commands(DarkColonyScriptBlock *block,
                                               DarkColonyScriptCommandType type,
                                               const int *v, int count) {
    if (!block || !v || count < 5) return;
    int team = v[0];
    int x = v[1];
    int y = v[2];
    bool drop_added = false;
    for (int pair = 3; pair + 1 < count; pair += 2) {
        DarkColonyScriptCommand cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = type;
        cmd.a[0] = team;
        cmd.a[1] = x;
        cmd.a[2] = y;
        cmd.a[3] = v[pair + 1];
        cmd.a[4] = v[pair];
        if (cmd.a[3] <= 0) continue;
        cmd.a[5] = type == DC_SCRIPT_CMD_REINFORCE && !drop_added;
        drop_added = drop_added || cmd.a[5];
        dark_colony_script_add_command(block, cmd);
    }
    if (block->trigger_x < 0) {
        block->trigger_x = x;
        block->trigger_y = y;
    }
}

static void dark_colony_parse_tro(DarkColonyMission *mission, const char *path) {
    char *text = dark_colony_load_text(path);
    if (!text) return;
    DarkColonyScriptBlock *block = NULL;
    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char nl = *next; *next++ = '\0';
            if (nl == '\r' && *next == '\n') next++;
        }
        char token[256];
        trim_copy(token, sizeof(token), line);
        if (token[0] == '\0') {
            line = next;
            continue;
        }
        int id = 0, enabled = 0, c_gt = 0;
        char kind[16] = { 0 };
        if (sscanf(token, "%d %15s %d", &id, kind, &enabled) == 3 &&
            (strcmp(kind, "norm") == 0 || strcmp(kind, "trip") == 0)) {
            if (mission->block_count < (int)(sizeof(mission->blocks) / sizeof(mission->blocks[0]))) {
                block = &mission->blocks[mission->block_count++];
                memset(block, 0, sizeof(*block));
                block->id = id;
                block->trip = strcmp(kind, "trip") == 0;
                block->c_gt = -1;
                block->trigger_x = -1;
                block->trigger_y = -1;
                if (sscanf(token, "%*d %*s %*d (c>%d)", &c_gt) == 1) block->c_gt = c_gt;
                if (block->trip) block->requires_player_near = true;
            }
        } else if (block && strcmp(token, "end") == 0) {
            block = NULL;
        } else if (block) {
            DarkColonyScriptCommand cmd;
            memset(&cmd, 0, sizeof(cmd));
            int v[32] = { 0 };
            int parsed = 0;
            if (sscanf(token, "msg %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5) {
                cmd.type = DC_SCRIPT_CMD_MSG;
                cmd.a[0] = v[2];
                dark_colony_script_add_command(block, cmd);
            } else if ((parsed = dark_colony_parse_command_ints(token, "reinforce2", v, 31)) >= 5) {
                dark_colony_add_reinforce_commands(block, DC_SCRIPT_CMD_REINFORCE2, v, parsed);
            } else if ((parsed = dark_colony_parse_command_ints(token, "reinforce", v, 31)) >= 5) {
                dark_colony_add_reinforce_commands(block, DC_SCRIPT_CMD_REINFORCE, v, parsed);
            } else if (sscanf(token, "newtype %d %d %d", &v[0], &v[1], &v[2]) == 3) {
                cmd.type = DC_SCRIPT_CMD_NEWTYPE;
                cmd.a[0] = v[0];
                cmd.a[1] = v[1];
                cmd.a[2] = v[2];
                dark_colony_script_add_command(block, cmd);
                if (block->trigger_x < 0) {
                    block->trigger_x = v[0];
                    block->trigger_y = v[1];
                }
            }
        }
        line = next;
    }
    free(text);
}

static bool dark_colony_load_font(SDL_Renderer *renderer, const char *data_root, BitmapFont *font) {
    if (!renderer || !data_root || !font) return false;
    memset(font, 0, sizeof(*font));
    for (int i = 0; i < 128; ++i) font->glyph_index[i] = -1;
    char path[1024];
    path_join(path, sizeof(path), data_root, "INTRFACE/MFONTO7.SPR");
    uint32_t palette[256] = { 0 };
    if (!load_dark_colony_sprite(renderer, path, &font->sprite, palette)) return false;
    const int font_offset = 31;
    int max_w = 0, max_h = 0;
    for (int ch = font_offset; ch < 128; ++ch) {
        int frame = ch - font_offset;
        if (frame >= font->sprite.frame_count) break;
        font->glyph_index[ch] = frame;
        SDL_Rect bounds = font->sprite.frame_bounds ? font->sprite.frame_bounds[frame] : font->sprite.frames[frame];
        if (bounds.w > max_w) max_w = bounds.w;
        if (bounds.h > max_h) max_h = bounds.h;
    }
    font->draw_divisor = 1;
    font->glyph_w = max_w > 0 ? max_w : 6;
    font->glyph_h = max_h > 0 ? max_h : font->sprite.frame_h;
    font->line_h = font->glyph_h + 1;
    for (int ch = 0; ch < 128; ++ch) {
        int frame = font->glyph_index[ch];
        int advance = font->glyph_w;
        if (frame >= 0 && frame < font->sprite.frame_count && font->sprite.frame_bounds) {
            SDL_Rect bounds = font->sprite.frame_bounds[frame];
            if (bounds.w > 0) advance = bounds.w + 1;
        }
        font->glyph_width[ch] = (uint8_t)advance;
    }
    return true;
}

static void *dark_colony_load_mission(const char *map_path) {
    if (!map_path) return NULL;
    DarkColonyMission *mission = calloc(1, sizeof(*mission));
    if (!mission) return NULL;
    char msg_path[1024], tro_path[1024];
    replace_extension(msg_path, sizeof(msg_path), map_path, ".MSG");
    replace_extension(tro_path, sizeof(tro_path), map_path, ".TRO");
    dark_colony_parse_messages(mission, msg_path);
    dark_colony_parse_tro(mission, tro_path);
    if (getenv("OPEN_RTS_DEBUG_SCRIPT")) {
        fprintf(stderr, "Dark Colony mission %s: %d messages, %d blocks\n",
                map_path, mission->message_count, mission->block_count);
    }
    if (mission->message_count == 0 && mission->block_count == 0) {
        free(mission);
        return NULL;
    }
    return mission;
}

static void dark_colony_update_mission(void *ptr, GameMap *map, Unit *units, int *unit_count,
                                       VisualEffect *effects, int max_effects,
                                       const GameInfo *game_info, HudText *hud, float dt) {
    DarkColonyMission *mission = ptr;
    if (!mission || !units || !unit_count) return;
    mission->elapsed_ms += (int)(dt * 1000.0f);
    bool debug_script = getenv("OPEN_RTS_DEBUG_SCRIPT") != NULL;
    for (int i = 0; i < mission->block_count; ++i) {
        DarkColonyScriptBlock *block = &mission->blocks[i];
        if (block->fired) continue;
        bool fire = false;
        if (block->trip) {
            fire = block->trigger_x >= 0 &&
                   dark_colony_player_near(map, units, *unit_count, block->trigger_x, block->trigger_y);
        } else if (block->c_gt >= 0) {
            fire = mission->elapsed_ms > block->c_gt * DC_SCRIPT_COUNTER_MS;
        }
        if (fire) {
            if (debug_script) {
                fprintf(stderr, "Dark Colony script block %d fired (%d commands)\n",
                        block->id, block->command_count);
            }
            dark_colony_execute_script_block(mission, block, map, units, unit_count,
                                             effects, max_effects, game_info, hud);
        }
    }
    dark_colony_update_pending_spawns(mission, map, units, unit_count, game_info);

    /* Advance orbiting dropships */
    for (int i = 0; i < (int)(sizeof(mission->dropships) / sizeof(mission->dropships[0])); ++i) {
        DarkColonyDropship *ship = &mission->dropships[i];
        if (!ship->active) continue;
        ship->elapsed_ms += (int)(dt * 1000.0f);
        if (ship->elapsed_ms >= ship->duration_ms) {
            ship->active = false;
            continue;
        }
        ship->angle += ship->speed * dt;
        float sx = ship->center_gx + cosf(ship->angle) * ship->radius;
        float sy = ship->center_gy + sinf(ship->angle) * ship->radius;
        if (ship->effect_slot >= 0 && ship->effect_slot < max_effects &&
            effects[ship->effect_slot].active) {
            effects[ship->effect_slot].gx = sx;
            effects[ship->effect_slot].gy = sy;
        }
    }
}

static void dark_colony_destroy_mission(void *mission) {
    free(mission);
}

static bool dark_colony_load_runtime_sprites(SDL_Renderer *renderer, const char *data_root,
                                             const GameMap *map, const Unit *units, int unit_count,
                                             SpriteCache *cache) {
    return load_dark_colony_unit_sprites(renderer, data_root, map, units, unit_count, cache);
}

static const Plugin DARK_COLONY_PLUGIN = {
    .id             = "dark-colony",
    .name           = "Dark Colony",
    .version        = "0.1",
    .default_root   = "data/DCOLONY",
    .default_map    = "SCENARIO/HUMAN/HUMAN01.MAP",
    .default_sprite = "SPRITES/TROOPER1.SPR",
    .subsystems     = RTS_SUBSYSTEM_FILESYSTEM | RTS_SUBSYSTEM_GRAPHICS |
                      RTS_SUBSYSTEM_PALETTES   | RTS_SUBSYSTEM_TILESETS |
                      RTS_SUBSYSTEM_MAPS       | RTS_SUBSYSTEM_SPRITES  |
                      RTS_SUBSYSTEM_WORLD      | RTS_SUBSYSTEM_PLAYERS  |
                      RTS_SUBSYSTEM_ORDERS     | RTS_SUBSYSTEM_SIMULATION |
                      RTS_SUBSYSTEM_RENDERER   | RTS_SUBSYSTEM_UI |
                      RTS_SUBSYSTEM_SCRIPTING,
    .cell_w            = 32,
    .cell_h            = 32,
    .game_info         = &dark_colony_game_info,
    .actor_types       = DARK_COLONY_ACTOR_TYPES,
    .actor_type_count  = (int)(sizeof(DARK_COLONY_ACTOR_TYPES) / sizeof(DARK_COLONY_ACTOR_TYPES[0])),
    .debug_enemy_type_id = MT_DC_GREY,
    .capabilities        = {
        .map_format = MAP_FORMAT_DARK_COLONY_MAP_MTG_OVH,
        .capabilities = PLUGIN_CAP_STATIC_METADATA |
                        PLUGIN_CAP_RUNTIME_SPRITES |
                        PLUGIN_CAP_MISSION_SCRIPT |
                        PLUGIN_CAP_SOFTWARE_RENDERER_SAFE,
        .data_capability = "dark-colony:data/DCOLONY",
        .graphics_capability = "dark-colony:spr-mtg-ovh",
    },
    .definition          = {
        .id             = "dark-colony",
        .name           = "Dark Colony",
        .version        = "0.1",
        .default_root   = "data/DCOLONY",
        .default_map    = "SCENARIO/HUMAN/HUMAN01.MAP",
        .default_sprite = "SPRITES/TROOPER1.SPR",
        .subsystems     = RTS_SUBSYSTEM_FILESYSTEM | RTS_SUBSYSTEM_GRAPHICS |
                          RTS_SUBSYSTEM_PALETTES   | RTS_SUBSYSTEM_TILESETS |
                          RTS_SUBSYSTEM_MAPS       | RTS_SUBSYSTEM_SPRITES  |
                          RTS_SUBSYSTEM_WORLD      | RTS_SUBSYSTEM_PLAYERS  |
                          RTS_SUBSYSTEM_ORDERS     | RTS_SUBSYSTEM_SIMULATION |
                          RTS_SUBSYSTEM_RENDERER   | RTS_SUBSYSTEM_UI |
                          RTS_SUBSYSTEM_SCRIPTING,
        .cell_w            = 32,
        .cell_h            = 32,
        .game_info         = &dark_colony_game_info,
        .actor_types       = DARK_COLONY_ACTOR_TYPES,
        .actor_type_count  = (int)(sizeof(DARK_COLONY_ACTOR_TYPES) / sizeof(DARK_COLONY_ACTOR_TYPES[0])),
        .debug_enemy_type_id = MT_DC_GREY,
        .capabilities        = {
            .map_format = MAP_FORMAT_DARK_COLONY_MAP_MTG_OVH,
            .capabilities = PLUGIN_CAP_STATIC_METADATA |
                            PLUGIN_CAP_RUNTIME_SPRITES |
                            PLUGIN_CAP_MISSION_SCRIPT |
                            PLUGIN_CAP_SOFTWARE_RENDERER_SAFE,
            .data_capability = "dark-colony:data/DCOLONY",
            .graphics_capability = "dark-colony:spr-mtg-ovh",
        },
    },
    .loaders             = {
        .load_map            = load_dark_colony_map,
        .load_assets         = dark_colony_plugin_load_assets,
        .load_initial_units  = load_dark_colony_initial_units,
        .load_runtime_sprites = dark_colony_load_runtime_sprites,
        .load_font           = dark_colony_load_font,
        .load_mission        = dark_colony_load_mission,
        .update_mission      = dark_colony_update_mission,
        .destroy_mission     = dark_colony_destroy_mission,
    },
    .load_map            = load_dark_colony_map,
    .load_assets         = dark_colony_plugin_load_assets,
    .load_initial_units  = load_dark_colony_initial_units,
    .load_runtime_sprites = dark_colony_load_runtime_sprites,
    .load_font           = dark_colony_load_font,
    .load_mission        = dark_colony_load_mission,
    .update_mission      = dark_colony_update_mission,
    .destroy_mission     = dark_colony_destroy_mission,
};

const Plugin *open_rts_plugin_entry(void) { return &DARK_COLONY_PLUGIN; }

/* keep old name for any static-link usage */
const Plugin *open_rts_dark_colony_plugin(void) { return &DARK_COLONY_PLUGIN; }
