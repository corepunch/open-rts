#define _DEFAULT_SOURCE
#include "z.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* PAC members are LZSS: a flag byte, most significant bit first, picks a
 * literal (1) or a little-endian pair (0) holding a 12-bit position in a
 * 4096-byte ring and a 4-bit length less two. The ring starts zeroed and
 * fills from position 1. */
bool Z_Unpack(const uint8_t *src, size_t size, uint8_t *dst, size_t length) {
    uint8_t ring[4096] = {0};
    unsigned r = 1;
    size_t in = 0, out = 0;
    while (out < length) {
        if (in >= size) return false;
        uint8_t flags = src[in++];
        for (int bit = 0x80; bit && out < length; bit >>= 1) {
            if (flags & bit) {
                if (in >= size) return false;
                dst[out++] = ring[r++ & 4095] = src[in++];
                continue;
            }
            if (size - in < 2) return false;
            unsigned pair = read_u16_le(src + in);
            in += 2;
            for (unsigned i = 0; i < (pair & 15) + 2 && out < length; ++i)
                dst[out++] = ring[r++ & 4095] = ring[((pair >> 4) + i) & 4095];
        }
        r &= 4095;
    }
    return true;
}

/* The directory ends the file: 24-byte entries (name[16], offset, unpacked
 * length), then their count and "JMP2". */
bool Z_ReadPac(const char *path, const char *name, blob_t *out) {
    *out = (blob_t){0};
    blob_t pac;
    if (!W_ReadFile(path, &pac)) return false;
    const uint8_t *data = pac.bytes;
    bool ok = false;
    if (pac.size < 8 || memcmp(data + pac.size - 4, "JMP2", 4)) goto done;
    uint32_t count = read_u32_le(data + pac.size - 8);
    if (count > (pac.size - 8) / 24) goto done;
    const uint8_t *dir = data + pac.size - 8 - (size_t)count * 24;
    size_t end = (size_t)(dir - data);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *entry = dir + i * 24;
        if (strncasecmp((const char *)entry, name, 16)) continue;
        uint32_t offset = read_u32_le(entry + 16), length = read_u32_le(entry + 20);
        if (offset > end) break;
        out->bytes = malloc((size_t)length + 1);
        if (!out->bytes) break;
        out->size = length;
        out->bytes[length] = 0;
        ok = Z_Unpack(data + offset, end - offset, out->bytes, length);
        if (!ok) W_FreeFile(out);
        break;
    }
done:
    W_FreeFile(&pac);
    return ok;
}
