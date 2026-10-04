#include "w2_local.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* WAR/GRP layout reimplemented from the documented archive. Wargus is GPL and
 * is not copied. Transparent GRP runs are index 0 so the indexed blitter
 * skips them. Palette index 0 on a sprite is also marked transparent. */

static bool pull_u8(const uint8_t **p, const uint8_t *end, unsigned *out) {
    if (*p >= end) return false;
    *out = *(*p)++;
    return true;
}

static bool pull_u16(const uint8_t **p, const uint8_t *end, unsigned *out) {
    if ((size_t)(end - *p) < 2) return false;
    *out = (unsigned)(*p)[0] | ((unsigned)(*p)[1] << 8);
    *p += 2;
    return true;
}

void w2_blob_free(w2_blob_t *blob) {
    if (!blob) return;
    free(blob->data);
    blob->data = NULL;
    blob->size = 0;
}

void w2_archive_close(w2_archive_t *arc) {
    if (!arc) return;
    free(arc->file);
    free(arc->offsets);
    memset(arc, 0, sizeof(*arc));
}

bool w2_archive_open(w2_archive_t *arc, const char *path) {
    memset(arc, 0, sizeof(*arc));
    blob_t file;
    if (!W_ReadFile(path, &file)) {
        fprintf(stderr, "warcraft-2: cannot read %s\n", path);
        return false;
    }
    if (file.size < 8 || read_u32_le(file.bytes) != 0x19u) {
        fprintf(stderr, "warcraft-2: %s is not a WAR archive\n", path);
        W_FreeFile(&file);
        return false;
    }
    int count = read_u16_le(file.bytes + 4);
    int type = read_u16_le(file.bytes + 6);
    /* MAINDAT is type 1000. REZDAT is type 3000. The entry records match. */
    if (count <= 0 || (type != 1000 && type != 3000) ||
        file.size < 8u + (size_t)count * 4u) {
        fprintf(stderr, "warcraft-2: %s has %d entries of type %d\n", path, count, type);
        W_FreeFile(&file);
        return false;
    }
    uint32_t *offsets = calloc((size_t)count + 1, sizeof(uint32_t));
    if (!offsets) {
        W_FreeFile(&file);
        return false;
    }
    for (int i = 0; i < count; ++i)
        offsets[i] = read_u32_le(file.bytes + 8 + (size_t)i * 4);
    offsets[count] = (uint32_t)file.size;
    arc->file = file.bytes;
    arc->file_size = file.size;
    arc->count = count;
    arc->offsets = offsets;
    return true;
}

bool w2_archive_extract(const w2_archive_t *arc, int index, w2_blob_t *out) {
    memset(out, 0, sizeof(*out));
    if (!arc || index < 0 || index >= arc->count) return false;
    uint32_t at = arc->offsets[index];
    if (at > arc->file_size || arc->file_size - at < 4) return false;
    const uint8_t *src = arc->file + at;
    const uint8_t *end = arc->file + arc->file_size;
    unsigned header = read_u32_le(src);
    src += 4;
    unsigned flags = header >> 24;
    size_t length = header & 0x00ffffffu;
    if (length == 0 || length > W2_ENTRY_LIMIT) return false;
    uint8_t *dest = calloc(length, 1);
    if (!dest) return false;
    if (flags == 0x00) {
        if ((size_t)(end - src) < length) { free(dest); return false; }
        memcpy(dest, src, length);
    } else if (flags == 0x20) {
        uint8_t ring[4096];
        memset(ring, 0, sizeof(ring));
        size_t filled = 0;
        unsigned ring_i = 0;
        while (filled < length) {
            unsigned ctrl;
            if (!pull_u8(&src, end, &ctrl)) { free(dest); return false; }
            for (int bit = 0; bit < 8 && filled < length; ++bit) {
                if (ctrl & 1u) {
                    unsigned literal;
                    if (!pull_u8(&src, end, &literal)) { free(dest); return false; }
                    dest[filled++] = (uint8_t)literal;
                    ring[ring_i++ & 0xfffu] = (uint8_t)literal;
                } else {
                    unsigned packed;
                    if (!pull_u16(&src, end, &packed)) { free(dest); return false; }
                    unsigned run = (packed >> 12) + 3;
                    unsigned from = packed & 0xfffu;
                    for (unsigned n = 0; n < run && filled < length; ++n) {
                        uint8_t value = ring[from++ & 0xfffu];
                        dest[filled++] = value;
                        ring[ring_i++ & 0xfffu] = value;
                    }
                }
                ctrl >>= 1;
            }
        }
    } else {
        fprintf(stderr, "warcraft-2: entry %d uses flag 0x%02x\n", index, flags);
        free(dest);
        return false;
    }
    out->data = dest;
    out->size = length;
    return true;
}

bool w2_decode_palette(const w2_blob_t *entry, uint32_t palette[256]) {
    if (!entry || entry->size < 768) return false;
    for (int i = 0; i < 256; ++i) {
        unsigned r = (unsigned)entry->data[i * 3] << 2;
        unsigned g = (unsigned)entry->data[i * 3 + 1] << 2;
        unsigned b = (unsigned)entry->data[i * 3 + 2] << 2;
        palette[i] = 0xff000000u | (r << 16) | (g << 8) | b;
    }
    return true;
}

int w2_grp_entry(const mobjinfo_t *unit, int era, int archive_count) {
    if (!unit || archive_count <= 1) return 0;
    if (era < 0 || era > 3) era = 0;
    int entry = unit->w2.grp[era];
    if (entry <= 0 || entry >= archive_count) entry = unit->w2.grp[0];
    if (entry <= 0 || entry >= archive_count) return 0;
    return entry;
}

static const int w2_tileset_entries[4][4] = {
    { 2, 3, 4, 5 },
    { 18, 19, 20, 21 },
    { 10, 11, 12, 13 },
    /* This MAINDAT ends at 437. Expansion swamp tiles 438-441 are absent,
     * so swamp reuses the wasteland tiles the way non-expansion Wargus does. */
    { 10, 11, 12, 13 },
};

static void blit_minitile(uint8_t *tile, int mx, int my, const uint8_t *mini,
                          size_t mini_size, unsigned ref) {
    size_t base = (size_t)(ref & 0xfffcu) * 16u;
    if (base + 64 > mini_size) return;
    bool flip_x = (ref & 2u) != 0;
    bool flip_y = (ref & 1u) != 0;
    static const int flip8[8] = { 7, 6, 5, 4, 3, 2, 1, 0 };
    for (int y = 0; y < 8; ++y) {
        int sy = flip_y ? flip8[y] : y;
        for (int x = 0; x < 8; ++x) {
            int sx = flip_x ? flip8[x] : x;
            tile[(my * 8 + y) * 32 + mx * 8 + x] = mini[base + (size_t)sy * 8u + (size_t)sx];
        }
    }
}

bool w2_decode_tileset(const w2_archive_t *arc, int era, tileset_t *out) {
    memset(out, 0, sizeof(*out));
    if (era < 0 || era > 3) era = 0;
    const int *entries = w2_tileset_entries[era];
    w2_blob_t palette_blob = { 0 }, mega = { 0 }, mini = { 0 }, map = { 0 };
    bool ok = false;
    if (!w2_archive_extract(arc, entries[0], &palette_blob) ||
        !w2_archive_extract(arc, entries[1], &mega) ||
        !w2_archive_extract(arc, entries[2], &mini) ||
        !w2_archive_extract(arc, entries[3], &map)) {
        fprintf(stderr, "warcraft-2: tileset entries %d/%d/%d/%d failed\n",
                entries[0], entries[1], entries[2], entries[3]);
        goto done;
    }
    if (!w2_decode_palette(&palette_blob, out->palette)) goto done;
    if (mega.size < 32 || mega.size / 32 > 100000) goto done;
    if (map.size < 0x9eu * 42u) {
        fprintf(stderr, "warcraft-2: map table is %zu bytes\n", map.size);
        goto done;
    }
    int count = (int)(mega.size / 32u);
    size_t pixels = (size_t)count * 32u * 32u;
    uint8_t *indices = calloc(pixels, 1);
    int *lookup = malloc((size_t)(W2_TILE_LOOKUP + 1) * sizeof(int));
    if (!indices || !lookup) {
        free(indices);
        free(lookup);
        goto done;
    }
    for (int i = 0; i < W2_TILE_LOOKUP; ++i) lookup[i] = -1;
    for (int i = 0; i < count; ++i) {
        uint8_t *tile = indices + (size_t)i * 32u * 32u;
        if (i < 16) {
            size_t raw = (size_t)i * 32u * 32u;
            if (raw + 32u * 32u <= mini.size)
                memcpy(tile, mini.data + raw, 32u * 32u);
            continue;
        }
        const uint8_t *rec = mega.data + (size_t)i * 32u;
        for (int my = 0; my < 4; ++my)
            for (int mx = 0; mx < 4; ++mx) {
                unsigned ref = read_u16_le(rec + ((size_t)my * 4u + (size_t)mx) * 2u);
                blit_minitile(tile, mx, my, mini.data, mini.size, ref);
            }
    }
    for (int group = 0; group < 0x9e; ++group) {
        const uint8_t *rec = map.data + (size_t)group * 42u;
        for (int sub = 0; sub < 16; ++sub) {
            unsigned mega_i = read_u16_le(rec + (size_t)sub * 2u);
            int slot = (group << 4) | sub;
            lookup[slot] = mega_i < (unsigned)count ? (int)mega_i : -1;
        }
    }
    out->indices = indices;
    lookup[W2_TILE_LOOKUP] = 126; /* All four native tilesets: removed-tree. */
    out->tile_lookup = lookup;
    out->tile_lookup_count = W2_TILE_LOOKUP + 1;
    out->count = count;
    out->tile_w = 32;
    out->tile_h = 32;
    ok = true;
done:
    w2_blob_free(&palette_blob);
    w2_blob_free(&mega);
    w2_blob_free(&mini);
    w2_blob_free(&map);
    if (!ok) R_FreeTileset(out);
    return ok;
}

static bool decode_grp_frame(const uint8_t *blob, size_t size, int index,
                             int box_w, int box_h, uint8_t *dst, irect_t *bounds) {
    const uint8_t *hdr = blob + 6 + (size_t)index * 8;
    unsigned xoff = hdr[0];
    unsigned yoff = hdr[1];
    unsigned width = hdr[2];
    unsigned height = hdr[3];
    unsigned offset = read_u32_le(hdr + 4);
    if (offset & 0x80000000u) {
        offset &= 0x7fffffffu;
        width += 256;
    }
    *bounds = (irect_t){ (int)xoff, (int)yoff, (int)width, (int)height };
    if (width == 0 || height == 0) return true;
    if (width > 1024 || height > 1024) return false;
    if ((size_t)offset + (size_t)height * 2u > size) return false;
    const uint8_t *rows = blob + offset;
    const uint8_t *end = blob + size;
    for (unsigned y = 0; y < height; ++y) {
        unsigned row_at = read_u16_le(rows + (size_t)y * 2u);
        if ((size_t)offset + row_at > size) return false;
        const uint8_t *sp = rows + row_at;
        unsigned x = 0;
        while (x < width) {
            unsigned ctrl;
            if (!pull_u8(&sp, end, &ctrl)) return false;
            unsigned n;
            const uint8_t *pixels = NULL;
            uint8_t repeated = 0;
            if (ctrl & 0x80u) {
                n = ctrl & 0x7fu;
            } else if (ctrl & 0x40u) {
                n = ctrl & 0x3fu;
                unsigned value;
                if (!pull_u8(&sp, end, &value)) return false;
                repeated = (uint8_t)value;
                pixels = &repeated;
            } else {
                n = ctrl & 0x3fu;
                if ((size_t)(end - sp) < n) return false;
                pixels = sp;
                sp += n;
            }
            if (n == 0) return false;
            if (pixels) {
                unsigned keep = width - x;
                if (keep > n) keep = n;
                for (unsigned i = 0; i < keep; ++i) {
                    unsigned dx = xoff + x + i;
                    unsigned dy = yoff + y;
                    if (dx < (unsigned)box_w && dy < (unsigned)box_h)
                        dst[(size_t)dy * (size_t)box_w + dx] =
                            (ctrl & 0x40u) ? repeated : pixels[i];
                }
            }
            x += n;
        }
    }
    return true;
}

static bool install_directions(spritesheet_t *sprite, int count, bool directional,
                               int *phases) {
    bool five = directional && count >= 5 && count % 5 == 0;
    int frame_count = five ? count / 5 : 1;
    int rotations = five ? 8 : 1;
    if (!R_InitSpriteDef(sprite, frame_count, rotations)) return false;
    if (!five) {
        *phases = 1;
        return R_InstallSpriteLump(sprite, 0, 0, 0, false);
    }
    static const int slot_lump[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };
    static const bool slot_flip[8] = { false, true, true, true, false, false, false, false };
    for (int phase = 0; phase < frame_count; ++phase) {
        int base = phase * 5;
        for (int slot = 0; slot < 8; ++slot)
            if (!R_InstallSpriteLump(sprite, phase, slot, base + slot_lump[slot],
                                     slot_flip[slot]))
                return false;
    }
    *phases = frame_count;
    return true;
}

bool w2_decode_grp(const w2_blob_t *entry, const uint32_t palette[256],
                   spritesheet_t *out, bool directional, int *phases) {
    memset(out, 0, sizeof(*out));
    if (phases) *phases = 1;
    if (!entry || entry->size < 6) return false;
    int count = read_u16_le(entry->data);
    int box_w = read_u16_le(entry->data + 2);
    int box_h = read_u16_le(entry->data + 4);
    if (count < 1 || count > 512 || box_w < 1 || box_w > 512 || box_h < 1 || box_h > 512)
        return false;
    if (entry->size < 6u + (size_t)count * 8u) return false;
    if (!R_AllocSpriteCells(out, count)) return false;
    size_t pixels = (size_t)box_w * (size_t)box_h;
    out->frame_size = (isize2_t){ box_w, box_h };
    out->indexed = true;
    memcpy(out->palette, palette, sizeof(out->palette));
    memcpy(out->source_palette, palette, sizeof(out->source_palette));
    out->palette[0] = 0;
    out->source_palette[0] = 0;
    for (int i = 0; i < count; ++i) {
        uint8_t *image = calloc(pixels, 1);
        if (!image) { R_FreeSprite(out); return false; }
        irect_t bounds;
        if (!decode_grp_frame(entry->data, entry->size, i, box_w, box_h, image, &bounds)) {
            free(image);
            R_FreeSprite(out);
            return false;
        }
        if (bounds.x < 0) bounds.x = 0;
        if (bounds.y < 0) bounds.y = 0;
        if (bounds.x + bounds.w > box_w) bounds.w = box_w - bounds.x;
        if (bounds.y + bounds.h > box_h) bounds.h = box_h - bounds.y;
        if (bounds.w < 0) bounds.w = 0;
        if (bounds.h < 0) bounds.h = 0;
        out->lumps[i].indices = image;
        out->cells[i].rect = (irect_t){ 0, 0, box_w, box_h };
        out->cells[i].bounds = bounds;
        out->cells[i].ground_point = (ivec2_t){ box_w / 2, box_h };
    }
    int phase_count = 1;
    if (!install_directions(out, count, directional, &phase_count)) {
        R_FreeSprite(out);
        return false;
    }
    if (phases) *phases = phase_count;
    return true;
}

/* GRP art is painted with the red ramp. Players 0..6 occupy four palette
 * slots each, starting at 208, brightest first. Yellow is 12..15 in the
 * forest, winter, and wasteland palettes; 236..238 are brown and 239 is
 * black. Wargus states the same split: DefinePlayerColorIndex(208, 4) and a
 * separate yellow color. The red RGB below is shared by all three palettes
 * and is the check that these slots are the team ramps. Winter and wasteland
 * store different RGB in the blue, violet, and orange slots, so the remap is
 * by index. */
static const uint8_t w2_player_slots[8][4] = {
    { 208, 209, 210, 211 },
    { 212, 213, 214, 215 },
    { 216, 217, 218, 219 },
    { 220, 221, 222, 223 },
    { 224, 225, 226, 227 },
    { 228, 229, 230, 231 },
    { 232, 233, 234, 235 },
    { 12, 13, 14, 15 },
};

static const uint8_t w2_red_slot_rgb[4][3] = {
    { 164, 0, 0 }, { 124, 0, 0 }, { 92, 4, 0 }, { 68, 4, 0 },
};

int w2_install_team_colors(spritesheet_t *sprite, const uint32_t palette[256]) {
    int matched = 0;
    for (int shade = 0; shade < 4; ++shade) {
        unsigned index = w2_player_slots[0][shade];
        uint32_t rgb = ((uint32_t)w2_red_slot_rgb[shade][0] << 16) |
                       ((uint32_t)w2_red_slot_rgb[shade][1] << 8) |
                       w2_red_slot_rgb[shade][2];
        if ((palette[index] & 0x00ffffffu) == rgb) matched++;
    }
    if (matched != 4) return matched;
    spritepalettemap_t *maps = calloc(7, sizeof(*maps));
    if (!maps) return matched;
    for (int player = 1; player <= 7; ++player) {
        spritepalettemap_t *map = &maps[player - 1];
        map->id = player;
        for (int i = 0; i < 256; ++i) map->indices[i] = (uint8_t)i;
        for (int shade = 0; shade < 4; ++shade)
            map->indices[w2_player_slots[0][shade]] = w2_player_slots[player][shade];
    }
    free(sprite->palette_maps);
    sprite->palette_maps = maps;
    sprite->palette_map_count = 7;
    return matched;
}
