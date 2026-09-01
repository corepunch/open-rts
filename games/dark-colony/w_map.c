#define _DEFAULT_SOURCE
#include "engine.h"
#include "info.h"
#include "gamestat.h"
#include "dc_types.h"
#include "w_spr.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

static void copy_trimmed_token(char *dst, size_t dst_size, const char *src, size_t len) {
    while (len > 0 && isspace((unsigned char)*src)) { src++; len--; }
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void uppercase_trimmed_token(char *dst, size_t dst_size, const char *src, size_t len) {
    while (len > 0 && isspace((unsigned char)*src)) { src++; len--; }
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;
    if (len >= dst_size) len = dst_size - 1;
    for (size_t i = 0; i < len; ++i) dst[i] = (char)toupper((unsigned char)src[i]);
    dst[len] = '\0';
}

static char *load_text_file(const char *path) {
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return NULL;
    char *text = malloc(blob.size + 1);
    if (!text) { W_FreeFile(&blob); return NULL; }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';
    W_FreeFile(&blob);
    return text;
}

enum {
    DARK_COLONY_SCN_MAX_TEAMS = 16,
    DARK_COLONY_SCN_AI_SLOTS = 2,
    DARK_COLONY_SCN_CITY_SLOTS = 7,
    DARK_COLONY_SCN_CITY_VALUES = 12,
    DARK_COLONY_SCN_CITY_ROWS = 9,
    DARK_COLONY_SCN_CITY_ROW_VALUES = 12,
    DARK_COLONY_SCN_LIST_VALUES = 32,
};

enum {
    DC_ALLEGIANCE_PLAYER,
    DC_ALLEGIANCE_ENEMY,
    DC_ALLEGIANCE_ALLIED,
};

typedef enum {
    DARK_COLONY_MAP_FILE_NONE,
    DARK_COLONY_MAP_FILE_MAP,
    DARK_COLONY_MAP_FILE_MTG,
} MapFileKind;

typedef struct {
    uint16_t background;
    uint16_t foreground;
    uint16_t flags;
    uint8_t mtg;
} MapCell;

typedef struct {
    MapFileKind kind;
    char path[1024];
    int width;
    int height;
    MapCell *cells;
    uint32_t *overview_colors;
    uint16_t *overview_words;
} MapFile;

typedef struct {
    int number;
    int active;
    int race;
    int money;
    int ai;
    int team_color_count;
    int team_colors[DARK_COLONY_SCN_LIST_VALUES];
    int depend_count;
    int depend[DARK_COLONY_SCN_LIST_VALUES];
    int allies_count;
    int allies[DARK_COLONY_SCN_LIST_VALUES];
    int ai_slot_count;
    int ai_slots[DARK_COLONY_SCN_AI_SLOTS][2];
    int city_value_count;
    int city_values[DARK_COLONY_SCN_CITY_VALUES];
    int city_row_count;
    int city_rows[DARK_COLONY_SCN_CITY_ROWS][DARK_COLONY_SCN_CITY_ROW_VALUES];
    int city_row_value_counts[DARK_COLONY_SCN_CITY_ROWS];
} ScenarioTeam;

typedef struct {
    int x;
    int y;
    int type;
    int team;
    int status;
    int extra;
    int value_count;
} ScenarioObject;

typedef struct {
    char path[1024];
    char tileset_file[32];
    char scenario_id[32];
    char display_name[64];
    int header_values[8];
    int header_value_count;
    ScenarioTeam teams[DARK_COLONY_SCN_MAX_TEAMS];
    int team_count;
    ScenarioObject *objects;
    int object_count;
} ScenarioFile;

typedef struct {
    MapFile map;
    ScenarioFile scenario;
    bool has_scenario;
} MapNative;

typedef struct {
    float speed;
    int max_health;
} UnitConfig;

static bool load_dark_colony_overview_colors(const char *path, size_t cell_count,
                                             uint32_t **colors_out);

static int parse_dark_colony_int_list(const char *token, int *out, int max_count) {
    int count = 0;
    const char *p = token;
    while (p && *p && count < max_count) {
        char *end = NULL;
        long value = strtol(p, &end, 10);
        if (end == p) break;
        out[count++] = (int)value;
        p = end;
        while (isspace((unsigned char)*p)) p++;
    }
    return count;
}

static void scenario_destroy(ScenarioFile *scenario) {
    if (!scenario) return;
    free(scenario->objects);
    memset(scenario, 0, sizeof(*scenario));
}

static void map_file_destroy(MapFile *map) {
    if (!map) return;
    free(map->cells);
    free(map->overview_colors);
    free(map->overview_words);
    memset(map, 0, sizeof(*map));
}

static void map_native_destroy(void *ptr) {
    MapNative *native = ptr;
    if (!native) return;
    map_file_destroy(&native->map);
    scenario_destroy(&native->scenario);
    free(native);
}

bool map_has_ai(const level_t *map, int owner) {
    if (!map || owner == 0 || !map->native_data) return false;
    const MapNative *native = map->native_data;
    if (!native->has_scenario) return false;
    for (int i = 0; i < native->scenario.team_count; ++i) {
        const ScenarioTeam *team = &native->scenario.teams[i];
        if (team->active && team->number == owner && team->ai > 0) return true;
    }
    return false;
}

static bool scenario_append_object(ScenarioFile *scenario,
                                               const ScenarioObject *object) {
    ScenarioObject *objects = realloc(
        scenario->objects, (size_t)(scenario->object_count + 1) * sizeof(*objects));
    if (!objects) return false;
    scenario->objects = objects;
    scenario->objects[scenario->object_count++] = *object;
    return true;
}

static bool scenario_load(const char *path, ScenarioFile *out) {
    memset(out, 0, sizeof(*out));
    char *text = load_text_file(path);
    if (!text) return false;
    snprintf(out->path, sizeof(out->path), "%s", path);

    int line_no = 0;
    int current_team = -1;
    int team_count = 0;
    bool object_mode = false;
    int trailing_blank_lines = 0;
    char section[32] = { 0 };
    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char nl = *next;
            *next++ = '\0';
            if (nl == '\r' && *next == '\n') next++;
        }
        line_no++;

        char token[256] = { 0 };
        copy_trimmed_token(token, sizeof(token), line, strlen(line));
        if (line_no == 1) {
            copy_trimmed_token(out->tileset_file, sizeof(out->tileset_file), line, strlen(line));
            line = next;
            continue;
        }
        if (line_no == 2) {
            copy_trimmed_token(out->scenario_id, sizeof(out->scenario_id), line, strlen(line));
            line = next;
            continue;
        }
        if (line_no == 3) {
            copy_trimmed_token(out->display_name, sizeof(out->display_name), line, strlen(line));
            line = next;
            continue;
        }
        if (line_no >= 4 && line_no <= 8) {
            int values[8] = { 0 };
            int count = parse_dark_colony_int_list(token, values, 8);
            for (int i = 0; i < count && out->header_value_count < 8; ++i)
                out->header_values[out->header_value_count++] = values[i];
            line = next;
            continue;
        }

        if (token[0] == '\0') {
            if (team_count >= 8 && !object_mode && ++trailing_blank_lines >= 2) {
                object_mode = true;
                current_team = -1;
                section[0] = '\0';
            }
            line = next;
            continue;
        }
        trailing_blank_lines = 0;

        int team = -1, active = 0;
        if (sscanf(token, "TEAM %d %d", &team, &active) >= 1) {
            if (team >= 0 && team < DARK_COLONY_SCN_MAX_TEAMS) {
                current_team = team;
                out->teams[team].number = team;
                out->teams[team].active = active != 0;
                if (team + 1 > out->team_count) out->team_count = team + 1;
                team_count++;
            } else {
                current_team = -1;
            }
            object_mode = false;
            section[0] = '\0';
            line = next;
            continue;
        }

        if (!object_mode && token[0] == '%') {
            copy_trimmed_token(section, sizeof(section), token, strlen(token));
            line = next;
            continue;
        }

        if (!object_mode && current_team >= 0 &&
            current_team < DARK_COLONY_SCN_MAX_TEAMS) {
            ScenarioTeam *team_info = &out->teams[current_team];
            int values[DARK_COLONY_SCN_LIST_VALUES] = { 0 };
            int value_count = parse_dark_colony_int_list(token, values,
                                                         DARK_COLONY_SCN_LIST_VALUES);
            if (section[0] == '\0') {
                if (value_count > 0) team_info->race = values[0];
            } else if (strcmp(section, "%Race") == 0) {
                if (value_count > 0) team_info->money = values[0];
            } else if (strcmp(section, "%Money") == 0) {
                if (value_count > 0) team_info->ai = values[0];
            } else if (strcmp(section, "%AI") == 0) {
                if (value_count > 0) {
                    team_info->team_color_count = value_count;
                    memcpy(team_info->team_colors, values, (size_t)value_count * sizeof(values[0]));
                }
            } else if (strcmp(section, "%TeamColour") == 0) {
                team_info->depend_count = value_count;
                memcpy(team_info->depend, values, (size_t)value_count * sizeof(values[0]));
            } else if (strcmp(section, "%Depend") == 0) {
                team_info->allies_count = value_count;
                memcpy(team_info->allies, values, (size_t)value_count * sizeof(values[0]));
            } else if (strcmp(section, "%TeamAllies") == 0) {
                /* Not currently used but parsed for completeness. */
            } else if (strcmp(section, "%AISlots") == 0) {
                if (value_count >= 2 && team_info->ai_slot_count < DARK_COLONY_SCN_AI_SLOTS) {
                    int slot = team_info->ai_slot_count++;
                    team_info->ai_slots[slot][0] = values[0];
                    team_info->ai_slots[slot][1] = values[1];
                }
            } else if (strcmp(section, "%City") == 0) {
                if (team_info->city_value_count == 0) {
                    team_info->city_value_count = value_count > DARK_COLONY_SCN_CITY_VALUES ?
                        DARK_COLONY_SCN_CITY_VALUES : value_count;
                    memcpy(team_info->city_values, values,
                           (size_t)team_info->city_value_count * sizeof(values[0]));
                } else if (team_info->city_row_count < DARK_COLONY_SCN_CITY_ROWS) {
                    int row = team_info->city_row_count++;
                    int n = value_count > DARK_COLONY_SCN_CITY_ROW_VALUES ?
                        DARK_COLONY_SCN_CITY_ROW_VALUES : value_count;
                    team_info->city_row_value_counts[row] = n;
                    memcpy(team_info->city_rows[row], values, (size_t)n * sizeof(values[0]));
                }
            }
            line = next;
            continue;
        }

        int values[6] = { 0 };
        int parsed = parse_dark_colony_int_list(token, values, 6);
        if (parsed >= 5) {
            ScenarioObject object = {
                .x = values[0],
                .y = values[1],
                .type = values[2],
                .team = values[3],
                .status = values[4],
                .extra = parsed >= 6 ? values[5] : 0,
                .value_count = parsed,
            };
            if (!scenario_append_object(out, &object)) {
                scenario_destroy(out);
                free(text);
                return false;
            }
        }
        line = next;
    }

    free(text);
    return true;
}

static bool map_file_load(const char *path, MapFile *out) {
    memset(out, 0, sizeof(*out));
    blob_t blob;
    char path_buf[1024];
    if (!W_ReadFile(path, &blob)) {
        const char *dot = strrchr(path, '.');
        if (dot && strcasecmp(dot, ".MAP") == 0) {
            replace_extension(path_buf, sizeof(path_buf), path, ".MTG");
            if (!W_ReadFile(path_buf, &blob)) return false;
            path = path_buf;
        } else {
            return false;
        }
    }

    bool ok = false;
    if (blob.size >= 8) {
        int width = read_i32_le(blob.bytes + 0);
        int height = read_i32_le(blob.bytes + 4);
        size_t cell_count = (size_t)width * (size_t)height;
        if (width > 0 && height > 0 && width <= 256 && height <= 256 &&
            blob.size >= 8 + cell_count * 6) {
            out->kind = DARK_COLONY_MAP_FILE_MAP;
            out->width = width;
            out->height = height;
            snprintf(out->path, sizeof(out->path), "%s", path);
            out->cells = calloc(cell_count, sizeof(*out->cells));
            if (!out->cells) {
                W_FreeFile(&blob);
                return false;
            }
            const uint8_t *tile_pairs = blob.bytes + 8;
            const uint8_t *tile_flags = blob.bytes + 8 + cell_count * 4;
            for (size_t i = 0; i < cell_count; ++i) {
                out->cells[i].background = read_u16_le(tile_pairs + i * 4);
                out->cells[i].foreground = read_u16_le(tile_pairs + i * 4 + 2);
                out->cells[i].flags = read_u16_le(tile_flags + i * 2);
            }
            ok = true;
        }
    }
    if (!ok && blob.size >= 2) {
        int width = blob.bytes[0];
        int height = blob.bytes[1];
        size_t cell_count = (size_t)width * (size_t)height;
        if (width > 0 && height > 0 && width <= 256 && height <= 256 &&
            blob.size >= 2 + cell_count) {
            out->kind = DARK_COLONY_MAP_FILE_MTG;
            out->width = width;
            out->height = height;
            snprintf(out->path, sizeof(out->path), "%s", path);
            out->cells = calloc(cell_count, sizeof(*out->cells));
            if (!out->cells) {
                W_FreeFile(&blob);
                return false;
            }
            for (size_t i = 0; i < cell_count; ++i)
                out->cells[i].mtg = blob.bytes[2 + i];
            ok = true;
        }
    }
    W_FreeFile(&blob);
    if (!ok) map_file_destroy(out);
    return ok;
}

static bool map_file_load_mtg_sidecar(MapFile *map) {
    if (!map || map->kind != DARK_COLONY_MAP_FILE_MAP || !map->cells) return false;
    char mtg_path[1024];
    replace_extension(mtg_path, sizeof(mtg_path), map->path, ".MTG");
    blob_t blob;
    if (!W_ReadFile(mtg_path, &blob)) return false;
    size_t cell_count = (size_t)map->width * (size_t)map->height;
    if (blob.size < 2 + cell_count || blob.bytes[0] != map->width ||
        blob.bytes[1] != map->height) {
        W_FreeFile(&blob);
        return false;
    }
    for (size_t i = 0; i < cell_count; ++i)
        map->cells[i].mtg = blob.bytes[2 + i];
    W_FreeFile(&blob);
    return true;
}

static bool map_file_load_overview(MapFile *map) {
    if (!map || map->width <= 0 || map->height <= 0) return false;
    char overview_path[1024];
    replace_extension(overview_path, sizeof(overview_path), map->path, ".OVH");
    size_t cell_count = (size_t)map->width * (size_t)map->height;
    if (!load_dark_colony_overview_colors(overview_path, cell_count, &map->overview_colors))
        return false;
    return true;
}

static bool map_file_load_o16(MapFile *map) {
    if (!map || map->width <= 0 || map->height <= 0) return false;
    char o16_path[1024];
    replace_extension(o16_path, sizeof(o16_path), map->path, ".O16");
    blob_t blob;
    if (!W_ReadFile(o16_path, &blob)) return false;
    size_t word_count = (size_t)map->width * (size_t)map->height * 2;
    if (blob.size < word_count * 2) {
        W_FreeFile(&blob);
        return false;
    }
    map->overview_words = calloc(word_count, sizeof(*map->overview_words));
    if (!map->overview_words) {
        W_FreeFile(&blob);
        return false;
    }
    for (size_t i = 0; i < word_count; ++i)
        map->overview_words[i] = read_u16_le(blob.bytes + i * 2);
    W_FreeFile(&blob);
    return true;
}

static bool append_dark_colony_resource_vent(level_t *map, int x, int y, int rate, int amount,
                                              const VentPlacement *placement) {
    if (!map || !L_Contains(map, x, y)) return false;
    if (amount <= 0) amount = 1;

    resourcevent_t *vents = realloc(map->resource_vents,
                                     (size_t)(map->resource_vent_count + 1) * sizeof(resourcevent_t));
    if (!vents) return false;
    map->resource_vents = vents;
    resourcevent_t *vent = &map->resource_vents[map->resource_vent_count++];
    vent->cell = (ivec2_t){ x, y };
    vent->attachment = (fvec2_t){ (float)x + 0.5f, (float)y - 0.5f };
    vent->amount = amount;
    vent->rate = rate;
    vent->active = rate > 0;
    vent->resource_type = 0;
    vent->decoration_index = -1;
    vent->smoke_decoration_index = -1;

    if (rate > 0 && map->decoration_count + 2 <= MAX_DECORATIONS) {
        mapdecoration_t *decorations = realloc(map->decorations,
                                             (size_t)(map->decoration_count + 2) * sizeof(mapdecoration_t));
        if (decorations) {
            map->decorations = decorations;
            mapdecoration_t *dec = &map->decorations[map->decoration_count++];
            memset(dec, 0, sizeof(*dec));
            dec->cell = (ivec2_t){ x, y };
            dec->footprint = (isize2_t){ 1, 1 };
            dec->center_anchor = true;
            if (placement && placement->valid) {
                dec->has_sprite_pivot = true;
                dec->sprite_pivot = (ivec2_t){ -placement->glow_left, -placement->glow_top };
            }
            dec->frame_index = 0;
            dec->render_flags = RTS_FRAME_ADDITIVE;
            snprintf(dec->sprite_name, sizeof(dec->sprite_name), "SPRITES/VENT2.SPR");
            vent->decoration_index = map->decoration_count - 1;

            dec = &map->decorations[map->decoration_count++];
            memset(dec, 0, sizeof(*dec));
            dec->cell = (ivec2_t){ x, y };
            dec->footprint = (isize2_t){ 1, 1 };
            dec->center_anchor = true;
            dec->has_sprite_pivot = true;
            dec->sprite_pivot = (ivec2_t){ 5, 4 };
            dec->frame_index = -1;
            if (placement && placement->valid) {
                dec->animation_frame_count = placement->smoke_frame_count;
                for (int i = 0; i < dec->animation_frame_count; ++i) {
                    dec->animation_frames[i].sprite_pivot = placement->smoke_frames[i].pivot;
                    dec->animation_frames[i].sprite_frame = placement->smoke_frames[i].sprite_frame;
                    dec->animation_frames[i].duration_ms = placement->smoke_frames[i].duration_ms;
                }
            }
            dec->render_flags = RTS_FRAME_ADDITIVE | RTS_FRAME_TINT_YELLOW;
            dec->render_selector = 5;
            snprintf(dec->sprite_name, sizeof(dec->sprite_name), "SPRITES/PUFF.SPR");
            vent->smoke_decoration_index = map->decoration_count - 1;
        }
    }
    return true;
}

static bool append_dark_colony_beacon(level_t *map, int x, int y, int type, int team) {
    if (!map || !L_Contains(map, x, y) || type != 84) return false;
    if (map->decoration_count >= MAX_DECORATIONS) return false;

    mapdecoration_t *decorations = realloc(map->decorations,
                                         (size_t)(map->decoration_count + 1) * sizeof(mapdecoration_t));
    if (!decorations) return false;
    map->decorations = decorations;
    mapdecoration_t *dec = &map->decorations[map->decoration_count++];
    memset(dec, 0, sizeof(*dec));
    dec->cell = (ivec2_t){ x, y };
    dec->footprint = (isize2_t){ 1, 1 };
    dec->center_anchor = true;
    dec->frame_index = 0;
    dec->frame2_index = 1;
    dec->render_remap = team >= 0 ? team : 0;
    dec->render2_flags = RTS_FRAME_ADDITIVE | RTS_FRAME_BLINK;
    dec->render2_selector = 5;
    snprintf(dec->sprite_name, sizeof(dec->sprite_name), "SPRITES/BEAC.SPR");
    snprintf(dec->sprite2_name, sizeof(dec->sprite2_name), "SPRITES/BEAC.SPR");
    return true;
}

static void load_dark_colony_resource_vents_from_scenario(const ScenarioFile *scenario,
                                                          level_t *map,
                                                          const VentPlacement *placement) {
    if (!scenario || !map) return;
    for (int i = 0; i < scenario->object_count; ++i) {
        const ScenarioObject *object = &scenario->objects[i];
        if (object->type == 40 && object->value_count >= 5) {
            append_dark_colony_resource_vent(map, object->x, object->y,
                                             object->team, object->status, placement);
        }
    }
}

static void load_dark_colony_beacons_from_scenario(const ScenarioFile *scenario,
                                                   level_t *map) {
    if (!scenario || !map) return;
    for (int i = 0; i < scenario->object_count; ++i) {
        const ScenarioObject *object = &scenario->objects[i];
        if (object->type == 84 && object->value_count >= 5) {
            append_dark_colony_beacon(map, object->x, object->y,
                                      object->type, object->team);
        }
    }
}

static void load_dark_colony_camera_from_scenario(const ScenarioFile *scenario,
                                                  level_t *map) {
    if (!scenario || !map || scenario->team_count <= 0) return;
    const ScenarioTeam *team = &scenario->teams[0];
    for (int i = 0; i < team->ai_slot_count; ++i) {
        int x = team->ai_slots[i][0];
        int y = team->ai_slots[i][1];
        if (x == 0 && y == 0) continue;
        map->has_camera = true;
        map->camera = fvec2_cell_center((ivec2_t){ x, y });
        return;
    }
}

static uint32_t rgb565_to_rgba(uint16_t value) {
    uint8_t r5 = (uint8_t)((value >> 11) & 0x1f);
    uint8_t g6 = (uint8_t)((value >> 5) & 0x3f);
    uint8_t b5 = (uint8_t)(value & 0x1f);
    uint8_t r = (uint8_t)((r5 << 3) | (r5 >> 2));
    uint8_t g = (uint8_t)((g6 << 2) | (g6 >> 4));
    uint8_t b = (uint8_t)((b5 << 3) | (b5 >> 2));
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static bool load_dark_colony_overview_colors(const char *path, size_t cell_count,
                                             uint32_t **colors_out) {
    blob_t blob;
    if (!colors_out || !W_ReadFile(path, &blob)) return false;
    if (blob.size < cell_count * 2) {
        W_FreeFile(&blob);
        return false;
    }
    uint32_t *colors = calloc(cell_count, sizeof(*colors));
    if (!colors) {
        W_FreeFile(&blob);
        return false;
    }
    for (size_t i = 0; i < cell_count; ++i) {
        colors[i] = rgb565_to_rgba(read_u16_le(blob.bytes + i * 2));
    }
    W_FreeFile(&blob);
    *colors_out = colors;
    return true;
}

bool load_dark_colony_map(const char *map_path, level_t *out) {
    memset(out, 0, sizeof(*out));

    MapNative *native = calloc(1, sizeof(*native));
    if (!native) return false;
    if (!map_file_load(map_path, &native->map)) {
        map_native_destroy(native);
        return false;
    }

    bool use_overview_colors = false;
    if (native->map.kind == DARK_COLONY_MAP_FILE_MTG) {
        size_t source_count = (size_t)native->map.width * (size_t)native->map.height;
        bool blank_mtg = true;
        for (size_t i = 0; i < source_count; ++i) {
            if (native->map.cells[i].mtg != 0) {
                blank_mtg = false;
                break;
            }
        }
        const char *dot = strrchr(native->map.path, '.');
        if (blank_mtg && dot && strcasecmp(dot, ".MTG") == 0) {
            char alt_path[1024];
            replace_extension(alt_path, sizeof(alt_path), native->map.path, ".MAP");
            MapFile alt_map;
            if (map_file_load(alt_path, &alt_map) &&
                alt_map.kind == DARK_COLONY_MAP_FILE_MAP) {
                map_file_destroy(&native->map);
                native->map = alt_map;
            } else {
                map_file_destroy(&alt_map);
                use_overview_colors = true;
            }
        }
    }

    map_file_load_mtg_sidecar(&native->map);
    map_file_load_o16(&native->map);
    if (map_file_load_overview(&native->map) &&
        (use_overview_colors || native->map.kind == DARK_COLONY_MAP_FILE_MTG)) {
        out->cell_colors = calloc((size_t)native->map.width * (size_t)native->map.height,
                                  sizeof(*out->cell_colors));
        if (!out->cell_colors) {
            map_native_destroy(native);
            return false;
        }
        for (int y = 0; y < native->map.height; ++y) {
            int source_y = native->map.height - 1 - y;
            for (int x = 0; x < native->map.width; ++x) {
                size_t dst = (size_t)y * (size_t)native->map.width + (size_t)x;
                size_t src = (size_t)source_y * (size_t)native->map.width + (size_t)x;
                out->cell_colors[dst] = native->map.overview_colors[src];
            }
        }
        out->render_capabilities |= MAP_RENDER_CAP_CELL_COLORS;
    }

    int width = native->map.width;
    int height = native->map.height;
    size_t source_count = (size_t)width * (size_t)height;
    out->width = width;
    out->height = height;
    out->render_capabilities |= MAP_RENDER_CAP_DEPTH_SORTED_TILE_LAYERS |
                                MAP_RENDER_CAP_TILE_TRANSFORMS;
    out->tile_ids        = calloc(source_count, sizeof(uint16_t));
    out->blocked         = calloc(source_count, sizeof(uint8_t));
    out->tile_overlay_count = 1;
    out->tile_overlays[0]   = calloc(source_count, sizeof(uint16_t));
    out->tile_transforms[0] = calloc(source_count, sizeof(uint8_t));
    out->tile_transforms[1] = calloc(source_count, sizeof(uint8_t));
    if (!out->tile_ids || !out->blocked ||
        !out->tile_overlays[0] || !out->tile_transforms[0] || !out->tile_transforms[1]) {
        map_native_destroy(native);
        P_FreeLevel(out);
        return false;
    }

    for (int y = 0; y < height; ++y) {
        int source_y = height - 1 - y;
        for (int x = 0; x < width; ++x) {
            size_t idx = (size_t)y * (size_t)width + (size_t)x;
            size_t source_idx = (size_t)source_y * (size_t)width + (size_t)x;
            const MapCell *cell = &native->map.cells[source_idx];
            if (native->map.kind == DARK_COLONY_MAP_FILE_MAP) {
                out->tile_ids[idx] = cell->background;
                out->tile_overlays[0][idx] = cell->foreground;
                out->blocked[idx] = (cell->flags & (1u << 9)) ? 1 : 0;
                out->tile_transforms[0][idx] =
                    (cell->flags & (1u << 5)) ? MAP_TILE_TRANSFORM_FLIP_X : 0;
                out->tile_transforms[1][idx] =
                    (cell->flags & (1u << 6)) ? MAP_TILE_TRANSFORM_FLIP_X : 0;
            } else {
                out->tile_ids[idx] = cell->mtg;
                out->tile_overlays[0][idx] = 0;
            }
        }
    }

    char scn_path[1024];
    replace_extension(scn_path, sizeof(scn_path), native->map.path, ".SCN");
    native->has_scenario = scenario_load(scn_path, &native->scenario);
    if (native->has_scenario) {
        VentPlacement vent_placement = {0};
        vent_placement_from_sprites(native->map.path, &vent_placement);
        char tileset_token[64] = { 0 };
        copy_trimmed_token(tileset_token, sizeof(tileset_token),
                           native->scenario.tileset_file,
                           strlen(native->scenario.tileset_file));
        char *dot = strrchr(tileset_token, '.');
        if (dot) *dot = '\0';
        uppercase_trimmed_token(out->tileset_name, sizeof(out->tileset_name),
                                tileset_token, strlen(tileset_token));
        load_dark_colony_camera_from_scenario(&native->scenario, out);
        load_dark_colony_resource_vents_from_scenario(&native->scenario, out, &vent_placement);
        load_dark_colony_beacons_from_scenario(&native->scenario, out);
        if (native->scenario.header_value_count > 3 &&
            native->scenario.header_values[3] > 0) {
            out->day_rate = native->scenario.header_values[3];
        }
        int team_count = native->scenario.team_count;
        if (team_count > 8) team_count = 8;
        for (int i = 0; i < team_count; ++i) {
            out->player_resources[i][0] = native->scenario.teams[i].money;
        }
    }

    const char *base = strrchr(native->map.path, '/');
    base = base ? base + 1 : native->map.path;
    if (out->tileset_name[0] == '\0') {
        if      (toupper((unsigned char)base[0]) == 'J') strncpy(out->tileset_name, "JUNGLE",   sizeof(out->tileset_name) - 1);
        else if (toupper((unsigned char)base[0]) == 'A') strncpy(out->tileset_name, "ATLANTIS", sizeof(out->tileset_name) - 1);
        else if (toupper((unsigned char)base[0]) == 'H') strncpy(out->tileset_name, "HTRAIN",   sizeof(out->tileset_name) - 1);
        else                                             strncpy(out->tileset_name, "DESERT",   sizeof(out->tileset_name) - 1);
    }

    out->native_data = native;
    out->destroy_native_data = map_native_destroy;
    return true;
}

enum { DARK_COLONY_MAX_GAMESTAT_UNITS = 128 };

static char ascii_lower_char(char c) {
    return (char)tolower((unsigned char)c);
}

static char *find_ascii_case_insensitive(char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0) return haystack;
    for (char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < needle_len && p[i] &&
               ascii_lower_char(p[i]) == ascii_lower_char(needle[i])) {
            i++;
        }
        if (i == needle_len) return p;
    }
    return NULL;
}

static float speed_from_gamestat(int speed) {
    return speed > 0 ? (float)speed / 32.0f : 0.0f;
}

static bool map_path_is_multiplayer(const char *map_path) {
    if (!map_path) return false;
    return find_ascii_case_insensitive((char *)map_path, "/MPLAYER/") ||
           find_ascii_case_insensitive((char *)map_path, "\\MPLAYER\\");
}

static void load_dark_colony_unit_config(UnitConfig configs[DARK_COLONY_MAX_GAMESTAT_UNITS]) {
    memset(configs, 0, sizeof(UnitConfig) * DARK_COLONY_MAX_GAMESTAT_UNITS);

    int count = GAMESTAT_UNIT_COUNT;
    if (count > DARK_COLONY_MAX_GAMESTAT_UNITS) count = DARK_COLONY_MAX_GAMESTAT_UNITS;
    for (int i = 0; i < count; ++i) {
        const DcGamestatUnit *unit = &dc_gamestat_units[i];
        if (unit->value_count > GAMESTAT_UNIT_SPEED)
            configs[i].speed = speed_from_gamestat(
                unit->values[GAMESTAT_UNIT_SPEED]);
        if (unit->value_count > GAMESTAT_UNIT_HEALTH) {
            configs[i].max_health = unit->values[GAMESTAT_UNIT_HEALTH];
        }
    }
}

static int fixed_from_cell(int cell) {
    return cell * 256 + FIXED_TILE_CENTER;
}

static int fixed_from_city_x(int cell) {
    return cell * 256;
}

static float fixed_to_cell(int fixed) {
    return (float)fixed / 256.0f;
}

static int default_health_for_type(int type,
                                               const UnitConfig *unit_config) {
    if (type >= 0 && type < DARK_COLONY_MAX_GAMESTAT_UNITS && unit_config &&
        unit_config[type].max_health > 0) {
        return unit_config[type].max_health;
    }
    return 1;
}

static int initial_health_for_type(int type, int scenario_health,
                                               const UnitConfig *unit_config) {
    return scenario_health >= 0 ? scenario_health :
        default_health_for_type(type, unit_config);
}

static void object_pool_init(DcObjectPool *pool) {
    if (!pool) return;
    memset(pool, 0, sizeof(*pool));
    pool->object_limit = DYNAMIC_OBJECT_FIRST;
}

static void object_pool_mark_active(DcObjectPool *pool, int object_index) {
    if (!pool || object_index < 0 || object_index >= MAX_OBJECTS) return;
    if (pool->active_count < MAX_OBJECTS)
        pool->active_objects[pool->active_count++] = (uint16_t)object_index;
    if (object_index >= pool->object_limit)
        pool->object_limit = object_index + 1;
}

static bool object_pool_init_object(DcObjectPool *pool, int object_index,
                                                int x_fixed, int z_fixed,
                                                int type, int team, int health,
                                                int subtype) {
    if (!pool || object_index < 0 || object_index >= MAX_OBJECTS ||
        x_fixed < 0 || z_fixed < 0 || team < 0 || team >= 10 ||
        type < 0 || type > 255) {
        return false;
    }

    DcObject *object = &pool->objects[object_index];
    memset(object, 0, sizeof(*object));
    object->x_pos = (int16_t)x_fixed;
    object->z_pos = (int16_t)z_fixed;
    object->type = (uint8_t)type;
    object->team = (uint8_t)team;
    object->regen_timer = 0x40;
    object->health_or_amount = health;
    object->active = 1;
    object->facing_or_anim = 0xff;
    object->subtype = (uint8_t)subtype;
    object->cell_x = (uint8_t)(x_fixed >> 8);
    object->cell_z = (uint8_t)(z_fixed >> 8);
    object->marker_d1 = 0xff;
    object->target_a = -2;
    object->target_b = -2;
    object_pool_mark_active(pool, object_index);
    return true;
}

static int city_object_index(int team, int slot) {
    if (team < 0 || team >= 8 || slot < 0 || slot >= BUILDINGS_PER_SIDE)
        return -1;
    return team * BUILDINGS_PER_SIDE + slot;
}

static int object_pool_alloc_dynamic(DcObjectPool *pool) {
    if (!pool) return -1;
    int limit = pool->object_limit;
    if (limit < DYNAMIC_OBJECT_FIRST) limit = DYNAMIC_OBJECT_FIRST;
    for (int i = DYNAMIC_OBJECT_FIRST; i < limit; ++i) {
        if (pool->objects[i].active == 0) return i;
    }
    if (limit >= MAX_OBJECTS) return -1;
    pool->object_limit = limit + 1;
    return limit;
}

static int object_pool_add_dynamic(DcObjectPool *pool, int x, int z,
                                               int type, int team, int scenario_health,
                                               int subtype,
                                               const UnitConfig *unit_config) {
    int object_index = object_pool_alloc_dynamic(pool);
    if (object_index < 0) {
        fprintf(stderr, "[dark-colony] cannot allocate dynamic object type=%d team=%d at (%d,%d)\n",
                type, team, x, z);
        return -1;
    }
    int health = initial_health_for_type(type, scenario_health, unit_config);
    if (!object_pool_init_object(pool, object_index,
                                             fixed_from_cell(x),
                                             fixed_from_cell(z),
                                             type, team, health, subtype)) {
        fprintf(stderr, "[dark-colony] cannot initialize dynamic object index=%d type=%d team=%d\n",
            object_index, type, team);
        return -1;
    }
    return object_index;
}

static bool object_pool_add_city_slot(DcObjectPool *pool, int team, int slot,
                                                  int x_fixed, int z_fixed, int type, int race,
                                                  const UnitConfig *unit_config) {
    int object_index = city_object_index(team, slot);
    if (object_index < 0 || type <= 0) {
        fprintf(stderr, "[dark-colony] cannot add city slot team=%d slot=%d type=%d\n",
                team, slot, type);
        return false;
    }
    int health = default_health_for_type(type, unit_config);
    (void)race;
    if (!object_pool_init_object(pool, object_index, x_fixed, z_fixed,
                                 type, team, health, 0)) {
        fprintf(stderr, "[dark-colony] cannot initialize city slot team=%d slot=%d type=%d index=%d\n",
                team, slot, type, object_index);
        return false;
    }
    return true;
}

static bool scenario_object_starts_visible(const ScenarioObject *object) {
    return object != NULL && object->status >= 0;
}

static int mobj_type_for_type(int type, int race) {
    switch (type) {
        case 16: return MT_DC_EXCOPOD;
        case 17: return MT_DC_BRRKPOD;
        case 18: return MT_DC_ROBOPOD;
        case 19: return MT_DC_ROBOPOD2;
        case 20: return MT_DC_SCNCPOD;
        case 21: return MT_DC_SCNCPOD2;
        case 22: return MT_DC_RSCHPOD;
        case 28: return MT_DC_ALIEN_MINDHIVE;
        case 29: return MT_DC_ALIEN_WARHIVE;
        case 30: return MT_DC_ALIEN_BRDRHIVE;
        case 31: return MT_DC_ALIEN_BRDRHIVE2;
        case 32: return MT_DC_ALIEN_MINDHIVE2;
        case 33: return MT_DC_ALIEN_MINDHIVE3;
        case 34: return MT_DC_ALIEN_RSCHIVE;
        case 41: return MT_DC_MOBILE_TOWER;
        case 81: return MT_DC_CITY_TOWER;
        case 86: return MT_DC_COMMS_DISH;
        case 89: return MT_DC_DROP_LINK;
        case 91: return MT_DC_ALIEN_COM;
        case 94: return MT_DC_VISION_SIGHT;
        default: break;
    }

    if (race == 1) {
        if (type == 0 || type == 8 || (type >= 69 && type <= 76)) return MT_DC_GREY;
        if (type == 13) return MT_DC_ORTU;
        if (type == 14) return MT_DC_SLUG;
        return 0;
    }

    if (type == 0 || (type >= 69 && type <= 72)) return MT_DC_TROOPER;
    switch (type) {
        case 2: return MT_DC_REAPER;
        case 3: return MT_DC_THUNDERBOLT;
        case 4: return MT_DC_CYBORG;
        case 5: return MT_DC_SCOUT;
        case 6: return MT_DC_EXPLOITER;
        default: return 0;
    }
}

static const char *unit_sprite_for_type(int type, int race) {
    if (type == 16 || type == 17) return "SPRITES/HUBU.SPR";
    if (type >= 18 && type <= 22) return "SPRITES/SHORTCIT.SPR";
    if (type >= 28 && type <= 34) return "SPRITES/ALBU.SPR";
    if (type == 41) return "SPRITES/TURR.SPR";
    if (type == 81) return "SPRITES/TOWR.SPR";
    if (type == 86) return "SPRITES/DISH.SPR";
    if (type == 89) return "SPRITES/CENT.SPR";
    if (type == 91) return "SPRITES/TONG.SPR";
    if (type == 94) return "SPRITES/DOTT.SPR";
    if (race == 1) {
        if (type == 0 || (type >= 69 && type <= 72)) return "SPRITES/GRAY.SPR";
        if (type == 6) return "SPRITES/SLUG.SPR";
    } else {
        if (type == 0 || (type >= 69 && type <= 72)) return "SPRITES/TRSC.SPR";
        if (type == 6) return "SPRITES/EXPL.SPR";
    }
    switch (type) {
        case  2: return "SPRITES/REAP.SPR";
        case  3: return "SPRITES/BARR.SPR";
        case  4: return "SPRITES/SARG.SPR";
        case  5: return "SPRITES/SCGM.SPR";
        case  8: return "SPRITES/GRAY.SPR";
        case  9: return "SPRITES/XENO.SPR";
        case 10: return "SPRITES/SCYT.SPR";
        case 11: return "SPRITES/ATRIL.SPR";
        case 12: return "SPRITES/PSYC.SPR";
        case 13: return "SPRITES/ORTU.SPR";
        case 14: return "SPRITES/SLUG.SPR";
        case 15: return "SPRITES/ATRIL.SPR";
        case 43: return "SPRITES/ENGI.SPR";
        case 44: return "SPRITES/SLOM.SPR";
        case 49: return "SPRITES/BEON.SPR";
        case 50: return "SPRITES/ZISP.SPR";
        case 73: case 74: case 75: case 76: return "SPRITES/GRAY.SPR";
        case 77: return "SPRITES/SARG.SPR";
        case 78: return "SPRITES/PSYC.SPR";
        default: return NULL;
    }
}

static int unit_frame_for_type(int type) {
    switch (type) {
        case 16: return 0; /* HUBU.FIN EXCOPODSTAND0 */
        case 17: return 4; /* HUBU.FIN BRRKPODSTAND0 */
        case 18: return 1; /* ROBOTICSSTAND0 */
        case 19: return 1; /* ROBOPOD2 reuses robotics art. */
        case 20: return 2; /* SCIENCESTAND0 */
        case 21: return 2; /* SCNCPOD2 reuses science art. */
        case 22: return 4; /* HUMRESSTAND0 */
        case 41: return 0; /* TURR */
        case 81: return 0; /* TOWR.FIN TOWRSTAND0 */
        case 89: return 0; /* CENT */
        case 91: return 0; /* TONG */
        case 94: return 0; /* DOTT */
        default: break;
    }
    if (type >= 28 && type <= 34) return type - 28;
    return 0;
}

static int unit_state_for_type(int type) {
    switch (type) {
        case 16: return S_DC_EXCOPOD_STND;
        case 17: return S_DC_BRRKPOD_STND;
        case 81: return S_DC_TOWR_STND;
        case 28: return S_DC_ALIEN_MINDHIVE_STND;
        case 29: return S_DC_ALIEN_WARHIVE_STND;
        case 30: return S_DC_ALIEN_BRDRHIVE_STND;
        case 31: return S_DC_ALIEN_BRDRHIVE2_STND;
        case 32: return S_DC_ALIEN_MINDHIVE2_STND;
        case 33: return S_DC_ALIEN_MINDHIVE3_STND;
        case 34: return S_DC_ALIEN_RSCHIVE_STND;
        default: return S_NULL;
    }
}

static int city_unit_type_for_slot(int race, int slot) {
    static const int city_types[2][15] = {
        { 16, 17, 18, 20, 22, 81, 25, 25, 25, 25, 25, 25, 25, 0, 0 },
        { 28, 29, 30, 32, 34, 81, 25, 25, 25, 25, 25, 25, 25, 0, 0 },
    };
    if (slot < 0 || slot >= 15) return 0;
    return city_types[race == 1 ? 1 : 0][slot];
}

static void city_slot_offset(int slot, int *x_out, int *z_out) {
    static const int slot_offsets[][2] = {
        { -64, 15 },
        {   0,  0 },
        {  32, 64 },
        {  64, 10 },
        { -32, 65 },
        {   0, 32 },
        {   0,  0 },
    };
    int x = 0, z = 0;
    if (slot >= 0 && slot < (int)(sizeof(slot_offsets) / sizeof(slot_offsets[0]))) {
        x = slot_offsets[slot][0];
        z = slot_offsets[slot][1];
    }
    if (x_out) *x_out = x;
    if (z_out) *z_out = z;
}

static void city_slot_position_fixed(int anchor_x, int anchor_y, int slot,
                                                 int *x_fixed, int *z_fixed) {
    /* DC.EXE city.c stores city object positions as 8.8 fixed-point map units.
       fcn.004412d4 writes:
           x_pos = city_x * 0x100 + dc_city_slot_offsets[slot].x * 8
           z_pos = city_z * 0x100 + dc_city_slot_offsets[slot].z * 8 */
    int slot_x = 0, slot_z = 0;
    city_slot_offset(slot, &slot_x, &slot_z);
    int fx = fixed_from_city_x(anchor_x);
    int fz = fixed_from_city_x(anchor_y);
    fx += slot_x * 8;
    fz += slot_z * 8;
    if (x_fixed) *x_fixed = fx;
    if (z_fixed) *z_fixed = fz;
}

static bool team_city_anchor(const ScenarioTeam *team,
                                         int *x_out, int *y_out) {
    if (!team || team->ai_slot_count < 1)
        return false;
    /* DC.EXE scenario.c stores the city anchor as the second %AISlots pair
       (team +0x2c/+0x30).  Allied AI teams such as Aerogen in HUMAN03 have
       (0,0) as the second pair; fall back to the first %AISlots pair so their
       city slots are still materialized at the team's start position. */
    int x = team->ai_slots[1][0];
    int y = team->ai_slots[1][1];
    if (x == 0 && team->ai_slot_count >= 2) {
        x = team->ai_slots[0][0];
        y = team->ai_slots[0][1];
    }
    if (x == 0)
        return false;
    if (x_out) *x_out = x;
    if (y_out) *y_out = y;
    return true;
}

static bool object_uses_city_render_origin(int object_index) {
    return object_index >= 0 && object_index < BUILDING_OBJECT_COUNT &&
        object_index % BUILDINGS_PER_SIDE < 6;
}

static void object_render_position_fixed(const DcObject *object, int object_index,
                                                      int *x_fixed, int *z_fixed) {
    int x = object ? object->x_pos : 0;
    int z = object ? object->z_pos : 0;
    if (object_uses_city_render_origin(object_index)) {
        int slot = object_index % BUILDINGS_PER_SIDE;
        int slot_x = 0, slot_z = 0;
        city_slot_offset(slot, &slot_x, &slot_z);
        x -= slot_x * 8;
        z -= slot_z * 8;
    }
    if (x_fixed) *x_fixed = x;
    if (z_fixed) *z_fixed = z;
}

static void compute_team_allegiances(const ScenarioFile *scenario,
                                     int allegiances[DARK_COLONY_SCN_MAX_TEAMS]) {
    for (int i = 0; i < DARK_COLONY_SCN_MAX_TEAMS; ++i)
        allegiances[i] = DC_ALLEGIANCE_ENEMY;
    for (int i = 0; i < scenario->team_count; ++i) {
        const ScenarioTeam *team = &scenario->teams[i];
        if (!team->active) continue;
        if (i == 0) {
            allegiances[i] = DC_ALLEGIANCE_PLAYER;
            continue;
        }
        if (team->race == 1) {
            allegiances[i] = DC_ALLEGIANCE_ENEMY;
            continue;
        }
        bool depends_on_player = false;
        for (int j = 0; j < team->allies_count && j < DARK_COLONY_SCN_MAX_TEAMS; ++j) {
            if (j == 0 && team->allies[j] == 1) {
                depends_on_player = true;
                break;
            }
        }
        allegiances[i] = depends_on_player ? DC_ALLEGIANCE_ALLIED : DC_ALLEGIANCE_ENEMY;
    }
}

static bool append_dark_colony_object_unit(mobj_t *units, int *count, int max_units,
                                            int object_index,
                                            const DcObject *object, int race,
                                            int scenario_team,
                                            int team_allegiance,
                                            const UnitConfig *unit_config,
                                            bool *player_selected,
                                            bool *player_has_exploiter,
                                            bool *player_anchor_set,
                                            int *player_anchor_x,
                                            int *player_anchor_y,
                                            bool hidden) {
    if (!units || !count || *count >= max_units || !object || object->active == 0) {
        return false;
    }
    int type = object->type;
    const char *sprite = unit_sprite_for_type(type, race);
    int mobj_type = mobj_type_for_type(type, race);
    if (!sprite || mobj_type <= 0) {
        return false;
    }

    mobj_t *u = &units[*count];
    memset(u, 0, sizeof(*u));
    u->hidden = hidden;
    int render_x_pos = 0, render_z_pos = 0;
    object_render_position_fixed(object, object_index,
                                             &render_x_pos, &render_z_pos);
    u->core.position = fixedvec3_from_fvec2((fvec2_t){
        fixed_to_cell(render_x_pos),
        fixed_to_cell(render_z_pos),
    }, 0);
    u->core.sprite_id = -1;
    u->attack.target = -1;
    u->harvest.target = -1;
    if (type >= 0 && type < DARK_COLONY_MAX_GAMESTAT_UNITS && unit_config)
        u->speed = unit_config[type].speed;
    if (mobj_type == MT_DC_EXPLOITER) u->speed = 3.5f;
    u->type_id = (uint16_t)mobj_type;
    u->native_type_id = (uint16_t)(type >= 0 ? type : 0);
    if (object_uses_city_render_origin(object_index))
        u->render_sort_y = fixed_to_cell(object->z_pos);
    u->owner = (team_allegiance == DC_ALLEGIANCE_PLAYER || mobj_type == MT_DC_COMMS_DISH) ? 0 :
               (team_allegiance == DC_ALLEGIANCE_ALLIED ? 2 : 1);
    u->team = (uint8_t)(scenario_team >= 0 ? scenario_team : 0);
    u->allegiance = (uint8_t)(team_allegiance == DC_ALLEGIANCE_PLAYER ? ALLEGIANCE_PLAYER :
                              team_allegiance == DC_ALLEGIANCE_ALLIED ? ALLEGIANCE_ALLIED :
                              ALLEGIANCE_ENEMY);
    u->hp = object->health_or_amount;
    u->selected = u->owner == 0 && mobj_type != MT_DC_COMMS_DISH &&
        (mobj_type < MT_DC_BUILDING_BASE) && player_selected && !*player_selected;
    if (u->selected) *player_selected = true;
    u->core.frame = unit_frame_for_type(type);
    snprintf(u->core.sprite_name, sizeof(u->core.sprite_name), "%s", sprite);
    statecontext_t ctx = { .game_info = &game_info };
    int state_id = unit_state_for_type(type);
    if (state_id == S_NULL && u->type_id > 0 && u->type_id < game_info.mobj_type_count)
        state_id = game_info.mobjinfo[u->type_id].spawnstate;
    if (state_id != S_NULL && !P_SetMobjState(&ctx, u, state_id)) {
        fprintf(stderr, "[dark-colony] state setup failed for object index=%d native_type=%d "
                "mobj=%d state=%d sprite=%s\n",
                object_index, type, mobj_type, state_id, sprite);
        return false;
    }
    if (object_uses_city_render_origin(object_index))
        u->core.render_offset.y += CELL_H;
    if (u->owner == 0) {
        int x = object->cell_x;
        int y = object->cell_z;
        if (player_anchor_set && player_anchor_x && player_anchor_y &&
            (!*player_anchor_set || x > *player_anchor_x)) {
            *player_anchor_x = x;
            *player_anchor_y = y;
            *player_anchor_set = true;
        }
        if (player_has_exploiter && mobj_type == MT_DC_EXPLOITER)
            *player_has_exploiter = true;
    }
    (*count)++;
    return true;
}

int load_dark_colony_initial_units(const char *map_path, mobj_t *units, int max_units) {
    char scn_path[1024];
    replace_extension(scn_path, sizeof(scn_path), map_path, ".SCN");
    ScenarioFile scenario;
    if (!scenario_load(scn_path, &scenario)) {
        fprintf(stderr, "[dark-colony] cannot load scenario %s\n", scn_path);
        return 0;
    }

    UnitConfig unit_config[DARK_COLONY_MAX_GAMESTAT_UNITS];
    load_dark_colony_unit_config(unit_config);
    DcObjectPool object_pool;
    object_pool_init(&object_pool);

    int team_allegiances[DARK_COLONY_SCN_MAX_TEAMS];
    compute_team_allegiances(&scenario, team_allegiances);

    int count = 0;
    bool player_has_exploiter = false;
    bool player_anchor_set = false;
    bool player_selected = false;
    int player_anchor_x = 0;
    int player_anchor_y = 0;
    bool alien_has_slug = false;
    bool alien_anchor_set = false;
    int alien_anchor_x = 0;
    int alien_anchor_y = 0;

    for (int team = 0; team < scenario.team_count; ++team) {
        const ScenarioTeam *team_info = &scenario.teams[team];
        if (!team_info->active) continue;
        int slot_x = 0;
        int slot_y = 0;
        if (!team_city_anchor(team_info, &slot_x, &slot_y)) {
            fprintf(stderr, "[dark-colony] active team %d has no city anchor; city slots not loaded\n",
                team);
            continue;
        }
        if (team_info->race == 1 && !alien_anchor_set) {
            alien_anchor_x = slot_x;
            alien_anchor_y = slot_y;
            alien_anchor_set = true;
        }
        for (int slot = 0; slot < DARK_COLONY_SCN_CITY_SLOTS; ++slot) {
            int value_index = slot * 2;
            if (value_index >= team_info->city_value_count) break;
            if (team_info->city_values[value_index] <= 0) continue;
            int type = city_unit_type_for_slot(team_info->race, slot);
            int x_fixed = 0, z_fixed = 0;
            city_slot_position_fixed(slot_x, slot_y, slot,
                                                 &x_fixed, &z_fixed);
                if (!object_pool_add_city_slot(&object_pool, team, slot,
                               x_fixed, z_fixed, type,
                               team_info->race, unit_config)) {
                fprintf(stderr, "[dark-colony] city object not loaded team=%d slot=%d native_type=%d\n",
                    team, slot, type);
                }
        }
        if (team_info->race == 1 && team_info->city_values[0] > 0 &&
            team_info->city_values[10] <= 0) {
            int tower_x = 0, tower_z = 0;
            city_slot_position_fixed(slot_x, slot_y, 5, &tower_x, &tower_z);
            if (object_pool_add_city_slot(&object_pool, team, 5,
                                          tower_x, tower_z, 81,
                                          team_info->race, unit_config)) {
                fprintf(stderr, "[dark-colony] synthesized alien city tower team=%d slot=5 from active base slot\n",
                        team);
            }
        }
    }

    bool pool_hidden[MAX_OBJECTS] = { false };
    for (int i = 0; i < scenario.object_count; ++i) {
        const ScenarioObject *object = &scenario.objects[i];
        if (object->type == OBJECT_TYPE_PETRA7_VENT || object->type == 84)
            continue;
        if (object->value_count < 5 ||
            object->x < 0 || object->y < 0) {
            continue;
        }
        bool hidden = !scenario_object_starts_visible(object);
        int pool_index = object_pool_add_dynamic(&object_pool, object->x, object->y,
                                            object->type, object->team,
                                            object->status, object->extra,
                                            unit_config);
        if (pool_index >= 0 && hidden) {
            pool_hidden[pool_index] = true;
        }
    }

    for (int i = 0; i < object_pool.active_count && count < max_units; ++i) {
        int object_index = object_pool.active_objects[i];
        const DcObject *object = &object_pool.objects[object_index];
        bool hidden = pool_hidden[object_index];
        int team = object->team;
        int race = team >= 0 && team < DARK_COLONY_SCN_MAX_TEAMS ?
            scenario.teams[team].race : 0;
        int allegiance = team >= 0 && team < DARK_COLONY_SCN_MAX_TEAMS ?
            team_allegiances[team] : DC_ALLEGIANCE_ENEMY;
        if (race == 1 && object->type == 14)
            alien_has_slug = true;
        if (race != 1 && object->type == 16 && count < max_units) {
            DcObject tower = *object;
            tower.type = 81;
            append_dark_colony_object_unit(units, &count, max_units, object_index,
                                           &tower, race, team, allegiance,
                                           unit_config,
                                           NULL, NULL, NULL, NULL, NULL,
                                           hidden);
        }
        append_dark_colony_object_unit(units, &count, max_units, object_index, object, race,
                                       team, allegiance,
                                       unit_config,
                                       &player_selected,
                                       &player_has_exploiter,
                                       &player_anchor_set,
                                       &player_anchor_x,
                                       &player_anchor_y,
                                       hidden);
    }
    if (map_path_is_multiplayer(map_path) && !player_has_exploiter &&
        player_anchor_set && count < max_units) {
        int object_index = object_pool_add_dynamic(&object_pool, player_anchor_x + 2,
                                                                player_anchor_y, 6, 0, -1, 0,
                                                                unit_config);
        if (object_index >= 0) {
            const DcObject *object = &object_pool.objects[object_index];
            append_dark_colony_object_unit(units, &count, max_units, object_index, object, 0,
                                           0, DC_ALLEGIANCE_PLAYER,
                                           unit_config, &player_selected,
                                           &player_has_exploiter,
                                           &player_anchor_set,
                                           &player_anchor_x,
                                           &player_anchor_y,
                                           false);
        }
    }
    if (map_path_is_multiplayer(map_path) && !alien_has_slug &&
        alien_anchor_set && count < max_units) {
        int object_index = object_pool_add_dynamic(&object_pool, alien_anchor_x + 2,
                                                                alien_anchor_y, 14, 1, -1, 0,
                                                                unit_config);
        if (object_index >= 0) {
            const DcObject *object = &object_pool.objects[object_index];
            append_dark_colony_object_unit(units, &count, max_units, object_index, object, 1,
                                           1, DC_ALLEGIANCE_ENEMY,
                                           unit_config, NULL, NULL, NULL, NULL, NULL,
                                           false);
        }
    }
    scenario_destroy(&scenario);
    return count;
}
