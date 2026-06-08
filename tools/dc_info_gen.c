#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    char path[256];
    char symbol[128];
    int frames;
} SpriteEntry;

typedef struct {
    SpriteEntry *items;
    int count;
    int cap;
} SpriteList;

typedef struct {
    char name[17];
    int start;
    int end;
} DcFinAnimationHeader;

typedef struct {
    char sprite[9];
    int frame;
    int x;
    int y;
    int remap;
    int intensity;
    int layer;
    int flags;
} DcFinDrawPart;

typedef struct {
    int part_count;
    int ticks;
    int draw_part_start;
} DcFinFrame;

typedef struct {
    char stem[9];
    char stem_lower[9];
    DcFinAnimationHeader *animation_headers;
    int animation_header_count;
    DcFinFrame *frames;
    int frame_count;
    DcFinDrawPart *draw_parts;
    int draw_part_count;
} DcFinAnimation;

typedef struct {
    int default_ticks;
    int frame_count;
    int animation_header_count;
    int dependency_count;
    size_t animation_header_offset;
    size_t frame_offset;
    size_t draw_part_offset;
    int draw_part_count;
} DcFinDiskLayout;

typedef struct {
    int trsc_attack_a;
    int trsc_attack_b;
    int reap_run;
    int reap_attack;
    int reap_death;
    int reap_diefx_a14;
    int reap_diefx_a6;
    int barr_run;
    int barr_attack;
    int barr_death;
    int sarg_run;
    int sarg_attack;
    int sarg_death;
    int scgm_run;
    int scgm_death;
    int expl_run;
    int expl_deploy;
    int expl_work;
    int expl_death;
} DcFinStateCounts;

typedef struct {
    int frame;
    int x;
    int y;
    int remap;
    int intensity;
} DcFinPulseFrame;

#define MAX_EXPL_MINING_PULSE_FRAMES 32

enum {
    DC_FIN_HEADER_DISK_SIZE = 8,
    DC_FIN_DEPENDENCY_DISK_SIZE = 8,
    DC_FIN_ANIMATION_HEADER_DISK_SIZE = 20,
    DC_FIN_FRAME_DISK_SIZE = 164,
    DC_FIN_DRAW_PART_DISK_SIZE = 22,
    DC_FIN_ANIMATION_HEADER_NAME_SIZE = 16,
    DC_FIN_DRAW_PART_SPRITE_NAME_SIZE = 8,
    DC_FIN_MAX_DRAW_PARTS_PER_FRAME = 100
};

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
    const char *p = strncmp(path, "SPRITES/", 8) == 0 ? base : path;
    for (; *p && len + 1 < out_size; ++p) {
        if (*p == '.') break;
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

static int compare_int(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

static int spr_frame_count(const char *path) {
    size_t size = 0;
    unsigned char *data = read_file(path, &size);
    if (size < 8 + 256 * 3) die("SPR too small", path);
    int count = read_u16_le(data + 2);
    free(data);
    return count;
}

static void uppercase_path(char *s) {
    for (; *s; ++s) *s = (char)toupper((unsigned char)*s);
}

static void add_sprite(SpriteList *list, const char *root, const char *rel_path) {
    if (list->count >= list->cap) {
        list->cap = list->cap ? list->cap * 2 : 256;
        list->items = realloc(list->items, (size_t)list->cap * sizeof(*list->items));
        if (!list->items) die("out of memory", NULL);
    }
    SpriteEntry *entry = &list->items[list->count];
    snprintf(entry->path, sizeof(entry->path), "%s", rel_path);
    uppercase_path(entry->path);
    sprite_symbol(entry->path, entry->symbol, sizeof(entry->symbol));
    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", root, entry->path);
    entry->frames = spr_frame_count(full);
    list->count++;
}

static void scan_sprites_recursive(SpriteList *list, const char *root, const char *rel_dir) {
    char dir_path[1024];
    if (rel_dir && rel_dir[0]) snprintf(dir_path, sizeof(dir_path), "%s/%s", root, rel_dir);
    else snprintf(dir_path, sizeof(dir_path), "%s", root);
    DIR *dir = opendir(dir_path);
    if (!dir) die(strerror(errno), dir_path);

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char child_rel[512];
        if (rel_dir && rel_dir[0]) snprintf(child_rel, sizeof(child_rel), "%s/%s", rel_dir, ent->d_name);
        else snprintf(child_rel, sizeof(child_rel), "%s", ent->d_name);

        char child_full[1024];
        snprintf(child_full, sizeof(child_full), "%s/%s", root, child_rel);
        struct stat st;
        if (stat(child_full, &st) != 0) die(strerror(errno), child_full);
        if (S_ISDIR(st.st_mode)) {
            scan_sprites_recursive(list, root, child_rel);
        } else if (S_ISREG(st.st_mode) && ends_with_ci(ent->d_name, ".SPR")) {
            add_sprite(list, root, child_rel);
        }
    }
    closedir(dir);
}

static void validate_unique_sprite_symbols(const SpriteEntry *sprites, int sprite_count) {
    for (int i = 0; i < sprite_count; ++i) {
        for (int j = i + 1; j < sprite_count; ++j) {
            if (strcmp(sprites[i].symbol, sprites[j].symbol) == 0) {
                fprintf(stderr, "dc_info_gen: duplicate sprite symbol %s for %s and %s\n",
                        sprites[i].symbol, sprites[i].path, sprites[j].path);
                exit(1);
            }
        }
    }
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

static int find_sprite_for_fin_stem(const SpriteEntry *sprites, int count, const char *stem) {
    char path[64];
    size_t n = 0;
    snprintf(path, sizeof(path), "SPRITES/");
    n = strlen(path);
    for (const char *p = stem; *p && n + 5 < sizeof(path); ++p) {
        path[n++] = (char)toupper((unsigned char)*p);
    }
    snprintf(path + n, sizeof(path) - n, ".SPR");
    return find_sprite(sprites, count, path);
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

static const DcFinAnimationHeader *dc_fin_find_animation_header(const DcFinAnimation *fin, const char *label) {
    if (!fin || !label) return NULL;
    for (int i = 0; i < fin->animation_header_count; ++i) {
        if (strcmp(fin->animation_headers[i].name, label) == 0) return &fin->animation_headers[i];
    }
    return NULL;
}

static DcFinDiskLayout dc_fin_read_disk_layout(const unsigned char *data, size_t size,
                                               const char *path) {
    if (size < DC_FIN_HEADER_DISK_SIZE) die("FIN too small", path);

    DcFinDiskLayout layout;
    memset(&layout, 0, sizeof(layout));
    layout.default_ticks = read_u16_le(data + 0);
    layout.frame_count = read_u16_le(data + 2);
    layout.animation_header_count = read_u16_le(data + 4);
    layout.dependency_count = read_u16_le(data + 6);
    layout.animation_header_offset =
        DC_FIN_HEADER_DISK_SIZE + (size_t)layout.dependency_count * DC_FIN_DEPENDENCY_DISK_SIZE;
    layout.frame_offset = layout.animation_header_offset +
        (size_t)layout.animation_header_count * DC_FIN_ANIMATION_HEADER_DISK_SIZE;
    layout.draw_part_offset =
        layout.frame_offset + (size_t)layout.frame_count * DC_FIN_FRAME_DISK_SIZE;

    if (layout.animation_header_offset > size ||
        layout.frame_offset > size ||
        layout.draw_part_offset > size) {
        die("FIN table offsets exceed file size", path);
    }
    if ((size - layout.draw_part_offset) % DC_FIN_DRAW_PART_DISK_SIZE != 0) {
        die("FIN draw part table has invalid DSPR/XSPR block layout", path);
    }
    layout.draw_part_count =
        (int)((size - layout.draw_part_offset) / DC_FIN_DRAW_PART_DISK_SIZE);
    return layout;
}

static void dc_fin_init_animation(DcFinAnimation *fin, const char *stem,
                                  const DcFinDiskLayout *layout) {
    memset(fin, 0, sizeof(*fin));
    snprintf(fin->stem, sizeof(fin->stem), "%s", stem);
    stem_lower_name(stem, fin->stem_lower);
    fin->animation_header_count = layout->animation_header_count;
    fin->frame_count = layout->frame_count;
    fin->draw_part_count = layout->draw_part_count;
    fin->animation_headers =
        calloc((size_t)fin->animation_header_count, sizeof(*fin->animation_headers));
    fin->frames = calloc((size_t)fin->frame_count, sizeof(*fin->frames));
    fin->draw_parts = calloc((size_t)fin->draw_part_count, sizeof(*fin->draw_parts));
    if (!fin->animation_headers || !fin->frames || !fin->draw_parts) die("out of memory", NULL);
}

static void dc_fin_read_animation_headers(DcFinAnimation *fin, const unsigned char *data,
                                          const DcFinDiskLayout *layout) {
    for (int i = 0; i < fin->animation_header_count; ++i) {
        size_t offset = layout->animation_header_offset +
            (size_t)i * DC_FIN_ANIMATION_HEADER_DISK_SIZE;
        DcFinAnimationHeader *header = &fin->animation_headers[i];
        copy_padded_name(header->name, sizeof(header->name), data + offset,
                         DC_FIN_ANIMATION_HEADER_NAME_SIZE);
        header->start = read_u16_le(data + offset + 16);
        header->end = read_u16_le(data + offset + 18);
    }
}

static void dc_fin_read_frames(DcFinAnimation *fin, const unsigned char *data,
                               const DcFinDiskLayout *layout, const char *path) {
    (void)layout->default_ticks;
    for (int i = 0; i < fin->frame_count; ++i) {
        size_t offset = layout->frame_offset + (size_t)i * DC_FIN_FRAME_DISK_SIZE;
        DcFinFrame *frame = &fin->frames[i];
        frame->part_count = read_u16_le(data + offset);
        if (frame->part_count > DC_FIN_MAX_DRAW_PARTS_PER_FRAME) {
            die("FIN frame has invalid part count", path);
        }
        frame->ticks = read_u16_le(data + offset + 2);
    }
}

static void dc_fin_link_frame_draw_parts(DcFinAnimation *fin, const char *path) {
    int draw_part_start = 0;
    for (int i = 0; i < fin->frame_count; ++i) {
        fin->frames[i].draw_part_start = draw_part_start;
        draw_part_start += fin->frames[i].part_count;
    }
    if (draw_part_start > fin->draw_part_count) {
        die("FIN frame part count exceeds draw part table", path);
    }
}

static void dc_fin_read_draw_parts(DcFinAnimation *fin, const unsigned char *data,
                                   const DcFinDiskLayout *layout) {
    for (int i = 0; i < fin->draw_part_count; ++i) {
        size_t offset = layout->draw_part_offset + (size_t)i * DC_FIN_DRAW_PART_DISK_SIZE;
        DcFinDrawPart *part = &fin->draw_parts[i];
        copy_padded_name(part->sprite, sizeof(part->sprite), data + offset,
                         DC_FIN_DRAW_PART_SPRITE_NAME_SIZE);
        part->frame = read_i16_le(data + offset + 8);
        part->x = read_i16_le(data + offset + 10);
        part->y = read_i16_le(data + offset + 12);
        part->remap = read_i16_le(data + offset + 14);
        part->intensity = read_i16_le(data + offset + 16);
        part->layer = read_i16_le(data + offset + 18);
        part->flags = read_i16_le(data + offset + 20);
    }
}

static DcFinAnimation dc_load_fin_animation(const char *path, const char *stem) {
    size_t size = 0;
    unsigned char *data = read_file(path, &size);
    DcFinDiskLayout layout = dc_fin_read_disk_layout(data, size, path);

    DcFinAnimation fin;
    dc_fin_init_animation(&fin, stem, &layout);
    dc_fin_read_animation_headers(&fin, data, &layout);
    dc_fin_read_frames(&fin, data, &layout, path);
    dc_fin_link_frame_draw_parts(&fin, path);
    dc_fin_read_draw_parts(&fin, data, &layout);

    free(data);
    return fin;
}

static void dc_free_fin_animation(DcFinAnimation *fin) {
    if (!fin) return;
    free(fin->animation_headers);
    free(fin->frames);
    free(fin->draw_parts);
    memset(fin, 0, sizeof(*fin));
}

static bool dc_fin_animation_header_range(const DcFinAnimation *fin, const char *label, int *start_out, int *end_out) {
    const DcFinAnimationHeader *l = dc_fin_find_animation_header(fin, label);
    if (!l) return false;
    if (start_out) *start_out = l->start;
    if (end_out) *end_out = l->end;
    return true;
}

static bool dc_fin_animation_header_has_valid_frames(const DcFinAnimation *fin, const DcFinAnimationHeader *label) {
    return fin && label && label->start >= 0 && label->end >= label->start &&
        label->end < fin->frame_count;
}

static int dc_fin_draw_parts_for_animation_header(const DcFinAnimation *fin, const DcFinAnimationHeader *label,
                                  const DcFinDrawPart **out, int max_out) {
    if (!dc_fin_animation_header_has_valid_frames(fin, label) || !out || max_out <= 0) return 0;
    int count = 0;
    for (int frame = label->start; frame <= label->end; ++frame) {
        const DcFinFrame *fin_frame = &fin->frames[frame];
        for (int part = 0; part < fin_frame->part_count; ++part) {
            int draw_part_index = fin_frame->draw_part_start + part;
            if (draw_part_index < 0 || draw_part_index >= fin->draw_part_count) return count;
            if (count < max_out) out[count++] = &fin->draw_parts[draw_part_index];
        }
    }
    return count;
}

static int dc_fin_draw_parts_for_animation_header_name(const DcFinAnimation *fin, const char *label_name,
                                       const DcFinDrawPart **out, int max_out) {
    return dc_fin_draw_parts_for_animation_header(fin, dc_fin_find_animation_header(fin, label_name), out, max_out);
}

static const DcFinDrawPart *dc_fin_find_draw_part_in_animation_header(const DcFinAnimation *fin, const char *label,
                                                   const char *sprite, int layer,
                                                   int frame) {
    const DcFinDrawPart *draw_parts[512];
    int count = dc_fin_draw_parts_for_animation_header_name(fin, label, draw_parts,
                                            (int)(sizeof(draw_parts) / sizeof(draw_parts[0])));
    for (int i = 0; i < count; ++i) {
        const DcFinDrawPart *cmd = draw_parts[i];
        if (strcmp(cmd->sprite, sprite) == 0 && cmd->layer == layer &&
            cmd->frame == frame) {
            return cmd;
        }
    }
    return NULL;
}

static void fin_append_pulse_frame(DcFinPulseFrame *frames, int *count, int max_count,
                                   const DcFinDrawPart *cmd) {
    if (!frames || !count || !cmd) die("invalid EXPL mining pulse draw part", NULL);
    if (*count > 0 && frames[*count - 1].frame == cmd->frame) return;
    if (*count >= max_count) die("EXPL mining pulse has too many frames", NULL);
    frames[*count].frame = cmd->frame;
    frames[*count].x = cmd->x;
    frames[*count].y = cmd->y;
    frames[*count].remap = cmd->remap;
    frames[*count].intensity = cmd->intensity;
    (*count)++;
}

static void fin_append_expl_top_frames_for_label(const DcFinAnimation *fin,
                                                 const char *label_name,
                                                 const int *wanted_frames,
                                                 int wanted_count,
                                                 DcFinPulseFrame *frames,
                                                 int *count,
                                                 int max_count) {
    const DcFinAnimationHeader *label = dc_fin_find_animation_header(fin, label_name);
    if (!fin || !label || !wanted_frames || wanted_count <= 0) {
        fprintf(stderr, "dc_info_gen: EXPL.FIN missing mining pulse label %s\n", label_name);
        exit(1);
    }
    const DcFinDrawPart *draw_parts[512];
    int draw_part_count = dc_fin_draw_parts_for_animation_header(fin, label, draw_parts,
                                               (int)(sizeof(draw_parts) / sizeof(draw_parts[0])));
    for (int wanted = 0; wanted < wanted_count; ++wanted) {
        const DcFinDrawPart *match = NULL;
        for (int i = 0; i < draw_part_count; ++i) {
            const DcFinDrawPart *cmd = draw_parts[i];
            if (strcmp(cmd->sprite, fin->stem_lower) == 0 && cmd->layer == 0 &&
                cmd->flags == 0 && cmd->frame == wanted_frames[wanted]) {
                match = cmd;
                break;
            }
        }
        if (!match) {
            fprintf(stderr, "dc_info_gen: EXPL.FIN label %s missing top frame %d\n",
                    label_name, wanted_frames[wanted]);
            exit(1);
        }
        fin_append_pulse_frame(frames, count, max_count, match);
    }
}

static int fin_build_expl_mining_pulse(const DcFinAnimation *fin, DcFinPulseFrame *frames,
                                       int max_count) {
    const DcFinDrawPart *body = dc_fin_find_draw_part_in_animation_header(fin, "EDPLYSTAND14",
                                                       fin->stem_lower, 1, 34);
    if (!body) die("EXPL.FIN missing EDPLYSTAND14 deployed body frame", NULL);

    static const int deploy_tail[] = {25, 26, 27, 28, 29, 30, 32, 33};
    static const int retract_tail[] = {32, 30, 28, 27, 26, 25, 24};
    int count = 0;
    fin_append_expl_top_frames_for_label(fin, "EXPLDEPLOY14", deploy_tail,
                                         (int)(sizeof(deploy_tail) / sizeof(deploy_tail[0])),
                                         frames, &count, max_count);
    fin_append_expl_top_frames_for_label(fin, "EXPLRETRACT14", retract_tail,
                                         (int)(sizeof(retract_tail) / sizeof(retract_tail[0])),
                                         frames, &count, max_count);
    return count;
}

static int fin_first_body_frame(const DcFinAnimation *fin, const char *label) {
    const DcFinAnimationHeader *l = dc_fin_find_animation_header(fin, label);
    if (!l) {
        fprintf(stderr, "dc_info_gen: missing FIN label %s in %s\n", label, fin ? fin->stem : "(null)");
        exit(1);
    }
    if (!dc_fin_animation_header_has_valid_frames(fin, l)) {
        fprintf(stderr, "dc_info_gen: FIN label %s has invalid frame range %d..%d\n",
                label, l->start, l->end);
        exit(1);
    }
    const DcFinDrawPart *draw_parts[512];
    int draw_part_count = dc_fin_draw_parts_for_animation_header(fin, l, draw_parts,
                                               (int)(sizeof(draw_parts) / sizeof(draw_parts[0])));
    for (int i = 0; i < draw_part_count; ++i) {
        const DcFinDrawPart *cmd = draw_parts[i];
        if (strcmp(cmd->sprite, fin->stem_lower) == 0 && cmd->layer == 1) return cmd->frame;
    }
    fprintf(stderr, "dc_info_gen: FIN label %s has no body frame for %s\n", label, fin->stem_lower);
    exit(1);
}

static const DcFinDrawPart *dc_fin_required_draw_part_in_animation_header(const DcFinAnimation *fin,
                                                       const char *label,
                                                       const char *sprite,
                                                       int layer,
                                                       int frame) {
    const DcFinDrawPart *cmd = dc_fin_find_draw_part_in_animation_header(fin, label, sprite, layer, frame);
    if (!cmd) {
        fprintf(stderr, "dc_info_gen: missing FIN draw part %s sprite=%s layer=%d frame=%d in %s\n",
                label, sprite, layer, frame, fin ? fin->stem : "(null)");
        exit(1);
    }
    return cmd;
}

static bool fin_label_is_fire(const DcFinAnimationHeader *label, const char *prefix) {
    if (!label || !prefix) return false;
    size_t n = strlen(prefix);
    return strncmp(label->name, prefix, n) == 0 && strncmp(label->name + n, "FIRE", 4) == 0;
}

static int fin_body_frames_for_label_full(const DcFinAnimation *fin, const char *label,
                                          int *frames, int *flags,
                                          int *offset_x, int *offset_y,
                                          int *remap, int *intensity,
                                          int max_frames) {
    const DcFinAnimationHeader *l = dc_fin_find_animation_header(fin, label);
    if (!fin || !l || !frames || max_frames <= 0 ||
        !dc_fin_animation_header_has_valid_frames(fin, l)) {
        return 0;
    }
    int count = 0;
    const DcFinDrawPart *draw_parts[512];
    int draw_part_count = dc_fin_draw_parts_for_animation_header(fin, l, draw_parts,
                                               (int)(sizeof(draw_parts) / sizeof(draw_parts[0])));
    for (int i = 0; i < draw_part_count && count < max_frames; ++i) {
        const DcFinDrawPart *cmd = draw_parts[i];
        if (strcmp(cmd->sprite, fin->stem_lower) == 0 && cmd->layer == 1) {
            frames[count] = cmd->frame;
            if (flags) flags[count] = (cmd->flags & 1) ? 1 : 0;
            if (offset_x) offset_x[count] = cmd->x;
            if (offset_y) offset_y[count] = cmd->y;
            if (remap) remap[count] = cmd->remap;
            if (intensity) intensity[count] = cmd->intensity;
            count++;
        }
    }
    return count;
}

static int fin_body_frames_for_direction_full(const DcFinAnimation *fin, const char *prefix, int dir,
                                              int *frames, int *flags,
                                              int *offset_x, int *offset_y,
                                              int *remap, int *intensity,
                                              int max_frames) {
    static const char *const suffixes[8] = {"0","14","12","10","8","6","4","2"};
    char label[32];
    for (int distance = 0; distance <= 4; ++distance) {
        for (int sign = -1; sign <= 1; sign += 2) {
            if (distance == 0 && sign > 0) continue;
            int candidate = (dir + sign * distance) & 7;
            snprintf(label, sizeof(label), "%s%s", prefix, suffixes[candidate]);
            int count = fin_body_frames_for_label_full(fin, label, frames, flags,
                                                       offset_x, offset_y, remap,
                                                       intensity, max_frames);
            if (count > 0) return count;
        }
    }
    return 0;
}

static int fin_body_frames_for_direction(const DcFinAnimation *fin, const char *prefix, int dir,
                                         int *frames, int max_frames) {
    return fin_body_frames_for_direction_full(fin, prefix, dir, frames, NULL,
                                             NULL, NULL, NULL, NULL, max_frames);
}

static int fin_stem_layer_frames_for_label_full(const DcFinAnimation *fin, const char *label,
                                                int layer, int *frames, int *flags,
                                                int *offset_x, int *offset_y,
                                                int *remap, int *intensity,
                                                int max_frames) {
    const DcFinAnimationHeader *l = dc_fin_find_animation_header(fin, label);
    if (!fin || !l || !frames || max_frames <= 0 ||
        !dc_fin_animation_header_has_valid_frames(fin, l)) {
        return 0;
    }
    int count = 0;
    const DcFinDrawPart *draw_parts[512];
    int draw_part_count = dc_fin_draw_parts_for_animation_header(fin, l, draw_parts,
                                               (int)(sizeof(draw_parts) / sizeof(draw_parts[0])));
    for (int i = 0; i < draw_part_count && count < max_frames; ++i) {
        const DcFinDrawPart *cmd = draw_parts[i];
        if (strcmp(cmd->sprite, fin->stem_lower) == 0 && cmd->layer == layer) {
            frames[count] = cmd->frame;
            if (flags) flags[count] = (cmd->flags & 1) ? 1 : 0;
            if (offset_x) offset_x[count] = cmd->x;
            if (offset_y) offset_y[count] = cmd->y;
            if (remap) remap[count] = cmd->remap;
            if (intensity) intensity[count] = cmd->intensity;
            count++;
        }
    }
    return count;
}

static int fin_stem_layer_frames_for_direction_full(const DcFinAnimation *fin,
                                                    const char *prefix, int dir,
                                                    int layer, int *frames, int *flags,
                                                    int *offset_x, int *offset_y,
                                                    int *remap, int *intensity,
                                                    int max_frames) {
    static const char *const suffixes[8] = {"0","14","12","10","8","6","4","2"};
    char label[32];
    for (int distance = 0; distance <= 4; ++distance) {
        for (int sign = -1; sign <= 1; sign += 2) {
            if (distance == 0 && sign > 0) continue;
            int candidate = (dir + sign * distance) & 7;
            snprintf(label, sizeof(label), "%s%s", prefix, suffixes[candidate]);
            int count = fin_stem_layer_frames_for_label_full(fin, label, layer,
                                                             frames, flags,
                                                             offset_x, offset_y,
                                                             remap, intensity,
                                                             max_frames);
            if (count > 0) return count;
        }
    }
    return 0;
}

static int fin_layer5_frames_for_label_full(const DcFinAnimation *fin, const char *label,
                                            int *body_frames, int *body_flags,
                                            int *body_x, int *body_y,
                                            int *body_remap, int *body_intensity,
                                            int *overlay_frames, int *overlay_flags,
                                            int *overlay_x, int *overlay_y,
                                            int *overlay_remap, int *overlay_intensity,
                                            int max_frames) {
    const DcFinAnimationHeader *l = dc_fin_find_animation_header(fin, label);
    if (!fin || !l || !body_frames || max_frames <= 0 ||
        !dc_fin_animation_header_has_valid_frames(fin, l)) {
        return 0;
    }

    const DcFinDrawPart *bodies[128];
    const DcFinDrawPart *overlays[128];
    int body_count = 0;
    int overlay_count = 0;
    const DcFinDrawPart *draw_parts[512];
    int draw_part_count = dc_fin_draw_parts_for_animation_header(fin, l, draw_parts,
                                               (int)(sizeof(draw_parts) / sizeof(draw_parts[0])));
    for (int i = 0; i < draw_part_count; ++i) {
        const DcFinDrawPart *cmd = draw_parts[i];
        if (strcmp(cmd->sprite, fin->stem_lower) != 0) continue;
        if (cmd->layer == 1 && body_count < (int)(sizeof(bodies) / sizeof(bodies[0]))) {
            bodies[body_count++] = cmd;
        } else if (cmd->layer == 5 &&
                   overlay_count < (int)(sizeof(overlays) / sizeof(overlays[0]))) {
            overlays[overlay_count++] = cmd;
        }
    }
    if (body_count <= 0) return 0;

    int count = body_count;
    if (count > max_frames) count = max_frames;
    for (int i = 0; i < count; ++i) {
        const DcFinDrawPart *body = bodies[i];
        const DcFinDrawPart *overlay = overlay_count > 0 ?
            overlays[i < overlay_count ? i : overlay_count - 1] : NULL;
        body_frames[i] = body->frame;
        if (body_flags) body_flags[i] = (body->flags & 1) ? 1 : 0;
        if (body_x) body_x[i] = body->x;
        if (body_y) body_y[i] = body->y;
        if (body_remap) body_remap[i] = body->remap;
        if (body_intensity) body_intensity[i] = body->intensity;
        if (overlay) {
            if (overlay_frames) overlay_frames[i] = overlay->frame;
            if (overlay_flags) overlay_flags[i] = (overlay->flags & 1) ? 1 : 0;
            if (overlay_x) overlay_x[i] = overlay->x;
            if (overlay_y) overlay_y[i] = overlay->y;
            if (overlay_remap) overlay_remap[i] = overlay->remap;
            if (overlay_intensity) overlay_intensity[i] = overlay->intensity;
        } else {
            if (overlay_frames) overlay_frames[i] = -1;
            if (overlay_flags) overlay_flags[i] = 0;
            if (overlay_x) overlay_x[i] = 0;
            if (overlay_y) overlay_y[i] = 0;
            if (overlay_remap) overlay_remap[i] = 0;
            if (overlay_intensity) overlay_intensity[i] = 16;
        }
    }
    return count;
}

static int fin_layer5_frames_for_direction16_full(const DcFinAnimation *fin, const char *prefix,
                                                  int code, int *body_frames,
                                                  int *body_flags, int *body_x,
                                                  int *body_y, int *body_remap,
                                                  int *body_intensity,
                                                  int *overlay_frames,
                                                  int *overlay_flags, int *overlay_x,
                                                  int *overlay_y, int *overlay_remap,
                                                  int *overlay_intensity,
                                                  int max_frames) {
    char label[32];
    for (int distance = 0; distance <= 8; ++distance) {
        for (int sign = -1; sign <= 1; sign += 2) {
            if (distance == 0 && sign > 0) continue;
            int candidate = (code + sign * distance) & 15;
            int suffix = (16 - candidate) & 15;
            snprintf(label, sizeof(label), "%s%d", prefix, suffix);
            int count = fin_layer5_frames_for_label_full(fin, label,
                                                         body_frames, body_flags,
                                                         body_x, body_y,
                                                         body_remap, body_intensity,
                                                         overlay_frames, overlay_flags,
                                                         overlay_x, overlay_y,
                                                         overlay_remap, overlay_intensity,
                                                         max_frames);
            if (count > 0) return count;
        }
    }
    return 0;
}

static int fin_body_frames_for_direction16(const DcFinAnimation *fin, const char *prefix, int code,
                                           int *frames, int *flags,
                                           int *offset_x, int *offset_y,
                                           int *remap, int *intensity,
                                           int max_frames) {
    char label[32];
    for (int distance = 0; distance <= 8; ++distance) {
        for (int sign = -1; sign <= 1; sign += 2) {
            if (distance == 0 && sign > 0) continue;
            int candidate = (code + sign * distance) & 15;
            int suffix = (16 - candidate) & 15;
            snprintf(label, sizeof(label), "%s%d", prefix, suffix);
            int count = fin_body_frames_for_label_full(fin, label, frames, flags,
                                                       offset_x, offset_y, remap,
                                                       intensity, max_frames);
            if (count > 0) return count;
        }
    }
    return 0;
}

static int fin_state_count_for_sequence(const DcFinAnimation *fin, const char *prefix) {
    bool seen[8] = {false};
    int frames[128];
    int all_frames[1024];
    int all_count = 0;
    int count = 0;
    for (int dir = 0; dir < 8; ++dir) {
        int source_dir = dir;
        if (seen[source_dir]) continue;
        seen[source_dir] = true;
        int dir_count = fin_body_frames_for_direction(fin, prefix, source_dir,
                                                      frames, (int)(sizeof(frames) / sizeof(frames[0])));
        if (dir_count <= 0) {
            fprintf(stderr, "dc_info_gen: no body frames for %s direction %d in %s\n",
                    prefix, source_dir, fin ? fin->stem : "(null)");
            exit(1);
        }
        for (int i = 0; i < dir_count && all_count < (int)(sizeof(all_frames) / sizeof(all_frames[0])); ++i) {
            all_frames[all_count++] = frames[i];
        }
        if (dir_count > count) count = dir_count;
    }
    qsort(all_frames, (size_t)all_count, sizeof(all_frames[0]), compare_int);
    int unique_count = 0;
    for (int i = 0; i < all_count; ++i) {
        if (i == 0 || all_frames[i] != all_frames[i - 1]) all_frames[unique_count++] = all_frames[i];
    }
    if (unique_count >= 8 && unique_count % 8 == 0 &&
        all_frames[unique_count - 1] - all_frames[0] + 1 == unique_count) {
        int row_count = unique_count / 8;
        if (row_count <= count) return row_count;
    }
    return count;
}

static int fin_state_count_for_sequence16(const DcFinAnimation *fin, const char *prefix) {
    int frames[128];
    int count = 0;
    for (int code = 0; code < 16; ++code) {
        int dir_count = fin_body_frames_for_direction16(fin, prefix, code,
                                                        frames, NULL, NULL, NULL,
                                                        NULL, NULL,
                                                        (int)(sizeof(frames) / sizeof(frames[0])));
        if (dir_count <= 0) {
            fprintf(stderr, "dc_info_gen: no body frames for %s direction %d in %s\n",
                    prefix, code, fin ? fin->stem : "(null)");
            exit(1);
        }
        if (dir_count > count) count = dir_count;
    }
    return count;
}

static int fin_effect_draw_part_count_for_label(const DcFinAnimation *fin, const char *label_name) {
    const DcFinAnimationHeader *label = dc_fin_find_animation_header(fin, label_name);
    if (!dc_fin_animation_header_has_valid_frames(fin, label)) {
        return 0;
    }
    int count = 0;
    const DcFinDrawPart *draw_parts[512];
    int draw_part_count = dc_fin_draw_parts_for_animation_header(fin, label, draw_parts,
                                               (int)(sizeof(draw_parts) / sizeof(draw_parts[0])));
    for (int i = 0; i < draw_part_count; ++i) {
        const DcFinDrawPart *cmd = draw_parts[i];
        if (strcmp(cmd->sprite, fin->stem_lower) == 0 && cmd->layer == 1) continue;
        count++;
    }
    return count;
}

static int fin_state_count_for_expl_mining_pulse(const DcFinAnimation *fin) {
    DcFinPulseFrame frames[MAX_EXPL_MINING_PULSE_FRAMES];
    return fin_build_expl_mining_pulse(fin, frames, MAX_EXPL_MINING_PULSE_FRAMES);
}

static void fin_muzzle_for_body_row(const DcFinAnimation *fin, const char *prefix, int body_base,
                                    int flash_w, int flash_h, int *frame_out,
                                    int offset_x[8], int offset_y[8]) {
    (void)flash_w;
    (void)flash_h;
    int flash_frame = -1;
    for (int dir = 0; dir < 8; ++dir) {
        int body_frame = body_base + 8 + dir;
        const DcFinDrawPart *best_flash = NULL;
        int best_score = 1000000;
        for (int l = 0; l < fin->animation_header_count; ++l) {
            const DcFinAnimationHeader *label = &fin->animation_headers[l];
            if (!fin_label_is_fire(label, prefix)) continue;
            const DcFinDrawPart *draw_parts[512];
            int draw_part_count = dc_fin_draw_parts_for_animation_header(fin, label, draw_parts,
                                                       (int)(sizeof(draw_parts) / sizeof(draw_parts[0])));
            if (draw_part_count <= 0) continue;
            for (int body_i = 0; body_i < draw_part_count; ++body_i) {
                const DcFinDrawPart *body = draw_parts[body_i];
                if (strcmp(body->sprite, fin->stem_lower) != 0 ||
                    body->layer != 1 || body->frame != body_frame) {
                    continue;
                }
                for (int flash_i = 0; flash_i < draw_part_count; ++flash_i) {
                    const DcFinDrawPart *flash = draw_parts[flash_i];
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
            fprintf(stderr, "dc_info_gen: no BLAZ muzzle draw part for %s body frame %d\n",
                    fin->stem, body_frame);
            exit(1);
        }
        if (flash_frame < 0) flash_frame = best_flash->frame;
        offset_x[dir] = best_flash->x;
        offset_y[dir] = best_flash->y;
    }
    if (frame_out) *frame_out = flash_frame < 0 ? 0 : flash_frame;
}

static const DcFinAnimationHeader *fin_label_for_direction16(const DcFinAnimation *fin, const char *prefix, int code) {
    static char label_name[32];
    for (int distance = 0; distance <= 8; ++distance) {
        for (int sign = -1; sign <= 1; sign += 2) {
            if (distance == 0 && sign > 0) continue;
            int candidate = (code + sign * distance) & 15;
            int suffix = (16 - candidate) & 15;
            snprintf(label_name, sizeof(label_name), "%s%d", prefix, suffix);
            const DcFinAnimationHeader *label = dc_fin_find_animation_header(fin, label_name);
            if (dc_fin_animation_header_has_valid_frames(fin, label)) {
                return label;
            }
        }
    }
    return NULL;
}

static void fin_muzzle_for_sequence16_step(const DcFinAnimation *fin, const char *prefix, int step,
                                           int flash_w, int flash_h, int *frame_out,
                                           int offset_x[16], int offset_y[16]) {
    (void)flash_w;
    (void)flash_h;
    int flash_frame = -1;
    for (int code = 0; code < 16; ++code) {
        const DcFinAnimationHeader *label = fin_label_for_direction16(fin, prefix, code);
        if (!label) {
            fprintf(stderr, "dc_info_gen: no FIN label for %s direction %d in %s\n",
                    prefix, code, fin ? fin->stem : "(null)");
            exit(1);
        }
        const DcFinDrawPart *bodies[128];
        int body_count = 0;
        const DcFinDrawPart *draw_parts[512];
        int draw_part_count = dc_fin_draw_parts_for_animation_header(fin, label, draw_parts,
                                                   (int)(sizeof(draw_parts) / sizeof(draw_parts[0])));
        for (int i = 0; i < draw_part_count; ++i) {
            const DcFinDrawPart *cmd = draw_parts[i];
            if (strcmp(cmd->sprite, fin->stem_lower) == 0 && cmd->layer == 1 &&
                body_count < (int)(sizeof(bodies) / sizeof(bodies[0]))) {
                bodies[body_count++] = cmd;
            }
        }
        if (body_count <= 0) {
            fprintf(stderr, "dc_info_gen: no body draw part for %s direction %d\n", prefix, code);
            exit(1);
        }
        const DcFinDrawPart *body = bodies[step < body_count ? step : body_count - 1];
        const DcFinDrawPart *best_flash = NULL;
        int best_score = 1000000;
        int body_index = -1;
        for (int i = 0; i < draw_part_count; ++i) {
            if (draw_parts[i] == body) {
                body_index = i;
                break;
            }
        }
        for (int i = 0; i < draw_part_count; ++i) {
            const DcFinDrawPart *flash = draw_parts[i];
            if (strcmp(flash->sprite, "blaz") != 0 || flash->layer != 3) continue;
            int delta = body_index >= 0 ? i - body_index : 0;
            int score = (delta >= 0 ? 0 : 1000) + abs(delta);
            if (score < best_score) {
                best_score = score;
                best_flash = flash;
            }
        }
        if (!best_flash) {
            fprintf(stderr, "dc_info_gen: no BLAZ muzzle draw part for %s direction %d\n",
                    prefix, code);
            exit(1);
        }
        if (flash_frame < 0) flash_frame = best_flash->frame;
        offset_x[code] = best_flash->x;
        offset_y[code] = best_flash->y;
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
    DcFinAnimation gray_fin = dc_load_fin_animation(fin_path, "GRAY");
    int start = 0, end = 0;
    if (!dc_fin_animation_header_range(&gray_fin, "GRAYDIEA14", &start, &end) || start != 254 || end != 265)
        die("GRAY.FIN missing expected GRAYDIEA14 range", fin_path);
    if (!dc_fin_animation_header_range(&gray_fin, "GRAYDIE210", &start, &end) || start != 266 || end != 277)
        die("GRAY.FIN missing expected GRAYDIE210 range", fin_path);
    if (fin_first_body_frame(&gray_fin, "GRAYFIREA0") != 80)
        die("GRAY.FIN FIREA0 does not resolve to expected first body frame", fin_path);
    dc_free_fin_animation(&gray_fin);
}

static DcFinStateCounts load_fin_state_counts(const char *root) {
    char trsc_fin_path[1024];
    char reap_fin_path[1024];
    char barr_fin_path[1024];
    char sarg_fin_path[1024];
    char scgm_fin_path[1024];
    char expl_fin_path[1024];
    snprintf(trsc_fin_path, sizeof(trsc_fin_path), "%s/ANIMATE/TRSC.FIN", root);
    snprintf(reap_fin_path, sizeof(reap_fin_path), "%s/ANIMATE/REAP.FIN", root);
    snprintf(barr_fin_path, sizeof(barr_fin_path), "%s/ANIMATE/BARR.FIN", root);
    snprintf(sarg_fin_path, sizeof(sarg_fin_path), "%s/ANIMATE/SARG.FIN", root);
    snprintf(scgm_fin_path, sizeof(scgm_fin_path), "%s/ANIMATE/SCGM.FIN", root);
    snprintf(expl_fin_path, sizeof(expl_fin_path), "%s/ANIMATE/EXPL.FIN", root);

    DcFinAnimation trsc_fin = dc_load_fin_animation(trsc_fin_path, "TRSC");
    DcFinAnimation reap_fin = dc_load_fin_animation(reap_fin_path, "REAP");
    DcFinAnimation barr_fin = dc_load_fin_animation(barr_fin_path, "BARR");
    DcFinAnimation sarg_fin = dc_load_fin_animation(sarg_fin_path, "SARG");
    DcFinAnimation scgm_fin = dc_load_fin_animation(scgm_fin_path, "SCGM");
    DcFinAnimation expl_fin = dc_load_fin_animation(expl_fin_path, "EXPL");

    DcFinStateCounts counts;
    counts.trsc_attack_a = 6;
    counts.trsc_attack_b = 4;
    counts.reap_run = fin_state_count_for_sequence16(&reap_fin, "REAPMOVE");
    counts.reap_attack = fin_state_count_for_sequence16(&reap_fin, "REAPFIRE");
    counts.reap_death = fin_state_count_for_sequence16(&reap_fin, "REAPDIEA");
    counts.reap_diefx_a14 = fin_effect_draw_part_count_for_label(&reap_fin, "REAPDIEA14");
    counts.reap_diefx_a6 = fin_effect_draw_part_count_for_label(&reap_fin, "REAPDIEA6");
    counts.barr_run = fin_state_count_for_sequence16(&barr_fin, "BARRMOVE");
    counts.barr_attack = fin_state_count_for_sequence16(&barr_fin, "BARRFIREA");
    counts.barr_death = fin_state_count_for_sequence16(&barr_fin, "BARRDIE");
    counts.sarg_run = fin_state_count_for_sequence(&sarg_fin, "SARGMOVE");
    counts.sarg_attack = fin_state_count_for_sequence(&sarg_fin, "SARGFIREA");
    counts.sarg_death = fin_state_count_for_sequence(&sarg_fin, "SARGDIE");
    counts.scgm_run = fin_state_count_for_sequence(&scgm_fin, "SCGMMOVE");
    counts.scgm_death = fin_state_count_for_sequence(&scgm_fin, "SCGMDIE");
    counts.expl_run = fin_state_count_for_sequence(&expl_fin, "EXPLMOVE");
    counts.expl_deploy = fin_state_count_for_sequence(&expl_fin, "EXPLDEPLOY");
    counts.expl_work = fin_state_count_for_expl_mining_pulse(&expl_fin);
    counts.expl_death = fin_state_count_for_sequence(&expl_fin, "EXPLDIE");

    if (fin_first_body_frame(&trsc_fin, "TRSCFIREA0") != 80)
        die("TRSC.FIN FIREA0 does not resolve to expected first body frame", trsc_fin_path);
    if (fin_first_body_frame(&trsc_fin, "TRSCFIREB0") != 104)
        die("TRSC.FIN FIREB0 does not resolve to expected first body frame", trsc_fin_path);

    dc_free_fin_animation(&trsc_fin);
    dc_free_fin_animation(&reap_fin);
    dc_free_fin_animation(&barr_fin);
    dc_free_fin_animation(&sarg_fin);
    dc_free_fin_animation(&scgm_fin);
    dc_free_fin_animation(&expl_fin);
    return counts;
}

static void write_header(FILE *out, const SpriteEntry *sprites, int sprite_count,
                         const DcFinStateCounts *counts) {
    fprintf(out, "/* Generated by tools/dc_info_gen.c. Do not edit by hand. */\n");
    fprintf(out, "#ifndef OPEN_RTS_DARK_COLONY_INFO_H\n#define OPEN_RTS_DARK_COLONY_INFO_H\n\n");
    fprintf(out, "#include \"engine.h\"\n\n");
    fprintf(out, "typedef enum {\n");
    for (int i = 0; i < sprite_count; ++i) fprintf(out, "    %s,\n", sprites[i].symbol);
    fprintf(out, "    NUMSPRITES\n} spritenum_t;\n\n");
    fprintf(out, "typedef enum {\n");
    fprintf(out, "    S_NULL,\n");
    fprintf(out, "    S_DC_EXCOPOD_STND, S_DC_BRRKPOD_STND, S_DC_TOWR_STND,\n");
    fprintf(out, "    S_DC_TRSC_STND, S_DC_TRSC_RUN1, S_DC_TRSC_RUN2, S_DC_TRSC_RUN3, S_DC_TRSC_RUN4, S_DC_TRSC_RUN5, S_DC_TRSC_RUN6, S_DC_TRSC_RUN7, S_DC_TRSC_RUN8,\n");
    fprintf(out, "    S_DC_TRSC_ATK_SELECT,\n");
    for (int i = 1; i <= counts->trsc_attack_a; ++i) fprintf(out, "    S_DC_TRSC_ATK%d,\n", i);
    for (int i = 1; i <= counts->trsc_attack_b; ++i) fprintf(out, "    S_DC_TRSC_ATKB%d,\n", i);
    fprintf(out, "    S_DC_TRSC_DIE1, S_DC_TRSC_DIE2, S_DC_TRSC_DIE3, S_DC_TRSC_DIE4, S_DC_TRSC_DIE5, S_DC_TRSC_DIE6, S_DC_TRSC_DIE7, S_DC_TRSC_DIE8, S_DC_TRSC_DIE9, S_DC_TRSC_DIE10, S_DC_TRSC_CORPSE,\n");
    fprintf(out, "    S_DC_GRAY_STND, S_DC_GRAY_RUN1, S_DC_GRAY_RUN2, S_DC_GRAY_RUN3, S_DC_GRAY_RUN4, S_DC_GRAY_RUN5, S_DC_GRAY_RUN6, S_DC_GRAY_RUN7, S_DC_GRAY_RUN8,\n");
    fprintf(out, "    S_DC_GRAY_ATK1, S_DC_GRAY_ATK2, S_DC_GRAY_ATK3, S_DC_GRAY_ATK4, S_DC_GRAY_ATK5, S_DC_GRAY_ATK6, S_DC_GRAY_ATK7, S_DC_GRAY_ATK8,\n");
    fprintf(out, "    S_DC_GRAY_DIE1, S_DC_GRAY_DIE2, S_DC_GRAY_DIE3, S_DC_GRAY_DIE4, S_DC_GRAY_DIE5, S_DC_GRAY_DIE6, S_DC_GRAY_DIE7, S_DC_GRAY_DIE8, S_DC_GRAY_DIE9, S_DC_GRAY_ROT1, S_DC_GRAY_ROT2, S_DC_GRAY_ROT3, S_DC_GRAY_CORPSE,\n");
    fprintf(out, "    S_DC_REAP_STND,\n");
    for (int i = 1; i <= counts->reap_run; ++i) fprintf(out, "    S_DC_REAP_RUN%d,\n", i);
    for (int i = 1; i <= counts->reap_attack; ++i) fprintf(out, "    S_DC_REAP_ATK%d,\n", i);
    for (int i = 1; i <= counts->reap_death; ++i) fprintf(out, "    S_DC_REAP_DIE%d,\n", i);
    fprintf(out, "    S_DC_REAP_CORPSE,\n");
    for (int i = 1; i <= counts->reap_diefx_a14; ++i) fprintf(out, "    S_DC_REAP_DIEA14_FX%d,\n", i);
    for (int i = 1; i <= counts->reap_diefx_a6; ++i) fprintf(out, "    S_DC_REAP_DIEA6_FX%d,\n", i);
    fprintf(out, "    S_DC_BARR_STND,\n");
    for (int i = 1; i <= counts->barr_run; ++i) fprintf(out, "    S_DC_BARR_RUN%d,\n", i);
    for (int i = 1; i <= counts->barr_attack; ++i) fprintf(out, "    S_DC_BARR_ATK%d,\n", i);
    for (int i = 1; i <= counts->barr_death; ++i) fprintf(out, "    S_DC_BARR_DIE%d,\n", i);
    fprintf(out, "    S_DC_BARR_CORPSE,\n");
    fprintf(out, "    S_DC_SARG_STND,\n");
    for (int i = 1; i <= counts->sarg_run; ++i) fprintf(out, "    S_DC_SARG_RUN%d,\n", i);
    for (int i = 1; i <= counts->sarg_attack; ++i) fprintf(out, "    S_DC_SARG_ATK%d,\n", i);
    for (int i = 1; i <= counts->sarg_death; ++i) fprintf(out, "    S_DC_SARG_DIE%d,\n", i);
    fprintf(out, "    S_DC_SARG_CORPSE,\n");
    fprintf(out, "    S_DC_SCGM_STND,\n");
    for (int i = 1; i <= counts->scgm_run; ++i) fprintf(out, "    S_DC_SCGM_RUN%d,\n", i);
    for (int i = 1; i <= counts->scgm_death; ++i) fprintf(out, "    S_DC_SCGM_DIE%d,\n", i);
    fprintf(out, "    S_DC_SCGM_CORPSE,\n");
    fprintf(out, "    S_DC_EXPL_STND,\n");
    for (int i = 1; i <= counts->expl_run; ++i) fprintf(out, "    S_DC_EXPL_RUN%d,\n", i);
    for (int i = 1; i <= counts->expl_deploy; ++i) fprintf(out, "    S_DC_EXPL_DEPLOY%d,\n", i);
    for (int i = 1; i <= counts->expl_work; ++i) fprintf(out, "    S_DC_EXPL_WORK%d,\n", i);
    for (int i = 1; i <= counts->expl_death; ++i) fprintf(out, "    S_DC_EXPL_DIE%d,\n", i);
    fprintf(out, "    S_DC_EXPL_CORPSE,\n");
    fprintf(out, "    S_DC_TRSC_MUZZLE, S_DC_GRAY_MUZZLE, S_DC_REAP_MUZZLE,\n");
    fprintf(out, "    NUMSTATES\n} statenum_t;\n\n");
    fprintf(out, "typedef enum { MT_NULL, MT_DC_TROOPER, MT_DC_GREY, MT_DC_EXPLOITER, MT_DC_REAPER, MT_DC_THUNDERBOLT, MT_DC_CYBORG, MT_DC_SCOUT, NUMMOBJTYPES } mobjtype_t;\n\n");
    fprintf(out, "extern const char *const sprnames[NUMSPRITES];\n");
    fprintf(out, "extern const State states[NUMSTATES];\n");
    fprintf(out, "extern const MobjInfo mobjinfo[NUMMOBJTYPES];\n");
    fprintf(out, "extern const GameInfo dark_colony_game_info;\n\n");
    fprintf(out, "void A_DC_TrooperAttackStart(StateContext *ctx, Unit *unit);\n");
    fprintf(out, "void A_DC_MuzzleFlash(StateContext *ctx, Unit *unit);\n");
    fprintf(out, "void A_DC_Attack(StateContext *ctx, Unit *unit);\n");
    fprintf(out, "void A_DC_Fall(StateContext *ctx, Unit *unit);\n");
    fprintf(out, "void A_DC_ReaperDeath(StateContext *ctx, Unit *unit);\n");
    fprintf(out, "void A_DC_Corpse(StateContext *ctx, Unit *unit);\n\n");
    fprintf(out, "#endif\n");
}

static void f6(FILE *out, const char *spr, int tics, const char *action, const char *next,
               int group, const int starts[6], int offset) {
    static const int dirs[6] = {4,2,14,10,8,6};
    fprintf(out, "    { %s, %d, %d, %s, %s, 0, %d, 0, 6, {", spr, starts[0] + offset, tics, action, next, group);
    for (int i = 0; i < 6; ++i) fprintf(out, "%s%d", i ? "," : "", dirs[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 6; ++i) fprintf(out, "%s%d", i ? "," : "", starts[i] + offset);
    fprintf(out, "}, {0}, {0}, {0}, DC_DEFAULT_REMAP, DC_DEFAULT_INTENSITY, DC_NO_OVERLAY },\n");
}

static void f1_fin_raw_state(FILE *out, const char *spr,
                             const DcFinDrawPart *cmd,
                             const char *next) {
    fprintf(out, "    { %s, %d, -1, A_None, %s, 0, 1, 0, 1, {0}, {%d}, {%s}, {%d}, {%d}, {%d}, {%d}, DC_NO_OVERLAY },\n",
            spr, cmd->frame, next, cmd->frame,
            (cmd->flags & 1) ? "FLIPPED" : "0",
            cmd->x,
            cmd->y,
            cmd->remap, cmd->intensity);
}

static void gray_die(FILE *out, const char *next, int n, const char *action) {
    int frame_a = n < 9 ? 262 + n : 286 + (n - 9);
    int frame_b = n < 9 ? 271 + n : 289 + (n - 9);
    fprintf(out, "    { SPR_DC_GRAY, %d, 3, %s, %s, 0, 4, 0, 4, {2,6,14,10}, {%d,%d,%d,%d}, {0,0,FLIPPED,FLIPPED}, {0}, {0}, DC_DEFAULT_REMAP, DC_DEFAULT_INTENSITY, DC_NO_OVERLAY },\n",
            frame_a, action, next, frame_a, frame_b, frame_a, frame_b);
}

static void state_name(char *dst, size_t dst_size, const char *prefix,
                       const char *kind, int index) {
    if (index > 0) snprintf(dst, dst_size, "S_DC_%s_%s%d", prefix, kind, index);
    else snprintf(dst, dst_size, "S_DC_%s_%s", prefix, kind);
}

static void f8_fin_state(FILE *out, const char *spr, const DcFinAnimation *fin,
                         const char *label_prefix, int step, int fallback_frame,
                         int tics, const char *action, const char *next, int group,
                         int sequence_count, bool mirror_left) {
    (void)mirror_left;
    static const int dirs[8] = {0,2,4,6,8,10,12,14};
    int frames[8];
    int flags[8] = {0};
    int offset_x[8] = {0};
    int offset_y[8] = {0};
    int remap[8] = {0};
    int intensity[8] = {0};
    for (int dir = 0; dir < 8; ++dir) {
        int candidates[64];
        int candidate_flags[64] = {0};
        int candidate_x[64] = {0};
        int candidate_y[64] = {0};
        int candidate_remap[64] = {0};
        int candidate_intensity[64] = {0};
        int count = fin_body_frames_for_direction_full(fin, label_prefix, dir,
                                                       candidates, candidate_flags,
                                                       candidate_x, candidate_y,
                                                       candidate_remap,
                                                       candidate_intensity,
                                                       (int)(sizeof(candidates) / sizeof(candidates[0])));
        if (count <= 0) {
            frames[dir] = fallback_frame;
            offset_x[dir] = 0;
            offset_y[dir] = 0;
            intensity[dir] = 16;
        } else {
            int frame_index = step < count ? step : count - 1;
            if (!mirror_left && dir > 4 && count == sequence_count * 2 &&
                step + sequence_count < count) {
                frame_index = step + sequence_count;
            }
            frames[dir] = candidates[frame_index];
            flags[dir] = candidate_flags[frame_index];
            offset_x[dir] = candidate_x[frame_index];
            offset_y[dir] = candidate_y[frame_index];
            remap[dir] = candidate_remap[frame_index];
            intensity[dir] = candidate_intensity[frame_index];
        }
    }

    fprintf(out, "    { %s, %d, %d, %s, %s, 0, %d, 0, 8, {",
            spr, frames[0], tics, action, next, group);
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", dirs[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", frames[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i)
        fprintf(out, "%s%s", i ? "," : "", flags[i] ? "FLIPPED" : "0");
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", offset_x[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", offset_y[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", remap[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", intensity[i]);
    fprintf(out, "}, DC_NO_OVERLAY },\n");
}

static void f8_fin_layer0_overlay_state(FILE *out, const char *spr, const DcFinAnimation *fin,
                                        const char *label_prefix, int step,
                                        int fallback_frame, int tics,
                                        const char *action, const char *next,
                                        int group, int sequence_count) {
    static const int dirs[8] = {0,2,4,6,8,10,12,14};
    int frames[8];
    int flags[8] = {0};
    int offset_x[8] = {0};
    int offset_y[8] = {0};
    int remap[8] = {0};
    int intensity[8] = {0};
    int overlay_frames[8];
    int overlay_flags[8] = {0};
    int overlay_x[8] = {0};
    int overlay_y[8] = {0};
    int overlay_remap[8] = {0};
    int overlay_intensity[8] = {0};
    bool has_overlay = false;
    for (int dir = 0; dir < 8; ++dir) {
        int candidates[64];
        int candidate_flags[64] = {0};
        int candidate_x[64] = {0};
        int candidate_y[64] = {0};
        int candidate_remap[64] = {0};
        int candidate_intensity[64] = {0};
        int overlay_candidates[64];
        int overlay_candidate_flags[64] = {0};
        int overlay_candidate_x[64] = {0};
        int overlay_candidate_y[64] = {0};
        int overlay_candidate_remap[64] = {0};
        int overlay_candidate_intensity[64] = {0};
        int count = fin_body_frames_for_direction_full(fin, label_prefix, dir,
                                                       candidates, candidate_flags,
                                                       candidate_x, candidate_y,
                                                       candidate_remap,
                                                       candidate_intensity,
                                                       (int)(sizeof(candidates) / sizeof(candidates[0])));
        int overlay_count = fin_stem_layer_frames_for_direction_full(fin, label_prefix,
                                                                     dir, 0,
                                                                     overlay_candidates,
                                                                     overlay_candidate_flags,
                                                                     overlay_candidate_x,
                                                                     overlay_candidate_y,
                                                                     overlay_candidate_remap,
                                                                     overlay_candidate_intensity,
                                                                     (int)(sizeof(overlay_candidates) / sizeof(overlay_candidates[0])));
        if (count <= 0) {
            frames[dir] = fallback_frame;
            offset_x[dir] = 0;
            offset_y[dir] = 0;
            intensity[dir] = 16;
        } else {
            int frame_index = step < count ? step : count - 1;
            if (dir > 4 && count == sequence_count * 2 &&
                step + sequence_count < count) {
                frame_index = step + sequence_count;
            }
            frames[dir] = candidates[frame_index];
            flags[dir] = candidate_flags[frame_index];
            offset_x[dir] = candidate_x[frame_index];
            offset_y[dir] = candidate_y[frame_index];
            remap[dir] = candidate_remap[frame_index];
            intensity[dir] = candidate_intensity[frame_index];
        }
        if (overlay_count <= 0) {
            overlay_frames[dir] = -1;
            overlay_intensity[dir] = 16;
        } else {
            int overlay_index = step < overlay_count ? step : overlay_count - 1;
            if (dir > 4 && overlay_count == sequence_count * 2 &&
                step + sequence_count < overlay_count) {
                overlay_index = step + sequence_count;
            }
            overlay_frames[dir] = overlay_candidates[overlay_index];
            overlay_flags[dir] = overlay_candidate_flags[overlay_index];
            overlay_x[dir] = overlay_candidate_x[overlay_index];
            overlay_y[dir] = overlay_candidate_y[overlay_index];
            overlay_remap[dir] = overlay_candidate_remap[overlay_index];
            overlay_intensity[dir] = overlay_candidate_intensity[overlay_index];
            has_overlay = true;
        }
    }

    fprintf(out, "    { %s, %d, %d, %s, %s, 0, %d, 0, 8, {",
            spr, frames[0], tics, action, next, group);
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", dirs[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", frames[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i)
        fprintf(out, "%s%s", i ? "," : "", flags[i] ? "FLIPPED" : "0");
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", offset_x[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", offset_y[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", remap[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", intensity[i]);
    if (!has_overlay) {
        fprintf(out, "}, DC_NO_OVERLAY },\n");
        return;
    }
    fprintf(out, "}, %s, %d, %s, 8, {",
            spr, overlay_frames[0],
            overlay_flags[0] ? "FLIPPED" : "0");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", dirs[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", overlay_frames[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i)
        fprintf(out, "%s%s", i ? "," : "", overlay_flags[i] ? "FLIPPED" : "0");
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", overlay_x[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", overlay_y[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", overlay_remap[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 8; ++i) fprintf(out, "%s%d", i ? "," : "", overlay_intensity[i]);
    fprintf(out, "} },\n");
}

static void f16_fin_state(FILE *out, const char *spr, const DcFinAnimation *fin,
                          const char *label_prefix, int step, int fallback_frame,
                          int tics, const char *action, const char *next, int group) {
    int frames[16];
    int flags[16] = {0};
    int offset_x[16] = {0};
    int offset_y[16] = {0};
    int remap[16] = {0};
    int intensity[16] = {0};
    for (int code = 0; code < 16; ++code) {
        int candidates[128];
        int candidate_flags[128] = {0};
        int candidate_x[128] = {0};
        int candidate_y[128] = {0};
        int candidate_remap[128] = {0};
        int candidate_intensity[128] = {0};
        int count = fin_body_frames_for_direction16(fin, label_prefix, code,
                                                    candidates, candidate_flags,
                                                    candidate_x, candidate_y,
                                                    candidate_remap,
                                                    candidate_intensity,
                                                    (int)(sizeof(candidates) / sizeof(candidates[0])));
        if (count <= 0) {
            frames[code] = fallback_frame;
            flags[code] = 0;
            offset_x[code] = 0;
            offset_y[code] = 0;
            intensity[code] = 16;
        } else {
            int frame_index = step < count ? step : count - 1;
            frames[code] = candidates[frame_index];
            flags[code] = candidate_flags[frame_index];
            offset_x[code] = candidate_x[frame_index];
            offset_y[code] = candidate_y[frame_index];
            remap[code] = candidate_remap[frame_index];
            intensity[code] = candidate_intensity[frame_index];
        }
    }

    fprintf(out, "    { %s, %d, %d, %s, %s, 0, %d, 0, 16, {",
            spr, frames[0], tics, action, next, group);
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", i);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", frames[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i)
        fprintf(out, "%s%s", i ? "," : "", flags[i] ? "FLIPPED" : "0");
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", offset_x[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", offset_y[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", remap[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", intensity[i]);
    fprintf(out, "}, DC_NO_OVERLAY },\n");
}

static void write_muzzle16(FILE *out, const char *spr, int frame, const int offsets_x[16],
                           const int offsets_y[16]) {
    fprintf(out, "    { %s, %d, 2, A_None, S_NULL, RTS_FRAME_ADDITIVE|RTS_FRAME_TINT_YELLOW, 5, 0, 16, {",
            spr, frame);
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", i);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", frame);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i)
        fprintf(out, "%sRTS_FRAME_ADDITIVE|RTS_FRAME_TINT_YELLOW", i ? "," : "");
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", offsets_x[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", offsets_y[i]);
    fprintf(out, "}, DC_DEFAULT_REMAP, DC_DEFAULT_INTENSITY, DC_NO_OVERLAY },\n");
}

static void f16_fin_layer5_state(FILE *out, const char *spr, const DcFinAnimation *fin,
                                 const char *label_prefix, int step, int fallback_frame,
                                 int tics, const char *action, const char *next, int group) {
    int frames[16];
    int flags[16] = {0};
    int offset_x[16] = {0};
    int offset_y[16] = {0};
    int remap[16] = {0};
    int intensity[16] = {0};
    int overlay_frames[16];
    int overlay_flags[16] = {0};
    int overlay_x[16] = {0};
    int overlay_y[16] = {0};
    int overlay_remap[16] = {0};
    int overlay_intensity[16] = {0};
    for (int code = 0; code < 16; ++code) {
        int candidates[128];
        int candidate_flags[128] = {0};
        int candidate_x[128] = {0};
        int candidate_y[128] = {0};
        int candidate_remap[128] = {0};
        int candidate_intensity[128] = {0};
        int overlay_candidates[128];
        int overlay_candidate_flags[128] = {0};
        int overlay_candidate_x[128] = {0};
        int overlay_candidate_y[128] = {0};
        int overlay_candidate_remap[128] = {0};
        int overlay_candidate_intensity[128] = {0};
        int count = fin_layer5_frames_for_direction16_full(fin, label_prefix, code,
                                                           candidates, candidate_flags,
                                                           candidate_x, candidate_y,
                                                           candidate_remap,
                                                           candidate_intensity,
                                                           overlay_candidates,
                                                           overlay_candidate_flags,
                                                           overlay_candidate_x,
                                                           overlay_candidate_y,
                                                           overlay_candidate_remap,
                                                           overlay_candidate_intensity,
                                                           (int)(sizeof(candidates) / sizeof(candidates[0])));
        if (count <= 0) {
            frames[code] = fallback_frame;
            flags[code] = 0;
            offset_x[code] = 0;
            offset_y[code] = 0;
            intensity[code] = 16;
            overlay_frames[code] = -1;
            overlay_intensity[code] = 16;
        } else {
            int frame_index = step < count ? step : count - 1;
            frames[code] = candidates[frame_index];
            flags[code] = candidate_flags[frame_index];
            offset_x[code] = candidate_x[frame_index];
            offset_y[code] = candidate_y[frame_index];
            remap[code] = candidate_remap[frame_index];
            intensity[code] = candidate_intensity[frame_index];
            overlay_frames[code] = overlay_candidates[frame_index];
            overlay_flags[code] = overlay_candidate_flags[frame_index];
            overlay_x[code] = overlay_candidate_x[frame_index];
            overlay_y[code] = overlay_candidate_y[frame_index];
            overlay_remap[code] = overlay_candidate_remap[frame_index];
            overlay_intensity[code] = overlay_candidate_intensity[frame_index];
        }
    }

    fprintf(out, "    { %s, %d, %d, %s, %s, 0, %d, 0, 16, {",
            spr, frames[0], tics, action, next, group);
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", i);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", frames[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i)
        fprintf(out, "%s%s", i ? "," : "", flags[i] ? "FLIPPED" : "0");
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", offset_x[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", offset_y[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", remap[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", intensity[i]);
    fprintf(out, "}, %s, %d, %s, 16, {",
            spr, overlay_frames[0], overlay_flags[0] ? "FLIPPED" : "0");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", i);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", overlay_frames[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i)
        fprintf(out, "%s%s", i ? "," : "", overlay_flags[i] ? "FLIPPED" : "0");
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", overlay_x[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", overlay_y[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", overlay_remap[i]);
    fprintf(out, "}, {");
    for (int i = 0; i < 16; ++i) fprintf(out, "%s%d", i ? "," : "", overlay_intensity[i]);
    fprintf(out, "} },\n");
}

static void write_fin_sequence(FILE *out, const char *spr, const DcFinAnimation *fin,
                               const char *label_prefix, const char *state_prefix,
                               const char *kind, int count, int fallback_frame,
                               int tics, int group, const char *first_action,
                               const char *exit_state, bool mirror_left) {
    char next[64];
    for (int i = 0; i < count; ++i) {
        if (i + 1 < count) state_name(next, sizeof(next), state_prefix, kind, i + 2);
        else snprintf(next, sizeof(next), "%s", exit_state);
        f8_fin_state(out, spr, fin, label_prefix, i, fallback_frame, tics,
                     i == 0 ? first_action : "A_None", next, group, count, mirror_left);
    }
}

static void write_fin_layer0_overlay_sequence(FILE *out, const char *spr, const DcFinAnimation *fin,
                                              const char *label_prefix,
                                              const char *state_prefix,
                                              const char *kind, int count,
                                              int fallback_frame, int tics,
                                              int group, const char *first_action,
                                              const char *exit_state) {
    char next[64];
    for (int i = 0; i < count; ++i) {
        if (i + 1 < count) state_name(next, sizeof(next), state_prefix, kind, i + 2);
        else snprintf(next, sizeof(next), "%s", exit_state);
        f8_fin_layer0_overlay_state(out, spr, fin, label_prefix, i, fallback_frame,
                                    tics, i == 0 ? first_action : "A_None",
                                    next, group, count);
    }
}

static void write_expl_mining_pulse(FILE *out, const char *spr, const DcFinAnimation *fin,
                                    int count, int tics, int group) {
    static const int dirs[8] = {0,2,4,6,8,10,12,14};
    const DcFinDrawPart *body = dc_fin_find_draw_part_in_animation_header(fin, "EDPLYSTAND14",
                                                       fin->stem_lower, 1, 34);
    if (!body) die("EXPL.FIN missing deployed Exploiter body", NULL);
    DcFinPulseFrame pulse[MAX_EXPL_MINING_PULSE_FRAMES];
    int pulse_count = fin_build_expl_mining_pulse(fin, pulse, MAX_EXPL_MINING_PULSE_FRAMES);
    if (count != pulse_count) die("EXPL mining pulse count mismatch", NULL);

    for (int i = 0; i < count; ++i) {
        const DcFinPulseFrame *overlay = &pulse[i];

        char next[64];
        if (i + 1 < count) state_name(next, sizeof(next), "EXPL", "WORK", i + 2);
        else snprintf(next, sizeof(next), "S_DC_EXPL_WORK1");

        fprintf(out, "    { %s, %d, %d, A_None, %s, 0, %d, 0, 8, {",
                spr, body->frame, tics, next, group);
        for (int dir = 0; dir < 8; ++dir) fprintf(out, "%s%d", dir ? "," : "", dirs[dir]);
        fprintf(out, "}, {");
        for (int dir = 0; dir < 8; ++dir) fprintf(out, "%s%d", dir ? "," : "", body->frame);
        fprintf(out, "}, {");
        for (int dir = 0; dir < 8; ++dir) fprintf(out, "%s0", dir ? "," : "");
        fprintf(out, "}, {");
        for (int dir = 0; dir < 8; ++dir) fprintf(out, "%s%d", dir ? "," : "", body->x);
        fprintf(out, "}, {");
        for (int dir = 0; dir < 8; ++dir) fprintf(out, "%s%d", dir ? "," : "", body->y);
        fprintf(out, "}, {");
        for (int dir = 0; dir < 8; ++dir) fprintf(out, "%s%d", dir ? "," : "", body->remap);
        fprintf(out, "}, {");
        for (int dir = 0; dir < 8; ++dir) fprintf(out, "%s%d", dir ? "," : "", body->intensity);
        fprintf(out, "}, %s, %d, 0, 8, {", spr, overlay->frame);
        for (int dir = 0; dir < 8; ++dir) fprintf(out, "%s%d", dir ? "," : "", dirs[dir]);
        fprintf(out, "}, {");
        for (int dir = 0; dir < 8; ++dir) fprintf(out, "%s%d", dir ? "," : "", overlay->frame);
        fprintf(out, "}, {");
        for (int dir = 0; dir < 8; ++dir) fprintf(out, "%s0", dir ? "," : "");
        fprintf(out, "}, {");
        for (int dir = 0; dir < 8; ++dir) fprintf(out, "%s%d", dir ? "," : "", overlay->x);
        fprintf(out, "}, {");
        for (int dir = 0; dir < 8; ++dir) fprintf(out, "%s%d", dir ? "," : "", overlay->y);
        fprintf(out, "}, {");
        for (int dir = 0; dir < 8; ++dir) fprintf(out, "%s%d", dir ? "," : "", overlay->remap);
        fprintf(out, "}, {");
        for (int dir = 0; dir < 8; ++dir) fprintf(out, "%s%d", dir ? "," : "", overlay->intensity);
        fprintf(out, "} },\n");
    }
}

static void write_fin_corpse(FILE *out, const char *spr, const DcFinAnimation *fin,
                             const char *label_prefix, int last_step, int fallback_frame,
                             bool mirror_left) {
    f8_fin_state(out, spr, fin, label_prefix, last_step, fallback_frame, 1,
                 "A_DC_Corpse", "S_NULL", 4, last_step + 1, mirror_left);
}

static void write_fin_sequence16(FILE *out, const char *spr, const DcFinAnimation *fin,
                                 const char *label_prefix, const char *state_prefix,
                                 const char *kind, int count, int fallback_frame,
                                 int tics, int group, const char *first_action,
                                 const char *exit_state) {
    char next[64];
    for (int i = 0; i < count; ++i) {
        if (i + 1 < count) state_name(next, sizeof(next), state_prefix, kind, i + 2);
        else snprintf(next, sizeof(next), "%s", exit_state);
        f16_fin_state(out, spr, fin, label_prefix, i, fallback_frame, tics,
                      i == 0 ? first_action : "A_None", next, group);
    }
}

static void write_fin_layer5_sequence16(FILE *out, const char *spr, const DcFinAnimation *fin,
                                        const char *label_prefix, const char *state_prefix,
                                        const char *kind, int count, int fallback_frame,
                                        int tics, int group, const char *const *actions,
                                        const char *exit_state) {
    char next[64];
    for (int i = 0; i < count; ++i) {
        if (i + 1 < count) state_name(next, sizeof(next), state_prefix, kind, i + 2);
        else snprintf(next, sizeof(next), "%s", exit_state);
        const char *action = actions && actions[i] ? actions[i] : "A_None";
        f16_fin_layer5_state(out, spr, fin, label_prefix, i, fallback_frame, tics,
                             action, next, group);
    }
}

static void write_fin_corpse16(FILE *out, const char *spr, const DcFinAnimation *fin,
                               const char *label_prefix, int last_step, int fallback_frame) {
    f16_fin_state(out, spr, fin, label_prefix, last_step, fallback_frame, 1,
                  "A_DC_Corpse", "S_NULL", 4);
}

static void write_fin_label_effect_chain(FILE *out, const char *root,
                                         const SpriteEntry *sprites, int sprite_count,
                                         const DcFinAnimation *fin, const char *label_name,
                                         const char *state_prefix, int direction_code,
                                         int tics) {
    (void)root;
    const DcFinAnimationHeader *label = dc_fin_find_animation_header(fin, label_name);
    if (!dc_fin_animation_header_has_valid_frames(fin, label)) {
        return;
    }
    const DcFinDrawPart *draw_parts[512];
    int draw_part_count = dc_fin_draw_parts_for_animation_header(fin, label, draw_parts,
                                               (int)(sizeof(draw_parts) / sizeof(draw_parts[0])));
    int effect_index = 0;
    for (int i = 0; i < draw_part_count; ++i) {
        const DcFinDrawPart *cmd = draw_parts[i];
        if (strcmp(cmd->sprite, fin->stem_lower) == 0 && cmd->layer == 1) continue;
        effect_index++;

        int sprite_index = find_sprite_for_fin_stem(sprites, sprite_count, cmd->sprite);

        char next[64];
        if (effect_index < fin_effect_draw_part_count_for_label(fin, label_name)) {
            snprintf(next, sizeof(next), "S_DC_%s_FX%d", state_prefix, effect_index + 1);
        } else {
            snprintf(next, sizeof(next), "S_NULL");
        }

        bool blaz = strcmp(cmd->sprite, "blaz") == 0;
        char flag_expr[96];
        if (blaz && (cmd->flags & 1)) {
            snprintf(flag_expr, sizeof(flag_expr),
                     "RTS_FRAME_ADDITIVE|RTS_FRAME_TINT_YELLOW|FLIPPED");
        } else if (blaz) {
            snprintf(flag_expr, sizeof(flag_expr), "RTS_FRAME_ADDITIVE|RTS_FRAME_TINT_YELLOW");
        } else if (cmd->flags & 1) {
            snprintf(flag_expr, sizeof(flag_expr), "FLIPPED");
        } else {
            snprintf(flag_expr, sizeof(flag_expr), "0");
        }
        fprintf(out, "    { %s, %d, %d, A_None, %s, %s, 5, 0, 1, {%d}, {%d}, {%s}, {%d}, {%d}, {%d}, {%d}, DC_NO_OVERLAY },\n",
                sprites[sprite_index].symbol, cmd->frame, tics, next, flag_expr,
                direction_code, cmd->frame, flag_expr,
                cmd->x, cmd->y,
                cmd->remap, cmd->intensity);
    }
}

static void write_muzzle(FILE *out, const char *spr, int frame, const int offsets_x[8],
                         const int offsets_y[8]) {
    static const int dirs[8] = {0,2,4,6,8,10,12,14};
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
    fprintf(out, "}, DC_DEFAULT_REMAP, DC_DEFAULT_INTENSITY, DC_NO_OVERLAY },\n");
}

static void write_source(FILE *out, const SpriteEntry *sprites, int sprite_count,
                         const char *root, const DcFinStateCounts *counts) {
    int trsc = find_sprite(sprites, sprite_count, "SPRITES/TRSC.SPR");
    int gray = find_sprite(sprites, sprite_count, "SPRITES/GRAY.SPR");
    int reap = find_sprite(sprites, sprite_count, "SPRITES/REAP.SPR");
    int barr = find_sprite(sprites, sprite_count, "SPRITES/BARR.SPR");
    int sarg = find_sprite(sprites, sprite_count, "SPRITES/SARG.SPR");
    int scgm = find_sprite(sprites, sprite_count, "SPRITES/SCGM.SPR");
    int expl = find_sprite(sprites, sprite_count, "SPRITES/EXPL.SPR");
    int hubu = find_sprite(sprites, sprite_count, "SPRITES/HUBU.SPR");
    int towr = find_sprite(sprites, sprite_count, "SPRITES/TOWR.SPR");
    int blaz = find_sprite(sprites, sprite_count, "SPRITES/BLAZ.SPR");
    char trsc_fin_path[1024];
    char gray_fin_path[1024];
    char reap_fin_path[1024];
    char barr_fin_path[1024];
    char sarg_fin_path[1024];
    char scgm_fin_path[1024];
    char expl_fin_path[1024];
    char hubu_fin_path[1024];
    char towr_fin_path[1024];
    snprintf(trsc_fin_path, sizeof(trsc_fin_path), "%s/ANIMATE/TRSC.FIN", root);
    snprintf(gray_fin_path, sizeof(gray_fin_path), "%s/ANIMATE/GRAY.FIN", root);
    snprintf(reap_fin_path, sizeof(reap_fin_path), "%s/ANIMATE/REAP.FIN", root);
    snprintf(barr_fin_path, sizeof(barr_fin_path), "%s/ANIMATE/BARR.FIN", root);
    snprintf(sarg_fin_path, sizeof(sarg_fin_path), "%s/ANIMATE/SARG.FIN", root);
    snprintf(scgm_fin_path, sizeof(scgm_fin_path), "%s/ANIMATE/SCGM.FIN", root);
    snprintf(expl_fin_path, sizeof(expl_fin_path), "%s/ANIMATE/EXPL.FIN", root);
    snprintf(hubu_fin_path, sizeof(hubu_fin_path), "%s/ANIMATE/HUBU.FIN", root);
    snprintf(towr_fin_path, sizeof(towr_fin_path), "%s/ANIMATE/TOWR.FIN", root);
    DcFinAnimation trsc_fin = dc_load_fin_animation(trsc_fin_path, "TRSC");
    DcFinAnimation gray_fin = dc_load_fin_animation(gray_fin_path, "GRAY");
    DcFinAnimation reap_fin = dc_load_fin_animation(reap_fin_path, "REAP");
    DcFinAnimation barr_fin = dc_load_fin_animation(barr_fin_path, "BARR");
    DcFinAnimation sarg_fin = dc_load_fin_animation(sarg_fin_path, "SARG");
    DcFinAnimation scgm_fin = dc_load_fin_animation(scgm_fin_path, "SCGM");
    DcFinAnimation expl_fin = dc_load_fin_animation(expl_fin_path, "EXPL");
    DcFinAnimation hubu_fin = dc_load_fin_animation(hubu_fin_path, "HUBU");
    DcFinAnimation towr_fin = dc_load_fin_animation(towr_fin_path, "TOWR");
    int trsc_attack_base = fin_first_body_frame(&trsc_fin, "TRSCFIREA0");
    int gray_attack_base = fin_first_body_frame(&gray_fin, "GRAYFIREA0");
    const DcFinDrawPart *excopod_stand =
        dc_fin_required_draw_part_in_animation_header(&hubu_fin, "EXCOPODSTAND0", "hubu", 1, 0);
    const DcFinDrawPart *brrkpod_stand =
        dc_fin_required_draw_part_in_animation_header(&hubu_fin, "BRRKPODSTAND0", "hubu", 1, 4);
    const DcFinDrawPart *towr_stand =
        dc_fin_required_draw_part_in_animation_header(&towr_fin, "TOWRSTAND0", "towr", 1, 0);
    char blaz_path[1024];
    snprintf(blaz_path, sizeof(blaz_path), "%s/SPRITES/BLAZ.SPR", root);
    int blaz_w = 0, blaz_h = 0;
    spr_frame_size(blaz_path, 0, &blaz_w, &blaz_h);
    int trsc_muzzle_frame = 0, gray_muzzle_frame = 0, reap_muzzle_frame = 0;
    int trsc_muzzle_x[8] = {0}, trsc_muzzle_y[8] = {0};
    int gray_muzzle_x[8] = {0}, gray_muzzle_y[8] = {0};
    int reap_muzzle_x[16] = {0}, reap_muzzle_y[16] = {0};
    fin_muzzle_for_body_row(&trsc_fin, "TRSC", trsc_attack_base, blaz_w, blaz_h,
                            &trsc_muzzle_frame, trsc_muzzle_x, trsc_muzzle_y);
    fin_muzzle_for_body_row(&gray_fin, "GRAY", gray_attack_base, blaz_w, blaz_h,
                            &gray_muzzle_frame, gray_muzzle_x, gray_muzzle_y);
    fin_muzzle_for_sequence16_step(&reap_fin, "REAPFIRE", 1, blaz_w, blaz_h,
                                   &reap_muzzle_frame, reap_muzzle_x, reap_muzzle_y);
    fprintf(out, "/* Generated by tools/dc_info_gen.c. Do not edit by hand. */\n");
    fprintf(out, "#include \"info.h\"\n\n");
    fprintf(out, "const char *const sprnames[NUMSPRITES] = {\n");
    for (int i = 0; i < sprite_count; ++i) fprintf(out, "    \"%s\",\n", sprites[i].path);
    fprintf(out, "};\n\n");
    fprintf(out, "#define A_None NULL\n");
    fprintf(out, "#define FLIPPED RTS_SPRITEFRAME_FLIP_X\n");
    fprintf(out, "#define DC_DEFAULT_REMAP {0}\n");
    fprintf(out, "#define DC_DEFAULT_INTENSITY {16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16}\n");
    fprintf(out, "#define DC_NO_OVERLAY 0, 0, 0, 0, {0}, {0}, {0}, {0}, {0}, DC_DEFAULT_REMAP, DC_DEFAULT_INTENSITY\n\n");
    fprintf(out, "const State states[NUMSTATES] = {\n");
    fprintf(out, "    { 0, 0, -1, A_None, S_NULL, 0, 0, 0, 0, {0}, {0}, {0}, {0}, {0}, DC_DEFAULT_REMAP, DC_DEFAULT_INTENSITY, DC_NO_OVERLAY },\n");
    f1_fin_raw_state(out, sprites[hubu].symbol, excopod_stand, "S_DC_EXCOPOD_STND");
    f1_fin_raw_state(out, sprites[hubu].symbol, brrkpod_stand, "S_DC_BRRKPOD_STND");
    f1_fin_raw_state(out, sprites[towr].symbol, towr_stand, "S_DC_TOWR_STND");

    int trsc_die[6] = {128,138,149,159,179,195};
    f8_fin_state(out, sprites[trsc].symbol, &trsc_fin, "TRSCSTAND", 0, 0, -1,
                 "A_None", "S_DC_TRSC_STND", 1, 1, false);
    const char *trsc_run_next[8] = {"S_DC_TRSC_RUN2","S_DC_TRSC_RUN3","S_DC_TRSC_RUN4","S_DC_TRSC_RUN5","S_DC_TRSC_RUN6","S_DC_TRSC_RUN7","S_DC_TRSC_RUN8","S_DC_TRSC_RUN1"};
    for (int i = 0; i < 8; ++i) {
        f8_fin_state(out, sprites[trsc].symbol, &trsc_fin, "TRSCMOVE", i, 0, 3,
                     "A_None", trsc_run_next[i], 2, 8, false);
    }
    f8_fin_state(out, sprites[trsc].symbol, &trsc_fin, "TRSCFIREA", 0, 0, 0,
                 "A_DC_TrooperAttackStart", "S_DC_TRSC_STND", 3,
                 counts->trsc_attack_a, false);
    for (int i = 0; i < counts->trsc_attack_a; ++i) {
        char next[64];
        if (i + 1 < counts->trsc_attack_a) state_name(next, sizeof(next), "TRSC", "ATK", i + 2);
        else snprintf(next, sizeof(next), "S_DC_TRSC_STND");
        const char *action = i == 1 ? "A_DC_MuzzleFlash" :
            (i == 2 ? "A_DC_Attack" : "A_None");
        f8_fin_state(out, sprites[trsc].symbol, &trsc_fin, "TRSCFIREA", i, 0, 2,
                     action, next, 3, counts->trsc_attack_a, false);
    }
    for (int i = 0; i < counts->trsc_attack_b; ++i) {
        char next[64];
        if (i + 1 < counts->trsc_attack_b) snprintf(next, sizeof(next), "S_DC_TRSC_ATKB%d", i + 2);
        else snprintf(next, sizeof(next), "S_DC_TRSC_STND");
        const char *action = i == 0 ? "A_DC_MuzzleFlash" :
            (i == 1 ? "A_DC_Attack" : "A_None");
        f8_fin_state(out, sprites[trsc].symbol, &trsc_fin, "TRSCFIREB", i, 0, 2,
                     action, next, 3, counts->trsc_attack_b, false);
    }
    const char *trsc_die_next[11] = {"S_DC_TRSC_DIE2","S_DC_TRSC_DIE3","S_DC_TRSC_DIE4","S_DC_TRSC_DIE5","S_DC_TRSC_DIE6","S_DC_TRSC_DIE7","S_DC_TRSC_DIE8","S_DC_TRSC_DIE9","S_DC_TRSC_DIE10","S_DC_TRSC_CORPSE","S_NULL"};
    for (int i = 0; i < 10; ++i) f6(out, sprites[trsc].symbol, 3, i == 0 ? "A_DC_Fall" : "A_None", trsc_die_next[i], 4, trsc_die, i);
    f6(out, sprites[trsc].symbol, 1, "A_DC_Corpse", trsc_die_next[10], 4, trsc_die, 9);

    f8_fin_state(out, sprites[gray].symbol, &gray_fin, "GRAYSTAND", 0, 0, -1,
                 "A_None", "S_DC_GRAY_STND", 1, 1, false);
    const char *gray_run_next[8] = {"S_DC_GRAY_RUN2","S_DC_GRAY_RUN3","S_DC_GRAY_RUN4","S_DC_GRAY_RUN5","S_DC_GRAY_RUN6","S_DC_GRAY_RUN7","S_DC_GRAY_RUN8","S_DC_GRAY_RUN1"};
    for (int i = 0; i < 8; ++i) {
        f8_fin_state(out, sprites[gray].symbol, &gray_fin, "GRAYMOVE", i, 0, 3,
                     "A_None", gray_run_next[i], 2, 8, false);
    }
    const char *gray_atk_next[8] = {"S_DC_GRAY_ATK2","S_DC_GRAY_ATK3","S_DC_GRAY_ATK4","S_DC_GRAY_ATK5","S_DC_GRAY_ATK6","S_DC_GRAY_STND","S_DC_GRAY_STND","S_DC_GRAY_STND"};
    const char *gray_atk_action[8] = {"A_None","A_DC_MuzzleFlash","A_DC_Attack","A_None","A_None","A_None","A_None","A_None"};
    for (int i = 0; i < 8; ++i) {
        f8_fin_state(out, sprites[gray].symbol, &gray_fin, "GRAYFIREA", i, 0, 2,
                     gray_atk_action[i], gray_atk_next[i], 3, 8, false);
    }
    const char *gray_die_next[13] = {"S_DC_GRAY_DIE2","S_DC_GRAY_DIE3","S_DC_GRAY_DIE4","S_DC_GRAY_DIE5","S_DC_GRAY_DIE6","S_DC_GRAY_DIE7","S_DC_GRAY_DIE8","S_DC_GRAY_DIE9","S_DC_GRAY_ROT1","S_DC_GRAY_ROT2","S_DC_GRAY_ROT3","S_DC_GRAY_CORPSE","S_NULL"};
    for (int i = 0; i < 12; ++i) gray_die(out, gray_die_next[i], i, i == 0 ? "A_DC_Fall" : "A_None");
    gray_die(out, gray_die_next[12], 11, "A_DC_Corpse");

    f16_fin_state(out, sprites[reap].symbol, &reap_fin, "REAPSTAND", 0, 0, -1,
                  "A_None", "S_DC_REAP_STND", 1);
    write_fin_sequence16(out, sprites[reap].symbol, &reap_fin, "REAPMOVE", "REAP", "RUN",
                         counts->reap_run, 0, 3, 2, "A_None", "S_DC_REAP_RUN1");
    const char *reap_atk_actions[8] = {
        "A_None", "A_DC_MuzzleFlash", "A_DC_Attack", "A_None",
        "A_None", "A_None", "A_None", "A_None"
    };
    write_fin_layer5_sequence16(out, sprites[reap].symbol, &reap_fin, "REAPFIRE", "REAP", "ATK",
                                counts->reap_attack, 0, 2, 3, reap_atk_actions,
                                "S_DC_REAP_STND");
    write_fin_sequence16(out, sprites[reap].symbol, &reap_fin, "REAPDIEA", "REAP", "DIE",
                         counts->reap_death, 0, 3, 4, "A_DC_ReaperDeath", "S_DC_REAP_CORPSE");
    write_fin_corpse16(out, sprites[reap].symbol, &reap_fin, "REAPDIEA", counts->reap_death - 1, 0);
    write_fin_label_effect_chain(out, root, sprites, sprite_count, &reap_fin,
                                 "REAPDIEA14", "REAP_DIEA14", 2, 2);
    write_fin_label_effect_chain(out, root, sprites, sprite_count, &reap_fin,
                                 "REAPDIEA6", "REAP_DIEA6", 10, 2);

    f16_fin_state(out, sprites[barr].symbol, &barr_fin, "BARRSTAND", 0, 0, -1,
                  "A_None", "S_DC_BARR_STND", 1);
    write_fin_sequence16(out, sprites[barr].symbol, &barr_fin, "BARRMOVE", "BARR", "RUN",
                         counts->barr_run, 0, 3, 2, "A_None", "S_DC_BARR_RUN1");
    write_fin_sequence16(out, sprites[barr].symbol, &barr_fin, "BARRFIREA", "BARR", "ATK",
                         counts->barr_attack, 0, 2, 3, "A_None", "S_DC_BARR_STND");
    write_fin_sequence16(out, sprites[barr].symbol, &barr_fin, "BARRDIE", "BARR", "DIE",
                         counts->barr_death, 0, 3, 4, "A_DC_Fall", "S_DC_BARR_CORPSE");
    write_fin_corpse16(out, sprites[barr].symbol, &barr_fin, "BARRDIE", counts->barr_death - 1, 0);

    f8_fin_state(out, sprites[sarg].symbol, &sarg_fin, "SARGSTAND", 0, 0, -1,
                 "A_None", "S_DC_SARG_STND", 1, 1, false);
    write_fin_sequence(out, sprites[sarg].symbol, &sarg_fin, "SARGMOVE", "SARG", "RUN",
                       counts->sarg_run, 0, 3, 2, "A_None", "S_DC_SARG_RUN1", false);
    write_fin_sequence(out, sprites[sarg].symbol, &sarg_fin, "SARGFIREA", "SARG", "ATK",
                       counts->sarg_attack, 0, 2, 3, "A_None", "S_DC_SARG_STND", false);
    write_fin_sequence(out, sprites[sarg].symbol, &sarg_fin, "SARGDIE", "SARG", "DIE",
                       counts->sarg_death, 0, 3, 4, "A_DC_Fall", "S_DC_SARG_CORPSE", false);
    write_fin_corpse(out, sprites[sarg].symbol, &sarg_fin, "SARGDIE", counts->sarg_death - 1, 0, false);

    f8_fin_state(out, sprites[scgm].symbol, &scgm_fin, "SCGMSTAND", 0, 0, -1,
                 "A_None", "S_DC_SCGM_STND", 1, 1, false);
    write_fin_sequence(out, sprites[scgm].symbol, &scgm_fin, "SCGMMOVE", "SCGM", "RUN",
                       counts->scgm_run, 0, 3, 2, "A_None", "S_DC_SCGM_RUN1", false);
    write_fin_sequence(out, sprites[scgm].symbol, &scgm_fin, "SCGMDIE", "SCGM", "DIE",
                       counts->scgm_death, 0, 3, 4, "A_DC_Fall", "S_DC_SCGM_CORPSE", false);
    write_fin_corpse(out, sprites[scgm].symbol, &scgm_fin, "SCGMDIE", counts->scgm_death - 1, 0, false);

    f8_fin_state(out, sprites[expl].symbol, &expl_fin, "EXPLSTAND", 0, 0, -1,
                 "A_None", "S_DC_EXPL_STND", 1, 1, false);
    write_fin_sequence(out, sprites[expl].symbol, &expl_fin, "EXPLMOVE", "EXPL", "RUN",
                       counts->expl_run, 0, 3, 2, "A_None", "S_DC_EXPL_RUN1", false);
    write_fin_layer0_overlay_sequence(out, sprites[expl].symbol, &expl_fin, "EXPLDEPLOY",
                                      "EXPL", "DEPLOY", counts->expl_deploy, 0, 3, 5,
                                      "A_None", "S_DC_EXPL_WORK1");
    write_expl_mining_pulse(out, sprites[expl].symbol, &expl_fin, counts->expl_work, 5, 5);
    write_fin_sequence(out, sprites[expl].symbol, &expl_fin, "EXPLDIE", "EXPL", "DIE",
                       counts->expl_death, 0, 3, 4, "A_DC_Fall", "S_DC_EXPL_CORPSE", false);
    write_fin_corpse(out, sprites[expl].symbol, &expl_fin, "EXPLDIE", counts->expl_death - 1, 0, false);
    write_muzzle(out, sprites[blaz].symbol, trsc_muzzle_frame, trsc_muzzle_x, trsc_muzzle_y);
    write_muzzle(out, sprites[blaz].symbol, gray_muzzle_frame, gray_muzzle_x, gray_muzzle_y);
    write_muzzle16(out, sprites[blaz].symbol, reap_muzzle_frame, reap_muzzle_x, reap_muzzle_y);
    fprintf(out, "};\n\n");

    fprintf(out, "const MobjInfo mobjinfo[NUMMOBJTYPES] = {\n");
    fprintf(out, "    {0},\n");
    fprintf(out, "    { 1, S_DC_TRSC_STND, 800, S_DC_TRSC_RUN1, 0, 0, 0, S_NULL, 0, 0, 0, S_DC_TRSC_ATK_SELECT, S_DC_TRSC_DIE1, S_DC_TRSC_DIE1, 0, 5, 16, 32, 100, 100, 0, RTS_TRAIT_SELECTABLE|RTS_TRAIT_MOBILE|RTS_TRAIT_RENDERABLE|RTS_TRAIT_ATTACK, S_NULL, S_DC_TRSC_MUZZLE },\n");
    fprintf(out, "    { 2, S_DC_GRAY_STND, 800, S_DC_GRAY_RUN1, 0, 0, 0, S_NULL, 0, 0, 0, S_DC_GRAY_ATK1, S_DC_GRAY_DIE1, S_DC_GRAY_DIE1, 0, 5, 16, 32, 100, 100, 0, RTS_TRAIT_SELECTABLE|RTS_TRAIT_MOBILE|RTS_TRAIT_RENDERABLE|RTS_TRAIT_ATTACK, S_NULL, S_DC_GRAY_MUZZLE },\n");
    fprintf(out, "    { 3, S_DC_EXPL_STND, 800, S_DC_EXPL_RUN1, 0, 0, 0, S_NULL, 0, 0, 0, S_NULL, S_DC_EXPL_DIE1, S_DC_EXPL_DIE1, 0, 5, 16, 32, 100, 0, 0, RTS_TRAIT_SELECTABLE|RTS_TRAIT_MOBILE|RTS_TRAIT_RENDERABLE|RTS_TRAIT_HARVESTER, S_NULL, S_NULL },\n");
    fprintf(out, "    { 2, S_DC_REAP_STND, 800, S_DC_REAP_RUN1, 0, 0, 0, S_NULL, 0, 0, 0, S_DC_REAP_ATK1, S_DC_REAP_DIE1, S_DC_REAP_DIE1, 0, 6, 16, 32, 100, 100, 0, RTS_TRAIT_SELECTABLE|RTS_TRAIT_MOBILE|RTS_TRAIT_RENDERABLE|RTS_TRAIT_ATTACK, S_NULL, S_DC_REAP_MUZZLE },\n");
    fprintf(out, "    { 3, S_DC_BARR_STND, 400, S_DC_BARR_RUN1, 0, 0, 0, S_NULL, 0, 0, 0, S_NULL, S_DC_BARR_DIE1, S_DC_BARR_DIE1, 0, 3, 16, 32, 100, 0, 0, RTS_TRAIT_SELECTABLE|RTS_TRAIT_MOBILE|RTS_TRAIT_RENDERABLE, S_NULL, S_NULL },\n");
    fprintf(out, "    { 4, S_DC_SARG_STND, 800, S_DC_SARG_RUN1, 0, 0, 0, S_NULL, 0, 0, 0, S_NULL, S_DC_SARG_DIE1, S_DC_SARG_DIE1, 0, 9, 16, 32, 100, 0, 0, RTS_TRAIT_SELECTABLE|RTS_TRAIT_MOBILE|RTS_TRAIT_RENDERABLE, S_NULL, S_NULL },\n");
    fprintf(out, "    { 5, S_DC_SCGM_STND, 800, S_DC_SCGM_RUN1, 0, 0, 0, S_NULL, 0, 0, 0, S_NULL, S_DC_SCGM_DIE1, S_DC_SCGM_DIE1, 0, 9, 16, 32, 100, 0, 0, RTS_TRAIT_SELECTABLE|RTS_TRAIT_MOBILE|RTS_TRAIT_RENDERABLE, S_NULL, S_NULL },\n");
    fprintf(out, "};\n\n");
    fprintf(out, "const GameInfo dark_colony_game_info = {\n");
    fprintf(out, "    sprnames,\n");
    fprintf(out, "    NUMSPRITES,\n");
    fprintf(out, "    states,\n");
    fprintf(out, "    NUMSTATES,\n");
    fprintf(out, "    mobjinfo,\n");
    fprintf(out, "    NUMMOBJTYPES,\n");
    fprintf(out, "    S_NULL,\n");
    fprintf(out, "    RTS_DIRECTION_DARK_COLONY_16,\n");
    fprintf(out, "    RTS_STATE_COORDS_FIN_TOP_LEFT,\n");
    fprintf(out, "    { SPR_DC_INTRFACE_CLIENT, 0, 1, 3, -3 },\n");
    fprintf(out, "};\n");
    dc_free_fin_animation(&trsc_fin);
    dc_free_fin_animation(&gray_fin);
    dc_free_fin_animation(&reap_fin);
    dc_free_fin_animation(&barr_fin);
    dc_free_fin_animation(&sarg_fin);
    dc_free_fin_animation(&scgm_fin);
    dc_free_fin_animation(&expl_fin);
    dc_free_fin_animation(&hubu_fin);
    dc_free_fin_animation(&towr_fin);
}

int main(int argc, char **argv) {
    if (argc != 4) die("usage: dc_info_gen DATA/DCOLONY plugins/DarkColony/info.h plugins/DarkColony/info.c", NULL);
    const char *root = argv[1];
    SpriteList sprite_list = {0};
    scan_sprites_recursive(&sprite_list, root, "");
    SpriteEntry *sprites = sprite_list.items;
    int count = sprite_list.count;
    qsort(sprites, (size_t)count, sizeof(*sprites), compare_sprite_entry);
    validate_unique_sprite_symbols(sprites, count);
    validate_dark_colony_data(root, sprites, count);
    DcFinStateCounts state_counts = load_fin_state_counts(root);

    FILE *h = fopen(argv[2], "w");
    if (!h) die(strerror(errno), argv[2]);
    write_header(h, sprites, count, &state_counts);
    fclose(h);

    FILE *c = fopen(argv[3], "w");
    if (!c) die(strerror(errno), argv[3]);
    write_source(c, sprites, count, root, &state_counts);
    fclose(c);

    free(sprite_list.items);
    return 0;
}
