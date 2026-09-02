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

/* ── tileset ────────────────────────────────────────────────────────────── */

static void dark_til_combine_add(const uint8_t *a, const uint8_t *b, uint8_t *out) {
    for (int i = 0; i < TILE_PIX_W * TILE_PIX_H; ++i) {
        int v = (int)a[i] + (int)b[i];
        out[i] = (uint8_t)(v > 255 ? 255 : v);
    }
}

static void dark_til_combine_min(const uint8_t *a, const uint8_t *b, uint8_t *out) {
    for (int i = 0; i < TILE_PIX_W * TILE_PIX_H; ++i)
        out[i] = a[i] < b[i] ? a[i] : b[i];
}

static void dark_til_mask_tile(uint32_t *dst, const uint32_t *src, const uint8_t *mask) {
    for (int i = 0; i < TILE_PIX_W * TILE_PIX_H; ++i) {
        uint32_t rgb   = src[i] & 0x00ffffffu;
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

bool load_dark_tileset(SDL_Renderer *renderer, const char *path, const uint32_t palette[256],
                       tileset_t *out) {
    memset(out, 0, sizeof(*out));
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return false;
    if (blob.size < 8 || memcmp(blob.bytes, "TILE", 4) != 0) {
        fprintf(stderr, "%s is not a Dark Reign TILE tileset\n", path);
        W_FreeFile(&blob); return false;
    }
    size_t tile_bytes = (size_t)read_u32_le(blob.bytes + 4);
    if (tile_bytes != TILE_PIX_W * TILE_PIX_H || blob.size < 8 + tile_bytes) {
        fprintf(stderr, "%s has unsupported tile dimensions/count\n", path);
        W_FreeFile(&blob); return false;
    }

    const int max_frames = 1103;
    uint32_t *frame_pixels = calloc((size_t)max_frames * TILE_PIX_W * TILE_PIX_H, sizeof(uint32_t));
    uint8_t *mask_frames   = calloc((size_t)256 * TILE_PIX_W * TILE_PIX_H, sizeof(uint8_t));
    if (!frame_pixels || !mask_frames) {
        free(frame_pixels); free(mask_frames); W_FreeFile(&blob); return false;
    }

    int count = 0;
    size_t pos = 8;
    const size_t normal_chunk = 1 + tile_bytes;
#define ADD_INDEXED_FRAME(SRC) do { \
    if (count < max_frames) { \
        V_IndexedToRGBA(frame_pixels + (size_t)count * tile_bytes, (SRC), tile_bytes, palette); \
        count++; \
    } \
} while (0)

    for (int i = 0; i < 128 && pos + normal_chunk <= blob.size; ++i) {
        ADD_INDEXED_FRAME(blob.bytes + pos + 1); pos += normal_chunk;
    }
    for (int i = 0; i < 64 && pos + tile_bytes <= blob.size; ++i) {
        ADD_INDEXED_FRAME(blob.bytes + pos); pos += tile_bytes;
    }
    for (int art = 0; art < 4 && pos < blob.size; ++art) {
        pos += normal_chunk;
        for (int shore = 0; shore < 14 && pos + normal_chunk <= blob.size; ++shore) {
            ADD_INDEXED_FRAME(blob.bytes + pos + 1); pos += normal_chunk;
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
        { 0, 1, 2, 3 }, { 64, 65, 66, 67 }, { 128, 129, 130, 131 }, { 192, 193, 194, 195 },
    };
    uint8_t north[TILE_PIX_W*TILE_PIX_H], east[TILE_PIX_W*TILE_PIX_H];
    uint8_t south[TILE_PIX_W*TILE_PIX_H], west[TILE_PIX_W*TILE_PIX_H];
    uint8_t ne_inner[TILE_PIX_W*TILE_PIX_H], nw_inner[TILE_PIX_W*TILE_PIX_H];
    uint8_t sw_inner[TILE_PIX_W*TILE_PIX_H], se_inner[TILE_PIX_W*TILE_PIX_H];
    uint8_t ne_sw_bridge[TILE_PIX_W*TILE_PIX_H], nw_se_bridge[TILE_PIX_W*TILE_PIX_H];
    const uint8_t *masks[14];

    for (int set = 0; set < 4; ++set) {
        const uint8_t *se = mask_frames + (size_t)corner_sets[set][0] * tile_bytes;
        const uint8_t *sw = mask_frames + (size_t)corner_sets[set][1] * tile_bytes;
        const uint8_t *nw = mask_frames + (size_t)corner_sets[set][2] * tile_bytes;
        const uint8_t *ne = mask_frames + (size_t)corner_sets[set][3] * tile_bytes;
        dark_til_combine_add(nw, ne, north); dark_til_combine_add(se, ne, east);
        dark_til_combine_add(sw, se, south); dark_til_combine_add(sw, nw, west);
        dark_til_combine_add(north, east, ne_inner); dark_til_combine_add(north, west, nw_inner);
        dark_til_combine_add(south, west, sw_inner); dark_til_combine_add(south, east, se_inner);
        dark_til_combine_min(ne_inner, sw_inner, ne_sw_bridge);
        dark_til_combine_min(nw_inner, se_inner, nw_se_bridge);
        masks[0]=se; masks[1]=sw; masks[2]=nw; masks[3]=ne;
        masks[4]=south; masks[5]=west; masks[6]=north; masks[7]=east;
        masks[8]=se_inner; masks[9]=sw_inner; masks[10]=nw_inner; masks[11]=ne_inner;
        masks[12]=ne_sw_bridge; masks[13]=nw_se_bridge;
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
        {192,206,220,234},{193,207,221,235},{195,209,223,237},{199,213,227,241},
        {194,208,222,236},{197,211,225,239},{203,217,231,245},{200,214,228,242},
        {202,216,230,244},{198,212,226,240},{205,219,233,247},{204,218,232,246},
        {196,210,224,238},{201,215,229,243},
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
    {
        const uint8_t *se2 = mask_frames + (size_t)shadow_set[0] * tile_bytes;
        const uint8_t *sw2 = mask_frames + (size_t)shadow_set[1] * tile_bytes;
        const uint8_t *nw2 = mask_frames + (size_t)shadow_set[2] * tile_bytes;
        const uint8_t *ne2 = mask_frames + (size_t)shadow_set[3] * tile_bytes;
        dark_til_combine_add(nw2, ne2, north); dark_til_combine_add(se2, ne2, east);
        dark_til_combine_add(sw2, se2, south); dark_til_combine_add(sw2, nw2, west);
        dark_til_combine_add(north, east, ne_inner); dark_til_combine_add(north, west, nw_inner);
        dark_til_combine_add(south, west, sw_inner); dark_til_combine_add(south, east, se_inner);
        dark_til_combine_min(ne_inner, sw_inner, ne_sw_bridge);
        dark_til_combine_min(nw_inner, se_inner, nw_se_bridge);
        masks[0]=north; masks[1]=east; masks[2]=south; masks[3]=west;
        masks[4]=nw2; masks[5]=ne2; masks[6]=sw2; masks[7]=se2;
        masks[8]=sw_inner; masks[9]=se_inner; masks[10]=nw_inner; masks[11]=ne_inner;
        masks[12]=nw_se_bridge; masks[13]=ne_sw_bridge;
        for (int i = 0; i < 14 && count < max_frames; ++i) {
            dark_til_shadow_tile(frame_pixels + (size_t)count * tile_bytes, masks[i], 35);
            count++;
        }
        if (count < max_frames) {
            dark_til_shadow_tile(frame_pixels + (size_t)count * tile_bytes, NULL, 35);
            count++;
        }
    }
#undef ADD_INDEXED_FRAME

    int rows = (count + TILE_ATLAS_COLS - 1) / TILE_ATLAS_COLS;
    int atlas_w = TILE_ATLAS_COLS * TILE_PIX_W;
    int atlas_h = rows * TILE_PIX_H;
    uint32_t *rgba = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    if (!rgba) { free(frame_pixels); free(mask_frames); W_FreeFile(&blob); return false; }

    for (int tile = 0; tile < count; ++tile) {
        int tx = (tile % TILE_ATLAS_COLS) * TILE_PIX_W;
        int ty = (tile / TILE_ATLAS_COLS) * TILE_PIX_H;
        const uint32_t *tile_src = frame_pixels + (size_t)tile * tile_bytes;
        for (int y = 0; y < TILE_PIX_H; ++y)
            for (int x = 0; x < TILE_PIX_W; ++x)
                rgba[(ty + y) * atlas_w + tx + x] = tile_src[y * TILE_PIX_W + x];
    }
    out->texture   = I_CreateTexture(renderer, rgba, atlas_w, atlas_h, false);
    out->count     = count;
    out->atlas_cols = TILE_ATLAS_COLS;
    out->tile_w    = TILE_PIX_W;
    out->tile_h    = TILE_PIX_H;
    out->draw_y_offset = 0;
    free(rgba); free(frame_pixels); free(mask_frames); W_FreeFile(&blob);
    return out->texture != NULL;
}

void add_water_animations(tileset_t *tileset) {
    const int frame_ms = 180;
    for (int variation = 0; variation < 8 && variation < tileset->count; ++variation) {
        int frames[4] = { variation, (variation+1)&7, (variation+2)&7, (variation+1)&7 };
        R_AddTileAnim(tileset, variation, frames, 4, frame_ms);
    }
    for (int group = 0; group < 14; ++group) {
        int base = 1032 + group * 4;
        if (base + 3 >= tileset->count) break;
        int frames[4] = { base, base+1, base+2, base+3 };
        for (int i = 0; i < 4; ++i) {
            int rotated[4] = { frames[i], frames[(i+1)&3], frames[(i+2)&3], frames[(i+3)&3] };
            R_AddTileAnim(tileset, frames[i], rotated, 4, frame_ms);
        }
    }
}
