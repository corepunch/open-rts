#define _DEFAULT_SOURCE
#include "kknd.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { KKND_MAX_LAYERS = 3, KKND_MAX_ANIMATIONS = 512, KKND_MAX_FRAMES = 4096 };

typedef struct {
    int width;
    int height;
    int layer_count;
    uint32_t palette[256];
    uint32_t *layer_pixels[KKND_MAX_LAYERS];
} KkndMapData;

typedef struct {
    uint8_t *pixels;
    int width;
    int height;
    int offset_x;
    int offset_y;
} KkndFrame;

static uint32_t read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static bool range_ok(size_t size, uint32_t offset, size_t length) {
    return (size_t)offset <= size && length <= size - (size_t)offset;
}

static void kknd_map_data_destroy(void *opaque) {
    KkndMapData *data = opaque;
    if (!data) return;
    for (int i = 0; i < KKND_MAX_LAYERS; ++i) free(data->layer_pixels[i]);
    free(data);
}

static bool kknd_open_lvl(const char *path, blob_t *blob, const uint8_t **segment,
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

static bool kknd_lvl_asset(const uint8_t *segment, size_t size, const char type[4],
                           int index, uint32_t *asset_offset) {
    uint32_t start = 0, end = 0;
    if (index < 0 || !kknd_lvl_file_list(segment, size, type, &start, &end)) return false;
    size_t slot = (size_t)start + (size_t)index * 4;
    if (!range_ok(size, (uint32_t)slot, 4) || slot >= end) return false;
    uint32_t offset = read_u32_le(segment + slot);
    if (offset == 0 || offset >= size) return false;
    *asset_offset = offset;
    return true;
}

static bool kknd_decode_mapd(const uint8_t *segment, size_t size, uint32_t mapd_offset,
                             KkndMapData *out) {
    if (!range_ok(size, mapd_offset, 12)) return false;
    uint32_t layer_count = read_u32_le(segment + mapd_offset);
    if (layer_count == 0 || layer_count > KKND_MAX_LAYERS ||
        !range_ok(size, mapd_offset + 4, (size_t)layer_count * 4 + 4)) return false;
    const uint8_t *header = segment + mapd_offset;
    uint32_t layer_offsets[KKND_MAX_LAYERS] = {0};
    for (uint32_t i = 0; i < layer_count; ++i) layer_offsets[i] = read_u32_le(header + 4 + i * 4);
    uint32_t palette_count = read_u32_le(header + 4 + layer_count * 4);
    uint32_t palette_offset = mapd_offset + 8 + layer_count * 4;
    if (palette_count == 0 || palette_count > 256 ||
        !range_ok(size, palette_offset, (size_t)palette_count * 4)) return false;
    for (uint32_t i = 0; i < palette_count; ++i) {
        const uint8_t *color = segment + palette_offset + i * 4;
        out->palette[i] = 0xff000000u | ((uint32_t)color[0] << 16) |
                          ((uint32_t)color[1] << 8) | color[2];
    }
    out->palette[0] = 0xff000000u;

    for (uint32_t layer_index = 0; layer_index < layer_count; ++layer_index) {
        uint32_t offset = layer_offsets[layer_index];
        if (!range_ok(size, offset, 20) || memcmp(segment + offset, "LRCS", 4) != 0) return false;
        int tile_w = (int)read_u32_le(segment + offset + 4);
        int tile_h = (int)read_u32_le(segment + offset + 8);
        int tiles_x = (int)read_u32_le(segment + offset + 12);
        int tiles_y = (int)read_u32_le(segment + offset + 16);
        if (tile_w != 32 || tile_h != 32 || tiles_x <= 0 || tiles_y <= 0 ||
            tiles_x > 512 || tiles_y > 512) return false;
        size_t cells = (size_t)tiles_x * (size_t)tiles_y;
        if (!range_ok(size, offset + 20, cells * 4)) return false;
        if (layer_index == 0) {
            out->width = tiles_x;
            out->height = tiles_y;
        } else if (out->width != tiles_x || out->height != tiles_y) {
            fprintf(stderr, "KKnD MAPD layers have mismatched dimensions\n");
            return false;
        }
        uint32_t *pixels = calloc(cells * 32u * 32u, sizeof(uint32_t));
        if (!pixels) return false;
        out->layer_pixels[layer_index] = pixels;
        for (size_t cell = 0; cell < cells; ++cell) {
            uint32_t tile = read_u32_le(segment + offset + 20 + cell * 4);
            if (tile == 0) continue;
            if (!range_ok(size, tile, 4 + 32u * 32u)) return false;
            const uint8_t *indices = segment + tile + 4; /* Gen1 tile prefix. */
            for (size_t pixel = 0; pixel < 32u * 32u; ++pixel) {
                uint8_t index = indices[pixel];
                if (layer_index > 0 && index == 0) pixels[cell * 1024u + pixel] = 0;
                else pixels[cell * 1024u + pixel] = out->palette[index];
            }
        }
    }
    out->layer_count = (int)layer_count;
    return true;
}

bool load_kknd_map(const char *map_path, level_t *out) {
    memset(out, 0, sizeof(*out));
    blob_t blob = {0};
    const uint8_t *segment = NULL;
    size_t segment_size = 0;
    if (!kknd_open_lvl(map_path, &blob, &segment, &segment_size)) return false;
    uint32_t mapd = 0;
    if (!kknd_lvl_asset(segment, segment_size, "MAPD", 0, &mapd)) {
        fprintf(stderr, "%s has no MAPD asset\n", map_path);
        W_FreeFile(&blob);
        return false;
    }
    KkndMapData *native = calloc(1, sizeof(*native));
    if (!native || !kknd_decode_mapd(segment, segment_size, mapd, native)) {
        fprintf(stderr, "%s has an invalid or unsupported KKnD MAPD asset\n", map_path);
        kknd_map_data_destroy(native);
        W_FreeFile(&blob);
        return false;
    }
    W_FreeFile(&blob);

    size_t cells = (size_t)native->width * (size_t)native->height;
    if (cells == 0 || cells * (size_t)native->layer_count + 1u > UINT16_MAX) {
        fprintf(stderr, "%s is too large for the current 16-bit terrain index contract\n", map_path);
        kknd_map_data_destroy(native);
        return false;
    }
    out->tile_ids = calloc(cells, sizeof(uint16_t));
    out->blocked = calloc(cells, 1);
    if (!out->tile_ids || !out->blocked) {
        kknd_map_data_destroy(native);
        free(out->tile_ids);
        free(out->blocked);
        memset(out, 0, sizeof(*out));
        return false;
    }
    out->width = native->width;
    out->height = native->height;
    for (size_t i = 0; i < cells; ++i) out->tile_ids[i] = (uint16_t)i;
    for (int layer = 1; layer < native->layer_count && layer - 1 < MAX_TILE_OVERLAYS; ++layer) {
        out->tile_overlays[layer - 1] = calloc(cells, sizeof(uint16_t));
        if (!out->tile_overlays[layer - 1]) {
            P_FreeLevel(out);
            kknd_map_data_destroy(native);
            return false;
        }
        for (size_t cell = 0; cell < cells; ++cell) {
            const uint32_t *tile = native->layer_pixels[layer] + cell * 1024u;
            bool visible = false;
            for (size_t p = 0; p < 1024u; ++p) if (tile[p] != 0) { visible = true; break; }
            if (visible) out->tile_overlays[layer - 1][cell] = (uint16_t)(cells * layer + 1 + cell);
        }
        out->tile_overlay_count++;
    }
    /* KKnD's second MAPD layer contains bridges, cliff faces, and other art
       that units can pass behind, so compose it in the world-object pass. */
    out->render_features = MAP_RENDER_SKIP_ZERO_TILES | MAP_RENDER_INTERLEAVED_OVERLAYS;
    /* MOBD facing 0 = north, indices increase clockwise — same convention as
       Dark Reign.  Without this the default compass16 scheme returns even
       codes 0/2/4…14 and misses the odd-numbered MOBD facings 1/3/5…15. */
    out->direction_mode = RTS_DIRECTION_DARK_REIGN_8;
    out->native_data = native;
    out->destroy_native_data = kknd_map_data_destroy;
    out->has_camera = true;
    out->camera_gx = (float)out->width * 0.5f;
    out->camera_gy = (float)out->height * 0.5f;
    snprintf(out->tileset_name, sizeof(out->tileset_name), "SURV_01 MAPD");
    return true;
}

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
    if (!range_ok(size, frame_offset, 28)) return false;
    out->offset_x = (int)read_u32_le(segment + frame_offset);
    out->offset_y = (int)read_u32_le(segment + frame_offset + 4);
    uint32_t flags_offset = read_u32_le(segment + frame_offset + 12);
    if (!range_ok(size, flags_offset, 12) || memcmp(segment + flags_offset, "TRPS", 4) != 0)
        return false;
    uint32_t flags = read_u32_le(segment + flags_offset + 4);
    uint32_t image = read_u32_le(segment + flags_offset + 8);
    if (!range_ok(size, image, 9)) return false;
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
            if (!range_ok(size, pos, 1)) { free(pixels); return false; }
            uint32_t line_bytes = (uint32_t)segment[pos++];
            if (line_bytes == 0) { free(pixels); return false; }
            line_bytes--;
            if (!range_ok(size, pos, line_bytes)) { free(pixels); return false; }
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
        if (!range_ok(size, pos, count)) { free(pixels); return false; }
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

    uint32_t ordered[KKND_MAX_ANIMATIONS];
    int ordered_count = 0;
    while (pos < first_frame && ordered_count < KKND_MAX_ANIMATIONS) {
        if (!range_ok(size, pos, 4)) return false;
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
        if (!range_ok(size, cursor, 8)) goto fail;
        cursor += 4; /* timing word */
        group_starts[group] = frame_count;
        while (frame_count < KKND_MAX_FRAMES) {
            if (!range_ok(size, cursor, 4)) goto fail;
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
    SDL_Rect *rects = calloc((size_t)frame_count, sizeof(SDL_Rect));
    SDL_Rect *bounds = calloc((size_t)frame_count, sizeof(SDL_Rect));
    SDL_Point *displacements = calloc((size_t)frame_count, sizeof(SDL_Point));
    if (!rgba || !rects || !bounds || !displacements) {
        free(rgba); free(rects); free(bounds); free(displacements); goto fail;
    }
    for (int i = 0; i < frame_count; ++i) {
        int dx = (i % cols) * max_w;
        int dy = (i / cols) * max_h;
        rects[i] = (SDL_Rect){ dx, dy, frames[i].width, frames[i].height };
        bounds[i] = (SDL_Rect){ 0, 0, frames[i].width, frames[i].height };
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
            sequence->direction_codes[facing] = facing;
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
