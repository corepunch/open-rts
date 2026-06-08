#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DC_GAMESTAT_UNIT_VALUE_COUNT 33
#define DC_GAMESTAT_WEAPON_VALUE_COUNT 11
#define DC_GAMESTAT_MAX_DEPEND_PARAMS 16
#define DC_GAMESTAT_MAX_SCENE_PREREQUISITES 16
#define DC_GAMESTAT_MAX_BOOM_EFFECTS 8
#define DC_GAMESTAT_MAX_BOOM_SIZE 7
#define DC_GAMESTAT_MBULLET_ROWS 9
#define DC_GAMESTAT_MBULLET_COLS 10

#define MAX_GAMESTAT_UNITS 256
#define MAX_DEPENDS 128
#define MAX_WEAPONS 128
#define MAX_UNIT_IDS 128
#define MAX_BOOMS 32
#define MAX_SCENE_MISSIONS 32

typedef struct {
    char sprite[64];
    int value_count;
    int values[DC_GAMESTAT_UNIT_VALUE_COUNT];
} UnitStat;

typedef struct {
    int row_id;
    int cost;
    int ui_id;
    int product_class;
    int product_type;
    int parameter_count;
    int parameters[DC_GAMESTAT_MAX_DEPEND_PARAMS];
} DependStat;

typedef struct {
    int id;
    char sprite[64];
    int values[DC_GAMESTAT_WEAPON_VALUE_COUNT];
} WeaponStat;

typedef struct {
    int team;
    int weapon_class;
    int unit_type;
    int unit_id;
} UnitIdStat;

typedef struct {
    int width;
    int height;
    int values[DC_GAMESTAT_MBULLET_ROWS][DC_GAMESTAT_MBULLET_COLS];
} MissileBulletStat;

typedef struct {
    int id;
    int size;
    int effect_count;
    char effects[DC_GAMESTAT_MAX_BOOM_EFFECTS][64];
    int damage[DC_GAMESTAT_MAX_BOOM_SIZE][DC_GAMESTAT_MAX_BOOM_SIZE];
    int falloff[3][3];
} BoomStat;

typedef struct {
    char scenario[128];
    char title[128];
    char region[128];
    char map_path[128];
    char intro_avi[128];
    char outro_avi[128];
    int color[3];
    int flag;
    int prerequisite_count;
    int prerequisites[DC_GAMESTAT_MAX_SCENE_PREREQUISITES];
} SceneMission;

typedef struct {
    char names[8][128];
    int mission_count;
    SceneMission missions[MAX_SCENE_MISSIONS];
} SceneTable;

typedef struct {
    UnitStat units[MAX_GAMESTAT_UNITS];
    int unit_count;
    DependStat depends[MAX_DEPENDS];
    int depend_count;
    WeaponStat weapons[MAX_WEAPONS];
    int weapon_count;
    UnitIdStat unit_ids[MAX_UNIT_IDS];
    int unit_id_count;
    MissileBulletStat mbullet;
    BoomStat booms[MAX_BOOMS];
    int boom_count;
    SceneTable hscene;
    SceneTable gscene;
    SceneTable htscene;
    SceneTable gtscene;
} GameStatData;

static void die(const char *msg, const char *path) {
    if (path) fprintf(stderr, "dc_gamestat_gen: %s: %s\n", path, msg);
    else fprintf(stderr, "dc_gamestat_gen: %s\n", msg);
    exit(1);
}

static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) die(strerror(errno), path);
    if (fseek(f, 0, SEEK_END) != 0) die("seek failed", path);
    long size = ftell(f);
    if (size < 0) die("ftell failed", path);
    if (fseek(f, 0, SEEK_SET) != 0) die("rewind failed", path);
    char *data = malloc((size_t)size + 1);
    if (!data) die("out of memory", NULL);
    if (size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size) die("read failed", path);
    data[size] = '\0';
    fclose(f);
    return data;
}

static void path_join(char *out, size_t out_size, const char *dir, const char *name) {
    snprintf(out, out_size, "%s/%s", dir, name);
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    return s;
}

static bool is_payload_line(const char *s) {
    return s[0] != '\0' && s[0] != '%';
}

static char *next_line(char **cursor) {
    if (!cursor || !*cursor || **cursor == '\0') return NULL;
    char *line = *cursor;
    char *end = strpbrk(line, "\r\n");
    if (end) {
        char nl = *end;
        *end++ = '\0';
        if (nl == '\r' && *end == '\n') end++;
        *cursor = end;
    } else {
        *cursor = line + strlen(line);
    }
    return line;
}

static char *next_payload_line(char **cursor) {
    char *line;
    while ((line = next_line(cursor)) != NULL) {
        line = trim(line);
        if (is_payload_line(line)) return line;
    }
    return NULL;
}

static int parse_ints(const char *line, int *out, int max_out) {
    int count = 0;
    const char *p = line;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;
        char *end = NULL;
        long value = strtol(p, &end, 10);
        if (end == p) break;
        if (count >= max_out) die("too many integers in row", line);
        out[count++] = (int)value;
        p = end;
    }
    return count;
}

static void copy_token(char *out, size_t out_size, const char **cursor) {
    const char *p = *cursor;
    while (*p && isspace((unsigned char)*p)) p++;
    size_t n = 0;
    while (*p && !isspace((unsigned char)*p)) {
        if (n + 1 < out_size) out[n++] = *p;
        p++;
    }
    out[n] = '\0';
    *cursor = p;
}

static int parse_leading_int(const char **cursor) {
    const char *p = *cursor;
    while (*p && isspace((unsigned char)*p)) p++;
    char *end = NULL;
    long value = strtol(p, &end, 10);
    if (end == p) die("expected integer", p);
    *cursor = end;
    return (int)value;
}

static bool equals_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void clean_scene_name(char *s) {
    s = trim(s);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    if (n > 0 && s[n - 1] == '.') s[--n] = '\0';
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

static int read_count_line(char **cursor, const char *path) {
    char *line = next_payload_line(cursor);
    if (!line) die("missing count", path);
    int values[4];
    int count = parse_ints(line, values, 4);
    if (count != 1) die("expected single count line", path);
    return values[0];
}

static void parse_game_stat_file(const char *dir, GameStatData *data) {
    char path[1024];
    path_join(path, sizeof(path), dir, "GAMESTAT.TXT");
    char *text = read_text_file(path);
    char *cursor = text;
    int expected = read_count_line(&cursor, path);

    char *line;
    while ((line = next_payload_line(&cursor)) != NULL) {
        if (data->unit_count >= MAX_GAMESTAT_UNITS) die("too many GAMESTAT rows", path);
        const char *p = line;
        UnitStat *unit = &data->units[data->unit_count];
        copy_token(unit->sprite, sizeof(unit->sprite), &p);
        if (isdigit((unsigned char)unit->sprite[0]) || unit->sprite[0] == '-') continue;
        unit->value_count = parse_ints(p, unit->values, DC_GAMESTAT_UNIT_VALUE_COUNT);
        if (unit->value_count <= 0) die("GAMESTAT row has no values", line);
        data->unit_count++;
    }

    if (data->unit_count != expected) die("GAMESTAT row count mismatch", path);
    free(text);
}

static void parse_depend_file(const char *dir, GameStatData *data) {
    char path[1024];
    path_join(path, sizeof(path), dir, "DEPEND.TXT");
    char *text = read_text_file(path);
    char *cursor = text;
    int expected = read_count_line(&cursor, path);

    char *line;
    while ((line = next_payload_line(&cursor)) != NULL) {
        int values[32];
        int count = parse_ints(line, values, 32);
        if (count == 0) continue;
        if (count < 6) die("DEPEND row too short", line);
        if (data->depend_count >= MAX_DEPENDS) die("too many DEPEND rows", path);
        DependStat *dep = &data->depends[data->depend_count++];
        dep->row_id = values[0];
        dep->cost = values[1];
        dep->ui_id = values[2];
        dep->product_class = values[3];
        dep->product_type = values[4];
        dep->parameter_count = 0;
        for (int i = 5; i < count; ++i) {
            if (values[i] == -1) break;
            if (dep->parameter_count >= DC_GAMESTAT_MAX_DEPEND_PARAMS) {
                die("too many DEPEND parameters", line);
            }
            dep->parameters[dep->parameter_count++] = values[i];
        }
    }

    if (data->depend_count != expected) die("DEPEND row count mismatch", path);
    free(text);
}

static void parse_weapon_file(const char *dir, GameStatData *data) {
    char path[1024];
    path_join(path, sizeof(path), dir, "WEAPSTAT.TXT");
    char *text = read_text_file(path);
    char *cursor = text;
    int expected = read_count_line(&cursor, path);

    char *line;
    while ((line = next_payload_line(&cursor)) != NULL) {
        if (data->weapon_count >= MAX_WEAPONS) die("too many WEAPSTAT rows", path);
        const char *p = line;
        WeaponStat *weapon = &data->weapons[data->weapon_count];
        weapon->id = parse_leading_int(&p);
        copy_token(weapon->sprite, sizeof(weapon->sprite), &p);
        int count = parse_ints(p, weapon->values, DC_GAMESTAT_WEAPON_VALUE_COUNT);
        if (count != DC_GAMESTAT_WEAPON_VALUE_COUNT) die("WEAPSTAT row does not have 11 values", line);
        data->weapon_count++;
    }

    if (data->weapon_count != expected) die("WEAPSTAT row count mismatch", path);
    free(text);
}

static void parse_unit_id_file(const char *dir, GameStatData *data) {
    char path[1024];
    path_join(path, sizeof(path), dir, "UNITID.TXT");
    char *text = read_text_file(path);
    char *cursor = text;

    char *line;
    while ((line = next_payload_line(&cursor)) != NULL) {
        int values[4];
        int count = parse_ints(line, values, 4);
        if (count != 4) die("UNITID row does not have four values", line);
        if (values[0] == -1 && values[1] == -1 && values[2] == -1 && values[3] == -1) break;
        if (data->unit_id_count >= MAX_UNIT_IDS) die("too many UNITID rows", path);
        UnitIdStat *unit_id = &data->unit_ids[data->unit_id_count++];
        unit_id->team = values[0];
        unit_id->weapon_class = values[1];
        unit_id->unit_type = values[2];
        unit_id->unit_id = values[3];
    }

    free(text);
}

static void parse_mbullet_file(const char *dir, GameStatData *data) {
    char path[1024];
    path_join(path, sizeof(path), dir, "MBULLET.TXT");
    char *text = read_text_file(path);
    char *cursor = text;
    data->mbullet.width = read_count_line(&cursor, path);
    data->mbullet.height = read_count_line(&cursor, path);
    if (data->mbullet.width > DC_GAMESTAT_MBULLET_COLS ||
        data->mbullet.height > DC_GAMESTAT_MBULLET_ROWS) {
        die("MBULLET matrix larger than generator constants", path);
    }

    for (int row = 0; row < data->mbullet.height; ++row) {
        char *line = next_payload_line(&cursor);
        if (!line) die("missing MBULLET row", path);
        int count = parse_ints(line, data->mbullet.values[row], DC_GAMESTAT_MBULLET_COLS);
        if (count != data->mbullet.width) die("MBULLET row width mismatch", line);
    }
    free(text);
}

static void parse_boom_file(const char *dir, GameStatData *data) {
    char path[1024];
    path_join(path, sizeof(path), dir, "BOOMSTAT.TXT");
    char *text = read_text_file(path);
    char *cursor = text;
    int expected = read_count_line(&cursor, path);

    while (data->boom_count < expected) {
        char *line = next_payload_line(&cursor);
        if (!line) die("missing BOOMSTAT block", path);
        int header[2];
        if (parse_ints(line, header, 2) != 2) die("invalid BOOMSTAT block header", line);
        if (header[1] > DC_GAMESTAT_MAX_BOOM_SIZE) die("BOOMSTAT block too large", line);

        BoomStat *boom = &data->booms[data->boom_count++];
        boom->id = header[0];
        boom->size = header[1];
        boom->effect_count = 0;

        while ((line = next_payload_line(&cursor)) != NULL) {
            if (equals_ci(line, "NONE")) break;
            if (boom->effect_count >= DC_GAMESTAT_MAX_BOOM_EFFECTS) die("too many BOOMSTAT effects", line);
            snprintf(boom->effects[boom->effect_count++], sizeof(boom->effects[0]), "%s", line);
        }
        if (!line) die("unterminated BOOMSTAT effects", path);

        for (int y = 0; y < boom->size; ++y) {
            line = next_payload_line(&cursor);
            if (!line) die("missing BOOMSTAT damage row", path);
            int count = parse_ints(line, boom->damage[y], DC_GAMESTAT_MAX_BOOM_SIZE);
            if (count != boom->size) die("BOOMSTAT damage row size mismatch", line);
        }
        for (int y = 0; y < 3; ++y) {
            line = next_payload_line(&cursor);
            if (!line) die("missing BOOMSTAT falloff row", path);
            int count = parse_ints(line, boom->falloff[y], 3);
            if (count != 3) die("BOOMSTAT falloff row size mismatch", line);
        }
    }

    free(text);
}

static void copy_clean_scene_name(char *out, size_t out_size, char *line) {
    clean_scene_name(line);
    snprintf(out, out_size, "%s", line);
}

static void parse_scene_file(const char *dir, const char *name, SceneTable *table) {
    char path[1024];
    path_join(path, sizeof(path), dir, name);
    char *text = read_text_file(path);
    char *cursor = text;

    for (int i = 0; i < 8; ++i) {
        char *line = next_payload_line(&cursor);
        if (!line) die("missing scene faction name", path);
        copy_clean_scene_name(table->names[i], sizeof(table->names[i]), line);
    }

    table->mission_count = read_count_line(&cursor, path);
    if (table->mission_count > MAX_SCENE_MISSIONS) die("too many scene missions", path);

    for (int i = 0; i < table->mission_count; ++i) {
        SceneMission *mission = &table->missions[i];
        char *line = next_payload_line(&cursor);
        if (!line) die("missing scene scenario path", path);
        snprintf(mission->scenario, sizeof(mission->scenario), "%s", line);
        line = next_payload_line(&cursor);
        if (!line) die("missing scene title", path);
        snprintf(mission->title, sizeof(mission->title), "%s", line);
        line = next_payload_line(&cursor);
        if (!line) die("missing scene region", path);
        snprintf(mission->region, sizeof(mission->region), "%s", line);
        line = next_payload_line(&cursor);
        if (!line) die("missing scene map path", path);
        snprintf(mission->map_path, sizeof(mission->map_path), "%s", line);
        line = next_payload_line(&cursor);
        if (!line) die("missing scene intro avi", path);
        snprintf(mission->intro_avi, sizeof(mission->intro_avi), "%s", line);
        line = next_payload_line(&cursor);
        if (!line) die("missing scene outro avi", path);
        snprintf(mission->outro_avi, sizeof(mission->outro_avi), "%s", line);

        line = next_payload_line(&cursor);
        if (!line) die("missing scene color", path);
        if (parse_ints(line, mission->color, 3) != 3) die("scene color row needs three values", line);

        line = next_payload_line(&cursor);
        if (!line) die("missing scene flag", path);
        int flag_values[1];
        if (parse_ints(line, flag_values, 1) != 1) die("scene flag row needs one value", line);
        mission->flag = flag_values[0];

        line = next_payload_line(&cursor);
        if (!line) die("missing scene prerequisite row", path);
        int prereqs[32];
        int count = parse_ints(line, prereqs, 32);
        mission->prerequisite_count = 0;
        for (int j = 0; j < count; ++j) {
            if (prereqs[j] == -1) break;
            if (mission->prerequisite_count >= DC_GAMESTAT_MAX_SCENE_PREREQUISITES) {
                die("too many scene prerequisites", line);
            }
            mission->prerequisites[mission->prerequisite_count++] = prereqs[j];
        }
    }

    free(text);
}

static void parse_all(const char *dir, GameStatData *data) {
    parse_game_stat_file(dir, data);
    parse_depend_file(dir, data);
    parse_weapon_file(dir, data);
    parse_unit_id_file(dir, data);
    parse_mbullet_file(dir, data);
    parse_boom_file(dir, data);
    parse_scene_file(dir, "HSCENE.TXT", &data->hscene);
    parse_scene_file(dir, "GSCENE.TXT", &data->gscene);
    parse_scene_file(dir, "HTSCENE.TXT", &data->htscene);
    parse_scene_file(dir, "GTSCENE.TXT", &data->gtscene);
}

static void emit_c_string(FILE *out, const char *s) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        switch (*p) {
            case '\\': fputs("\\\\", out); break;
            case '"': fputs("\\\"", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if (*p < 32 || *p > 126) fprintf(out, "\\x%02x", *p);
                else fputc(*p, out);
                break;
        }
    }
    fputc('"', out);
}

static void emit_int_array(FILE *out, const int *values, int count) {
    fputc('{', out);
    for (int i = 0; i < count; ++i) {
        if (i) fputs(", ", out);
        fprintf(out, "%d", values[i]);
    }
    fputc('}', out);
}

static void emit_header_prelude(FILE *out, const GameStatData *data) {
    fprintf(out, "/* Generated by tools/dc_gamestat_gen.c. Do not edit by hand. */\n");
    fprintf(out, "#ifndef OPEN_RTS_DARK_COLONY_GAMESTAT_H\n");
    fprintf(out, "#define OPEN_RTS_DARK_COLONY_GAMESTAT_H\n\n");
    fprintf(out, "#define DC_GAMESTAT_UNIT_COUNT %d\n", data->unit_count);
    fprintf(out, "#define DC_GAMESTAT_UNIT_VALUE_COUNT %d\n", DC_GAMESTAT_UNIT_VALUE_COUNT);
    fprintf(out, "#define DC_GAMESTAT_DEPEND_COUNT %d\n", data->depend_count);
    fprintf(out, "#define DC_GAMESTAT_WEAPON_COUNT %d\n", data->weapon_count);
    fprintf(out, "#define DC_GAMESTAT_WEAPON_VALUE_COUNT %d\n", DC_GAMESTAT_WEAPON_VALUE_COUNT);
    fprintf(out, "#define DC_GAMESTAT_UNIT_ID_COUNT %d\n", data->unit_id_count);
    fprintf(out, "#define DC_GAMESTAT_BOOM_COUNT %d\n", data->boom_count);
    fprintf(out, "#define DC_GAMESTAT_MAX_DEPEND_PARAMS %d\n", DC_GAMESTAT_MAX_DEPEND_PARAMS);
    fprintf(out, "#define DC_GAMESTAT_MAX_SCENE_PREREQUISITES %d\n", DC_GAMESTAT_MAX_SCENE_PREREQUISITES);
    fprintf(out, "#define DC_GAMESTAT_MAX_BOOM_EFFECTS %d\n", DC_GAMESTAT_MAX_BOOM_EFFECTS);
    fprintf(out, "#define DC_GAMESTAT_MAX_BOOM_SIZE %d\n", DC_GAMESTAT_MAX_BOOM_SIZE);
    fprintf(out, "#define DC_GAMESTAT_MBULLET_ROWS %d\n", DC_GAMESTAT_MBULLET_ROWS);
    fprintf(out, "#define DC_GAMESTAT_MBULLET_COLS %d\n\n", DC_GAMESTAT_MBULLET_COLS);

    fprintf(out, "typedef enum {\n");
    fprintf(out, "    DC_GAMESTAT_UNIT_RACE = 0,\n");
    fprintf(out, "    DC_GAMESTAT_UNIT_TURN_SPEED = 1,\n");
    fprintf(out, "    DC_GAMESTAT_UNIT_SPEED = 2,\n");
    fprintf(out, "    DC_GAMESTAT_UNIT_OBS_DAY = 3,\n");
    fprintf(out, "    DC_GAMESTAT_UNIT_OBS_NIGHT = 4,\n");
    fprintf(out, "    DC_GAMESTAT_UNIT_WEAPON0 = 5,\n");
    fprintf(out, "    DC_GAMESTAT_UNIT_WEAPON1 = 6,\n");
    fprintf(out, "    DC_GAMESTAT_UNIT_WEAPON2 = 7,\n");
    fprintf(out, "    DC_GAMESTAT_UNIT_DEFENSE = 8,\n");
    fprintf(out, "    DC_GAMESTAT_UNIT_UNKNOWN_9 = 9,\n");
    fprintf(out, "    DC_GAMESTAT_UNIT_FLY = 10,\n");
    fprintf(out, "    DC_GAMESTAT_UNIT_HEALTH = 11\n");
    fprintf(out, "} DcGamestatUnitValueIndex;\n\n");

    fprintf(out, "typedef struct {\n");
    fprintf(out, "    const char *sprite;\n");
    fprintf(out, "    int value_count;\n");
    fprintf(out, "    int values[DC_GAMESTAT_UNIT_VALUE_COUNT];\n");
    fprintf(out, "} DcGamestatUnit;\n\n");

    fprintf(out, "typedef struct {\n");
    fprintf(out, "    int row_id;\n");
    fprintf(out, "    int cost;\n");
    fprintf(out, "    int ui_id;\n");
    fprintf(out, "    int product_class;\n");
    fprintf(out, "    int product_type;\n");
    fprintf(out, "    int parameter_count;\n");
    fprintf(out, "    int parameters[DC_GAMESTAT_MAX_DEPEND_PARAMS];\n");
    fprintf(out, "} DcGamestatDepend;\n\n");

    fprintf(out, "typedef struct {\n");
    fprintf(out, "    int id;\n");
    fprintf(out, "    const char *sprite;\n");
    fprintf(out, "    int weapon_class;\n");
    fprintf(out, "    int sound;\n");
    fprintf(out, "    int rate_of_fire;\n");
    fprintf(out, "    int damage;\n");
    fprintf(out, "    int speed;\n");
    fprintf(out, "    int range;\n");
    fprintf(out, "    int shots;\n");
    fprintf(out, "    int reload;\n");
    fprintf(out, "    int magic_chewing;\n");
    fprintf(out, "    int explosion_type;\n");
    fprintf(out, "    int flags;\n");
    fprintf(out, "} DcGamestatWeapon;\n\n");

    fprintf(out, "typedef struct {\n");
    fprintf(out, "    int team;\n");
    fprintf(out, "    int weapon_class;\n");
    fprintf(out, "    int unit_type;\n");
    fprintf(out, "    int unit_id;\n");
    fprintf(out, "} DcGamestatUnitId;\n\n");

    fprintf(out, "typedef struct {\n");
    fprintf(out, "    int width;\n");
    fprintf(out, "    int height;\n");
    fprintf(out, "    int values[DC_GAMESTAT_MBULLET_ROWS][DC_GAMESTAT_MBULLET_COLS];\n");
    fprintf(out, "} DcGamestatMissileBullet;\n\n");

    fprintf(out, "typedef struct {\n");
    fprintf(out, "    int id;\n");
    fprintf(out, "    int size;\n");
    fprintf(out, "    int effect_count;\n");
    fprintf(out, "    const char *effects[DC_GAMESTAT_MAX_BOOM_EFFECTS];\n");
    fprintf(out, "    int damage[DC_GAMESTAT_MAX_BOOM_SIZE][DC_GAMESTAT_MAX_BOOM_SIZE];\n");
    fprintf(out, "    int falloff[3][3];\n");
    fprintf(out, "} DcGamestatBoom;\n\n");

    fprintf(out, "typedef struct {\n");
    fprintf(out, "    const char *scenario;\n");
    fprintf(out, "    const char *title;\n");
    fprintf(out, "    const char *region;\n");
    fprintf(out, "    const char *map_path;\n");
    fprintf(out, "    const char *intro_avi;\n");
    fprintf(out, "    const char *outro_avi;\n");
    fprintf(out, "    int color[3];\n");
    fprintf(out, "    int flag;\n");
    fprintf(out, "    int prerequisite_count;\n");
    fprintf(out, "    int prerequisites[DC_GAMESTAT_MAX_SCENE_PREREQUISITES];\n");
    fprintf(out, "} DcGamestatScene;\n\n");

    fprintf(out, "typedef struct {\n");
    fprintf(out, "    const char *const *names;\n");
    fprintf(out, "    int mission_count;\n");
    fprintf(out, "    const DcGamestatScene *missions;\n");
    fprintf(out, "} DcGamestatSceneTable;\n\n");
}

static void emit_units(FILE *out, const GameStatData *data) {
    fprintf(out, "static const DcGamestatUnit dc_gamestat_units[DC_GAMESTAT_UNIT_COUNT] = {\n");
    for (int i = 0; i < data->unit_count; ++i) {
        fprintf(out, "    { ");
        emit_c_string(out, data->units[i].sprite);
        fprintf(out, ", %d, ", data->units[i].value_count);
        emit_int_array(out, data->units[i].values, DC_GAMESTAT_UNIT_VALUE_COUNT);
        fprintf(out, " },\n");
    }
    fprintf(out, "};\n\n");
}

static void emit_depends(FILE *out, const GameStatData *data) {
    fprintf(out, "static const DcGamestatDepend dc_gamestat_depends[DC_GAMESTAT_DEPEND_COUNT] = {\n");
    for (int i = 0; i < data->depend_count; ++i) {
        const DependStat *dep = &data->depends[i];
        fprintf(out, "    { %d, %d, %d, %d, %d, %d, ",
                dep->row_id, dep->cost, dep->ui_id, dep->product_class,
                dep->product_type, dep->parameter_count);
        emit_int_array(out, dep->parameters, DC_GAMESTAT_MAX_DEPEND_PARAMS);
        fprintf(out, " },\n");
    }
    fprintf(out, "};\n\n");
}

static void emit_weapons(FILE *out, const GameStatData *data) {
    fprintf(out, "static const DcGamestatWeapon dc_gamestat_weapons[DC_GAMESTAT_WEAPON_COUNT] = {\n");
    for (int i = 0; i < data->weapon_count; ++i) {
        const WeaponStat *weapon = &data->weapons[i];
        fprintf(out, "    { %d, ", weapon->id);
        emit_c_string(out, weapon->sprite);
        for (int j = 0; j < DC_GAMESTAT_WEAPON_VALUE_COUNT; ++j) {
            fprintf(out, ", %d", weapon->values[j]);
        }
        fprintf(out, " },\n");
    }
    fprintf(out, "};\n\n");
}

static void emit_unit_ids(FILE *out, const GameStatData *data) {
    fprintf(out, "static const DcGamestatUnitId dc_gamestat_unit_ids[DC_GAMESTAT_UNIT_ID_COUNT] = {\n");
    for (int i = 0; i < data->unit_id_count; ++i) {
        const UnitIdStat *unit_id = &data->unit_ids[i];
        fprintf(out, "    { %d, %d, %d, %d },\n",
                unit_id->team, unit_id->weapon_class, unit_id->unit_type, unit_id->unit_id);
    }
    fprintf(out, "};\n\n");
}

static void emit_mbullet(FILE *out, const GameStatData *data) {
    fprintf(out, "static const DcGamestatMissileBullet dc_gamestat_mbullet = {\n");
    fprintf(out, "    %d, %d,\n", data->mbullet.width, data->mbullet.height);
    fprintf(out, "    {\n");
    for (int y = 0; y < DC_GAMESTAT_MBULLET_ROWS; ++y) {
        fprintf(out, "        ");
        emit_int_array(out, data->mbullet.values[y], DC_GAMESTAT_MBULLET_COLS);
        fprintf(out, ",\n");
    }
    fprintf(out, "    }\n");
    fprintf(out, "};\n\n");
}

static void emit_booms(FILE *out, const GameStatData *data) {
    fprintf(out, "static const DcGamestatBoom dc_gamestat_booms[DC_GAMESTAT_BOOM_COUNT] = {\n");
    for (int i = 0; i < data->boom_count; ++i) {
        const BoomStat *boom = &data->booms[i];
        fprintf(out, "    {\n");
        fprintf(out, "        %d, %d, %d,\n", boom->id, boom->size, boom->effect_count);
        fprintf(out, "        { ");
        for (int j = 0; j < DC_GAMESTAT_MAX_BOOM_EFFECTS; ++j) {
            if (j) fprintf(out, ", ");
            emit_c_string(out, boom->effects[j]);
        }
        fprintf(out, " },\n");
        fprintf(out, "        {\n");
        for (int y = 0; y < DC_GAMESTAT_MAX_BOOM_SIZE; ++y) {
            fprintf(out, "            ");
            emit_int_array(out, boom->damage[y], DC_GAMESTAT_MAX_BOOM_SIZE);
            fprintf(out, ",\n");
        }
        fprintf(out, "        },\n");
        fprintf(out, "        {\n");
        for (int y = 0; y < 3; ++y) {
            fprintf(out, "            ");
            emit_int_array(out, boom->falloff[y], 3);
            fprintf(out, ",\n");
        }
        fprintf(out, "        }\n");
        fprintf(out, "    },\n");
    }
    fprintf(out, "};\n\n");
}

static void emit_scene_mission(FILE *out, const SceneMission *mission) {
    fprintf(out, "    { ");
    emit_c_string(out, mission->scenario);
    fprintf(out, ", ");
    emit_c_string(out, mission->title);
    fprintf(out, ", ");
    emit_c_string(out, mission->region);
    fprintf(out, ", ");
    emit_c_string(out, mission->map_path);
    fprintf(out, ", ");
    emit_c_string(out, mission->intro_avi);
    fprintf(out, ", ");
    emit_c_string(out, mission->outro_avi);
    fprintf(out, ", ");
    emit_int_array(out, mission->color, 3);
    fprintf(out, ", %d, %d, ", mission->flag, mission->prerequisite_count);
    emit_int_array(out, mission->prerequisites, DC_GAMESTAT_MAX_SCENE_PREREQUISITES);
    fprintf(out, " },\n");
}

static void emit_scene_table(FILE *out, const char *symbol, const SceneTable *table) {
    fprintf(out, "static const DcGamestatScene dc_gamestat_%s_missions[] = {\n", symbol);
    for (int i = 0; i < table->mission_count; ++i) {
        emit_scene_mission(out, &table->missions[i]);
    }
    fprintf(out, "};\n\n");

    fprintf(out, "static const char *dc_gamestat_%s_names[8] = {\n", symbol);
    for (int i = 0; i < 8; ++i) {
        fprintf(out, "    ");
        emit_c_string(out, table->names[i]);
        fprintf(out, ",\n");
    }
    fprintf(out, "};\n\n");

    fprintf(out, "static const DcGamestatSceneTable dc_gamestat_%s = {\n", symbol);
    fprintf(out, "    dc_gamestat_%s_names,\n", symbol);
    fprintf(out, "    %d,\n", table->mission_count);
    fprintf(out, "    dc_gamestat_%s_missions\n", symbol);
    fprintf(out, "};\n\n");
}

static void emit_header(const char *path, const GameStatData *data) {
    FILE *out = fopen(path, "wb");
    if (!out) die(strerror(errno), path);
    emit_header_prelude(out, data);
    emit_units(out, data);
    emit_depends(out, data);
    emit_weapons(out, data);
    emit_unit_ids(out, data);
    emit_mbullet(out, data);
    emit_booms(out, data);
    emit_scene_table(out, "hscene", &data->hscene);
    emit_scene_table(out, "gscene", &data->gscene);
    emit_scene_table(out, "htscene", &data->htscene);
    emit_scene_table(out, "gtscene", &data->gtscene);
    fprintf(out, "#endif /* OPEN_RTS_DARK_COLONY_GAMESTAT_H */\n");
    if (fclose(out) != 0) die(strerror(errno), path);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        die("usage: dc_gamestat_gen DATA/DCOLONY/GAMESTAT plugins/DarkColony/gamestat.h", NULL);
    }

    GameStatData data;
    memset(&data, 0, sizeof(data));
    parse_all(argv[1], &data);
    emit_header(argv[2], &data);
    return 0;
}
