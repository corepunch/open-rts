#include "w_sprite_private.h"
#include <stdlib.h>
#include <string.h>

void DC_DecodePalette(const uint8_t *spr, size_t size, uint32_t colors[256]) {
    if (size < 8 + 256 * 3) return;
    const uint8_t *p = spr + 8;
    for (int i = 0; i < 256; ++i) {
        int r = clamp255((int)p[i * 3 + 0] * 4 + 3);
        int g = clamp255((int)p[i * 3 + 1] * 4 + 3);
        int b = clamp255((int)p[i * 3 + 2] * 4 + 3);
        colors[i] = i == 0 ? 0x00000000u :
            (0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
    }
}

void DC_FreeJuice(JuiceFile *juice) {
    if (!juice) return;
    for (int i = 0; i < juice->cell_count; ++i) free(juice->cells[i].data);
    free(juice->cells);
    memset(juice, 0, sizeof(*juice));
}

bool DC_LoadJuice(const char *path, JuiceFile *out) {
    memset(out, 0, sizeof(*out));
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return false;
    if (blob.size < 8 + 256 * 3) { W_FreeFile(&blob); return false; }

    out->flags = read_u16_le(blob.bytes + 0);
    out->cell_count = read_u16_le(blob.bytes + 2);
    out->payload_bytes = read_u32_le(blob.bytes + 4);
    out->chunked = (out->flags & 0x180) != 0;
    if (out->cell_count == 0 || out->cell_count > 1024) {
        W_FreeFile(&blob);
        return false;
    }

    DC_DecodePalette(blob.bytes, blob.size, out->palette);
    size_t desc_off = 8 + 256 * 3;
    size_t data_off = desc_off + (size_t)out->cell_count * 8;
    if (data_off > blob.size) {
        W_FreeFile(&blob);
        return false;
    }

    out->cells = calloc(out->cell_count, sizeof(*out->cells));
    if (!out->cells) {
        W_FreeFile(&blob);
        return false;
    }
    for (int i = 0; i < out->cell_count; ++i) {
        const uint8_t *desc = blob.bytes + desc_off + (size_t)i * 8;
        JuiceCell *cell = &out->cells[i];
        cell->width = read_u16_le(desc + 0);
        cell->height = read_u16_le(desc + 2);
        cell->dis_x = read_u16_le(desc + 4);
        cell->dis_y = read_u16_le(desc + 6);
        cell->used = cell->width > 0 && cell->height > 0;
        if (cell->width > 512 || cell->height > 512) {
            DC_FreeJuice(out);
            W_FreeFile(&blob);
            return false;
        }
    }

    size_t src_pos = data_off;
    if (out->chunked) {
        uint32_t total_chunks = 0;
        for (int i = 0; i < out->cell_count; ++i) {
            if (src_pos + 4 > blob.size) {
                DC_FreeJuice(out);
                W_FreeFile(&blob);
                return false;
            }
            uint32_t chunk_len = read_u32_le(blob.bytes + src_pos);
            src_pos += 4;
            total_chunks += chunk_len;
            if (total_chunks > out->payload_bytes || src_pos + chunk_len > blob.size) {
                DC_FreeJuice(out);
                W_FreeFile(&blob);
                return false;
            }
            JuiceCell *cell = &out->cells[i];
            cell->data_size = chunk_len;
            cell->data = malloc(chunk_len > 0 ? chunk_len : 1);
            if (!cell->data) {
                DC_FreeJuice(out);
                W_FreeFile(&blob);
                return false;
            }
            if (chunk_len > 0) memcpy(cell->data, blob.bytes + src_pos, chunk_len);
            src_pos += chunk_len;
        }
    } else {
        for (int i = 0; i < out->cell_count; ++i) {
            JuiceCell *cell = &out->cells[i];
            uint32_t pixel_count = (uint32_t)cell->width * (uint32_t)cell->height;
            cell->data_size = pixel_count;
            if (pixel_count == 0) continue;
            if (src_pos + pixel_count > blob.size) {
                DC_FreeJuice(out);
                W_FreeFile(&blob);
                return false;
            }
            cell->data = malloc(pixel_count);
            if (!cell->data) {
                DC_FreeJuice(out);
                W_FreeFile(&blob);
                return false;
            }
            memcpy(cell->data, blob.bytes + src_pos, pixel_count);
            src_pos += pixel_count;
        }
    }
    W_FreeFile(&blob);
    return true;
}

