#define _DEFAULT_SOURCE
#include "engine.h"
#include "plugin.h"
#include "renderer.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DEFAULT_DATA_ROOT "data/REIGN/dark"
#define DEFAULT_UNIT_SPR "ucfcnst0.spr"

static void render_dark_reign_edges_for_cell(App *app, const GameMap *map, const Tileset *tileset,
                                             int x, int y, int dx, int dy);

typedef struct {
    char name[28];
    int32_t offset;
    int32_t size;
} FtgEntry;

typedef struct {
    uint8_t *bytes;
    size_t size;
    FtgEntry *entries;
    int count;
} FtgArchive;

static bool load_dark_palette_with_multipliers(const char *path, uint32_t colors[256],
                                               int standard_multiplier, int terrain_multiplier) {
    Blob blob;
    if (!load_blob(path, &blob)) return false;
    if (blob.size < 8 + 256 * 3 || memcmp(blob.bytes, "PALS", 4) != 0) {
        fprintf(stderr, "%s is not a Dark Reign PALS palette\n", path);
        free_blob(&blob);
        return false;
    }

    const uint8_t *p = blob.bytes + 8;
    const uint8_t *r = p;
    const uint8_t *g = p + 256;
    const uint8_t *b = p + 512;
    for (int i = 0; i < 256; ++i) {
        if (i == 0) {
            colors[i] = 0x00000000u;
            continue;
        }
        int mult = (i < 160 || i == 255) ? standard_multiplier : terrain_multiplier;
        uint8_t rr = (uint8_t)clamp255((int)r[i] * mult + 1);
        uint8_t gg = (uint8_t)clamp255((int)g[i] * mult + 1);
        uint8_t bb = (uint8_t)clamp255((int)b[i] * mult + 1);
        colors[i] = 0xff000000u | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | bb;
    }
    colors[47] = 0x70000000u;
    free_blob(&blob);
    return true;
}

static bool load_dark_sprite_palette(const char *path, uint32_t colors[256]) {
    return load_dark_palette_with_multipliers(path, colors, 6, 6);
}

static bool load_dark_terrain_palette(const char *path, uint32_t colors[256]) {
    int terrain_multiplier = (strstr(path, "BARREN") || strstr(path, "JUNGLE")) ? 6 : 4;
    return load_dark_palette_with_multipliers(path, colors, 4, terrain_multiplier);
}

static void dark_til_combine_add(const uint8_t *a, const uint8_t *b, uint8_t *out) {
    for (int i = 0; i < TILE_PIX_W * TILE_PIX_H; ++i) {
        int value = (int)a[i] + (int)b[i];
        out[i] = (uint8_t)(value > 255 ? 255 : value);
    }
}

static void dark_til_combine_min(const uint8_t *a, const uint8_t *b, uint8_t *out) {
    for (int i = 0; i < TILE_PIX_W * TILE_PIX_H; ++i) {
        out[i] = a[i] < b[i] ? a[i] : b[i];
    }
}

static void dark_til_mask_tile(uint32_t *dst, const uint32_t *src, const uint8_t *mask) {
    for (int i = 0; i < TILE_PIX_W * TILE_PIX_H; ++i) {
        uint32_t rgb = src[i] & 0x00ffffffu;
        uint32_t alpha = (uint32_t)(mask[i] > 63 ? 255 : mask[i] * 4);
        dst[i] = (alpha << 24) | rgb;
    }
}

static void dark_til_shadow_tile(uint32_t *dst, const uint8_t *mask, uint8_t alpha) {
    for (int i = 0; i < TILE_PIX_W * TILE_PIX_H; ++i) {
        uint32_t a = mask ? (mask[i] < alpha ? mask[i] : alpha) : alpha;
        dst[i] = a << 24;
    }
}

static bool load_dark_tileset(SDL_Renderer *renderer, const char *path, const uint32_t palette[256], Tileset *out) {
    memset(out, 0, sizeof(*out));
    Blob blob;
    if (!load_blob(path, &blob)) return false;
    if (blob.size < 8 || memcmp(blob.bytes, "TILE", 4) != 0) {
        fprintf(stderr, "%s is not a Dark Reign TILE tileset\n", path);
        free_blob(&blob);
        return false;
    }
    size_t tile_bytes = (size_t)read_u32_le(blob.bytes + 4);
    if (tile_bytes != TILE_PIX_W * TILE_PIX_H || blob.size < 8 + tile_bytes) {
        fprintf(stderr, "%s has unsupported tile dimensions/count\n", path);
        free_blob(&blob);
        return false;
    }

    const int max_frames = 1103;
    uint32_t *frame_pixels = calloc((size_t)max_frames * TILE_PIX_W * TILE_PIX_H, sizeof(uint32_t));
    uint8_t *mask_frames = calloc((size_t)256 * TILE_PIX_W * TILE_PIX_H, sizeof(uint8_t));
    if (!frame_pixels || !mask_frames) {
        free(frame_pixels);
        free(mask_frames);
        free_blob(&blob);
        return false;
    }

    int count = 0;
    size_t pos = 8;
    const size_t normal_chunk = 1 + tile_bytes;
    #define ADD_INDEXED_FRAME(SRC) do { \
        if (count < max_frames) { \
            indexed_to_rgba(frame_pixels + (size_t)count * tile_bytes, (SRC), tile_bytes, palette); \
            count++; \
        } \
    } while (0)

    for (int i = 0; i < 128 && pos + normal_chunk <= blob.size; ++i) {
        ADD_INDEXED_FRAME(blob.bytes + pos + 1);
        pos += normal_chunk;
    }
    for (int i = 0; i < 64 && pos + tile_bytes <= blob.size; ++i) {
        ADD_INDEXED_FRAME(blob.bytes + pos);
        pos += tile_bytes;
    }
    for (int art = 0; art < 4 && pos < blob.size; ++art) {
        pos += normal_chunk;
        for (int shore = 0; shore < 14 && pos + normal_chunk <= blob.size; ++shore) {
            ADD_INDEXED_FRAME(blob.bytes + pos + 1);
            pos += normal_chunk;
        }
        pos += normal_chunk;
    }

    for (int mask = 0; mask < 256 && pos + tile_bytes <= blob.size; ++mask) {
        if (mask % 4 == 0 && pos + 4 <= blob.size) pos += 4;
        if (pos + tile_bytes > blob.size) break;
        memcpy(mask_frames + (size_t)mask * tile_bytes, blob.bytes + pos, tile_bytes);
        pos += tile_bytes;
    }
    pos += 4 * tile_bytes;

    const int corner_sets[4][4] = {
        { 0, 1, 2, 3 },
        { 64, 65, 66, 67 },
        { 128, 129, 130, 131 },
        { 192, 193, 194, 195 },
    };
    uint8_t north[TILE_PIX_W * TILE_PIX_H], east[TILE_PIX_W * TILE_PIX_H];
    uint8_t south[TILE_PIX_W * TILE_PIX_H], west[TILE_PIX_W * TILE_PIX_H];
    uint8_t ne_inner[TILE_PIX_W * TILE_PIX_H], nw_inner[TILE_PIX_W * TILE_PIX_H];
    uint8_t sw_inner[TILE_PIX_W * TILE_PIX_H], se_inner[TILE_PIX_W * TILE_PIX_H];
    uint8_t ne_sw_bridge[TILE_PIX_W * TILE_PIX_H], nw_se_bridge[TILE_PIX_W * TILE_PIX_H];
    const uint8_t *masks[14];
    for (int set = 0; set < 4; ++set) {
        const uint8_t *se = mask_frames + (size_t)corner_sets[set][0] * tile_bytes;
        const uint8_t *sw = mask_frames + (size_t)corner_sets[set][1] * tile_bytes;
        const uint8_t *nw = mask_frames + (size_t)corner_sets[set][2] * tile_bytes;
        const uint8_t *ne = mask_frames + (size_t)corner_sets[set][3] * tile_bytes;
        dark_til_combine_add(nw, ne, north);
        dark_til_combine_add(se, ne, east);
        dark_til_combine_add(sw, se, south);
        dark_til_combine_add(sw, nw, west);
        dark_til_combine_add(north, east, ne_inner);
        dark_til_combine_add(north, west, nw_inner);
        dark_til_combine_add(south, west, sw_inner);
        dark_til_combine_add(south, east, se_inner);
        dark_til_combine_min(ne_inner, sw_inner, ne_sw_bridge);
        dark_til_combine_min(nw_inner, se_inner, nw_se_bridge);

        masks[0] = se;
        masks[1] = sw;
        masks[2] = nw;
        masks[3] = ne;
        masks[4] = south;
        masks[5] = west;
        masks[6] = north;
        masks[7] = east;
        masks[8] = se_inner;
        masks[9] = sw_inner;
        masks[10] = nw_inner;
        masks[11] = ne_inner;
        masks[12] = ne_sw_bridge;
        masks[13] = nw_se_bridge;

        for (int tile_index = 2; tile_index < 16; ++tile_index) {
            const uint32_t *source = frame_pixels + (size_t)(tile_index * 8) * tile_bytes;
            for (int mask = 0; mask < 14 && count < max_frames; ++mask) {
                dark_til_mask_tile(frame_pixels + (size_t)count * tile_bytes, source, masks[mask]);
                count++;
            }
        }
    }

    const int sea_tiles[14] = { 4, 9, 18, 35, 12, 25, 50, 36, 44, 28, 57, 52, 20, 41 };
    const int sea_tile_masks[14][4] = {
        { 192, 206, 220, 234 },
        { 193, 207, 221, 235 },
        { 195, 209, 223, 237 },
        { 199, 213, 227, 241 },
        { 194, 208, 222, 236 },
        { 197, 211, 225, 239 },
        { 203, 217, 231, 245 },
        { 200, 214, 228, 242 },
        { 202, 216, 230, 244 },
        { 198, 212, 226, 240 },
        { 205, 219, 233, 247 },
        { 204, 218, 232, 246 },
        { 196, 210, 224, 238 },
        { 201, 215, 229, 243 },
    };
    for (int mask_index = 0; mask_index < 14; ++mask_index) {
        const uint8_t *mask = mask_frames + (size_t)sea_tiles[mask_index] * tile_bytes;
        for (int sea = 0; sea < 4 && count < max_frames; ++sea) {
            const uint32_t *source = frame_pixels + (size_t)sea_tile_masks[mask_index][sea] * tile_bytes;
            dark_til_mask_tile(frame_pixels + (size_t)count * tile_bytes, source, mask);
            count++;
        }
    }

    const int shadow_set[4] = { 64, 65, 66, 67 };
    const uint8_t *se = mask_frames + (size_t)shadow_set[0] * tile_bytes;
    const uint8_t *sw = mask_frames + (size_t)shadow_set[1] * tile_bytes;
    const uint8_t *nw = mask_frames + (size_t)shadow_set[2] * tile_bytes;
    const uint8_t *ne = mask_frames + (size_t)shadow_set[3] * tile_bytes;
    dark_til_combine_add(nw, ne, north);
    dark_til_combine_add(se, ne, east);
    dark_til_combine_add(sw, se, south);
    dark_til_combine_add(sw, nw, west);
    dark_til_combine_add(north, east, ne_inner);
    dark_til_combine_add(north, west, nw_inner);
    dark_til_combine_add(south, west, sw_inner);
    dark_til_combine_add(south, east, se_inner);
    dark_til_combine_min(ne_inner, sw_inner, ne_sw_bridge);
    dark_til_combine_min(nw_inner, se_inner, nw_se_bridge);
    masks[0] = north; masks[1] = east; masks[2] = south; masks[3] = west;
    masks[4] = nw; masks[5] = ne; masks[6] = sw; masks[7] = se;
    masks[8] = sw_inner; masks[9] = se_inner; masks[10] = nw_inner; masks[11] = ne_inner;
    masks[12] = nw_se_bridge; masks[13] = ne_sw_bridge;
    for (int i = 0; i < 14 && count < max_frames; ++i) {
        dark_til_shadow_tile(frame_pixels + (size_t)count * tile_bytes, masks[i], 35);
        count++;
    }
    if (count < max_frames) {
        dark_til_shadow_tile(frame_pixels + (size_t)count * tile_bytes, NULL, 35);
        count++;
    }
    #undef ADD_INDEXED_FRAME

    int rows = (count + TILE_ATLAS_COLS - 1) / TILE_ATLAS_COLS;
    int atlas_w = TILE_ATLAS_COLS * TILE_PIX_W;
    int atlas_h = rows * TILE_PIX_H;
    uint32_t *rgba = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    if (!rgba) {
        free(frame_pixels);
        free(mask_frames);
        free_blob(&blob);
        return false;
    }

    for (int tile = 0; tile < count; ++tile) {
        int tx = (tile % TILE_ATLAS_COLS) * TILE_PIX_W;
        int ty = (tile / TILE_ATLAS_COLS) * TILE_PIX_H;
        const uint32_t *tile_src = frame_pixels + (size_t)tile * tile_bytes;
        for (int y = 0; y < TILE_PIX_H; ++y) {
            for (int x = 0; x < TILE_PIX_W; ++x) {
                rgba[(ty + y) * atlas_w + tx + x] = tile_src[y * TILE_PIX_W + x];
            }
        }
    }

    out->texture = rgba_texture(renderer, rgba, atlas_w, atlas_h, false);
    out->count = count;
    out->atlas_cols = TILE_ATLAS_COLS;
    out->tile_w = TILE_PIX_W;
    out->tile_h = TILE_PIX_H;
    out->draw_y_offset = 0;
    free(rgba);
    free(frame_pixels);
    free(mask_frames);
    free_blob(&blob);
    return out->texture != NULL;
}

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

static void dark_reign_add_water_animations(Tileset *tileset) {
    const int frame_ms = 180;
    for (int variation = 0; variation < 8 && variation < tileset->count; ++variation) {
        int frames[4] = {
            variation,
            (variation + 1) & 7,
            (variation + 2) & 7,
            (variation + 1) & 7,
        };
        tileset_add_animation(tileset, variation, frames, 4, frame_ms);
    }

    for (int group = 0; group < 14; ++group) {
        int base = 1032 + group * 4;
        if (base + 3 >= tileset->count) break;
        int frames[4] = { base, base + 1, base + 2, base + 3 };
        for (int i = 0; i < 4; ++i) {
            int rotated[4] = {
                frames[i],
                frames[(i + 1) & 3],
                frames[(i + 2) & 3],
                frames[(i + 3) & 3],
            };
            tileset_add_animation(tileset, frames[i], rotated, 4, frame_ms);
        }
    }
}

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

enum { DARK_COLONY_WATER_WAVE_COUNT = 7 };

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
    int water = 0;
    int opaque = 0;
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
    int wave_count = 0;
    int index_pos = -1;
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
            int dx = dst_x + x;
            int dy = dst_y + y;
            if (dx >= 0 && dy >= 0 && dx < dst_w && dy < dst_h) {
                dst[dy * dst_w + dx] = color;
            }
        }
    }
}

static bool load_dark_colony_tileset(SDL_Renderer *renderer, const char *path, Tileset *out) {
    memset(out, 0, sizeof(*out));
    Blob blob;
    if (!load_blob(path, &blob)) return false;
    const int tile_w = 32;
    const int tile_h = 32;
    const int palette_count = 256;
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

    int max_key = 0;
    int animated_count = 0;
    uint8_t *animate_tile = calloc((size_t)count, sizeof(uint8_t));
    uint32_t *record_keys = calloc((size_t)count, sizeof(uint32_t));
    if (!animate_tile || !record_keys) {
        free(animate_tile);
        free(record_keys);
        free_blob(&blob);
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
        free(tile_lookup);
        free(rgba);
        free(animate_tile);
        free(record_keys);
        free_blob(&blob);
        return false;
    }
    for (int i = 0; i < lookup_count; ++i) tile_lookup[i] = -1;
    int extra_tile = count;
    int extra_key = synthetic_key_base;
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
    free(rgba);
    free(animate_tile);
    free(record_keys);
    free_blob(&blob);
    if (!out->texture) {
        destroy_tileset(out);
        return false;
    }
    return true;
}

static void detect_tileset_from_mm(const char *map_path, char *tileset, size_t tileset_size) {
    strncpy(tileset, "BARREN", tileset_size - 1);
    tileset[tileset_size - 1] = '\0';

    char mm_path[1024];
    strncpy(mm_path, map_path, sizeof(mm_path) - 1);
    mm_path[sizeof(mm_path) - 1] = '\0';
    char *slash = strrchr(mm_path, '/');
    if (!slash) return;
    slash[1] = '\0';
    strncat(mm_path, "TACTICS.MM", sizeof(mm_path) - strlen(mm_path) - 1);

    Blob blob;
    if (!load_blob(mm_path, &blob)) return;
    if (blob.size >= 28) {
        char raw[17];
        memcpy(raw, blob.bytes + 12, 16);
        raw[16] = '\0';
        size_t n = strnlen(raw, sizeof(raw));
        while (n > 0 && isspace((unsigned char)raw[n - 1])) raw[--n] = '\0';
        for (size_t i = 0; i < n; ++i) raw[i] = (char)toupper((unsigned char)raw[i]);
        if (n > 0) {
            strncpy(tileset, raw, tileset_size - 1);
            tileset[tileset_size - 1] = '\0';
        }
    }
    free_blob(&blob);
}

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

static void uppercase_trimmed_token(char *dst, size_t dst_size, const char *src, size_t len) {
    while (len > 0 && isspace((unsigned char)*src)) {
        src++;
        len--;
    }
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;
    if (len >= dst_size) len = dst_size - 1;
    for (size_t i = 0; i < len; ++i) dst[i] = (char)toupper((unsigned char)src[i]);
    dst[len] = '\0';
}

typedef struct {
    const char *type_name;
    const char *sprite_name;
    const char *shadow_name;
    int footprint_w;
    int footprint_h;
    bool solid;
} DarkReignDecorationSpec;

static const DarkReignDecorationSpec DARK_REIGN_DECORATION_SPECS[] = {
    { "clif1", "aoclf000.spr", "aoclf0sh.spr", 1, 4, true },
    { "clif2", "aoclf001.spr", "aoclf1sh.spr", 1, 3, true },
    { "clif3", "aoclf002.spr", "aoclf2sh.spr", 1, 3, true },
    { "clif4", "aoclf003.spr", "aoclf3sh.spr", 3, 4, true },
    { "clif5", "aoclf004.spr", "aoclf4sh.spr", 3, 5, true },
    { "clif6", "aoclf005.spr", "aoclf5sh.spr", 3, 3, true },
    { "plnt1", "aopln000.spr", "aopln0sh.spr", 1, 1, false },
    { "plnt2", "aopln001.spr", "aopln1sh.spr", 1, 1, false },
    { "plnt3", "aopln002.spr", "aopln2sh.spr", 1, 1, false },
    { "rock1", "aoroc000.spr", "aoroc0sh.spr", 1, 1, true },
    { "rock2", "aoroc001.spr", "aoroc1sh.spr", 1, 1, true },
    { "rock3", "aoroc002.spr", "aoroc2sh.spr", 1, 1, true },
    { "rock4", "aoroc003.spr", "aoroc3sh.spr", 3, 3, true },
    { "rock5", "aoroc004.spr", "aoroc4sh.spr", 3, 3, true },
    { "rock6", "aoroc005.spr", "aoroc5sh.spr", 3, 3, true },
    { "tree1", "aotre000.spr", "aotre0sh.spr", 1, 1, true },
    { "tree2", "aotre001.spr", "aotre1sh.spr", 1, 1, true },
    { "tree3", "aotre002.spr", "aotre2sh.spr", 1, 1, true },
    { "tree4", "aotre003.spr", "aotre3sh.spr", 1, 1, true },
    { "tree5", "aotre004.spr", "aotre4sh.spr", 1, 1, true },
    { "tree6", "aotre005.spr", "aotre5sh.spr", 1, 1, true },
    { "rubble1", "aorub000.spr", "aorub0sh.spr", 1, 1, false },
    { "rubble2", "aorub001.spr", "aorub1sh.spr", 1, 1, false },
    { "rubble3", "aorub002.spr", "aorub2sh.spr", 1, 1, false },
    { "water1", "aowtr000.spr", "aowtr0sh.spr", 1, 1, false },
    { "water2", "aowtr001.spr", "aowtr1sh.spr", 1, 1, false },
    { "water3", "aowtr002.spr", "aowtr2sh.spr", 1, 1, false },
    { "impww", "ncwel1l0.spr", "", 3, 3, true },
    { "impmn", "ncmin1l0.spr", "", 3, 3, true },
};

typedef struct {
    char sprite_name[32];
    char shadow_name[32];
    int footprint_w;
    int footprint_h;
    bool solid;
} DarkReignVisualSpec;

typedef struct {
    char *units;
    char *buildings;
    char *overlay;
    char *animate;
} DarkReignDefinitions;

static const char *find_case_insensitive(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) return haystack;
    size_t needle_len = strlen(needle);
    for (const char *p = haystack; *p; ++p) {
        if (strncasecmp(p, needle, needle_len) == 0) return p;
    }
    return NULL;
}

static const char *find_case_insensitive_n(const char *haystack, size_t haystack_len, const char *needle) {
    if (!haystack || !needle || !needle[0]) return haystack;
    size_t needle_len = strlen(needle);
    if (needle_len > haystack_len) return NULL;
    for (size_t i = 0; i + needle_len <= haystack_len; ++i) {
        if (strncasecmp(haystack + i, needle, needle_len) == 0) return haystack + i;
    }
    return NULL;
}

static bool dark_reign_is_commented_call(const char *body_start, const char *hit) {
    const char *p = hit;
    while (p > body_start && p[-1] != '\n' && p[-1] != '\r') p--;
    while (p < hit) {
        if (*p == ';') return true;
        p++;
    }
    return false;
}

static void copy_trimmed_token(char *dst, size_t dst_size, const char *src, size_t len) {
    while (len > 0 && isspace((unsigned char)*src)) {
        src++;
        len--;
    }
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static char *load_text_file(const char *path) {
    Blob blob;
    if (!load_blob(path, &blob)) return NULL;
    char *text = malloc(blob.size + 1);
    if (!text) {
        free_blob(&blob);
        return NULL;
    }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';
    free_blob(&blob);
    return text;
}

static void dark_reign_root_from_map(const char *map_path, char *root, size_t root_size) {
    const char *scenario = find_case_insensitive(map_path, "/scenario/");
    if (!scenario) {
        snprintf(root, root_size, "%s", DEFAULT_DATA_ROOT);
        return;
    }
    size_t len = (size_t)(scenario - map_path);
    if (len >= root_size) len = root_size - 1;
    memcpy(root, map_path, len);
    root[len] = '\0';
}

static void dark_reign_load_definitions(const char *map_path, DarkReignDefinitions *defs) {
    memset(defs, 0, sizeof(*defs));
    char root[1024];
    char path[1024];
    dark_reign_root_from_map(map_path, root, sizeof(root));

    path_join(path, sizeof(path), root, "deftxt/UNITS.TXT");
    defs->units = load_text_file(path);
    path_join(path, sizeof(path), root, "deftxt/BUILD.TXT");
    defs->buildings = load_text_file(path);
    path_join(path, sizeof(path), root, "deftxt/OVERLAY.TXT");
    defs->overlay = load_text_file(path);
    path_join(path, sizeof(path), root, "deftxt/ANIMATE.TXT");
    defs->animate = load_text_file(path);
}

static void dark_reign_free_definitions(DarkReignDefinitions *defs) {
    free(defs->units);
    free(defs->buildings);
    free(defs->overlay);
    free(defs->animate);
    memset(defs, 0, sizeof(*defs));
}

static bool dark_reign_find_definition_block(const char *text, const char *define_call,
                                             const char *type_name, const char **body,
                                             size_t *body_len) {
    if (!text) return false;
    const char *cursor = text;
    while ((cursor = find_case_insensitive(cursor, define_call)) != NULL) {
        const char *open = strchr(cursor, '(');
        const char *close = open ? strchr(open + 1, ')') : NULL;
        if (!open || !close) {
            cursor += strlen(define_call);
            continue;
        }

        char candidate[96];
        copy_trimmed_token(candidate, sizeof(candidate), open + 1, (size_t)(close - open - 1));
        if (strcasecmp(candidate, type_name) == 0) {
            const char *brace = strchr(close + 1, '{');
            if (!brace) return false;
            int depth = 0;
            for (const char *p = brace; *p; ++p) {
                if (*p == '{') {
                    depth++;
                } else if (*p == '}') {
                    depth--;
                    if (depth == 0) {
                        *body = brace + 1;
                        *body_len = (size_t)(p - (brace + 1));
                        return true;
                    }
                }
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
                if (end > arg) {
                    copy_trimmed_token(dst, dst_size, arg, (size_t)(end - arg));
                    return dst[0] != '\0';
                }
            }
        }
        const char *next = hit + strlen(call);
        remaining = (size_t)((body + body_len) - next);
        cursor = next;
    }
    return false;
}

static bool dark_reign_resolve_animation_sprite(const DarkReignDefinitions *defs,
                                                const char *animation_name,
                                                char *sprite_name, size_t sprite_name_size) {
    const char *body = NULL;
    size_t body_len = 0;
    if (!dark_reign_find_definition_block(defs->animate, "DefineAnimationType",
                                          animation_name, &body, &body_len)) {
        return false;
    }
    return dark_reign_find_call_arg(body, body_len, "SetSprite", sprite_name, sprite_name_size);
}

static bool dark_reign_visual_from_static(const char *type_name, DarkReignVisualSpec *out) {
    size_t count = sizeof(DARK_REIGN_DECORATION_SPECS) / sizeof(DARK_REIGN_DECORATION_SPECS[0]);
    for (size_t i = 0; i < count; ++i) {
        if (strcasecmp(DARK_REIGN_DECORATION_SPECS[i].type_name, type_name) == 0) {
            snprintf(out->sprite_name, sizeof(out->sprite_name), "%s", DARK_REIGN_DECORATION_SPECS[i].sprite_name);
            snprintf(out->shadow_name, sizeof(out->shadow_name), "%s", DARK_REIGN_DECORATION_SPECS[i].shadow_name);
            out->footprint_w = DARK_REIGN_DECORATION_SPECS[i].footprint_w;
            out->footprint_h = DARK_REIGN_DECORATION_SPECS[i].footprint_h;
            out->solid = DARK_REIGN_DECORATION_SPECS[i].solid;
            return true;
        }
    }
    return false;
}

static bool dark_reign_resolve_unit_visual(const DarkReignDefinitions *defs, const char *type_name,
                                           DarkReignVisualSpec *out) {
    memset(out, 0, sizeof(*out));
    const char *body = NULL;
    size_t body_len = 0;
    if (!dark_reign_find_definition_block(defs->units, "DefineUnitType", type_name, &body, &body_len)) {
        return dark_reign_visual_from_static(type_name, out);
    }
    if (!dark_reign_find_call_arg(body, body_len, "SetImage", out->sprite_name, sizeof(out->sprite_name))) {
        return false;
    }
    dark_reign_find_call_arg(body, body_len, "SetShadowImage", out->shadow_name, sizeof(out->shadow_name));
    out->footprint_w = 1;
    out->footprint_h = 1;
    out->solid = false;
    return true;
}

static bool dark_reign_resolve_building_visual(const DarkReignDefinitions *defs, const char *type_name,
                                               DarkReignVisualSpec *out) {
    memset(out, 0, sizeof(*out));
    const char *body = NULL;
    size_t body_len = 0;
    if (!dark_reign_find_definition_block(defs->buildings, "DefineBuildingType", type_name, &body, &body_len)) {
        return dark_reign_visual_from_static(type_name, out);
    }
    if (!dark_reign_find_call_arg(body, body_len, "SetBuildingImages",
                                  out->sprite_name, sizeof(out->sprite_name))) {
        return false;
    }
    dark_reign_find_call_arg(body, body_len, "SetShadowImage", out->shadow_name, sizeof(out->shadow_name));
    out->footprint_w = 3;
    out->footprint_h = 3;
    out->solid = true;
    return true;
}

static bool dark_reign_resolve_thing_visual(const DarkReignDefinitions *defs, const char *type_name,
                                            DarkReignVisualSpec *out) {
    memset(out, 0, sizeof(*out));
    if (dark_reign_visual_from_static(type_name, out)) return true;

    const char *body = NULL;
    size_t body_len = 0;
    if (!dark_reign_find_definition_block(defs->overlay, "DefineThingType", type_name, &body, &body_len)) {
        return false;
    }

    char animation[64] = { 0 };
    if (!dark_reign_find_call_arg(body, body_len, "SetThingImage", animation, sizeof(animation)) ||
        !dark_reign_resolve_animation_sprite(defs, animation, out->sprite_name, sizeof(out->sprite_name))) {
        return false;
    }
    char shadow_animation[64] = { 0 };
    if (dark_reign_find_call_arg(body, body_len, "SetThingShadowImage",
                                 shadow_animation, sizeof(shadow_animation))) {
        dark_reign_resolve_animation_sprite(defs, shadow_animation,
                                            out->shadow_name, sizeof(out->shadow_name));
    }
    out->footprint_w = 1;
    out->footprint_h = 1;
    out->solid = find_case_insensitive_n(body, body_len, "IsCrater") == NULL &&
                 find_case_insensitive_n(body, body_len, "NoEdit") == NULL;
    return true;
}

static int compare_map_decorations(const void *a, const void *b) {
    const MapDecoration *da = a;
    const MapDecoration *db = b;
    int ya = da->gy + da->footprint_h;
    int yb = db->gy + db->footprint_h;
    if (ya != yb) return ya - yb;
    return da->gx - db->gx;
}

static void add_dark_reign_decoration(GameMap *map, const DarkReignVisualSpec *spec, int gx, int gy) {
    if (!spec || gx < 0 || gy < 0 || gx >= map->width || gy >= map->height ||
        map->decoration_count >= MAX_DECORATIONS) {
        return;
    }
    MapDecoration *dec = &map->decorations[map->decoration_count++];
    dec->gx = gx;
    dec->gy = gy;
    dec->footprint_w = spec->footprint_w;
    dec->footprint_h = spec->footprint_h;
    dec->solid = spec->solid;
    snprintf(dec->sprite_name, sizeof(dec->sprite_name), "%s", spec->sprite_name);
    snprintf(dec->shadow_name, sizeof(dec->shadow_name), "%s", spec->shadow_name);

    if (!spec->solid) return;
    for (int y = 0; y < spec->footprint_h; ++y) {
        for (int x = 0; x < spec->footprint_w; ++x) {
            int mx = gx + x;
            int my = gy + y;
            if (mx >= 0 && my >= 0 && mx < map->width && my < map->height) {
                map->blocked[map_index(map, mx, my)] = 1;
            }
        }
    }
}

static void detect_tileset_from_scn(const char *map_path, char *tileset, size_t tileset_size) {
    char scn_path[1024];
    replace_extension(scn_path, sizeof(scn_path), map_path, ".SCN");

    Blob blob;
    if (!load_blob(scn_path, &blob)) return;
    char *text = malloc(blob.size + 1);
    if (!text) {
        free_blob(&blob);
        return;
    }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';

    const char *tag = "SetDefaultTerrain(";
    char *hit = strstr(text, tag);
    if (hit) {
        hit += strlen(tag);
        char *end = strchr(hit, ')');
        if (end && end > hit) {
            uppercase_trimmed_token(tileset, tileset_size, hit, (size_t)(end - hit));
        }
    }

    free(text);
    free_blob(&blob);
}

int load_dark_reign_initial_units(const char *map_path, Unit *units, int max_units) {
    if (max_units <= 0) return 0;
    DarkReignDefinitions defs;
    dark_reign_load_definitions(map_path, &defs);

    char scn_path[1024];
    replace_extension(scn_path, sizeof(scn_path), map_path, ".SCN");

    Blob blob;
    if (!load_blob(scn_path, &blob)) {
        dark_reign_free_definitions(&defs);
        return 0;
    }
    char *text = malloc(blob.size + 1);
    if (!text) {
        free_blob(&blob);
        dark_reign_free_definitions(&defs);
        return 0;
    }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';

    int count = 0;
    const char *tag = "PutUnitAt(";
    char *cursor = text;
    while (count < max_units) {
        char *hit = strstr(cursor, tag);
        if (!hit) break;

        int object_id = 0;
        int gx = 0;
        int gy = 0;
        char unit_type[64] = { 0 };
        if (sscanf(hit, "PutUnitAt(%d %63[^ )] %d %d", &object_id, unit_type, &gx, &gy) == 4) {
            (void)object_id;
            if (gx >= 0 && gy >= 0) {
                units[count].gx = (float)gx + 0.5f;
                units[count].gy = (float)gy + 0.5f;
                units[count].speed = 5.5f;
                units[count].selected = count == 0;
                DarkReignVisualSpec visual;
                if (dark_reign_resolve_unit_visual(&defs, unit_type, &visual)) {
                    snprintf(units[count].sprite_name, sizeof(units[count].sprite_name),
                             "%s", visual.sprite_name);
                    snprintf(units[count].shadow_name, sizeof(units[count].shadow_name),
                             "%s", visual.shadow_name);
                } else {
                    snprintf(units[count].sprite_name, sizeof(units[count].sprite_name),
                             "%s", DEFAULT_UNIT_SPR);
                    units[count].shadow_name[0] = '\0';
                    fprintf(stderr, "warning: unresolved Dark Reign unit type %s\n", unit_type);
                }
                count++;
            }
        }
        cursor = hit + strlen(tag);
    }

    free(text);
    free_blob(&blob);
    dark_reign_free_definitions(&defs);
    return count;
}

static void load_dark_reign_decorations(const char *map_path, GameMap *map) {
    DarkReignDefinitions defs;
    dark_reign_load_definitions(map_path, &defs);

    char scn_path[1024];
    replace_extension(scn_path, sizeof(scn_path), map_path, ".SCN");

    Blob blob;
    if (!load_blob(scn_path, &blob)) {
        dark_reign_free_definitions(&defs);
        return;
    }
    char *text = malloc(blob.size + 1);
    if (!text) {
        free_blob(&blob);
        dark_reign_free_definitions(&defs);
        return;
    }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';

    char *cursor = text;
    while (map->decoration_count < MAX_DECORATIONS) {
        char *thing_hit = strstr(cursor, "AddThingAt(");
        char *building_hit = strstr(cursor, "AddBuildingAt(");
        bool building = false;
        char *hit = thing_hit;
        if (building_hit && (!hit || building_hit < hit)) {
            hit = building_hit;
            building = true;
        }
        if (!hit) break;

        int object_id = 0;
        int gx = 0;
        int gy = 0;
        char type_name[64] = { 0 };
        int parsed = building ?
            sscanf(hit, "AddBuildingAt(%d %63[^ )] %d %d", &object_id, type_name, &gx, &gy) :
            sscanf(hit, "AddThingAt(%d %63[^ )] %d %d", &object_id, type_name, &gx, &gy);
        if (parsed == 4) {
            (void)object_id;
            DarkReignVisualSpec visual;
            bool resolved = building ?
                dark_reign_resolve_building_visual(&defs, type_name, &visual) :
                dark_reign_resolve_thing_visual(&defs, type_name, &visual);
            if (resolved) {
                add_dark_reign_decoration(map, &visual, gx, gy);
            } else {
                fprintf(stderr, "warning: unresolved Dark Reign %s type %s\n",
                        building ? "building" : "thing", type_name);
            }
        }
        cursor = hit + (building ? strlen("AddBuildingAt(") : strlen("AddThingAt("));
    }

    free(text);
    free_blob(&blob);
    dark_reign_free_definitions(&defs);
    qsort(map->decorations, (size_t)map->decoration_count, sizeof(MapDecoration), compare_map_decorations);
}

bool load_dark_map(const char *map_path, GameMap *out) {
    memset(out, 0, sizeof(*out));
    Blob blob;
    if (!load_blob(map_path, &blob)) return false;
    if (blob.size < 20 || memcmp(blob.bytes, "MAP_", 4) != 0) {
        fprintf(stderr, "%s is not a Dark Reign MAP_ map\n", map_path);
        free_blob(&blob);
        return false;
    }
    int width = read_i32_le(blob.bytes + 8);
    int height = read_i32_le(blob.bytes + 12);
    size_t record_count = (size_t)width * (size_t)height;
    if (width <= 0 || height <= 0 || width > 512 || height > 512 ||
        blob.size < 20 + record_count * 6) {
        fprintf(stderr, "%s has unsupported map dimensions\n", map_path);
        free_blob(&blob);
        return false;
    }

    out->width = width;
    out->height = height;
    out->tile_ids = calloc(record_count, sizeof(uint16_t));
    out->blocked = calloc(record_count, sizeof(uint8_t));
    out->decorations = calloc(MAX_DECORATIONS, sizeof(MapDecoration));
    if (!out->tile_ids || !out->blocked || !out->decorations) {
        free_blob(&blob);
        return false;
    }
    const uint8_t *records = blob.bytes + 20;
    for (size_t i = 0; i < record_count; ++i) {
        const uint8_t *record = records + i * 6;
        uint8_t byte1 = record[0];
        uint8_t byte2 = record[1];
        uint8_t subindex = (uint8_t)(byte1 / 64);
        uint8_t variation = (uint8_t)(subindex * (byte2 + 1));
        if (variation > 7) variation = 7;

        int terrain_type = byte1 % 16;
        terrain_type--;
        if (terrain_type < 0) terrain_type = 15;

        uint16_t frame = terrain_type == 15 ?
            variation :
            (uint16_t)(8 + terrain_type * 8 + variation);
        out->tile_ids[i] = frame;
        out->blocked[i] = terrain_type == 15 || (terrain_type >= 11 && terrain_type <= 14);
    }
    detect_tileset_from_mm(map_path, out->tileset_name, sizeof(out->tileset_name));
    detect_tileset_from_scn(map_path, out->tileset_name, sizeof(out->tileset_name));
    out->render_features |= MAP_RENDER_SMOOTH_TRANSITIONS;
    out->render_transitions = render_dark_reign_edges_for_cell;
    load_dark_reign_decorations(map_path, out);
    free_blob(&blob);
    return true;
}

bool load_dark_colony_map(const char *map_path, GameMap *out) {
    memset(out, 0, sizeof(*out));
    Blob blob;
    if (!load_blob(map_path, &blob)) return false;
    if (blob.size < 8) {
        free_blob(&blob);
        return false;
    }
    int width = read_i32_le(blob.bytes + 0);
    int height = read_i32_le(blob.bytes + 4);
    if (width <= 0 || height <= 0 || width > 512 || height > 512) {
        fprintf(stderr, "%s has unsupported Dark Colony map dimensions\n", map_path);
        free_blob(&blob);
        return false;
    }
    size_t source_count = (size_t)width * (size_t)height;
    size_t cell_count = source_count;
    if (blob.size < 8 + source_count * 2 * 3) {
        fprintf(stderr, "%s has truncated Dark Colony map planes\n", map_path);
        free_blob(&blob);
        return false;
    }
    out->width = width;
    out->height = height;
    out->render_features |= MAP_RENDER_SKIP_ZERO_TILES | MAP_RENDER_INTERLEAVED_OVERLAYS;
    out->tile_ids = calloc(cell_count, sizeof(uint16_t));
    out->blocked = calloc(cell_count, sizeof(uint8_t));
    out->tile_overlay_count = 1;
    out->tile_overlays[0] = calloc(cell_count, sizeof(uint16_t));
    out->tile_flip_flags[0] = calloc(cell_count, sizeof(uint8_t));
    out->tile_flip_flags[1] = calloc(cell_count, sizeof(uint8_t));
    if (!out->tile_ids || !out->blocked) {
        free_blob(&blob);
        destroy_map(out);
        return false;
    }
    if (!out->tile_overlays[0] || !out->tile_flip_flags[0] || !out->tile_flip_flags[1]) {
        free_blob(&blob);
        destroy_map(out);
        return false;
    }
    const uint8_t *tile_pairs = blob.bytes + 8;
    const uint8_t *tile_flags = blob.bytes + 8 + source_count * 4;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = (size_t)y * (size_t)width + (size_t)x;
            const uint8_t *cell = tile_pairs + idx * 4;
            out->tile_ids[idx] = read_u16_le(cell);
            out->tile_overlays[0][idx] = read_u16_le(cell + 2);
            uint16_t flags = read_u16_le(tile_flags + idx * 2);
            out->tile_flip_flags[0][idx] = (flags & (1u << 5)) ? 1 : 0;
            out->tile_flip_flags[1][idx] = (flags & (1u << 6)) ? 1 : 0;
            uint16_t base = out->tile_ids[idx];
            if (base == 0) out->blocked[idx] = 1;
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
        free(scn);
    }

    const char *base = strrchr(map_path, '/');
    base = base ? base + 1 : map_path;
    if (out->tileset_name[0] != '\0') {
        /* SCN supplied the tileset. */
    } else if (toupper((unsigned char)base[0]) == 'J') {
        strncpy(out->tileset_name, "JUNGLE", sizeof(out->tileset_name) - 1);
    } else if (toupper((unsigned char)base[0]) == 'A') {
        strncpy(out->tileset_name, "ATLANTIS", sizeof(out->tileset_name) - 1);
    } else if (toupper((unsigned char)base[0]) == 'H') {
        strncpy(out->tileset_name, "HTRAIN", sizeof(out->tileset_name) - 1);
    } else {
        strncpy(out->tileset_name, "DESERT", sizeof(out->tileset_name) - 1);
    }
    free_blob(&blob);
    return true;
}

static const char *dark_colony_unit_sprite_for_type(int type, int race) {
    if (race == 1) {
        if (type == 0 || (type >= 69 && type <= 72)) return "SPRITES/GRAY.SPR";
        if (type == 6) return "SPRITES/SLUG.SPR";
    } else {
        if (type == 0 || (type >= 69 && type <= 72)) return "SPRITES/TRSC.SPR";
        if (type == 6) return "SPRITES/EXPL.SPR";
    }

    switch (type) {
        case 2: return "SPRITES/REAP.SPR";
        case 3: return "SPRITES/BARR.SPR";
        case 4: return "SPRITES/SARG.SPR";
        case 5: return "SPRITES/SCGM.SPR";
        case 8: return "SPRITES/GRAY.SPR";
        case 9: return "SPRITES/XENO.SPR";
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
        case 73:
        case 74:
        case 75:
        case 76:
            return "SPRITES/GRAY.SPR";
        case 77: return "SPRITES/SARG.SPR";
        case 78: return "SPRITES/PSYC.SPR";
        default: return NULL;
    }
}

int load_dark_colony_initial_units(const char *map_path, Unit *units, int max_units) {
    char scn_path[1024];
    replace_extension(scn_path, sizeof(scn_path), map_path, ".SCN");
    char *text = load_text_file(scn_path);
    if (!text) return 0;

    int team_race[16] = { 0 };
    int current_team = -1;
    int team_count = 0;
    bool expect_race = false;
    bool object_mode = false;
    int trailing_blank_lines = 0;
    int count = 0;
    for (char *line = text; line && *line && count < max_units;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char newline = *next;
            *next++ = '\0';
            if (newline == '\r' && *next == '\n') next++;
        }

        char token[128] = { 0 };
        copy_trimmed_token(token, sizeof(token), line, strlen(line));
        if (token[0] == '\0') {
            if (team_count >= 8 && !object_mode && ++trailing_blank_lines >= 2) {
                object_mode = true;
            }
            line = next;
            continue;
        }

        if (strncmp(token, "TEAM ", 5) == 0) {
            int active = 0;
            if (sscanf(token, "TEAM %d %d", &current_team, &active) >= 1 &&
                current_team >= 0 && current_team < 16) {
                team_count++;
                expect_race = true;
            } else {
                current_team = -1;
                expect_race = false;
            }
            trailing_blank_lines = 0;
            line = next;
            continue;
        }
        if (!object_mode && expect_race) {
            int race = 0;
            if (sscanf(token, "%d", &race) == 1 && current_team >= 0 && current_team < 16) {
                team_race[current_team] = race;
                expect_race = false;
                trailing_blank_lines = 0;
                line = next;
                continue;
            }
        }
        if (!object_mode) {
            trailing_blank_lines = 0;
            line = next;
            continue;
        }

        int x = 0, y = 0, type = 0, team = 0, owner = 0, extra = 0;
        if (sscanf(line, " %d %d %d %d %d %d", &x, &y, &type, &team, &owner, &extra) == 6 &&
            team >= 0 && team < 16 && x >= 0 && y >= 0) {
            const char *sprite = dark_colony_unit_sprite_for_type(type, team_race[team]);
            if (!sprite) {
                line = next;
                continue;
            }
            Unit *u = &units[count];
            u->gx = (float)x + 0.5f;
            u->gy = (float)y + 0.5f;
            u->speed = 5.5f;
            u->selected = count == 0;
            snprintf(u->sprite_name, sizeof(u->sprite_name), "%s", sprite);
            count++;
        }
        line = next;
    }

    free(text);
    return count;
}

static bool ftg_load(const char *path, FtgArchive *out) {
    memset(out, 0, sizeof(*out));
    Blob blob;
    if (!load_blob(path, &blob)) return false;
    if (blob.size < 12 || memcmp(blob.bytes, "BOTG", 4) != 0) {
        fprintf(stderr, "%s is not a Dark Reign FTG archive\n", path);
        free_blob(&blob);
        return false;
    }
    int32_t dir_offset = read_i32_le(blob.bytes + 4);
    int32_t count = read_i32_le(blob.bytes + 8);
    if (dir_offset < 12 || count <= 0 || count > 65536 ||
        (size_t)dir_offset + (size_t)count * 36 > blob.size) {
        fprintf(stderr, "%s has invalid FTG directory\n", path);
        free_blob(&blob);
        return false;
    }
    out->entries = calloc((size_t)count, sizeof(FtgEntry));
    if (!out->entries) {
        free_blob(&blob);
        return false;
    }
    out->bytes = blob.bytes;
    out->size = blob.size;
    out->count = count;
    for (int i = 0; i < count; ++i) {
        const uint8_t *e = out->bytes + dir_offset + i * 36;
        memcpy(out->entries[i].name, e, 27);
        out->entries[i].name[27] = '\0';
        out->entries[i].offset = read_i32_le(e + 28);
        out->entries[i].size = read_i32_le(e + 32);
    }
    return true;
}

static void ftg_free(FtgArchive *ftg) {
    free(ftg->bytes);
    free(ftg->entries);
    memset(ftg, 0, sizeof(*ftg));
}

static const FtgEntry *ftg_find(const FtgArchive *ftg, const char *name) {
    for (int i = 0; i < ftg->count; ++i) {
        if (strcasecmp(ftg->entries[i].name, name) == 0) return &ftg->entries[i];
    }
    return NULL;
}

typedef struct {
    int first_anim;
    int last_anim;
    int framerate;
    int hotspots;
} SprSection;

static void sprite_sheet_add_sequence(SpriteSheet *sheet, const char *name, int facings, int length,
                                      int tick_ms, const int *frame_starts,
                                      const int *direction_codes) {
    if (!sheet || !name || sheet->sequence_count >= MAX_SPRITE_SEQUENCES ||
        facings <= 0 || facings > MAX_SEQUENCE_FACINGS || length <= 0) {
        return;
    }
    SpriteSequence *seq = &sheet->sequences[sheet->sequence_count++];
    memset(seq, 0, sizeof(*seq));
    snprintf(seq->name, sizeof(seq->name), "%s", name);
    seq->facings = facings;
    seq->length = length;
    seq->frame_stride = 1;
    seq->tick_ms = tick_ms > 0 ? tick_ms : 120;
    for (int i = 0; i < facings; ++i) {
        seq->frame_starts[i] = frame_starts ? frame_starts[i] : i * length;
        seq->direction_codes[i] = direction_codes ? direction_codes[i] : i * 16 / facings;
    }
}

static void sprite_sheet_set_last_sequence_stride(SpriteSheet *sheet, int frame_stride) {
    if (!sheet || sheet->sequence_count <= 0 || frame_stride <= 0) return;
    sheet->sequences[sheet->sequence_count - 1].frame_stride = frame_stride;
}

static void sprite_sheet_add_linear_sequence(SpriteSheet *sheet, const char *name, int start,
                                             int facings, int length, int tick_ms) {
    int starts[MAX_SEQUENCE_FACINGS];
    for (int i = 0; i < facings && i < MAX_SEQUENCE_FACINGS; ++i) {
        starts[i] = start + i * length;
    }
    sprite_sheet_add_sequence(sheet, name, facings, length, tick_ms, starts, NULL);
}

static bool sprite_sheet_has_sequence(const SpriteSheet *sheet, const char *name) {
    if (!sheet || !name) return false;
    for (int i = 0; i < sheet->sequence_count; ++i) {
        if (strcmp(sheet->sequences[i].name, name) == 0) return true;
    }
    return false;
}

static bool load_dark_sprite(SDL_Renderer *renderer, const uint8_t *data, size_t size,
                             const uint32_t palette[256], SpriteSheet *out) {
    memset(out, 0, sizeof(*out));
    if (size < 32 || (memcmp(data, "RSPR", 4) != 0 && memcmp(data, "SSPR", 4) != 0 &&
                      memcmp(data, "LSPR", 4) != 0)) {
        return false;
    }
    bool shadow = memcmp(data, "SSPR", 4) == 0;
    int version = read_i32_le(data + 4);
    int nanims = read_i32_le(data + 8);
    int nrots = read_i32_le(data + 12);
    int szx = read_i32_le(data + 16);
    int szy = read_i32_le(data + 20);
    int npics = read_i32_le(data + 24);
    int nsects = read_i32_le(data + 28);
    if ((version != 0x0210 && version != 0x0200) || nanims <= 0 || nrots <= 0 ||
        szx <= 0 || szy <= 0 || npics <= 0 || nsects <= 0) {
        return false;
    }

    int off_sections = 32 + 4 * nanims * nrots;
    int off_anims = off_sections + 16 * nsects;
    int off_picoffs = off_anims + 4 * nanims;
    int off_bits = off_picoffs + 8 * npics + 4;
    if (off_bits <= 0 || (size_t)off_bits > size) return false;

    SprSection *sects = calloc((size_t)nsects, sizeof(*sects));
    if (!sects) return false;
    int total_frames = 0;
    for (int s = 0; s < nsects; ++s) {
        const uint8_t *base = data + off_sections + s * 16;
        sects[s].first_anim = read_i32_le(base + 0);
        sects[s].last_anim = read_i32_le(base + 4);
        sects[s].framerate = read_i32_le(base + 8);
        sects[s].hotspots = read_i32_le(base + 12);
        int section_frames = sects[s].last_anim - sects[s].first_anim + 1;
        if (section_frames > 0) total_frames += nrots * section_frames;
    }
    if (total_frames <= 0) {
        free(sects);
        return false;
    }

    int cols = (int)ceilf(sqrtf((float)total_frames));
    int rows = (total_frames + cols - 1) / cols;
    int atlas_w = cols * szx;
    int atlas_h = rows * szy;
    uint8_t *indices = calloc((size_t)atlas_w * (size_t)atlas_h, 1);
    if (!indices) {
        free(sects);
        return false;
    }

    int rot_offset = nrots >= 4 ? nrots / 4 : 0;
    int frame_cursor = 0;
    for (int s = 0; s < nsects; ++s) {
        for (int r = 0; r < nrots; ++r) {
            int disk_r = (r + rot_offset) % nrots;
            for (int a = sects[s].first_anim; a <= sects[s].last_anim; ++a) {
                int picindex = a * nrots + disk_r;
                if (picindex < 0 || picindex >= nanims * nrots) goto spr_fail;
                int picnr = read_i32_le(data + 32 + picindex * 4);
                if (picnr < 0 || picnr >= npics) goto spr_fail;
                int poff = off_picoffs + 8 * picnr;
                int pic_start = read_i32_le(data + poff);
                int pic_end = read_i32_le(data + poff + 8);
                if (pic_start < 0 || pic_end < pic_start || (size_t)(off_bits + pic_end) > size) goto spr_fail;

                const uint8_t *compressed = data + off_bits + pic_start;
                size_t comp_size = (size_t)(pic_end - pic_start);
                size_t comp_pos = 0;
                int ox = (frame_cursor % cols) * szx;
                int oy = (frame_cursor / cols) * szy;

                for (int y = 0; y < szy; ++y) {
                    int x = 0;
                    int step = 0;
                    while (x < szx) {
                        if (comp_pos >= comp_size) goto spr_fail;
                        int cnt = compressed[comp_pos++];
                        if (step & 1) cnt &= 0x7f;
                        if (cnt < 0 || x + cnt > szx) goto spr_fail;
                        if (step & 1) {
                            uint8_t *dst = indices + (oy + y) * atlas_w + ox + x;
                            if (shadow) {
                                memset(dst, 47, (size_t)cnt);
                            } else {
                                if (comp_pos + (size_t)cnt > comp_size) goto spr_fail;
                                memcpy(dst, compressed + comp_pos, (size_t)cnt);
                                comp_pos += (size_t)cnt;
                            }
                        }
                        x += cnt;
                        step++;
                    }
                }
                frame_cursor++;
            }
        }
    }

    uint32_t *rgba = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    SDL_Rect *frames = calloc((size_t)total_frames, sizeof(SDL_Rect));
    if (!rgba || !frames) {
        free(rgba);
        free(frames);
        goto spr_fail;
    }
    indexed_to_rgba(rgba, indices, (size_t)atlas_w * (size_t)atlas_h, palette);
    for (int i = 0; i < total_frames; ++i) {
        frames[i].x = (i % cols) * szx;
        frames[i].y = (i / cols) * szy;
        frames[i].w = szx;
        frames[i].h = szy;
    }
    out->texture = rgba_texture(renderer, rgba, atlas_w, atlas_h, true);
    out->frames = frames;
    out->frame_count = total_frames;
    out->frame_w = szx;
    out->frame_h = szy;
    out->rotations = nrots;
    out->primary_frames_per_rotation = sects[0].last_anim - sects[0].first_anim + 1;
    int sequence_start = 0;
    static const char *section_names[] = { "run", "shoot", "idle", "stand" };
    for (int s = 0; s < nsects && s < (int)(sizeof(section_names) / sizeof(section_names[0])); ++s) {
        int section_frames = sects[s].last_anim - sects[s].first_anim + 1;
        if (section_frames > 0) {
            sprite_sheet_add_linear_sequence(out, section_names[s], sequence_start,
                                             nrots, section_frames, sects[s].framerate);
            sequence_start += nrots * section_frames;
        }
    }
    if (!sprite_sheet_has_sequence(out, "stand") && out->sequence_count > 0) {
        SpriteSequence stand = out->sequences[out->sequence_count - 1];
        snprintf(stand.name, sizeof(stand.name), "stand");
        if (out->sequence_count < MAX_SPRITE_SEQUENCES) out->sequences[out->sequence_count++] = stand;
    }
    free(rgba);
    free(indices);
    free(sects);
    return out->texture != NULL;

spr_fail:
    free(indices);
    free(sects);
    return false;
}

static bool load_unit_sprite(SDL_Renderer *renderer, const char *data_root,
                             const char *tileset_name, const char *sprite_name,
                             const uint32_t palette[256], SpriteSheet *out) {
    const char *archives[2] = { NULL, NULL };
    char themed_path[1024];
    char shared_path[1024];
    snprintf(themed_path, sizeof(themed_path), "%s/graphics/%s/SPRITES.FTG", data_root, tileset_name);
    snprintf(shared_path, sizeof(shared_path), "%s/graphics/SPRITES.FTG", data_root);
    archives[0] = themed_path;
    archives[1] = shared_path;

    for (int i = 0; i < 2; ++i) {
        FtgArchive ftg;
        if (!ftg_load(archives[i], &ftg)) continue;
        const FtgEntry *entry = ftg_find(&ftg, sprite_name);
        if (!entry) {
            ftg_free(&ftg);
            continue;
        }
        if (entry->offset < 0 || entry->size <= 0 ||
            (size_t)entry->offset + (size_t)entry->size > ftg.size) {
            ftg_free(&ftg);
            return false;
        }
        bool ok = load_dark_sprite(renderer, ftg.bytes + entry->offset, (size_t)entry->size, palette, out);
        ftg_free(&ftg);
        return ok;
    }

    fprintf(stderr, "sprite %s not found in Dark Reign FTG archives\n", sprite_name);
    return false;
}

static bool sprite_cache_load_dark_reign(SpriteCache *cache, SDL_Renderer *renderer, const char *data_root,
                                         const char *tileset_name, const char *name,
                                         const uint32_t palette[256]) {
    if (!name || name[0] == '\0') return true;
    if (sprite_cache_find(cache, name)) return true;
    if (cache->count >= MAX_DECORATION_SPRITES) {
        fprintf(stderr, "too many decoration sprites; skipped %s\n", name);
        return false;
    }
    CachedSprite *entry = &cache->entries[cache->count];
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    if (!load_unit_sprite(renderer, data_root, tileset_name, name, palette, &entry->sprite)) {
        memset(entry, 0, sizeof(*entry));
        return false;
    }
    cache->count++;
    return true;
}

bool load_dark_reign_decoration_sprites(SDL_Renderer *renderer, const char *data_root,
                                        const GameMap *map, const Unit *units,
                                        int unit_count, SpriteCache *cache) {
    memset(cache, 0, sizeof(*cache));

    uint32_t sprite_palette[256];
    char palette_path[1024];
    snprintf(palette_path, sizeof(palette_path), "%s/graphics/BARREN.PAL", data_root);
    if (!load_dark_sprite_palette(palette_path, sprite_palette)) return false;

    bool ok = true;
    for (int i = 0; i < map->decoration_count; ++i) {
        const MapDecoration *dec = &map->decorations[i];
        if (!sprite_cache_load_dark_reign(cache, renderer, data_root, map->tileset_name,
                                          dec->shadow_name, sprite_palette)) {
            ok = false;
        }
        if (!sprite_cache_load_dark_reign(cache, renderer, data_root, map->tileset_name,
                                          dec->sprite_name, sprite_palette)) {
            ok = false;
        }
    }
    for (int i = 0; i < unit_count; ++i) {
        const Unit *unit = &units[i];
        if (!sprite_cache_load_dark_reign(cache, renderer, data_root, map->tileset_name,
                                          unit->shadow_name, sprite_palette)) {
            ok = false;
        }
        if (!sprite_cache_load_dark_reign(cache, renderer, data_root, map->tileset_name,
                                          unit->sprite_name, sprite_palette)) {
            ok = false;
        }
    }
    return ok;
}

static bool load_dark_colony_sprite(SDL_Renderer *renderer, const char *path, SpriteSheet *out,
                                    uint32_t palette_out[256]) {
    memset(out, 0, sizeof(*out));
    Blob blob;
    if (!load_blob(path, &blob)) return false;
    if (blob.size < 8 + 256 * 3) {
        free_blob(&blob);
        return false;
    }
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
    int max_w = 1;
    int max_h = 1;
    size_t total_pixels = 0;
    for (int i = 0; i < visible_frames; ++i) {
        const uint8_t *d = blob.bytes + desc_off + (size_t)i * 8;
        int w = read_u16_le(d + 0);
        int h = read_u16_le(d + 2);
        if (w <= 0 || h <= 0 || w > 512 || h > 512) {
            free_blob(&blob);
            return false;
        }
        if (w > max_w) max_w = w;
        if (h > max_h) max_h = h;
        total_pixels += (size_t)w * (size_t)h;
    }
    if (!compressed && data_off + total_pixels > blob.size) {
        fprintf(stderr, "%s has truncated Dark Colony sprite pixels\n", path);
        free_blob(&blob);
        return false;
    }

    int cols = (int)ceilf(sqrtf((float)visible_frames));
    int rows = (visible_frames + cols - 1) / cols;
    int atlas_w = cols * max_w;
    int atlas_h = rows * max_h;
    uint32_t *rgba = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    SDL_Rect *frames = calloc((size_t)visible_frames, sizeof(SDL_Rect));
    if (!rgba || !frames) {
        free(rgba);
        free(frames);
        free_blob(&blob);
        return false;
    }

    size_t src_pos = data_off;
    for (int i = 0; i < visible_frames; ++i) {
        const uint8_t *d = blob.bytes + desc_off + (size_t)i * 8;
        int w = read_u16_le(d + 0);
        int h = read_u16_le(d + 2);
        int fx = (i % cols) * max_w + (max_w - w) / 2;
        int fy = (i / cols) * max_h + (max_h - h) / 2;
        if (compressed) {
            if (src_pos + 4 > blob.size) {
                free(rgba);
                free(frames);
                free_blob(&blob);
                return false;
            }
            uint32_t chunk_size = read_u32_le(blob.bytes + src_pos);
            src_pos += 4;
            if (src_pos + chunk_size > blob.size) {
                free(rgba);
                free(frames);
                free_blob(&blob);
                return false;
            }

            const uint8_t *src = blob.bytes + src_pos;
            size_t pos = 0;
            int x = 0;
            int y = 0;
            while (pos < chunk_size && y < h) {
                int8_t cmd = (int8_t)src[pos++];
                if (cmd < 0) {
                    x += -cmd;
                } else {
                    int count = cmd + 1;
                    if (pos + (size_t)count > chunk_size) break;
                    for (int p = 0; p < count; ++p) {
                        if (x >= 0 && x < w && y >= 0 && y < h) {
                            int dst_x = fx + x;
                            int dst_y = fy + y;
                            if (dst_x >= 0 && dst_x < atlas_w && dst_y >= 0 && dst_y < atlas_h) {
                                rgba[dst_y * atlas_w + dst_x] = palette_out[src[pos + (size_t)p]];
                            }
                        }
                        x++;
                    }
                    pos += (size_t)count;
                }
                while (x >= w) {
                    x -= w;
                    y++;
                }
            }
            src_pos += chunk_size;
        } else {
            const uint8_t *src = blob.bytes + src_pos;
            blit_indexed_to_rgba(rgba, atlas_w, atlas_h, fx, fy, src, w, h, palette_out);
            src_pos += (size_t)w * (size_t)h;
        }
        frames[i] = (SDL_Rect){ (i % cols) * max_w, (i / cols) * max_h, max_w, max_h };
    }

    out->texture = rgba_texture(renderer, rgba, atlas_w, atlas_h, true);
    out->frames = frames;
    out->frame_count = visible_frames;
    out->frame_w = max_w;
    out->frame_h = max_h;
    out->rotations = 1;
    out->primary_frames_per_rotation = visible_frames;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    static const int dc_codes_east_first[8] = { 12, 14, 0, 2, 4, 6, 8, 10 };
    static const int dc_codes_trooper[8] = { 6, 8, 10, 12, 14, 0, 2, 4 };
    static const int trsc_stand[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    static const int trsc_run[8] = { 16, 17, 18, 19, 20, 21, 22, 23 };
    static const int gray_run[8] = { 16, 17, 18, 19, 20, 21, 22, 23 };
    static const int trooper_stand[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    static const int trooper_run[8] = { 8, 9, 10, 11, 12, 13, 14, 15 };
    if (strcasecmp(base, "TRSC.SPR") == 0) {
        sprite_sheet_add_sequence(out, "stand", 8, 1, 120, trsc_stand, dc_codes_east_first);
        sprite_sheet_add_sequence(out, "run", 8, 8, 120, trsc_run, dc_codes_east_first);
        sprite_sheet_set_last_sequence_stride(out, 8);
    } else if (strcasecmp(base, "GRAY.SPR") == 0) {
        sprite_sheet_add_sequence(out, "stand", 8, 1, 120, trsc_stand, dc_codes_east_first);
        sprite_sheet_add_sequence(out, "run", 8, 7, 120, gray_run, dc_codes_east_first);
        sprite_sheet_set_last_sequence_stride(out, 8);
    } else if (strcasecmp(base, "TROOPER1.SPR") == 0) {
        sprite_sheet_add_sequence(out, "stand", 8, 1, 120, trooper_stand, dc_codes_trooper);
        sprite_sheet_add_sequence(out, "run", 8, 2, 120, trooper_run, dc_codes_trooper);
        sprite_sheet_set_last_sequence_stride(out, 8);
    } else if (visible_frames >= 16) {
        int stand[8];
        int run[8];
        for (int i = 0; i < 8; ++i) {
            stand[i] = i;
            run[i] = i + 8;
        }
        sprite_sheet_add_sequence(out, "stand", 8, 1, 120, stand, dc_codes_east_first);
        int run_length = (visible_frames - 8) / 8;
        if (run_length < 1) run_length = 1;
        if (run_length > 8) run_length = 8;
        sprite_sheet_add_sequence(out, "run", 8, run_length, 120, run, dc_codes_east_first);
        sprite_sheet_set_last_sequence_stride(out, 8);
    }
    free(rgba);
    free_blob(&blob);
    return out->texture != NULL;
}

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

bool load_dark_colony_unit_sprites(SDL_Renderer *renderer, const char *data_root,
                                   const Unit *units, int unit_count, SpriteCache *cache) {
    bool ok = true;
    for (int i = 0; i < unit_count; ++i) {
        if (!sprite_cache_load_dark_colony(cache, renderer, data_root, units[i].sprite_name)) {
            ok = false;
        }
    }
    return ok;
}

bool dark_reign_plugin_load_assets(SDL_Renderer *renderer, const char *data_root,
                                   const GameMap *map, const char *sprite_name,
                                   Tileset *tileset, SpriteSheet *unit_sprite) {
    uint32_t terrain_palette[256];
    uint32_t sprite_palette[256];
    char palette_path[1024];
    snprintf(palette_path, sizeof(palette_path), "%s/graphics/%s.PAL", data_root, map->tileset_name);
    if (!load_dark_terrain_palette(palette_path, terrain_palette)) {
        snprintf(palette_path, sizeof(palette_path), "%s/graphics/BARREN.PAL", data_root);
        if (!load_dark_terrain_palette(palette_path, terrain_palette)) return false;
    }
    snprintf(palette_path, sizeof(palette_path), "%s/graphics/BARREN.PAL", data_root);
    if (!load_dark_sprite_palette(palette_path, sprite_palette)) return false;

    char til_path[1024];
    snprintf(til_path, sizeof(til_path), "%s/graphics/%s.TIL", data_root, map->tileset_name);
    if (!load_dark_tileset(renderer, til_path, terrain_palette, tileset)) {
        snprintf(til_path, sizeof(til_path), "%s/graphics/BARREN.TIL", data_root);
        if (!load_dark_tileset(renderer, til_path, terrain_palette, tileset)) return false;
    }
    if (strcasecmp(map->tileset_name, "SNOW") != 0) {
        dark_reign_add_water_animations(tileset);
    }

    if (!load_unit_sprite(renderer, data_root, map->tileset_name, sprite_name, sprite_palette, unit_sprite)) {
        fprintf(stderr, "failed to load %s\n", sprite_name);
        destroy_tileset(tileset);
        return false;
    }
    return true;
}

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

static int dark_reign_edge_frame_for_template(int template_id, int variation) {
    if (template_id >= 226 && template_id <= 239) {
        (void)variation;
        return 1032 + (template_id - 226) * 4;
    }
    return template_id + 218;
}

static int dark_reign_neighbor_type(const GameMap *map, int x, int y, int fallback) {
    if (!map_contains(map, x, y)) return fallback;
    return dark_reign_terrain_type_from_frame(map->tile_ids[map_index(map, x, y)]);
}

static bool dark_reign_rule_matches(EdgeMatchType match, int neighbor_type, int self_value) {
    if (match == EDGE_MATCH_BELOW) return neighbor_type < self_value;
    return neighbor_type >= self_value;
}

static void render_dark_reign_edges_for_cell(App *app, const GameMap *map, const Tileset *tileset,
                                             int x, int y, int dx, int dy) {
    int frame = map->tile_ids[map_index(map, x, y)] % tileset->count;
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

    SDL_Rect whole = { 0, 0, tileset->tile_w, tileset->tile_h };
    int scale = app->render_scale > 0 ? app->render_scale : 1;
    SDL_Rect dst = { dx, dy, tileset->tile_w * scale, tileset->tile_h * scale };

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
            render_tile_at(app, tileset, shim_frame, whole, dst);
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
        render_tile_at(app, tileset, edge_frames[i].frame, whole, dst);
    }
}

int main(int argc, char **argv) {
    bool check_only = argc > 1 && strcmp(argv[1], "--check") == 0;
    bool screenshot_only = argc > 1 && strcmp(argv[1], "--screenshot") == 0;
    const char *screenshot_path = screenshot_only && argc > 2 ? argv[2] : NULL;
    int arg_base = check_only ? 2 : (screenshot_only ? 3 : 1);
    int render_scale = 2;
    const RtsPlugin *plugin = rts_find_plugin("dark-reign");
    while (argc > arg_base) {
        if (argc > arg_base + 1 && strcmp(argv[arg_base], "--game") == 0) {
            plugin = rts_find_plugin(argv[arg_base + 1]);
            if (!plugin) {
                fprintf(stderr, "unknown game '%s'\n", argv[arg_base + 1]);
                return 1;
            }
            arg_base += 2;
        } else if (argc > arg_base + 1 && strcmp(argv[arg_base], "--scale") == 0) {
            render_scale = atoi(argv[arg_base + 1]);
            if (render_scale < 1) render_scale = 1;
            if (render_scale > 6) render_scale = 6;
            arg_base += 2;
        } else {
            break;
        }
    }

    const char *data_root = argc > arg_base ? argv[arg_base] : plugin->default_root;
    const char *map_rel_or_abs = argc > arg_base + 1 ? argv[arg_base + 1] : plugin->default_map;
    const char *sprite_name = argc > arg_base + 2 ? argv[arg_base + 2] : plugin->default_sprite;

    char map_path[1024];
    if (map_rel_or_abs[0] == '/') {
        snprintf(map_path, sizeof(map_path), "%s", map_rel_or_abs);
    } else {
        path_join(map_path, sizeof(map_path), data_root, map_rel_or_abs);
    }

    RtsRenderer renderer;
    App app = { 0 };
    app.win_w = 1280;
    app.win_h = 800;
    app.render_scale = render_scale;
    app.show_grid = false;
    app.running = true;
    if (!rts_renderer_create(&renderer, rts_sdl_renderer_backend(), "open-rts - paletted RTS base",
                             app.win_w, app.win_h, check_only || screenshot_only,
                             check_only || screenshot_only)) {
        return 1;
    }
    app.window = renderer.window;
    app.renderer = renderer.sdl;

    GameMap map;
    if (!plugin->load_map(map_path, &map)) {
        rts_renderer_destroy(&renderer);
        return 1;
    }

    Tileset tileset;
    SpriteSheet unit_sprite;
    memset(&tileset, 0, sizeof(tileset));
    memset(&unit_sprite, 0, sizeof(unit_sprite));
    if (!plugin->load_assets(app.renderer, data_root, &map, sprite_name, &tileset, &unit_sprite)) {
        destroy_map(&map);
        rts_renderer_destroy(&renderer);
        return 1;
    }
    app.cell_w = plugin->cell_w > 0 ? plugin->cell_w : (tileset.tile_w > 0 ? tileset.tile_w : CELL_W);
    app.cell_h = plugin->cell_h > 0 ? plugin->cell_h : (tileset.tile_h > 0 ? tileset.tile_h : CELL_H);

    Unit units[MAX_UNITS] = { 0 };
    int unit_count = plugin->load_initial_units ? plugin->load_initial_units(map_path, units, MAX_UNITS) : 0;
    if (unit_count <= 0) {
        unit_count = 6;
        int cx = map.width / 2;
        int cy = map.height / 2;
        for (int i = 0; i < unit_count; ++i) {
            units[i].gx = (float)(cx + i % 3) + 0.5f;
            units[i].gy = (float)(cy + i / 3) + 0.5f;
            units[i].speed = 5.5f;
            units[i].selected = i == 0;
            snprintf(units[i].sprite_name, sizeof(units[i].sprite_name), "%s", sprite_name);
        }
    }

    SpriteCache decoration_sprites = { 0 };
    if (plugin->load_runtime_sprites &&
        !plugin->load_runtime_sprites(app.renderer, data_root, &map, units, unit_count,
                                      &decoration_sprites)) {
        fprintf(stderr, "warning: some %s runtime sprites were not loaded\n", plugin->name);
    }

    float focus_gx = unit_count > 0 ? units[0].gx : (float)map.width * 0.5f;
    float focus_gy = unit_count > 0 ? units[0].gy : (float)map.height * 0.5f;
    float sx, sy;
    grid_to_screen(&app, focus_gx, focus_gy, &sx, &sy);
    app.cam_x = (float)app.win_w * 0.5f - sx;
    app.cam_y = (float)app.win_h * 0.5f - sy;

    printf("Loaded %s (%dx%d, tileset %s, scale %dx, %d units, %d map decorations). Controls: left select/drag, right move, WASD/arrows pan, G grid, Ctrl+A select all.\n",
           map_path, map.width, map.height, map.tileset_name, app.render_scale, unit_count, map.decoration_count);

    if (check_only || screenshot_only) {
        if (screenshot_only) {
            app.ticks_ms = SDL_GetTicks();
            rts_renderer_begin_frame(&renderer, (SDL_Color){ 11, 14, 16, 255 });
            render_map(&app, &map, &tileset);
            render_decorations(&app, &map, &decoration_sprites);
            render_units(&app, units, unit_count, &unit_sprite, &decoration_sprites, SDL_GetTicks());
            if (rts_renderer_save_screenshot(&renderer, screenshot_path)) {
                printf("Saved screenshot %s.\n", screenshot_path);
            }
        }
        printf("Smoke check OK: %d terrain tiles, %d unit frames from %s.\n",
               tileset.count, unit_sprite.frame_count, sprite_name);
        destroy_sprite_cache(&decoration_sprites);
        destroy_sprite(&unit_sprite);
        destroy_tileset(&tileset);
        destroy_map(&map);
        rts_renderer_destroy(&renderer);
        return 0;
    }

    uint64_t prev = SDL_GetPerformanceCounter();
    double freq = (double)SDL_GetPerformanceFrequency();
    float accumulator = 0.0f;

    while (app.running) {
        uint64_t now = SDL_GetPerformanceCounter();
        float frame_dt = (float)((double)(now - prev) / freq);
        if (frame_dt > 0.25f) frame_dt = 0.25f;
        prev = now;
        accumulator += frame_dt;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            handle_event(&app, &map, units, unit_count, &e);
        }
        update_camera_from_keyboard(&app, frame_dt);
        while (accumulator >= FIXED_DT) {
            update_units(units, unit_count, FIXED_DT);
            accumulator -= FIXED_DT;
        }

        app.ticks_ms = SDL_GetTicks();
        rts_renderer_begin_frame(&renderer, (SDL_Color){ 11, 14, 16, 255 });
        render_map(&app, &map, &tileset);
        render_decorations(&app, &map, &decoration_sprites);
        render_units(&app, units, unit_count, &unit_sprite, &decoration_sprites, SDL_GetTicks());
        if (app.dragging_select) {
            SDL_SetRenderDrawColor(app.renderer, 98, 224, 161, 70);
            SDL_RenderFillRect(app.renderer, &app.selection_rect);
            SDL_SetRenderDrawColor(app.renderer, 98, 224, 161, 220);
            SDL_RenderDrawRect(app.renderer, &app.selection_rect);
        }
        rts_renderer_end_frame(&renderer);
    }

    destroy_sprite_cache(&decoration_sprites);
    destroy_sprite(&unit_sprite);
    destroy_tileset(&tileset);
    destroy_map(&map);
    rts_renderer_destroy(&renderer);
    return 0;
}
