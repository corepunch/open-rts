#define _DEFAULT_SOURCE
#include "kknd.h"
#include "w_lvl.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { MAX_ANIMATIONS = 512, MAX_FRAMES = 4096 };
static bool build_map_tileset(SDL_Renderer *renderer, const KkndMapData *map,
                                   tileset_t *out) {
    out->texture = I_CreateTexture(renderer, map->pixels, map->atlas.w, map->atlas.h, true);
    if (!out->texture) return false;
    out->count = map->tile_count;
    out->atlas_cols = 64;
    out->tile_w = 32;
    out->tile_h = 32;
    return true;
}

static int sprite_member_index(const char *name) {
    if (!name || !*name) return -1;
    if (isdigit((unsigned char)name[0])) return atoi(name);
    static const struct { const char *name; int index; } names[] = {
        { "Infantry.mobd", 34 }, { "Shotgunner.mobd", 68 },
        { "Bike.mobd", 7 }, { "Cursors.mobd", 17 },
        { "Tank.mobd", 77 }, { "Technician.mobd", 78 },
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcasecmp(name, names[i].name) == 0) return names[i].index;
    return -1;
}

static bool decode_mobd_image(SDL_Renderer *renderer, const uint8_t *segment, size_t size,
                               uint32_t frame_offset, const uint32_t palette[256],
                               spritecell_t *cell, spritelump_t *lump, bool *flip_out) {
    if (!range_ok(size, frame_offset, 28)) return false;
    ivec2_t offset = { read_i32_le(segment + frame_offset), read_i32_le(segment + frame_offset + 4) };
    uint32_t flags_offset = read_u32_le(segment + frame_offset + 12);
    if (!range_ok(size, flags_offset, 12) || memcmp(segment + flags_offset, "TRPS", 4) != 0)
        return false;
    uint32_t flags = read_u32_le(segment + flags_offset + 4);
    *flip_out = (flags & 1u) != 0;
    uint32_t image = read_u32_le(segment + flags_offset + 8);
    if (!range_ok(size, image, 9)) return false;
    int width = (int)read_u32_le(segment + image);
    int height = (int)read_u32_le(segment + image + 4);
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024) return false;
    size_t count = (size_t)width * (size_t)height;
    uint32_t *pixels = calloc(count, sizeof(*pixels));
    if (!pixels) return false;
    uint32_t pos = image + 9;
    if (segment[image + 8] == 2) {
        size_t write = 0;
        while (write < count) {
            if (!range_ok(size, pos, 1)) goto fail;
            uint32_t line_bytes = (uint32_t)segment[pos++];
            if (line_bytes == 0) goto fail;
            line_bytes--;
            if (!range_ok(size, pos, line_bytes)) goto fail;
            uint32_t end = pos + line_bytes;
            bool skip = true;
            while (pos < end) {
                uint8_t chunk = segment[pos++];
                if ((size_t)chunk > count - write) goto fail;
                if (skip) write += chunk;
                else {
                    if (chunk > end - pos) goto fail;
                    for (int i = 0; i < chunk; ++i) {
                        uint8_t index = segment[pos + i];
                        pixels[write + i] = index ? palette[index] : 0;
                    }
                    write += chunk;
                    pos += chunk;
                }
                skip = !skip;
            }
            if (write < count) write += ((size_t)width - write % (size_t)width) % (size_t)width;
        }
    } else {
        if (!range_ok(size, pos, count)) goto fail;
        for (size_t i = 0; i < count; ++i) {
            uint8_t index = segment[pos + i];
            pixels[i] = index ? palette[index] : 0;
        }
    }
    cell->rect = cell->bounds = (irect_t){ 0, 0, width, height };
    cell->displacement = ivec2_sub((ivec2_t){ width / 2, height / 2 }, offset);
    lump->texture = I_CreateTexture(renderer, pixels, width, height, true);
    free(pixels);
    return lump->texture != NULL;
fail:
    free(pixels);
    return false;
}

static bool decode_mobd(SDL_Renderer *renderer, const uint8_t *segment, size_t size,
                             uint32_t member, const uint32_t palette[256], spritesheet_t *out) {
    uint32_t animation_offsets[MAX_ANIMATIONS];
    int animation_count = 0;
    uint32_t first_frame = (uint32_t)size;
    uint32_t pos = member;
    while (pos < first_frame && animation_count < MAX_ANIMATIONS) {
        if (!range_ok(size, pos, 4)) return false;
        int32_t value = read_i32_le(segment + pos);
        pos += 4;
        if (value == 0 || (value < (int32_t)pos && value >= (int32_t)member)) {
            pos -= 4;
            break;
        }
        animation_offsets[animation_count++] = pos - 4;
        while (true) {
            if (!range_ok(size, pos, 4)) return false;
            value = read_i32_le(segment + pos);
            pos += 4;
            if (value == 0 || value == -1) break;
            if (value < 0 || (uint32_t)value >= size) return false;
            if ((uint32_t)value < first_frame) first_frame = (uint32_t)value;
        }
    }

    uint32_t ordered[MAX_ANIMATIONS];
    int ordered_count = 0;
    while (pos < first_frame && ordered_count < MAX_ANIMATIONS) {
        if (!range_ok(size, pos, 4)) return false;
        uint32_t value = read_u32_le(segment + pos);
        pos += 4;
        if (value == 0) continue;
        ordered[ordered_count++] = value;
        for (int i = 0; i < animation_count; ++i)
            if (animation_offsets[i] == value) animation_offsets[i] = 0;
    }
    for (int i = 0; i < animation_count && ordered_count < MAX_ANIMATIONS; ++i)
        if (animation_offsets[i] != 0) ordered[ordered_count++] = animation_offsets[i];

    uint32_t frames[MAX_FRAMES];
    int group_starts[MAX_ANIMATIONS] = {0};
    int group_lengths[MAX_ANIMATIONS] = {0};
    int frame_count = 0;
    for (int group = 0; group < ordered_count; ++group) {
        uint32_t cursor = ordered[group];
        if (!range_ok(size, cursor, 8)) goto fail;
        cursor += 4; /* timing word */
        group_starts[group] = frame_count;
        while (frame_count < MAX_FRAMES) {
            if (!range_ok(size, cursor, 4)) goto fail;
            int32_t frame = read_i32_le(segment + cursor);
            cursor += 4;
            if (frame == 0 || frame == -1) break;
            if (frame < 0) goto fail;
            frames[frame_count++] = (uint32_t)frame;
            group_lengths[group]++;
        }
    }
    if (frame_count == 0) goto fail;

    bool flips[MAX_FRAMES] = {0};
    if (!R_AllocSpriteCells(out, frame_count)) goto fail;
    out->frame_size = (isize2_t){ 1, 1 };
    for (int i = 0; i < frame_count; ++i) {
        spritecell_t *cell = &out->cells[i];
        if (!decode_mobd_image(renderer, segment, size, frames[i], palette, cell, &out->lumps[i], &flips[i])) goto fail;
        if (cell->rect.w > out->frame_size.w) out->frame_size.w = cell->rect.w;
        if (cell->rect.h > out->frame_size.h) out->frame_size.h = cell->rect.h;
    }

    int sequence_blocks = ordered_count / 16;
    if (sequence_blocks > 3) sequence_blocks = 3;
    int logical_frames = 0;
    for (int block = 0; block < sequence_blocks; ++block) {
        int length = group_lengths[block * 16];
        if (length > 0) logical_frames += length;
    }
    if (logical_frames <= 0 || !R_InitSpriteDef(out, logical_frames, 16))
        goto fail;
    int logical_frame = 0;
    for (int block = 0; block < sequence_blocks; ++block) {
        int length = group_lengths[block * 16];
        if (length <= 0) continue;
        for (int frame = 0; frame < length; ++frame, ++logical_frame)
            for (int rotation = 0; rotation < 16; ++rotation) {
                int lump = group_starts[block * 16 + rotation] + frame;
                R_InstallSpriteLump(out, logical_frame, (16 - rotation) % 16,
                    lump, flips[lump]);
            }
    }
    return true;

fail:
    R_FreeSprite(out);
    return false;
}

static bool load_sprite(SDL_Renderer *renderer, const char *data_root,
                             const char *spec, const uint32_t palette[256], spritesheet_t *out) {
    char archive_rel[768];
    const char *member_name = NULL;
    const char *bar = spec ? strrchr(spec, '|') : NULL;
    if (bar) {
        size_t len = (size_t)(bar - spec);
        if (len >= sizeof(archive_rel)) return false;
        memcpy(archive_rel, spec, len);
        archive_rel[len] = '\0';
        member_name = bar + 1;
    } else {
        snprintf(archive_rel, sizeof(archive_rel), "LEVELS/640/SPRITES.LVL");
        member_name = spec;
    }
    int member_index = sprite_member_index(member_name);
    if (member_index < 0) {
        fprintf(stderr, "unknown KKnD MOBD member '%s' (use a numeric member such as 34.mobd)\n",
                member_name ? member_name : "");
        return false;
    }
    char path[1024];
    M_PathJoin(path, sizeof(path), data_root, archive_rel);
    blob_t blob = {0};
    const uint8_t *segment = NULL;
    size_t segment_size = 0;
    if (!open_lvl(path, &blob, &segment, &segment_size)) return false;
    uint32_t member = 0;
    bool ok = lvl_asset(segment, segment_size, "MOBD", member_index, &member) &&
              decode_mobd(renderer, segment, segment_size, member, palette, out);
    if (!ok) fprintf(stderr, "failed to decode MOBD member %d from %s\n", member_index, path);
    W_FreeFile(&blob);
    return ok;
}

bool load_assets(SDL_Renderer *renderer, const char *data_root,
                      const level_t *map, const char *sprite_name,
                      tileset_t *tileset, spritesheet_t *unit_sprite) {
    const KkndMapData *native = map ? map->native_data : NULL;
    if (!native || !build_map_tileset(renderer, native, tileset)) return false;
    if (!load_sprite(renderer, data_root, sprite_name, native->palette, unit_sprite)) {
        R_FreeTileset(tileset);
        return false;
    }
    return true;
}
