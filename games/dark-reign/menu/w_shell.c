#include "engine.h"
#include "dark-reign.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* SHELL.RLI indexes LZSS-packed "TLF." images in SHELL.RLD. Opening and
 * decoding: dkreign.exe 0x57e0f0 and 0x57e430; see docs/DR_EXE_FINDINGS.md,
 * Native shell. */
static blob_t rli, rld;

void DR_ShellClose(void) {
    W_FreeFile(&rli);
    W_FreeFile(&rld);
    memset(&rli, 0, sizeof(rli));
    memset(&rld, 0, sizeof(rld));
}

bool DR_ShellOpen(const char *root) {
    char path[1024];
    DR_ShellClose();
    M_PathJoin(path, sizeof(path), root, "shell/SHELL.RLI");
    if (!W_ReadFile(path, &rli)) return false;
    M_PathJoin(path, sizeof(path), root, "shell/SHELL.RLD");
    if (!W_ReadFile(path, &rld) || rli.size < 12 || memcmp(rli.bytes, "ILR.", 4) ||
        12 + (size_t)read_i32_le(rli.bytes + 8) * 32 > rli.size) {
        DR_ShellClose();
        return false;
    }
    return true;
}

/* 0x57e430: one flag byte per eight items, least significant bit first; a
 * set bit is a literal, a clear bit a 12-bit distance back into the output
 * (stored as 4096 - distance) and a 4-bit length - 3. There is no ring
 * buffer; overlapping copies repeat bytes. */
static bool unpack(const uint8_t *src, size_t size, uint8_t *out, size_t capacity) {
    const uint8_t *end = src + size;
    size_t n = 0;
    unsigned flags = 1;
    while (src < end) {
        if (flags == 1) flags = *src++ | 0x100u;
        if (flags & 1) {
            if (src == end || n == capacity) return false;
            out[n++] = *src++;
        } else {
            if (end - src < 2) return false;
            size_t distance = 4096 - (src[0] | (size_t)(src[1] & 15) << 8);
            size_t length = (src[1] >> 4) + 3u;
            src += 2;
            if (distance > n || length > capacity - n) return false;
            for (; length; --length, ++n) out[n] = out[n - distance];
        }
        flags >>= 1;
    }
    return n == capacity;
}

static uint8_t *entry(const char *name, size_t *size) {
    if (!rli.bytes) return NULL;
    int count = read_i32_le(rli.bytes + 8);
    for (int i = 0; i < count; ++i) {
        const uint8_t *e = rli.bytes + 12 + (size_t)i * 32;
        if (strncasecmp((const char *)e, name, 12) || memcmp(e + 12, "TLF.", 4)) continue;
        size_t offset = (uint32_t)read_i32_le(e + 20), packed = (uint32_t)read_i32_le(e + 24);
        *size = (uint32_t)read_i32_le(e + 28);
        if (offset > rld.size || packed > rld.size - offset || *size < 8) return NULL;
        uint8_t *out = malloc(*size);
        if (out && unpack(rld.bytes + offset, packed, out, *size)) return out;
        free(out);
        return NULL;
    }
    return NULL;
}

/* 0x57e580: chunks follow the 8-byte header as {tag, size with header};
 * tags are byte-reversed multi-character constants. */
static const uint8_t *chunk(const uint8_t *tlf, size_t size, const char tag[4]) {
    size_t total = (uint32_t)read_i32_le(tlf + 4);
    if (memcmp(tlf, "TLF.", 4) || total > size) return NULL;
    for (size_t at = 8; at + 8 <= total;) {
        size_t length = (uint32_t)read_i32_le(tlf + at + 4);
        if (length < 8 || length > total - at) return NULL;
        if (!memcmp(tlf + at, tag, 4)) return tlf + at;
        at += length;
    }
    return NULL;
}

/* "3BGR" holds 256 8-bit RGB triples; "LXIP" a zero word, u16 width and
 * height and the indexed rows. */
bool DR_ShellImage(const char *name, spritesheet_t *out) {
    memset(out, 0, sizeof(*out));
    size_t size;
    uint8_t *tlf = entry(name, &size);
    if (!tlf) return false;
    const uint8_t *rgb = chunk(tlf, size, "3BGR"), *pixl = chunk(tlf, size, "LXIP");
    bool ok = rgb && pixl && read_i32_le(rgb + 4) == 8 + 768 && read_i32_le(pixl + 4) >= 16;
    int w = ok ? read_u16_le(pixl + 12) : 0, h = ok ? read_u16_le(pixl + 14) : 0;
    ok = ok && w > 0 && h > 0 && (size_t)read_i32_le(pixl + 4) == 16 + (size_t)w * h &&
         R_AllocSpriteCells(out, 1);
    if (ok) ok = (out->lumps[0].indices = malloc((size_t)w * h)) != NULL;
    if (ok) {
        memcpy(out->lumps[0].indices, pixl + 16, (size_t)w * h);
        out->cells[0].rect = out->cells[0].bounds = (irect_t){0, 0, w, h};
        out->frame_size = (isize2_t){w, h};
        out->indexed = true;
        for (int i = 0; i < 256; ++i)
            out->palette[i] = out->source_palette[i] = 0xff000000u |
                (uint32_t)rgb[8 + i * 3] << 16 | (uint32_t)rgb[9 + i * 3] << 8 | rgb[10 + i * 3];
    } else R_FreeSprite(out);
    free(tlf);
    return ok;
}

/* 0x57ad10: a font is one strip. The first pixel's colour separates the
 * glyphs of codes 0..255 along the top row, starting at x 1; the glyph
 * width is its advance. The PCX fonts of the multiplayer screens use the
 * same strip below a marker row. */
bool DR_StripFont(const spritesheet_t *strip, int top, bitmapfont_t *out) {
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < 128; ++i) out->glyph_index[i] = -1;
    if (!strip->numlumps) return false;
    const uint8_t *pixels = strip->lumps[0].indices;
    int w = strip->cells[0].rect.w, h = strip->cells[0].rect.h - top;
    if (h <= 0 || !R_AllocSpriteCells(&out->sprite, 128)) return false;
    memcpy(out->sprite.palette, strip->palette, sizeof(out->sprite.palette));
    memcpy(out->sprite.source_palette, strip->source_palette, sizeof(out->sprite.source_palette));
    out->sprite.indexed = true;
    for (int x = 1, code = 0; x < w && code < 128; ++code) {
        int start = x;
        while (x < w && pixels[x] != pixels[0]) ++x;
        int gw = x++ - start;
        if (gw <= 0) continue;
        uint8_t *glyph = malloc((size_t)gw * h);
        if (!glyph) { HU_FreeFont(out); return false; }
        for (int y = 0; y < h; ++y) memcpy(glyph + y * gw, pixels + (size_t)(y + top) * w + start, gw);
        out->sprite.lumps[code].indices = glyph;
        out->sprite.cells[code].rect = out->sprite.cells[code].bounds = (irect_t){0, 0, gw, h};
        out->glyph_index[code] = code;
        out->glyph_width[code] = (uint8_t)gw;
        if (gw > out->glyph_size.w) out->glyph_size.w = gw;
    }
    out->glyph_size.h = out->line_h = h;
    out->draw_divisor = 1;
    out->native_origin = true;
    out->own_palette = true;
    return out->glyph_index['A'] >= 0;
}

bool DR_ShellFont(const char *name, bitmapfont_t *out) {
    spritesheet_t strip;
    if (!DR_ShellImage(name, &strip)) return false;
    bool ok = DR_StripFont(&strip, 0, out);
    R_FreeSprite(&strip);
    return ok;
}
