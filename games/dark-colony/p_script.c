#include "p_script.h"
#include "p_drop.h"
#include "p_reinforce.h"
#include "dc_types.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static char *load_text(const char *path) {
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
    SCRIPT_CMD_NONE,
    SCRIPT_CMD_MSG,
    SCRIPT_CMD_REINFORCE,
    SCRIPT_CMD_REINFORCE2,
    SCRIPT_CMD_NEWTYPE,
    SCRIPT_CMD_BAIL,
    SCRIPT_CMD_NEWRATE,
    SCRIPT_CMD_SETARRAY,
    SCRIPT_CMD_SETLIFES,
} ScriptCommandType;

typedef enum {
    COND_COUNTER_GT,    /* c > N */
    COND_ALL_BUILDINGS_DESTROYED, /* b(team,0..4)==0 */
    COND_UNIT_TYPE_EXISTS, /* s(team,type,slot)==1 */
    COND_TRIP_PLAYER_NEAR, /* S==0 */
    COND_COUNTER_GT_STATE, /* c > s(team,type,slot) */
    COND_STATE_ARRAY_EQ,   /* s(team,arr,idx) == val */
    COND_UNSUPPORTED,
} ConditionKind;

typedef struct {
    int team;
    int slots[5];
    int slot_count;
} BuildingCondition;

typedef struct {
    int team;
    int type;
    int slot;
    int expected_value;
} UnitStateCondition;

enum {
    SCRIPT_COUNTER_MS = 1000,
    MAX_CITY_SLOTS = 5,
};

typedef struct {
    ScriptCommandType type;
    int a[8];
} ScriptCommand;

typedef struct {
    int id;
    bool trip;
    bool fired;
    int c_gt;
    bool requires_player_near;
    int trigger_x;
    int trigger_y;
    /* Block condition fields for s() and b() predicates. */
    int cond_type; /* 0 = none/c>N, 1 = b(team,slot)==N, 2 = s(team,arr,idx)==N, 3 = c>s(team,arr,idx) */
    int cond_a;    /* team (b, s) */
    int cond_b;    /* slot (b), arr (s) */
    int cond_c;    /* ==N value (b, s) */
    int cond_d;    /* idx (s) */
    int cond_gt;   /* for c>s: state_arrays[team][idx] threshold */
    ScriptCommand commands[32];
    int command_count;
    ConditionKind condition_kind;
    bool condition_negated; /* true if condition should be negated (e.g. enabled=0) */
    BuildingCondition building_cond;
    UnitStateCondition unit_state_cond;
    int counter_gt_state_team;
    int counter_gt_state_type;
    int counter_gt_state_slot;
} ScriptBlock;

typedef struct {
    int id;
    char text[256];
} ScriptMessage;

typedef struct {
    int team;
    int slot;
    int anchor_x;
    int anchor_y;
    int race;
} CitySlotInfo;

struct ScriptState {
    ScriptMessage messages[64];
    int message_count;
    ScriptBlock blocks[64];
    int block_count;
    int elapsed_ms;
    MissionState state;
    int city_slot_count;
    CitySlotInfo city_slots[32];
    int script_arrays[16];
    int state_arrays[8][128];
};

static const char *script_message(const ScriptState *script, int id) {
    if (!script) return NULL;
    for (int i = 0; i < script->message_count; ++i)
        if (script->messages[i].id == id) return script->messages[i].text;
    return NULL;
}

static bool player_near(const level_t *map, const mobj_t *units,
                                    int unit_count, int gx, int gy) {
    (void)map;
    fvec2_t center = fvec2_cell_center((ivec2_t){ gx, gy });
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].owner != 0 || units[i].remove || units[i].hp <= 0) continue;
        if (fvec2_distance_squared(
            fixed3_xy_to_fvec2(units[i].core.position), center) <= 16.0f) return true;
    }
    return false;
}

static int script_nearest_vent(const level_t *map, int gx, int gy) {
    int best = -1;
    float best_distance2 = INFINITY;
    if (!map || !map->resource_vents) return best;
    fvec2_t target = fvec2_cell_center((ivec2_t){ gx, gy });
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!vent->active || vent->amount <= 0) continue;
        float distance2 = fvec2_distance_squared(target, vent->attachment);
        if (distance2 < best_distance2) {
            best_distance2 = distance2;
            best = i;
        }
    }
    return best;
}

static void execute_script_block(ScriptState *script, ScriptBlock *block,
                                              level_t *map, mobj_t *units, int *unit_count,
                                              effect_t *effects, int max_effects,
                                              const gameinfo_t *game_info, hudtext_t *hud) {
    if (!script || !block) return;
    for (int i = 0; i < block->command_count; ++i) {
        ScriptCommand *cmd = &block->commands[i];
        if (cmd->type == SCRIPT_CMD_MSG) {
            const char *message = script_message(script, cmd->a[0]);
            if (message) HU_PushMessage(hud, message, -1);
        } else if (cmd->type == SCRIPT_CMD_REINFORCE ||
                   cmd->type == SCRIPT_CMD_REINFORCE2) {
            int team = cmd->a[0], x = cmd->a[1], y = cmd->a[2];
            int count = cmd->a[3] > 0 ? cmd->a[3] : 1;
            int type = cmd->a[4];
            if (cmd->type == SCRIPT_CMD_REINFORCE && cmd->a[5]) {
                DropshipPayload payload[DROPSHIP_MAX_PAYLOAD_TYPES];
                int payload_count = 0;
                for (int j = i; j < block->command_count; ++j) {
                    const ScriptCommand *drop_cmd = &block->commands[j];
                    if (drop_cmd->type != SCRIPT_CMD_REINFORCE) break;
                    if (j != i && drop_cmd->a[5]) break;
                    if (payload_count < DROPSHIP_MAX_PAYLOAD_TYPES)
                        payload[payload_count++] = (DropshipPayload){
                            drop_cmd->a[4], drop_cmd->a[3] > 0 ? drop_cmd->a[3] : 1,
                        };
                }
                DC_StartDropship(map, effects, max_effects, team,
                                 (ivec2_t){ x, y }, payload, payload_count);
            }
            for (int n = 0; n < count; ++n) {
                if (cmd->type == SCRIPT_CMD_REINFORCE2)
                    DC_SpawnReinforcement(map, units, unit_count, team, x, y, type, game_info);
            }
        } else if (cmd->type == SCRIPT_CMD_BAIL) {
            int n = cmd->a[0], m = cmd->a[1];
            if (n >= 0 && n < script->block_count)
                script->blocks[n].fired = true;
            if (m >= 0 && m < script->block_count)
                script->blocks[m].fired = false;
            {
                MissionState new_state = MISSION_ACTIVE;
                const char *msg = NULL;
                if (n == 0 && m == 1) {
                    new_state = MISSION_WON;
                    msg = "ALIEN HIVE DESTROYED.";
                } else if (n == 1 && m == 2) {
                    new_state = MISSION_LOST;
                    msg = "ALL YOUR BUILDINGS HAVE BEEN DESTROYED.";
                } else if (n == 1 && m == 3) {
                    new_state = MISSION_ALLY_LOST;
                    msg = "ALLIED BASE LOST.";
                }
                if (new_state != MISSION_ACTIVE) {
                    if (getenv("OPEN_RTS_DEBUG_SCRIPT")) {
                        fprintf(stderr, "Dark Colony mission state -> %d (bail %d %d)\n",
                                new_state, n, m);
                    }
                    script->state = new_state;
                    if (msg) HU_PushMessage(hud, msg, -1);
                }
            }
        } else if (cmd->type == SCRIPT_CMD_NEWRATE) {
            int rate = cmd->a[0], x = cmd->a[1], y = cmd->a[2];
            int vi = script_nearest_vent(map, x, y);
            if (vi >= 0) map->resource_vents[vi].rate = rate;
        } else if (cmd->type == SCRIPT_CMD_SETARRAY) {
            int slot = cmd->a[0];
            int val = 0;
            if (cmd->a[1] == -1) {
                val = script->elapsed_ms / SCRIPT_COUNTER_MS + cmd->a[2];
            } else {
                val = cmd->a[2];
            }
            if (slot >= 0 && slot < 128)
                script->state_arrays[0][slot] = val;
        } else if (cmd->type == SCRIPT_CMD_SETLIFES) {
            int blk = cmd->a[0], val = cmd->a[1];
            if (blk >= 0 && blk < script->block_count)
                script->blocks[blk].fired = val == 0;
        }
    }
    block->fired = true;
}

static void parse_messages(ScriptState *script, const char *path) {
    char *text = load_text(path);
    if (!text) return;
    ScriptMessage *current = NULL;
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
            if (script->message_count < (int)(sizeof(script->messages) / sizeof(script->messages[0]))) {
                current = &script->messages[script->message_count++];
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

static void script_add_command(ScriptBlock *block,
                                           ScriptCommand command) {
    if (!block || block->command_count >= (int)(sizeof(block->commands) / sizeof(block->commands[0])))
        return;
    block->commands[block->command_count++] = command;
}

static int parse_command_ints(const char *token, const char *keyword,
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

static void add_reinforce_commands(ScriptBlock *block,
                                               ScriptCommandType type,
                                               const int *v, int count) {
    if (!block || !v || count < 5) return;
    int team = v[0];
    int x = v[1];
    int y = v[2];
    bool drop_added = false;
    for (int pair = 3; pair + 1 < count; pair += 2) {
        ScriptCommand cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = type;
        cmd.a[0] = team;
        cmd.a[1] = x;
        cmd.a[2] = y;
        cmd.a[3] = v[pair + 1];
        cmd.a[4] = v[pair];
        if (cmd.a[3] <= 0) continue;
        cmd.a[5] = type == SCRIPT_CMD_REINFORCE && !drop_added;
        drop_added = drop_added || cmd.a[5];
        script_add_command(block, cmd);
    }
    if (block->trigger_x < 0) {
        block->trigger_x = x;
        block->trigger_y = y;
    }
}

static void parse_block_condition(ScriptBlock *block, const char *cond) {
    if (!block || !cond) return;
    /* Detect b(team,slot)==0 patterns: ((b(2,0)==0)&&(b(2,1)==0)&&...) */
    int team = -1, slot = -1;
    if (sscanf(cond, "b(%d,%d)==0", &team, &slot) == 2 ||
        sscanf(cond, "(b(%d,%d))==0", &team, &slot) == 2) {
        block->condition_kind = COND_ALL_BUILDINGS_DESTROYED;
        block->building_cond.team = team;
        block->building_cond.slot_count = 1;
        block->building_cond.slots[0] = slot;
        return;
    }
    /* Detect compound b() conditions: ((b(2,0)==0)&&(b(2,1)==0)&&(b(2,2)==0)&&...) */
    if (strstr(cond, "b(") && strstr(cond, "==0")) {
        block->condition_kind = COND_ALL_BUILDINGS_DESTROYED;
        block->building_cond.team = -1;
        block->building_cond.slot_count = 0;
        const char *p = cond;
        while (*p && block->building_cond.slot_count < MAX_CITY_SLOTS) {
            if (sscanf(p, " b(%d,%d)==0", &team, &slot) == 2 ||
                sscanf(p, "(b(%d,%d))==0", &team, &slot) == 2) {
                if (block->building_cond.team < 0)
                    block->building_cond.team = team;
                block->building_cond.slots[block->building_cond.slot_count++] = slot;
            }
            p++;
        }
        return;
    }
    /* Detect s(team,type,slot)==N */
    int array = -1, index = -1, expected = -1;
    if (sscanf(cond, "s(%d,%d,%d)==%d", &team, &array, &index, &expected) == 4 ||
        sscanf(cond, "(s(%d,%d,%d))==%d", &team, &array, &index, &expected) == 4) {
        block->condition_kind = COND_UNIT_TYPE_EXISTS;
        block->unit_state_cond.team = team;
        block->unit_state_cond.type = array;
        block->unit_state_cond.slot = index;
        block->unit_state_cond.expected_value = expected;
        return;
    }
    /* Detect compound s() conditions with || */
    if (strstr(cond, "s(") && strstr(cond, "==1")) {
        block->condition_kind = COND_UNIT_TYPE_EXISTS;
        block->unit_state_cond.team = -1;
        block->unit_state_cond.type = -1;
        block->unit_state_cond.slot = -1;
        block->unit_state_cond.expected_value = 1;
        const char *p = cond;
        while (*p) {
            int s_team = -1, s_type = -1, s_slot = -1;
            if (sscanf(p, " s(%d,%d,%d)==1", &s_team, &s_type, &s_slot) == 3 ||
                sscanf(p, "(s(%d,%d,%d))==1", &s_team, &s_type, &s_slot) == 3) {
                if (block->unit_state_cond.team < 0) {
                    block->unit_state_cond.team = s_team;
                    block->unit_state_cond.type = s_type;
                    block->unit_state_cond.slot = s_slot;
                }
            }
            p++;
        }
        return;
    }
    /* Detect c>s(team,type,slot) */
    if (sscanf(cond, "c>s(%d,%d,%d)", &team, &slot, &expected) == 3) {
        block->condition_kind = COND_COUNTER_GT_STATE;
        block->counter_gt_state_team = team;
        block->counter_gt_state_type = slot;
        block->counter_gt_state_slot = expected;
        return;
    }
}

static void parse_tro(ScriptState *script, const char *path) {
    char *text = load_text(path);
    if (!text) return;
    ScriptBlock *block = NULL;
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
            if (script->block_count < (int)(sizeof(script->blocks) / sizeof(script->blocks[0]))) {
                block = &script->blocks[script->block_count++];
                memset(block, 0, sizeof(*block));
                block->id = id;
                block->trip = strcmp(kind, "trip") == 0;
                block->c_gt = -1;
                block->trigger_x = -1;
                block->trigger_y = -1;
                block->condition_kind = block->trip ? COND_TRIP_PLAYER_NEAR : COND_UNSUPPORTED;
                block->condition_negated = (enabled == 0);
                block->cond_type = 0;
                if (sscanf(token, "%*d %*s %*d (c>%d)", &c_gt) == 1) {
                    block->c_gt = c_gt;
                    block->condition_kind = COND_COUNTER_GT;
                }
                else if (sscanf(token, "%*d %*s %*d (b(%d,%d)==%d)", &block->cond_a, &block->cond_b, &block->cond_c) == 3)
                    block->cond_type = 1;
                else if (sscanf(token, "%*d %*s %*d (s(%d,%d,%d)==%d)", &block->cond_a, &block->cond_d, &block->cond_b, &block->cond_c) == 4)
                    block->cond_type = 2;
                else if (sscanf(token, "%*d %*s %*d (c>s(%d,%d,%d))", &block->cond_a, &block->cond_d, &block->cond_b) == 3)
                    block->cond_type = 3;
                if (block->cond_type == 2) block->condition_kind = COND_STATE_ARRAY_EQ;
                else if (block->cond_type == 3) block->condition_kind = COND_COUNTER_GT_STATE;
                if (block->trip) block->requires_player_near = true;
                /* Parse condition from block header */
                const char *cond_start = strchr(token, '(');
                if (cond_start) {
                    /* Skip leading parentheses */
                    while (*cond_start == '(') cond_start++;
                    /* Find matching close paren */
                    const char *cond_end = cond_start + strlen(cond_start);
                    while (cond_end > cond_start && *(cond_end - 1) == ')') cond_end--;
                    char cond_buf[256];
                    size_t cond_len = (size_t)(cond_end - cond_start);
                    if (cond_len < sizeof(cond_buf)) {
                        memcpy(cond_buf, cond_start, cond_len);
                        cond_buf[cond_len] = '\0';
                        /* Strip inner parentheses */
                        char inner[256];
                        const char *src = cond_buf;
                        char *dst = inner;
                        while (*src && (size_t)(dst - inner) < sizeof(inner) - 1) {
                            if (*src != '(' && *src != ')') *dst++ = *src;
                            src++;
                        }
                        *dst = '\0';
                        parse_block_condition(block, inner);
                    }
                }
            }
        } else if (block && strcmp(token, "end") == 0) {
            block = NULL;
        } else if (block) {
            ScriptCommand cmd;
            memset(&cmd, 0, sizeof(cmd));
            int v[32] = { 0 };
            int parsed = 0;
            if (sscanf(token, "msg %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5) {
                cmd.type = SCRIPT_CMD_MSG;
                cmd.a[0] = v[2];
                script_add_command(block, cmd);
            } else if ((parsed = parse_command_ints(token, "reinforce2", v, 31)) >= 5) {
                add_reinforce_commands(block, SCRIPT_CMD_REINFORCE2, v, parsed);
            } else if ((parsed = parse_command_ints(token, "reinforce", v, 31)) >= 5) {
                add_reinforce_commands(block, SCRIPT_CMD_REINFORCE, v, parsed);
            } else if (sscanf(token, "newtype %d %d %d", &v[0], &v[1], &v[2]) == 3) {
                cmd.type = SCRIPT_CMD_NEWTYPE;
                cmd.a[0] = v[0];
                cmd.a[1] = v[1];
                cmd.a[2] = v[2];
                script_add_command(block, cmd);
                if (block->trigger_x < 0) {
                    block->trigger_x = v[0];
                    block->trigger_y = v[1];
                }
            } else if (sscanf(token, "bail %d %d", &v[0], &v[1]) == 2) {
                cmd.type = SCRIPT_CMD_BAIL;
                cmd.a[0] = v[0];
                cmd.a[1] = v[1];
                script_add_command(block, cmd);
            } else if (sscanf(token, "newrate %d %d %d", &v[0], &v[1], &v[2]) == 3) {
                cmd.type = SCRIPT_CMD_NEWRATE;
                cmd.a[0] = v[0];
                cmd.a[1] = v[1];
                cmd.a[2] = v[2];
                script_add_command(block, cmd);
            } else if (sscanf(token, "setarray %d c+%d", &v[0], &v[1]) == 2 ||
                       sscanf(token, "setarray %d (c+%d)", &v[0], &v[1]) == 2) {
                cmd.type = SCRIPT_CMD_SETARRAY;
                cmd.a[0] = v[0];
                cmd.a[1] = -1; /* expression: c+N */
                cmd.a[2] = v[1];
                script_add_command(block, cmd);
            } else if (sscanf(token, "setarray %d %d", &v[0], &v[1]) == 2) {
                cmd.type = SCRIPT_CMD_SETARRAY;
                cmd.a[0] = v[0];
                cmd.a[1] = 0; /* expression: literal */
                cmd.a[2] = v[1];
                script_add_command(block, cmd);
            } else if (sscanf(token, "setlifes %d %d", &v[0], &v[1]) == 2) {
                cmd.type = SCRIPT_CMD_SETLIFES;
                cmd.a[0] = v[0];
                cmd.a[1] = v[1];
                script_add_command(block, cmd);
            } else if (getenv("OPEN_RTS_DEBUG_SCRIPT")) {
                fprintf(stderr, "[dark-colony script] block %d: unknown command: %s\n",
                        block->id, token);
            }
        }
        line = next;
    }
    free(text);
}

static void load_city_slot_data(ScriptState *script, const char *map_path) {
    if (!script || !map_path) return;
    char scn_path[1024];
    replace_extension(scn_path, sizeof(scn_path), map_path, ".SCN");
    FILE *fp = fopen(scn_path, "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    if (size <= 0) { fclose(fp); return; }
    fseek(fp, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    if (!text) { fclose(fp); return; }
    if (fread(text, 1, (size_t)size, fp) != (size_t)size) {
        free(text); fclose(fp); return;
    }
    fclose(fp);
    text[size] = '\0';

    int current_team = -1;
    int team_count = 0;
    bool object_mode = false;
    int trailing_blanks = 0;
    char section[32] = { 0 };
    /* Temporary storage for team data */
    struct { int active; int race; int ai_slot_count; int ai_slots[2][2];
             int city_values[12]; int city_value_count; } teams[8] = { 0 };

    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char nl = *next; *next++ = '\0';
            if (nl == '\r' && *next == '\n') next++;
        }
        char token[256] = { 0 };
        trim_copy(token, sizeof(token), line);
        if (line == text || (line == text + 1 && token[0] != '\0')) {
            /* Skip header lines */
            line = next;
            continue;
        }
        if (token[0] == '\0') {
            if (team_count >= 8 && !object_mode && ++trailing_blanks >= 2) {
                object_mode = true;
                current_team = -1;
                section[0] = '\0';
            }
            line = next;
            continue;
        }
        trailing_blanks = 0;

        int team = -1, active = 0;
        if (sscanf(token, "TEAM %d %d", &team, &active) >= 1) {
            if (team >= 0 && team < 8) {
                current_team = team;
                teams[team].active = active != 0;
                if (team + 1 > team_count) team_count = team + 1;
            } else {
                current_team = -1;
            }
            object_mode = false;
            section[0] = '\0';
            line = next;
            continue;
        }

        if (token[0] == '%') {
            trim_copy(section, sizeof(section), token);
            line = next;
            continue;
        }

        if (!object_mode && current_team >= 0 && current_team < 8) {
            int values[32] = { 0 };
            int value_count = 0;
            const char *p = token;
            while (*p && value_count < 32) {
                while (isspace((unsigned char)*p)) p++;
                if (*p == '\0') break;
                char *end = NULL;
                long v = strtol(p, &end, 10);
                if (end == p) break;
                values[value_count++] = (int)v;
                p = end;
            }

            if (strcmp(section, "%Race") == 0) {
                if (value_count > 0) teams[current_team].race = values[0];
            } else if (strcmp(section, "%AISlots") == 0) {
                if (value_count >= 2 && teams[current_team].ai_slot_count < 2) {
                    int s = teams[current_team].ai_slot_count++;
                    teams[current_team].ai_slots[s][0] = values[0];
                    teams[current_team].ai_slots[s][1] = values[1];
                }
            } else if (strcmp(section, "%City") == 0) {
                if (teams[current_team].city_value_count == 0) {
                    teams[current_team].city_value_count = value_count > 12 ? 12 : value_count;
                    memcpy(teams[current_team].city_values, values,
                           (size_t)teams[current_team].city_value_count * sizeof(int));
                }
            }
            line = next;
            continue;
        }
        if (object_mode) break;
        line = next;
    }
    free(text);

    /* Extract city slot info for each team */
    script->city_slot_count = 0;
    for (int t = 0; t < 8; ++t) {
        if (!teams[t].active) continue;
        int anchor_x = 0, anchor_y = 0;
        if (teams[t].ai_slot_count >= 2) {
            anchor_x = teams[t].ai_slots[1][0];
            anchor_y = teams[t].ai_slots[1][1];
        }
        if (anchor_x == 0 && teams[t].ai_slot_count >= 1) {
            anchor_x = teams[t].ai_slots[0][0];
            anchor_y = teams[t].ai_slots[0][1];
        }
        if (anchor_x == 0) continue;
        for (int s = 0; s < 5 && script->city_slot_count < 32; ++s) {
            if (s < teams[t].city_value_count && teams[t].city_values[s * 2] > 0) {
                CitySlotInfo *info = &script->city_slots[script->city_slot_count++];
                info->team = t;
                info->slot = s;
                info->anchor_x = anchor_x;
                info->anchor_y = anchor_y;
                info->race = teams[t].race;
            }
        }
    }
}

ScriptState *DC_LoadScript(const char *map_path) {
    if (!map_path) return NULL;
    ScriptState *script = calloc(1, sizeof(*script));
    if (!script) return NULL;
    char msg_path[1024], tro_path[1024];
    replace_extension(msg_path, sizeof(msg_path), map_path, ".MSG");
    replace_extension(tro_path, sizeof(tro_path), map_path, ".TRO");
    parse_messages(script, msg_path);
    parse_tro(script, tro_path);
    load_city_slot_data(script, map_path);
    if (getenv("OPEN_RTS_DEBUG_SCRIPT")) {
        fprintf(stderr, "Dark Colony mission %s: %d messages, %d blocks, %d city slots\n",
                map_path, script->message_count, script->block_count, script->city_slot_count);
    }
    return script;
}

/* City slot offsets from DC.EXE - same as in w_map.c */
static const struct { int x; int z; } dc_city_slot_offsets[] = {
    { -64, 15 }, { 0, 0 }, { 32, 64 }, { 64, 10 }, { -32, 65 }, { 0, 32 }, { 0, 0 },
};

static fvec2_t city_slot_cell_center(const CitySlotInfo *slot_info) {
    if (!slot_info) return (fvec2_t){ 0.0f, 0.0f };
    int sx = 0, sz = 0;
    if (slot_info->slot >= 0 && slot_info->slot < 7) {
        sx = dc_city_slot_offsets[slot_info->slot].x;
        sz = dc_city_slot_offsets[slot_info->slot].z;
    }
    float cell_x = (float)slot_info->anchor_x + (float)sx * 8.0f / 256.0f;
    float cell_y = (float)slot_info->anchor_y + (float)sz * 8.0f / 256.0f;
    return fvec2_cell_center((ivec2_t){ (int)cell_x, (int)cell_y });
}

static bool building_alive_at_slot(const ScriptState *script, int team, int slot,
                                    const mobj_t *units, int unit_count) {
    fvec2_t expected = (fvec2_t){ 0.0f, 0.0f };
    bool found_slot = false;
    for (int i = 0; i < script->city_slot_count; ++i) {
        const CitySlotInfo *info = &script->city_slots[i];
        if (info->team == team && info->slot == slot) {
            expected = city_slot_cell_center(info);
            found_slot = true;
            break;
        }
    }
    if (!found_slot) return false;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *unit = &units[i];
        if (unit->remove || unit->hp <= 0) continue;
        if (unit->owner != (uint8_t)(team == 0 ? 0 : 1)) continue;
        fvec2_t pos = fixed3_xy_to_fvec2(unit->core.position);
        if (fvec2_near(pos, expected, 1.5f)) return true;
    }
    return false;
}

static bool evaluate_condition(const ScriptState *script, const ScriptBlock *block,
                                const mobj_t *units, int unit_count) {
    switch (block->condition_kind) {
    case COND_ALL_BUILDINGS_DESTROYED: {
        int team = block->building_cond.team;
        for (int i = 0; i < block->building_cond.slot_count; ++i) {
            if (building_alive_at_slot(script, team, block->building_cond.slots[i],
                                       units, unit_count))
                return false;
        }
        return block->building_cond.slot_count > 0;
    }
    case COND_UNIT_TYPE_EXISTS: {
        int team = block->unit_state_cond.team;
        int type = block->unit_state_cond.type;
        (void)type;
        for (int i = 0; i < unit_count; ++i) {
            const mobj_t *unit = &units[i];
            if (unit->remove || unit->hp <= 0) continue;
            if (team == 0 && unit->owner != 0) continue;
            if (team != 0 && unit->owner == 0) continue;
            if (unit->native_type_id == (uint16_t)type) return true;
        }
        return false;
    }
    case COND_COUNTER_GT:
        return block->c_gt >= 0 &&
               script->elapsed_ms > block->c_gt * SCRIPT_COUNTER_MS;
    case COND_STATE_ARRAY_EQ:
        if (block->cond_a >= 0 && block->cond_a < 8 &&
            block->cond_d >= 0 && block->cond_d < 128)
            return script->state_arrays[block->cond_a][block->cond_d] == block->cond_c;
        return false;
    case COND_COUNTER_GT_STATE:
        if (block->cond_a >= 0 && block->cond_a < 8 &&
            block->cond_d >= 0 && block->cond_d < 128)
            return script->elapsed_ms >
                   script->state_arrays[block->cond_a][block->cond_d] * SCRIPT_COUNTER_MS;
        return false;
    case COND_TRIP_PLAYER_NEAR:
        return true; /* trip is handled by caller before evaluate_condition */
    case COND_UNSUPPORTED:
        return false;
    }
    return false;
}

void DC_UpdateScript(ScriptState *script, level_t *map, mobj_t *units, int *unit_count,
                                effect_t *effects, int max_effects,
                                const gameinfo_t *game_info, hudtext_t *hud, float dt) {
    if (!script || !units || !unit_count) return;
    if (script->state != MISSION_ACTIVE) return;
    script->elapsed_ms += (int)(dt * 1000.0f);
    bool debug_script = getenv("OPEN_RTS_DEBUG_SCRIPT") != NULL;
    for (int i = 0; i < script->block_count; ++i) {
        ScriptBlock *block = &script->blocks[i];
        if (block->fired) continue;
        if (script->state != MISSION_ACTIVE) break;
        if (block->condition_kind == COND_UNSUPPORTED) continue;
        bool fire = false;
        if (block->trip) {
            fire = block->trigger_x >= 0 &&
                   player_near(map, units, *unit_count, block->trigger_x, block->trigger_y);
        } else {
            fire = evaluate_condition(script, block, units, *unit_count);
        }
        if (block->condition_negated) fire = !fire;
        if (fire) {
            if (debug_script) {
                fprintf(stderr, "Dark Colony script block %d fired (%d commands)\n",
                        block->id, block->command_count);
            }
            execute_script_block(script, block, map, units, unit_count,
                                             effects, max_effects, game_info, hud);
        }
    }
}

MissionState DC_ScriptState(const ScriptState *script) {
    return script ? script->state : MISSION_ACTIVE;
}

void DC_FreeScript(ScriptState *script) {
    free(script);
}
