#define _DEFAULT_SOURCE
#include "engine.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DEFAULT_DATA_ROOT "data/REIGN/dark"
#define DEFAULT_UNIT_SPR  "ucfcnst0.spr"

/* ── definition-file parser ─────────────────────────────────────────────── */

typedef struct { char *units; char *buildings; char *overlay; char *animate; } DarkReignDefinitions;

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

static void replace_extension(char *dst, size_t dst_size, const char *path, const char *ext) {
    snprintf(dst, dst_size, "%s", path);
    char *dot = strrchr(dst, '.'), *slash = strrchr(dst, '/');
    if (dot && (!slash || dot > slash))
        snprintf(dot, dst_size - (size_t)(dot - dst), "%s", ext);
    else
        strncat(dst, ext, dst_size - strlen(dst) - 1);
}

static void copy_trimmed_token(char *dst, size_t dst_size, const char *src, size_t len) {
    while (len > 0 && isspace((unsigned char)*src)) { src++; len--; }
    while (len > 0 && isspace((unsigned char)src[len-1])) len--;
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len); dst[len] = '\0';
}

static void uppercase_trimmed_token(char *dst, size_t dst_size, const char *src, size_t len) {
    while (len > 0 && isspace((unsigned char)*src)) { src++; len--; }
    while (len > 0 && isspace((unsigned char)src[len-1])) len--;
    if (len >= dst_size) len = dst_size - 1;
    for (size_t i = 0; i < len; ++i) dst[i] = (char)toupper((unsigned char)src[i]);
    dst[len] = '\0';
}

static const char *find_case_insensitive(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) return haystack;
    size_t needle_len = strlen(needle);
    for (const char *p = haystack; *p; ++p)
        if (strncasecmp(p, needle, needle_len) == 0) return p;
    return NULL;
}

static const char *find_case_insensitive_n(const char *haystack, size_t haystack_len,
                                           const char *needle) {
    if (!haystack || !needle || !needle[0]) return haystack;
    size_t needle_len = strlen(needle);
    if (needle_len > haystack_len) return NULL;
    for (size_t i = 0; i + needle_len <= haystack_len; ++i)
        if (strncasecmp(haystack + i, needle, needle_len) == 0) return haystack + i;
    return NULL;
}

static bool dark_reign_is_commented_call(const char *body_start, const char *hit) {
    const char *p = hit;
    while (p > body_start && p[-1] != '\n' && p[-1] != '\r') p--;
    while (p < hit) { if (*p == ';') return true; p++; }
    return false;
}

static void dark_reign_scn_path_from_map(const char *map_path, char *scn_path,
                                         size_t scn_path_size) {
    if (!map_path || !scn_path || scn_path_size == 0) return;
    snprintf(scn_path, scn_path_size, "%s", map_path);
    size_t len = strlen(scn_path);
    if (len >= 4 && strcasecmp(scn_path + len - 4, ".SCN") == 0) return;
    if (len >= 11 && strcasecmp(scn_path + len - 11, "/TACTICS.MM") == 0) {
        char *slash = strrchr(scn_path, '/');
        if (!slash || slash == scn_path) {
            replace_extension(scn_path, scn_path_size, scn_path, ".SCN");
            return;
        }
        *slash = '\0';
        char *parent = strrchr(scn_path, '/');
        const char *stem = parent ? parent + 1 : scn_path;
        char rebuilt[1024];
        if (parent) {
            *parent = '\0';
            snprintf(rebuilt, sizeof(rebuilt), "%s/%s/%s.SCN", scn_path, stem, stem);
        } else {
            snprintf(rebuilt, sizeof(rebuilt), "%s/%s.SCN", stem, stem);
        }
        snprintf(scn_path, scn_path_size, "%s", rebuilt);
        return;
    }
    replace_extension(scn_path, scn_path_size, scn_path, ".SCN");
}

static void dark_reign_mm_path_from_map(const char *map_path, char *mm_path,
                                        size_t mm_path_size) {
    if (!map_path || !mm_path || mm_path_size == 0) return;
    snprintf(mm_path, mm_path_size, "%s", map_path);
    size_t len = strlen(mm_path);
    if (len >= 3 && strcasecmp(mm_path + len - 3, ".MM") == 0) return;
    char *slash = strrchr(mm_path, '/');
    if (!slash) return;
    slash[1] = '\0';
    strncat(mm_path, "TACTICS.MM", mm_path_size - strlen(mm_path) - 1);
}

static void dark_reign_map_path_from_scn(const char *scn_path, char *map_path,
                                         size_t map_path_size) {
    if (!scn_path || !map_path || map_path_size == 0) return;
    snprintf(map_path, map_path_size, "%s", scn_path);
    replace_extension(map_path, map_path_size, map_path, ".MAP");
}

static void dark_reign_root_from_map(const char *map_path, char *root, size_t root_size) {
    const char *scenario = find_case_insensitive(map_path, "/scenario/");
    if (!scenario) { snprintf(root, root_size, "%s", DEFAULT_DATA_ROOT); return; }
    size_t len = (size_t)(scenario - map_path);
    if (len >= root_size) len = root_size - 1;
    memcpy(root, map_path, len); root[len] = '\0';
}

static void dark_reign_load_definitions(const char *map_path, DarkReignDefinitions *defs) {
    memset(defs, 0, sizeof(*defs));
    char root[1024], path[1024];
    dark_reign_root_from_map(map_path, root, sizeof(root));
    M_PathJoin(path, sizeof(path), root, "deftxt/UNITS.TXT");   defs->units     = load_text_file(path);
    M_PathJoin(path, sizeof(path), root, "deftxt/BUILD.TXT");   defs->buildings = load_text_file(path);
    M_PathJoin(path, sizeof(path), root, "deftxt/OVERLAY.TXT"); defs->overlay   = load_text_file(path);
    M_PathJoin(path, sizeof(path), root, "deftxt/ANIMATE.TXT"); defs->animate   = load_text_file(path);
}

static void dark_reign_free_definitions(DarkReignDefinitions *defs) {
    free(defs->units); free(defs->buildings); free(defs->overlay); free(defs->animate);
    memset(defs, 0, sizeof(*defs));
}

static bool dark_reign_find_definition_block(const char *text, const char *define_call,
                                             const char *type_name, const char **body,
                                             size_t *body_len) {
    if (!text) return false;
    const char *cursor = text;
    while ((cursor = find_case_insensitive(cursor, define_call)) != NULL) {
        const char *open  = strchr(cursor, '(');
        const char *close = open ? strchr(open + 1, ')') : NULL;
        if (!open || !close) { cursor += strlen(define_call); continue; }
        char candidate[96];
        copy_trimmed_token(candidate, sizeof(candidate), open + 1, (size_t)(close - open - 1));
        if (strcasecmp(candidate, type_name) == 0) {
            const char *brace = strchr(close + 1, '{');
            if (!brace) return false;
            int depth = 0;
            for (const char *p = brace; *p; ++p) {
                if (*p == '{') depth++;
                else if (*p == '}') { if (--depth == 0) { *body = brace + 1; *body_len = (size_t)(p - (brace+1)); return true; } }
            }
            return false;
        }
        cursor = close + 1;
    }
    return false;
}

static bool dark_reign_find_call_arg(const char *body, size_t body_len, const char *call,
                                     char *dst, size_t dst_size) {
    const char *cursor = body;
    size_t remaining = body_len;
    while (remaining > 0) {
        const char *hit = find_case_insensitive_n(cursor, remaining, call);
        if (!hit) return false;
        if (!dark_reign_is_commented_call(body, hit)) {
            const char *open = strchr(hit, '(');
            if (open && open < body + body_len) {
                const char *arg = open + 1;
                while (arg < body + body_len && isspace((unsigned char)*arg)) arg++;
                const char *end = arg;
                while (end < body + body_len && *end != ')' && !isspace((unsigned char)*end)) end++;
                if (end > arg) { copy_trimmed_token(dst, dst_size, arg, (size_t)(end-arg)); return dst[0] != '\0'; }
            }
        }
        const char *next = hit + strlen(call);
        remaining = (size_t)((body + body_len) - next);
        cursor = next;
    }
    return false;
}

static int dark_reign_find_call_args(const char *body, size_t body_len, const char *call,
                                     char args[][32], int max_args) {
    if (!body || !call || !args || max_args <= 0) return 0;
    const char *hit = find_case_insensitive_n(body, body_len, call);
    if (!hit) return 0;
    const char *end = body + body_len;
    const char *cursor = hit + strlen(call);
    while (cursor < end && *cursor != '(') cursor++;
    if (cursor >= end) return 0;
    cursor++;

    int count = 0;
    while (cursor < end && *cursor != ')' && count < max_args) {
        while (cursor < end && isspace((unsigned char)*cursor)) cursor++;
        if (cursor >= end || *cursor == ')') break;
        const char *start = cursor;
        while (cursor < end && *cursor != ')' && !isspace((unsigned char)*cursor)) cursor++;
        if (cursor > start) {
            copy_trimmed_token(args[count], 32, start, (size_t)(cursor - start));
            if (args[count][0] != '\0') count++;
        }
    }
    return count;
}

static bool dark_reign_resolve_animation_sprite(const DarkReignDefinitions *defs,
                                                const char *animation_name,
                                                char *sprite_name, size_t sprite_name_size) {
    const char *body = NULL; size_t body_len = 0;
    if (!dark_reign_find_definition_block(defs->animate, "DefineAnimationType",
                                          animation_name, &body, &body_len)) return false;
    return dark_reign_find_call_arg(body, body_len, "SetSprite", sprite_name, sprite_name_size);
}

typedef struct {
    const char *type_name, *sprite_name, *shadow_name;
    isize2_t footprint;
    bool solid;
} DarkReignDecorationSpec;

#define DR_DECORATION_SPEC(type, sprite, shadow, width, height, is_solid) \
    { type, sprite, shadow, { width, height }, is_solid }

static const DarkReignDecorationSpec DARK_REIGN_DECORATION_SPECS[] = {
    DR_DECORATION_SPEC("clif1", "aoclf000.spr", "aoclf0sh.spr", 1, 4, true),
    DR_DECORATION_SPEC("clif2", "aoclf001.spr", "aoclf1sh.spr", 1, 3, true),
    DR_DECORATION_SPEC("clif3", "aoclf002.spr", "aoclf2sh.spr", 1, 3, true),
    DR_DECORATION_SPEC("clif4", "aoclf003.spr", "aoclf3sh.spr", 3, 4, true),
    DR_DECORATION_SPEC("clif5", "aoclf004.spr", "aoclf4sh.spr", 3, 5, true),
    DR_DECORATION_SPEC("clif6", "aoclf005.spr", "aoclf5sh.spr", 3, 3, true),
    DR_DECORATION_SPEC("plnt1", "aopln000.spr", "aopln0sh.spr", 1, 1, false),
    DR_DECORATION_SPEC("plnt2", "aopln001.spr", "aopln1sh.spr", 1, 1, false),
    DR_DECORATION_SPEC("plnt3", "aopln002.spr", "aopln2sh.spr", 1, 1, false),
    DR_DECORATION_SPEC("rock1", "aoroc000.spr", "aoroc0sh.spr", 1, 1, true),
    DR_DECORATION_SPEC("rock2", "aoroc001.spr", "aoroc1sh.spr", 1, 1, true),
    DR_DECORATION_SPEC("rock3", "aoroc002.spr", "aoroc2sh.spr", 1, 1, true),
    DR_DECORATION_SPEC("rock4", "aoroc003.spr", "aoroc3sh.spr", 3, 3, true),
    DR_DECORATION_SPEC("rock5", "aoroc004.spr", "aoroc4sh.spr", 3, 3, true),
    DR_DECORATION_SPEC("rock6", "aoroc005.spr", "aoroc5sh.spr", 3, 3, true),
    DR_DECORATION_SPEC("tree1", "aotre000.spr", "aotre0sh.spr", 1, 1, true),
    DR_DECORATION_SPEC("tree2", "aotre001.spr", "aotre1sh.spr", 1, 1, true),
    DR_DECORATION_SPEC("tree3", "aotre002.spr", "aotre2sh.spr", 1, 1, true),
    DR_DECORATION_SPEC("tree4", "aotre003.spr", "aotre3sh.spr", 1, 1, true),
    DR_DECORATION_SPEC("tree5", "aotre004.spr", "aotre4sh.spr", 1, 1, true),
    DR_DECORATION_SPEC("tree6", "aotre005.spr", "aotre5sh.spr", 1, 1, true),
    DR_DECORATION_SPEC("rubble1", "aorub000.spr", "aorub0sh.spr", 1, 1, false),
    DR_DECORATION_SPEC("rubble2", "aorub001.spr", "aorub1sh.spr", 1, 1, false),
    DR_DECORATION_SPEC("rubble3", "aorub002.spr", "aorub2sh.spr", 1, 1, false),
    DR_DECORATION_SPEC("water1", "aowtr000.spr", "aowtr0sh.spr", 1, 1, false),
    DR_DECORATION_SPEC("water2", "aowtr001.spr", "aowtr1sh.spr", 1, 1, false),
    DR_DECORATION_SPEC("water3", "aowtr002.spr", "aowtr2sh.spr", 1, 1, false),
    DR_DECORATION_SPEC("impww", "ncwel1l0.spr", "", 3, 3, false),
    DR_DECORATION_SPEC("impmn", "ncmin1l0.spr", "", 3, 3, false),
};

#undef DR_DECORATION_SPEC

typedef struct {
    char sprite_name[32], sprite2_name[32], sprite3_name[32], shadow_name[32];
    isize2_t footprint;
    bool solid;
    bool center_anchor;
    bool has_sprite_pivot;
    ivec2_t sprite_pivot;
    int frame_index;
} DarkReignVisualSpec;

static bool dark_reign_visual_from_static(const char *type_name, DarkReignVisualSpec *out) {
    size_t count = sizeof(DARK_REIGN_DECORATION_SPECS) / sizeof(DARK_REIGN_DECORATION_SPECS[0]);
    for (size_t i = 0; i < count; ++i) {
        if (strcasecmp(DARK_REIGN_DECORATION_SPECS[i].type_name, type_name) == 0) {
            snprintf(out->sprite_name, sizeof(out->sprite_name), "%s", DARK_REIGN_DECORATION_SPECS[i].sprite_name);
            snprintf(out->shadow_name, sizeof(out->shadow_name), "%s", DARK_REIGN_DECORATION_SPECS[i].shadow_name);
            out->footprint = DARK_REIGN_DECORATION_SPECS[i].footprint;
            out->solid = DARK_REIGN_DECORATION_SPECS[i].solid;
            return true;
        }
    }
    return false;
}

static bool dark_reign_resolve_unit_visual(const DarkReignDefinitions *defs, const char *type_name,
                                           DarkReignVisualSpec *out) {
    memset(out, 0, sizeof(*out));
    const char *body = NULL; size_t body_len = 0;
    if (!dark_reign_find_definition_block(defs->units, "DefineUnitType", type_name, &body, &body_len))
        return dark_reign_visual_from_static(type_name, out);
    if (!dark_reign_find_call_arg(body, body_len, "SetImage", out->sprite_name, sizeof(out->sprite_name)))
        return false;
    dark_reign_find_call_arg(body, body_len, "SetShadowImage", out->shadow_name, sizeof(out->shadow_name));
    out->footprint = (isize2_t){ 1, 1 };
    out->solid = false;
    return true;
}

static bool dark_reign_resolve_building_visual(const DarkReignDefinitions *defs, const char *type_name,
                                               DarkReignVisualSpec *out) {
    memset(out, 0, sizeof(*out));
    const char *body = NULL; size_t body_len = 0;
    if (!dark_reign_find_definition_block(defs->buildings, "DefineBuildingType", type_name, &body, &body_len))
        return dark_reign_visual_from_static(type_name, out);
    char images[3][32] = {{ 0 }};
    if (dark_reign_find_call_args(body, body_len, "SetBuildingImages", images, 3) < 2)
        return false;

    /* Completed Dark Reign buildings are composites. The terrain archive
       supplies the ground underlay while the shared archive supplies the body
       and top layer, even though the underlay and body reuse a basename. */
    snprintf(out->sprite_name, sizeof(out->sprite_name), "tileset|%s", images[0]);
    snprintf(out->sprite2_name, sizeof(out->sprite2_name), "base|%s", images[0]);
    snprintf(out->sprite3_name, sizeof(out->sprite3_name), "base|%s", images[1]);
    char shadow[32] = { 0 };
    if (dark_reign_find_call_arg(body, body_len, "SetShadowImage", shadow, sizeof(shadow)))
        snprintf(out->shadow_name, sizeof(out->shadow_name), "base|%s", shadow);

    out->footprint = (isize2_t){ 3, 3 };
    if (strcasecmp(type_name, "fh1") == 0 || strcasecmp(type_name, "fh2") == 0 ||
        strcasecmp(type_name, "fh3") == 0) {
        out->footprint = (isize2_t){ 4, 4 };
    } else if (strcasecmp(type_name, "fglp") == 0) {
        out->footprint = (isize2_t){ 4, 3 };
    } else if (strcasecmp(type_name, "fgpp") == 0) {
        out->footprint = (isize2_t){ 3, 4 };
    } else if (strcasecmp(type_name, "CivilianBridge") == 0 ||
               strcasecmp(type_name, "CivilianVerticalBridge") == 0) {
        out->footprint = (isize2_t){ 4, 3 };
    } else if (find_case_insensitive_n(type_name, strlen(type_name), "SmallHorizontalBridge") ||
               find_case_insensitive_n(type_name, strlen(type_name), "SmallVerticalBridge") ||
               find_case_insensitive_n(type_name, strlen(type_name), "SmallCentreBridge")) {
        out->footprint = (isize2_t){ 4, 4 };
    }
    /* Resource nodes (impmn, impww) must be passable: harvesters walk into
       them to reach the attachment point at the centre of the footprint. */
    out->solid = !(strcasecmp(type_name, "impmn") == 0 ||
                   strcasecmp(type_name, "impww") == 0);
    /* AddBuildingAt coordinates identify the top-left of Dark Reign's authored
       RSPR canvas.  They are not the top-left of a collision footprint.  The
       canvas sizes (for example 144x120 for the bridge and 120x144 for the FG
       HQ) deliberately include the structure's complete placement envelope. */
    out->has_sprite_pivot = true;
    out->sprite_pivot = (ivec2_t){ 0, 0 };
    out->frame_index = 1;
    return true;
}

static bool dark_reign_resolve_thing_visual(const DarkReignDefinitions *defs, const char *type_name,
                                            DarkReignVisualSpec *out) {
    memset(out, 0, sizeof(*out));
    if (dark_reign_visual_from_static(type_name, out)) return true;
    const char *body = NULL; size_t body_len = 0;
    if (!dark_reign_find_definition_block(defs->overlay, "DefineThingType", type_name, &body, &body_len))
        return false;
    char animation[64] = { 0 };
    if (!dark_reign_find_call_arg(body, body_len, "SetThingImage", animation, sizeof(animation)) ||
        !dark_reign_resolve_animation_sprite(defs, animation, out->sprite_name, sizeof(out->sprite_name)))
        return false;
    char shadow_animation[64] = { 0 };
    if (dark_reign_find_call_arg(body, body_len, "SetThingShadowImage", shadow_animation, sizeof(shadow_animation)))
        dark_reign_resolve_animation_sprite(defs, shadow_animation, out->shadow_name, sizeof(out->shadow_name));
    out->footprint = (isize2_t){ 1, 1 };
    out->solid = find_case_insensitive_n(body, body_len, "IsCrater") == NULL &&
                 find_case_insensitive_n(body, body_len, "NoEdit") == NULL;
    return true;
}

static int compare_map_decorations(const void *a, const void *b) {
    const mapdecoration_t *da = a, *db = b;
    int ya = da->cell.y + da->footprint.h, yb = db->cell.y + db->footprint.h;
    if (ya != yb) return ya - yb;
    return da->cell.x - db->cell.x;
}

static void add_dark_reign_decoration(level_t *map, const DarkReignVisualSpec *spec,
                                      ivec2_t cell) {
    if (!spec || cell.x < 0 || cell.y < 0 || cell.x >= map->width || cell.y >= map->height ||
        map->decoration_count >= MAX_DECORATIONS) return;
    mapdecoration_t *dec = &map->decorations[map->decoration_count++];
    dec->cell = cell;
    dec->footprint = spec->footprint;
    dec->solid = spec->solid;
    dec->center_anchor = spec->center_anchor;
    dec->has_sprite_pivot = spec->has_sprite_pivot;
    dec->sprite_pivot = spec->sprite_pivot;
    dec->frame_index = spec->frame_index;
    dec->frame2_index = spec->frame_index;
    dec->frame3_index = spec->frame_index;
    snprintf(dec->sprite_name, sizeof(dec->sprite_name), "%s", spec->sprite_name);
    snprintf(dec->sprite2_name, sizeof(dec->sprite2_name), "%s", spec->sprite2_name);
    snprintf(dec->sprite3_name, sizeof(dec->sprite3_name), "%s", spec->sprite3_name);
    snprintf(dec->shadow_name, sizeof(dec->shadow_name), "%s", spec->shadow_name);
    if (!spec->solid) return;
    for (int y = 0; y < spec->footprint.h; ++y)
        for (int x = 0; x < spec->footprint.w; ++x) {
            int mx = cell.x + x, my = cell.y + y;
            if (mx >= 0 && my >= 0 && mx < map->width && my < map->height)
                map->blocked[L_Index(map, mx, my)] = 1;
        }
}

static void load_dark_reign_decorations(const char *map_path, level_t *map) {
    DarkReignDefinitions defs;
    dark_reign_load_definitions(map_path, &defs);
    char scn_path[1024];
    dark_reign_scn_path_from_map(map_path, scn_path, sizeof(scn_path));
    blob_t blob;
    if (!W_ReadFile(scn_path, &blob)) { dark_reign_free_definitions(&defs); return; }
    char *text = malloc(blob.size + 1);
    if (!text) { W_FreeFile(&blob); dark_reign_free_definitions(&defs); return; }
    memcpy(text, blob.bytes, blob.size); text[blob.size] = '\0';
    char *cursor = text;
    while (map->decoration_count < MAX_DECORATIONS) {
        char *thing_hit    = strstr(cursor, "AddThingAt(");
        char *building_hit = strstr(cursor, "AddBuildingAt(");
        bool building = false;
        char *hit = thing_hit;
        if (building_hit && (!hit || building_hit < hit)) { hit = building_hit; building = true; }
        if (!hit) break;
        int object_id = 0, gx = 0, gy = 0;
        char type_name[64] = { 0 };
        int parsed = building ?
            sscanf(hit, "AddBuildingAt(%d %63[^ )] %d %d", &object_id, type_name, &gx, &gy) :
            sscanf(hit, "AddThingAt(%d %63[^ )] %d %d",    &object_id, type_name, &gx, &gy);
        if (parsed == 4) {
            (void)object_id;
            DarkReignVisualSpec visual;
            bool resolved = building ?
                dark_reign_resolve_building_visual(&defs, type_name, &visual) :
                dark_reign_resolve_thing_visual(&defs, type_name, &visual);
            if (resolved) add_dark_reign_decoration(map, &visual, (ivec2_t){ gx, gy });
            else fprintf(stderr, "warning: unresolved Dark Reign %s type %s\n",
                         building ? "building" : "thing", type_name);
        }
        cursor = hit + (building ? strlen("AddBuildingAt(") : strlen("AddThingAt("));
    }
    free(text); W_FreeFile(&blob); dark_reign_free_definitions(&defs);
    qsort(map->decorations, (size_t)map->decoration_count, sizeof(mapdecoration_t),
          compare_map_decorations);
}

/* Default taelon amount and harvest rate per mine.  The BUILD.TXT definition
   lists SetResource(1 1 500 40) but does not encode per-scenario deposits; we
   use a fixed baseline that matches the original game's economy pacing. */
#define DR_TAELON_MINE_AMOUNT 3000
#define DR_TAELON_MINE_RATE   20

static void load_dark_reign_resource_vents(const char *map_path, level_t *map) {
    char scn_path[1024];
    dark_reign_scn_path_from_map(map_path, scn_path, sizeof(scn_path));
    blob_t blob;
    if (!W_ReadFile(scn_path, &blob)) return;
    char *text = malloc(blob.size + 1);
    if (!text) { W_FreeFile(&blob); return; }
    memcpy(text, blob.bytes, blob.size); text[blob.size] = '\0';
    const char *tag = "AddBuildingAt(";
    char *cursor = text;
    while (1) {
        char *hit = strstr(cursor, tag);
        if (!hit) break;
        int object_id = 0, gx = 0, gy = 0;
        char type_name[64] = { 0 };
        if (sscanf(hit, "AddBuildingAt(%d %63[^ )] %d %d",
                   &object_id, type_name, &gx, &gy) == 4 &&
            strcasecmp(type_name, "impmn") == 0 &&
            L_Contains(map, gx, gy)) {
            (void)object_id;
            resourcevent_t *vents = realloc(map->resource_vents,
                (size_t)(map->resource_vent_count + 1) * sizeof(resourcevent_t));
            if (vents) {
                map->resource_vents = vents;
                resourcevent_t *v = &map->resource_vents[map->resource_vent_count++];
                v->cell = (ivec2_t){ gx, gy };
                /* 3×3 mine footprint: attach to the centre cell */
                v->attachment = (fvec2_t){ (float)gx + 1.5f, (float)gy + 1.5f };
                v->amount = DR_TAELON_MINE_AMOUNT;
                v->rate = DR_TAELON_MINE_RATE;
                v->active = true;
                v->resource_type = 0;
                v->decoration_index = -1;
            }
        }
        cursor = hit + strlen(tag);
    }
    free(text); W_FreeFile(&blob);
}

static void load_dark_reign_team_credits(const char *map_path, level_t *map) {
    if (!map) return;
    char scn_path[1024];
    dark_reign_scn_path_from_map(map_path, scn_path, sizeof(scn_path));
    blob_t blob;
    if (!W_ReadFile(scn_path, &blob)) return;
    char *text = malloc(blob.size + 1);
    if (!text) { W_FreeFile(&blob); return; }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';

    int current_team = -1;
    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char nl = *next;
            *next++ = '\0';
            if (nl == '\r' && *next == '\n') next++;
        }
        while (isspace((unsigned char)*line)) line++;
        int team = -1;
        int credit = 0;
        if (sscanf(line, "SetTeam(%d", &team) == 1) {
            current_team = team;
        } else if (current_team >= 0 && current_team < 8 &&
                   sscanf(line, "SetCredit(%d", &credit) == 1) {
            map->player_resources[current_team][0] = credit;
        } else if (current_team == 0) {
            int world_x = 0, world_y = 0;
            if (sscanf(line, "SetStartLocation(%d %d", &world_x, &world_y) == 2) {
                /* Dark Reign stores scenario starts in world pixels. */
                map->has_camera = true;
                map->camera = (fvec2_t){
                    (float)world_x / 24.0f,
                    (float)world_y / 24.0f,
                };
            }
        }
        line = next;
    }

    free(text);
    W_FreeFile(&blob);
}

/* ── tileset detection ──────────────────────────────────────────────────── */

static void detect_tileset_from_mm(const char *map_path, char *tileset, size_t tileset_size) {
    strncpy(tileset, "BARREN", tileset_size - 1); tileset[tileset_size-1] = '\0';
    char mm_path[1024];
    dark_reign_mm_path_from_map(map_path, mm_path, sizeof(mm_path));
    blob_t blob;
    if (!W_ReadFile(mm_path, &blob)) return;
    if (blob.size >= 28) {
        char raw[17]; memcpy(raw, blob.bytes + 12, 16); raw[16] = '\0';
        size_t n = strnlen(raw, sizeof(raw));
        while (n > 0 && isspace((unsigned char)raw[n-1])) raw[--n] = '\0';
        for (size_t i = 0; i < n; ++i) raw[i] = (char)toupper((unsigned char)raw[i]);
        if (n > 0) { strncpy(tileset, raw, tileset_size - 1); tileset[tileset_size-1] = '\0'; }
    }
    W_FreeFile(&blob);
}

static void detect_tileset_from_scn(const char *map_path, char *tileset, size_t tileset_size) {
    char scn_path[1024];
    dark_reign_scn_path_from_map(map_path, scn_path, sizeof(scn_path));
    blob_t blob;
    if (!W_ReadFile(scn_path, &blob)) return;
    char *text = malloc(blob.size + 1);
    if (!text) { W_FreeFile(&blob); return; }
    memcpy(text, blob.bytes, blob.size); text[blob.size] = '\0';
    const char *tag = "SetDefaultTerrain(";
    char *hit = strstr(text, tag);
    if (hit) {
        hit += strlen(tag);
        char *end = strchr(hit, ')');
        if (end && end > hit)
            uppercase_trimmed_token(tileset, tileset_size, hit, (size_t)(end - hit));
    }
    free(text); W_FreeFile(&blob);
}

/* ── edge transition renderer ───────────────────────────────────────────── */

typedef enum {
    EDGE_MATCH_BELOW,
    EDGE_MATCH_EQUAL,
} EdgeMatchType;

typedef struct {
    bool self_below;
    int set_type;
    EdgeMatchType northwest;
    EdgeMatchType north;
    EdgeMatchType west;
} EdgeMatchRule;

typedef struct {
    int layer;
    int frame;
} DarkReignEdgeFrame;

static const EdgeMatchRule DARK_REIGN_EDGE_RULES[] = {
    { false, 36, EDGE_MATCH_BELOW, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL },
    { true,  37, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL },
    { true,  34, EDGE_MATCH_EQUAL, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW },
    { false, 35, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW },
    { false, 32, EDGE_MATCH_BELOW, EDGE_MATCH_BELOW, EDGE_MATCH_BELOW },
    { true,  33, EDGE_MATCH_BELOW, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL },
    { true,  31, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW },
    { true,  30, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW, EDGE_MATCH_BELOW },
    { false, 39, EDGE_MATCH_EQUAL, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW },
    { true,  38, EDGE_MATCH_EQUAL, EDGE_MATCH_EQUAL, EDGE_MATCH_EQUAL },
    { false, 40, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL, EDGE_MATCH_EQUAL },
    { false, 41, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL },
    { true,  43, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL, EDGE_MATCH_EQUAL },
    { false, 42, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW, EDGE_MATCH_BELOW },
};

static int dark_reign_terrain_type_from_frame(int frame) {
    if (frame < 8) return 15;
    if (frame < 128) return (frame - 8) / 8;
    return 0;
}

static int dark_reign_base_frame_for_type(int terrain_type, int variation) {
    variation &= 7;
    if (terrain_type == 15) return variation;
    return 8 + terrain_type * 8 + variation;
}

static bool dark_reign_terrain_is_blocked(int terrain_type) {
    /* The MAP record's third byte is the authored terrain/effect id.  In
       TRNEFF.TXT, 0 is liquid and 3 is impassable rock/wall.  Values 10..15
       are auto-ridge/rim variants; they are visual/elevation data, not
       blanket collision flags. */
    return terrain_type == 0 || terrain_type == 3;
}

static int dark_reign_edge_frame_for_template(int template_id, int variation) {
    if (template_id >= 226 && template_id <= 239) {
        (void)variation;
        return 1032 + (template_id - 226) * 4;
    }
    return template_id + 218;
}

static int dark_reign_neighbor_type(const level_t *map, int x, int y, int fallback) {
    if (!L_Contains(map, x, y)) return fallback;
    return dark_reign_terrain_type_from_frame(map->tile_ids[L_Index(map, x, y)]);
}

static bool dark_reign_rule_matches(EdgeMatchType match, int neighbor_type, int self_value) {
    if (match == EDGE_MATCH_BELOW) return neighbor_type < self_value;
    return neighbor_type >= self_value;
}

static void render_dark_reign_edges_for_cell(app_t *app, const level_t *map, const tileset_t *tileset,
                                             int x, int y, int dx, int dy) {
    int frame = map->tile_ids[L_Index(map, x, y)] % tileset->count;
    int self_type = dark_reign_terrain_type_from_frame(frame);
    int variation = frame < 8 ? frame : (frame - 8) & 7;
    int northwest_type = dark_reign_neighbor_type(map, x - 1, y - 1, self_type);
    int north_type = dark_reign_neighbor_type(map, x, y - 1, self_type);
    int west_type = dark_reign_neighbor_type(map, x - 1, y, self_type);
    int neighbor_types[3] = { northwest_type, north_type, west_type };
    int shim_type = -1;
    bool used[16] = { false };
    DarkReignEdgeFrame edge_frames[32];
    int edge_frame_count = 0;

    irect_t whole = { 0, 0, tileset->tile_w, tileset->tile_h };
    irect_t dst = { dx, dy, tileset->tile_w, tileset->tile_h };

    for (size_t i = 0; i < sizeof(DARK_REIGN_EDGE_RULES) / sizeof(DARK_REIGN_EDGE_RULES[0]); ++i) {
        const EdgeMatchRule *rule = &DARK_REIGN_EDGE_RULES[i];
        EdgeMatchType matches[3] = { rule->northwest, rule->north, rule->west };
        int self_value = self_type;

        if (rule->self_below) {
            bool found_equal_neighbor = false;
            int lowest_equal = 256;
            for (int n = 0; n < 3; ++n) {
                if (matches[n] == EDGE_MATCH_EQUAL && neighbor_types[n] < lowest_equal) {
                    lowest_equal = neighbor_types[n];
                    found_equal_neighbor = true;
                }
            }
            if (found_equal_neighbor) {
                self_value = lowest_equal;
                if (self_value < 1 || self_value == self_type) continue;
            }
        }

        bool all_match = true;
        int lowest_match_value = -1;
        for (int n = 0; n < 3; ++n) {
            int neighbor_type = neighbor_types[n];
            if (!dark_reign_rule_matches(matches[n], neighbor_type, self_value)) {
                all_match = false;
                break;
            }
            if (matches[n] == EDGE_MATCH_BELOW) {
                if (!rule->self_below && (shim_type < 0 || neighbor_type < shim_type)) {
                    shim_type = neighbor_type;
                }
            } else if (neighbor_type >= 1 && (lowest_match_value < 0 || neighbor_type < lowest_match_value)) {
                lowest_match_value = neighbor_type;
            }
        }
        if (!all_match) continue;

        if (!rule->self_below && lowest_match_value > self_type) lowest_match_value = self_type;
        if (lowest_match_value < 0) lowest_match_value = self_type;
        if (lowest_match_value < 1 || lowest_match_value > 15 || used[lowest_match_value]) continue;
        used[lowest_match_value] = true;

        int template_id = rule->set_type + (lowest_match_value - 1) * 14;
        int edge_frame = dark_reign_edge_frame_for_template(template_id, variation);
        if (edge_frame >= 0 && edge_frame < tileset->count &&
            edge_frame_count < (int)(sizeof(edge_frames) / sizeof(edge_frames[0]))) {
            edge_frames[edge_frame_count++] = (DarkReignEdgeFrame){ lowest_match_value, edge_frame };
        }
    }

    if (shim_type >= 0 && shim_type != 15) {
        int shim_frame = dark_reign_base_frame_for_type(shim_type, variation);
        if (shim_frame >= 0 && shim_frame < tileset->count) {
            R_DrawTile(app, tileset, shim_frame, whole, dst);
        }
    }

    for (int i = 1; i < edge_frame_count; ++i) {
        DarkReignEdgeFrame edge = edge_frames[i];
        int j = i - 1;
        while (j >= 0 && edge_frames[j].layer > edge.layer) {
            edge_frames[j + 1] = edge_frames[j];
            j--;
        }
        edge_frames[j + 1] = edge;
    }

    for (int i = 0; i < edge_frame_count; ++i) {
        R_DrawTile(app, tileset, edge_frames[i].frame, whole, dst);
    }
}

/* ── map loader ─────────────────────────────────────────────────────────── */

bool load_dark_map(const char *map_path, level_t *out) {
    memset(out, 0, sizeof(*out));
    blob_t blob;
    if (!W_ReadFile(map_path, &blob)) {
        char fallback_mm[1024];
        dark_reign_mm_path_from_map(map_path, fallback_mm, sizeof(fallback_mm));
        if (strcmp(fallback_mm, map_path) == 0 || !W_ReadFile(fallback_mm, &blob)) {
            return false;
        }
    }

    size_t map_len = strlen(map_path);
    if (map_len >= 4 && strcasecmp(map_path + map_len - 4, ".SCN") == 0) {
        char terrain_path[1024];
        dark_reign_map_path_from_scn(map_path, terrain_path, sizeof(terrain_path));
        blob_t map_blob;
        if (!W_ReadFile(terrain_path, &map_blob)) {
            fprintf(stderr, "failed to load sibling Dark Reign MAP terrain %s\n", terrain_path);
            W_FreeFile(&blob);
            return false;
        }
        W_FreeFile(&blob);
        blob = map_blob;
    }

    int width = 0;
    int height = 0;
    size_t record_count = 0;
    bool map_record_format = blob.size >= 20 && memcmp(blob.bytes, "MAP_", 4) == 0;
    if (map_record_format) {
        width  = read_i32_le(blob.bytes + 8);
        height = read_i32_le(blob.bytes + 12);
        record_count = (size_t)width * (size_t)height;
        if (width <= 0 || height <= 0 || width > 512 || height > 512 ||
            blob.size < 20 + record_count * 6) {
            fprintf(stderr, "%s has unsupported map dimensions\n", map_path);
            W_FreeFile(&blob); return false;
        }
    } else {
        if (blob.size < 12) {
            fprintf(stderr, "%s is not a supported Dark Reign map/MM file\n", map_path);
            W_FreeFile(&blob); return false;
        }
        width = read_i32_le(blob.bytes + 4);
        height = read_i32_le(blob.bytes + 8);
        record_count = (size_t)width * (size_t)height;
        if (width <= 0 || height <= 0 || width > 512 || height > 512 || record_count == 0) {
            fprintf(stderr, "%s has unsupported Dark Reign MM dimensions\n", map_path);
            W_FreeFile(&blob); return false;
        }
    }

    out->width  = width;
    out->height = height;
    out->tile_ids   = calloc(record_count, sizeof(uint16_t));
    out->blocked    = calloc(record_count, sizeof(uint8_t));
    out->decorations = calloc(MAX_DECORATIONS, sizeof(mapdecoration_t));
    if (!out->tile_ids || !out->blocked || !out->decorations) { W_FreeFile(&blob); return false; }
    if (map_record_format) {
        const uint8_t *records = blob.bytes + 20;
        for (size_t i = 0; i < record_count; ++i) {
            const uint8_t *record = records + i * 6;
            uint8_t tile_byte = record[0], tile_variation = record[1];
            int terrain_type = record[2];
            uint8_t subindex  = (uint8_t)(tile_byte / 64);
            uint8_t variation = (uint8_t)(subindex * (tile_variation + 1));
            if (variation > 7) variation = 7;
            uint16_t frame = terrain_type == 15 ? variation :
                             (uint16_t)(8 + terrain_type * 8 + variation);
            out->tile_ids[i] = frame;
            out->blocked[i]  = dark_reign_terrain_is_blocked(terrain_type);
        }
    } else {
        size_t terrain_offset = 0;
        if (blob.size >= 32) {
            int32_t maybe_offset = read_i32_le(blob.bytes + 28);
            if (maybe_offset >= 0 && (size_t)maybe_offset < blob.size) {
                terrain_offset = (size_t)maybe_offset;
            }
        }
        if (terrain_offset == 0) terrain_offset = 32;
        if (terrain_offset >= blob.size) terrain_offset = 0;
        const uint8_t *terrain = blob.bytes + terrain_offset;
        size_t terrain_bytes = blob.size - terrain_offset;
        size_t max_cells = terrain_bytes * 2;
        for (size_t i = 0; i < record_count; ++i) {
            uint8_t terrain_type = 15;
            if (i < max_cells) {
                uint8_t packed = terrain[i / 2];
                terrain_type = (i & 1u) == 0 ? (packed & 0x0fu) : ((packed >> 4) & 0x0fu);
            }
            uint8_t variation = (uint8_t)((i + (size_t)(i / width)) & 7u);
            uint16_t frame = terrain_type == 15 ? variation :
                             (uint16_t)(8 + terrain_type * 8 + variation);
            out->tile_ids[i] = frame;
            out->blocked[i]  = dark_reign_terrain_is_blocked(terrain_type);
        }
    }
    detect_tileset_from_mm(map_path,  out->tileset_name, sizeof(out->tileset_name));
    detect_tileset_from_scn(map_path, out->tileset_name, sizeof(out->tileset_name));
    out->render_capabilities |= MAP_RENDER_CAP_TERRAIN_TRANSITIONS;
    out->render_transitions = render_dark_reign_edges_for_cell;
    load_dark_reign_decorations(map_path, out);
    load_dark_reign_resource_vents(map_path, out);
    load_dark_reign_team_credits(map_path, out);
    W_FreeFile(&blob);
    return true;
}

/* ── unit SCN parser ────────────────────────────────────────────────────── */

int load_dark_reign_initial_units(const char *map_path, mobj_t *units, int max_units) {
    if (max_units <= 0) return 0;
    DarkReignDefinitions defs;
    dark_reign_load_definitions(map_path, &defs);
    char scn_path[1024];
    dark_reign_scn_path_from_map(map_path, scn_path, sizeof(scn_path));
    blob_t blob;
    if (!W_ReadFile(scn_path, &blob)) { dark_reign_free_definitions(&defs); return 0; }
    char *text = malloc(blob.size + 1);
    if (!text) { W_FreeFile(&blob); dark_reign_free_definitions(&defs); return 0; }
    memcpy(text, blob.bytes, blob.size); text[blob.size] = '\0';

    int count = 0;
    bool has_player_unit = false;
    int current_team = 0;
    const char *team_tag = "SetDefaultTeam(";
    const char *unit_tag = "PutUnitAt(";
    char *cursor = text;
    while (count < max_units) {
        char *team_hit = strstr(cursor, team_tag);
        char *hit = strstr(cursor, unit_tag);
        if (team_hit && (!hit || team_hit < hit)) {
            int team = 0;
            if (sscanf(team_hit, "SetDefaultTeam(%d", &team) == 1) current_team = team;
            cursor = team_hit + strlen(team_tag);
            continue;
        }
        if (!hit) break;
        int object_id = 0, gx = 0, gy = 0;
        char unit_type[64] = { 0 };
        if (sscanf(hit, "PutUnitAt(%d %63[^ )] %d %d", &object_id, unit_type, &gx, &gy) == 4) {
            (void)object_id;
            if (gx >= 0 && gy >= 0) {
                units[count].core.position = fixedvec3_from_fvec2(
                    fvec2_cell_center((ivec2_t){ gx, gy }), 0);
                units[count].speed = 5.5f;
                units[count].owner = current_team >= 0 && current_team < 8 ?
                    (uint8_t)current_team : 1;
                if (units[count].owner == 0) has_player_unit = true;
                units[count].selected = units[count].owner == 0 && count == 0;
                DarkReignVisualSpec visual;
                if (dark_reign_resolve_unit_visual(&defs, unit_type, &visual)) {
                    snprintf(units[count].core.sprite_name,
                             sizeof(units[count].core.sprite_name), "%s", visual.sprite_name);
                    snprintf(units[count].shadow_name, sizeof(units[count].shadow_name), "%s", visual.shadow_name);
                } else {
                    snprintf(units[count].core.sprite_name,
                             sizeof(units[count].core.sprite_name), "%s", DEFAULT_UNIT_SPR);
                    units[count].shadow_name[0] = '\0';
                    fprintf(stderr, "warning: unresolved Dark Reign unit type %s\n", unit_type);
                }
                count++;
            }
        }
        cursor = hit + strlen(unit_tag);
    }

    /* Campaign maps may derive their opening freighter from a player's
       AssociatedUnit building declaration rather than PutUnitAt. Resolve that
       relationship through BUILD.TXT and place it at the scenario start. */
    if (!has_player_unit && count < max_units) {
        int team = -1;
        int start_x = 0, start_y = 0;
        bool have_start = false;
        char associated_type[64] = { 0 };
        for (char *line = text; line && *line;) {
            char *next = strpbrk(line, "\r\n");
            if (next) {
                char nl = *next;
                *next++ = '\0';
                if (nl == '\r' && *next == '\n') next++;
            }
            while (isspace((unsigned char)*line)) line++;
            int parsed_team = 0;
            if (sscanf(line, "SetTeam(%d", &parsed_team) == 1) team = parsed_team;
            if (team == 0 && sscanf(line, "SetStartLocation(%d %d", &start_x, &start_y) == 2)
                have_start = true;
            if (sscanf(line, "SetDefaultTeam(%d", &parsed_team) == 1) team = parsed_team;
            if (team == 0 && associated_type[0] == '\0') {
                int object_id = 0, gx = 0, gy = 0;
                char building_type[64] = { 0 };
                if (sscanf(line, "AddBuildingAt(%d %63[^ )] %d %d",
                           &object_id, building_type, &gx, &gy) == 4) {
                    (void)object_id; (void)gx; (void)gy;
                    const char *body = NULL;
                    size_t body_len = 0;
                    if (dark_reign_find_definition_block(defs.buildings, "DefineBuildingType",
                                                         building_type, &body, &body_len)) {
                        dark_reign_find_call_arg(body, body_len, "AssociatedUnit",
                                                 associated_type, sizeof(associated_type));
                    }
                }
            }
            line = next;
        }
        if (have_start && associated_type[0] != '\0') {
            mobj_t *unit = &units[count];
            unit->core.position = fixedvec3_from_fvec2((fvec2_t){
                (float)start_x / 24.0f,
                (float)start_y / 24.0f,
            }, 0);
            unit->speed = 4.5f;
            unit->owner = 0;
            unit->selected = true;
            DarkReignVisualSpec visual;
            if (dark_reign_resolve_unit_visual(&defs, associated_type, &visual)) {
                snprintf(unit->core.sprite_name, sizeof(unit->core.sprite_name),
                         "%s", visual.sprite_name);
                snprintf(unit->shadow_name, sizeof(unit->shadow_name), "%s", visual.shadow_name);
                count++;
            }
        }
    }
    free(text); W_FreeFile(&blob); dark_reign_free_definitions(&defs);
    return count;
}
