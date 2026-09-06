#define _DEFAULT_SOURCE
#include "engine.h"

#include <stdlib.h>
#include <string.h>

static int gif_read_code(const uint8_t *data, size_t size, size_t *bit_pos, int code_size) {
    int code = 0;
    for (int bit = 0; bit < code_size; ++bit) {
        size_t byte_pos = (*bit_pos) >> 3;
        if (byte_pos >= size) return -1;
        if (data[byte_pos] & (1u << ((*bit_pos) & 7))) code |= 1 << bit;
        (*bit_pos)++;
    }
    return code;
}

static bool gif_decode_lzw(const uint8_t *data, size_t size, int min_code_size,
                           uint8_t *out, size_t out_size) {
    if (!data || !out || min_code_size < 2 || min_code_size > 8) return false;
    enum { GIF_MAX_CODES = 4096 };
    int prefix[GIF_MAX_CODES];
    uint8_t suffix[GIF_MAX_CODES];
    uint8_t stack[GIF_MAX_CODES];
    int clear_code = 1 << min_code_size;
    int end_code = clear_code + 1;
    int code_size = min_code_size + 1;
    int next_code = end_code + 1;
    int old_code = -1;
    uint8_t first_char = 0;
    size_t bit_pos = 0, out_pos = 0;

    for (int i = 0; i < GIF_MAX_CODES; ++i) {
        prefix[i] = -1;
        suffix[i] = (uint8_t)i;
    }

    while (out_pos < out_size) {
        int code = gif_read_code(data, size, &bit_pos, code_size);
        if (code < 0) return false;
        if (code == clear_code) {
            code_size = min_code_size + 1;
            next_code = end_code + 1;
            old_code = -1;
            continue;
        }
        if (code == end_code) break;

        int stack_len = 0;
        int in_code = code;
        if (old_code >= 0 && code == next_code) {
            in_code = old_code;
            stack[stack_len++] = first_char;
        } else if (code > next_code) {
            return false;
        }

        int walk = in_code;
        while (walk >= clear_code) {
            if (walk < 0 || walk >= next_code || stack_len >= GIF_MAX_CODES) return false;
            stack[stack_len++] = suffix[walk];
            walk = prefix[walk];
        }
        if (walk < 0 || walk >= clear_code || stack_len >= GIF_MAX_CODES) return false;
        first_char = (uint8_t)walk;
        stack[stack_len++] = first_char;

        while (stack_len > 0 && out_pos < out_size) out[out_pos++] = stack[--stack_len];

        if (old_code >= 0 && next_code < GIF_MAX_CODES) {
            prefix[next_code] = old_code;
            suffix[next_code] = first_char;
            next_code++;
            if (next_code == (1 << code_size) && code_size < 12) code_size++;
        }
        old_code = code;
    }
    return out_pos == out_size;
}

bool W_LoadGIFTexture(SDL_Renderer *renderer, const char *path, spritesheet_t *out) {
    memset(out, 0, sizeof(*out));
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return false;
    const uint8_t *bytes = blob.bytes;
    size_t size = blob.size;
    if (size < 13 || (memcmp(bytes, "GIF87a", 6) != 0 && memcmp(bytes, "GIF89a", 6) != 0)) {
        W_FreeFile(&blob);
        return false;
    }

    int canvas_w = read_u16_le(bytes + 6);
    int canvas_h = read_u16_le(bytes + 8);
    uint8_t packed = bytes[10];
    bool has_global_palette = (packed & 0x80) != 0;
    int global_count = has_global_palette ? (2 << (packed & 0x07)) : 0;
    int bg_index = bytes[11];
    size_t pos = 13;
    uint32_t global_palette[256] = { 0 };
    if (global_count > 0) {
        if (pos + (size_t)global_count * 3 > size) { W_FreeFile(&blob); return false; }
        for (int i = 0; i < global_count; ++i) {
            const uint8_t *p = bytes + pos + (size_t)i * 3;
            global_palette[i] = 0xff000000u | ((uint32_t)p[0] << 16) |
                                ((uint32_t)p[1] << 8) | (uint32_t)p[2];
        }
        pos += (size_t)global_count * 3;
    }

    int transparent_index = -1;
    uint32_t *canvas = calloc((size_t)canvas_w * (size_t)canvas_h, sizeof(uint32_t));
    if (!canvas) { W_FreeFile(&blob); return false; }
    uint32_t bg = bg_index >= 0 && bg_index < global_count ? global_palette[bg_index] : 0xff000000u;
    for (int i = 0; i < canvas_w * canvas_h; ++i) canvas[i] = bg;

    bool decoded = false;
    while (pos < size && !decoded) {
        uint8_t marker = bytes[pos++];
        if (marker == 0x3b) break;
        if (marker == 0x21) {
            if (pos >= size) break;
            uint8_t label = bytes[pos++];
            while (pos < size) {
                uint8_t block_size = bytes[pos++];
                if (block_size == 0) break;
                if (label == 0xf9 && block_size == 4 && pos + 4 <= size) {
                    if (bytes[pos] & 0x01) transparent_index = bytes[pos + 3];
                }
                pos += block_size;
            }
            continue;
        }
        if (marker != 0x2c || pos + 9 > size) break;

        int left = read_u16_le(bytes + pos + 0);
        int top = read_u16_le(bytes + pos + 2);
        int image_w = read_u16_le(bytes + pos + 4);
        int image_h = read_u16_le(bytes + pos + 6);
        uint8_t image_packed = bytes[pos + 8];
        pos += 9;
        bool interlaced = (image_packed & 0x40) != 0;
        bool has_local_palette = (image_packed & 0x80) != 0;
        int local_count = has_local_palette ? (2 << (image_packed & 0x07)) : 0;
        uint32_t local_palette[256] = { 0 };
        uint32_t *palette = global_palette;
        int palette_count = global_count;
        if (has_local_palette) {
            if (pos + (size_t)local_count * 3 > size) break;
            for (int i = 0; i < local_count; ++i) {
                const uint8_t *p = bytes + pos + (size_t)i * 3;
                local_palette[i] = 0xff000000u | ((uint32_t)p[0] << 16) |
                                   ((uint32_t)p[1] << 8) | (uint32_t)p[2];
            }
            pos += (size_t)local_count * 3;
            palette = local_palette;
            palette_count = local_count;
        }
        if (pos >= size) break;
        int min_code_size = bytes[pos++];
        uint8_t *compressed = NULL;
        size_t compressed_size = 0;
        while (pos < size) {
            uint8_t block_size = bytes[pos++];
            if (block_size == 0) break;
            if (pos + block_size > size) { free(compressed); free(canvas); W_FreeFile(&blob); return false; }
            uint8_t *next = realloc(compressed, compressed_size + block_size);
            if (!next) { free(compressed); free(canvas); W_FreeFile(&blob); return false; }
            compressed = next;
            memcpy(compressed + compressed_size, bytes + pos, block_size);
            compressed_size += block_size;
            pos += block_size;
        }

        uint8_t *indices = malloc((size_t)image_w * (size_t)image_h);
        if (!indices) { free(compressed); free(canvas); W_FreeFile(&blob); return false; }
        bool ok = gif_decode_lzw(compressed, compressed_size, min_code_size,
                                 indices, (size_t)image_w * (size_t)image_h);
        free(compressed);
        if (!ok) { free(indices); break; }

        int pass_starts[4] = { 0, 4, 2, 1 };
        int pass_steps[4] = { 8, 8, 4, 2 };
        size_t src = 0;
        for (int pass = 0; pass < (interlaced ? 4 : 1); ++pass) {
            int y_start = interlaced ? pass_starts[pass] : 0;
            int y_step = interlaced ? pass_steps[pass] : 1;
            for (int y = y_start; y < image_h; y += y_step) {
                for (int x = 0; x < image_w && src < (size_t)image_w * (size_t)image_h; ++x, ++src) {
                    int index = indices[src];
                    int dx = left + x, dy = top + y;
                    if (dx < 0 || dy < 0 || dx >= canvas_w || dy >= canvas_h ||
                        index == transparent_index || index >= palette_count) {
                        continue;
                    }
                    canvas[dy * canvas_w + dx] = palette[index];
                }
            }
        }
        free(indices);
        decoded = true;
    }

    if (!decoded) {
        free(canvas);
        W_FreeFile(&blob);
        return false;
    }
    out->textures[0] = I_CreateTexture(renderer, canvas, canvas_w, canvas_h, false);
    free(canvas);
    W_FreeFile(&blob);
    if (!out->textures[0]) return false;
    out->lumps = calloc(1, sizeof(*out->lumps));
    out->spritedef.spriteframes = calloc(1, sizeof(*out->spritedef.spriteframes));
    if (!out->lumps || !out->spritedef.spriteframes) {
        R_FreeSprite(out);
        return false;
    }
    out->lumps[0].rect = (irect_t){ 0, 0, canvas_w, canvas_h };
    out->lumps[0].bounds = out->lumps[0].rect;
    out->lumps[0].ground_point = (ivec2_t){ canvas_w / 2, canvas_h };
    out->numlumps = 1;
    out->spritedef.numframes = 1;
    out->spritedef.rotations = 1;
    out->spritedef.spriteframes[0].lump[0] = 0;
    out->frame_w = canvas_w;
    out->frame_h = canvas_h;
    return true;
}
