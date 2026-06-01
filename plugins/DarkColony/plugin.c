#include "plugin.h"
#include "info.h"

#include <ctype.h>
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

void A_DC_MuzzleFlash(RtsStateContext *ctx, Unit *unit) {
    if (!ctx || !unit) return;
    int muzzle_state = 0;
    if (ctx->game_info && unit->type_id > 0 &&
        unit->type_id < ctx->game_info->mobj_type_count) {
        muzzle_state = ctx->game_info->mobjinfo[unit->type_id].muzzleflash;
    }
    rts_spawn_state_effect(ctx, muzzle_state, unit->gx, unit->gy, unit->facing_code);
}

void A_DC_Attack(RtsStateContext *ctx, Unit *unit) {
    rts_unit_fire_attack(ctx, unit);
}

void A_DC_Fall(RtsStateContext *ctx, Unit *unit) {
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

void A_DC_Corpse(RtsStateContext *ctx, Unit *unit) {
    if (!unit) return;
    rts_unit_add_corpse_decoration(ctx, unit);
    unit->remove = true;
}

static const RtsActorType DARK_COLONY_ACTOR_TYPES[] = {
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
        .traits = RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE | RTS_TRAIT_RENDERABLE,
        .speed = 6.0f,
        .max_hp = 800,
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
};

static const RtsActorType *dark_colony_actor_type_by_id(uint16_t type_id) {
    for (int i = 0; i < (int)(sizeof(DARK_COLONY_ACTOR_TYPES) / sizeof(DARK_COLONY_ACTOR_TYPES[0])); ++i) {
        if (DARK_COLONY_ACTOR_TYPES[i].id == type_id) return &DARK_COLONY_ACTOR_TYPES[i];
    }
    return NULL;
}

static void dark_colony_apply_actor_type_defaults(Unit *unit, const RtsActorType *type) {
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
    if (unit->attack_target <= 0) unit->attack_target = -1;
    if (unit->harvest_target == 0) unit->harvest_target = -1;
    if (unit->sprite_name[0] == '\0' && type->sprite_name)
        snprintf(unit->sprite_name, sizeof(unit->sprite_name), "%s", type->sprite_name);
    if (unit->shadow_name[0] == '\0' && type->shadow_name)
        snprintf(unit->shadow_name, sizeof(unit->shadow_name), "%s", type->shadow_name);
    if (unit->muzzle_flash_name[0] == '\0' && type->muzzle_flash_name)
        snprintf(unit->muzzle_flash_name, sizeof(unit->muzzle_flash_name), "%s", type->muzzle_flash_name);
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
    DC_SCRIPT_CMD_NEWTYPE,
} DarkColonyScriptCommandType;

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
    DarkColonyScriptMessage messages[64];
    int message_count;
    DarkColonyScriptBlock blocks[64];
    int block_count;
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

static bool dark_colony_player_near(const Unit *units, int unit_count, int gx, int gy) {
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].owner != 0 || units[i].remove || units[i].hp <= 0) continue;
        float dx = units[i].gx - ((float)gx + 0.5f);
        float dy = units[i].gy - ((float)gy + 0.5f);
        if (dx * dx + dy * dy <= 16.0f) return true;
    }
    return false;
}

static void dark_colony_spawn_drop_effect(RtsVisualEffect *effects, int max_effects, int gx, int gy) {
    if (!effects || max_effects <= 0) return;
    for (int i = 0; i < max_effects; ++i) {
        if (effects[i].active) continue;
        RtsVisualEffect *effect = &effects[i];
        memset(effect, 0, sizeof(*effect));
        effect->active = true;
        effect->gx = (float)gx + 0.5f;
        effect->gy = (float)gy + 0.5f;
        effect->duration_ms = 1400;
        effect->frame_ms = 90;
        snprintf(effect->sprite_name, sizeof(effect->sprite_name), "SPRITES/DROP.SPR");
        return;
    }
}

static void dark_colony_spawn_script_unit(Unit *units, int *unit_count, int team,
                                          int gx, int gy, int type, int index,
                                          const RtsGameInfo *game_info) {
    if (!units || !unit_count || *unit_count >= MAX_UNITS) return;
    Unit *unit = &units[*unit_count];
    memset(unit, 0, sizeof(*unit));
    int offset_x = index % 2;
    int offset_y = index / 2;
    unit->gx = (float)(gx + offset_x) + 0.5f;
    unit->gy = (float)(gy + offset_y) + 0.5f;
    unit->owner = team == 0 ? 0 : 1;
    unit->facing_code = unit->owner == 0 ? 6 : 14;
    uint16_t type_id = dark_colony_script_unit_type(team, type);
    const RtsActorType *actor = dark_colony_actor_type_by_id(type_id);
    dark_colony_apply_actor_type_defaults(unit, actor);
    rts_apply_mobjinfo_defaults(game_info, unit);
    (*unit_count)++;
}

static void dark_colony_execute_script_block(DarkColonyMission *mission, DarkColonyScriptBlock *block,
                                             GameMap *map, Unit *units, int *unit_count,
                                             RtsVisualEffect *effects, int max_effects,
                                             const RtsGameInfo *game_info, RtsHudText *hud) {
    (void)map;
    if (!mission || !block) return;
    for (int i = 0; i < block->command_count; ++i) {
        DarkColonyScriptCommand *cmd = &block->commands[i];
        if (cmd->type == DC_SCRIPT_CMD_MSG) {
            const char *message = dark_colony_script_message(mission, cmd->a[0]);
            if (message) rts_hud_text_push(hud, message, 6500);
        } else if (cmd->type == DC_SCRIPT_CMD_REINFORCE) {
            int team = cmd->a[0], x = cmd->a[1], y = cmd->a[2];
            int count = cmd->a[3] > 0 ? cmd->a[3] : 1;
            int type = cmd->a[4];
            dark_colony_spawn_drop_effect(effects, max_effects, x, y);
            for (int n = 0; n < count; ++n)
                dark_colony_spawn_script_unit(units, unit_count, team, x, y, type, n, game_info);
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
            int v[13] = { 0 };
            if (sscanf(token, "msg %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5) {
                cmd.type = DC_SCRIPT_CMD_MSG;
                cmd.a[0] = v[2];
                dark_colony_script_add_command(block, cmd);
            } else if (sscanf(token, "reinforce %d %d %d %d %d %d %d %d %d %d %d %d %d",
                              &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6],
                              &v[7], &v[8], &v[9], &v[10], &v[11], &v[12]) >= 6) {
                cmd.type = DC_SCRIPT_CMD_REINFORCE;
                cmd.a[0] = v[0];
                cmd.a[1] = v[1];
                cmd.a[2] = v[2];
                cmd.a[3] = v[4];
                cmd.a[4] = v[5];
                dark_colony_script_add_command(block, cmd);
                if (block->trigger_x < 0) {
                    block->trigger_x = v[1];
                    block->trigger_y = v[2];
                }
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

static bool dark_colony_load_font(SDL_Renderer *renderer, const char *data_root, RtsBitmapFont *font) {
    if (!renderer || !data_root || !font) return false;
    memset(font, 0, sizeof(*font));
    for (int i = 0; i < 128; ++i) font->glyph_index[i] = -1;
    char path[1024];
    path_join(path, sizeof(path), data_root, "INTRFACE/MFONT.SPR");
    uint32_t palette[256] = { 0 };
    if (!load_dark_colony_sprite(renderer, path, &font->sprite, palette)) return false;
    int max_w = 0, max_h = 0;
    for (int ch = 32; ch < 128; ++ch) {
        int frame = ch - 32;
        if (frame >= font->sprite.frame_count) break;
        font->glyph_index[ch] = frame;
        SDL_Rect bounds = font->sprite.frame_bounds ? font->sprite.frame_bounds[frame] : font->sprite.frames[frame];
        if (bounds.w > max_w) max_w = bounds.w;
        if (bounds.h > max_h) max_h = bounds.h;
    }
    font->draw_divisor = 2;
    font->glyph_w = max_w > 0 ? (max_w + 3) / 2 : 8;
    font->glyph_h = max_h > 0 ? (max_h + 1) / 2 : font->sprite.frame_h / 2;
    font->line_h = font->glyph_h + 3;
    for (int i = 0; i < 128; ++i) font->glyph_width[i] = (uint8_t)font->glyph_w;
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
                                       RtsVisualEffect *effects, int max_effects,
                                       const RtsGameInfo *game_info, RtsHudText *hud, float dt) {
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
                   dark_colony_player_near(units, *unit_count, block->trigger_x, block->trigger_y);
        } else if (block->c_gt >= 0) {
            fire = mission->elapsed_ms > block->c_gt * 33;
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
}

static void dark_colony_destroy_mission(void *mission) {
    free(mission);
}

static bool dark_colony_load_runtime_sprites(SDL_Renderer *renderer, const char *data_root,
                                             const GameMap *map, const Unit *units, int unit_count,
                                             SpriteCache *cache) {
    return load_dark_colony_unit_sprites(renderer, data_root, map, units, unit_count, cache);
}

static const RtsPlugin DARK_COLONY_PLUGIN = {
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
    .load_map            = load_dark_colony_map,
    .load_assets         = dark_colony_plugin_load_assets,
    .load_initial_units  = load_dark_colony_initial_units,
    .load_runtime_sprites = dark_colony_load_runtime_sprites,
    .load_font           = dark_colony_load_font,
    .load_mission        = dark_colony_load_mission,
    .update_mission      = dark_colony_update_mission,
    .destroy_mission     = dark_colony_destroy_mission,
};

const RtsPlugin *open_rts_plugin_entry(void) { return &DARK_COLONY_PLUGIN; }

/* keep old name for any static-link usage */
const RtsPlugin *open_rts_dark_colony_plugin(void) { return &DARK_COLONY_PLUGIN; }
