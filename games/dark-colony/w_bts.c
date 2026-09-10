#include "engine.h"

/* Preserve the existing water selection until retail cycling is verified. */
static bool palette_index_is_water(uint8_t index, const uint32_t palette[256]) {
    uint32_t color = palette[index];
    uint8_t r = (uint8_t)(color >> 16);
    uint8_t g = (uint8_t)(color >> 8);
    uint8_t b = (uint8_t)color;
    return index >= 201 && index <= 211 && r < 80 && g > 36 && b > 36 && g + b > r * 2;
}

static bool palette_index_is_wave(uint8_t index, const uint32_t palette[256]) {
    return index >= 201 && index <= 207 && palette_index_is_water(index, palette);
}

static bool tile_has_water(const uint8_t *src, const uint32_t palette[256],
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
        if (palette_index_is_water(index, palette)) water++;
    }
    return water >= 96 && water * 4 >= opaque;
}

bool load_dark_colony_tileset(const char *path, tileset_t *out) {
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
    uint32_t *palette = out->palette;
    for (int i = 0; i < palette_count; ++i) {
        const uint8_t *p = blob.bytes + 8 + i * 3;
        int r = clamp255((int)p[0] * 4 + 3);
        int g = clamp255((int)p[1] * 4 + 3);
        int b = clamp255((int)p[2] * 4 + 3);
        bool transparent = (i == 0) || (r == 255 && g == 3 && b == 255);
        palette[i] = (transparent ? 0x00000000u : 0xff000000u) |
                     ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }

    for (int index = 201; index <= 207; ++index)
        if (palette_index_is_wave(index, palette))
            out->palette_cycle.indices[out->palette_cycle.count++] = index;
    out->palette_cycle.frame_ms = 180;

    int max_key = 0;
    for (int tile = 0; tile < count; ++tile) {
        const uint8_t *record = blob.bytes + header_bytes + (size_t)tile * record_bytes;
        uint32_t key = read_u32_le(record);
        if (key <= UINT16_MAX && (int)key > max_key) max_key = (int)key;
    }
    out->tile_lookup_count = SDL_max(max_key + 1, count);
    out->tile_lookup = malloc((size_t)out->tile_lookup_count * sizeof(*out->tile_lookup));
    out->indices = malloc((size_t)count * tile_bytes);
    out->palette_cycle.tiles = calloc((size_t)count, 1);
    if (!out->tile_lookup || !out->indices || !out->palette_cycle.tiles) {
        W_FreeFile(&blob);
        R_FreeTileset(out);
        return false;
    }
    for (int i = 0; i < out->tile_lookup_count; ++i) out->tile_lookup[i] = -1;
    /* MTG stores sequential record indices; MAP uses the native BTS keys. */
    for (int i = 0; i < count; ++i) out->tile_lookup[i] = i;
    for (int tile = 0; tile < count; ++tile) {
        const uint8_t *record = blob.bytes + header_bytes + (size_t)tile * record_bytes;
        uint32_t key = read_u32_le(record);
        if (key <= (uint32_t)max_key) out->tile_lookup[key] = tile;
        memcpy(out->indices + (size_t)tile * tile_bytes, record + 4, tile_bytes);
        out->palette_cycle.tiles[tile] = out->palette_cycle.count > 1 &&
                                         tile_has_water(record + 4, palette, tile_bytes);
    }
    out->count = count;
    out->tile_w = tile_w;
    out->tile_h = tile_h;
    W_FreeFile(&blob);
    return true;
}
