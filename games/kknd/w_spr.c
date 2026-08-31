#define _DEFAULT_SOURCE
#include "kknd.h"
#include "w_lvl.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef struct {
    uint8_t *pixels;
    int width;
    int height;
    int offset_x;
    int offset_y;
} KkndFrame;

enum { KKND_MAX_ANIMATIONS = 512, KKND_MAX_FRAMES = 4096 };
static bool kknd_build_map_tileset(SDL_Renderer *renderer, const KkndMapData *map,
                                   tileset_t *out) {
    size_t cells = (size_t)map->width * (size_t)map->height;
    int frame_count = (int)(cells * (size_t)map->layer_count + 1);
    int cols = 64;
    int rows = (frame_count + cols - 1) / cols;
    int atlas_w = cols * 32;
    int atlas_h = rows * 32;
    uint32_t *atlas = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    if (!atlas) return false;
    for (int layer = 0; layer < map->layer_count; ++layer) {
        for (size_t cell = 0; cell < cells; ++cell) {
            int frame = layer == 0 ? (int)cell : (int)(cells * (size_t)layer + 1 + cell);
            int dx = (frame % cols) * 32;
            int dy = (frame / cols) * 32;
            const uint32_t *src = map->layer_pixels[layer] + cell * 1024u;
            for (int y = 0; y < 32; ++y)
                memcpy(atlas + (size_t)(dy + y) * (size_t)atlas_w + (size_t)dx,
                       src + (size_t)y * 32u, 32u * sizeof(uint32_t));
        }
    }
    out->texture = I_CreateTexture(renderer, atlas, atlas_w, atlas_h, true);
    free(atlas);
    if (!out->texture) return false;
    out->count = frame_count;
    out->atlas_cols = cols;
    out->tile_w = 32;
    out->tile_h = 32;
    return true;
}

static int kknd_sprite_member_index(const char *name) {
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

static bool kknd_decode_mobd_image(const uint8_t *segment, size_t size, uint32_t frame_offset,
                                   KkndFrame *out) {
    if (!kknd_range_ok(size, frame_offset, 28)) return false;
    out->offset_x = (int)read_u32_le(segment + frame_offset);
    out->offset_y = (int)read_u32_le(segment + frame_offset + 4);
    uint32_t flags_offset = read_u32_le(segment + frame_offset + 12);
    if (!kknd_range_ok(size, flags_offset, 12) || memcmp(segment + flags_offset, "TRPS", 4) != 0)
        return false;
    uint32_t flags = read_u32_le(segment + flags_offset + 4);
    uint32_t image = read_u32_le(segment + flags_offset + 8);
    if (!kknd_range_ok(size, image, 9)) return false;
    int width = (int)read_u32_le(segment + image);
    int height = (int)read_u32_le(segment + image + 4);
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024) return false;
    size_t count = (size_t)width * (size_t)height;
    uint8_t *pixels = calloc(count, 1);
    if (!pixels) return false;
    uint32_t pos = image + 9;
    if (segment[image + 8] == 2) {
        size_t write = 0;
        while (write < count) {
            if (!kknd_range_ok(size, pos, 1)) { free(pixels); return false; }
            uint32_t line_bytes = (uint32_t)segment[pos++];
            if (line_bytes == 0) { free(pixels); return false; }
            line_bytes--;
            if (!kknd_range_ok(size, pos, line_bytes)) { free(pixels); return false; }
            uint32_t end = pos + line_bytes;
            bool skip = true;
            while (pos < end) {
                uint8_t chunk = segment[pos++];
                if (skip) write += chunk;
                else {
                    if ((size_t)chunk > count - (write < count ? write : count) || pos + chunk > end) {
                        free(pixels); return false;
                    }
                    memcpy(pixels + write, segment + pos, chunk);
                    write += chunk;
                    pos += chunk;
                }
                skip = !skip;
            }
            if (write < count) write += ((size_t)width - write % (size_t)width) % (size_t)width;
        }
    } else {
        if (!kknd_range_ok(size, pos, count)) { free(pixels); return false; }
        memcpy(pixels, segment + pos, count);
    }
    if ((flags & 1u) != 0) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width / 2; ++x) {
                uint8_t tmp = pixels[(size_t)y * width + x];
                pixels[(size_t)y * width + x] = pixels[(size_t)y * width + width - 1 - x];
                pixels[(size_t)y * width + width - 1 - x] = tmp;
            }
    }
    out->pixels = pixels;
    out->width = width;
    out->height = height;
    return true;
}

static bool kknd_decode_mobd(SDL_Renderer *renderer, const uint8_t *segment, size_t size,
                             uint32_t member, const uint32_t palette[256], spritesheet_t *out) {
    uint32_t animation_offsets[KKND_MAX_ANIMATIONS];
    int animation_count = 0;
    uint32_t first_frame = (uint32_t)size;
    uint32_t pos = member;
    while (pos < first_frame && animation_count < KKND_MAX_ANIMATIONS) {
        if (!kknd_range_ok(size, pos, 4)) return false;
        int32_t value = read_i32_le(segment + pos);
        pos += 4;
        if (value == 0 || (value < (int32_t)pos && value >= (int32_t)member)) {
            pos -= 4;
            break;
        }
        animation_offsets[animation_count++] = pos - 4;
        while (true) {
            if (!kknd_range_ok(size, pos, 4)) return false;
            value = read_i32_le(segment + pos);
            pos += 4;
            if (value == 0 || value == -1) break;
            if (value < 0 || (uint32_t)value >= size) return false;
            if ((uint32_t)value < first_frame) first_frame = (uint32_t)value;
        }
    }

    uint32_t ordered[KKND_MAX_ANIMATIONS];
    int ordered_count = 0;
    while (pos < first_frame && ordered_count < KKND_MAX_ANIMATIONS) {
        if (!kknd_range_ok(size, pos, 4)) return false;
        uint32_t value = read_u32_le(segment + pos);
        pos += 4;
        if (value == 0) continue;
        ordered[ordered_count++] = value;
        for (int i = 0; i < animation_count; ++i)
            if (animation_offsets[i] == value) animation_offsets[i] = 0;
    }
    for (int i = 0; i < animation_count && ordered_count < KKND_MAX_ANIMATIONS; ++i)
        if (animation_offsets[i] != 0) ordered[ordered_count++] = animation_offsets[i];

    KkndFrame frames[KKND_MAX_FRAMES] = {{0}};
    int group_starts[KKND_MAX_ANIMATIONS] = {0};
    int group_lengths[KKND_MAX_ANIMATIONS] = {0};
    int frame_count = 0;
    for (int group = 0; group < ordered_count; ++group) {
        uint32_t cursor = ordered[group];
        if (!kknd_range_ok(size, cursor, 8)) goto fail;
        cursor += 4; /* timing word */
        group_starts[group] = frame_count;
        while (frame_count < KKND_MAX_FRAMES) {
            if (!kknd_range_ok(size, cursor, 4)) goto fail;
            int32_t frame = read_i32_le(segment + cursor);
            cursor += 4;
            if (frame == 0 || frame == -1) break;
            if (frame < 0 || !kknd_decode_mobd_image(segment, size, (uint32_t)frame,
                                                     &frames[frame_count])) goto fail;
            frame_count++;
            group_lengths[group]++;
        }
    }
    if (frame_count == 0) goto fail;

    int max_w = 1, max_h = 1;
    for (int i = 0; i < frame_count; ++i) {
        if (frames[i].width > max_w) max_w = frames[i].width;
        if (frames[i].height > max_h) max_h = frames[i].height;
    }
    int cols = 16;
    int rows = (frame_count + cols - 1) / cols;
    int atlas_w = cols * max_w;
    int atlas_h = rows * max_h;
    uint32_t *rgba = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    irect_t *rects = calloc((size_t)frame_count, sizeof(irect_t));
    irect_t *bounds = calloc((size_t)frame_count, sizeof(irect_t));
    SDL_Point *displacements = calloc((size_t)frame_count, sizeof(SDL_Point));
    if (!rgba || !rects || !bounds || !displacements) {
        free(rgba); free(rects); free(bounds); free(displacements); goto fail;
    }
    for (int i = 0; i < frame_count; ++i) {
        int dx = (i % cols) * max_w;
        int dy = (i / cols) * max_h;
        rects[i] = (irect_t){ dx, dy, frames[i].width, frames[i].height };
        bounds[i] = (irect_t){ 0, 0, frames[i].width, frames[i].height };
        displacements[i] = (SDL_Point){ frames[i].width / 2 - frames[i].offset_x,
                                        frames[i].height / 2 - frames[i].offset_y };
        for (int y = 0; y < frames[i].height; ++y)
            for (int x = 0; x < frames[i].width; ++x) {
                uint8_t index = frames[i].pixels[(size_t)y * frames[i].width + x];
                rgba[(size_t)(dy + y) * atlas_w + dx + x] = index == 0 ? 0 : palette[index];
            }
    }
    out->texture = I_CreateTexture(renderer, rgba, atlas_w, atlas_h, true);
    free(rgba);
    if (!out->texture) { free(rects); free(bounds); free(displacements); goto fail; }
    out->frames = rects;
    out->frame_bounds = bounds;
    out->frame_displacements = displacements;
    out->frame_count = frame_count;
    out->frame_w = max_w;
    out->frame_h = max_h;
    out->rotations = ordered_count >= 16 ? 16 : 1;
    out->primary_frames_per_rotation = 1;

    static const char *sequence_names[] = { "stand", "shoot", "run" };
    int sequence_blocks = ordered_count / 16;
    if (sequence_blocks > 3) sequence_blocks = 3;
    for (int block = 0; block < sequence_blocks; ++block) {
        int length = group_lengths[block * 16];
        if (length <= 0) continue;
        spritesequence_t *sequence = &out->sequences[out->sequence_count++];
        snprintf(sequence->name, sizeof(sequence->name), "%s", sequence_names[block]);
        sequence->facings = 16;
        sequence->length = length;
        sequence->frame_stride = 1;
        sequence->tick_ms = 120;
        for (int facing = 0; facing < 16; ++facing) {
            sequence->frame_starts[facing] = group_starts[block * 16 + facing];
            sequence->rotation_angles[facing] =
                direction_to_angle(facing, 16, ANG90, true);
            if (group_lengths[block * 16 + facing] < sequence->length)
                sequence->length = group_lengths[block * 16 + facing];
        }
    }
    for (int i = 0; i < frame_count; ++i) free(frames[i].pixels);
    return true;

fail:
    for (int i = 0; i < KKND_MAX_FRAMES; ++i) free(frames[i].pixels);
    return false;
}

static bool kknd_load_sprite(SDL_Renderer *renderer, const char *data_root,
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
    int member_index = kknd_sprite_member_index(member_name);
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
    if (!kknd_open_lvl(path, &blob, &segment, &segment_size)) return false;
    uint32_t member = 0;
    bool ok = kknd_lvl_asset(segment, segment_size, "MOBD", member_index, &member) &&
              kknd_decode_mobd(renderer, segment, segment_size, member, palette, out);
    if (!ok) fprintf(stderr, "failed to decode MOBD member %d from %s\n", member_index, path);
    W_FreeFile(&blob);
    return ok;
}

bool kknd_load_assets(SDL_Renderer *renderer, const char *data_root,
                      const level_t *map, const char *sprite_name,
                      tileset_t *tileset, spritesheet_t *unit_sprite) {
    const KkndMapData *native = map ? map->native_data : NULL;
    if (!native || !kknd_build_map_tileset(renderer, native, tileset)) return false;
    if (!kknd_load_sprite(renderer, data_root, sprite_name, native->palette, unit_sprite)) {
        R_FreeTileset(tileset);
        return false;
    }
    return true;
}
