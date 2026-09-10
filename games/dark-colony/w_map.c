#define _GNU_SOURCE
#include "engine.h"
#include "game.h"
#include "info.h"
#include "gamestat.h"
#include "dc_types.h"

#include <ctype.h>
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

enum {
    DARK_COLONY_SCN_MAX_TEAMS = 16,
    DARK_COLONY_SCN_AI_SLOTS = 2,
    DARK_COLONY_SCN_CITY_SLOTS = 7,
    DARK_COLONY_SCN_CITY_VALUES = 12,
    DARK_COLONY_SCN_LIST_VALUES = 32,
};

enum {
    DC_ALLEGIANCE_PLAYER,
    DC_ALLEGIANCE_ENEMY,
    DC_ALLEGIANCE_ALLIED,
};

typedef struct {
    blob_t file;
    char path[1024];
    isize2_t size;
    bool terrain;
} MapFile;

typedef struct {
    int number;
    int active;
    int race;
    int money;
    int ai;
    int allies_count;
    int allies[DARK_COLONY_SCN_LIST_VALUES];
    int ai_slot_count;
    ivec2_t ai_slots[DARK_COLONY_SCN_AI_SLOTS];
    int city_value_count;
    int city_values[DARK_COLONY_SCN_CITY_VALUES];
} ScenarioTeam;

typedef struct {
    ivec2_t cell;
    int type;
    int team;
    int status;
} ScenarioObject;

typedef struct {
    char tileset_file[32];
    int header_values[8];
    int header_value_count;
    ScenarioTeam teams[DARK_COLONY_SCN_MAX_TEAMS];
    int team_count;
    ScenarioObject *objects;
    int object_count;
} ScenarioFile;

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

static void scenario_free(void *ptr) {
    ScenarioFile *scenario = ptr;
    if (!scenario) return;
    scenario_destroy(scenario);
    free(scenario);
}

bool map_has_ai(const level_t *map, int owner) {
    if (!map || owner == 0 || !map->native_data) return false;
    const ScenarioFile *scenario = map->native_data;
    for (int i = 0; i < scenario->team_count; ++i) {
        const ScenarioTeam *team = &scenario->teams[i];
        if (team->active && team->number != 0 && team->ai > 0) return true;
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
    blob_t file;
    if (!W_ReadFile(path, &file)) return false;
    char *text = (char *)file.bytes;

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
        if (line_no == 2 || line_no == 3) {
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
            } else if (strcmp(section, "%Depend") == 0) {
                team_info->allies_count = value_count;
                memcpy(team_info->allies, values, (size_t)value_count * sizeof(values[0]));
            } else if (strcmp(section, "%AISlots") == 0) {
                if (value_count >= 2 && team_info->ai_slot_count < DARK_COLONY_SCN_AI_SLOTS) {
                    int slot = team_info->ai_slot_count++;
                    team_info->ai_slots[slot] = (ivec2_t){ values[0], values[1] };
                }
            } else if (strcmp(section, "%City") == 0) {
                if (team_info->city_value_count == 0) {
                    team_info->city_value_count = value_count > DARK_COLONY_SCN_CITY_VALUES ?
                        DARK_COLONY_SCN_CITY_VALUES : value_count;
                    memcpy(team_info->city_values, values,
                           (size_t)team_info->city_value_count * sizeof(values[0]));

                }
            }
            line = next;
            continue;
        }

        int values[6] = { 0 };
        int parsed = parse_dark_colony_int_list(token, values, 6);
        if (parsed >= 5) {
            ScenarioObject object = {
                .cell = { values[0], values[1] },
                .type = values[2],
                .team = values[3],
                .status = values[4],
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
    snprintf(out->path, sizeof(out->path), "%s", path);
    if (!W_ReadFile(out->path, &out->file)) {
        const char *dot = strrchr(path, '.');
        if (!dot || strcasecmp(dot, ".MAP") != 0) return false;
        replace_extension(out->path, sizeof(out->path), path, ".MTG");
        if (!W_ReadFile(out->path, &out->file)) return false;
    }
    const blob_t *file = &out->file;
    if (file->size >= 8) {
        isize2_t size = { read_i32_le(file->bytes), read_i32_le(file->bytes + 4) };
        if (size.w > 0 && size.h > 0 && size.w <= 256 && size.h <= 256 &&
            file->size >= 8 + (size_t)size.w * size.h * 6) {
            out->size = size;
            out->terrain = true;
            return true;
        }
    }
    if (file->size >= 2) {
        isize2_t size = { file->bytes[0], file->bytes[1] };
        if (size.w > 0 && size.h > 0 && file->size >= 2 + (size_t)size.w * size.h) {
            out->size = size;
            return true;
        }
    }
    W_FreeFile(&out->file);
    return false;
}

static bool append_dark_colony_resource_vent(level_t *map, int x, int y, int rate, int amount) {
    if (!map || !L_Contains(map, x, y)) return false;

    resourcevent_t *vents = realloc(map->resource_vents,
                                     (size_t)(map->resource_vent_count + 1) * sizeof(resourcevent_t));
    if (!vents) return false;
    map->resource_vents = vents;
    resourcevent_t *vent = &map->resource_vents[map->resource_vent_count++];
    vent->cell = (ivec2_t){ x, y };
    vent->attachment = (fvec2_t){ (float)x + 0.5f, (float)y - 0.5f };
    vent->amount = amount;
    vent->rate = rate;
    vent->active = rate > 0 && amount > 0;
    vent->resource_type = 0;
    return true;
}

static void load_dark_colony_resource_vents_from_scenario(const ScenarioFile *scenario,
                                                          level_t *map) {
    if (!scenario || !map) return;
    for (int i = 0; i < scenario->object_count; ++i) {
        const ScenarioObject *object = &scenario->objects[i];
        if (object->type == OBJECT_TYPE_PETRA7_VENT) {
            append_dark_colony_resource_vent(map, object->cell.x, object->cell.y,
                                             object->team, object->status);
        }
    }
}

static void load_dark_colony_camera_from_scenario(const ScenarioFile *scenario,
                                                  level_t *map) {
    if (!scenario || !map || scenario->team_count <= 0) return;
    const ScenarioTeam *team = &scenario->teams[0];
    for (int i = 0; i < team->ai_slot_count; ++i) {
        if (ivec2_equal(team->ai_slots[i], (ivec2_t){0})) continue;
        map->has_camera = true;
        map->camera = fvec2_cell_center(team->ai_slots[i]);
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

bool load_dark_colony_map(const char *map_path, level_t *out) {
    memset(out, 0, sizeof(*out));
    MapFile map;
    if (!map_file_load(map_path, &map)) return false;

    /* Preserve the blank-MTG fallback, but keep only the file being decoded. */
    if (!map.terrain) {
        size_t count = (size_t)map.size.w * map.size.h;
        bool blank = true;
        for (size_t i = 0; i < count; ++i)
            if (map.file.bytes[2 + i]) { blank = false; break; }
        const char *dot = strrchr(map.path, '.');
        if (blank && dot && strcasecmp(dot, ".MTG") == 0) {
            char path[1024];
            replace_extension(path, sizeof(path), map.path, ".MAP");
            MapFile alternate;
            if (map_file_load(path, &alternate) && alternate.terrain) {
                W_FreeFile(&map.file);
                map = alternate;
            } else {
                W_FreeFile(&alternate.file);
            }
        }
    }

    out->width = map.size.w;
    out->height = map.size.h;
    size_t count = (size_t)out->width * out->height;
    out->render_capabilities = MAP_RENDER_CAP_DEPTH_SORTED_TILE_LAYERS |
                               MAP_RENDER_CAP_TILE_TRANSFORMS;
    out->tile_ids = calloc(count, sizeof(*out->tile_ids));
    out->blocked = calloc(count, sizeof(*out->blocked));
    out->tile_flags = calloc(count, sizeof(*out->tile_flags));
    out->tile_overlay_count = 1;
    out->tile_overlays[0] = calloc(count, sizeof(*out->tile_overlays[0]));
    out->tile_transforms[0] = calloc(count, sizeof(*out->tile_transforms[0]));
    out->tile_transforms[1] = calloc(count, sizeof(*out->tile_transforms[1]));
    if (!out->tile_ids || !out->blocked || !out->tile_flags || !out->tile_overlays[0] ||
        !out->tile_transforms[0] || !out->tile_transforms[1]) goto fail;

    blob_t overview = {0};
    if (!map.terrain) {
        char path[1024];
        replace_extension(path, sizeof(path), map.path, ".OVH");
        if (W_ReadFile(path, &overview) && overview.size >= count * 2) {
            out->cell_colors = calloc(count, sizeof(*out->cell_colors));
            if (!out->cell_colors) { W_FreeFile(&overview); goto fail; }
            out->render_capabilities |= MAP_RENDER_CAP_CELL_COLORS;
        }
    }
    for (int y = 0; y < out->height; ++y) {
        for (int x = 0; x < out->width; ++x) {
            size_t dst = (size_t)y * out->width + x;
            size_t src = (size_t)(out->height - 1 - y) * out->width + x;
            if (map.terrain) {
                const uint8_t *pair = map.file.bytes + 8 + src * 4;
                uint16_t flags = read_u16_le(map.file.bytes + 8 + count * 4 + src * 2);
                /* DC.EXE 0x44ea30: non-obstacle cells always pass sight. */
                out->tile_flags[dst] = flags | ((flags & (1u << 9)) ? 0 : MAP_SIGHT_PASS);
                out->tile_ids[dst] = read_u16_le(pair);
                out->tile_overlays[0][dst] = read_u16_le(pair + 2);
                out->blocked[dst] = (flags & (1u << 9)) != 0;
                out->tile_transforms[0][dst] = (flags & (1u << 5)) ? MAP_TILE_TRANSFORM_FLIP_X : 0;
                out->tile_transforms[1][dst] = (flags & (1u << 6)) ? MAP_TILE_TRANSFORM_FLIP_X : 0;
            } else {
                out->tile_ids[dst] = map.file.bytes[2 + src];
                out->tile_flags[dst] = MAP_SIGHT_PASS;
            }
            if (out->cell_colors)
                out->cell_colors[dst] = rgb565_to_rgba(read_u16_le(overview.bytes + src * 2));
        }
    }
    W_FreeFile(&overview);
    W_FreeFile(&map.file);

    ScenarioFile *scenario = calloc(1, sizeof(*scenario));
    if (!scenario) goto fail;
    out->native_data = scenario;
    out->destroy_native_data = scenario_free;
    char path[1024];
    replace_extension(path, sizeof(path), map.path, ".SCN");
    if (scenario_load(path, scenario)) {
        char *dot = strrchr(scenario->tileset_file, '.');
        if (dot) *dot = '\0';
        snprintf(out->tileset_name, sizeof(out->tileset_name), "%s", M_Upper(scenario->tileset_file));
        load_dark_colony_camera_from_scenario(scenario, out);
        load_dark_colony_resource_vents_from_scenario(scenario, out);
        if (scenario->header_value_count >= 6) {
            if (scenario->header_values[3] < 0 || scenario->header_values[4] < 0 ||
                scenario->header_values[5] < 0) goto fail;
            out->daylight = (daylight_t){
                .phase = scenario->header_values[2] != 0,
                .duration = scenario->header_values[3],
                .tics = scenario->header_values[4],
                .transition = scenario->header_values[5],
                .weight = scenario->header_values[2] ? 256 : 0,
            };
        }
        for (int team = 0; team < scenario->team_count && team < 8; ++team) {
            const ScenarioTeam *info = &scenario->teams[team];
            for (int i = 0; i < info->allies_count && i < 8; ++i)
                if (info->allies[i] == 1)
                    out->sight.allies[team] |= UINT32_C(0x40000000) >> i;
        }
        for (int i = 0; i < scenario->team_count && i < 8; ++i)
            out->player_resources[i][0] = scenario->teams[i].money;
    }
    if (!out->tileset_name[0]) {
        const char *tileset = "DESERT";
        switch (toupper((unsigned char)M_FileName(map.path)[0])) {
            case 'J': tileset = "JUNGLE"; break;
            case 'A': tileset = "ATLANTIS"; break;
            case 'H': tileset = "HTRAIN"; break;
        }
        snprintf(out->tileset_name, sizeof(out->tileset_name), "%s", tileset);
    }
    return true;
fail:
    W_FreeFile(&map.file);
    P_FreeLevel(out);
    return false;
}

static int default_health_for_type(int type) {
    if (type >= 0 && type < GAMESTAT_UNIT_COUNT) {
        const DcGamestatUnit *unit = &dc_gamestat_units[type];
        if (unit->value_count > GAMESTAT_UNIT_HEALTH && unit->values[GAMESTAT_UNIT_HEALTH] > 0)
            return unit->values[GAMESTAT_UNIT_HEALTH];
    }
    return 1;
}

static int mobj_type_for_type(int type, int race) {
    switch (type) {
        case 16: return MT_EXCOPOD;
        case 17: return MT_BRRKPOD;
        case 18: return MT_ROBOPOD;
        case 19: return MT_ROBOPOD2;
        case 20: return MT_SCNCPOD;
        case 21: return MT_SCNCPOD2;
        case 22: return MT_RSCHPOD;
        case 28: return MT_ALIEN_MINDHIVE;
        case 29: return MT_ALIEN_WARHIVE;
        case 30: return MT_ALIEN_BRDRHIVE;
        case 31: return MT_ALIEN_BRDRHIVE2;
        case 32: return MT_ALIEN_MINDHIVE2;
        case 33: return MT_ALIEN_MINDHIVE3;
        case 34: return MT_ALIEN_RSCHIVE;
        case 41: return MT_MOBILE_TOWER;
        case 81: return MT_CITY_TOWER;
        case 84: return MT_BEACON;
        case 86: return MT_COMMS_DISH;
        case 89: return MT_DROP_LINK;
        case 91: return MT_ALIEN_COM;
        case 94: return MT_VISION_SIGHT;
        default: break;
    }

    if (race == 1) {
        if (type == 0 || type == 8 || (type >= 69 && type <= 76)) return MT_GREY;
        if (type == 13) return MT_ORTU;
        if (type == 14) return MT_SLUG;
        return 0;
    }

    if (type == 0 || (type >= 69 && type <= 72)) return MT_TROOPER;
    switch (type) {
        case 2: return MT_REAPER;
        case 3: return MT_THUNDERBOLT;
        case 4: return MT_CYBORG;
        case 5: return MT_SCOUT;
        case 6: return MT_EXPLOITER;
        default: return 0;
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

ivec2_t DC_CitySlotOffset(int slot) {
    static const ivec2_t offsets[DARK_COLONY_SCN_CITY_SLOTS] = {
        { -64, 15 }, { 0, 0 }, { 32, 64 }, { 64, 10 }, { -32, 65 }, { 0, 32 }, { 0, 0 },
    };
    return slot >= 0 && slot < DARK_COLONY_SCN_CITY_SLOTS ? offsets[slot] : (ivec2_t){0};
}

static bool team_city_anchor(const ScenarioTeam *team, ivec2_t *anchor) {
    if (team->ai_slot_count < 2) return false;
    /* DC.EXE 0x41ad47: only the second AISlots pair enables city slots. */
    *anchor = team->ai_slots[1];
    return anchor->x != 0;
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

typedef struct {
    int count;
    int dynamic_count;
    bool player_selected;
    bool player_has_exploiter;
    bool player_anchor_set;
    ivec2_t player_anchor;
    bool alien_has_slug;
} InitialUnits;

static void spawn_object(InitialUnits *units, int type, int team, int race,
                         int allegiance, ivec2_t position, int health, int city_slot) {
    int mobj_type = mobj_type_for_type(type, race);
    if (mobj_type <= 0 || !actor_type_by_id((uint16_t)mobj_type)) return;
    mobj_t *u = P_SpawnMobj(fixed3_zero(), (uint16_t)mobj_type);
    if (!u) return;
    /* Preserve native signed 8.8 gameplay positions, including city slots.
     * Their raw Z controls depth sorting, as in DC.EXE 0x4365a7. */
    ivec2_t native = { (int16_t)position.x, (int16_t)position.y };
    bool city_origin = city_slot >= 0 && city_slot < 6;
    u->core.position = (fixed3_t){ native.x * 256, native.y * 256, 0 };
    u->attack.target = NULL;
    u->harvest.target = -1;
    u->native_type_id = (uint16_t)type;
    u->ability_charge = 0x40; /* DC.EXE 0x419d44: object byte +0x0a. */
    u->owner = (allegiance == DC_ALLEGIANCE_PLAYER || mobj_type == MT_COMMS_DISH) ? 0 :
               (allegiance == DC_ALLEGIANCE_ALLIED ? 2 : 1);
    u->team = (uint8_t)team;
    u->allegiance = allegiance == DC_ALLEGIANCE_PLAYER ? ALLEGIANCE_PLAYER :
                    allegiance == DC_ALLEGIANCE_ALLIED ? ALLEGIANCE_ALLIED : ALLEGIANCE_ENEMY;
    u->hp = health;
    P_MobjSetSelected(u, u->owner == 0 && (u->traits & MF_SELECTABLE) &&
                      (u->traits & MF_MOBILE) && !units->player_selected);
    if (P_MobjIsSelected(u)) units->player_selected = true;
    if (city_origin) {
        /* The native draw queue subtracts the slot in world coordinates;
         * screen Y runs in the opposite direction. FIN keeps the shared origin. */
        ivec2_t slot = DC_CitySlotOffset(city_slot);
        u->core.render_offset = (ivec2_t){ -slot.x, slot.y + g_cell_h };
    }
    if (u->owner == 0) {
        ivec2_t cell = { (uint8_t)(position.x >> 8), (uint8_t)(position.y >> 8) };
        if (!units->player_anchor_set || cell.x > units->player_anchor.x) {
            units->player_anchor = cell;
            units->player_anchor_set = true;
        }
        if (mobj_type == MT_EXPLOITER) units->player_has_exploiter = true;
    }
    units->count++;
}

static void spawn_dynamic(InitialUnits *units, const ScenarioFile *scenario,
                          const int *allegiances, ScenarioObject object) {
    if (object.cell.x < 0 || object.cell.y < 0 || object.team < 0 || object.team >= 10 ||
        object.type < 0 || object.type > 255 ||
        units->dynamic_count >= MAX_OBJECTS - DYNAMIC_OBJECT_FIRST) return;
    units->dynamic_count++;
    int race = scenario->teams[object.team].race;
    int allegiance = allegiances[object.team];
    ivec2_t position = ivec2_add(ivec2_scale(object.cell, 256),
                               (ivec2_t){ FIXED_TILE_CENTER, FIXED_TILE_CENTER });
    int health = object.status >= 0 ? object.status : default_health_for_type(object.type);
    if (race == 1 && object.type == 14) units->alien_has_slug = true;
    if (race != 1 && object.type == 16)
        spawn_object(units, 81, object.team, race, allegiance, position, health, -1);
    spawn_object(units, object.type, object.team, race, allegiance, position, health, -1);
}

int load_dark_colony_initial_units(const char *map_path) {
    /* The active level already owns the parsed SCN used for terrain setup. */
    const ScenarioFile *scenario = level.native_data;
    if (!scenario) return 0;
    InitialUnits units = {0};
    int allegiances[DARK_COLONY_SCN_MAX_TEAMS];
    compute_team_allegiances(scenario, allegiances);
    bool alien_anchor_set = false;
    ivec2_t alien_anchor = {0};
    for (int team = 0; team < scenario->team_count && team < 8; ++team) {
        const ScenarioTeam *info = &scenario->teams[team];
        if (!info->active) continue;
        ivec2_t anchor;
        if (!team_city_anchor(info, &anchor)) continue;
        if (info->race == 1 && !alien_anchor_set) {
            alien_anchor = anchor;
            alien_anchor_set = true;
        }
        for (int slot = 0; slot < DARK_COLONY_SCN_CITY_SLOTS; ++slot) {
            bool tower = slot == 5 && info->city_values[0] > 0 &&
                         info->city_values[10] <= 0;
            if (!tower && (slot * 2 >= info->city_value_count || info->city_values[slot * 2] <= 0)) continue;
            int type = city_unit_type_for_slot(info->race, slot);
            /* DC.EXE 0x4412d4: city anchor * 256 + authored slot offset * 8. */
            ivec2_t position = ivec2_add(ivec2_scale(anchor, 256), ivec2_scale(DC_CitySlotOffset(slot), 8));
            if (position.x < 0 || position.y < 0 || type <= 0) continue;
            int health = default_health_for_type(type);
            spawn_object(&units, type, team, info->race, allegiances[team], position, health, slot);
        }
    }
    int vent_index = 0;
    for (int i = 0; i < scenario->object_count; ++i) {
        const ScenarioObject *object = &scenario->objects[i];
        if (object->type != OBJECT_TYPE_PETRA7_VENT) {
            spawn_dynamic(&units, scenario, allegiances, *object);
        } else if (vent_index < level.resource_vent_count &&
                   ivec2_equal(object->cell, level.resource_vents[vent_index].cell)) {
            const resourcevent_t *vent = &level.resource_vents[vent_index];
            /* SCN cell is the script key; attachment is the crater's shared
             * visual and harvesting origin. Keep all FIN offsets intact. */
            mobj_t *actor = P_SpawnMobj(fixed3_from_fvec2(vent->attachment, 0), MT_VENT);
            if (actor) {
                actor->resource_vent_index = vent_index;
                A_DC_Vent(actor);
                units.count++;
            }
            vent_index++;
        }
    }
    if (strcasestr(map_path, "/MPLAYER/") || strcasestr(map_path, "\\MPLAYER\\")) {
        if (!units.player_has_exploiter && units.player_anchor_set) {
            ivec2_t position = ivec2_add(ivec2_scale(ivec2_add(units.player_anchor, (ivec2_t){2, 0}), 256),
                                       (ivec2_t){ FIXED_TILE_CENTER, FIXED_TILE_CENTER });
            if (units.dynamic_count < MAX_OBJECTS - DYNAMIC_OBJECT_FIRST) {
                units.dynamic_count++;
                spawn_object(&units, 6, 0, 0, DC_ALLEGIANCE_PLAYER, position, default_health_for_type(6), -1);
            }
        }
        if (!units.alien_has_slug && alien_anchor_set) {
            ivec2_t position = ivec2_add(ivec2_scale(ivec2_add(alien_anchor, (ivec2_t){2, 0}), 256),
                                       (ivec2_t){ FIXED_TILE_CENTER, FIXED_TILE_CENTER });
            if (units.dynamic_count < MAX_OBJECTS - DYNAMIC_OBJECT_FIRST) {
                spawn_object(&units, 14, 1, 1, DC_ALLEGIANCE_ENEMY, position, default_health_for_type(14), -1);
            }
        }
    }
    return units.count;
}
