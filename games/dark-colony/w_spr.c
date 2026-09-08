#define _DEFAULT_SOURCE
#include "w_spr.h"
#include "dc_facing.h"
#include "engine.h"
#include "info.h"
#include "w_wad.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t cell_count;
    uint32_t payload_bytes;
} dc_spr_header_t;

typedef struct __attribute__((packed)) {
    uint16_t width;
    uint16_t height;
    uint16_t dis_x;
    uint16_t dis_y;
} dc_spr_cell_t;

/* WAD cell lump layout: this header followed immediately by width*height indexed pixels. */
typedef struct __attribute__((packed)) {
    uint16_t width;
    uint16_t height;
    uint16_t dis_x;
    uint16_t dis_y;
} dc_wad_cell_t;

typedef struct {
    void *data;
    size_t size;
    const dc_spr_header_t *header;
    const uint8_t *palette;
    const dc_spr_cell_t *cells;
    const uint8_t *payload;
} dc_spr_t;

static uint32_t render_palette[256];
static uint8_t render_blend[256 * 256];
static bool render_tables_ready;

_Static_assert(sizeof(dc_spr_header_t) == 8, "Dark Colony SPR header layout");
_Static_assert(sizeof(dc_spr_cell_t) == 8, "Dark Colony SPR cell layout");

static bool fread_file(const char *path, void **data, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) goto fail;
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) goto fail;
    *size = (size_t)length;
    *data = malloc(*size);
    if (!*data || fread(*data, *size, 1, file) != 1) goto fail;
    fclose(file);
    return true;
fail:
    if (file) fclose(file);
    free(*data);
    *data = NULL;
    *size = 0;
    return false;
}

bool W_LoadFin(const char *path, dc_fin_t *fin) {
    memset(fin, 0, sizeof(*fin));
    if (!fread_file(path, &fin->data, &fin->size) || fin->size < sizeof(dc_fin_header_t))
        return false;
    fin->header = fin->data;
    size_t offset = sizeof(*fin->header);
    fin->dependencies = (const dc_fin_dependency_t *)((const uint8_t *)fin->data + offset);
    offset += (size_t)fin->header->dependency_count * sizeof(*fin->dependencies);
    fin->labels = (const dc_fin_label_t *)((const uint8_t *)fin->data + offset);
    offset += (size_t)fin->header->label_count * sizeof(*fin->labels);
    fin->frames = (const dc_fin_frame_t *)((const uint8_t *)fin->data + offset);
    offset += (size_t)fin->header->frame_count * sizeof(*fin->frames);
    if (offset > fin->size || (fin->size - offset) % sizeof(spritelayer_t)) goto fail;
    fin->layers = (const spritelayer_t *)((const uint8_t *)fin->data + offset);
    fin->layer_count = (int)((fin->size - offset) / sizeof(*fin->layers));
    size_t referenced = 0;
    for (int i = 0; i < fin->header->frame_count; ++i) referenced += fin->frames[i].part_count;
    if (referenced > (size_t)fin->layer_count) goto fail;
    return true;
fail:
    W_FreeFin(fin);
    return false;
}

bool W_LoadFinForMap(const char *map_path, const char *name, dc_fin_t *fin) {
    const char *scenario = strcasestr(map_path, "/SCENARIO/");
    if (!scenario) return false;
    char path[1024];
    int root_length = (int)(scenario - map_path);
    if (snprintf(path, sizeof(path), "%.*s/ANIMATE/%s.FIN",
                 root_length, map_path, name) >= (int)sizeof(path)) return false;
    return W_LoadFin(path, fin);
}

void W_FreeFin(dc_fin_t *fin) {
    if (!fin) return;
    free(fin->data);
    memset(fin, 0, sizeof(*fin));
}

const dc_fin_label_t *W_FinLabel(const dc_fin_t *fin, const char *name) {
    if (!fin || !fin->header) return NULL;
    for (int i = 0; i < fin->header->label_count; ++i) {
        char label_name[17];
        memcpy(label_name, fin->labels[i].name, 16);
        label_name[16] = '\0';
        for (int end = 15; end >= 0 &&
             (label_name[end] == '\0' || label_name[end] == ' '); --end)
            label_name[end] = '\0';
        if (strcasecmp(label_name, name) == 0)
            return &fin->labels[i];
    }
    return NULL;
}

const spritelayer_t *W_FinFrameLayers(const dc_fin_t *fin, int frame, int *count) {
    if (count) *count = 0;
    if (!fin || !fin->header || frame < 0 || frame >= fin->header->frame_count) return NULL;
    int first = 0;
    for (int i = 0; i < frame; ++i) first += fin->frames[i].part_count;
    if (first + fin->frames[frame].part_count > fin->layer_count) return NULL;
    if (count) *count = fin->frames[frame].part_count;
    return fin->layers + first;
}

int W_FinFrameDuration(const dc_fin_t *fin, int frame) {
    if (!fin || !fin->header || frame < 0 || frame >= fin->header->frame_count) return 0;
    int ticks = fin->frames[frame].ticks ? fin->frames[frame].ticks : fin->header->default_ticks;
    return ((((ticks + 3) * 19) / 100) * 1000 + 15) / 30;
}

static bool load_spr(const char *path, dc_spr_t *spr) {
    memset(spr, 0, sizeof(*spr));
    if (!fread_file(path, &spr->data, &spr->size) ||
        spr->size < sizeof(dc_spr_header_t) + 768) return false;
    spr->header = spr->data;
    if (!spr->header->cell_count || spr->header->cell_count > 1024) goto fail;
    spr->palette = (const uint8_t *)spr->data + sizeof(*spr->header);
    spr->cells = (const dc_spr_cell_t *)(spr->palette + 768);
    spr->payload = (const uint8_t *)(spr->cells + spr->header->cell_count);
    if (spr->payload > (const uint8_t *)spr->data + spr->size) goto fail;
    for (int i = 0; i < spr->header->cell_count; ++i)
        if (spr->cells[i].width > 512 || spr->cells[i].height > 512) goto fail;
    return true;
fail:
    free(spr->data);
    memset(spr, 0, sizeof(*spr));
    return false;
}

static bool decode_spr(const dc_spr_t *spr, uint8_t *pixels, size_t pixel_count) {
    const uint8_t *source = spr->payload;
    const uint8_t *end = (const uint8_t *)spr->data + spr->size;
    size_t target = 0;
    for (int i = 0; i < spr->header->cell_count; ++i) {
        size_t count = (size_t)spr->cells[i].width * spr->cells[i].height;
        if (target + count > pixel_count) return false;
        if (!(spr->header->flags & 0x180)) {
            if (source + count > end) return false;
            memcpy(pixels + target, source, count);
            source += count;
        } else {
            if (source + 4 > end) return false;
            uint32_t chunk_size;
            memcpy(&chunk_size, source, sizeof(chunk_size));
            source += 4;
            const uint8_t *chunk_end = source + chunk_size;
            if (chunk_end > end) return false;
            size_t written = 0;
            while (source < chunk_end && written < count) {
                int8_t command = (int8_t)*source++;
                if (command < 0) {
                    written += (size_t)-command;
                } else {
                    size_t run = (size_t)command + 1;
                    if (source + run > chunk_end || written + run > count) return false;
                    memcpy(pixels + target + written, source, run);
                    source += run;
                    written += run;
                }
            }
            source = chunk_end;
        }
        target += count;
    }
    return true;
}

static irect_t indexed_visible_bounds(const uint8_t *indices, int w, int h) {
    int left = w, top = h, right = -1, bottom = -1;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        if (!indices[y * w + x]) continue;
        if (x < left) left = x;
        if (x > right) right = x;
        if (y < top) top = y;
        if (y > bottom) bottom = y;
    }
    return right < left ? (irect_t){ 0, 0, w, h } :
        (irect_t){ left, top, right - left + 1, bottom - top + 1 };
}

static void convert_vga_palette(uint32_t palette[256], const uint8_t *vga) {
    palette[0] = 0;
    for (int i = 1; i < 256; ++i)
        palette[i] = 0xff000000u |
            ((uint32_t)(vga[i * 3]     * 4 + 3) << 16) |
            ((uint32_t)(vga[i * 3 + 1] * 4 + 3) << 8)  |
             (uint32_t)(vga[i * 3 + 2] * 4 + 3);
}

static bool W_ParseFin(const void *data, size_t size, dc_fin_t *fin);

static void fixed_name(char out[17], const char *source, size_t length) {
    memcpy(out, source, length);
    out[length] = '\0';
    for (int i = (int)length - 1; i >= 0 && (out[i] == '\0' || out[i] == ' '); --i)
        out[i] = '\0';
}

static void path_stem(char out[9], const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t length = strcspn(base, ".");
    if (length > 8) length = 8;
    for (size_t i = 0; i < length; ++i) out[i] = (char)tolower((unsigned char)base[i]);
    out[length] = '\0';
}

/* Parse "{base}{direction_number}" from a FIN label name.
   Returns false if the name has no trailing digits or direction is out of range. */
static bool parse_fin_dir(const char raw[SPRITE_FRAME_NAME_SIZE],
                           char base[17], int *direction) {
    char name[17];
    fixed_name(name, raw, SPRITE_FRAME_NAME_SIZE);
    int len = (int)strlen(name), cut = len;
    while (cut > 0 && isdigit((unsigned char)name[cut - 1])) cut--;
    if (cut == len || cut == 0) return false;
    *direction = atoi(name + cut);
    if (*direction < 0 || *direction >= 16) return false;
    memcpy(base, name, (size_t)cut); base[cut] = '\0';
    return true;
}

/* First lump in ns_sprites whose name equals the (case-insensitive) given name. */
static int find_label_first_lump(const char *name) {
    char upper[WAD_NAME_SIZE] = {0};
    for (int i = 0; i < WAD_NAME_SIZE && name[i]; i++)
        upper[i] = (char)toupper((unsigned char)name[i]);
    for (int i = 0; i < numlumps; i++)
        if (lumpinfo[i].ns == ns_sprites &&
            memcmp(lumpinfo[i].name, upper, WAD_NAME_SIZE) == 0)
            return i;
    return -1;
}

/* Build spritedef from WAD FIN label lumps.  For each direction-0 label, the
   WAD already holds consecutive same-name lumps for every step and rotation
   (registered by DC_PopulateWAD / register_fin).  We copy each step's layer
   data from the lump and add a null sentinel so the render can iterate it. */
static bool init_spritedef_from_wad(spritesheet_t *sheet, const dc_fin_t *fin) {
    if (!fin) {
        if (!R_InitSpriteDef(sheet, sheet->numlumps, 1, ANG270, true)) return false;
        for (int f = 0; f < sheet->numlumps; f++)
            R_InstallSpriteLump(sheet, f, 0, f, false);
        return true;
    }

    int max_dir = 0;
    for (int i = 0; i < fin->header->label_count; i++) {
        char base[17]; int dir;
        if (parse_fin_dir(fin->labels[i].name, base, &dir) && dir > max_dir) max_dir = dir;
    }
    int rotations = max_dir >= 8 ? 16 : max_dir >= 1 ? 8 : 1;

    int frame_count = fin->header->frame_count > sheet->numlumps ?
        fin->header->frame_count : sheet->numlumps;
    if (!R_InitSpriteDef(sheet, frame_count, rotations,
                         dc_fin_direction_to_angle(0), true)) return false;
    for (int f = 0; f < sheet->numlumps; f++)
        for (int r = 0; r < rotations; r++)
            R_InstallSpriteLump(sheet, f, r, f, false);

    for (int i = 0; i < fin->header->label_count; i++) {
        char base[17]; int dir;
        if (!parse_fin_dir(fin->labels[i].name, base, &dir) || dir != 0) continue;
        const dc_fin_label_t *origin = &fin->labels[i];
        for (int step = 0; step <= origin->end - origin->start; step++) {
            int logical = origin->start + step;
            if (logical >= frame_count) break;
            for (int rot = 0; rot < rotations; rot++) {
                int native = rot * (16 / rotations);
                char dir_name[17];
                snprintf(dir_name, sizeof(dir_name), "%s%d", base, native);
                int first = find_label_first_lump(dir_name);
                if (first < 0) continue;
                int lump = first + step;
                if (lump >= numlumps || lumpinfo[lump].ns != ns_sprites ||
                    !lumpinfo[lump].data || lumpinfo[lump].size <= 0) continue;
                int n = lumpinfo[lump].size / (int)sizeof(spritelayer_t);
                if (n <= 0) continue;
                spritelayer_t *layers = calloc((size_t)n + 1, sizeof(*layers));
                if (!layers) continue;
                memcpy(layers, lumpinfo[lump].data, (size_t)n * sizeof(*layers));
                spritedirection_t *d = &sheet->spritedef.spriteframes[logical].directions[rot];
                free(d->layers);
                d->layers = layers;
                d->ticks  = fin->frames[origin->start + step].ticks;
            }
        }
    }
    return true;
}

static bool paired_paths(const char *path, char fin_path[1024], char spr_path[1024]) {
    const char *extension = strrchr(path, '.');
    char stem[9];
    path_stem(stem, path);
    if (extension && strcasecmp(extension, ".FIN") == 0) {
        snprintf(fin_path, 1024, "%s", path);
        const char *animate = strcasestr(path, "/ANIMATE/");
        if (!animate) return false;
        return snprintf(spr_path, 1024, "%.*s/SPRITES/%s.SPR",
                        (int)(animate - path), path, stem) < 1024;
    }
    snprintf(spr_path, 1024, "%s", path);
    const char *sprites = strcasestr(path, "/SPRITES/");
    if (!sprites) {
        fin_path[0] = '\0';
        return true;
    }
    snprintf(fin_path, 1024, "%.*s/ANIMATE/%s.FIN",
             (int)(sprites - path), path, stem);
    return true;
}

bool load_render_tables(const char *root, const char *tileset) {
    char relative[128], path[1024];
    void *data = NULL;
    size_t size = 0;
    snprintf(relative, sizeof(relative), "SCENARIO/%s.BTS", tileset);
    M_PathJoin(path, sizeof(path), root, relative);
    if (!fread_file(path, &data, &size) || size < 8 + 768) goto fail;
    convert_vga_palette(render_palette, (const uint8_t *)data + 8);
    free(data);
    data = NULL;
    snprintf(relative, sizeof(relative), "%s.RMP", tileset);
    M_PathJoin(path, sizeof(path), root, relative);
    if (!fread_file(path, &data, &size) || size < 3 * sizeof(render_blend)) goto fail;
    memcpy(render_blend, (const uint8_t *)data + 2 * sizeof(render_blend),
           sizeof(render_blend));
    free(data);
    render_tables_ready = true;
    return true;
fail:
    free(data);
    return false;
}


static bool register_spr(const char *path) {
    dc_spr_t spr = {0};
    if (!load_spr(path, &spr)) return false;

    char stem[9];
    path_stem(stem, path);

    /* palette — 768-byte raw VGA, like PLAYPAL */
    void *pal = malloc(768);
    if (pal) {
        memcpy(pal, spr.palette, 768);
        char pal_name[WAD_NAME_SIZE + 1];
        snprintf(pal_name, sizeof(pal_name), "%sPAL", stem);
        W_AddLump(pal_name, pal, 768, ns_global);
    }

    /* cells — all lumps share the stem name (e.g. 5 consecutive "TRSC"
       lumps = frames 0-4).  FIN lump index maps to offset from first. */
    size_t total = 0;
    for (int i = 0; i < spr.header->cell_count; i++)
        total += (size_t)spr.cells[i].width * spr.cells[i].height;

    uint8_t *pixels = calloc(total ? total : 1, 1);
    if (!pixels || !decode_spr(&spr, pixels, total)) {
        free(pixels);
        free(spr.data);
        return false;
    }

    size_t offset = 0;
    for (int i = 0; i < spr.header->cell_count; i++) {
        size_t cell_size = (size_t)spr.cells[i].width * spr.cells[i].height;
        size_t lump_size = sizeof(dc_wad_cell_t) + cell_size;
        dc_wad_cell_t *lump_data = malloc(lump_size);
        if (lump_data) {
            lump_data->width  = spr.cells[i].width;
            lump_data->height = spr.cells[i].height;
            lump_data->dis_x  = spr.cells[i].dis_x;
            lump_data->dis_y  = spr.cells[i].dis_y;
            if (cell_size) memcpy(lump_data + 1, pixels + offset, cell_size);
            W_AddLump(stem, lump_data, (int)lump_size, ns_sprites);
        }
        offset += cell_size;
    }

    free(pixels);
    free(spr.data);

    /* Store the paired FIN binary so R_InitDCSprites can build spritedef later. */
    char fin_path[1024], spr_path_buf[1024];
    paired_paths(path, fin_path, spr_path_buf);
    if (fin_path[0]) {
        void *fin_data = NULL;
        size_t fin_size = 0;
        if (fread_file(fin_path, &fin_data, &fin_size)) {
            char fin_lump[WAD_NAME_SIZE + 1];
            snprintf(fin_lump, sizeof(fin_lump), "%sFIN", stem);
            W_AddLump(fin_lump, fin_data, (int)fin_size, ns_global);
        }
    }
    return true;
}

static bool register_fin(const char *path) {
    dc_fin_t fin = {0};
    if (!W_LoadFin(path, &fin)) return false;

    for (int i = 0; i < fin.header->label_count; i++) {
        const dc_fin_label_t *label = &fin.labels[i];

        /* FIN labels already use Doom naming: STNDA0, RUNA1, etc.
           Store each frame's spritelayer_t array directly as lump data. */
        for (int step = 0; step <= label->end - label->start; step++) {
            int native = label->start + step;
            int count = 0;
            const spritelayer_t *layers = W_FinFrameLayers(&fin, native, &count);
            if (!layers || !count) continue;

            char name[WAD_NAME_SIZE + 1];
            fixed_name(name, label->name, SPRITE_FRAME_NAME_SIZE);

            size_t sz = (size_t)count * sizeof(spritelayer_t);
            void *data = malloc(sz);
            if (!data) continue;
            memcpy(data, layers, sz);
            W_AddLump(name, data, (int)sz, ns_sprites);
        }
    }

    W_FreeFin(&fin);
    return true;
}

static void scan_dir_register(const char *root, const char *subdir,
                              const char *ext,
                              bool (*reg)(const char *)) {
    char dirpath[1024];
    M_PathJoin(dirpath, sizeof(dirpath), root, subdir);
    DIR *dir = opendir(dirpath);
    if (!dir) return;
    size_t ext_len = strlen(ext);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len <= ext_len ||
            strcasecmp(entry->d_name + len - ext_len, ext) != 0)
            continue;
        char path[1024];
        M_PathJoin(path, sizeof(path), dirpath, entry->d_name);
        reg(path);
    }
    closedir(dir);
}

bool DC_PopulateWAD(const char *root) {
    W_AddMarker("S_START");

    scan_dir_register(root, "SPRITES",  ".SPR", register_spr);
    scan_dir_register(root, "INTRFACE", ".SPR", register_spr);
    scan_dir_register(root, "CURSOR",   ".SPR", register_spr);
    scan_dir_register(root, "ANIMATE",  ".FIN", register_fin);

    W_AddMarker("S_END");
    return numlumps > 2;
}

/* Parse a dc_fin_t from an in-memory buffer (no file I/O).
   fin->data is left NULL so W_FreeFin won't free the buffer. */
static bool W_ParseFin(const void *data, size_t size, dc_fin_t *fin) {
    memset(fin, 0, sizeof(*fin));
    if (!data || size < sizeof(dc_fin_header_t)) return false;
    fin->size   = size;
    fin->header = (const dc_fin_header_t *)data;
    size_t off  = sizeof(*fin->header);
    fin->dependencies = (const dc_fin_dependency_t *)((const uint8_t *)data + off);
    off += (size_t)fin->header->dependency_count * sizeof(*fin->dependencies);
    if (off > size) return false;
    fin->labels = (const dc_fin_label_t *)((const uint8_t *)data + off);
    off += (size_t)fin->header->label_count * sizeof(*fin->labels);
    if (off > size) return false;
    fin->frames = (const dc_fin_frame_t *)((const uint8_t *)data + off);
    off += (size_t)fin->header->frame_count * sizeof(*fin->frames);
    if (off > size || (size - off) % sizeof(spritelayer_t)) return false;
    fin->layers     = (const spritelayer_t *)((const uint8_t *)data + off);
    fin->layer_count = (int)((size - off) / sizeof(*fin->layers));
    return true;
}

/* SPR cell lump: size == sizeof(dc_wad_cell_t) + header.width * header.height. */
static bool is_spr_cell_lump(int lump_idx) {
    if (lumpinfo[lump_idx].size < (int)sizeof(dc_wad_cell_t)) return false;
    if (!lumpinfo[lump_idx].data) return false;
    const dc_wad_cell_t *hdr = (const dc_wad_cell_t *)lumpinfo[lump_idx].data;
    return lumpinfo[lump_idx].size ==
           (int)sizeof(dc_wad_cell_t) + (int)hdr->width * (int)hdr->height;
}

/* Build a spritesheet from count consecutive WAD cell lumps starting at first. */
static bool init_lumps_from_wad(int first, int count, spritesheet_t *out) {
    memset(out, 0, sizeof(*out));
    if (count <= 0) return false;

    size_t total = 0;
    for (int i = 0; i < count; i++) {
        const dc_wad_cell_t *hdr = (const dc_wad_cell_t *)lumpinfo[first + i].data;
        if (hdr) total += (size_t)hdr->width * hdr->height;
    }

    out->pixel_data = calloc(total ? total : 1, 1);
    if (!out->pixel_data) return false;
    out->lumps = calloc((size_t)count, sizeof(*out->lumps));
    if (!out->lumps) { free(out->pixel_data); out->pixel_data = NULL; return false; }
    out->numlumps = count;

    size_t offset = 0;
    int max_w = 1, max_h = 1;
    for (int i = 0; i < count; i++) {
        const dc_wad_cell_t *hdr = (const dc_wad_cell_t *)lumpinfo[first + i].data;
        int w = hdr && hdr->width  ? hdr->width  : 1;
        int h = hdr && hdr->height ? hdr->height : 1;
        if (hdr && hdr->width && hdr->height) {
            size_t cell_size = (size_t)hdr->width * hdr->height;
            memcpy(out->pixel_data + offset, (const uint8_t *)(hdr + 1), cell_size);
            out->lumps[i].indices     = out->pixel_data + offset;
            out->lumps[i].bounds      = indexed_visible_bounds(out->lumps[i].indices, w, h);
            out->lumps[i].displacement = (ivec2_t){ hdr->dis_x, hdr->dis_y };
            offset += cell_size;
        } else {
            out->lumps[i].indices = out->pixel_data;
        }
        out->lumps[i].rect         = (irect_t){ 0, 0, w, h };
        out->lumps[i].ground_point = (ivec2_t){ w / 2, h };
        if (w > max_w) max_w = w;
        if (h > max_h) max_h = h;
    }
    out->frame_size = (isize2_t){ max_w, max_h };
    out->indexed = true;
    return true;
}

/* Load a single sprite from the WAD by path stem.  Call after DC_PopulateWAD. */
bool R_LoadWADSprite(const char *path, spritesheet_t *out) {
    char stem[9];
    path_stem(stem, path);
    char upper[WAD_NAME_SIZE] = {0};
    for (int k = 0; k < WAD_NAME_SIZE && stem[k]; k++)
        upper[k] = (char)toupper((unsigned char)stem[k]);

    int first = -1;
    for (int i = 0; i < numlumps; i++) {
        if (lumpinfo[i].ns == ns_sprites &&
            memcmp(lumpinfo[i].name, upper, WAD_NAME_SIZE) == 0 &&
            is_spr_cell_lump(i)) { first = i; break; }
    }
    if (first < 0) return false;

    int count = 0;
    while (first + count < numlumps &&
           memcmp(lumpinfo[first + count].name, lumpinfo[first].name, WAD_NAME_SIZE) == 0 &&
           lumpinfo[first + count].ns == ns_sprites &&
           is_spr_cell_lump(first + count))
        count++;

    if (!init_lumps_from_wad(first, count, out)) return false;

    char pal_name[WAD_NAME_SIZE + 1];
    snprintf(pal_name, sizeof(pal_name), "%sPAL", stem);
    int pal_lump = W_CheckNumForName(pal_name);
    if (pal_lump >= 0 && lumpinfo[pal_lump].size >= 768 && lumpinfo[pal_lump].data)
        convert_vga_palette(out->palette, (const uint8_t *)lumpinfo[pal_lump].data);

    char fin_name[WAD_NAME_SIZE + 1];
    snprintf(fin_name, sizeof(fin_name), "%sFIN", stem);
    int fin_lump = W_CheckNumForName(fin_name);
    dc_fin_t fin = {0};
    dc_fin_t *fin_ptr = NULL;
    if (fin_lump >= 0 && lumpinfo[fin_lump].data &&
        W_ParseFin(lumpinfo[fin_lump].data, (size_t)lumpinfo[fin_lump].size, &fin))
        fin_ptr = &fin;
    init_spritedef_from_wad(out, fin_ptr);

    if (render_tables_ready) {
        memcpy(out->palette, render_palette, sizeof(out->palette));
        out->indexed_blend_selector = 5;
        out->indexed_blend_table = render_blend;
    }
    return true;
}

/* Build the full sprite cache from WAD lumps.  Call after DC_PopulateWAD and
   load_render_tables so the render palette is ready. */
bool R_InitDCSprites(const char *root, spritecache_t *cache) {
    (void)root;
    memset(cache, 0, sizeof(*cache));

    for (int i = 0; i < numlumps && cache->count < MAX_DECORATION_SPRITES; i++) {
        if (lumpinfo[i].ns != ns_sprites) continue;
        if (!is_spr_cell_lump(i)) continue;

        /* Skip if already cached (subsequent cells of same sprite). */
        char stem[WAD_NAME_SIZE + 1];
        memset(stem, 0, sizeof(stem));
        for (int k = 0; k < WAD_NAME_SIZE; k++) stem[k] = lumpinfo[i].name[k];
        for (int k = WAD_NAME_SIZE - 1; k >= 0 && (stem[k] == '\0' || stem[k] == ' '); k--)
            stem[k] = '\0';
        if (R_CacheFind(cache, stem)) continue;

        /* Count consecutive same-name cell lumps in ns_sprites. */
        int count = 0;
        while (i + count < numlumps &&
               memcmp(lumpinfo[i + count].name, lumpinfo[i].name, WAD_NAME_SIZE) == 0 &&
               lumpinfo[i + count].ns == ns_sprites &&
               is_spr_cell_lump(i + count))
            count++;

        cachedsprite_t *entry = &cache->entries[cache->count];
        snprintf(entry->name, sizeof(entry->name), "%s", stem);

        if (!init_lumps_from_wad(i, count, &entry->sprite)) continue;

        /* VGA palette from companion PAL lump. */
        char pal_name[WAD_NAME_SIZE + 1];
        snprintf(pal_name, sizeof(pal_name), "%sPAL", stem);
        int pal_lump = W_CheckNumForName(pal_name);
        if (pal_lump >= 0 && lumpinfo[pal_lump].size >= 768 && lumpinfo[pal_lump].data)
            convert_vga_palette(entry->sprite.palette,
                                (const uint8_t *)lumpinfo[pal_lump].data);

        /* FIN binary stored by register_spr as {stem}FIN → spritedef. */
        char fin_name[WAD_NAME_SIZE + 1];
        snprintf(fin_name, sizeof(fin_name), "%sFIN", stem);
        int fin_lump = W_CheckNumForName(fin_name);
        dc_fin_t fin = {0};
        dc_fin_t *fin_ptr = NULL;
        if (fin_lump >= 0 && lumpinfo[fin_lump].data) {
            if (W_ParseFin(lumpinfo[fin_lump].data,
                           (size_t)lumpinfo[fin_lump].size, &fin))
                fin_ptr = &fin;
        }
        init_spritedef_from_wad(&entry->sprite, fin_ptr);

        /* Apply terrain-blend palette and table. */
        if (render_tables_ready) {
            memcpy(entry->sprite.palette, render_palette, sizeof(entry->sprite.palette));
            entry->sprite.indexed_blend_selector = 5;
            entry->sprite.indexed_blend_table    = render_blend;
        }

        cache->count++;
    }
    return cache->count > 0;
}

