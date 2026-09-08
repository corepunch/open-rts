#define _DEFAULT_SOURCE
#include "engine.h"
#include "info.h"
#include "gamestat.h"
#include "dc_types.h"
#include "dc_facing.h"
#include "w_spr.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "w_sprite_private.h"

static uint8_t remap_palette_index(uint8_t index, int remap) {
    if (remap < 0 || remap > 7 || index < 138 || index > 143) return index;
    int remapped = (int)index + (remap - 7) * 6;
    if (remapped < 0 || remapped > 255) return index;
    return (uint8_t)remapped;
}

static uint32_t sprite_pixel_rgba(uint8_t index, const uint32_t palette[256],
                                             int remap) {
    if (index == 0) return 0x00000000u;
    index = remap_palette_index(index, remap);
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
    JuiceFile juice;
    AnimationFile animation;
    bool has_animation;
} SpriteNative;

typedef struct {
    bool valid;
    uint32_t palette[256];
    uint8_t selector5[256 * 256];
} RenderTables;

static RenderTables render_tables;

static bool install_fin_parts(spriteframe_t *spriteframe, int rotation,
                              const AnimationFile *animation, int frame_index,
                              const char *stem, int numlumps) {
    int command_index = 0;
    for (int i = 0; i < frame_index; ++i)
        command_index += read_u16_le(animation->aux_records + (size_t)i * 164);
    int part_count = read_u16_le(animation->aux_records + (size_t)frame_index * 164);
    if (part_count <= 0 || command_index + part_count > animation->command_count)
        return false;

    spritedirection_t *direction = &spriteframe->directions[rotation];
    spritelayer_t *layers = calloc((size_t)part_count + 1, sizeof(*layers));
    if (!layers) return false;
    const AnimationCommand *body = NULL;
    for (int i = 0; i < part_count; ++i) {
        const AnimationCommand *command = &animation->commands[command_index + i];
        if (command->frame < 0 || command->remap < 0 || command->remap > UINT8_MAX ||
            command->intensity < 0 || command->intensity > UINT8_MAX ||
            command->layer < 0 || command->layer > UINT8_MAX ||
            command->flags < 0 || command->flags > UINT8_MAX) {
            free(layers);
            return false;
        }
        spritelayer_t *part = &layers[i];
        if (strcasecmp(command->sprite, stem) == 0) {
            snprintf(part->sprite_name, sizeof(part->sprite_name), ".");
        } else {
            snprintf(part->sprite_name, sizeof(part->sprite_name), "%s", command->sprite);
            for (char *p = part->sprite_name; *p; ++p)
                *p = (char)toupper((unsigned char)*p);
        }
        part->lump = command->frame;
        part->offset = (ivec2_t){ command->x, command->y };
        part->remap = command->remap;
        part->intensity = command->intensity;
        part->layer = command->layer;
        part->flags = (uint8_t)command->flags;
        if (!body && strcasecmp(command->sprite, stem) == 0 && command->layer == 1)
            body = command;
    }
    if (!body || body->frame < 0 || body->frame >= numlumps) {
        free(layers);
        return false;
    }
    free(direction->layers);
    direction->layers = layers;
    direction->ticks = read_u16_le(
        animation->aux_records + (size_t)frame_index * 164 + 2);
    return true;
}

static void install_dark_colony_fin_frames(spritesheet_t *sheet,
                                           const AnimationFile *animation,
                                           const char *stem, const char *action) {
    if (!sheet || !animation || !stem || !action ||
        !sheet->spritedef.spriteframes) return;
    char label_name[17];
    char label_stem[9];
    snprintf(label_stem, sizeof(label_stem), "%s", stem);
    for (char *c = label_stem; *c; ++c)
        *c = (char)toupper((unsigned char)*c);
    enum { MAX_FIN_SEQUENCE_FRAMES = 32 };
    int rotations = 16;
    int length = -1;
retry:
    length = -1;
    for (int rotation = 0; rotation < rotations; ++rotation) {
        int direction = rotations == 16 ? rotation : rotation * 2;
        const char *label_action = strcmp(label_stem, "EXPL") == 0 &&
            strcmp(action, "STAND") == 0 && (direction & 1) ? "SHUF" : action;
        snprintf(label_name, sizeof(label_name), "%s%s%d", label_stem, label_action,
                 direction);
        const AnimationLabel *label = DC_FindAnimationLabel(animation, label_name);
        if (!label || label->end < label->start) {
            if (rotations == 16) {
                rotations = 8;
                goto retry;
            }
            return;
        }
        int label_length = label->end - label->start + 1;
        if (label_length > MAX_FIN_SEQUENCE_FRAMES) return;
        if (length < 0) length = label_length;
        if (label_length > length) length = label_length;
    }

    sheet->spritedef.rotations = rotations;
    sheet->spritedef.first_angle = dc_fin_direction_to_angle(0);
    sheet->spritedef.clockwise = true;
    snprintf(label_name, sizeof(label_name), "%s%s0", label_stem, action);
    const AnimationLabel *base_label = DC_FindAnimationLabel(animation, label_name);
    if (!base_label) return;
    for (int frame = 0; frame < length; ++frame) {
        int logical_frame = base_label->start + frame;
        if (logical_frame < 0 || logical_frame >= sheet->spritedef.numframes) continue;
        spriteframe_t *spriteframe = &sheet->spritedef.spriteframes[logical_frame];
        snprintf(spriteframe->frame_name, sizeof(spriteframe->frame_name),
             "%s", base_label->name);
        for (int rotation = 0; rotation < sheet->spritedef.rotations; ++rotation) {
            int direction = rotations == 16 ? rotation : rotation * 2;
            const char *label_action = strcmp(label_stem, "EXPL") == 0 &&
                strcmp(action, "STAND") == 0 && (direction & 1) ? "SHUF" : action;
            snprintf(label_name, sizeof(label_name), "%s%s%d", label_stem,
                     label_action, direction);
            const AnimationLabel *label = DC_FindAnimationLabel(animation, label_name);
            if (!label) continue;
            int label_length = label->end - label->start + 1;
            int source_frame = label->start + (frame < label_length ? frame : label_length - 1);
            install_fin_parts(spriteframe, rotation, animation, source_frame,
                              stem, sheet->numlumps);
        }
    }
}

static void sprite_native_destroy(SpriteNative *native) {
    if (!native) return;
    DC_FreeJuice(&native->juice);
    DC_FreeAnimation(&native->animation);
    free(native);
}

bool load_render_tables(const char *data_root, const char *tileset_name) {
    memset(&render_tables, 0, sizeof(render_tables));
    if (!data_root || !tileset_name || tileset_name[0] == '\0') return false;

    char bts_name[64];
    char scenario_dir[1024];
    char bts_path[1024];
    snprintf(bts_name, sizeof(bts_name), "%s.BTS", tileset_name);
    M_PathJoin(scenario_dir, sizeof(scenario_dir), data_root, "SCENARIO");
    M_PathJoin(bts_path, sizeof(bts_path), scenario_dir, bts_name);
    blob_t bts;
    if (!W_ReadFile(bts_path, &bts)) return false;
    if (bts.size < 8 + 256 * 3) {
        W_FreeFile(&bts);
        return false;
    }
    DC_DecodePalette(bts.bytes, bts.size, render_tables.palette);
    W_FreeFile(&bts);

    char rmp_name[64];
    char rmp_path[1024];
    snprintf(rmp_name, sizeof(rmp_name), "%s.RMP", tileset_name);
    M_PathJoin(rmp_path, sizeof(rmp_path), data_root, rmp_name);
    blob_t rmp;
    if (!W_ReadFile(rmp_path, &rmp)) return false;
    if (rmp.size < 3 * 256 * 256) {
        W_FreeFile(&rmp);
        return false;
    }
    memcpy(render_tables.selector5, rmp.bytes + 256 * 256,
           sizeof(render_tables.selector5));
    W_FreeFile(&rmp);
    render_tables.valid = true;
    return true;
}

static void resolve_fin_ground_points(const SpriteNative *native, const char *sprite_path,
                                      SDL_Point *ground_points) {
    if (!native || !native->has_animation || !sprite_path || !ground_points) return;
    char stem[9];
    DC_SpriteStem(stem, sizeof(stem), sprite_path);
    bool *resolved = calloc(native->juice.cell_count, sizeof(*resolved));
    if (!resolved) return;
    for (int i = 0; i < native->animation.command_count; ++i) {
        const AnimationCommand *command = &native->animation.commands[i];
        if (command->layer != 1 || strcasecmp(command->sprite, stem) != 0 ||
            command->frame < 0 || command->frame >= native->juice.cell_count) {
            continue;
        }
        const JuiceCell *cell = &native->juice.cells[command->frame];
        SDL_Point pivot = {
            (command->flags & 1) ? (int)cell->width + command->x :
                                   -command->x - (int)cell->dis_x,
            (int)cell->height - command->y,
        };
        if (!resolved[command->frame]) {
            ground_points[command->frame] = pivot;
            resolved[command->frame] = true;
        }
    }
    free(resolved);
}

bool DC_LoadSpriteWithAnimation(SDL_Renderer *renderer, const char *path,
                                             spritesheet_t *out,
                                             uint32_t palette_out[256],
                                             AnimationFile *animation_out) {
    memset(out, 0, sizeof(*out));

    SpriteNative *native = calloc(1, sizeof(*native));
    if (!native) return false;
    char sprite_path[1024];
    char animation_path[1024];
    const char *extension = strrchr(path, '.');
    bool fin_first = extension && strcasecmp(extension, ".FIN") == 0;
    if (fin_first) {
        if (!DC_LoadAnimation(path, &native->animation) ||
            !DC_SpritePath(sprite_path, sizeof(sprite_path), path)) {
            fprintf(stderr, "%s is not a supported Dark Colony FIN\n", path);
            sprite_native_destroy(native);
            return false;
        }
        native->has_animation = true;
    } else {
        snprintf(sprite_path, sizeof(sprite_path), "%s", path);
    }
    if (!DC_LoadJuice(sprite_path, &native->juice)) {
        fprintf(stderr, "%s is not a supported Dark Colony raw SPR\n", sprite_path);
        sprite_native_destroy(native);
        return false;
    }
    if (palette_out) memcpy(palette_out, native->juice.palette, sizeof(native->juice.palette));
    const uint32_t *palette = native->juice.palette;

    if (!fin_first && DC_AnimationPath(animation_path, sizeof(animation_path), path) &&
        DC_AssetExists(animation_path) &&
        DC_LoadAnimation(animation_path, &native->animation)) {
        native->has_animation = true;
    }

    JuiceFile *juice = &native->juice;
    int visible_frames = juice->cell_count;
    int max_w = 1, max_h = 1;
    int canvas_w = 1, canvas_h = 1;
    for (int i = 0; i < visible_frames; ++i) {
        const JuiceCell *cell = &juice->cells[i];
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
    SDL_Point *ground_points = calloc((size_t)visible_frames, sizeof(SDL_Point));
    SDL_Point *displacements = calloc((size_t)visible_frames, sizeof(SDL_Point));
    if (!rgba || !indices || !frames || !bounds || !ground_points || !displacements) {
        free(rgba); free(indices); free(frames); free(bounds);
        free(ground_points); free(displacements);
        sprite_native_destroy(native);
        return false;
    }

    bool has_team_colors = false;
    for (int i = 0; i < visible_frames; ++i) {
        const JuiceCell *cell = &juice->cells[i];
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
                                rgba[dst] = sprite_pixel_rgba(index, palette, -1);
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
                            rgba[dst] = sprite_pixel_rgba(index, palette, -1);
                        }
                    }
                }
            }
        }
        frames[i] = (irect_t){ fx, fy, w, h };
        bounds[i] = dc_visible_bounds(rgba, atlas_w, frames[i]);
    }

    out->lumps = calloc((size_t)visible_frames, sizeof(*out->lumps));
    int logical_frames = native->has_animation ? native->animation.frame_count : visible_frames;
    if (logical_frames < visible_frames) logical_frames = visible_frames;
    out->spritedef.spriteframes = calloc(
        (size_t)logical_frames, sizeof(*out->spritedef.spriteframes));
    if (!out->lumps || !out->spritedef.spriteframes) {
        free(rgba);
        free(indices);
        free(frames);
        free(bounds);
        free(ground_points);
        free(displacements);
        sprite_native_destroy(native);
        R_FreeSprite(out);
        return false;
    }
    out->numlumps = visible_frames;
    for (int i = 0; i < visible_frames; ++i) {
        ground_points[i] = (SDL_Point){
            bounds[i].x + bounds[i].w / 2,
            bounds[i].y + bounds[i].h,
        };
    }
    resolve_fin_ground_points(native, sprite_path, ground_points);
    for (int i = 0; i < visible_frames; ++i) {
        out->lumps[i] = (spritelump_t){
            .bounds = bounds[i],
            .ground_point = { ground_points[i].x, ground_points[i].y },
            .displacement = { displacements[i].x, displacements[i].y },
        };
        if (!R_CreateSpriteLumpTexture(renderer, &out->lumps[i], rgba, atlas_w,
                                       frames[i], true, -1)) {
            free(rgba); free(indices); free(frames); free(bounds);
            free(ground_points); free(displacements);
            sprite_native_destroy(native);
            R_FreeSprite(out);
            return false;
        }
        size_t frame_pixels = (size_t)frames[i].w * (size_t)frames[i].h;
        out->lumps[i].indices = malloc(frame_pixels);
        if (!out->lumps[i].indices) {
            free(rgba); free(indices); free(frames); free(bounds);
            free(ground_points); free(displacements);
            sprite_native_destroy(native);
            R_FreeSprite(out);
            return false;
        }
        for (int y = 0; y < frames[i].h; ++y) {
            memcpy(out->lumps[i].indices + (size_t)y * (size_t)frames[i].w,
                   indices + (size_t)(frames[i].y + y) * (size_t)atlas_w +
                       (size_t)frames[i].x,
                   (size_t)frames[i].w);
        }
        spritelayer_t *layer = calloc(2, sizeof(*layer));
        if (!layer) {
            free(rgba); free(indices); free(frames); free(bounds);
            free(ground_points); free(displacements);
            sprite_native_destroy(native);
            R_FreeSprite(out);
            return false;
        }
        out->spritedef.spriteframes[i].directions[0].layers = layer;
        snprintf(layer->sprite_name, sizeof(layer->sprite_name), ".");
        layer->lump = i;
        layer->intensity = 16;
    }
    if (has_team_colors) {
        uint32_t *remap_rgba = calloc((size_t)atlas_w * (size_t)atlas_h,
                                      sizeof(*remap_rgba));
        if (!remap_rgba) {
            free(rgba); free(indices); free(frames); free(bounds);
            free(ground_points); free(displacements);
            sprite_native_destroy(native);
            R_FreeSprite(out);
            return false;
        }
        for (int remap = 0; remap < 8; ++remap) {
            for (size_t pos = 0; pos < (size_t)atlas_w * (size_t)atlas_h; ++pos)
                remap_rgba[pos] = sprite_pixel_rgba(indices[pos], palette, remap);
            for (int i = 0; i < visible_frames; ++i) {
                if (!R_CreateSpriteLumpTexture(renderer, &out->lumps[i], remap_rgba,
                                               atlas_w, frames[i], true, remap)) {
                    free(remap_rgba); free(rgba); free(indices); free(frames);
                    free(bounds); free(ground_points); free(displacements);
                    sprite_native_destroy(native);
                    R_FreeSprite(out);
                    return false;
                }
            }
        }
        free(remap_rgba);
    }
    free(frames);
    free(bounds);
    free(ground_points);
    free(displacements);
    out->spritedef.numframes = logical_frames;
    out->spritedef.rotations = 1;
    out->spritedef.first_angle = ANG270;
    out->frame_size = (isize2_t){ canvas_w, canvas_h };
    out->indexed = true;
    if (render_tables.valid) {
        memcpy(out->palette, render_tables.palette, sizeof(out->palette));
        out->indexed_blend_selector = 5;
        out->indexed_blend_table = render_tables.selector5;
    } else {
        memcpy(out->palette, palette, sizeof(out->palette));
    }
    if (native->has_animation) {
        char stem[9];
        DC_SpriteStem(stem, sizeof(stem), sprite_path);
        install_dark_colony_fin_frames(out, &native->animation, stem, "STAND");
        install_dark_colony_fin_frames(out, &native->animation, stem, "MOVE");
        install_dark_colony_fin_frames(out, &native->animation, stem, "FIREA");
        install_dark_colony_fin_frames(out, &native->animation, stem, "FIREB");
        install_dark_colony_fin_frames(out, &native->animation, stem, "FIRE");
        install_dark_colony_fin_frames(out, &native->animation, stem, "DIEA");
        install_dark_colony_fin_frames(out, &native->animation, stem, "DIEB");
        install_dark_colony_fin_frames(out, &native->animation, stem, "DIEC");
    }

    free(rgba);
    free(indices);
    if (animation_out) {
        *animation_out = native->animation;
        memset(&native->animation, 0, sizeof(native->animation));
    }
    sprite_native_destroy(native);
    return true;
}

bool load_dark_colony_sprite(SDL_Renderer *renderer, const char *path, spritesheet_t *out,
                             uint32_t palette_out[256]) {
    return DC_LoadSpriteWithAnimation(renderer, path, out, palette_out, NULL);
}
