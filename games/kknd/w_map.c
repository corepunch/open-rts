#define _DEFAULT_SOURCE
#include "kknd.h"
#include "w_lvl.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static bool kknd_decode_mapd(const uint8_t *segment, size_t size, uint32_t mapd_offset,
                             KkndMapData *out) {
    if (!kknd_range_ok(size, mapd_offset, 12)) return false;
    uint32_t layer_count = read_u32_le(segment + mapd_offset);
    if (layer_count == 0 || layer_count > KKND_MAX_LAYERS ||
        !kknd_range_ok(size, mapd_offset + 4, (size_t)layer_count * 4 + 4)) return false;
    const uint8_t *header = segment + mapd_offset;
    uint32_t layer_offsets[KKND_MAX_LAYERS] = {0};
    for (uint32_t i = 0; i < layer_count; ++i) layer_offsets[i] = read_u32_le(header + 4 + i * 4);
    uint32_t palette_count = read_u32_le(header + 4 + layer_count * 4);
    uint32_t palette_offset = mapd_offset + 8 + layer_count * 4;
    if (palette_count == 0 || palette_count > 256 ||
        !kknd_range_ok(size, palette_offset, (size_t)palette_count * 4)) return false;
    for (uint32_t i = 0; i < palette_count; ++i) {
        const uint8_t *color = segment + palette_offset + i * 4;
        out->palette[i] = 0xff000000u | ((uint32_t)color[0] << 16) |
                          ((uint32_t)color[1] << 8) | color[2];
    }
    out->palette[0] = 0xff000000u;

    for (uint32_t layer_index = 0; layer_index < layer_count; ++layer_index) {
        uint32_t offset = layer_offsets[layer_index];
        if (!kknd_range_ok(size, offset, 20) || memcmp(segment + offset, "LRCS", 4) != 0) return false;
        int tile_w = (int)read_u32_le(segment + offset + 4);
        int tile_h = (int)read_u32_le(segment + offset + 8);
        int tiles_x = (int)read_u32_le(segment + offset + 12);
        int tiles_y = (int)read_u32_le(segment + offset + 16);
        if (tile_w != 32 || tile_h != 32 || tiles_x <= 0 || tiles_y <= 0 ||
            tiles_x > 512 || tiles_y > 512) return false;
        size_t cells = (size_t)tiles_x * (size_t)tiles_y;
        if (!kknd_range_ok(size, offset + 20, cells * 4)) return false;
        if (layer_index == 0) {
            out->width = tiles_x;
            out->height = tiles_y;
        } else if (out->width != tiles_x || out->height != tiles_y) {
            fprintf(stderr, "KKnD MAPD layers have mismatched dimensions\n");
            return false;
        }
        uint32_t *pixels = calloc(cells * 32u * 32u, sizeof(uint32_t));
        if (!pixels) return false;
        out->layer_pixels[layer_index] = pixels;
        for (size_t cell = 0; cell < cells; ++cell) {
            uint32_t tile = read_u32_le(segment + offset + 20 + cell * 4);
            if (tile == 0) continue;
            if (!kknd_range_ok(size, tile, 4 + 32u * 32u)) return false;
            const uint8_t *indices = segment + tile + 4; /* Gen1 tile prefix. */
            for (size_t pixel = 0; pixel < 32u * 32u; ++pixel) {
                uint8_t index = indices[pixel];
                if (layer_index > 0 && index == 0) pixels[cell * 1024u + pixel] = 0;
                else pixels[cell * 1024u + pixel] = out->palette[index];
            }
        }
    }
    out->layer_count = (int)layer_count;
    return true;
}

bool load_kknd_map(const char *map_path, level_t *out) {
    memset(out, 0, sizeof(*out));
    blob_t blob = {0};
    const uint8_t *segment = NULL;
    size_t segment_size = 0;
    if (!kknd_open_lvl(map_path, &blob, &segment, &segment_size)) return false;
    uint32_t mapd = 0;
    if (!kknd_lvl_asset(segment, segment_size, "MAPD", 0, &mapd)) {
        fprintf(stderr, "%s has no MAPD asset\n", map_path);
        W_FreeFile(&blob);
        return false;
    }
    KkndMapData *native = calloc(1, sizeof(*native));
    if (!native || !kknd_decode_mapd(segment, segment_size, mapd, native)) {
        fprintf(stderr, "%s has an invalid or unsupported KKnD MAPD asset\n", map_path);
        kknd_map_data_destroy(native);
        W_FreeFile(&blob);
        return false;
    }
    W_FreeFile(&blob);

    size_t cells = (size_t)native->width * (size_t)native->height;
    if (cells == 0 || cells * (size_t)native->layer_count + 1u > UINT16_MAX) {
        fprintf(stderr, "%s is too large for the current 16-bit terrain index contract\n", map_path);
        kknd_map_data_destroy(native);
        return false;
    }
    out->tile_ids = calloc(cells, sizeof(uint16_t));
    out->blocked = calloc(cells, 1);
    if (!out->tile_ids || !out->blocked) {
        kknd_map_data_destroy(native);
        free(out->tile_ids);
        free(out->blocked);
        memset(out, 0, sizeof(*out));
        return false;
    }
    out->width = native->width;
    out->height = native->height;
    for (size_t i = 0; i < cells; ++i) out->tile_ids[i] = (uint16_t)i;
    for (int layer = 1; layer < native->layer_count && layer - 1 < MAX_TILE_OVERLAYS; ++layer) {
        out->tile_overlays[layer - 1] = calloc(cells, sizeof(uint16_t));
        if (!out->tile_overlays[layer - 1]) {
            P_FreeLevel(out);
            kknd_map_data_destroy(native);
            return false;
        }
        for (size_t cell = 0; cell < cells; ++cell) {
            const uint32_t *tile = native->layer_pixels[layer] + cell * 1024u;
            bool visible = false;
            for (size_t p = 0; p < 1024u; ++p) if (tile[p] != 0) { visible = true; break; }
            if (visible) out->tile_overlays[layer - 1][cell] = (uint16_t)(cells * layer + 1 + cell);
        }
        out->tile_overlay_count++;
    }
    /* KKnD's second MAPD layer contains bridges, cliff faces, and other art
       that units can pass behind, so compose it in the world-object pass. */
    out->render_features = MAP_RENDER_SKIP_ZERO_TILES | MAP_RENDER_INTERLEAVED_OVERLAYS;
    /* MOBD facing 0 = north, indices increase clockwise — same convention as
       Dark Reign.  Without this the default compass16 scheme returns even
       codes 0/2/4…14 and misses the odd-numbered MOBD facings 1/3/5…15. */
    out->direction_mode = RTS_DIRECTION_DARK_REIGN_8;
    out->native_data = native;
    out->destroy_native_data = kknd_map_data_destroy;
    out->has_camera = true;
    out->camera_gx = (float)out->width * 0.5f;
    out->camera_gy = (float)out->height * 0.5f;
    snprintf(out->tileset_name, sizeof(out->tileset_name), "SURV_01 MAPD");
    return true;
}
