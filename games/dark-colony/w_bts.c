#define _DEFAULT_SOURCE
#include "engine.h"
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

bool load_dark_colony_tileset(SDL_Renderer *renderer, const char *path, tileset_t *out) {
    memset(out, 0, sizeof(*out));
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return false;
    const int tile_w = 32, tile_h = 32, palette_count = 256;
    const size_t tile_bytes = (size_t)tile_w * (size_t)tile_h;
    const size_t header_bytes = 8 + (size_t)palette_count * 3;
    if (blob.size < header_bytes) {
        fprintf(stderr, "%s is not a Dark Colony BTS terrain tile set\n", path);
        W_FreeFile(&blob);
        return false;
    }
    int count = (int)read_u32_le(blob.bytes + 4);
    const size_t record_bytes = 4 + tile_bytes;
    if (count <= 0 || count > 4096 ||
        blob.size < header_bytes + (size_t)count * record_bytes) {
        fprintf(stderr, "%s has unsupported Dark Colony BTS tile records\n", path);
        W_FreeFile(&blob);
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
        free(animate_tile); free(record_keys); W_FreeFile(&blob);
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
        free(animate_tile); free(record_keys); W_FreeFile(&blob);
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
        V_BlitIndexed(rgba, atlas_w, atlas_h, tx, ty, src, tile_w, tile_h, palette);
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
            R_AddTileAnim(out, (int)key, frames, wave_phase_count, 180);
            extra_key += extra_phase_count;
        }
    }
    out->texture = I_CreateTexture(renderer, rgba, atlas_w, atlas_h, true);
    out->tile_lookup = tile_lookup;
    out->tile_lookup_count = lookup_count;
    out->count = total_tiles;
    out->atlas_cols = TILE_ATLAS_COLS;
    out->tile_w = tile_w;
    out->tile_h = tile_h;
    out->draw_y_offset = 0;
    free(rgba); free(animate_tile); free(record_keys); W_FreeFile(&blob);
    if (!out->texture) { R_FreeTileset(out); return false; }
    return true;
}
