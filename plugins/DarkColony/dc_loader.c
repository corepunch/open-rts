#define _DEFAULT_SOURCE
#include "plugin.h"
#include "info.h"
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
        int r = clamp255((int)p[i * 3 + 0] * 4);
        int g = clamp255((int)p[i * 3 + 1] * 4);
        int b = clamp255((int)p[i * 3 + 2] * 4);
        colors[i] = i == 0 ? 0x00000000u :
            (0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
    }
}

static bool path_basename_is(const char *path, const char *name) {
    if (!path || !name) return false;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return strcasecmp(base, name) == 0;
}

static uint32_t dark_colony_sprite_pixel_rgba(const char *path, int frame, uint8_t index,
                                             const uint32_t palette[256]) {
    if (index == 0) return 0x00000000u;
    if (frame == 1 && path_basename_is(path, "BEAC.SPR") && index == 112)
        return 0x00000000u;
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
        int r = clamp255((int)p[0] * 4);
        int g = clamp255((int)p[1] * 4);
        int b = clamp255((int)p[2] * 4);
        bool transparent = (i == 0) || (r > 240 && g < 16 && b > 240);
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
    int w;
    int h;
    int dis_x;
    int dis_y;
    bool blank;
} DcSprFrameInfo;

bool load_dark_colony_sprite(SDL_Renderer *renderer, const char *path, SpriteSheet *out,
                             uint32_t palette_out[256]) {
    memset(out, 0, sizeof(*out));
    Blob blob;
    if (!load_blob(path, &blob)) return false;
    if (blob.size < 8 + 256 * 3) { free_blob(&blob); return false; }

    int flags = read_u16_le(blob.bytes + 0);
    bool compressed = (flags & 0x80) != 0;
    int frame_count = read_u16_le(blob.bytes + 2);
    size_t desc_off = 8 + 256 * 3;
    size_t data_off = desc_off + (size_t)frame_count * 8;
    if (frame_count <= 0 || frame_count > 1024 || data_off > blob.size) {
        fprintf(stderr, "%s is not a supported Dark Colony raw SPR\n", path);
        free_blob(&blob);
        return false;
    }

    dark_colony_palette_from_spr(blob.bytes, blob.size, palette_out);
    int visible_frames = frame_count;
    int min_dis_x = INT32_MAX, min_dis_y = INT32_MAX;
    int max_dis_x = INT32_MIN, max_dis_y = INT32_MIN;
    size_t total_pixels = 0;
    DcSprFrameInfo *info = calloc((size_t)visible_frames, sizeof(*info));
    if (!info) {
        free_blob(&blob);
        return false;
    }
    for (int i = 0; i < visible_frames; ++i) {
        const uint8_t *d = blob.bytes + desc_off + (size_t)i * 8;
        int w = read_u16_le(d + 0);
        int h = read_u16_le(d + 2);
        int dis_x = read_u16_le(d + 4);
        int dis_y = read_u16_le(d + 6);
        bool blank = w <= 0 || h <= 0;
        if (w > 512 || h > 512) {
            free(info); free_blob(&blob);
            return false;
        }
        info[i] = (DcSprFrameInfo){ blank ? 1 : w, blank ? 1 : h, dis_x, dis_y, blank };
        if (dis_x < min_dis_x) min_dis_x = dis_x;
        if (dis_y < min_dis_y) min_dis_y = dis_y;
        if (dis_x + info[i].w > max_dis_x) max_dis_x = dis_x + info[i].w;
        if (dis_y + info[i].h > max_dis_y) max_dis_y = dis_y + info[i].h;
        if (!blank) total_pixels += (size_t)w * (size_t)h;
    }
    if (!compressed && data_off + total_pixels > blob.size) {
        fprintf(stderr, "%s has truncated Dark Colony sprite pixels\n", path);
        free(info); free_blob(&blob);
        return false;
    }
    int max_w = max_dis_x > min_dis_x ? max_dis_x - min_dis_x : 1;
    int max_h = max_dis_y > min_dis_y ? max_dis_y - min_dis_y : 1;
    if (max_w <= 0 || max_h <= 0 || max_w > 1024 || max_h > 1024) {
        free(info); free_blob(&blob);
        return false;
    }

    int cols = (int)ceilf(sqrtf((float)visible_frames));
    int rows = (visible_frames + cols - 1) / cols;
    int atlas_w = cols * max_w;
    int atlas_h = rows * max_h;
    uint32_t *rgba = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    SDL_Rect *frames = calloc((size_t)visible_frames, sizeof(SDL_Rect));
    SDL_Rect *bounds = calloc((size_t)visible_frames, sizeof(SDL_Rect));
    SDL_Point *ground_points = calloc((size_t)visible_frames, sizeof(SDL_Point));
    if (!rgba || !frames || !bounds || !ground_points) {
        free(info); free(rgba); free(frames); free(bounds); free(ground_points); free_blob(&blob);
        return false;
    }

    size_t src_pos = data_off;
    for (int i = 0; i < visible_frames; ++i) {
        int w = info[i].w;
        int h = info[i].h;
        int fx = (i % cols) * max_w + (info[i].dis_x - min_dis_x);
        int fy = (i / cols) * max_h + (info[i].dis_y - min_dis_y);
        if (compressed) {
            if (src_pos + 4 > blob.size) { free(info); free(rgba); free(frames); free(bounds); free(ground_points); free_blob(&blob); return false; }
            uint32_t chunk_size = read_u32_le(blob.bytes + src_pos);
            src_pos += 4;
            if (src_pos + chunk_size > blob.size) { free(info); free(rgba); free(frames); free(bounds); free(ground_points); free_blob(&blob); return false; }
            if (info[i].blank) {
                src_pos += chunk_size;
                frames[i] = (SDL_Rect){ (i % cols) * max_w, (i / cols) * max_h, max_w, max_h };
                bounds[i] = (SDL_Rect){ 0, 0, max_w, max_h };
                /* Ground anchor = center-bottom of the shared canvas.
                   dis_x/dis_y place each frame within a canvas of max_w×max_h;
                   the origin (foot of the unit) is at the canvas center-bottom. */
                ground_points[i] = (SDL_Point){ max_w / 2, max_h };
                continue;
            }
            const uint8_t *src = blob.bytes + src_pos;
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
                            if (dst_x >= 0 && dst_x < atlas_w && dst_y >= 0 && dst_y < atlas_h)
                                rgba[dst_y * atlas_w + dst_x] =
                                    dark_colony_sprite_pixel_rgba(path, i, src[pos + (size_t)p],
                                                                  palette_out);
                        }
                        write++;
                    }
                    pos += (size_t)count;
                }
            }
            src_pos += chunk_size;
        } else {
            if (!info[i].blank) {
                const uint8_t *src = blob.bytes + src_pos;
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        int dst_x = fx + x;
                        int dst_y = fy + y;
                        if (dst_x >= 0 && dst_x < atlas_w && dst_y >= 0 && dst_y < atlas_h) {
                            rgba[dst_y * atlas_w + dst_x] =
                                dark_colony_sprite_pixel_rgba(path, i, src[y * w + x],
                                                              palette_out);
                        }
                    }
                }
                src_pos += (size_t)w * (size_t)h;
            }
        }
        frames[i] = (SDL_Rect){ (i % cols) * max_w, (i / cols) * max_h, max_w, max_h };
        bounds[i] = dc_visible_bounds(rgba, atlas_w, frames[i]);
        /* Ground anchor = center-bottom of the shared canvas. */
        ground_points[i] = (SDL_Point){ max_w / 2, max_h };
    }

    SDL_Texture *texture = rgba_texture(renderer, rgba, atlas_w, atlas_h, true);
    if (!texture) {
        free(info);
        free(rgba);
        free(frames);
        free(bounds);
        free(ground_points);
        free_blob(&blob);
        return false;
    }

    out->texture = texture;
    out->frames = frames;
    out->frame_bounds = bounds;
    out->frame_ground_points = ground_points;
    out->frame_count = visible_frames;
    out->frame_w = max_w;
    out->frame_h = max_h;
    out->rotations = 1;
    out->primary_frames_per_rotation = visible_frames;

    free(info);
    free(rgba);
    free_blob(&blob);
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

static bool token_has_only_trailing_space(const char *token, int consumed) {
    if (!token || consumed < 0) return false;
    for (const char *p = token + consumed; *p; ++p) {
        if (!isspace((unsigned char)*p)) return false;
    }
    return true;
}

static int dark_colony_y_to_map_height(int height, int y) {
    if (height <= 0) return y;
    int map_y = height - 1 - y;
    if (map_y < 0) map_y = 0;
    if (map_y >= height) map_y = height - 1;
    return map_y;
}

static int dark_colony_scn_y_to_map(const GameMap *map, int y) {
    return dark_colony_y_to_map_height(map ? map->height : 0, y);
}

static bool dark_colony_map_dimensions(const char *map_path, int *width, int *height) {
    if (!map_path || !width || !height) return false;
    Blob blob;
    char path_buf[1024];
    if (!load_blob(map_path, &blob)) {
        const char *dot = strrchr(map_path, '.');
        if (dot && strcasecmp(dot, ".MAP") == 0) {
            replace_extension(path_buf, sizeof(path_buf), map_path, ".MTG");
            if (!load_blob(path_buf, &blob)) return false;
        } else {
            return false;
        }
    }
    bool ok = false;
    if (blob.size >= 8) {
        int maybe_width = read_i32_le(blob.bytes + 0);
        int maybe_height = read_i32_le(blob.bytes + 4);
        size_t maybe_count = (size_t)maybe_width * (size_t)maybe_height;
        if (maybe_width > 0 && maybe_height > 0 && maybe_width <= 512 && maybe_height <= 512 &&
            blob.size >= 8 + maybe_count * 2 * 3) {
            *width = maybe_width;
            *height = maybe_height;
            ok = true;
        }
    }
    if (!ok && blob.size >= 2) {
        int maybe_width = (int)blob.bytes[0];
        int maybe_height = (int)blob.bytes[1];
        size_t maybe_count = (size_t)maybe_width * (size_t)maybe_height;
        if (maybe_width > 0 && maybe_height > 0 && maybe_width <= 512 && maybe_height <= 512 &&
            blob.size >= 2 + maybe_count) {
            *width = maybe_width;
            *height = maybe_height;
            ok = true;
        }
    }
    free_blob(&blob);
    return ok;
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

static void load_dark_colony_resource_vents_from_scn(const char *scn, GameMap *map) {
    if (!scn || !map) return;
    for (const char *line = scn; line && *line;) {
        const char *next = strpbrk(line, "\r\n");
        size_t len = next ? (size_t)(next - line) : strlen(line);
        char token[128] = { 0 };
        copy_trimmed_token(token, sizeof(token), line, len);

        int x = 0, y = 0, type = 0, rate = 0, amount = 0, consumed = 0;
        if (sscanf(token, "%d %d %d %d %d%n", &x, &y, &type, &rate, &amount, &consumed) == 5 &&
            type == 40 && token_has_only_trailing_space(token, consumed)) {
            append_dark_colony_resource_vent(map, x, dark_colony_scn_y_to_map(map, y), rate, amount);
        }

        if (!next) break;
        char nl = *next++;
        if (nl == '\r' && *next == '\n') next++;
        line = next;
    }
}

static void load_dark_colony_beacons_from_scn(const char *scn, GameMap *map) {
    if (!scn || !map) return;
    int team_count = 0;
    bool object_mode = false;
    int trailing_blank_lines = 0;
    for (const char *line = scn; line && *line;) {
        const char *next = strpbrk(line, "\r\n");
        size_t len = next ? (size_t)(next - line) : strlen(line);
        char token[128] = { 0 };
        copy_trimmed_token(token, sizeof(token), line, len);

        if (token[0] == '\0') {
            if (team_count >= 8 && !object_mode && ++trailing_blank_lines >= 2)
                object_mode = true;
        } else if (strncmp(token, "TEAM ", 5) == 0) {
            team_count++;
            trailing_blank_lines = 0;
        } else if (object_mode) {
            int x = 0, y = 0, type = 0, team = 0, owner = 0, extra = 0, consumed = 0;
            if (sscanf(token, "%d %d %d %d %d %d%n",
                       &x, &y, &type, &team, &owner, &extra, &consumed) == 6 &&
                token_has_only_trailing_space(token, consumed)) {
                append_dark_colony_beacon(map, x, dark_colony_scn_y_to_map(map, y), type);
            }
            (void)team;
            (void)owner;
            (void)extra;
        } else {
            trailing_blank_lines = 0;
        }

        if (!next) break;
        char nl = *next++;
        if (nl == '\r' && *next == '\n') next++;
        line = next;
    }
}

static void load_dark_colony_camera_from_scn(const char *scn, GameMap *map) {
    if (!scn || !map) return;
    int current_team = -1;
    int aislot_lines = 0;
    for (const char *line = scn; line && *line;) {
        const char *next = strpbrk(line, "\r\n");
        size_t len = next ? (size_t)(next - line) : strlen(line);
        char token[64] = { 0 };
        copy_trimmed_token(token, sizeof(token), line, len);

        int team = -1;
        if (sscanf(token, "TEAM %d", &team) == 1) {
            current_team = team;
            aislot_lines = 0;
        } else if (strcmp(token, "%AISlots") == 0) {
            aislot_lines = current_team == 0 ? 2 : 0;
        } else if (aislot_lines > 0) {
            int x = 0, y = 0;
            if (sscanf(token, "%d %d", &x, &y) == 2 && (x != 0 || y != 0)) {
                int map_y = map->height - 1 - y;
                if (map_y < 0) map_y = 0;
                if (map_y >= map->height) map_y = map->height - 1;
                map->has_camera = true;
                map->camera_gx = (float)x + 0.5f;
                map->camera_gy = (float)map_y + 0.5f;
                return;
            }
            aislot_lines--;
        }

        if (!next) break;
        char nl = *next++;
        if (nl == '\r' && *next == '\n') next++;
        line = next;
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
    char map_path_buf[1024];
    Blob blob;
    if (!load_blob(map_path, &blob)) {
        /* If a .MAP was requested but doesn't exist, try the companion .MTG. */
        const char *dot = strrchr(map_path, '.');
        if (dot && strcasecmp(dot, ".MAP") == 0) {
            replace_extension(map_path_buf, sizeof(map_path_buf), map_path, ".MTG");
            if (!load_blob(map_path_buf, &blob)) return false;
            map_path = map_path_buf;
        } else {
            return false;
        }
    }
    if (blob.size < 2) { free_blob(&blob); return false; }
    int width = 0;
    int height = 0;
    size_t source_count = 0;
    bool legacy_map_format = false;
    bool legacy_no_header = false; /* .O16: tile pairs start at byte 0, no flags plane */
    bool use_overview_colors = false;
    char overview_path[1024] = { 0 };
    if (blob.size >= 8) {
        int maybe_width = read_i32_le(blob.bytes + 0);
        int maybe_height = read_i32_le(blob.bytes + 4);
        size_t maybe_count = (size_t)maybe_width * (size_t)maybe_height;
        if (maybe_width > 0 && maybe_height > 0 && maybe_width <= 512 && maybe_height <= 512 &&
            blob.size >= 8 + maybe_count * 2 * 3) {
            width = maybe_width;
            height = maybe_height;
            source_count = maybe_count;
            legacy_map_format = true;
        }
    }
    if (!legacy_map_format) {
        width = (int)blob.bytes[0];
        height = (int)blob.bytes[1];
        source_count = (size_t)width * (size_t)height;
        if (width <= 0 || height <= 0 || width > 512 || height > 512 || blob.size < 2 + source_count) {
            fprintf(stderr, "%s has unsupported Dark Colony map dimensions\n", map_path);
            free_blob(&blob); return false;
        }
        bool blank_mtg = true;
        for (size_t i = 0; i < source_count; ++i) {
            if (blob.bytes[2 + i] != 0) {
                blank_mtg = false;
                break;
            }
        }
        const char *dot = strrchr(map_path, '.');
        if (blank_mtg && dot && strcasecmp(dot, ".MTG") == 0) {
            /* Try .MAP (legacy format with header) first. */
            char alt_path[1024];
            replace_extension(alt_path, sizeof(alt_path), map_path, ".MAP");
            Blob alt_blob;
            bool used_alt = false;
            if (load_blob(alt_path, &alt_blob) && alt_blob.size >= 8) {
                int mw = read_i32_le(alt_blob.bytes + 0);
                int mh = read_i32_le(alt_blob.bytes + 4);
                size_t mc = (size_t)mw * (size_t)mh;
                if (mw > 0 && mh > 0 && mw <= 512 && mh <= 512 && alt_blob.size >= 8 + mc * 2 * 3) {
                    free_blob(&blob);
                    blob = alt_blob;
                    map_path = alt_path;
                    width = mw; height = mh; source_count = mc;
                    legacy_map_format = true;
                    used_alt = true;
                } else {
                    free_blob(&alt_blob);
                }
            }
            /* If no .MAP exists, use the companion terrain overview.  The .O16
               stream is overview/remap data too; treating it as BTS tile IDs
               aliases huge 16-bit values into one repeated tile. */
            if (!used_alt) {
                replace_extension(overview_path, sizeof(overview_path), map_path, ".OVH");
                use_overview_colors = true;
            }
        }
    }

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
        free_blob(&blob); destroy_map(out); return false;
    }
    if (legacy_map_format) {
        const uint8_t *tile_pairs = blob.bytes + (legacy_no_header ? 0 : 8);
        const uint8_t *tile_flags = legacy_no_header ? NULL : blob.bytes + 8 + source_count * 4;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                size_t idx = (size_t)y * (size_t)width + (size_t)x;
                const uint8_t *cell = tile_pairs + idx * 4;
                out->tile_ids[idx] = read_u16_le(cell);
                out->tile_overlays[0][idx] = read_u16_le(cell + 2);
                if (tile_flags) {
                    uint16_t flags = read_u16_le(tile_flags + idx * 2);
                    out->blocked[idx] = (flags & (1u << 9)) ? 1 : 0;
                    out->tile_flip_flags[0][idx] = (flags & (1u << 5)) ? 1 : 0;
                    out->tile_flip_flags[1][idx] = (flags & (1u << 6)) ? 1 : 0;
                }
            }
        }
    } else {
        const uint8_t *mtg_tiles = blob.bytes + 2;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                size_t idx = (size_t)y * (size_t)width + (size_t)x;
                out->tile_ids[idx] = mtg_tiles[idx];
                out->tile_overlays[0][idx] = 0;
            }
        }
    }
    if (use_overview_colors) {
        if (load_dark_colony_overview_colors(overview_path, source_count, &out->cell_colors)) {
            out->render_features |= MAP_RENDER_USE_CELL_COLORS;
        } else {
            fprintf(stderr, "warning: failed to load Dark Colony overview colors from %s\n",
                    overview_path);
        }
    }

    char scn_path[1024];
    replace_extension(scn_path, sizeof(scn_path), map_path, ".SCN");
    char *scn = load_text_file(scn_path);
    if (scn) {
        char first_token[64] = { 0 };
        const char *line_end = strpbrk(scn, "\r\n");
        size_t token_len = line_end ? (size_t)(line_end - scn) : strlen(scn);
        copy_trimmed_token(first_token, sizeof(first_token), scn, token_len);
        char *dot = strrchr(first_token, '.');
        if (dot) *dot = '\0';
        uppercase_trimmed_token(out->tileset_name, sizeof(out->tileset_name),
                                first_token, strlen(first_token));
        load_dark_colony_camera_from_scn(scn, out);
        load_dark_colony_resource_vents_from_scn(scn, out);
        load_dark_colony_beacons_from_scn(scn, out);
        free(scn);
    }

    const char *base = strrchr(map_path, '/');
    base = base ? base + 1 : map_path;
    if (out->tileset_name[0] == '\0') {
        if      (toupper((unsigned char)base[0]) == 'J') strncpy(out->tileset_name, "JUNGLE",   sizeof(out->tileset_name) - 1);
        else if (toupper((unsigned char)base[0]) == 'A') strncpy(out->tileset_name, "ATLANTIS", sizeof(out->tileset_name) - 1);
        else if (toupper((unsigned char)base[0]) == 'H') strncpy(out->tileset_name, "HTRAIN",   sizeof(out->tileset_name) - 1);
        else                                             strncpy(out->tileset_name, "DESERT",   sizeof(out->tileset_name) - 1);
    }
    free_blob(&blob);
    return true;
}

/* ── unit SCN parser ────────────────────────────────────────────────────── */

enum { DARK_COLONY_MAX_GAMESTAT_UNITS = 128 };

typedef struct {
    float speed;
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

static bool dark_colony_root_from_map_path(const char *map_path, char *root, size_t root_size) {
    if (!map_path || !root || root_size == 0) return false;
    snprintf(root, root_size, "%s", map_path);
    char *marker = find_ascii_case_insensitive(root, "/SCENARIO/");
    if (!marker) marker = find_ascii_case_insensitive(root, "\\SCENARIO\\");
    if (!marker) return false;
    *marker = '\0';
    return root[0] != '\0';
}

static float dark_colony_speed_from_gamestat(int speed) {
    return speed > 0 ? (float)speed / 32.0f : 0.0f;
}

static bool dark_colony_map_path_is_multiplayer(const char *map_path) {
    if (!map_path) return false;
    return find_ascii_case_insensitive((char *)map_path, "/MPLAYER/") ||
           find_ascii_case_insensitive((char *)map_path, "\\MPLAYER\\");
}

static void load_dark_colony_unit_config(const char *map_path,
                                         DarkColonyUnitConfig configs[DARK_COLONY_MAX_GAMESTAT_UNITS]) {
    memset(configs, 0, sizeof(DarkColonyUnitConfig) * DARK_COLONY_MAX_GAMESTAT_UNITS);

    char root[1024];
    if (!dark_colony_root_from_map_path(map_path, root, sizeof(root))) return;

    char gamestat_path[1024];
    snprintf(gamestat_path, sizeof(gamestat_path), "%s/GAMESTAT/GAMESTAT.TXT", root);
    char *text = load_text_file(gamestat_path);
    if (!text) return;

    int type_index = 0;
    for (char *line = text; line && *line && type_index < DARK_COLONY_MAX_GAMESTAT_UNITS;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char nl = *next; *next++ = '\0';
            if (nl == '\r' && *next == '\n') next++;
        }

        char token[256] = { 0 };
        copy_trimmed_token(token, sizeof(token), line, strlen(line));
        if (token[0] != '\0' && token[0] != '%') {
            char sprite[32] = { 0 };
            int race = 0, turn = 0, speed = 0;
            if (!isdigit((unsigned char)token[0]) &&
                sscanf(token, "%31s %d %d %d", sprite, &race, &turn, &speed) == 4) {
                (void)race;
                (void)turn;
                configs[type_index].speed = dark_colony_speed_from_gamestat(speed);
                type_index++;
            }
        }
        line = next;
    }
    free(text);
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
    if (type >= 16 && type <= 22) return "SPRITES/BUILDNG.SPR";
    if (type >= 28 && type <= 34) return "SPRITES/ALIEN1.SPR";
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
    if (type >= 16 && type <= 22) return type - 16;
    if (type >= 28 && type <= 34) return type - 28;
    return 0;
}

static int dark_colony_city_unit_type_for_slot(int race, int slot) {
    static const int human_city_types[] = { 16, 17, 20, 18, 22, 21, 19 };
    static const int alien_city_types[] = { 28, 29, 32, 30, 34, 33, 31 };
    int count = (int)(sizeof(human_city_types) / sizeof(human_city_types[0]));
    if (slot < 0 || slot >= count) return 0;
    return race == 1 ? alien_city_types[slot] : human_city_types[slot];
}

static bool append_dark_colony_initial_unit(Unit *units, int *count, int max_units,
                                            int x, int y, int type, int team, int status,
                                            int race, const DarkColonyUnitConfig *unit_config,
                                            bool *player_selected,
                                            bool *player_has_exploiter,
                                            bool *player_anchor_set,
                                            int *player_anchor_x,
                                            int *player_anchor_y);

static bool append_dark_colony_city_building(Unit *units, int *count, int max_units,
                                             int x, int y, int team, int race,
                                             int slot,
                                             const DarkColonyUnitConfig *unit_config,
                                             bool *player_selected,
                                             bool *player_has_exploiter,
                                             bool *player_anchor_set,
                                             int *player_anchor_x,
                                             int *player_anchor_y) {
    int type = dark_colony_city_unit_type_for_slot(race, slot);
    if (type <= 0) return false;
    return append_dark_colony_initial_unit(units, count, max_units, x, y, type,
                                           team, 0, race, unit_config,
                                           player_selected, player_has_exploiter,
                                           player_anchor_set, player_anchor_x,
                                           player_anchor_y);
}

static bool append_dark_colony_initial_unit(Unit *units, int *count, int max_units,
                                            int x, int y, int type, int team, int status,
                                            int race, const DarkColonyUnitConfig *unit_config,
                                            bool *player_selected,
                                            bool *player_has_exploiter,
                                            bool *player_anchor_set,
                                            int *player_anchor_x,
                                            int *player_anchor_y) {
    if (!units || !count || *count >= max_units || x < 0 || y < 0 || status < 0) {
        return false;
    }
    const char *sprite = dark_colony_unit_sprite_for_type(type, race);
    int mobj_type = dark_colony_mobj_type_for_type(type, race);
    if (!sprite || mobj_type <= 0) return false;

    Unit *u = &units[*count];
    memset(u, 0, sizeof(*u));
    u->gx = (float)x + 0.5f;
    u->gy = (float)y + 0.5f;
    u->sprite_id = -1;
    u->attack_target = -1;
    u->harvest_target = -1;
    if (type >= 0 && type < DARK_COLONY_MAX_GAMESTAT_UNITS && unit_config) {
        u->speed = unit_config[type].speed;
    }
    u->type_id = (uint16_t)mobj_type;
    u->owner = (team == 0 || mobj_type == MT_DC_COMMS_DISH) ? 0 : 1;
    u->hp = status;
    u->selected = u->owner == 0 && mobj_type != MT_DC_COMMS_DISH &&
        (mobj_type < MT_DC_BUILDING_BASE) && player_selected && !*player_selected;
    if (u->selected) *player_selected = true;
    u->frame = dark_colony_unit_frame_for_type(type);
    snprintf(u->sprite_name, sizeof(u->sprite_name), "%s", sprite);
    if (u->owner == 0) {
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
    char *text = load_text_file(scn_path);
    if (!text) return 0;

    DarkColonyUnitConfig unit_config[DARK_COLONY_MAX_GAMESTAT_UNITS];
    load_dark_colony_unit_config(map_path, unit_config);

    enum { DC_MAX_SCN_TEAMS = 16, DC_CITY_SLOTS = 7 };
    int team_race[DC_MAX_SCN_TEAMS] = { 0 };
    int team_active[DC_MAX_SCN_TEAMS] = { 0 };
    int team_ai_slots[DC_MAX_SCN_TEAMS][DC_CITY_SLOTS][2] = { 0 };
    int team_ai_slot_count[DC_MAX_SCN_TEAMS] = { 0 };
    int team_city_enabled[DC_MAX_SCN_TEAMS][DC_CITY_SLOTS] = { 0 };
    int current_team = -1, team_count = 0;
    bool expect_race = false, object_mode = false;
    int trailing_blank_lines = 0, count = 0;
    int aislot_lines = 0;
    int city_lines = 0;
    bool city_buildings_added = false;
    bool player_has_exploiter = false;
    bool player_anchor_set = false;
    bool player_selected = false;
    int player_anchor_x = 0;
    int player_anchor_y = 0;
    int map_width = 0;
    int map_height = 0;
    dark_colony_map_dimensions(map_path, &map_width, &map_height);
    (void)map_width;

    for (char *line = text; line && *line && count < max_units;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char nl = *next; *next++ = '\0';
            if (nl == '\r' && *next == '\n') next++;
        }
        char token[128] = { 0 };
        copy_trimmed_token(token, sizeof(token), line, strlen(line));
        if (token[0] == '\0') {
            if (team_count >= 8 && !object_mode && ++trailing_blank_lines >= 2) {
                object_mode = true;
                if (!city_buildings_added) {
                    for (int team = 0; team < DC_MAX_SCN_TEAMS; ++team) {
                        if (!team_active[team]) continue;
                        for (int slot = 0; slot < team_ai_slot_count[team] && slot < DC_CITY_SLOTS; ++slot) {
                            if (!team_city_enabled[team][slot]) continue;
                            append_dark_colony_city_building(units, &count, max_units,
                                                             team_ai_slots[team][slot][0],
                                                             team_ai_slots[team][slot][1],
                                                             team, team_race[team], slot,
                                                             unit_config,
                                                             &player_selected,
                                                             &player_has_exploiter,
                                                             &player_anchor_set,
                                                             &player_anchor_x,
                                                             &player_anchor_y);
                        }
                    }
                    city_buildings_added = true;
                }
            }
            line = next; continue;
        }
        if (strncmp(token, "TEAM ", 5) == 0) {
            int active = 0;
            if (sscanf(token, "TEAM %d %d", &current_team, &active) >= 1 &&
                current_team >= 0 && current_team < DC_MAX_SCN_TEAMS) {
                team_active[current_team] = active != 0;
                team_count++; expect_race = true;
            } else { current_team = -1; expect_race = false; }
            aislot_lines = 0;
            city_lines = 0;
            trailing_blank_lines = 0; line = next; continue;
        }
        if (!object_mode && expect_race) {
            int race = 0;
            if (sscanf(token, "%d", &race) == 1 && current_team >= 0 && current_team < DC_MAX_SCN_TEAMS) {
                team_race[current_team] = race;
                expect_race = false; trailing_blank_lines = 0;
                line = next; continue;
            }
        }
        if (!object_mode && strcmp(token, "%AISlots") == 0) {
            aislot_lines = current_team >= 0 && current_team < DC_MAX_SCN_TEAMS ? 2 : 0;
            city_lines = 0;
            trailing_blank_lines = 0;
            line = next; continue;
        }
        if (!object_mode && aislot_lines > 0) {
            int x = 0, y = 0;
            if (sscanf(token, "%d %d", &x, &y) == 2 &&
                current_team >= 0 && current_team < DC_MAX_SCN_TEAMS &&
                team_ai_slot_count[current_team] < DC_CITY_SLOTS &&
                (x != 0 || y != 0)) {
                int slot = team_ai_slot_count[current_team]++;
                team_ai_slots[current_team][slot][0] = x;
                team_ai_slots[current_team][slot][1] = y;
            }
            aislot_lines--;
            trailing_blank_lines = 0;
            line = next; continue;
        }
        if (!object_mode && strcmp(token, "%City") == 0) {
            city_lines = current_team >= 0 && current_team < DC_MAX_SCN_TEAMS ? 1 : 0;
            aislot_lines = 0;
            trailing_blank_lines = 0;
            line = next; continue;
        }
        if (!object_mode && city_lines > 0) {
            int v[12] = { 0 };
            int parsed = sscanf(token, "%d %d %d %d %d %d %d %d %d %d %d %d",
                                &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                                &v[6], &v[7], &v[8], &v[9], &v[10], &v[11]);
            if (parsed > 0 && current_team >= 0 && current_team < DC_MAX_SCN_TEAMS) {
                for (int slot = 0; slot < DC_CITY_SLOTS; ++slot) {
                    int value_index = slot * 2;
                    if (value_index >= parsed) break;
                    team_city_enabled[current_team][slot] = v[value_index] > 0;
                }
            }
            city_lines--;
            trailing_blank_lines = 0;
            line = next; continue;
        }
        if (!object_mode) { trailing_blank_lines = 0; line = next; continue; }

        int x = 0, y = 0, type = 0, team = 0, status = 0, extra = 0;
        if (sscanf(line, " %d %d %d %d %d %d", &x, &y, &type, &team, &status, &extra) == 6 &&
            team >= 0 && team < 16 && x >= 0 && y >= 0) {
            int map_y = type == 86 ? dark_colony_y_to_map_height(map_height, y) : y;
            append_dark_colony_initial_unit(units, &count, max_units, x, map_y, type,
                                            team, status, team_race[team], unit_config,
                                            &player_selected, &player_has_exploiter,
                                            &player_anchor_set, &player_anchor_x,
                                            &player_anchor_y);
        }
        line = next;
    }
    if (dark_colony_map_path_is_multiplayer(map_path) && !player_has_exploiter &&
        player_anchor_set && count < max_units) {
        Unit *u = &units[count];
        memset(u, 0, sizeof(*u));
        u->gx = (float)(player_anchor_x + 2) + 0.5f;
        u->gy = (float)player_anchor_y + 0.5f;
        if (6 < DARK_COLONY_MAX_GAMESTAT_UNITS) u->speed = unit_config[6].speed;
        u->type_id = MT_DC_EXPLOITER;
        u->owner = 0;
        u->selected = true;
        snprintf(u->sprite_name, sizeof(u->sprite_name), "SPRITES/EXPL.SPR");
        count++;
    }
    free(text);
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
