#define _DEFAULT_SOURCE
#include "kknd.h"
#include "w_lvl.h"
#include "info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static uint16_t cplc_to_type(const char *name) {
    for (int i = 1; i < NUMMOBJTYPES; ++i)
        if (cplc_names[i] && strcasecmp(cplc_names[i], name) == 0)
            return (uint16_t)i;
    return 0;
}

static void decode_cplc(const uint8_t *segment, size_t size,
                         uint32_t cplc_offset, KkndMapData *native) {
    if (!range_ok(size, cplc_offset, 4)) return;
    uint32_t strtab = read_u32_le(segment + cplc_offset);
    if (!strtab || strtab >= size) return;
    /* Unit records follow a 584-byte (146 × uint32_le) header.
       Each record is 132 bytes: +0x08=type-string ptr, +0x0c=team,
       +0x5c=X pixel (>>8), +0x60=Y pixel (>>8). */
    uint32_t pos = cplc_offset + 584;
    while (pos + 132 <= strtab) {
        uint32_t type_ptr = read_u32_le(segment + pos + 8);
        if (type_ptr >= strtab && type_ptr + 5 < size &&
            strncmp((const char *)(segment + type_ptr), "UNIT_", 5) == 0) {
            const char *type_str = (const char *)(segment + type_ptr);
            uint16_t type = cplc_to_type(type_str);
            if (type) {
                uint32_t team  = read_u32_le(segment + pos + 0x0c);
                uint32_t x_enc = read_u32_le(segment + pos + 0x5c);
                uint32_t y_enc = read_u32_le(segment + pos + 0x60);
                KkndUnitPlacement *u = realloc(native->units,
                    (size_t)(native->unit_count + 1) * sizeof(*native->units));
                if (!u) return;
                native->units = u;
                u[native->unit_count++] = (KkndUnitPlacement){
                    .type  = type,
                    .owner = (team == 2) ? 1 : 0,
                    .x     = (float)(x_enc >> 8) / 32.0f,
                    .y     = (float)(y_enc >> 8) / 32.0f,
                };
            }
            pos += 132;
        } else {
            pos += 4;
        }
    }
}
static bool decode_mapd(const uint8_t *segment, size_t size, uint32_t mapd_offset,
                         level_t *out, KkndMapData *native) {
    if (!range_ok(size, mapd_offset, 12)) return false;
    uint32_t layers = read_u32_le(segment + mapd_offset);
    if (!layers || layers > MAX_LAYERS ||
        !range_ok(size, mapd_offset + 4, (size_t)layers * 4 + 4)) return false;
    const uint8_t *offsets = segment + mapd_offset + 4;
    uint32_t colors = read_u32_le(offsets + layers * 4);
    uint32_t palette_offset = mapd_offset + 8 + layers * 4;
    if (!colors || colors > 256 || !range_ok(size, palette_offset, (size_t)colors * 4)) return false;
    for (uint32_t i = 0; i < colors; ++i) {
        const uint8_t *color = segment + palette_offset + i * 4;
        native->palette[i] = 0xff000000u | ((uint32_t)color[0] << 16) |
                              ((uint32_t)color[1] << 8) | color[2];
    }
    native->palette[0] = 0xff000000u;
    for (uint32_t layer = 0; layer < layers; ++layer) {
        uint32_t offset = read_u32_le(offsets + layer * 4);
        if (!range_ok(size, offset, 20) || memcmp(segment + offset, "LRCS", 4) != 0) return false;
        isize2_t tile = { read_i32_le(segment + offset + 4), read_i32_le(segment + offset + 8) };
        isize2_t grid = { read_i32_le(segment + offset + 12), read_i32_le(segment + offset + 16) };
        if (tile.w != 32 || tile.h != 32 || grid.w <= 0 || grid.h <= 0 ||
            grid.w > 512 || grid.h > 512) return false;
        size_t cells = (size_t)grid.w * grid.h;
        if (!range_ok(size, offset + 20, cells * 4)) return false;
        if (layer == 0) {
            if (cells * layers + 1 > UINT16_MAX) return false;
            out->width = grid.w;
            out->height = grid.h;
            out->tile_ids = calloc(cells, sizeof(*out->tile_ids));
            out->blocked = calloc(cells, sizeof(*out->blocked));
            native->tile_count = (int)(cells * layers + 1);
            native->atlas = (isize2_t){ 64 * 32, ((native->tile_count + 63) / 64) * 32 };
            native->pixels = calloc((size_t)native->atlas.w * native->atlas.h, sizeof(*native->pixels));
            if (!out->tile_ids || !out->blocked || !native->pixels) return false;
        } else {
            if (grid.w != out->width || grid.h != out->height) return false;
            out->tile_overlays[layer - 1] = calloc(cells, sizeof(*out->tile_overlays[layer - 1]));
            if (!out->tile_overlays[layer - 1]) return false;
            out->tile_overlay_count++;
        }
        for (size_t cell = 0; cell < cells; ++cell) {
            int frame = layer == 0 ? (int)cell : (int)(cells * layer + 1 + cell);
            if (layer == 0) out->tile_ids[cell] = (uint16_t)frame;
            uint32_t source = read_u32_le(segment + offset + 20 + cell * 4);
            if (!source) continue;
            if (!range_ok(size, source, 4 + 32 * 32)) return false;
            const uint8_t *indices = segment + source + 4;
            ivec2_t origin = { (frame % 64) * 32, (frame / 64) * 32 };
            bool visible = false;
            for (int y = 0; y < 32; ++y) {
                uint32_t *row = native->pixels + (size_t)(origin.y + y) * native->atlas.w + origin.x;
                for (int x = 0; x < 32; ++x) {
                    uint8_t index = indices[y * 32 + x];
                    row[x] = layer && !index ? 0 : native->palette[index];
                    if (row[x]) visible = true;
                }
            }
            if (layer && visible) out->tile_overlays[layer - 1][cell] = (uint16_t)frame;
        }
    }
    return true;
}

bool load_kknd_map(const char *map_path, level_t *out) {
    memset(out, 0, sizeof(*out));
    blob_t blob;
    const uint8_t *segment;
    size_t size;
    if (!open_lvl(map_path, &blob, &segment, &size)) return false;
    KkndMapData *native = calloc(1, sizeof(*native));
    out->native_data = native;
    out->destroy_native_data = map_data_destroy;
    uint32_t mapd, cplc;
    bool ok = native && lvl_asset(segment, size, "MAPD", 0, &mapd) &&
              decode_mapd(segment, size, mapd, out, native);
    if (ok && lvl_asset(segment, size, "CPLC", 0, &cplc))
        decode_cplc(segment, size, cplc, native);
    W_FreeFile(&blob);
    if (!ok) {
        fprintf(stderr, "%s has an invalid or unsupported KKnD MAPD asset\n", map_path);
        P_FreeLevel(out);
        return false;
    }
    /* Bridges and cliff faces compose in the world-object pass. */
    out->render_capabilities = MAP_RENDER_CAP_ZERO_TILE_EMPTY |
                               MAP_RENDER_CAP_DEPTH_SORTED_TILE_LAYERS;
    out->has_camera = true;
    out->camera = (fvec2_t){ out->width * 0.5f, out->height * 0.5f };
    snprintf(out->tileset_name, sizeof(out->tileset_name), "SURV_01 MAPD");
    return true;
}
