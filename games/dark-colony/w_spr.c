#include "dc_facing.h"
#include "w_spr.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "w_sprite_private.h"

static void decode_palette(const uint8_t *spr, size_t size, uint32_t colors[256]) {
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
    decode_palette(bts.bytes, bts.size, render_tables.palette);
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

/* SPR is an 8-byte header, 256 RGB entries, 8-byte cell descriptors,
 * then consecutive raw pixels or length-prefixed skip/literal streams.
 * Read those spans directly from the file into the final indexed lumps. */
static bool decode_cell(uint8_t *dst, size_t pixels,
                        const uint8_t *src, size_t size, bool compressed) {
    if (!compressed) {
        memcpy(dst, src, size);
        return true;
    }
    size_t read = 0, write = 0;
    while (read < size && write < pixels) {
        int command = (int8_t)src[read++];
        size_t count = command < 0 ? (size_t)-command : (size_t)command + 1;
        if (count > pixels - write) return false;
        if (command >= 0) {
            if (count > size - read) return false;
            memcpy(dst + write, src + read, count);
            read += count;
        }
        write += count;
    }
    return true;
}

static irect_t cell_bounds(const spritelump_t *lump) {
    int min_x = lump->rect.w, min_y = lump->rect.h, max_x = -1, max_y = -1;
    for (int y = 0; y < lump->rect.h; ++y) {
        for (int x = 0; x < lump->rect.w; ++x) {
            if (!lump->indices[y * lump->rect.w + x]) continue;
            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x > max_x) max_x = x;
            if (y > max_y) max_y = y;
        }
    }
    return max_x < min_x ? lump->rect :
        (irect_t){ min_x, min_y, max_x - min_x + 1, max_y - min_y + 1 };
}

static ivec2_t cell_ground_point(const spritelump_t *lump, int frame,
                                 const AnimationFile *animation, const char *stem) {
    for (int i = 0; i < animation->command_count; ++i) {
        const AnimationCommand *command = &animation->commands[i];
        if (command->layer == 1 && command->frame == frame &&
            strcasecmp(command->sprite, stem) == 0) {
            return (ivec2_t){
                (command->flags & 1) ? lump->rect.w + command->x :
                                       -command->x - lump->displacement.x,
                lump->rect.h - command->y,
            };
        }
    }
    return (ivec2_t){ lump->bounds.x + lump->bounds.w / 2,
                      lump->bounds.y + lump->bounds.h };
}

static bool create_cell_textures(SDL_Renderer *renderer, spritelump_t *lump,
                                  const uint32_t palette[256]) {
    size_t count = (size_t)lump->rect.w * (size_t)lump->rect.h;
    uint32_t *rgba = malloc(count * sizeof(*rgba));
    if (!rgba) return false;
    bool team_colors = false;
    for (size_t i = 0; i < count; ++i) {
        uint8_t index = lump->indices[i];
        rgba[i] = palette[index];
        team_colors |= index >= 138 && index <= 143;
    }
    lump->texture = I_CreateTexture(renderer, rgba, lump->rect.w, lump->rect.h, true);
    if (!lump->texture) goto fail;
    if (team_colors) {
        lump->translations = calloc(8, sizeof(*lump->translations));
        if (!lump->translations) goto fail;
        for (int remap = 0; remap < 8; ++remap) {
            for (size_t i = 0; i < count; ++i) {
                int index = lump->indices[i];
                if (index >= 138 && index <= 143) index += (remap - 7) * 6;
                rgba[i] = palette[index];
            }
            SDL_Texture *texture = I_CreateTexture(
                renderer, rgba, lump->rect.w, lump->rect.h, true);
            if (!texture) goto fail;
            lump->translations[lump->translation_count++] =
                (spritetranslation_t){ .id = remap, .texture = texture };
        }
    }
    free(rgba);
    return true;
fail:
    free(rgba);
    return false; /* The sheet owns every texture already created. */
}

bool DC_LoadSpriteWithAnimation(SDL_Renderer *renderer, const char *path,
                                spritesheet_t *out, uint32_t palette_out[256],
                                AnimationFile *animation_out) {
    memset(out, 0, sizeof(*out));
    blob_t file = {0};
    AnimationFile animation = {0};
    uint32_t palette[256];
    char sprite_path[1024], animation_path[1024], stem[9];
    const char *extension = strrchr(path, '.');
    bool fin_first = extension && strcasecmp(extension, ".FIN") == 0;
    if (fin_first) {
        if (!DC_LoadAnimation(path, &animation) ||
            !DC_SpritePath(sprite_path, sizeof(sprite_path), path)) goto fail;
    } else {
        snprintf(sprite_path, sizeof(sprite_path), "%s", path);
    }
    if (!W_ReadFile(sprite_path, &file) || file.size < 8 + 256 * 3) goto fail;
    int cell_count = read_u16_le(file.bytes + 2);
    bool compressed = (read_u16_le(file.bytes) & 0x180) != 0;
    size_t payload_left = read_u32_le(file.bytes + 4);
    size_t cursor = 8 + 256 * 3 + (size_t)cell_count * 8;
    if (cell_count == 0 || cell_count > 1024 || cursor > file.size) goto fail;
    decode_palette(file.bytes, file.size, palette);
    if (palette_out) memcpy(palette_out, palette, sizeof(palette));
    if (!fin_first && DC_AnimationPath(animation_path, sizeof(animation_path), path) &&
        DC_AssetExists(animation_path) && !DC_LoadAnimation(animation_path, &animation))
        DC_FreeAnimation(&animation);
    DC_SpriteStem(stem, sizeof(stem), sprite_path);

    int logical_frames = animation.frame_count > cell_count ? animation.frame_count : cell_count;
    out->lumps = calloc((size_t)cell_count, sizeof(*out->lumps));
    if (!out->lumps) goto fail;
    out->numlumps = cell_count;
    out->spritedef.spriteframes = calloc((size_t)logical_frames,
                                        sizeof(*out->spritedef.spriteframes));
    if (!out->spritedef.spriteframes) goto fail;
    out->spritedef.numframes = logical_frames;
    out->spritedef.rotations = 1;
    out->spritedef.first_angle = ANG270;
    out->frame_size = (isize2_t){ 1, 1 };
    for (int i = 0; i < cell_count; ++i) {
        const uint8_t *desc = file.bytes + 8 + 256 * 3 + (size_t)i * 8;
        isize2_t size = { read_u16_le(desc), read_u16_le(desc + 2) };
        if (size.w > 512 || size.h > 512) goto fail;
        size_t cell_bytes = (size_t)size.w * (size_t)size.h;
        if (compressed) {
            if (file.size - cursor < 4) goto fail;
            cell_bytes = read_u32_le(file.bytes + cursor);
            cursor += 4;
            if (cell_bytes > payload_left) goto fail;
            payload_left -= cell_bytes;
        }
        if (cell_bytes > file.size - cursor) goto fail;
        spritelump_t *lump = &out->lumps[i];
        lump->rect = (irect_t){ 0, 0, size.w > 0 ? size.w : 1,
                                     size.h > 0 ? size.h : 1 };
        lump->displacement = (ivec2_t){ read_u16_le(desc + 4), read_u16_le(desc + 6) };
        size_t pixels = (size_t)lump->rect.w * (size_t)lump->rect.h;
        lump->indices = calloc(pixels, 1);
        if (!lump->indices) goto fail;
        if (size.w && size.h &&
            !decode_cell(lump->indices, pixels, file.bytes + cursor, cell_bytes, compressed))
            goto fail;
        cursor += cell_bytes;
        lump->bounds = cell_bounds(lump);
        lump->ground_point = cell_ground_point(lump, i, &animation, stem);
        if (!create_cell_textures(renderer, lump, palette)) goto fail;
        if (lump->displacement.x + lump->rect.w > out->frame_size.w)
            out->frame_size.w = lump->displacement.x + lump->rect.w;
        if (lump->displacement.y + lump->rect.h > out->frame_size.h)
            out->frame_size.h = lump->displacement.y + lump->rect.h;
        spritelayer_t *layer = calloc(2, sizeof(*layer));
        if (!layer) goto fail;
        out->spritedef.spriteframes[i].directions[0].layers = layer;
        snprintf(layer->sprite_name, sizeof(layer->sprite_name), ".");
        layer->lump = i;
        layer->intensity = 16;
    }
    out->indexed = true;
    memcpy(out->palette, render_tables.valid ? render_tables.palette : palette,
           sizeof(out->palette));
    if (render_tables.valid) {
        out->indexed_blend_selector = 5;
        out->indexed_blend_table = render_tables.selector5;
    }
    static const char *const actions[] = {
        "STAND", "MOVE", "FIREA", "FIREB", "FIRE", "DIEA", "DIEB", "DIEC",
    };
    for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i)
        install_dark_colony_fin_frames(out, &animation, stem, actions[i]);
    if (animation_out) {
        *animation_out = animation;
        memset(&animation, 0, sizeof(animation));
    }
    W_FreeFile(&file);
    DC_FreeAnimation(&animation);
    return true;
fail:
    W_FreeFile(&file);
    DC_FreeAnimation(&animation);
    R_FreeSprite(out);
    return false;
}

bool load_dark_colony_sprite(SDL_Renderer *renderer, const char *path, spritesheet_t *out,
                             uint32_t palette_out[256]) {
    return DC_LoadSpriteWithAnimation(renderer, path, out, palette_out, NULL);
}
