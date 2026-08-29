#define _DEFAULT_SOURCE
#include "plugin.h"
#include "info.h"
#include "gamestat.h"
#include "dc_types.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ── helpers shared with Dark Reign loader ─────────────────────────────── */

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
    Blob blob;
    if (!load_blob(path, &blob)) return NULL;
    char *text = malloc(blob.size + 1);
    if (!text) { free_blob(&blob); return NULL; }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';
    free_blob(&blob);
    return text;
}

/* ── palette ────────────────────────────────────────────────────────────── */

static void dark_colony_palette_from_spr(const uint8_t *spr, size_t size, uint32_t colors[256]) {
    if (size < 8 + 256 * 3) return;
    const uint8_t *p = spr + 8;
    for (int i = 0; i < 256; ++i) {
        int r = clamp255((int)p[i * 3 + 0] * 4 + 3);
        int g = clamp255((int)p[i * 3 + 1] * 4 + 3);
        int b = clamp255((int)p[i * 3 + 2] * 4 + 3);
        colors[i] = i == 0 ? 0x00000000u :
            (0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
    }
}

static uint8_t dark_colony_remap_palette_index(uint8_t index, int remap) {
    if (remap <= 0 || remap > 7 || index < 138 || index > 143) return index;
    int remapped = (int)index + (remap - 7) * 6;
    if (remapped < 0 || remapped > 255) return index;
    return (uint8_t)remapped;
}

static uint32_t dark_colony_sprite_pixel_rgba(uint8_t index, const uint32_t palette[256],
                                             int remap) {
    if (index == 0) return 0x00000000u;
    index = dark_colony_remap_palette_index(index, remap);
    return palette[index];
}

/* ── water animation helpers ────────────────────────────────────────────── */

enum { DARK_COLONY_WATER_WAVE_COUNT = 7 };

static bool dark_colony_palette_index_is_water(uint8_t index, const uint32_t palette[256]) {
    uint32_t color = palette[index];
    uint8_t r = (uint8_t)(color >> 16);
    uint8_t g = (uint8_t)(color >> 8);
    uint8_t b = (uint8_t)color;
    return index >= 201 && index <= 211 && r < 80 && g > 36 && b > 36 && g + b > r * 2;
}

static int dark_colony_palette_wave_score(uint8_t index, const uint32_t palette[256]) {
    uint32_t color = palette[index];
    uint8_t r = (uint8_t)(color >> 16);
    uint8_t g = (uint8_t)(color >> 8);
    uint8_t b = (uint8_t)color;
    return (int)g + (int)b - (int)r * 2;
}

static bool dark_colony_palette_index_is_wave(uint8_t index, const uint32_t palette[256]) {
    if (index < 201 || index > 207 || !dark_colony_palette_index_is_water(index, palette)) return false;
    int score = dark_colony_palette_wave_score(index, palette);
    int rank = 0;
    for (uint8_t other = 201; other <= 207; ++other) {
        if (!dark_colony_palette_index_is_water(other, palette)) continue;
        int other_score = dark_colony_palette_wave_score(other, palette);
        if (other_score > score || (other_score == score && other < index)) rank++;
    }
    return rank < DARK_COLONY_WATER_WAVE_COUNT;
}

static int dark_colony_palette_wave_count(const uint32_t palette[256]) {
    int count = 0;
    for (uint8_t index = 201; index <= 207; ++index) {
        if (dark_colony_palette_index_is_wave(index, palette)) count++;
    }
    return count;
}

static bool dark_colony_tile_has_water(const uint8_t *src, const uint32_t palette[256],
                                       size_t tile_bytes) {
    int water = 0, opaque = 0;
    for (size_t i = 0; i < tile_bytes; ++i) {
        uint8_t index = src[i];
        uint32_t color = palette[index];
        uint8_t r = (uint8_t)(color >> 16);
        uint8_t g = (uint8_t)(color >> 8);
        uint8_t b = (uint8_t)color;
        if (index == 0 || (r > 240 && g < 16 && b > 240)) continue;
        opaque++;
        if (dark_colony_palette_index_is_water(index, palette)) water++;
    }
    return water >= 96 && water * 4 >= opaque;
}

static uint8_t dark_colony_cycle_water_index(uint8_t index, const uint32_t palette[256], int phase) {
    if (!dark_colony_palette_index_is_wave(index, palette)) return index;
    uint8_t wave_indices[DARK_COLONY_WATER_WAVE_COUNT];
    int wave_count = 0, index_pos = -1;
    for (uint8_t other = 201; other <= 207; ++other) {
        if (!dark_colony_palette_index_is_wave(other, palette)) continue;
        if (wave_count < DARK_COLONY_WATER_WAVE_COUNT) {
            if (other == index) index_pos = wave_count;
            wave_indices[wave_count++] = other;
        }
    }
    if (index_pos < 0 || wave_count == 0) return index;
    return wave_indices[(index_pos + phase) % wave_count];
}

static void blit_dark_colony_tile_phase(uint32_t *dst, int dst_w, int dst_h, int dst_x, int dst_y,
                                        const uint8_t *src, int src_w, int src_h,
                                        const uint32_t palette[256], int phase) {
    for (int y = 0; y < src_h; ++y) {
        for (int x = 0; x < src_w; ++x) {
            uint8_t index = dark_colony_cycle_water_index(src[y * src_w + x], palette, phase);
            uint32_t color = palette[index];
            int dx = dst_x + x, dy = dst_y + y;
            if (dx >= 0 && dy >= 0 && dx < dst_w && dy < dst_h)
                dst[dy * dst_w + dx] = color;
        }
    }
}

/* ── tileset ────────────────────────────────────────────────────────────── */

bool load_dark_colony_tileset(SDL_Renderer *renderer, const char *path, Tileset *out) {
    memset(out, 0, sizeof(*out));
    Blob blob;
    if (!load_blob(path, &blob)) return false;
    const int tile_w = 32, tile_h = 32, palette_count = 256;
    const size_t tile_bytes = (size_t)tile_w * (size_t)tile_h;
    const size_t header_bytes = 8 + (size_t)palette_count * 3;
    if (blob.size < header_bytes) {
        fprintf(stderr, "%s is not a Dark Colony BTS terrain tile set\n", path);
        free_blob(&blob);
        return false;
    }
    int count = (int)read_u32_le(blob.bytes + 4);
    const size_t record_bytes = 4 + tile_bytes;
    if (count <= 0 || count > 4096 ||
        blob.size < header_bytes + (size_t)count * record_bytes) {
        fprintf(stderr, "%s has unsupported Dark Colony BTS tile records\n", path);
        free_blob(&blob);
        return false;
    }
    uint32_t palette[256];
    for (int i = 0; i < palette_count; ++i) {
        const uint8_t *p = blob.bytes + 8 + i * 3;
        int r = clamp255((int)p[0] * 4 + 3);
        int g = clamp255((int)p[1] * 4 + 3);
        int b = clamp255((int)p[2] * 4 + 3);
        bool transparent = (i == 0) || (r == 255 && g == 3 && b == 255);
        palette[i] = (transparent ? 0x00000000u : 0xff000000u) |
                     ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }

    int wave_phase_count = dark_colony_palette_wave_count(palette);
    if (wave_phase_count > MAX_TILE_ANIMATION_FRAMES) wave_phase_count = MAX_TILE_ANIMATION_FRAMES;
    int extra_phase_count = wave_phase_count > 1 ? wave_phase_count - 1 : 0;

    int max_key = 0, animated_count = 0;
    uint8_t *animate_tile = calloc((size_t)count, sizeof(uint8_t));
    uint32_t *record_keys = calloc((size_t)count, sizeof(uint32_t));
    if (!animate_tile || !record_keys) {
        free(animate_tile); free(record_keys); free_blob(&blob);
        return false;
    }
    for (int tile = 0; tile < count; ++tile) {
        const uint8_t *record = blob.bytes + header_bytes + (size_t)tile * record_bytes;
        uint32_t key = read_u32_le(record);
        record_keys[tile] = key;
        if (key <= UINT16_MAX && (int)key > max_key) max_key = (int)key;
        if (extra_phase_count > 0 && dark_colony_tile_has_water(record + 4, palette, tile_bytes)) {
            animate_tile[tile] = 1;
            animated_count++;
        }
    }
    int total_tiles = count + animated_count * extra_phase_count;
    int synthetic_key_base = max_key + 1;
    int lookup_count = synthetic_key_base + animated_count * extra_phase_count;
    int rows = (total_tiles + TILE_ATLAS_COLS - 1) / TILE_ATLAS_COLS;
    int atlas_w = TILE_ATLAS_COLS * tile_w;
    int atlas_h = rows * tile_h;
    int *tile_lookup = calloc((size_t)lookup_count, sizeof(int));
    uint32_t *rgba = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    if (!rgba || !tile_lookup) {
        free(tile_lookup); free(rgba);
        free(animate_tile); free(record_keys); free_blob(&blob);
        return false;
    }
    for (int i = 0; i < lookup_count; ++i) tile_lookup[i] = -1;
    /* Also map sequential indices 0..count-1 so MTG-format maps work.
       MTG stores 1-byte record indices; BTS keys (302+) are far above this range
       and will overwrite any collision when populated below. */
    for (int i = 0; i < count && i < lookup_count; ++i) tile_lookup[i] = i;
    int extra_tile = count, extra_key = synthetic_key_base;
    for (int tile = 0; tile < count; ++tile) {
        int tx = (tile % TILE_ATLAS_COLS) * tile_w;
        int ty = (tile / TILE_ATLAS_COLS) * tile_h;
        const uint8_t *record = blob.bytes + header_bytes + (size_t)tile * record_bytes;
        uint32_t key = record_keys[tile];
        if (key <= (uint32_t)max_key) tile_lookup[key] = tile;
        const uint8_t *src = record + 4;
        blit_indexed_to_rgba(rgba, atlas_w, atlas_h, tx, ty, src, tile_w, tile_h, palette);
        if (animate_tile[tile]) {
            int frames[MAX_TILE_ANIMATION_FRAMES] = { (int)key };
            for (int phase = 1; phase < wave_phase_count; ++phase) {
                int anim_tile = extra_tile++;
                int ax = (anim_tile % TILE_ATLAS_COLS) * tile_w;
                int ay = (anim_tile / TILE_ATLAS_COLS) * tile_h;
                int phase_key = extra_key + phase - 1;
                frames[phase] = phase_key;
                tile_lookup[phase_key] = anim_tile;
                blit_dark_colony_tile_phase(rgba, atlas_w, atlas_h, ax, ay,
                                            src, tile_w, tile_h, palette, phase);
            }
            tileset_add_animation(out, (int)key, frames, wave_phase_count, 180);
            extra_key += extra_phase_count;
        }
    }
    out->texture = rgba_texture(renderer, rgba, atlas_w, atlas_h, true);
    out->tile_lookup = tile_lookup;
    out->tile_lookup_count = lookup_count;
    out->count = total_tiles;
    out->atlas_cols = TILE_ATLAS_COLS;
    out->tile_w = tile_w;
    out->tile_h = tile_h;
    out->draw_y_offset = 0;
    free(rgba); free(animate_tile); free(record_keys); free_blob(&blob);
    if (!out->texture) { destroy_tileset(out); return false; }
    return true;
}

/* ── SPR loader ─────────────────────────────────────────────────────────── */

static SDL_Rect dc_visible_bounds(const uint32_t *rgba, int atlas_w, SDL_Rect frame) {
    int min_x = frame.w, min_y = frame.h, max_x = -1, max_y = -1;
    for (int y = 0; y < frame.h; ++y) {
        for (int x = 0; x < frame.w; ++x) {
            uint32_t px = rgba[(frame.y + y) * atlas_w + frame.x + x];
            if ((px >> 24) == 0) continue;
            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x > max_x) max_x = x;
            if (y > max_y) max_y = y;
        }
    }
    if (max_x < min_x || max_y < min_y) return (SDL_Rect){ 0, 0, frame.w, frame.h };
    return (SDL_Rect){ min_x, min_y, max_x - min_x + 1, max_y - min_y + 1 };
}

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t dis_x;
    uint16_t dis_y;
    uint8_t used;
    uint32_t data_size;
    uint8_t *data;
} DarkColonyJuiceCell;

typedef struct {
    uint16_t flags;
    uint16_t cell_count;
    uint32_t payload_bytes;
    bool chunked;
    uint32_t palette[256];
    DarkColonyJuiceCell *cells;
} DarkColonyJuiceFile;

typedef struct {
    char name[9];
} DarkColonyAnimationDependency;

typedef struct {
    char name[17];
    uint16_t start;
    uint16_t end;
} DarkColonyAnimationLabel;

typedef struct {
    char sprite[9];
    int16_t frame;
    int16_t x;
    int16_t y;
    int16_t remap;
    int16_t intensity;
    int16_t layer;
    int16_t flags;
} DarkColonyAnimationCommand;

typedef struct {
    uint16_t frame_count;
    uint16_t aux_count;
    uint16_t label_count;
    uint16_t dependency_count;
    DarkColonyAnimationDependency *dependencies;
    DarkColonyAnimationLabel *labels;
    uint8_t *aux_records;
    DarkColonyAnimationCommand *commands;
    int command_count;
} DarkColonyAnimationFile;

typedef struct {
    DarkColonyJuiceFile juice;
    DarkColonyAnimationFile animation;
    bool has_animation;
} DarkColonySpriteNative;

static int16_t read_i16_le_dc(const uint8_t *p) {
    return (int16_t)read_u16_le(p);
}

static void copy_padded_ascii(char *dst, size_t dst_size, const uint8_t *src, size_t src_size) {
    size_t n = 0;
    while (n + 1 < dst_size && n < src_size && src[n] != '\0') {
        dst[n] = (char)src[n];
        n++;
    }
    dst[n] = '\0';
}

static void dark_colony_juice_destroy(DarkColonyJuiceFile *juice) {
    if (!juice) return;
    for (int i = 0; i < juice->cell_count; ++i) free(juice->cells[i].data);
    free(juice->cells);
    memset(juice, 0, sizeof(*juice));
}

static bool dark_colony_juice_load(const char *path, DarkColonyJuiceFile *out) {
    memset(out, 0, sizeof(*out));
    Blob blob;
    if (!load_blob(path, &blob)) return false;
    if (blob.size < 8 + 256 * 3) { free_blob(&blob); return false; }

    out->flags = read_u16_le(blob.bytes + 0);
    out->cell_count = read_u16_le(blob.bytes + 2);
    out->payload_bytes = read_u32_le(blob.bytes + 4);
    out->chunked = (out->flags & 0x180) != 0;
    if (out->cell_count == 0 || out->cell_count > 1024) {
        free_blob(&blob);
        return false;
    }

    dark_colony_palette_from_spr(blob.bytes, blob.size, out->palette);
    size_t desc_off = 8 + 256 * 3;
    size_t data_off = desc_off + (size_t)out->cell_count * 8;
    if (data_off > blob.size) {
        free_blob(&blob);
        return false;
    }

    out->cells = calloc(out->cell_count, sizeof(*out->cells));
    if (!out->cells) {
        free_blob(&blob);
        return false;
    }
    for (int i = 0; i < out->cell_count; ++i) {
        const uint8_t *desc = blob.bytes + desc_off + (size_t)i * 8;
        DarkColonyJuiceCell *cell = &out->cells[i];
        cell->width = read_u16_le(desc + 0);
        cell->height = read_u16_le(desc + 2);
        cell->dis_x = read_u16_le(desc + 4);
        cell->dis_y = read_u16_le(desc + 6);
        cell->used = cell->width > 0 && cell->height > 0;
        if (cell->width > 512 || cell->height > 512) {
            dark_colony_juice_destroy(out);
            free_blob(&blob);
            return false;
        }
    }

    size_t src_pos = data_off;
    if (out->chunked) {
        uint32_t total_chunks = 0;
        for (int i = 0; i < out->cell_count; ++i) {
            if (src_pos + 4 > blob.size) {
                dark_colony_juice_destroy(out);
                free_blob(&blob);
                return false;
            }
            uint32_t chunk_len = read_u32_le(blob.bytes + src_pos);
            src_pos += 4;
            total_chunks += chunk_len;
            if (total_chunks > out->payload_bytes || src_pos + chunk_len > blob.size) {
                dark_colony_juice_destroy(out);
                free_blob(&blob);
                return false;
            }
            DarkColonyJuiceCell *cell = &out->cells[i];
            cell->data_size = chunk_len;
            cell->data = malloc(chunk_len > 0 ? chunk_len : 1);
            if (!cell->data) {
                dark_colony_juice_destroy(out);
                free_blob(&blob);
                return false;
            }
            if (chunk_len > 0) memcpy(cell->data, blob.bytes + src_pos, chunk_len);
            src_pos += chunk_len;
        }
    } else {
        for (int i = 0; i < out->cell_count; ++i) {
            DarkColonyJuiceCell *cell = &out->cells[i];
            uint32_t pixel_count = (uint32_t)cell->width * (uint32_t)cell->height;
            cell->data_size = pixel_count;
            if (pixel_count == 0) continue;
            if (src_pos + pixel_count > blob.size) {
                dark_colony_juice_destroy(out);
                free_blob(&blob);
                return false;
            }
            cell->data = malloc(pixel_count);
            if (!cell->data) {
                dark_colony_juice_destroy(out);
                free_blob(&blob);
                return false;
            }
            memcpy(cell->data, blob.bytes + src_pos, pixel_count);
            src_pos += pixel_count;
        }
    }
    free_blob(&blob);
    return true;
}

static void dark_colony_animation_destroy(DarkColonyAnimationFile *animation) {
    if (!animation) return;
    free(animation->dependencies);
    free(animation->labels);
    free(animation->aux_records);
    free(animation->commands);
    memset(animation, 0, sizeof(*animation));
}

static bool dark_colony_animation_load(const char *path, DarkColonyAnimationFile *out) {
    memset(out, 0, sizeof(*out));
    Blob blob;
    if (!load_blob(path, &blob)) return false;
    if (blob.size < 8) { free_blob(&blob); return false; }

    out->frame_count = read_u16_le(blob.bytes + 0);
    out->aux_count = read_u16_le(blob.bytes + 2);
    out->label_count = read_u16_le(blob.bytes + 4);
    out->dependency_count = read_u16_le(blob.bytes + 6);
    if (out->label_count > 1024 || out->dependency_count > 1024 || out->aux_count > 4096) {
        free_blob(&blob);
        return false;
    }

    size_t dependency_off = 8;
    size_t label_off = dependency_off + (size_t)out->dependency_count * 8;
    size_t aux_off = label_off + (size_t)out->label_count * 20;
    size_t command_off = aux_off + (size_t)out->aux_count * 164;
    if (command_off > blob.size || ((blob.size - command_off) % 22) != 0) {
        free_blob(&blob);
        return false;
    }

    out->dependencies = calloc(out->dependency_count ? out->dependency_count : 1,
                               sizeof(*out->dependencies));
    out->labels = calloc(out->label_count ? out->label_count : 1, sizeof(*out->labels));
    out->aux_records = malloc((size_t)out->aux_count * 164 > 0 ?
                              (size_t)out->aux_count * 164 : 1);
    out->command_count = (int)((blob.size - command_off) / 22);
    out->commands = calloc(out->command_count ? out->command_count : 1, sizeof(*out->commands));
    if (!out->dependencies || !out->labels || !out->aux_records || !out->commands) {
        dark_colony_animation_destroy(out);
        free_blob(&blob);
        return false;
    }

    for (int i = 0; i < out->dependency_count; ++i) {
        copy_padded_ascii(out->dependencies[i].name, sizeof(out->dependencies[i].name),
                          blob.bytes + dependency_off + (size_t)i * 8, 8);
    }
    for (int i = 0; i < out->label_count; ++i) {
        size_t off = label_off + (size_t)i * 20;
        copy_padded_ascii(out->labels[i].name, sizeof(out->labels[i].name),
                          blob.bytes + off, 16);
        out->labels[i].start = read_u16_le(blob.bytes + off + 16);
        out->labels[i].end = read_u16_le(blob.bytes + off + 18);
    }
    if (out->aux_count > 0) {
        memcpy(out->aux_records, blob.bytes + aux_off, (size_t)out->aux_count * 164);
    }
    for (int i = 0; i < out->command_count; ++i) {
        size_t off = command_off + (size_t)i * 22;
        DarkColonyAnimationCommand *cmd = &out->commands[i];
        copy_padded_ascii(cmd->sprite, sizeof(cmd->sprite), blob.bytes + off, 8);
        cmd->frame = read_i16_le_dc(blob.bytes + off + 8);
        cmd->x = read_i16_le_dc(blob.bytes + off + 10);
        cmd->y = read_i16_le_dc(blob.bytes + off + 12);
        cmd->remap = read_i16_le_dc(blob.bytes + off + 14);
        cmd->intensity = read_i16_le_dc(blob.bytes + off + 16);
        cmd->layer = read_i16_le_dc(blob.bytes + off + 18);
        cmd->flags = read_i16_le_dc(blob.bytes + off + 20);
    }
    free_blob(&blob);
    return true;
}

static void dark_colony_sprite_native_destroy(void *ptr) {
    DarkColonySpriteNative *native = ptr;
    if (!native) return;
    dark_colony_juice_destroy(&native->juice);
    dark_colony_animation_destroy(&native->animation);
    free(native);
}

static bool dark_colony_animation_path_for_sprite(char *out, size_t out_size,
                                                  const char *sprite_path) {
    if (!out || out_size == 0 || !sprite_path) return false;
    const char *base = strrchr(sprite_path, '/');
    base = base ? base + 1 : sprite_path;
    const char *dot = strrchr(base, '.');
    if (!dot || strcasecmp(dot, ".SPR") != 0) return false;

    const char *dir_end = base > sprite_path ? base - 1 : NULL;
    const char *dir_start = dir_end;
    while (dir_start && dir_start > sprite_path && dir_start[-1] != '/') dir_start--;
    size_t dir_len = dir_start ? (size_t)(dir_end - dir_start) : 0;
    if (!dir_start || dir_len != strlen("SPRITES") ||
        strncasecmp(dir_start, "SPRITES", dir_len) != 0) {
        return false;
    }

    size_t prefix_len = (size_t)(dir_start - sprite_path);
    size_t stem_len = (size_t)(dot - base);
    if (prefix_len + strlen("ANIMATE/") + stem_len + strlen(".FIN") + 1 > out_size)
        return false;
    memcpy(out, sprite_path, prefix_len);
    out[prefix_len] = '\0';
    strncat(out, "ANIMATE/", out_size - strlen(out) - 1);
    strncat(out, base, stem_len);
    strncat(out, ".FIN", out_size - strlen(out) - 1);
    return true;
}

static bool dark_colony_dependency_sprite_name(char *out, size_t out_size,
                                               const char *dependency) {
    if (!out || out_size == 0 || !dependency) return false;
    char stem[16];
    size_t len = 0;
    while (dependency[len] != '\0' && len < 8 &&
           !isspace((unsigned char)dependency[len])) {
        unsigned char ch = (unsigned char)dependency[len];
        if (!isalnum(ch) && ch != '_') return false;
        stem[len++] = (char)toupper(ch);
    }
    if (len == 0) return false;
    stem[len] = '\0';
    return snprintf(out, out_size, "SPRITES/%s.SPR", stem) < (int)out_size;
}

static bool dark_colony_file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

bool load_dark_colony_sprite(SDL_Renderer *renderer, const char *path, SpriteSheet *out,
                             uint32_t palette_out[256]) {
    memset(out, 0, sizeof(*out));

    DarkColonySpriteNative *native = calloc(1, sizeof(*native));
    if (!native) return false;
    if (!dark_colony_juice_load(path, &native->juice)) {
        fprintf(stderr, "%s is not a supported Dark Colony raw SPR\n", path);
        dark_colony_sprite_native_destroy(native);
        return false;
    }
    if (palette_out) memcpy(palette_out, native->juice.palette, sizeof(native->juice.palette));
    const uint32_t *palette = native->juice.palette;

    char animation_path[1024];
    if (dark_colony_animation_path_for_sprite(animation_path, sizeof(animation_path), path) &&
        dark_colony_file_exists(animation_path) &&
        dark_colony_animation_load(animation_path, &native->animation)) {
        native->has_animation = true;
    }

    DarkColonyJuiceFile *juice = &native->juice;
    int visible_frames = juice->cell_count;
    int max_w = 1, max_h = 1;
    int canvas_w = 1, canvas_h = 1;
    for (int i = 0; i < visible_frames; ++i) {
        const DarkColonyJuiceCell *cell = &juice->cells[i];
        int w = cell->width > 0 ? cell->width : 1;
        int h = cell->height > 0 ? cell->height : 1;
        if (w > max_w) max_w = w;
        if (h > max_h) max_h = h;
        if (cell->dis_x + w > canvas_w) canvas_w = cell->dis_x + w;
        if (cell->dis_y + h > canvas_h) canvas_h = cell->dis_y + h;
    }

    int cols = (int)ceilf(sqrtf((float)visible_frames));
    int rows = (visible_frames + cols - 1) / cols;
    int atlas_w = cols * max_w;
    int atlas_h = rows * max_h;
    uint32_t *rgba = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    uint8_t *indices = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint8_t));
    SDL_Rect *frames = calloc((size_t)visible_frames, sizeof(SDL_Rect));
    SDL_Rect *bounds = calloc((size_t)visible_frames, sizeof(SDL_Rect));
    SDL_Point *displacements = calloc((size_t)visible_frames, sizeof(SDL_Point));
    if (!rgba || !indices || !frames || !bounds || !displacements) {
        free(rgba); free(indices); free(frames); free(bounds); free(displacements);
        dark_colony_sprite_native_destroy(native);
        return false;
    }

    bool has_team_colors = false;
    for (int i = 0; i < visible_frames; ++i) {
        const DarkColonyJuiceCell *cell = &juice->cells[i];
        int w = cell->width > 0 ? cell->width : 1;
        int h = cell->height > 0 ? cell->height : 1;
        bool blank = cell->width == 0 || cell->height == 0;
        int fx = (i % cols) * max_w;
        int fy = (i / cols) * max_h;
        displacements[i] = (SDL_Point){ cell->dis_x, cell->dis_y };
        if (juice->chunked) {
            uint32_t chunk_size = cell->data_size;
            if (blank) {
                frames[i] = (SDL_Rect){ fx, fy, w, h };
                bounds[i] = (SDL_Rect){ 0, 0, w, h };
                continue;
            }
            const uint8_t *src = cell->data;
            size_t pos = 0;
            int write = 0;
            int pixel_count = w * h;
            while (pos < chunk_size && write < pixel_count) {
                int8_t cmd = (int8_t)src[pos++];
                if (cmd < 0) {
                    write += -cmd;
                } else {
                    int count = cmd + 1;
                    if (pos + (size_t)count > chunk_size) break;
                    for (int p = 0; p < count; ++p) {
                        if (write >= 0 && write < pixel_count) {
                            int dst_x = fx + (write % w), dst_y = fy + (write / w);
                            if (dst_x >= 0 && dst_x < atlas_w && dst_y >= 0 && dst_y < atlas_h) {
                                size_t dst = (size_t)dst_y * (size_t)atlas_w + (size_t)dst_x;
                                uint8_t index = src[pos + (size_t)p];
                                if (index >= 138 && index <= 143) has_team_colors = true;
                                indices[dst] = index;
                                rgba[dst] = dark_colony_sprite_pixel_rgba(index, palette, 0);
                            }
                        }
                        write++;
                    }
                    pos += (size_t)count;
                }
            }
        } else {
            if (!blank) {
                const uint8_t *src = cell->data;
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        int dst_x = fx + x;
                        int dst_y = fy + y;
                        if (dst_x >= 0 && dst_x < atlas_w && dst_y >= 0 && dst_y < atlas_h) {
                            size_t dst = (size_t)dst_y * (size_t)atlas_w + (size_t)dst_x;
                            uint8_t index = src[y * w + x];
                            if (index >= 138 && index <= 143) has_team_colors = true;
                            indices[dst] = index;
                            rgba[dst] = dark_colony_sprite_pixel_rgba(index, palette, 0);
                        }
                    }
                }
            }
        }
        frames[i] = (SDL_Rect){ fx, fy, w, h };
        bounds[i] = dc_visible_bounds(rgba, atlas_w, frames[i]);
    }

    SDL_Texture *texture = rgba_texture(renderer, rgba, atlas_w, atlas_h, true);
    if (!texture) {
        free(rgba);
        free(indices);
        free(frames);
        free(bounds);
        free(displacements);
        dark_colony_sprite_native_destroy(native);
        return false;
    }

    SDL_Texture *remap_textures[8] = {0};
    uint32_t *remap_rgba = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    if (!remap_rgba) {
        SDL_DestroyTexture(texture);
        free(rgba);
        free(indices);
        free(frames);
        free(bounds);
        free(displacements);
        dark_colony_sprite_native_destroy(native);
        return false;
    }
    for (int remap = 1; has_team_colors && remap < 8; ++remap) {
        for (int y = 0; y < atlas_h; ++y) {
            int row = max_h > 0 ? y / max_h : 0;
            for (int x = 0; x < atlas_w; ++x) {
                int col = max_w > 0 ? x / max_w : 0;
                int frame = row * cols + col;
                size_t pos = (size_t)y * (size_t)atlas_w + (size_t)x;
                remap_rgba[pos] = frame >= 0 && frame < visible_frames ?
                    dark_colony_sprite_pixel_rgba(indices[pos], palette, remap) :
                    0x00000000u;
            }
        }
        remap_textures[remap] = rgba_texture(renderer, remap_rgba, atlas_w, atlas_h, true);
        if (!remap_textures[remap]) {
            for (int i = 1; i < remap; ++i)
                if (remap_textures[i]) SDL_DestroyTexture(remap_textures[i]);
            SDL_DestroyTexture(texture);
            free(remap_rgba);
            free(rgba);
            free(indices);
            free(frames);
            free(bounds);
            free(displacements);
            dark_colony_sprite_native_destroy(native);
            return false;
        }
    }
    free(remap_rgba);

    out->texture = texture;
    for (int remap = 1; remap < 8; ++remap)
        out->remap_textures[remap] = remap_textures[remap];
    out->frames = frames;
    out->frame_bounds = bounds;
    out->frame_ground_points = NULL;
    out->frame_displacements = displacements;
    out->frame_count = visible_frames;
    out->frame_w = canvas_w;
    out->frame_h = canvas_h;
    out->rotations = 1;
    out->primary_frames_per_rotation = visible_frames;
    out->native_data = native;
    out->destroy_native_data = dark_colony_sprite_native_destroy;

    free(rgba);
    free(indices);
    return true;
}

/* ── sprite cache helper ────────────────────────────────────────────────── */

static bool sprite_cache_load_dark_colony(SpriteCache *cache, SDL_Renderer *renderer,
                                          const char *data_root, const char *name) {
    if (!name || name[0] == '\0') return true;
    if (sprite_cache_find(cache, name)) return true;
    if (cache->count >= MAX_DECORATION_SPRITES) {
        fprintf(stderr, "too many Dark Colony sprites; skipped %s\n", name);
        return false;
    }
    char sprite_path[1024];
    if (name[0] == '/') {
        snprintf(sprite_path, sizeof(sprite_path), "%s", name);
    } else {
        path_join(sprite_path, sizeof(sprite_path), data_root, name);
    }
    CachedSprite *entry = &cache->entries[cache->count];
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    uint32_t palette[256] = { 0 };
    if (!load_dark_colony_sprite(renderer, sprite_path, &entry->sprite, palette)) {
        fprintf(stderr, "failed to load %s\n", sprite_path);
        memset(entry, 0, sizeof(*entry));
        return false;
    }
    cache->count++;
    DarkColonySpriteNative *native = entry->sprite.native_data;
    if (native && native->has_animation) {
        for (int i = 0; i < native->animation.dependency_count; ++i) {
            char dependency_name[64];
            if (!dark_colony_dependency_sprite_name(dependency_name, sizeof(dependency_name),
                                                    native->animation.dependencies[i].name)) {
                continue;
            }
            if (sprite_cache_find(cache, dependency_name)) continue;
            char dependency_path[1024];
            path_join(dependency_path, sizeof(dependency_path), data_root, dependency_name);
            if (!dark_colony_file_exists(dependency_path)) continue;
            if (!sprite_cache_load_dark_colony(cache, renderer, data_root, dependency_name))
                return false;
        }
    }
    return true;
}

/* ── public entry points ────────────────────────────────────────────────── */

bool load_dark_colony_unit_sprites(SDL_Renderer *renderer, const char *data_root,
                                   const GameMap *map, const Unit *units, int unit_count,
                                   SpriteCache *cache) {
    bool ok = true;
    static const char *const ui_sprites[] = {
        "INTRFACE/DCSS.SPR",
        "INTRFACE/DCUT.SPR",
        "INTRFACE/MAINBUT.SPR",
        "INTRFACE/SHUMANE.SPR",
        "SPRITES/DROP.SPR",
        "SPRITES/BEAC.SPR",
        "SPRITES/MUZA.SPR",
        "SPRITES/BLOO.SPR",
    };
    for (int i = 0; i < NUMSTATES; ++i) {
        int sprite = states[i].sprite;
        if (sprite >= 0 && sprite < NUMSPRITES &&
            !sprite_cache_load_dark_colony(cache, renderer, data_root, sprnames[sprite])) {
            ok = false;
        }
    }
    for (size_t i = 0; i < sizeof(ui_sprites) / sizeof(ui_sprites[0]); ++i) {
        if (!sprite_cache_load_dark_colony(cache, renderer, data_root, ui_sprites[i]))
            ok = false;
    }
    int selection_sprite = dark_colony_game_info.selection_marker.sprite;
    if (selection_sprite >= 0 && selection_sprite < NUMSPRITES &&
        !sprite_cache_load_dark_colony(cache, renderer, data_root, sprnames[selection_sprite])) {
        ok = false;
    }
    if (map) {
        for (int i = 0; i < map->decoration_count; ++i) {
            if (!sprite_cache_load_dark_colony(cache, renderer, data_root, map->decorations[i].sprite_name))
                ok = false;
            if (!sprite_cache_load_dark_colony(cache, renderer, data_root, map->decorations[i].sprite2_name))
                ok = false;
            if (!sprite_cache_load_dark_colony(cache, renderer, data_root, map->decorations[i].shadow_name))
                ok = false;
        }
    }
    for (int i = 0; i < unit_count; ++i) {
        if (!sprite_cache_load_dark_colony(cache, renderer, data_root, units[i].sprite_name))
            ok = false;
        if (!sprite_cache_load_dark_colony(cache, renderer, data_root, units[i].shadow_name))
            ok = false;
        if (!sprite_cache_load_dark_colony(cache, renderer, data_root, units[i].muzzle_flash_name))
            ok = false;
        if (!sprite_cache_load_dark_colony(cache, renderer, data_root, units[i].hit_effect_name))
            ok = false;
    }
    return ok;
}

/* ── map loader ─────────────────────────────────────────────────────────── */

enum {
    DARK_COLONY_SCN_MAX_TEAMS = 16,
    DARK_COLONY_SCN_AI_SLOTS = 2,
    DARK_COLONY_SCN_CITY_SLOTS = 7,
    DARK_COLONY_SCN_CITY_VALUES = 12,
    DARK_COLONY_SCN_CITY_ROWS = 9,
    DARK_COLONY_SCN_CITY_ROW_VALUES = 12,
    DARK_COLONY_SCN_LIST_VALUES = 32,
};

typedef enum {
    DARK_COLONY_MAP_FILE_NONE,
    DARK_COLONY_MAP_FILE_MAP,
    DARK_COLONY_MAP_FILE_MTG,
} DarkColonyMapFileKind;

typedef struct {
    uint16_t background;
    uint16_t foreground;
    uint16_t flags;
    uint8_t mtg;
} DarkColonyMapCell;

typedef struct {
    DarkColonyMapFileKind kind;
    char path[1024];
    int width;
    int height;
    DarkColonyMapCell *cells;
    uint32_t *overview_colors;
    uint16_t *overview_words;
} DarkColonyMapFile;

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
} DarkColonyScenarioTeam;

typedef struct {
    int x;
    int y;
    int type;
    int team;
    int status;
    int extra;
    int value_count;
} DarkColonyScenarioObject;

typedef struct {
    char path[1024];
    char tileset_file[32];
    char scenario_id[32];
    char display_name[64];
    int header_values[8];
    int header_value_count;
    DarkColonyScenarioTeam teams[DARK_COLONY_SCN_MAX_TEAMS];
    int team_count;
    DarkColonyScenarioObject *objects;
    int object_count;
} DarkColonyScenarioFile;

typedef struct {
    DarkColonyMapFile map;
    DarkColonyScenarioFile scenario;
    bool has_scenario;
} DarkColonyMapNative;

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

static void dark_colony_scenario_destroy(DarkColonyScenarioFile *scenario) {
    if (!scenario) return;
    free(scenario->objects);
    memset(scenario, 0, sizeof(*scenario));
}

static void dark_colony_map_file_destroy(DarkColonyMapFile *map) {
    if (!map) return;
    free(map->cells);
    free(map->overview_colors);
    free(map->overview_words);
    memset(map, 0, sizeof(*map));
}

static void dark_colony_map_native_destroy(void *ptr) {
    DarkColonyMapNative *native = ptr;
    if (!native) return;
    dark_colony_map_file_destroy(&native->map);
    dark_colony_scenario_destroy(&native->scenario);
    free(native);
}

static bool dark_colony_scenario_append_object(DarkColonyScenarioFile *scenario,
                                               const DarkColonyScenarioObject *object) {
    DarkColonyScenarioObject *objects = realloc(
        scenario->objects, (size_t)(scenario->object_count + 1) * sizeof(*objects));
    if (!objects) return false;
    scenario->objects = objects;
    scenario->objects[scenario->object_count++] = *object;
    return true;
}

static bool dark_colony_scenario_load(const char *path, DarkColonyScenarioFile *out) {
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
            DarkColonyScenarioTeam *team_info = &out->teams[current_team];
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
            DarkColonyScenarioObject object = {
                .x = values[0],
                .y = values[1],
                .type = values[2],
                .team = values[3],
                .status = values[4],
                .extra = parsed >= 6 ? values[5] : 0,
                .value_count = parsed,
            };
            if (!dark_colony_scenario_append_object(out, &object)) {
                dark_colony_scenario_destroy(out);
                free(text);
                return false;
            }
        }
        line = next;
    }

    free(text);
    return true;
}

static bool dark_colony_map_file_load(const char *path, DarkColonyMapFile *out) {
    memset(out, 0, sizeof(*out));
    Blob blob;
    char path_buf[1024];
    if (!load_blob(path, &blob)) {
        const char *dot = strrchr(path, '.');
        if (dot && strcasecmp(dot, ".MAP") == 0) {
            replace_extension(path_buf, sizeof(path_buf), path, ".MTG");
            if (!load_blob(path_buf, &blob)) return false;
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
                free_blob(&blob);
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
                free_blob(&blob);
                return false;
            }
            for (size_t i = 0; i < cell_count; ++i)
                out->cells[i].mtg = blob.bytes[2 + i];
            ok = true;
        }
    }
    free_blob(&blob);
    if (!ok) dark_colony_map_file_destroy(out);
    return ok;
}

static bool dark_colony_map_file_load_mtg_sidecar(DarkColonyMapFile *map) {
    if (!map || map->kind != DARK_COLONY_MAP_FILE_MAP || !map->cells) return false;
    char mtg_path[1024];
    replace_extension(mtg_path, sizeof(mtg_path), map->path, ".MTG");
    Blob blob;
    if (!load_blob(mtg_path, &blob)) return false;
    size_t cell_count = (size_t)map->width * (size_t)map->height;
    if (blob.size < 2 + cell_count || blob.bytes[0] != map->width ||
        blob.bytes[1] != map->height) {
        free_blob(&blob);
        return false;
    }
    for (size_t i = 0; i < cell_count; ++i)
        map->cells[i].mtg = blob.bytes[2 + i];
    free_blob(&blob);
    return true;
}

static bool dark_colony_map_file_load_overview(DarkColonyMapFile *map) {
    if (!map || map->width <= 0 || map->height <= 0) return false;
    char overview_path[1024];
    replace_extension(overview_path, sizeof(overview_path), map->path, ".OVH");
    size_t cell_count = (size_t)map->width * (size_t)map->height;
    if (!load_dark_colony_overview_colors(overview_path, cell_count, &map->overview_colors))
        return false;
    return true;
}

static bool dark_colony_map_file_load_o16(DarkColonyMapFile *map) {
    if (!map || map->width <= 0 || map->height <= 0) return false;
    char o16_path[1024];
    replace_extension(o16_path, sizeof(o16_path), map->path, ".O16");
    Blob blob;
    if (!load_blob(o16_path, &blob)) return false;
    size_t word_count = (size_t)map->width * (size_t)map->height * 2;
    if (blob.size < word_count * 2) {
        free_blob(&blob);
        return false;
    }
    map->overview_words = calloc(word_count, sizeof(*map->overview_words));
    if (!map->overview_words) {
        free_blob(&blob);
        return false;
    }
    for (size_t i = 0; i < word_count; ++i)
        map->overview_words[i] = read_u16_le(blob.bytes + i * 2);
    free_blob(&blob);
    return true;
}

static bool append_dark_colony_resource_vent(GameMap *map, int x, int y, int rate, int amount) {
    if (!map || !map_contains(map, x, y)) return false;
    if (amount <= 0) amount = 1;

    MapResourceVent *vents = realloc(map->resource_vents,
                                     (size_t)(map->resource_vent_count + 1) * sizeof(MapResourceVent));
    if (!vents) return false;
    map->resource_vents = vents;
    MapResourceVent *vent = &map->resource_vents[map->resource_vent_count++];
    vent->gx = x;
    vent->gy = y;
    vent->amount = amount;
    vent->rate = rate;
    vent->active = rate > 0;

    if (rate > 0 && map->decoration_count < MAX_DECORATIONS) {
        MapDecoration *decorations = realloc(map->decorations,
                                             (size_t)(map->decoration_count + 1) * sizeof(MapDecoration));
        if (decorations) {
            map->decorations = decorations;
            MapDecoration *dec = &map->decorations[map->decoration_count++];
            memset(dec, 0, sizeof(*dec));
            dec->gx = x;
            dec->gy = y;
            dec->footprint_w = 1;
            dec->footprint_h = 1;
            dec->center_anchor = true;
            dec->frame_index = -1;
            dec->render_flags = RTS_FRAME_ADDITIVE;
            snprintf(dec->sprite_name, sizeof(dec->sprite_name), "SPRITES/VENT2.SPR");
        }
    }
    return true;
}

static bool append_dark_colony_beacon(GameMap *map, int x, int y, int type) {
    if (!map || !map_contains(map, x, y) || type != 84) return false;
    if (map->decoration_count >= MAX_DECORATIONS) return false;

    MapDecoration *decorations = realloc(map->decorations,
                                         (size_t)(map->decoration_count + 1) * sizeof(MapDecoration));
    if (!decorations) return false;
    map->decorations = decorations;
    MapDecoration *dec = &map->decorations[map->decoration_count++];
    memset(dec, 0, sizeof(*dec));
    dec->gx = x;
    dec->gy = y;
    dec->footprint_w = 1;
    dec->footprint_h = 1;
    dec->center_anchor = true;
    dec->frame_index = 0;
    dec->frame2_index = 1;
    dec->render2_flags = RTS_FRAME_ADDITIVE | RTS_FRAME_BLINK;
    snprintf(dec->sprite_name, sizeof(dec->sprite_name), "SPRITES/BEAC.SPR");
    snprintf(dec->sprite2_name, sizeof(dec->sprite2_name), "SPRITES/BEAC.SPR");
    return true;
}

static void load_dark_colony_resource_vents_from_scenario(const DarkColonyScenarioFile *scenario,
                                                          GameMap *map) {
    if (!scenario || !map) return;
    for (int i = 0; i < scenario->object_count; ++i) {
        const DarkColonyScenarioObject *object = &scenario->objects[i];
        if (object->type == 40 && object->value_count >= 5) {
            append_dark_colony_resource_vent(map, object->x, object->y,
                                             object->team, object->status);
        }
    }
}

static void load_dark_colony_beacons_from_scenario(const DarkColonyScenarioFile *scenario,
                                                   GameMap *map) {
    if (!scenario || !map) return;
    for (int i = 0; i < scenario->object_count; ++i) {
        const DarkColonyScenarioObject *object = &scenario->objects[i];
        if (object->type == 84 && object->value_count >= 5) {
            append_dark_colony_beacon(map, object->x, object->y,
                                      object->type);
        }
    }
}

static void load_dark_colony_camera_from_scenario(const DarkColonyScenarioFile *scenario,
                                                  GameMap *map) {
    if (!scenario || !map || scenario->team_count <= 0) return;
    const DarkColonyScenarioTeam *team = &scenario->teams[0];
    for (int i = 0; i < team->ai_slot_count; ++i) {
        int x = team->ai_slots[i][0];
        int y = team->ai_slots[i][1];
        if (x == 0 && y == 0) continue;
        map->has_camera = true;
        map->camera_gx = (float)x + 0.5f;
        map->camera_gy = (float)y + 0.5f;
        return;
    }
}

static uint32_t dark_colony_rgb565_to_rgba(uint16_t value) {
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
    Blob blob;
    if (!colors_out || !load_blob(path, &blob)) return false;
    if (blob.size < cell_count * 2) {
        free_blob(&blob);
        return false;
    }
    uint32_t *colors = calloc(cell_count, sizeof(*colors));
    if (!colors) {
        free_blob(&blob);
        return false;
    }
    for (size_t i = 0; i < cell_count; ++i) {
        colors[i] = dark_colony_rgb565_to_rgba(read_u16_le(blob.bytes + i * 2));
    }
    free_blob(&blob);
    *colors_out = colors;
    return true;
}

bool load_dark_colony_map(const char *map_path, GameMap *out) {
    memset(out, 0, sizeof(*out));
    out->bottom_up_coordinates = true;

    DarkColonyMapNative *native = calloc(1, sizeof(*native));
    if (!native) return false;
    if (!dark_colony_map_file_load(map_path, &native->map)) {
        dark_colony_map_native_destroy(native);
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
            DarkColonyMapFile alt_map;
            if (dark_colony_map_file_load(alt_path, &alt_map) &&
                alt_map.kind == DARK_COLONY_MAP_FILE_MAP) {
                dark_colony_map_file_destroy(&native->map);
                native->map = alt_map;
            } else {
                dark_colony_map_file_destroy(&alt_map);
                use_overview_colors = true;
            }
        }
    }

    dark_colony_map_file_load_mtg_sidecar(&native->map);
    dark_colony_map_file_load_o16(&native->map);
    if (dark_colony_map_file_load_overview(&native->map) &&
        (use_overview_colors || native->map.kind == DARK_COLONY_MAP_FILE_MTG)) {
        out->cell_colors = calloc((size_t)native->map.width * (size_t)native->map.height,
                                  sizeof(*out->cell_colors));
        if (!out->cell_colors) {
            dark_colony_map_native_destroy(native);
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
        out->render_features |= MAP_RENDER_USE_CELL_COLORS;
    }

    int width = native->map.width;
    int height = native->map.height;
    size_t source_count = (size_t)width * (size_t)height;
    out->width = width;
    out->height = height;
    out->render_features |= MAP_RENDER_INTERLEAVED_OVERLAYS;
    out->tile_ids        = calloc(source_count, sizeof(uint16_t));
    out->blocked         = calloc(source_count, sizeof(uint8_t));
    out->tile_overlay_count = 1;
    out->tile_overlays[0]   = calloc(source_count, sizeof(uint16_t));
    out->tile_flip_flags[0] = calloc(source_count, sizeof(uint8_t));
    out->tile_flip_flags[1] = calloc(source_count, sizeof(uint8_t));
    if (!out->tile_ids || !out->blocked ||
        !out->tile_overlays[0] || !out->tile_flip_flags[0] || !out->tile_flip_flags[1]) {
        dark_colony_map_native_destroy(native);
        destroy_map(out);
        return false;
    }

    for (int y = 0; y < height; ++y) {
        int source_y = height - 1 - y;
        for (int x = 0; x < width; ++x) {
            size_t idx = (size_t)y * (size_t)width + (size_t)x;
            size_t source_idx = (size_t)source_y * (size_t)width + (size_t)x;
            const DarkColonyMapCell *cell = &native->map.cells[source_idx];
            if (native->map.kind == DARK_COLONY_MAP_FILE_MAP) {
                out->tile_ids[idx] = cell->background;
                out->tile_overlays[0][idx] = cell->foreground;
                out->blocked[idx] = (cell->flags & (1u << 9)) ? 1 : 0;
                out->tile_flip_flags[0][idx] = (cell->flags & (1u << 5)) ? 1 : 0;
                out->tile_flip_flags[1][idx] = (cell->flags & (1u << 6)) ? 1 : 0;
            } else {
                out->tile_ids[idx] = cell->mtg;
                out->tile_overlays[0][idx] = 0;
            }
        }
    }

    char scn_path[1024];
    replace_extension(scn_path, sizeof(scn_path), native->map.path, ".SCN");
    native->has_scenario = dark_colony_scenario_load(scn_path, &native->scenario);
    if (native->has_scenario) {
        char tileset_token[64] = { 0 };
        copy_trimmed_token(tileset_token, sizeof(tileset_token),
                           native->scenario.tileset_file,
                           strlen(native->scenario.tileset_file));
        char *dot = strrchr(tileset_token, '.');
        if (dot) *dot = '\0';
        uppercase_trimmed_token(out->tileset_name, sizeof(out->tileset_name),
                                tileset_token, strlen(tileset_token));
        load_dark_colony_camera_from_scenario(&native->scenario, out);
        load_dark_colony_resource_vents_from_scenario(&native->scenario, out);
        load_dark_colony_beacons_from_scenario(&native->scenario, out);
        int team_count = native->scenario.team_count;
        if (team_count > 8) team_count = 8;
        for (int i = 0; i < team_count; ++i) {
            out->player_resources[i] = native->scenario.teams[i].money;
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
    out->destroy_native_data = dark_colony_map_native_destroy;
    return true;
}

/* ── unit SCN parser ────────────────────────────────────────────────────── */

enum { DARK_COLONY_MAX_GAMESTAT_UNITS = 128 };

typedef struct {
    float speed;
    int max_health;
} DarkColonyUnitConfig;

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

static float dark_colony_speed_from_gamestat(int speed) {
    return speed > 0 ? (float)speed / 32.0f : 0.0f;
}

static bool dark_colony_map_path_is_multiplayer(const char *map_path) {
    if (!map_path) return false;
    return find_ascii_case_insensitive((char *)map_path, "/MPLAYER/") ||
           find_ascii_case_insensitive((char *)map_path, "\\MPLAYER\\");
}

static void load_dark_colony_unit_config(DarkColonyUnitConfig configs[DARK_COLONY_MAX_GAMESTAT_UNITS]) {
    memset(configs, 0, sizeof(DarkColonyUnitConfig) * DARK_COLONY_MAX_GAMESTAT_UNITS);

    int count = DC_GAMESTAT_UNIT_COUNT;
    if (count > DARK_COLONY_MAX_GAMESTAT_UNITS) count = DARK_COLONY_MAX_GAMESTAT_UNITS;
    for (int i = 0; i < count; ++i) {
        const DcGamestatUnit *unit = &dc_gamestat_units[i];
        if (unit->value_count > DC_GAMESTAT_UNIT_SPEED) {
            configs[i].speed = dark_colony_speed_from_gamestat(unit->values[DC_GAMESTAT_UNIT_SPEED]);
        }
        if (unit->value_count > DC_GAMESTAT_UNIT_HEALTH) {
            configs[i].max_health = unit->values[DC_GAMESTAT_UNIT_HEALTH];
        }
    }
}

static int dark_colony_fixed_from_cell(int cell) {
    return cell * 256 + DC_FIXED_TILE_CENTER;
}

static int dark_colony_fixed_from_city_x(int cell) {
    return cell * 256;
}

static float dark_colony_fixed_to_cell(int fixed) {
    return (float)fixed / 256.0f;
}

static int dark_colony_default_health_for_type(int type,
                                               const DarkColonyUnitConfig *unit_config) {
    if (type >= 0 && type < DARK_COLONY_MAX_GAMESTAT_UNITS && unit_config &&
        unit_config[type].max_health > 0) {
        return unit_config[type].max_health;
    }
    return 1;
}

static int dark_colony_initial_health_for_type(int type, int scenario_health,
                                               const DarkColonyUnitConfig *unit_config) {
    return scenario_health >= 0 ? scenario_health :
        dark_colony_default_health_for_type(type, unit_config);
}

static void dark_colony_object_pool_init(DcObjectPool *pool) {
    if (!pool) return;
    memset(pool, 0, sizeof(*pool));
    pool->object_limit = DC_DYNAMIC_OBJECT_FIRST;
}

static void dark_colony_object_pool_mark_active(DcObjectPool *pool, int object_index) {
    if (!pool || object_index < 0 || object_index >= DC_MAX_OBJECTS) return;
    if (pool->active_count < DC_MAX_OBJECTS)
        pool->active_objects[pool->active_count++] = (uint16_t)object_index;
    if (object_index >= pool->object_limit)
        pool->object_limit = object_index + 1;
}

static bool dark_colony_object_pool_init_object(DcObjectPool *pool, int object_index,
                                                int x_fixed, int z_fixed,
                                                int type, int team, int health,
                                                int subtype) {
    if (!pool || object_index < 0 || object_index >= DC_MAX_OBJECTS ||
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
    dark_colony_object_pool_mark_active(pool, object_index);
    return true;
}

static int dark_colony_city_object_index(int team, int slot) {
    if (team < 0 || team >= 8 || slot < 0 || slot >= DC_BUILDINGS_PER_SIDE)
        return -1;
    return team * DC_BUILDINGS_PER_SIDE + slot;
}

static int dark_colony_object_pool_alloc_dynamic(DcObjectPool *pool) {
    if (!pool) return -1;
    int limit = pool->object_limit;
    if (limit < DC_DYNAMIC_OBJECT_FIRST) limit = DC_DYNAMIC_OBJECT_FIRST;
    for (int i = DC_DYNAMIC_OBJECT_FIRST; i < limit; ++i) {
        if (pool->objects[i].active == 0) return i;
    }
    if (limit >= DC_MAX_OBJECTS) return -1;
    pool->object_limit = limit + 1;
    return limit;
}

static int dark_colony_object_pool_add_dynamic(DcObjectPool *pool, int x, int z,
                                               int type, int team, int scenario_health,
                                               int subtype,
                                               const DarkColonyUnitConfig *unit_config) {
    int object_index = dark_colony_object_pool_alloc_dynamic(pool);
    if (object_index < 0) return -1;
    int health = dark_colony_initial_health_for_type(type, scenario_health, unit_config);
    if (!dark_colony_object_pool_init_object(pool, object_index,
                                             dark_colony_fixed_from_cell(x),
                                             dark_colony_fixed_from_cell(z),
                                             type, team, health, subtype)) {
        return -1;
    }
    return object_index;
}

static bool dark_colony_object_pool_add_city_slot(DcObjectPool *pool, int team, int slot,
                                                  int x_fixed, int z_fixed, int type, int race,
                                                  const DarkColonyUnitConfig *unit_config) {
    int object_index = dark_colony_city_object_index(team, slot);
    if (object_index < 0 || type <= 0) return false;
    int health = dark_colony_default_health_for_type(type, unit_config);
    (void)race;
    return dark_colony_object_pool_init_object(pool, object_index, x_fixed, z_fixed,
                                               type, team, health, 0);
}

static bool dark_colony_scenario_object_starts_visible(const DarkColonyScenarioObject *object) {
    return object && object->status >= 0;
}

static int dark_colony_mobj_type_for_type(int type, int race) {
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
        case 81: return MT_DC_CITY_TOWER;
        case 86: return MT_DC_COMMS_DISH;
        default: break;
    }

    if (race == 1) {
        if (type == 0 || type == 8 || (type >= 69 && type <= 76)) return MT_DC_GREY;
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

static const char *dark_colony_unit_sprite_for_type(int type, int race) {
    if (type == 16 || type == 17) return "SPRITES/HUBU.SPR";
    if (type >= 18 && type <= 22) return "SPRITES/SHORTCIT.SPR";
    if (type >= 28 && type <= 34) return "SPRITES/ALIEN1.SPR";
    if (type == 81) return "SPRITES/TOWR.SPR";
    if (type == 86) return "SPRITES/DISH.SPR";
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

static int dark_colony_unit_frame_for_type(int type) {
    switch (type) {
        case 16: return 0; /* HUBU.FIN EXCOPODSTAND0 */
        case 17: return 4; /* HUBU.FIN BRRKPODSTAND0 */
        case 18: return 1; /* ROBOTICSSTAND0 */
        case 19: return 1; /* ROBOPOD2 reuses robotics art. */
        case 20: return 2; /* SCIENCESTAND0 */
        case 21: return 2; /* SCNCPOD2 reuses science art. */
        case 22: return 4; /* HUMRESSTAND0 */
        case 81: return 0; /* TOWR.FIN TOWRSTAND0 */
        default: break;
    }
    if (type >= 28 && type <= 34) return type - 28;
    return 0;
}

static int dark_colony_unit_state_for_type(int type) {
    switch (type) {
        case 16: return S_DC_EXCOPOD_STND;
        case 17: return S_DC_BRRKPOD_STND;
        case 81: return S_DC_TOWR_STND;
        default: return S_NULL;
    }
}

static int dark_colony_city_unit_type_for_slot(int race, int slot) {
    static const int city_types[2][15] = {
        { 16, 17, 18, 20, 22, 81, 25, 25, 25, 25, 25, 25, 25, 0, 0 },
        { 28, 29, 30, 32, 34, 81, 25, 25, 25, 25, 25, 25, 25, 0, 0 },
    };
    if (slot < 0 || slot >= 15) return 0;
    return city_types[race == 1 ? 1 : 0][slot];
}

static void dark_colony_city_slot_offset(int slot, int *x_out, int *z_out) {
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

static void dark_colony_city_slot_position_fixed(int anchor_x, int anchor_y, int slot,
                                                 int *x_fixed, int *z_fixed) {
    /* DC.EXE city.c stores city object positions as 8.8 fixed-point map units.
       fcn.004412d4 writes:
           x_pos = city_x * 0x100 + dc_city_slot_offsets[slot].x * 8
           z_pos = city_z * 0x100 + dc_city_slot_offsets[slot].z * 8 */
    int slot_x = 0, slot_z = 0;
    dark_colony_city_slot_offset(slot, &slot_x, &slot_z);
    int fx = dark_colony_fixed_from_city_x(anchor_x);
    int fz = dark_colony_fixed_from_cell(anchor_y);
    fx += slot_x * 8;
    fz += slot_z * 8;
    if (x_fixed) *x_fixed = fx;
    if (z_fixed) *z_fixed = fz;
}

static bool dark_colony_team_city_anchor(const DarkColonyScenarioTeam *team,
                                         int *x_out, int *y_out) {
    if (!team || team->ai_slot_count < 2)
        return false;
    /* DC.EXE scenario.c reads the second %AISlots X into team +0x2c. With our
       bottom-up map storage, the matching visible city Y is the first row. */
    int x = team->ai_slots[1][0];
    int y = team->ai_slots[0][1];
    if (x == 0)
        return false;
    if (x_out) *x_out = x;
    if (y_out) *y_out = y;
    return true;
}

static int dark_colony_city_render_x_fixed(const DcObject *object, int object_index) {
    if (!object || object_index < 0 || object_index >= DC_BUILDING_OBJECT_COUNT)
        return object ? object->x_pos : 0;
    int slot = object_index % DC_BUILDINGS_PER_SIDE;
    int slot_x = 0;
    dark_colony_city_slot_offset(slot, &slot_x, NULL);
    return object->x_pos - slot_x * 8;
}

static int dark_colony_city_render_z_fixed(const DcObject *object, int object_index) {
    if (!object || object_index < 0 || object_index >= DC_BUILDING_OBJECT_COUNT)
        return object ? object->z_pos : 0;
    int slot = object_index % DC_BUILDINGS_PER_SIDE;
    int slot_z = 0;
    dark_colony_city_slot_offset(slot, NULL, &slot_z);
    return object->z_pos - slot_z * 8;
}

static bool append_dark_colony_object_unit(Unit *units, int *count, int max_units,
                                           int object_index,
                                           const DcObject *object, int race,
                                           const DarkColonyUnitConfig *unit_config,
                                           bool city_object,
                                           bool *player_selected,
                                           bool *player_has_exploiter,
                                           bool *player_anchor_set,
                                           int *player_anchor_x,
                                           int *player_anchor_y) {
    if (!units || !count || *count >= max_units || !object || object->active == 0) {
        return false;
    }
    int type = object->type;
    int team = object->team;
    const char *sprite = dark_colony_unit_sprite_for_type(type, race);
    int mobj_type = dark_colony_mobj_type_for_type(type, race);
    if (!sprite || mobj_type <= 0) return false;

    Unit *u = &units[*count];
    memset(u, 0, sizeof(*u));
    int render_x_pos = city_object ? dark_colony_city_render_x_fixed(object, object_index) :
        object->x_pos;
    int render_z_pos = city_object ? dark_colony_city_render_z_fixed(object, object_index) :
        object->z_pos;
    u->gx = dark_colony_fixed_to_cell(render_x_pos);
    u->gy = dark_colony_fixed_to_cell(render_z_pos);
    u->sprite_id = -1;
    u->attack_target = -1;
    u->harvest_target = -1;
    if (type >= 0 && type < DARK_COLONY_MAX_GAMESTAT_UNITS && unit_config) {
        u->speed = unit_config[type].speed;
    }
    u->type_id = (uint16_t)mobj_type;
    if (city_object) u->render_sort_y = (float)object->cell_z + 0.5f;
    u->owner = (team == 0 || mobj_type == MT_DC_COMMS_DISH) ? 0 : 1;
    u->hp = object->health_or_amount;
    u->selected = u->owner == 0 && mobj_type != MT_DC_COMMS_DISH &&
        (mobj_type < MT_DC_BUILDING_BASE) && player_selected && !*player_selected;
    if (u->selected) *player_selected = true;
    u->frame = dark_colony_unit_frame_for_type(type);
    snprintf(u->sprite_name, sizeof(u->sprite_name), "%s", sprite);
    int state_id = dark_colony_unit_state_for_type(type);
    if (state_id != S_NULL) {
        StateContext ctx = { .game_info = &dark_colony_game_info };
        set_unit_state(&ctx, u, state_id);
    }
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

int load_dark_colony_initial_units(const char *map_path, Unit *units, int max_units) {
    char scn_path[1024];
    replace_extension(scn_path, sizeof(scn_path), map_path, ".SCN");
    DarkColonyScenarioFile scenario;
    if (!dark_colony_scenario_load(scn_path, &scenario)) return 0;

    DarkColonyUnitConfig unit_config[DARK_COLONY_MAX_GAMESTAT_UNITS];
    load_dark_colony_unit_config(unit_config);
    DcObjectPool object_pool;
    dark_colony_object_pool_init(&object_pool);

    int count = 0;
    bool player_has_exploiter = false;
    bool player_anchor_set = false;
    bool player_selected = false;
    int player_anchor_x = 0;
    int player_anchor_y = 0;

    for (int team = 0; team < scenario.team_count; ++team) {
        const DarkColonyScenarioTeam *team_info = &scenario.teams[team];
        if (!team_info->active) continue;
        int slot_x = 0;
        int slot_y = 0;
        if (!dark_colony_team_city_anchor(team_info, &slot_x, &slot_y)) continue;
        for (int slot = 0; slot < DARK_COLONY_SCN_CITY_SLOTS; ++slot) {
            int value_index = slot * 2;
            if (value_index >= team_info->city_value_count) break;
            if (team_info->city_values[value_index] <= 0) continue;
            int type = dark_colony_city_unit_type_for_slot(team_info->race, slot);
            int x_fixed = 0, z_fixed = 0;
            dark_colony_city_slot_position_fixed(slot_x, slot_y, slot,
                                                 &x_fixed, &z_fixed);
            dark_colony_object_pool_add_city_slot(&object_pool, team, slot,
                                                  x_fixed, z_fixed, type,
                                                  team_info->race, unit_config);
        }
    }

    for (int i = 0; i < scenario.object_count; ++i) {
        const DarkColonyScenarioObject *object = &scenario.objects[i];
        if (object->value_count < 6 ||
            object->team < 0 || object->team >= DARK_COLONY_SCN_MAX_TEAMS ||
            object->x < 0 || object->y < 0) {
            continue;
        }
        if (!dark_colony_scenario_object_starts_visible(object)) continue;
        dark_colony_object_pool_add_dynamic(&object_pool, object->x, object->y,
                                            object->type, object->team,
                                            object->status, object->extra,
                                            unit_config);
    }

    for (int i = 0; i < object_pool.active_count && count < max_units; ++i) {
        int object_index = object_pool.active_objects[i];
        const DcObject *object = &object_pool.objects[object_index];
        int team = object->team;
        int race = team >= 0 && team < DARK_COLONY_SCN_MAX_TEAMS ?
            scenario.teams[team].race : 0;
        if (object->type == DC_OBJECT_TYPE_PETRA7_VENT || object->type == 84)
            continue;
        if (race != 1 && object->type == 16 && count < max_units) {
            DcObject tower = *object;
            tower.type = 81;
            append_dark_colony_object_unit(units, &count, max_units, object_index,
                                           &tower, race,
                                           unit_config, object_index < DC_BUILDING_OBJECT_COUNT,
                                           NULL, NULL, NULL, NULL, NULL);
        }
        append_dark_colony_object_unit(units, &count, max_units, object_index, object, race,
                                       unit_config, object_index < DC_BUILDING_OBJECT_COUNT,
                                       &player_selected,
                                       &player_has_exploiter,
                                       &player_anchor_set,
                                       &player_anchor_x,
                                       &player_anchor_y);
    }
    if (dark_colony_map_path_is_multiplayer(map_path) && !player_has_exploiter &&
        player_anchor_set && count < max_units) {
        int object_index = dark_colony_object_pool_add_dynamic(&object_pool, player_anchor_x + 2,
                                                               player_anchor_y, 6, 0, -1, 0,
                                                               unit_config);
        if (object_index >= 0) {
            const DcObject *object = &object_pool.objects[object_index];
            append_dark_colony_object_unit(units, &count, max_units, object_index,
                                           object, 0,
                                           unit_config, false, &player_selected,
                                           &player_has_exploiter,
                                           &player_anchor_set,
                                           &player_anchor_x,
                                           &player_anchor_y);
        }
    }
    dark_colony_scenario_destroy(&scenario);
    return count;
}

/* ── plugin asset loader ────────────────────────────────────────────────── */

bool dark_colony_plugin_load_assets(SDL_Renderer *renderer, const char *data_root,
                                    const GameMap *map, const char *sprite_name,
                                    Tileset *tileset, SpriteSheet *unit_sprite) {
    char bts_path[1024];
    snprintf(bts_path, sizeof(bts_path), "%s/SCENARIO/%s.BTS", data_root, map->tileset_name);
    if (!load_dark_colony_tileset(renderer, bts_path, tileset)) return false;

    char sprite_path[1024];
    uint32_t sprite_palette[256] = { 0 };
    if (sprite_name[0] == '/') {
        snprintf(sprite_path, sizeof(sprite_path), "%s", sprite_name);
    } else {
        path_join(sprite_path, sizeof(sprite_path), data_root, sprite_name);
    }
    if (!load_dark_colony_sprite(renderer, sprite_path, unit_sprite, sprite_palette)) {
        fprintf(stderr, "failed to load %s\n", sprite_path);
        destroy_tileset(tileset);
        return false;
    }
    return true;
}
