#define _DEFAULT_SOURCE
#include "kknd.h"
#include "w_lvl.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static uint32_t read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

bool kknd_range_ok(size_t size, uint32_t offset, size_t length) {
    return (size_t)offset <= size && length <= size - (size_t)offset;
}

void kknd_map_data_destroy(void *opaque) {
    KkndMapData *data = opaque;
    if (!data) return;
    for (int i = 0; i < KKND_MAX_LAYERS; ++i) free(data->layer_pixels[i]);
    free(data);
}

bool kknd_open_lvl(const char *path, blob_t *blob, const uint8_t **segment,
                   size_t *segment_size) {
    if (!W_ReadFile(path, blob)) return false;
    if (blob->size < 12 || memcmp(blob->bytes, "DATA", 4) != 0) {
        fprintf(stderr, "%s is not a KKnD DATA/LVL container\n", path);
        W_FreeFile(blob);
        return false;
    }
    size_t declared = read_u32_be(blob->bytes + 4);
    size_t available = blob->size - 8;
    if (declared == 0 || declared > available) declared = available;
    *segment = blob->bytes + 8;
    *segment_size = declared;
    return true;
}

static bool kknd_lvl_file_list(const uint8_t *segment, size_t size, const char type[4],
                               uint32_t *list_start, uint32_t *list_end) {
    if (size < 4) return false;
    uint32_t types = read_u32_le(segment);
    if (!kknd_range_ok(size, types, 8)) return false;
    for (uint32_t pos = types; kknd_range_ok(size, pos, 16); pos += 8) {
        uint32_t list = read_u32_le(segment + pos + 4);
        if (list == 0) break;
        uint32_t next = read_u32_le(segment + pos + 12);
        if (memcmp(segment + pos, type, 4) == 0) {
            if (next == 0) next = types;
            if (list > next || !kknd_range_ok(size, list, (size_t)next - list)) return false;
            *list_start = list;
            *list_end = next;
            return true;
        }
    }
    return false;
}

bool kknd_lvl_asset(const uint8_t *segment, size_t size, const char type[4],
                    int index, uint32_t *asset_offset) {
    uint32_t start = 0, end = 0;
    if (index < 0 || !kknd_lvl_file_list(segment, size, type, &start, &end)) return false;
    size_t slot = (size_t)start + (size_t)index * 4;
    if (!kknd_range_ok(size, (uint32_t)slot, 4) || slot >= end) return false;
    uint32_t offset = read_u32_le(segment + slot);
    if (offset == 0 || offset >= size) return false;
    *asset_offset = offset;
    return true;
}
