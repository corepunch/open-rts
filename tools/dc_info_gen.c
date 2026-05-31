#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char path[256];
    char symbol[128];
    int frames;
} SpriteEntry;

typedef struct {
    char name[17];
    int start;
    int end;
} FinLabel;

typedef struct {
    char sprite[9];
    int frame;
    int x;
    int y;
    int layer;
} FinCommand;

typedef struct {
    char stem[9];
    char stem_lower[9];
    FinLabel *labels;
    int label_count;
    FinCommand *commands;
    int command_count;
} FinAnim;

static uint16_t read_u16_le(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int16_t read_i16_le(const unsigned char *p) {
    return (int16_t)read_u16_le(p);
}

static void die(const char *msg, const char *path) {
    if (path) fprintf(stderr, "dc_info_gen: %s: %s\n", path, msg);
    else fprintf(stderr, "dc_info_gen: %s\n", msg);
    exit(1);
}

static unsigned char *read_file(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) die(strerror(errno), path);
    if (fseek(f, 0, SEEK_END) != 0) die("seek failed", path);
    long size = ftell(f);
    if (size < 0) die("ftell failed", path);
    if (fseek(f, 0, SEEK_SET) != 0) die("rewind failed", path);
    unsigned char *data = malloc((size_t)size);
    if (!data) die("out of memory", NULL);
    if (size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size) die("read failed", path);
    fclose(f);
    *size_out = (size_t)size;
    return data;
}

static bool ends_with_ci(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    if (n < m) return false;
    for (size_t i = 0; i < m; ++i) {
        if (tolower((unsigned char)s[n - m + i]) != tolower((unsigned char)suffix[i]))
            return false;
    }
    return true;
}

static void sprite_symbol(const char *path, char *out, size_t out_size) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t len = 0;
    snprintf(out, out_size, "SPR_DC_");
    len = strlen(out);
    for (const char *p = base; *p && *p != '.' && len + 1 < out_size; ++p) {
        unsigned char ch = (unsigned char)*p;
        out[len++] = (char)(isalnum(ch) ? toupper(ch) : '_');
    }
    out[len] = '\0';
}

static int compare_sprite_entry(const void *a, const void *b) {
    const SpriteEntry *sa = (const SpriteEntry *)a;
    const SpriteEntry *sb = (const SpriteEntry *)b;
    return strcmp(sa->path, sb->path);
}

static int spr_frame_count(const char *path) {
    size_t size = 0;
    unsigned char *data = read_file(path, &size);
    if (size < 8 + 256 * 3) die("SPR too small", path);
    int count = read_u16_le(data + 2);
    free(data);
    return count;
}

static void spr_frame_size(const char *path, int frame, int *w_out, int *h_out) {
    size_t size = 0;
    unsigned char *data = read_file(path, &size);
    if (size < 8 + 256 * 3) die("SPR too small", path);
    int count = read_u16_le(data + 2);
    if (frame < 0 || frame >= count) die("SPR frame out of range", path);
    size_t desc = 8 + 256 * 3 + (size_t)frame * 8;
    if (desc + 8 > size) die("SPR descriptor table truncated", path);
    if (w_out) *w_out = read_u16_le(data + desc);
    if (h_out) *h_out = read_u16_le(data + desc + 2);
    free(data);
}

static int find_sprite(const SpriteEntry *sprites, int count, const char *path) {
    for (int i = 0; i < count; ++i) {
        if (strcmp(sprites[i].path, path) == 0) return i;
    }
    fprintf(stderr, "dc_info_gen: missing sprite %s\n", path);
    exit(1);
}

static void copy_padded_name(char *out, size_t out_size, const unsigned char *src, size_t src_size) {
    size_t n = 0;
    while (n + 1 < out_size && n < src_size && src[n] != '\0') {
        out[n] = (char)src[n];
        n++;
    }
    out[n] = '\0';
}

static void stem_lower_name(const char *stem, char out[9]) {
    size_t n = 0;
    for (; stem[n] && n < 8; ++n) out[n] = (char)tolower((unsigned char)stem[n]);
    out[n] = '\0';
}

static const FinLabel *fin_find_label(const FinAnim *fin, const char *label) {
    if (!fin || !label) return NULL;
    for (int i = 0; i < fin->label_count; ++i) {
        if (strcmp(fin->labels[i].name, label) == 0) return &fin->labels[i];
    }
    return NULL;
}

static FinAnim fin_load(const char *path, const char *stem) {
    size_t size = 0;
    unsigned char *data = read_file(path, &size);
    if (size < 8) die("FIN too small", path);
    int valid_labels = read_u16_le(data + 4);
    int deps = read_u16_le(data + 6);
    size_t label_off = 8 + (size_t)deps * 8;
    if (label_off + (size_t)valid_labels * 20 > size) die("FIN label table truncated", path);

    FinAnim fin;
    memset(&fin, 0, sizeof(fin));
    snprintf(fin.stem, sizeof(fin.stem), "%s", stem);
    stem_lower_name(stem, fin.stem_lower);
    fin.label_count = valid_labels;
    fin.labels = calloc((size_t)fin.label_count, sizeof(*fin.labels));
    if (!fin.labels) die("out of memory", NULL);
    for (int i = 0; i < fin.label_count; ++i) {
        size_t off = label_off + (size_t)i * 20;
        copy_padded_name(fin.labels[i].name, sizeof(fin.labels[i].name), data + off, 16);
        fin.labels[i].start = read_u16_le(data + off + 16);
        fin.labels[i].end = read_u16_le(data + off + 18);
    }

    unsigned char pattern[8] = {0};
    memcpy(pattern, fin.stem_lower, strlen(fin.stem_lower));
    size_t command_off = 0;
    bool found_commands = false;
    size_t search_off = label_off + (size_t)valid_labels * 20;
    for (size_t off = search_off; off + 22 <= size; ++off) {
        if ((size - off) % 22 != 0) continue;
        if (memcmp(data + off, pattern, sizeof(pattern)) == 0) {
            command_off = off;
            found_commands = true;
            break;
        }
    }
    if (!found_commands) die("FIN command table not found", path);

    fin.command_count = (int)((size - command_off) / 22);
    fin.commands = calloc((size_t)fin.command_count, sizeof(*fin.commands));
    if (!fin.commands) die("out of memory", NULL);
    for (int i = 0; i < fin.command_count; ++i) {
        size_t off = command_off + (size_t)i * 22;
        copy_padded_name(fin.commands[i].sprite, sizeof(fin.commands[i].sprite), data + off, 8);
        fin.commands[i].frame = read_i16_le(data + off + 8);
        fin.commands[i].x = read_i16_le(data + off + 10);
        fin.commands[i].y = read_i16_le(data + off + 12);
        fin.commands[i].layer = read_i16_le(data + off + 18);
    }

    free(data);
    return fin;
}

static void fin_free(FinAnim *fin) {
    if (!fin) return;
    free(fin->labels);
    free(fin->commands);
    memset(fin, 0, sizeof(*fin));
}

static bool fin_label_range(const FinAnim *fin, const char *label, int *start_out, int *end_out) {
    const FinLabel *l = fin_find_label(fin, label);
    if (!l) return false;
    if (start_out) *start_out = l->start;
    if (end_out) *end_out = l->end;
    return true;
}

static int fin_first_body_frame(const FinAnim *fin, const char *label) {
    const FinLabel *l = fin_find_label(fin, label);
    if (!l) {
        fprintf(stderr, "dc_info_gen: missing FIN label %s in %s\n", label, fin ? fin->stem : "(null)");
        exit(1);
    }
    if (l->start < 0 || l->end < l->start || l->end >= fin->command_count) {
        fprintf(stderr, "dc_info_gen: FIN label %s has invalid command range %d..%d\n",
                label, l->start, l->end);
        exit(1);
    }
    for (int i = l->start; i <= l->end; ++i) {
        const FinCommand *cmd = &fin->commands[i];
        if (strcmp(cmd->sprite, fin->stem_lower) == 0 && cmd->layer == 1) return cmd->frame;
    }
    fprintf(stderr, "dc_info_gen: FIN label %s has no body frame for %s\n", label, fin->stem_lower);
    exit(1);
}

static bool fin_label_is_fire(const FinLabel *label, const char *prefix) {
    if (!label || !prefix) return false;
    size_t n = strlen(prefix);
    return strncmp(label->name, prefix, n) == 0 && strncmp(label->name + n, "FIRE", 4) == 0;
}

static void fin_muzzle_for_body_row(const FinAnim *fin, const char *prefix, int body_base,
                                    int flash_w, int flash_h, int *frame_out,
                                    int offset_x[8], int offset_y[8]) {
    int flash_frame = -1;
    for (int dir = 0; dir < 8; ++dir) {
        int body_frame = body_base + 8 + dir;
        const FinCommand *best_flash = NULL;
        int best_score = 1000000;
        for (int l = 0; l < fin->label_count; ++l) {
            const FinLabel *label = &fin->labels[l];
            if (!fin_label_is_fire(label, prefix)) continue;
            if (label->start < 0 || label->end >= fin->command_count) continue;
            for (int body_i = label->start; body_i <= label->end; ++body_i) {
                const FinCommand *body = &fin->commands[body_i];
                if (strcmp(body->sprite, fin->stem_lower) != 0 ||
                    body->layer != 1 || body->frame != body_frame) {
                    continue;
                }
                for (int flash_i = label->start; flash_i <= label->end; ++flash_i) {
                    const FinCommand *flash = &fin->commands[flash_i];
                    if (strcmp(flash->sprite, "blaz") != 0 || flash->layer != 3) continue;
                    int delta = flash_i - body_i;
                    int score = (delta >= 0 ? 0 : 1000) + abs(delta);
                    if (score < best_score) {
                        best_score = score;
                        best_flash = flash;
                    }
                }
            }
        }
        if (!best_flash) {
            fprintf(stderr, "dc_info_gen: no BLAZ muzzle command for %s body frame %d\n",
                    fin->stem, body_frame);
            exit(1);
        }
        if (flash_frame < 0) flash_frame = best_flash->frame;
        offset_x[dir] = best_flash->x + flash_w / 2;
        offset_y[dir] = best_flash->y + flash_h / 2;
    }
    if (frame_out) *frame_out = flash_frame < 0 ? 0 : flash_frame;
}

static void validate_dark_colony_data(const char *root, const SpriteEntry *sprites, int sprite_count) {
    int gray = find_sprite(sprites, sprite_count, "SPRITES/GRAY.SPR");
    int trsc = find_sprite(sprites, sprite_count, "SPRITES/TRSC.SPR");
    if (sprites[gray].frames <= 291) die("GRAY.SPR does not contain expected corpse frames", sprites[gray].path);
    if (sprites[trsc].frames <= 208) die("TRSC.SPR does not contain expected death frames", sprites[trsc].path);

    char fin_path[1024];
    snprintf(fin_path, sizeof(fin_path), "%s/ANIMATE/GRAY.FIN", root);
    FinAnim gray_fin = fin_load(fin_path, "GRAY");
    int start = 0, end = 0;
    if (!fin_label_range(&gray_fin, "GRAYDIEA14", &start, &end) || start != 254 || end != 265)
        die("GRAY.FIN missing expected GRAYDIEA14 range", fin_path);
    if (!fin_label_range(&gray_fin, "GRAYDIE210", &start, &end) || start != 266 || end != 277)
        die("GRAY.FIN missing expected GRAYDIE210 range", fin_path);
    if (fin_first_body_frame(&gray_fin, "GRAYFIREA0") != 80)
        die("GRAY.FIN FIREA0 does not resolve to expected first body frame", fin_path);
    fin_free(&gray_fin);
}

static void write_header(FILE *out, const SpriteEntry *sprites, int sprite_count) {
    fprintf(out, "/* Generated by tools/dc_info_gen.c. Do not edit by hand. */\n");
    fprintf(out, "#ifndef OPEN_RTS_DARK_COLONY_INFO_H\n#define OPEN_RTS_DARK_COLONY_INFO_H\n\n");
    fprintf(out, "#include \"engine.h\"\n\n");
    fprintf(out, "typedef enum {\n");
    for (int i = 0; i < sprite_count; ++i) fprintf(out, "    %s,\n", sprites[i].symbol);
    fprintf(out, "    NUMSPRITES\n} spritenum_t;\n\n");
    fprintf(out, "typedef enum {\n");
    fprintf(out, "    S_NULL,\n");
    fprintf(out, "    S_DC_TRSC_STND, S_DC_TRSC_RUN1, S_DC_TRSC_RUN2, S_DC_TRSC_RUN3, S_DC_TRSC_RUN4, S_DC_TRSC_RUN5, S_DC_TRSC_RUN6, S_DC_TRSC_RUN7, S_DC_TRSC_RUN8,\n");
    fprintf(out, "    S_DC_TRSC_ATK1, S_DC_TRSC_ATK2, S_DC_TRSC_ATK3, S_DC_TRSC_ATK4, S_DC_TRSC_ATK5, S_DC_TRSC_ATK6, S_DC_TRSC_ATK7, S_DC_TRSC_ATK8,\n");
    fprintf(out, "    S_DC_TRSC_DIE1, S_DC_TRSC_DIE2, S_DC_TRSC_DIE3, S_DC_TRSC_DIE4, S_DC_TRSC_DIE5, S_DC_TRSC_DIE6, S_DC_TRSC_DIE7, S_DC_TRSC_DIE8, S_DC_TRSC_DIE9, S_DC_TRSC_DIE10, S_DC_TRSC_CORPSE,\n");
    fprintf(out, "    S_DC_GRAY_STND, S_DC_GRAY_RUN1, S_DC_GRAY_RUN2, S_DC_GRAY_RUN3, S_DC_GRAY_RUN4, S_DC_GRAY_RUN5, S_DC_GRAY_RUN6, S_DC_GRAY_RUN7, S_DC_GRAY_RUN8,\n");
    fprintf(out, "    S_DC_GRAY_ATK1, S_DC_GRAY_ATK2, S_DC_GRAY_ATK3, S_DC_GRAY_ATK4, S_DC_GRAY_ATK5, S_DC_GRAY_ATK6, S_DC_GRAY_ATK7, S_DC_GRAY_ATK8,\n");
    fprintf(out, "    S_DC_GRAY_DIE1, S_DC_GRAY_DIE2, S_DC_GRAY_DIE3, S_DC_GRAY_DIE4, S_DC_GRAY_DIE5, S_DC_GRAY_DIE6, S_DC_GRAY_DIE7, S_DC_GRAY_DIE8, S_DC_GRAY_DIE9, S_DC_GRAY_ROT1, S_DC_GRAY_ROT2, S_DC_GRAY_ROT3, S_DC_GRAY_CORPSE,\n");
    fprintf(out, "    S_DC_EXPL_STND,\n");
    fprintf(out, "    S_DC_TRSC_MUZZLE, S_DC_GRAY_MUZZLE,\n");
    fprintf(out, "    NUMSTATES\n} statenum_t;\n\n");
    fprintf(out, "typedef enum { MT_NULL, MT_DC_TROOPER, MT_DC_GREY, MT_DC_EXPLOITER, NUMMOBJTYPES } mobjtype_t;\n\n");
    fprintf(out, "extern const char *const sprnames[NUMSPRITES];\n");
    fprintf(out, "extern const RtsState states[NUMSTATES];\n");
    fprintf(out, "extern const RtsMobjInfo mobjinfo[NUMMOBJTYPES];\n");
    fprintf(out, "extern const RtsGameInfo dark_colony_game_info;\n\n");
    fprintf(out, "void A_DC_MuzzleFlash(RtsStateContext *ctx, Unit *unit);\n");
    fprintf(out, "void A_DC_Attack(RtsStateContext *ctx, Unit *unit);\n");
    fprintf(out, "void A_DC_Fall(RtsStateContext *ctx, Unit *unit);\n");
    fprintf(out, "void A_DC_Corpse(RtsStateContext *ctx, Unit *unit);\n\n");
    fprintf(out, "#endif\n");
}

static void f8_frame_major_abs(FILE *out, const char *spr, int tics, const char *action,
                               const char *next, int group, int frame_base) {
    static const int dirs[8] = {0,1,2,3,4,5,6,7};
    fprintf(out, "    { %s, %d, %d, %s, %s, 0, %d, 0, 8, {",
            spr, frame_base, tics, action, next, group);
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", dirs[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", frame_base + i);
    fprintf(out, "}, {0}, {0}, {0} },\n");
}

static void f8_frame_major(FILE *out, const char *spr, int tics, const char *action,
                           const char *next, int group, int base, int offset) {
    f8_frame_major_abs(out, spr, tics, action, next, group, base + offset * 8);
}

static void f6(FILE *out, const char *spr, int tics, const char *action, const char *next,
               int group, const int starts[6], int offset) {
    static const int dirs[6] = {2,1,7,5,4,3};
    fprintf(out, "    { %s, %d, %d, %s, %s, 0, %d, 0, 6, {", spr, starts[0] + offset, tics, action, next, group);
    for (int i = 0; i < 6; ++i) fprintf(out, "%s%d", i ? "," : "", dirs[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 6; ++i) fprintf(out, "%s%d", i ? "," : "", starts[i] + offset);
    fprintf(out, "}, {0}, {0}, {0} },\n");
}

static void fabs_state(FILE *out, const char *spr, int frame, int tics, const char *action,
                       const char *next, int group) {
    fprintf(out, "    { %s, %d, %d, %s, %s, 0, %d, 0, 0, {0}, {0}, {0}, {0}, {0} },\n",
            spr, frame, tics, action, next, group);
}

static void gray_die(FILE *out, const char *next, int n, const char *action) {
    int frame_a = n < 9 ? 262 + n : 286 + (n - 9);
    int frame_b = n < 9 ? 271 + n : 289 + (n - 9);
    fprintf(out, "    { SPR_DC_GRAY, %d, 3, %s, %s, 0, 4, 0, 4, {1,3,7,5}, {%d,%d,%d,%d}, {0,0,RTS_FRAME_FLIP_X,RTS_FRAME_FLIP_X}, {0}, {0} },\n",
            frame_a, action, next, frame_a, frame_b, frame_a, frame_b);
}

static void write_muzzle(FILE *out, const char *spr, int frame, const int offsets_x[8],
                         const int offsets_y[8]) {
    static const int dirs[8] = {0,1,2,3,4,5,6,7};
    fprintf(out, "    { %s, %d, 2, A_None, S_NULL, RTS_FRAME_ADDITIVE|RTS_FRAME_TINT_YELLOW, 5, 0, 8, {",
            spr, frame);
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", dirs[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", frame);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i)
        fprintf(out, "%sRTS_FRAME_ADDITIVE|RTS_FRAME_TINT_YELLOW", i ? "," : "");
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", offsets_x[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", offsets_y[i]);
    fprintf(out, "} },\n");
}

static void write_source(FILE *out, const SpriteEntry *sprites, int sprite_count, const char *root) {
    int trsc = find_sprite(sprites, sprite_count, "SPRITES/TRSC.SPR");
    int gray = find_sprite(sprites, sprite_count, "SPRITES/GRAY.SPR");
    int expl = find_sprite(sprites, sprite_count, "SPRITES/EXPL.SPR");
    int blaz = find_sprite(sprites, sprite_count, "SPRITES/BLAZ.SPR");
    char trsc_fin_path[1024];
    char gray_fin_path[1024];
    snprintf(trsc_fin_path, sizeof(trsc_fin_path), "%s/ANIMATE/TRSC.FIN", root);
    snprintf(gray_fin_path, sizeof(gray_fin_path), "%s/ANIMATE/GRAY.FIN", root);
    FinAnim trsc_fin = fin_load(trsc_fin_path, "TRSC");
    FinAnim gray_fin = fin_load(gray_fin_path, "GRAY");
    int trsc_move_base = fin_first_body_frame(&trsc_fin, "TRSCMOVE0");
    int trsc_attack_base = fin_first_body_frame(&trsc_fin, "TRSCFIREA0");
    int gray_move_base = fin_first_body_frame(&gray_fin, "GRAYMOVE0");
    int gray_attack_base = fin_first_body_frame(&gray_fin, "GRAYFIREA0");
    char blaz_path[1024];
    snprintf(blaz_path, sizeof(blaz_path), "%s/SPRITES/BLAZ.SPR", root);
    int blaz_w = 0, blaz_h = 0;
    spr_frame_size(blaz_path, 0, &blaz_w, &blaz_h);
    int trsc_muzzle_frame = 0, gray_muzzle_frame = 0;
    int trsc_muzzle_x[8] = {0}, trsc_muzzle_y[8] = {0};
    int gray_muzzle_x[8] = {0}, gray_muzzle_y[8] = {0};
    fin_muzzle_for_body_row(&trsc_fin, "TRSC", trsc_attack_base, blaz_w, blaz_h,
                            &trsc_muzzle_frame, trsc_muzzle_x, trsc_muzzle_y);
    fin_muzzle_for_body_row(&gray_fin, "GRAY", gray_attack_base, blaz_w, blaz_h,
                            &gray_muzzle_frame, gray_muzzle_x, gray_muzzle_y);
    fprintf(out, "/* Generated by tools/dc_info_gen.c. Do not edit by hand. */\n");
    fprintf(out, "#include \"info.h\"\n\n");
    fprintf(out, "const char *const sprnames[NUMSPRITES] = {\n");
    for (int i = 0; i < sprite_count; ++i) fprintf(out, "    \"%s\",\n", sprites[i].path);
    fprintf(out, "};\n\n");
    fprintf(out, "#define A_None NULL\n\n");
    fprintf(out, "const RtsState states[NUMSTATES] = {\n");
    fprintf(out, "    { 0, 0, -1, A_None, S_NULL, 0, 0, 0, 0, {0}, {0}, {0}, {0}, {0} },\n");

    int trsc_die[6] = {128,138,149,159,179,195};
    f8_frame_major(out, sprites[trsc].symbol, -1, "A_None", "S_DC_TRSC_STND", 1, 0, 0);
    const char *trsc_run_next[8] = {"S_DC_TRSC_RUN2","S_DC_TRSC_RUN3","S_DC_TRSC_RUN4","S_DC_TRSC_RUN5","S_DC_TRSC_RUN6","S_DC_TRSC_RUN7","S_DC_TRSC_RUN8","S_DC_TRSC_RUN1"};
    for (int i = 0; i < 8; ++i) f8_frame_major(out, sprites[trsc].symbol, 3, "A_None", trsc_run_next[i], 2, trsc_move_base, i);
    const int trsc_atk_frames[8] = {
        trsc_attack_base,
        trsc_attack_base + 8,
        trsc_attack_base + 16,
        trsc_attack_base + 24,
        trsc_attack_base + 32,
        trsc_attack_base + 40,
        trsc_attack_base + 40,
        trsc_attack_base + 40
    };
    const char *trsc_atk_next[8] = {"S_DC_TRSC_ATK2","S_DC_TRSC_ATK3","S_DC_TRSC_ATK4","S_DC_TRSC_ATK5","S_DC_TRSC_ATK6","S_DC_TRSC_STND","S_DC_TRSC_STND","S_DC_TRSC_STND"};
    const char *trsc_atk_action[8] = {"A_None","A_DC_MuzzleFlash","A_DC_Attack","A_None","A_None","A_None","A_None","A_None"};
    for (int i = 0; i < 8; ++i) f8_frame_major_abs(out, sprites[trsc].symbol, 2, trsc_atk_action[i], trsc_atk_next[i], 3, trsc_atk_frames[i]);
    const char *trsc_die_next[11] = {"S_DC_TRSC_DIE2","S_DC_TRSC_DIE3","S_DC_TRSC_DIE4","S_DC_TRSC_DIE5","S_DC_TRSC_DIE6","S_DC_TRSC_DIE7","S_DC_TRSC_DIE8","S_DC_TRSC_DIE9","S_DC_TRSC_DIE10","S_DC_TRSC_CORPSE","S_NULL"};
    for (int i = 0; i < 10; ++i) f6(out, sprites[trsc].symbol, 3, i == 0 ? "A_DC_Fall" : "A_None", trsc_die_next[i], 4, trsc_die, i);
    f6(out, sprites[trsc].symbol, 1, "A_DC_Corpse", trsc_die_next[10], 4, trsc_die, 9);

    f8_frame_major(out, sprites[gray].symbol, -1, "A_None", "S_DC_GRAY_STND", 1, 0, 0);
    const char *gray_run_next[8] = {"S_DC_GRAY_RUN2","S_DC_GRAY_RUN3","S_DC_GRAY_RUN4","S_DC_GRAY_RUN5","S_DC_GRAY_RUN6","S_DC_GRAY_RUN7","S_DC_GRAY_RUN8","S_DC_GRAY_RUN1"};
    for (int i = 0; i < 8; ++i) f8_frame_major(out, sprites[gray].symbol, 3, "A_None", gray_run_next[i], 2, gray_move_base, i);
    const int gray_atk_frames[8] = {
        gray_attack_base,
        gray_attack_base + 8,
        gray_attack_base + 16,
        gray_attack_base + 24,
        gray_attack_base + 32,
        gray_attack_base + 40,
        gray_attack_base + 40,
        gray_attack_base + 40
    };
    const char *gray_atk_next[8] = {"S_DC_GRAY_ATK2","S_DC_GRAY_ATK3","S_DC_GRAY_ATK4","S_DC_GRAY_ATK5","S_DC_GRAY_ATK6","S_DC_GRAY_STND","S_DC_GRAY_STND","S_DC_GRAY_STND"};
    const char *gray_atk_action[8] = {"A_None","A_DC_MuzzleFlash","A_DC_Attack","A_None","A_None","A_None","A_None","A_None"};
    for (int i = 0; i < 8; ++i) f8_frame_major_abs(out, sprites[gray].symbol, 2, gray_atk_action[i], gray_atk_next[i], 3, gray_atk_frames[i]);
    const char *gray_die_next[13] = {"S_DC_GRAY_DIE2","S_DC_GRAY_DIE3","S_DC_GRAY_DIE4","S_DC_GRAY_DIE5","S_DC_GRAY_DIE6","S_DC_GRAY_DIE7","S_DC_GRAY_DIE8","S_DC_GRAY_DIE9","S_DC_GRAY_ROT1","S_DC_GRAY_ROT2","S_DC_GRAY_ROT3","S_DC_GRAY_CORPSE","S_NULL"};
    for (int i = 0; i < 12; ++i) gray_die(out, gray_die_next[i], i, i == 0 ? "A_DC_Fall" : "A_None");
    gray_die(out, gray_die_next[12], 11, "A_DC_Corpse");

    fabs_state(out, sprites[expl].symbol, 0, -1, "A_None", "S_DC_EXPL_STND", 1);
    write_muzzle(out, sprites[blaz].symbol, trsc_muzzle_frame, trsc_muzzle_x, trsc_muzzle_y);
    write_muzzle(out, sprites[blaz].symbol, gray_muzzle_frame, gray_muzzle_x, gray_muzzle_y);
    fprintf(out, "};\n\n");

    fprintf(out, "const RtsMobjInfo mobjinfo[NUMMOBJTYPES] = {\n");
    fprintf(out, "    {0},\n");
    fprintf(out, "    { 1, S_DC_TRSC_STND, 800, S_DC_TRSC_RUN1, 0, 0, 0, S_NULL, 0, 0, 0, S_DC_TRSC_ATK1, S_DC_TRSC_DIE1, S_DC_TRSC_DIE1, 0, 5, 16, 32, 100, 100, 0, RTS_TRAIT_SELECTABLE|RTS_TRAIT_MOBILE|RTS_TRAIT_RENDERABLE|RTS_TRAIT_ATTACK, S_NULL, S_DC_TRSC_MUZZLE },\n");
    fprintf(out, "    { 2, S_DC_GRAY_STND, 800, S_DC_GRAY_RUN1, 0, 0, 0, S_NULL, 0, 0, 0, S_DC_GRAY_ATK1, S_DC_GRAY_DIE1, S_DC_GRAY_DIE1, 0, 5, 16, 32, 100, 100, 0, RTS_TRAIT_SELECTABLE|RTS_TRAIT_MOBILE|RTS_TRAIT_RENDERABLE|RTS_TRAIT_ATTACK, S_NULL, S_DC_GRAY_MUZZLE },\n");
    fprintf(out, "    { 3, S_DC_EXPL_STND, 800, S_DC_EXPL_STND, 0, 0, 0, S_NULL, 0, 0, 0, S_NULL, S_DC_EXPL_STND, S_DC_EXPL_STND, 0, 5, 16, 32, 100, 0, 0, RTS_TRAIT_SELECTABLE|RTS_TRAIT_MOBILE|RTS_TRAIT_RENDERABLE, S_NULL, S_NULL },\n");
    fprintf(out, "};\n\n");
    fprintf(out, "const RtsGameInfo dark_colony_game_info = { sprnames, NUMSPRITES, states, NUMSTATES, mobjinfo, NUMMOBJTYPES, S_NULL, RTS_DIRECTION_DARK_COLONY_8 };\n");
    fin_free(&trsc_fin);
    fin_free(&gray_fin);
}

int main(int argc, char **argv) {
    if (argc != 4) die("usage: dc_info_gen DATA/DCOLONY plugins/DarkColony/info.h plugins/DarkColony/info.c", NULL);
    const char *root = argv[1];
    char sprite_dir[1024];
    snprintf(sprite_dir, sizeof(sprite_dir), "%s/SPRITES", root);
    DIR *dir = opendir(sprite_dir);
    if (!dir) die(strerror(errno), sprite_dir);

    SpriteEntry *sprites = NULL;
    int count = 0, cap = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!ends_with_ci(ent->d_name, ".SPR")) continue;
        if (count >= cap) {
            cap = cap ? cap * 2 : 128;
            sprites = realloc(sprites, (size_t)cap * sizeof(*sprites));
            if (!sprites) die("out of memory", NULL);
        }
        snprintf(sprites[count].path, sizeof(sprites[count].path), "SPRITES/%s", ent->d_name);
        for (char *p = sprites[count].path; *p; ++p) *p = (char)toupper((unsigned char)*p);
        sprite_symbol(sprites[count].path, sprites[count].symbol, sizeof(sprites[count].symbol));
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", root, sprites[count].path);
        sprites[count].frames = spr_frame_count(full);
        count++;
    }
    closedir(dir);
    qsort(sprites, (size_t)count, sizeof(*sprites), compare_sprite_entry);
    validate_dark_colony_data(root, sprites, count);

    FILE *h = fopen(argv[2], "w");
    if (!h) die(strerror(errno), argv[2]);
    write_header(h, sprites, count);
    fclose(h);

    FILE *c = fopen(argv[3], "w");
    if (!c) die(strerror(errno), argv[3]);
    write_source(c, sprites, count, root);
    fclose(c);

    free(sprites);
    return 0;
}
