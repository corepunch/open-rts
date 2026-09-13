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

bool range_ok(size_t size, uint32_t offset, size_t length) {
    return (size_t)offset <= size && length <= size - (size_t)offset;
}

void map_data_destroy(void *opaque) {
    KkndMapData *data = opaque;
    if (!data) return;
    free(data->pixels);
    free(data);
}

bool open_lvl(const char *path, blob_t *blob, const uint8_t **segment,
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

static bool lvl_file_list(const uint8_t *segment, size_t size, const char type[4],
                               uint32_t *list_start, uint32_t *list_end) {
    if (size < 4) return false;
    uint32_t types = read_u32_le(segment);
    if (!range_ok(size, types, 8)) return false;
    for (uint32_t pos = types; range_ok(size, pos, 16); pos += 8) {
        uint32_t list = read_u32_le(segment + pos + 4);
        if (list == 0) break;
        uint32_t next = read_u32_le(segment + pos + 12);
        if (memcmp(segment + pos, type, 4) == 0) {
            if (next == 0) next = types;
            if (list > next || !range_ok(size, list, (size_t)next - list)) return false;
            *list_start = list;
            *list_end = next;
            return true;
        }
    }
    return false;
}

bool lvl_asset(const uint8_t *segment, size_t size, const char type[4],
                    int index, uint32_t *asset_offset) {
    uint32_t start = 0, end = 0;
    if (index < 0 || !lvl_file_list(segment, size, type, &start, &end)) return false;
    size_t slot = (size_t)start + (size_t)index * 4;
    if (!range_ok(size, (uint32_t)slot, 4) || slot >= end) return false;
    uint32_t offset = read_u32_le(segment + slot);
    if (offset == 0 || offset >= size) return false;
    *asset_offset = offset;
    return true;
}

static bool cplc_node_ok(uint32_t offset, uint32_t cplc_end, size_t size) {
    return offset >= 20 && offset < cplc_end &&
           range_ok(size, offset, 64) && offset + 64 <= cplc_end;
}

int load_kknd_map_units(const char *path, KkndMapUnit *out, int max_units) {
    if (!out || max_units <= 0) return 0;

    blob_t blob;
    const uint8_t *segment;
    size_t size;
    if (!open_lvl(path, &blob, &segment, &size)) return 0;

    uint32_t cplc, mapd;
    bool ok = lvl_asset(segment, size, "CPLC", 0, &cplc) &&
              lvl_asset(segment, size, "MAPD", 0, &mapd) &&
              cplc < mapd && range_ok(size, cplc, 20);
    if (!ok) {
        W_FreeFile(&blob);
        return 0;
    }

    uint32_t cplc_size = read_u32_le(segment + cplc);
    if (cplc_size < 20 || cplc_size > mapd - cplc) {
        W_FreeFile(&blob);
        return 0;
    }
    /* The header's file_size covers the structured data, but its strings are
       stored in the space before the following MAPD asset. */
    uint32_t cplc_end = mapd;

    size_t visited_size = (size_t)cplc_end;
    uint8_t *visited = calloc(visited_size, 1);
    if (!visited) {
        W_FreeFile(&blob);
        return 0;
    }

    int count = 0;
    for (int list = 0; list < 4; ++list) {
        uint32_t node = read_u32_le(segment + cplc + 4 + (size_t)list * 4);
        while (node != 0) {
            if (!cplc_node_ok(node, cplc_end, size)) {
                fprintf(stderr, "%s has an invalid CPLC node at 0x%x\n", path, node);
                free(visited);
                W_FreeFile(&blob);
                return 0;
            }
            size_t visit = node;
            if (visited[visit]) break;
            visited[visit] = 1;

            const uint8_t *record = segment + node;
            uint32_t name_offset = read_u32_le(record + 52);
            if (name_offset < cplc_end && name_offset >= cplc &&
                range_ok(size, name_offset, 1)) {
                size_t name_size = cplc_end - name_offset;
                size_t name_length = strnlen((const char *)segment + name_offset, name_size);
                if (name_length < sizeof(out[0].name) &&
                    name_length >= 5 &&
                    strncmp((const char *)segment + name_offset, "UNIT_", 5) == 0 &&
                    strcmp((const char *)segment + name_offset, "UNIT_DUMMY") != 0) {
                    if (count < max_units) {
                        KkndMapUnit *unit = &out[count];
                        memset(unit, 0, sizeof(*unit));
                        memcpy(unit->name, segment + name_offset, name_length);
                        unit->native_team = read_u16_le(record + 56);
                        unit->position = (fvec2_t){
                            (float)read_u32_le(record + 5) / 32.0f,
                            (float)read_u32_le(record + 9) / 32.0f,
                        };
                    }
                    count++;
                }
            }
            node = read_u32_le(record + 16 + (size_t)list * 4);
        }
    }

    free(visited);
    W_FreeFile(&blob);
    return count > max_units ? max_units : count;
}
