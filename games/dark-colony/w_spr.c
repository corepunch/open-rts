#define _DEFAULT_SOURCE
#include "engine.h"
#include "info.h"
#include "gamestat.h"
#include "dc_types.h"
#include "w_spr.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void dark_colony_palette_from_spr(const uint8_t *spr, size_t size, uint32_t colors[256]) {
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

static uint8_t dark_colony_remap_palette_index(uint8_t index, int remap) {
    if (remap < 0 || remap > 7 || index < 138 || index > 143) return index;
    int remapped = (int)index + (remap - 7) * 6;
    if (remapped < 0 || remapped > 255) return index;
    return (uint8_t)remapped;
}

static uint32_t dark_colony_sprite_pixel_rgba(uint8_t index, const uint32_t palette[256],
                                             int remap) {
    if (index == 0) return 0x00000000u;
    index = dark_colony_remap_palette_index(index, remap);
    return palette[index];
}

static irect_t dc_visible_bounds(const uint32_t *rgba, int atlas_w, irect_t frame) {
    int min_x = frame.w, min_y = frame.h, max_x = -1, max_y = -1;
    for (int y = 0; y < frame.h; ++y) {
        for (int x = 0; x < frame.w; ++x) {
            uint32_t px = rgba[(frame.y + y) * atlas_w + frame.x + x];
            if ((px >> 24) == 0) continue;
            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x > max_x) max_x = x;
            if (y > max_y) max_y = y;
        }
    }
    if (max_x < min_x || max_y < min_y) return (irect_t){ 0, 0, frame.w, frame.h };
    return (irect_t){ min_x, min_y, max_x - min_x + 1, max_y - min_y + 1 };
}

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t dis_x;
    uint16_t dis_y;
    uint8_t used;
    uint32_t data_size;
    uint8_t *data;
} DarkColonyJuiceCell;

typedef struct {
    uint16_t flags;
    uint16_t cell_count;
    uint32_t payload_bytes;
    bool chunked;
    uint32_t palette[256];
    DarkColonyJuiceCell *cells;
} DarkColonyJuiceFile;

typedef struct {
    char name[9];
} DarkColonyAnimationDependency;

typedef struct {
    char name[17];
    uint16_t start;
    uint16_t end;
} DarkColonyAnimationLabel;

typedef struct {
    char sprite[9];
    int16_t frame;
    int16_t x;
    int16_t y;
    int16_t remap;
    int16_t intensity;
    int16_t layer;
    int16_t flags;
} DarkColonyAnimationCommand;

typedef struct {
    uint16_t frame_count;
    uint16_t aux_count;
    uint16_t label_count;
    uint16_t dependency_count;
    DarkColonyAnimationDependency *dependencies;
    DarkColonyAnimationLabel *labels;
    uint8_t *aux_records;
    DarkColonyAnimationCommand *commands;
    int command_count;
} DarkColonyAnimationFile;

typedef struct {
    DarkColonyJuiceFile juice;
    DarkColonyAnimationFile animation;
    bool has_animation;
} DarkColonySpriteNative;

static int16_t read_i16_le_dc(const uint8_t *p) {
    return (int16_t)read_u16_le(p);
}

static void copy_padded_ascii(char *dst, size_t dst_size, const uint8_t *src, size_t src_size) {
    size_t n = 0;
    while (n + 1 < dst_size && n < src_size && src[n] != '\0') {
        dst[n] = (char)src[n];
        n++;
    }
    dst[n] = '\0';
}

static void dark_colony_juice_destroy(DarkColonyJuiceFile *juice) {
    if (!juice) return;
    for (int i = 0; i < juice->cell_count; ++i) free(juice->cells[i].data);
    free(juice->cells);
    memset(juice, 0, sizeof(*juice));
}

static bool dark_colony_juice_load(const char *path, DarkColonyJuiceFile *out) {
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

    dark_colony_palette_from_spr(blob.bytes, blob.size, out->palette);
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
        DarkColonyJuiceCell *cell = &out->cells[i];
        cell->width = read_u16_le(desc + 0);
        cell->height = read_u16_le(desc + 2);
        cell->dis_x = read_u16_le(desc + 4);
        cell->dis_y = read_u16_le(desc + 6);
        cell->used = cell->width > 0 && cell->height > 0;
        if (cell->width > 512 || cell->height > 512) {
            dark_colony_juice_destroy(out);
            W_FreeFile(&blob);
            return false;
        }
    }

    size_t src_pos = data_off;
    if (out->chunked) {
        uint32_t total_chunks = 0;
        for (int i = 0; i < out->cell_count; ++i) {
            if (src_pos + 4 > blob.size) {
                dark_colony_juice_destroy(out);
                W_FreeFile(&blob);
                return false;
            }
            uint32_t chunk_len = read_u32_le(blob.bytes + src_pos);
            src_pos += 4;
            total_chunks += chunk_len;
            if (total_chunks > out->payload_bytes || src_pos + chunk_len > blob.size) {
                dark_colony_juice_destroy(out);
                W_FreeFile(&blob);
                return false;
            }
            DarkColonyJuiceCell *cell = &out->cells[i];
            cell->data_size = chunk_len;
            cell->data = malloc(chunk_len > 0 ? chunk_len : 1);
            if (!cell->data) {
                dark_colony_juice_destroy(out);
                W_FreeFile(&blob);
                return false;
            }
            if (chunk_len > 0) memcpy(cell->data, blob.bytes + src_pos, chunk_len);
            src_pos += chunk_len;
        }
    } else {
        for (int i = 0; i < out->cell_count; ++i) {
            DarkColonyJuiceCell *cell = &out->cells[i];
            uint32_t pixel_count = (uint32_t)cell->width * (uint32_t)cell->height;
            cell->data_size = pixel_count;
            if (pixel_count == 0) continue;
            if (src_pos + pixel_count > blob.size) {
                dark_colony_juice_destroy(out);
                W_FreeFile(&blob);
                return false;
            }
            cell->data = malloc(pixel_count);
            if (!cell->data) {
                dark_colony_juice_destroy(out);
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

static void dark_colony_animation_destroy(DarkColonyAnimationFile *animation) {
    if (!animation) return;
    free(animation->dependencies);
    free(animation->labels);
    free(animation->aux_records);
    free(animation->commands);
    memset(animation, 0, sizeof(*animation));
}

static bool dark_colony_animation_load(const char *path, DarkColonyAnimationFile *out) {
    memset(out, 0, sizeof(*out));
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return false;
    if (blob.size < 8) { W_FreeFile(&blob); return false; }

    out->frame_count = read_u16_le(blob.bytes + 0);
    out->aux_count = read_u16_le(blob.bytes + 2);
    out->label_count = read_u16_le(blob.bytes + 4);
    out->dependency_count = read_u16_le(blob.bytes + 6);
    if (out->label_count > 1024 || out->dependency_count > 1024 || out->aux_count > 4096) {
        W_FreeFile(&blob);
        return false;
    }

    size_t dependency_off = 8;
    size_t label_off = dependency_off + (size_t)out->dependency_count * 8;
    size_t aux_off = label_off + (size_t)out->label_count * 20;
    size_t command_off = aux_off + (size_t)out->aux_count * 164;
    if (command_off > blob.size || ((blob.size - command_off) % 22) != 0) {
        W_FreeFile(&blob);
        return false;
    }

    out->dependencies = calloc(out->dependency_count ? out->dependency_count : 1,
                               sizeof(*out->dependencies));
    out->labels = calloc(out->label_count ? out->label_count : 1, sizeof(*out->labels));
    out->aux_records = malloc((size_t)out->aux_count * 164 > 0 ?
                              (size_t)out->aux_count * 164 : 1);
    out->command_count = (int)((blob.size - command_off) / 22);
    out->commands = calloc(out->command_count ? out->command_count : 1, sizeof(*out->commands));
    if (!out->dependencies || !out->labels || !out->aux_records || !out->commands) {
        dark_colony_animation_destroy(out);
        W_FreeFile(&blob);
        return false;
    }

    for (int i = 0; i < out->dependency_count; ++i) {
        copy_padded_ascii(out->dependencies[i].name, sizeof(out->dependencies[i].name),
                          blob.bytes + dependency_off + (size_t)i * 8, 8);
    }
    for (int i = 0; i < out->label_count; ++i) {
        size_t off = label_off + (size_t)i * 20;
        copy_padded_ascii(out->labels[i].name, sizeof(out->labels[i].name),
                          blob.bytes + off, 16);
        out->labels[i].start = read_u16_le(blob.bytes + off + 16);
        out->labels[i].end = read_u16_le(blob.bytes + off + 18);
    }
    if (out->aux_count > 0) {
        memcpy(out->aux_records, blob.bytes + aux_off, (size_t)out->aux_count * 164);
    }
    for (int i = 0; i < out->command_count; ++i) {
        size_t off = command_off + (size_t)i * 22;
        DarkColonyAnimationCommand *cmd = &out->commands[i];
        copy_padded_ascii(cmd->sprite, sizeof(cmd->sprite), blob.bytes + off, 8);
        cmd->frame = read_i16_le_dc(blob.bytes + off + 8);
        cmd->x = read_i16_le_dc(blob.bytes + off + 10);
        cmd->y = read_i16_le_dc(blob.bytes + off + 12);
        cmd->remap = read_i16_le_dc(blob.bytes + off + 14);
        cmd->intensity = read_i16_le_dc(blob.bytes + off + 16);
        cmd->layer = read_i16_le_dc(blob.bytes + off + 18);
        cmd->flags = read_i16_le_dc(blob.bytes + off + 20);
    }
    W_FreeFile(&blob);
    return true;
}

static const DarkColonyAnimationCommand *dark_colony_animation_find_command(
    const DarkColonyAnimationFile *animation, const char *label_name,
    const char *sprite_name, int frame, int layer) {
    if (!animation || !label_name || !sprite_name) return NULL;

    int command_index = 0;
    for (int frame_index = 0; frame_index < animation->aux_count; ++frame_index) {
        int part_count = read_u16_le(animation->aux_records + (size_t)frame_index * 164);
        for (int label_index = 0; label_index < animation->label_count; ++label_index) {
            const DarkColonyAnimationLabel *label = &animation->labels[label_index];
            if (strcmp(label->name, label_name) != 0 ||
                frame_index < label->start || frame_index > label->end) {
                continue;
            }
            for (int part = 0; part < part_count; ++part) {
                if (command_index + part >= animation->command_count) return NULL;
                const DarkColonyAnimationCommand *command =
                    &animation->commands[command_index + part];
                if (strcasecmp(command->sprite, sprite_name) == 0 &&
                    command->frame == frame && command->layer == layer) {
                    return command;
                }
            }
        }
        command_index += part_count;
        if (command_index > animation->command_count) return NULL;
    }
    return NULL;
}

static const DarkColonyAnimationCommand *dark_colony_animation_frame_command(
    const DarkColonyAnimationFile *animation, int frame_index,
    const char *sprite_name, int layer) {
    if (!animation || !sprite_name || frame_index < 0 || frame_index >= animation->aux_count)
        return NULL;

    int command_index = 0;
    for (int i = 0; i < frame_index; ++i)
        command_index += read_u16_le(animation->aux_records + (size_t)i * 164);
    int part_count = read_u16_le(animation->aux_records + (size_t)frame_index * 164);
    for (int part = 0; part < part_count; ++part) {
        if (command_index + part >= animation->command_count) return NULL;
        const DarkColonyAnimationCommand *command = &animation->commands[command_index + part];
        if (strcasecmp(command->sprite, sprite_name) == 0 && command->layer == layer)
            return command;
    }
    return NULL;
}

static void dark_colony_sprite_native_destroy(void *ptr) {
    DarkColonySpriteNative *native = ptr;
    if (!native) return;
    dark_colony_juice_destroy(&native->juice);
    dark_colony_animation_destroy(&native->animation);
    free(native);
}

static bool dark_colony_animation_path_for_sprite(char *out, size_t out_size,
                                                  const char *sprite_path) {
    if (!out || out_size == 0 || !sprite_path) return false;
    const char *base = strrchr(sprite_path, '/');
    base = base ? base + 1 : sprite_path;
    const char *dot = strrchr(base, '.');
    if (!dot || strcasecmp(dot, ".SPR") != 0) return false;

    const char *dir_end = base > sprite_path ? base - 1 : NULL;
    const char *dir_start = dir_end;
    while (dir_start && dir_start > sprite_path && dir_start[-1] != '/') dir_start--;
    size_t dir_len = dir_start ? (size_t)(dir_end - dir_start) : 0;
    if (!dir_start || dir_len != strlen("SPRITES") ||
        strncasecmp(dir_start, "SPRITES", dir_len) != 0) {
        return false;
    }

    size_t prefix_len = (size_t)(dir_start - sprite_path);
    size_t stem_len = (size_t)(dot - base);
    if (prefix_len + strlen("ANIMATE/") + stem_len + strlen(".FIN") + 1 > out_size)
        return false;
    memcpy(out, sprite_path, prefix_len);
    out[prefix_len] = '\0';
    strncat(out, "ANIMATE/", out_size - strlen(out) - 1);
    strncat(out, base, stem_len);
    strncat(out, ".FIN", out_size - strlen(out) - 1);
    return true;
}

static bool dark_colony_dependency_sprite_name(char *out, size_t out_size,
                                               const char *dependency) {
    if (!out || out_size == 0 || !dependency) return false;
    char stem[16];
    size_t len = 0;
    while (dependency[len] != '\0' && len < 8 &&
           !isspace((unsigned char)dependency[len])) {
        unsigned char ch = (unsigned char)dependency[len];
        if (!isalnum(ch) && ch != '_') return false;
        stem[len++] = (char)toupper(ch);
    }
    if (len == 0) return false;
    stem[len] = '\0';
    return snprintf(out, out_size, "SPRITES/%s.SPR", stem) < (int)out_size;
}

static bool dark_colony_file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

bool load_dark_colony_sprite(SDL_Renderer *renderer, const char *path, spritesheet_t *out,
                             uint32_t palette_out[256]) {
    memset(out, 0, sizeof(*out));

    DarkColonySpriteNative *native = calloc(1, sizeof(*native));
    if (!native) return false;
    if (!dark_colony_juice_load(path, &native->juice)) {
        fprintf(stderr, "%s is not a supported Dark Colony raw SPR\n", path);
        dark_colony_sprite_native_destroy(native);
        return false;
    }
    if (palette_out) memcpy(palette_out, native->juice.palette, sizeof(native->juice.palette));
    const uint32_t *palette = native->juice.palette;

    char animation_path[1024];
    if (dark_colony_animation_path_for_sprite(animation_path, sizeof(animation_path), path) &&
        dark_colony_file_exists(animation_path) &&
        dark_colony_animation_load(animation_path, &native->animation)) {
        native->has_animation = true;
    }

    DarkColonyJuiceFile *juice = &native->juice;
    int visible_frames = juice->cell_count;
    int max_w = 1, max_h = 1;
    int canvas_w = 1, canvas_h = 1;
    for (int i = 0; i < visible_frames; ++i) {
        const DarkColonyJuiceCell *cell = &juice->cells[i];
        int w = cell->width > 0 ? cell->width : 1;
        int h = cell->height > 0 ? cell->height : 1;
        if (w > max_w) max_w = w;
        if (h > max_h) max_h = h;
        if (cell->dis_x + w > canvas_w) canvas_w = cell->dis_x + w;
        if (cell->dis_y + h > canvas_h) canvas_h = cell->dis_y + h;
    }

    int cols = (int)ceilf(sqrtf((float)visible_frames));
    int rows = (visible_frames + cols - 1) / cols;
    int atlas_w = cols * max_w;
    int atlas_h = rows * max_h;
    uint32_t *rgba = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    uint8_t *indices = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint8_t));
    irect_t *frames = calloc((size_t)visible_frames, sizeof(irect_t));
    irect_t *bounds = calloc((size_t)visible_frames, sizeof(irect_t));
    SDL_Point *displacements = calloc((size_t)visible_frames, sizeof(SDL_Point));
    if (!rgba || !indices || !frames || !bounds || !displacements) {
        free(rgba); free(indices); free(frames); free(bounds); free(displacements);
        dark_colony_sprite_native_destroy(native);
        return false;
    }

    bool has_team_colors = false;
    for (int i = 0; i < visible_frames; ++i) {
        const DarkColonyJuiceCell *cell = &juice->cells[i];
        int w = cell->width > 0 ? cell->width : 1;
        int h = cell->height > 0 ? cell->height : 1;
        bool blank = cell->width == 0 || cell->height == 0;
        int fx = (i % cols) * max_w;
        int fy = (i / cols) * max_h;
        displacements[i] = (SDL_Point){ cell->dis_x, cell->dis_y };
        if (juice->chunked) {
            uint32_t chunk_size = cell->data_size;
            if (blank) {
                frames[i] = (irect_t){ fx, fy, w, h };
                bounds[i] = (irect_t){ 0, 0, w, h };
                continue;
            }
            const uint8_t *src = cell->data;
            size_t pos = 0;
            int write = 0;
            int pixel_count = w * h;
            while (pos < chunk_size && write < pixel_count) {
                int8_t cmd = (int8_t)src[pos++];
                if (cmd < 0) {
                    write += -cmd;
                } else {
                    int count = cmd + 1;
                    if (pos + (size_t)count > chunk_size) break;
                    for (int p = 0; p < count; ++p) {
                        if (write >= 0 && write < pixel_count) {
                            int dst_x = fx + (write % w), dst_y = fy + (write / w);
                            if (dst_x >= 0 && dst_x < atlas_w && dst_y >= 0 && dst_y < atlas_h) {
                                size_t dst = (size_t)dst_y * (size_t)atlas_w + (size_t)dst_x;
                                uint8_t index = src[pos + (size_t)p];
                                if (index >= 138 && index <= 143) has_team_colors = true;
                                indices[dst] = index;
                                rgba[dst] = dark_colony_sprite_pixel_rgba(index, palette, -1);
                            }
                        }
                        write++;
                    }
                    pos += (size_t)count;
                }
            }
        } else {
            if (!blank) {
                const uint8_t *src = cell->data;
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        int dst_x = fx + x;
                        int dst_y = fy + y;
                        if (dst_x >= 0 && dst_x < atlas_w && dst_y >= 0 && dst_y < atlas_h) {
                            size_t dst = (size_t)dst_y * (size_t)atlas_w + (size_t)dst_x;
                            uint8_t index = src[y * w + x];
                            if (index >= 138 && index <= 143) has_team_colors = true;
                            indices[dst] = index;
                            rgba[dst] = dark_colony_sprite_pixel_rgba(index, palette, -1);
                        }
                    }
                }
            }
        }
        frames[i] = (irect_t){ fx, fy, w, h };
        bounds[i] = dc_visible_bounds(rgba, atlas_w, frames[i]);
    }

    SDL_Texture *texture = I_CreateTexture(renderer, rgba, atlas_w, atlas_h, true);
    if (!texture) {
        free(rgba);
        free(indices);
        free(frames);
        free(bounds);
        free(displacements);
        dark_colony_sprite_native_destroy(native);
        return false;
    }

    SDL_Texture *remap_textures[8] = {0};
    uint32_t *remap_rgba = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    if (!remap_rgba) {
        SDL_DestroyTexture(texture);
        free(rgba);
        free(indices);
        free(frames);
        free(bounds);
        free(displacements);
        dark_colony_sprite_native_destroy(native);
        return false;
    }
    for (int remap = 0; has_team_colors && remap < 8; ++remap) {
        for (int y = 0; y < atlas_h; ++y) {
            int row = max_h > 0 ? y / max_h : 0;
            for (int x = 0; x < atlas_w; ++x) {
                int col = max_w > 0 ? x / max_w : 0;
                int frame = row * cols + col;
                size_t pos = (size_t)y * (size_t)atlas_w + (size_t)x;
                remap_rgba[pos] = frame >= 0 && frame < visible_frames ?
                    dark_colony_sprite_pixel_rgba(indices[pos], palette, remap) :
                    0x00000000u;
            }
        }
        remap_textures[remap] = I_CreateTexture(renderer, remap_rgba, atlas_w, atlas_h, true);
        if (!remap_textures[remap]) {
            for (int i = 0; i < remap; ++i)
                if (remap_textures[i]) SDL_DestroyTexture(remap_textures[i]);
            SDL_DestroyTexture(texture);
            free(remap_rgba);
            free(rgba);
            free(indices);
            free(frames);
            free(bounds);
            free(displacements);
            dark_colony_sprite_native_destroy(native);
            return false;
        }
    }
    free(remap_rgba);

    out->texture = texture;
    for (int remap = 0; remap < 8; ++remap)
        out->remap_textures[remap] = remap_textures[remap];
    out->frames = frames;
    out->frame_bounds = bounds;
    out->frame_ground_points = NULL;
    out->frame_displacements = displacements;
    out->frame_count = visible_frames;
    out->frame_w = canvas_w;
    out->frame_h = canvas_h;
    out->rotations = 1;
    out->primary_frames_per_rotation = visible_frames;
    out->native_data = native;
    out->destroy_native_data = dark_colony_sprite_native_destroy;

    free(rgba);
    free(indices);
    return true;
}

static bool sprite_cache_load_dark_colony(spritecache_t *cache, SDL_Renderer *renderer,
                                          const char *data_root, const char *name) {
    if (!name || name[0] == '\0') return true;
    if (R_CacheFind(cache, name)) return true;
    if (cache->count >= MAX_DECORATION_SPRITES) {
        fprintf(stderr, "too many Dark Colony sprites; skipped %s\n", name);
        return false;
    }
    char sprite_path[1024];
    if (name[0] == '/') {
        snprintf(sprite_path, sizeof(sprite_path), "%s", name);
    } else {
        M_PathJoin(sprite_path, sizeof(sprite_path), data_root, name);
    }
    cachedsprite_t *entry = &cache->entries[cache->count];
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    uint32_t palette[256] = { 0 };
    if (!load_dark_colony_sprite(renderer, sprite_path, &entry->sprite, palette)) {
        fprintf(stderr, "failed to load %s\n", sprite_path);
        memset(entry, 0, sizeof(*entry));
        return false;
    }
    cache->count++;
    DarkColonySpriteNative *native = entry->sprite.native_data;
    if (native && native->has_animation) {
        for (int i = 0; i < native->animation.dependency_count; ++i) {
            char dependency_name[64];
            if (!dark_colony_dependency_sprite_name(dependency_name, sizeof(dependency_name),
                                                    native->animation.dependencies[i].name)) {
                continue;
            }
            if (R_CacheFind(cache, dependency_name)) continue;
            char dependency_path[1024];
            M_PathJoin(dependency_path, sizeof(dependency_path), data_root, dependency_name);
            if (!dark_colony_file_exists(dependency_path)) continue;
            if (!sprite_cache_load_dark_colony(cache, renderer, data_root, dependency_name))
                return false;
        }
    }
    return true;
}

bool load_dark_colony_unit_sprites(SDL_Renderer *renderer, const char *data_root,
                                   const level_t *map, const mobj_t *units, int unit_count,
                                   spritecache_t *cache) {
    bool ok = true;
    static const char *const ui_sprites[] = {
        "INTRFACE/DCSS.SPR",
        "INTRFACE/DCUT.SPR",
        "INTRFACE/MAINBUT.SPR",
        "INTRFACE/SHUMANE.SPR",
        "SPRITES/DROP.SPR",
        "SPRITES/BEAC.SPR",
        "SPRITES/MUZA.SPR",
        "SPRITES/BLOO.SPR",
    };
    for (int i = 0; i < NUMSTATES; ++i) {
        int sprite = states[i].sprite;
        if (sprite >= 0 && sprite < NUMSPRITES &&
            !sprite_cache_load_dark_colony(cache, renderer, data_root, sprnames[sprite])) {
            ok = false;
        }
    }
    for (size_t i = 0; i < sizeof(ui_sprites) / sizeof(ui_sprites[0]); ++i) {
        if (!sprite_cache_load_dark_colony(cache, renderer, data_root, ui_sprites[i]))
            ok = false;
    }
    int selection_sprite = dark_colony_game_info.selection_marker.sprite;
    if (selection_sprite >= 0 && selection_sprite < NUMSPRITES &&
        !sprite_cache_load_dark_colony(cache, renderer, data_root, sprnames[selection_sprite])) {
        ok = false;
    }
    if (map) {
        for (int i = 0; i < map->decoration_count; ++i) {
            if (!sprite_cache_load_dark_colony(cache, renderer, data_root, map->decorations[i].sprite_name))
                ok = false;
            if (!sprite_cache_load_dark_colony(cache, renderer, data_root, map->decorations[i].sprite2_name))
                ok = false;
            if (!sprite_cache_load_dark_colony(cache, renderer, data_root, map->decorations[i].shadow_name))
                ok = false;
        }
    }
    for (int i = 0; i < unit_count; ++i) {
        if (!sprite_cache_load_dark_colony(cache, renderer, data_root,
                           units[i].core.sprite_name))
            ok = false;
        if (!sprite_cache_load_dark_colony(cache, renderer, data_root, units[i].shadow_name))
            ok = false;
        if (!sprite_cache_load_dark_colony(cache, renderer, data_root, units[i].muzzle_flash_name))
            ok = false;
        if (!sprite_cache_load_dark_colony(cache, renderer, data_root, units[i].hit_effect_name))
            ok = false;
    }
    return ok;
}

bool dark_colony_vent_placement_from_sprites(const char *map_path,
                                             DarkColonyVentPlacement *out) {
    if (!map_path || !out) return false;
    memset(out, 0, sizeof(*out));

    const char *scenario = strcasestr(map_path, "/SCENARIO/");
    if (!scenario) return false;
    size_t root_len = (size_t)(scenario - map_path);
    if (root_len == 0 || root_len >= 900) return false;

    char glow_path[1024];
    char smoke_path[1024];
    char animation_path[1024];
    snprintf(glow_path, sizeof(glow_path), "%.*s/SPRITES/VENT2.SPR", (int)root_len, map_path);
    snprintf(smoke_path, sizeof(smoke_path), "%.*s/SPRITES/PUFF.SPR", (int)root_len, map_path);
    snprintf(animation_path, sizeof(animation_path), "%.*s/ANIMATE/VENT.FIN",
             (int)root_len, map_path);

    DarkColonyJuiceFile glow = {0};
    DarkColonyJuiceFile smoke = {0};
    DarkColonyAnimationFile animation = {0};
    if (!dark_colony_juice_load(glow_path, &glow) ||
        !dark_colony_juice_load(smoke_path, &smoke) ||
        !dark_colony_animation_load(animation_path, &animation) ||
        glow.cell_count <= 0) {
        dark_colony_juice_destroy(&glow);
        dark_colony_juice_destroy(&smoke);
        dark_colony_animation_destroy(&animation);
        return false;
    }

    const DarkColonyJuiceCell *plume = &glow.cells[0];
    const DarkColonyAnimationCommand *plume_command = dark_colony_animation_find_command(
        &animation, "VENTSTAND0", "vent2", 0, 0);
    if (!plume_command) {
        dark_colony_juice_destroy(&glow);
        dark_colony_juice_destroy(&smoke);
        dark_colony_animation_destroy(&animation);
        return false;
    }

    out->glow_left = plume_command->x + (int)plume->dis_x;
    out->glow_top = -plume_command->y + (int)plume->dis_y;

    const DarkColonyAnimationLabel *label = NULL;
    for (int i = 0; i < animation.label_count; ++i) {
        if (strcmp(animation.labels[i].name, "VENTSTAND0") == 0) {
            label = &animation.labels[i];
            break;
        }
    }
    if (!label || label->end < label->start ||
        label->end - label->start + 1 > DC_VENT_SMOKE_MAX_FRAMES) {
        dark_colony_juice_destroy(&glow);
        dark_colony_juice_destroy(&smoke);
        dark_colony_animation_destroy(&animation);
        return false;
    }
    for (int frame_index = label->start; frame_index <= label->end; ++frame_index) {
        const DarkColonyAnimationCommand *command = dark_colony_animation_frame_command(
            &animation, frame_index, "puff", 5);
        if (!command || command->frame < 0 || command->frame >= smoke.cell_count) {
            dark_colony_juice_destroy(&glow);
            dark_colony_juice_destroy(&smoke);
            dark_colony_animation_destroy(&animation);
            return false;
        }
        const DarkColonyJuiceCell *cell = &smoke.cells[command->frame];
        int raw_ticks = read_u16_le(animation.aux_records + (size_t)frame_index * 164 + 2);
        if (raw_ticks == 0) raw_ticks = 15;
        int runtime_tics = ((raw_ticks + 3) * 19) / 100;
        DarkColonyVentSmokeFrame *frame = &out->smoke_frames[out->smoke_frame_count++];
        frame->sprite_frame = command->frame;
        frame->pivot = (ivec2_t){
            -(command->x + (int)cell->dis_x),
            -(command->y - (int)cell->height),
        };
        frame->duration_ms = (runtime_tics * 1000 + 15) / 30;
    }
    out->valid = true;

    dark_colony_juice_destroy(&glow);
    dark_colony_juice_destroy(&smoke);
    dark_colony_animation_destroy(&animation);
    return true;
}

bool dark_colony_dropship_animation_from_sprites(const char *map_path,
                                                 DarkColonyDropshipAnimation *out) {
    if (!map_path || !out) return false;
    memset(out, 0, sizeof(*out));

    const char *scenario = strcasestr(map_path, "/SCENARIO/");
    if (!scenario) return false;
    size_t root_len = (size_t)(scenario - map_path);
    if (root_len == 0 || root_len >= 900) return false;

    char animation_path[1024];
    snprintf(animation_path, sizeof(animation_path), "%.*s/ANIMATE/DROP.FIN",
             (int)root_len, map_path);

    DarkColonyAnimationFile animation = {0};
    if (!dark_colony_animation_load(animation_path, &animation)) return false;

    const DarkColonyAnimationLabel *label = NULL;
    for (int i = 0; i < animation.label_count; ++i) {
        if (strcmp(animation.labels[i].name, "DROPTWO") == 0) {
            label = &animation.labels[i];
            break;
        }
    }
    if (!label || label->end < label->start ||
        label->end - label->start + 1 > DC_DROPSHIP_MAX_FRAMES) {
        dark_colony_animation_destroy(&animation);
        return false;
    }

    int command_index = 0;
    for (int frame_index = 0; frame_index < animation.aux_count; ++frame_index) {
        int part_count = read_u16_le(animation.aux_records + (size_t)frame_index * 164);
        if (frame_index >= label->start && frame_index <= label->end) {
            if (part_count > DC_DROPSHIP_MAX_PARTS ||
                command_index + part_count > animation.command_count) {
                dark_colony_animation_destroy(&animation);
                return false;
            }
            DarkColonyDropshipFrame *frame = &out->frames[out->frame_count++];
            int raw_ticks = read_u16_le(
                animation.aux_records + (size_t)frame_index * 164 + 2);
            if (raw_ticks == 0) raw_ticks = 15;
            int runtime_tics = ((raw_ticks + 3) * 19) / 100;
            frame->duration_ms = (runtime_tics * 1000 + 15) / 30;
            frame->part_count = part_count;
            out->duration_ms += frame->duration_ms;
            for (int part_index = 0; part_index < part_count; ++part_index) {
                const DarkColonyAnimationCommand *command =
                    &animation.commands[command_index + part_index];
                DarkColonyDropshipPart *part = &frame->parts[part_index];
                snprintf(part->sprite_name, sizeof(part->sprite_name),
                         "SPRITES/%s.SPR", command->sprite);
                for (char *p = part->sprite_name; *p; ++p)
                    *p = (char)toupper((unsigned char)*p);
                part->offset = (ivec2_t){ command->x, command->y };
                part->sprite_frame = command->frame;
                part->render_remap = command->remap;
                part->render_intensity = command->intensity;
                part->layer = command->layer;
                part->flags = command->flags;
            }
        }
        command_index += part_count;
    }
    out->valid = out->frame_count > 0 && out->duration_ms > 0;
    dark_colony_animation_destroy(&animation);
    return out->valid;
}
