#define _DEFAULT_SOURCE
#include "engine.h"
#include "sl_types.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── BIM / COL helpers ──────────────────────────────────────────────────── */

/* COL file: 256 colours × 3 bytes (R,G,B), 6-bit VGA range (0-63).
   BIM transparency is encoded by absent scan-line spans, so every palette
   entry (including index zero) remains opaque. */
static bool sl_load_col_palette(const char *path, uint32_t palette[256]) {
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return false;
    if (blob.size < 256 * 3) {
        fprintf(stderr, "7legion: %s too short for 256-colour palette\n", path);
        W_FreeFile(&blob);
        return false;
    }
    const uint8_t *p = (const uint8_t *)blob.bytes;
    for (int i = 0; i < 256; ++i) {
        int r = ((int)p[i * 3 + 0] * 255 + 31) / 63;
        int g = ((int)p[i * 3 + 1] * 255 + 31) / 63;
        int b = ((int)p[i * 3 + 2] * 255 + 31) / 63;
        palette[i] = 0xff000000u | ((uint32_t)r << 16) |
                     ((uint32_t)g << 8) | (uint32_t)b;
    }
    W_FreeFile(&blob);
    return true;
}

/* BIM file layout:
     [offset table]  N × uint32_le, where N = first_offset / 4
     [frame data]    each frame: uint16_le width + uint16_le height + w*h bytes
   Tiles in TILES*.BIM are always 32×32 uncompressed palette-indexed pixels. */

#define ATLAS_COLS 64

static bool sl_load_bim_tileset(SDL_Renderer *renderer, const char *path,
                                const uint32_t palette[256], tileset_t *out) {
    memset(out, 0, sizeof(*out));

    blob_t blob;
    if (!W_ReadFile(path, &blob)) return false;
    if (blob.size < 4) { W_FreeFile(&blob); return false; }

    const uint8_t *data = (const uint8_t *)blob.bytes;
    uint32_t first_offset = read_u32_le(data);
    if (first_offset == 0 || first_offset % 4 != 0 || first_offset > blob.size) {
        fprintf(stderr, "7legion: %s: bad BIM offset table\n", path);
        W_FreeFile(&blob);
        return false;
    }
    int tile_count = (int)(first_offset / 4);

    /* Validate and count usable tiles. */
    int usable = 0;
    for (int i = 0; i < tile_count; ++i) {
        uint32_t off = read_u32_le(data + (size_t)i * 4);
        if (off + 4 > blob.size) break;
        uint16_t w = read_u16_le(data + off);
        uint16_t h = read_u16_le(data + off + 2);
        if (w != TILE_W || h != TILE_H) break;
        if (off + 4 + (size_t)w * h > blob.size) break;
        usable++;
    }
    if (usable == 0) {
        fprintf(stderr, "7legion: %s: no usable 32×32 tiles\n", path);
        W_FreeFile(&blob);
        return false;
    }

    int atlas_rows = (usable + ATLAS_COLS - 1) / ATLAS_COLS;
    int atlas_w    = ATLAS_COLS * TILE_W;
    int atlas_h    = atlas_rows  * TILE_H;

    uint32_t *rgba = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    if (!rgba) { W_FreeFile(&blob); return false; }

    for (int i = 0; i < usable; ++i) {
        uint32_t off = read_u32_le(data + (size_t)i * 4);
        const uint8_t *pixels = data + off + 4; /* skip 4-byte header */

        int tx = (i % ATLAS_COLS) * TILE_W;
        int ty = (i / ATLAS_COLS) * TILE_H;
        for (int py = 0; py < TILE_H; ++py) {
            for (int px = 0; px < TILE_W; ++px) {
                uint8_t idx = pixels[py * TILE_W + px];
                rgba[(ty + py) * atlas_w + tx + px] = palette[idx];
            }
        }
    }

    out->texture    = I_CreateTexture(renderer, rgba, atlas_w, atlas_h, false);
    out->count      = usable;
    out->atlas_cols = ATLAS_COLS;
    out->tile_w     = TILE_W;
    out->tile_h     = TILE_H;

    free(rgba);
    W_FreeFile(&blob);
    return out->texture != NULL;
}

/* ── sparse BIM sprites ─────────────────────────────────────────────────── */

/* Sprite BIM frames use the same leading uint32 offset table as tile BIMs.
   A frame is:

       uint16 pixel_data_offset;       relative to the frame start
       uint16 height;
       for each scan line:
           uint16 span_count;
           span_count * { uint16 x; uint16 length; }
       uint8 pixels[sum(span lengths)];

   The x values are absolute positions on the frame canvas, not deltas.  Empty
   pixels are transparent; palette index zero inside a span is a real colour. */
typedef struct {
    uint16_t data_offset;
    uint16_t height;
    int width;
    int min_x;
    int min_y;
    int max_x;
    int max_y;
    size_t pixel_count;
} SlBimFrameInfo;

/* VCLZ is the small 4 KiB-window LZSS wrapper used by a few large BIMs.
   Control bits are consumed least-significant first: one means a literal;
   zero means a 12-bit ring position plus a four-bit length stored as
   length - 3.  The write cursor starts 18 bytes before the ring wraps. */
static bool sl_expand_vclz(const uint8_t *source, size_t source_size,
                           uint8_t **out_data, size_t *out_size) {
    if (!source || source_size < 8 || memcmp(source, "VCLZ", 4) != 0) return false;
    uint32_t expanded_size = read_u32_le(source + 4);
    if (expanded_size == 0) return false;
    uint8_t *expanded = malloc(expanded_size);
    if (!expanded) return false;
    uint8_t ring[4096] = { 0 };
    size_t ring_write = 4096 - 18;

    size_t src = 8;
    size_t dst = 0;
    while (dst < expanded_size && src < source_size) {
        uint8_t control = source[src++];
        for (int bit = 0; bit < 8 && dst < expanded_size; ++bit) {
            if ((control & (1u << bit)) != 0) {
                if (src >= source_size) { free(expanded); return false; }
                uint8_t value = source[src++];
                expanded[dst++] = value;
                ring[ring_write] = value;
                ring_write = (ring_write + 1) & 0x0fffu;
                continue;
            }
            if (src + 2 > source_size) { free(expanded); return false; }
            uint8_t low = source[src];
            uint8_t high = source[src + 1];
            src += 2;
            size_t ring_read = (size_t)low | ((size_t)(high & 0xf0u) << 4);
            size_t length = (size_t)(high & 0x0fu) + 3;
            if (length > expanded_size - dst) {
                free(expanded);
                return false;
            }
            for (size_t i = 0; i < length; ++i) {
                uint8_t value = ring[ring_read];
                ring_read = (ring_read + 1) & 0x0fffu;
                expanded[dst++] = value;
                ring[ring_write] = value;
                ring_write = (ring_write + 1) & 0x0fffu;
            }
        }
    }
    if (dst != expanded_size) { free(expanded); return false; }
    *out_data = expanded;
    *out_size = expanded_size;
    return true;
}

static bool sl_bim_frame_info(const uint8_t *data, size_t size,
                              uint32_t offset, uint32_t end,
                              SlBimFrameInfo *out) {
    memset(out, 0, sizeof(*out));
    out->min_x = INT32_MAX;
    out->min_y = INT32_MAX;
    if (offset == end) return true;
    if ((size_t)offset + 4 > size || end < offset || end > size) return false;

    out->data_offset = read_u16_le(data + offset);
    out->height = read_u16_le(data + offset + 2);
    /* Several BIMs terminate their offset table with a four-byte null frame.
       Its first word is not a meaningful relative data offset. */
    if (out->height == 0) return true;
    if (out->data_offset < 4 || (size_t)offset + out->data_offset > end) return false;

    size_t cursor = (size_t)offset + 4;
    for (int y = 0; y < out->height; ++y) {
        if (cursor + 2 > (size_t)offset + out->data_offset) return false;
        uint16_t span_count = read_u16_le(data + cursor);
        cursor += 2;
        for (int span = 0; span < span_count; ++span) {
            if (cursor + 4 > (size_t)offset + out->data_offset) return false;
            uint16_t x = read_u16_le(data + cursor);
            uint16_t length = read_u16_le(data + cursor + 2);
            cursor += 4;
            if (length == 0 || (uint32_t)x + length > INT16_MAX) return false;
            if ((int)x < out->min_x) out->min_x = x;
            if (y < out->min_y) out->min_y = y;
            if ((int)x + length > out->max_x) out->max_x = (int)x + length;
            out->max_y = y + 1;
            out->pixel_count += length;
        }
    }
    if (cursor != (size_t)offset + out->data_offset) return false;
    if ((size_t)offset + out->data_offset + out->pixel_count > end) return false;
    out->width = out->max_x;
    if (out->min_x == INT32_MAX) out->min_x = out->min_y = 0;
    return true;
}

static bool sl_load_bim_sprite(SDL_Renderer *renderer, const char *path,
                               const uint32_t palette[256], spritesheet_t *out) {
    memset(out, 0, sizeof(*out));

    blob_t blob;
    if (!W_ReadFile(path, &blob)) return false;
    uint32_t *rgba = NULL;
    if (blob.size >= 4 && memcmp(blob.bytes, "VCLZ", 4) == 0) {
        blob_t expanded = {0};
        if (!sl_expand_vclz(blob.bytes, blob.size, &expanded.bytes, &expanded.size)) goto fail;
        W_FreeFile(&blob);
        blob = expanded;
    }
    if (blob.size < 4) goto fail;
    const uint8_t *data = blob.bytes;
    uint32_t first_offset = read_u32_le(data);
    if (first_offset == 0 || first_offset % 4 != 0 || first_offset > blob.size) goto fail;
    int table_count = (int)(first_offset / 4);
    if (!R_AllocSpriteCells(out, table_count)) goto fail;
    isize2_t canvas = { 1, 1 };
    int frame_count = 0;
    for (int i = 0; i < table_count; ++i) {
        uint32_t offset = read_u32_le(data + (size_t)i * 4);
        uint32_t end = i + 1 < table_count ? read_u32_le(data + (size_t)(i + 1) * 4) : (uint32_t)blob.size;
        SlBimFrameInfo info;
        if (offset < first_offset || end < offset || end > blob.size ||
            !sl_bim_frame_info(data, blob.size, offset, end, &info)) goto fail;
        out->cells[i].bounds = (irect_t){ info.min_x, info.min_y,
                                         info.max_x - info.min_x, info.max_y - info.min_y };
        if (info.width > canvas.w) canvas.w = info.width;
        if (info.height > canvas.h) canvas.h = info.height;
        if (info.height) frame_count = i + 1;
    }
    if (!frame_count) goto fail;
    out->numlumps = frame_count;
    out->frame_size = canvas;
    size_t pixels = (size_t)canvas.w * canvas.h;
    rgba = calloc(pixels, sizeof(*rgba));
    if (!rgba) goto fail;
    for (int i = 0; i < frame_count; ++i) {
        spritecell_t *cell = &out->cells[i];
        cell->rect = (irect_t){ 0, 0, canvas.w, canvas.h };
        cell->ground_point = (ivec2_t){ canvas.w / 2, canvas.h };
        memset(rgba, 0, pixels * sizeof(*rgba));
        uint32_t offset = read_u32_le(data + (size_t)i * 4);
        uint32_t end = i + 1 < table_count ? read_u32_le(data + (size_t)(i + 1) * 4) : (uint32_t)blob.size;
        if (offset != end) {
            int height = read_u16_le(data + offset + 2);
            size_t command = (size_t)offset + 4;
            size_t source = (size_t)offset + read_u16_le(data + offset);
            for (int y = 0; y < height; ++y) {
                int spans = read_u16_le(data + command);
                command += 2;
                for (int span = 0; span < spans; ++span) {
                    int x = read_u16_le(data + command), length = read_u16_le(data + command + 2);
                    command += 4;
                    V_IndexedToRGBA(rgba + (size_t)y * canvas.w + x, data + source, length, palette);
                    source += length;
                }
            }
        }
        out->lumps[i].texture = I_CreateTexture(renderer, rgba, canvas.w, canvas.h, true);
        if (!out->lumps[i].texture) goto fail;
    }
    /* Movement sheets are arranged as eight contiguous facing blocks. */
    int rotations = frame_count >= 8 && frame_count % 8 == 0 ? 8 : 1;
    int frames = frame_count / rotations;
    if (!R_InitSpriteDef(out, frames, rotations)) goto fail;
    for (int frame = 0; frame < frames; ++frame)
        for (int rotation = 0; rotation < rotations; ++rotation)
            R_InstallSpriteLump(out, frame, (rotations - rotation) % rotations,
                                rotation * frames + frame, false);
    free(rgba);
    W_FreeFile(&blob);
    return true;
fail:
    free(rgba);
    W_FreeFile(&blob);
    R_FreeSprite(out);
    return false;
}

static const char *sl_palette_for_tileset(const level_t *map) {
    if (map && strcmp(map->tileset_name, "GFX/TILES1.BIM") == 0) return "GFX/PAL2.COL";
    if (map && strcmp(map->tileset_name, "GFX/TILES2.BIM") == 0) return "GFX/PAL3.COL";
    if (map && strcmp(map->tileset_name, "GFX/TILES3.BIM") == 0) return "GFX/PAL4.COL";
    return "GFX/PAL1.COL";
}
bool sl_load_assets(SDL_Renderer *renderer, const char *data_root,
                    const level_t *map,
                    const char *sprite_name,
                    tileset_t *tileset, spritesheet_t *unit_sprite) {

    /* Palette */
    uint32_t palette[256];
    char col_path[512];
    snprintf(col_path, sizeof(col_path), "%s/%s", data_root, sl_palette_for_tileset(map));
    if (!sl_load_col_palette(col_path, palette)) {
        fprintf(stderr, "7legion: failed to load palette %s\n", col_path);
        return false;
    }

    /* tileset_t */
    char til_path[512];
    snprintf(til_path, sizeof(til_path), "%s/%s", data_root,
             map && map->tileset_name[0] ? map->tileset_name : "GFX/TILES.BIM");
    if (!sl_load_bim_tileset(renderer, til_path, palette, tileset)) {
        fprintf(stderr, "7legion: failed to load tileset %s\n", til_path);
        return false;
    }

    /* mobj_t sprite */
    char sprite_path[512];
    snprintf(sprite_path, sizeof(sprite_path), "%s/%s", data_root,
             sprite_name && sprite_name[0] ? sprite_name : "GFX/TROOP1W.BIM");
    if (!sl_load_bim_sprite(renderer, sprite_path, palette, unit_sprite)) {
        fprintf(stderr, "7legion: failed to load sprite %s\n", sprite_path);
        R_FreeTileset(tileset);
        return false;
    }
    return true;
}

static bool sl_cache_bim_sprite(spritecache_t *cache, SDL_Renderer *renderer,
                                const char *data_root, const char *sprite_name,
                                const uint32_t palette[256]) {
    if (!sprite_name || sprite_name[0] == '\0') return true;
    if (R_CacheFind(cache, sprite_name)) return true;
    if (cache->count >= MAX_DECORATION_SPRITES) return false;

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", data_root, sprite_name);
    cachedsprite_t *entry = &cache->entries[cache->count];
    memset(entry, 0, sizeof(*entry));
    if (!sl_load_bim_sprite(renderer, path, palette, &entry->sprite)) {
        fprintf(stderr, "7legion: failed to load runtime sprite %s\n", path);
        return false;
    }
    snprintf(entry->name, sizeof(entry->name), "%s", sprite_name);
    cache->count++;
    return true;
}

bool sl_load_runtime_sprites(SDL_Renderer *renderer, const char *data_root,
                             const level_t *map, mobj_t *const *units, int unit_count,
                             spritecache_t *cache) {
    uint32_t palette[256];
    char col_path[512];
    snprintf(col_path, sizeof(col_path), "%s/%s", data_root, sl_palette_for_tileset(map));
    if (!sl_load_col_palette(col_path, palette)) return false;

    bool ok = true;
    for (int i = 0; i < unit_count; ++i) {
        if (!sl_cache_bim_sprite(cache, renderer, data_root,
                     units[i]->core.sprite_name, palette))
            ok = false;
    }
    return ok;
}
