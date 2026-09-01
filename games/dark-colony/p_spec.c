#include "game.h"
#include "dc_facing.h"
#include "info.h"
#include "dc_types.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const actortype_t *dark_colony_actor_type_by_id(uint16_t type_id);
void dark_colony_apply_actor_type_defaults(mobj_t *unit, const actortype_t *type);

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
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return NULL;
    char *text = malloc(blob.size + 1);
    if (!text) {
        W_FreeFile(&blob);
        return NULL;
    }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';
    W_FreeFile(&blob);
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
    int ai_elapsed_ms;
    int ai_wave_elapsed_ms;
    uint32_t ai_wave_target_id;
} DarkColonyMission;

typedef struct {
    int think_interval_ms;
    int attack_wave_interval_ms;
    float defense_radius;
    float attack_eagerness;
} DarkColonyAiConfig;

/* These values mirror the observable Krusty attack policy: think in batches,
 * prefer dangerous mobile targets, then use distance as the stable tie-break.
 * The table is intentionally plugin-local until DC's unitid/depend tables are
 * fully decoded. */
static const DarkColonyAiConfig dark_colony_ai_config = {
    .think_interval_ms = 500,
    .attack_wave_interval_ms = 5000,
    .defense_radius = 12.0f,
    .attack_eagerness = 1.0f,
};

static int dark_colony_ai_target(const mobj_t *attacker, const mobj_t *units,
                                 int unit_count, int preferred_index, bool defending) {
    int best = -1;
    float best_score = -INFINITY;
    fvec2_t attacker_position = fixedvec3_xy_to_fvec2(attacker->core.position);
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *candidate = &units[i];
        if (candidate == attacker || candidate->remove || candidate->hp <= 0 ||
            candidate->owner == attacker->owner) continue;
        fvec2_t delta = fvec2_sub(fixedvec3_xy_to_fvec2(candidate->core.position),
                                  attacker_position);
        float distance2 = fvec2_length_squared(delta);
        float threat = (candidate->traits & MF_ATTACK) != 0 ? 2.0f : 0.0f;
        float mobility = (candidate->traits & MF_MOBILE) != 0 ? 0.5f : 0.0f;
        float preferred = i == preferred_index ? 1.5f : 0.0f;
        if (defending && (candidate->traits & MF_ATTACK) != 0) preferred += 1.0f;
        float score = dark_colony_ai_config.attack_eagerness * (threat + mobility) -
                      distance2 * 0.02f + preferred;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

static int dark_colony_ai_find_wave_target(const mobj_t *units, int unit_count) {
    int best = -1;
    float best_score = -INFINITY;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *candidate = &units[i];
        if (candidate->remove || candidate->hp <= 0 || candidate->owner != 0) continue;
        float score = (candidate->traits & MF_ATTACK) != 0 ? 3.0f : 0.0f;
        score += (candidate->traits & MF_MOBILE) != 0 ? 1.0f : 0.0f;
        score += candidate->max_hp > 0 ? (float)candidate->max_hp / 2000.0f : 0.0f;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

static bool dark_colony_ai_is_defending(const mobj_t *units, int unit_count,
                                        fvec2_t base_position) {
    float radius2 = dark_colony_ai_config.defense_radius *
                    dark_colony_ai_config.defense_radius;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *unit = &units[i];
        if (unit->remove || unit->hp <= 0 || unit->owner != 0 ||
            (unit->traits & MF_ATTACK) == 0) continue;
        if (fvec2_distance_squared(fixedvec3_xy_to_fvec2(unit->core.position),
                                   base_position) <= radius2) return true;
    }
    return false;
}

static void dark_colony_update_ai(DarkColonyMission *mission, const level_t *map,
                                  mobj_t *units, int unit_count, int dt_ms) {
    if (!map || !units || unit_count <= 0 ||
        !dark_colony_map_has_ai(map, 1)) return;
    mission->ai_elapsed_ms += dt_ms;
    if (mission->ai_elapsed_ms < dark_colony_ai_config.think_interval_ms) return;
    mission->ai_elapsed_ms %= dark_colony_ai_config.think_interval_ms;

    mission->ai_wave_elapsed_ms += dark_colony_ai_config.think_interval_ms;
    int wave_target = -1;
    if (mission->ai_wave_elapsed_ms >= dark_colony_ai_config.attack_wave_interval_ms) {
        mission->ai_wave_elapsed_ms %= dark_colony_ai_config.attack_wave_interval_ms;
        wave_target = dark_colony_ai_find_wave_target(units, unit_count);
        mission->ai_wave_target_id = wave_target >= 0 ? units[wave_target].id : 0;
    }
    if (wave_target < 0 && mission->ai_wave_target_id != 0) {
        for (int i = 0; i < unit_count; ++i) {
            if (units[i].id == mission->ai_wave_target_id &&
                !units[i].remove && units[i].hp > 0) {
                wave_target = i;
                break;
            }
        }
    }

    fvec2_t base_position = { 0.0f, 0.0f };
    int base_count = 0;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *unit = &units[i];
        if (unit->remove || unit->hp <= 0 || unit->owner == 0 ||
            (unit->traits & MF_MOBILE) != 0) continue;
        base_position = fvec2_add(base_position,
                                  fixedvec3_xy_to_fvec2(unit->core.position));
        base_count++;
    }
    if (base_count > 0) base_position = fvec2_scale(base_position, 1.0f / (float)base_count);
    bool defending = base_count > 0 &&
                     dark_colony_ai_is_defending(units, unit_count, base_position);

    for (int i = 0; i < unit_count; ++i) {
        mobj_t *attacker = &units[i];
        if (attacker->remove || attacker->hp <= 0 || attacker->owner == 0 ||
            (attacker->traits & (MF_MOBILE | MF_ATTACK)) != (MF_MOBILE | MF_ATTACK)) {
            continue;
        }
        int target_index = dark_colony_ai_target(attacker, units, unit_count,
                                                 defending ? -1 : wave_target,
                                                 defending);
        if (target_index < 0) continue;
        mobj_t *target = &units[target_index];
        attacker->attack.target = target_index;
        fvec2_t target_position = fixedvec3_xy_to_fvec2(target->core.position);
        fvec2_t attacker_position = fixedvec3_xy_to_fvec2(attacker->core.position);
        float range = attacker->attack.range > 0.0f ? attacker->attack.range : 1.0f;
        if (fvec2_distance_squared(attacker_position, target_position) > range * range) {
            P_MoveUnitTo(map, attacker, target_position);
        }
    }
}

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

static bool dark_colony_player_near(const level_t *map, const mobj_t *units,
                                    int unit_count, int gx, int gy) {
    (void)map;
    fvec2_t center = fvec2_cell_center((ivec2_t){ gx, gy });
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].owner != 0 || units[i].remove || units[i].hp <= 0) continue;
        if (fvec2_distance_squared(
            fixedvec3_xy_to_fvec2(units[i].core.position), center) <= 16.0f) return true;
    }
    return false;
}

static int dark_colony_spawn_dropship_effect(effect_t *effects, int max_effects,
                                             float gx, float gy, int duration_ms) {
    for (int i = 0; i < max_effects; ++i) {
        if (effects[i].active) continue;
        effect_t *effect = &effects[i];
        memset(effect, 0, sizeof(*effect));
        effect->active = true;
        effect->core.position = fixedvec3_from_fvec2((fvec2_t){ gx, gy }, 0);
        effect->duration_ms = duration_ms;
        effect->frame_ms = duration_ms + 1;
        effect->core.render_intensity = 16;
        effect->core.render_offset = (ivec2_t){ 0, -75 };
        snprintf(effect->core.sprite_name, sizeof(effect->core.sprite_name), "SPRITES/DROP.SPR");
        return i;
    }
    return -1;
}

static void dark_colony_spawn_drop_effect(DarkColonyMission *mission,
                                          effect_t *effects, int max_effects,
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

static void dark_colony_spawn_script_unit(const level_t *map, mobj_t *units, int *unit_count, int team,
                                          int gx, int gy, int type, int index,
                                          const gameinfo_t *game_info) {
    if (!units || !unit_count || *unit_count >= MAXMOBJS) return;
    mobj_t *unit = &units[*unit_count];
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
    unit->core.position = fixedvec3_from_fvec2(
        fvec2_cell_center((ivec2_t){ spawn_x, spawn_y }), 0);
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
    unit->core.angle = dc_direction_to_angle(unit->owner == 0 ? 6 : 14);
    uint16_t type_id = dark_colony_script_unit_type(team, type);
    const actortype_t *actor = dark_colony_actor_type_by_id(type_id);
    dark_colony_apply_actor_type_defaults(unit, actor);
    P_SpawnMobj(game_info, unit);
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

static void dark_colony_update_pending_spawns(DarkColonyMission *mission, level_t *map,
                                              mobj_t *units, int *unit_count,
                                              const gameinfo_t *game_info) {
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
                                             level_t *map, mobj_t *units, int *unit_count,
                                             effect_t *effects, int max_effects,
                                             const gameinfo_t *game_info, hudtext_t *hud) {
    if (!mission || !block) return;
    int drop_sequence = 0;
    for (int i = 0; i < block->command_count; ++i) {
        DarkColonyScriptCommand *cmd = &block->commands[i];
        if (cmd->type == DC_SCRIPT_CMD_MSG) {
            const char *message = dark_colony_script_message(mission, cmd->a[0]);
            if (message) HU_PushMessage(hud, message, 6500);
        } else if (cmd->type == DC_SCRIPT_CMD_REINFORCE ||
                   cmd->type == DC_SCRIPT_CMD_REINFORCE2) {
            int team = cmd->a[0], x = cmd->a[1], y = cmd->a[2];
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

void *dark_colony_load_mission(const char *map_path) {
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
    /* Keep an empty mission object alive: the scenario's native %AI records
     * are map data, and the AI thinker must still run when no .MSG/.TRO script
     * accompanies the map. */
    return mission;
}

void dark_colony_update_mission(void *ptr, level_t *map, mobj_t *units, int *unit_count,
                                effect_t *effects, int max_effects,
                                const gameinfo_t *game_info, hudtext_t *hud, float dt) {
    DarkColonyMission *mission = ptr;
    if (!mission || !units || !unit_count) return;
    mission->elapsed_ms += (int)(dt * 1000.0f);
    dark_colony_update_ai(mission, map, units, *unit_count, (int)(dt * 1000.0f));
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
            effects[ship->effect_slot].core.position =
                fixedvec3_from_fvec2((fvec2_t){ sx, sy }, 0);
        }
    }
}

void dark_colony_destroy_mission(void *mission) {
    free(mission);
}
