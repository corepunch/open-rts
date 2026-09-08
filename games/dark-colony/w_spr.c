#define _DEFAULT_SOURCE
#include "w_spr.h"
#include "dc_facing.h"
#include "engine.h"
#include "info.h"

#include <ctype.h>
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

static bool init_lumps(SDL_Renderer *renderer, const dc_spr_t *spr, spritesheet_t *out) {
    int count = spr->header->cell_count, max_w = 1, max_h = 1;
    size_t total = 0;
    for (int i = 0; i < count; ++i) {
        if (spr->cells[i].width  > max_w) max_w = spr->cells[i].width;
        if (spr->cells[i].height > max_h) max_h = spr->cells[i].height;
        total += (size_t)spr->cells[i].width * spr->cells[i].height;
    }

    uint8_t *decoded = calloc(total ? total : 1, 1);
    if (!decoded || !decode_spr(spr, decoded, total)) { free(decoded); return false; }

    convert_vga_palette(out->palette, spr->palette);

    out->lumps = calloc((size_t)count, sizeof(*out->lumps));
    if (!out->lumps) { free(decoded); return false; }
    out->numlumps = count;

    size_t offset = 0;
    for (int i = 0; i < count; ++i) {
        int w = spr->cells[i].width  ? spr->cells[i].width  : 1;
        int h = spr->cells[i].height ? spr->cells[i].height : 1;
        size_t pixels = (size_t)w * h;

        out->lumps[i].indices = calloc(pixels, 1);
        if (!out->lumps[i].indices) goto fail;
        memcpy(out->lumps[i].indices, decoded + offset,
               (size_t)spr->cells[i].width * spr->cells[i].height);
        offset += (size_t)spr->cells[i].width * spr->cells[i].height;

        out->lumps[i].bounds       = indexed_visible_bounds(out->lumps[i].indices, w, h);
        out->lumps[i].displacement = (ivec2_t){ spr->cells[i].dis_x, spr->cells[i].dis_y };
        out->lumps[i].ground_point = (ivec2_t){ w / 2, h };

        uint32_t *rgba = malloc(pixels * sizeof(*rgba));
        if (!rgba) goto fail;
        V_IndexedToRGBA(rgba, out->lumps[i].indices, pixels, out->palette);
        bool ok = R_CreateSpriteLumpTexture(renderer, &out->lumps[i], rgba, w,
                                            (irect_t){ 0, 0, w, h }, true, -1);
        free(rgba);
        if (!ok) goto fail;
    }

    out->frame_size = (isize2_t){ max_w, max_h };
    out->indexed = true;
    free(decoded);
    return true;
fail:
    free(decoded);
    R_FreeSprite(out);
    return false;
}

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

static bool split_direction(const dc_fin_label_t *label, char base[17], int *direction) {
    char name[17];
    fixed_name(name, label->name, SPRITE_FRAME_NAME_SIZE);
    int end = (int)strlen(name), start = end;
    while (start > 0 && isdigit((unsigned char)name[start - 1])) start--;
    if (start == end) return false;
    *direction = atoi(name + start);
    memcpy(base, name, (size_t)start);
    base[start] = '\0';
    return *direction >= 0 && *direction < 16;
}

static const dc_fin_label_t *direction_label(const dc_fin_t *fin,
                                              const char *base, int direction) {
    char name[17];
    snprintf(name, sizeof(name), "%s%d", base, direction);
    return W_FinLabel(fin, name);
}

static bool sixteen_facing_block(const dc_fin_t *fin, int first) {
    if (first + 16 > fin->header->label_count) return false;
    int length = fin->labels[first].end - fin->labels[first].start;
    for (int i = 0; i < 16; ++i) {
        char base[17];
        int direction;
        if (!split_direction(&fin->labels[first + i], base, &direction) ||
            direction != (i ? 16 - i : 0) ||
            fin->labels[first + i].end - fin->labels[first + i].start != length)
            return false;
    }
    return true;
}

static bool has_sixteen_directions(const dc_fin_t *fin) {
    for (int i = 0; i < fin->header->label_count; ++i)
        if (sixteen_facing_block(fin, i)) return true;
    return false;
}

static bool install_fin_frame(spritesheet_t *sheet, int logical, int rotation,
                              const dc_fin_t *fin, int native, const char *stem) {
    int count = 0;
    const spritelayer_t *source = W_FinFrameLayers(fin, native, &count);
    if (!source || !count) return false;
    spritelayer_t *layers = calloc((size_t)count + 1, sizeof(*layers));
    if (!layers) return false;
    for (int i = 0; i < count; ++i) {
        layers[i] = source[i];
        char name[17];
        fixed_name(name, source[i].sprite, SPRITE_LAYER_NAME_SIZE);
        if (strcasecmp(name, stem) == 0) {
            memset(layers[i].sprite, 0, sizeof(layers[i].sprite));
            layers[i].sprite[0] = '.';
        }
    }
    spritedirection_t *direction =
        &sheet->spritedef.spriteframes[logical].directions[rotation];
    free(direction->layers);
    direction->layers = layers;
    direction->ticks = fin->frames[native].ticks;
    return true;
}

static bool init_definitions(spritesheet_t *sheet, const dc_fin_t *fin,
                             const char *stem) {
    int frame_count = fin && fin->header->frame_count > sheet->numlumps ?
        fin->header->frame_count : sheet->numlumps;
    int rotations = fin && has_sixteen_directions(fin) ? 16 : fin ? 8 : 1;
    if (!R_InitSpriteDef(sheet, frame_count, rotations,
                         fin ? dc_fin_direction_to_angle(0) : ANG270, true)) return false;
    for (int frame = 0; frame < sheet->numlumps; ++frame)
        for (int rotation = 0; rotation < rotations; ++rotation)
            if (!R_InstallSpriteLump(sheet, frame, rotation, frame, false)) return false;
    if (!fin) return true;
    for (int i = 0; i < fin->header->label_count; ++i) {
        char base[17];
        int direction;
        if (!split_direction(&fin->labels[i], base, &direction) || direction != 0) continue;
        const dc_fin_label_t *origin = &fin->labels[i];
        bool ordered = rotations == 16 && sixteen_facing_block(fin, i);
        for (int step = 0; step <= origin->end - origin->start; ++step) {
            int logical = origin->start + step;
            fixed_name(sheet->spritedef.spriteframes[logical].frame_name,
                       origin->name, SPRITE_FRAME_NAME_SIZE);
            for (int rotation = 0; rotation < rotations; ++rotation) {
                int native_direction = rotation * (16 / rotations);
                const dc_fin_label_t *label = direction_label(fin, base, native_direction);
                if (!label && ordered)
                    label = &fin->labels[i + (native_direction ? 16 - native_direction : 0)];
                if (!label || label->end < label->start) continue;
                int label_step = step;
                if (label_step > label->end - label->start)
                    label_step = label->end - label->start;
                if (!install_fin_frame(sheet, logical, rotation, fin,
                                       label->start + label_step, stem)) return false;
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

bool load_dark_colony_sprite(SDL_Renderer *renderer, const char *path,
                             spritesheet_t *out, uint32_t palette_out[256]) {
    memset(out, 0, sizeof(*out));
    char fin_path[1024], spr_path[1024], stem[9];
    dc_fin_t fin = {0};
    dc_spr_t spr = {0};
    paired_paths(path, fin_path, spr_path);
    if (fin_path[0]) W_LoadFin(fin_path, &fin);
    if (!load_spr(spr_path, &spr) || !init_lumps(renderer, &spr, out)) goto fail;
    path_stem(stem, spr_path);
    if (!init_definitions(out, fin.data ? &fin : NULL, stem)) goto fail;
    if (palette_out) memcpy(palette_out, out->palette, sizeof(out->palette));
    if (render_tables_ready) {
        memcpy(out->palette, render_palette, sizeof(out->palette));
        out->indexed_blend_selector = 5;
        out->indexed_blend_table = render_blend;
    }
    W_FreeFin(&fin);
    free(spr.data);
    return true;
fail:
    W_FreeFin(&fin);
    free(spr.data);
    R_FreeSprite(out);
    return false;
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

static bool file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

static bool resolve_sprite(char out[1024], const char *root, const char *name) {
    if (!name || !name[0]) return false;
    if (name[0] == '/') {
        snprintf(out, 1024, "%s", name);
        return file_exists(out);
    }
    if (strchr(name, '/')) {
        M_PathJoin(out, 1024, root, name);
        return file_exists(out);
    }
    static const char *directories[] = { "ANIMATE", "SPRITES", "INTRFACE", "CURSOR" };
    for (size_t i = 0; i < sizeof(directories) / sizeof(*directories); ++i) {
        char directory[1024], filename[32];
        M_PathJoin(directory, sizeof(directory), root, directories[i]);
        snprintf(filename, sizeof(filename), "%s.%s", name, i ? "SPR" : "FIN");
        M_PathJoin(out, 1024, directory, filename);
        if (file_exists(out)) return true;
    }
    return false;
}

static bool cache_sprite(spritecache_t *cache, SDL_Renderer *renderer,
                         const char *root, const char *name) {
    if (!name || !name[0] || R_CacheFind(cache, name)) return true;
    if (cache->count >= MAX_DECORATION_SPRITES) return false;
    char path[1024];
    if (!resolve_sprite(path, root, name)) return false;
    cachedsprite_t *entry = &cache->entries[cache->count];
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    if (!load_dark_colony_sprite(renderer, path, &entry->sprite, NULL)) return false;
    cache->count++;

    char fin_path[1024], spr_path[1024];
    dc_fin_t fin = {0};
    paired_paths(path, fin_path, spr_path);
    if (!fin_path[0] || !W_LoadFin(fin_path, &fin)) return true;
    bool ok = true;
    for (int i = 0; i < fin.header->dependency_count; ++i) {
        char dependency[17], relative[32];
        fixed_name(dependency, fin.dependencies[i].name, SPRITE_LAYER_NAME_SIZE);
        for (char *c = dependency; *c; ++c) *c = (char)toupper((unsigned char)*c);
        snprintf(relative, sizeof(relative), "SPRITES/%s.SPR", dependency);
        if (!cache_sprite(cache, renderer, root, relative)) ok = false;
    }
    W_FreeFin(&fin);
    return ok;
}

bool R_PrecacheLevel(SDL_Renderer *renderer, const char *root, const level_t *map,
                     const mobj_t *units, int unit_count, spritecache_t *cache) {
    bool ok = true;
    memset(cache, 0, sizeof(*cache));
    for (int i = 0; map && i < map->decoration_count; ++i) {
        ok &= cache_sprite(cache, renderer, root, map->decorations[i].sprite_name);
        ok &= cache_sprite(cache, renderer, root, map->decorations[i].sprite2_name);
        ok &= cache_sprite(cache, renderer, root, map->decorations[i].sprite3_name);
        ok &= cache_sprite(cache, renderer, root, map->decorations[i].shadow_name);
    }
    for (int i = 0; i < unit_count; ++i) {
        ok &= cache_sprite(cache, renderer, root, units[i].core.sprite_name);
        if (units[i].info) {
            ok &= cache_sprite(cache, renderer, root, units[i].info->shadow_name);
            ok &= cache_sprite(cache, renderer, root, units[i].info->hit_effect_name);
        }
    }
    static const char *interface_sprites[] = {
        "INTRFACE/DCSS.SPR", "INTRFACE/DCUT.SPR", "INTRFACE/MAINBUT.SPR",
        "INTRFACE/SHUMANE.SPR", "SPRITES/BEAC.SPR"
    };
    for (size_t i = 0; i < sizeof(interface_sprites) / sizeof(*interface_sprites); ++i)
        ok &= cache_sprite(cache, renderer, root, interface_sprites[i]);
    return ok;
}
