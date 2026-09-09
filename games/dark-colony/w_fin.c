#include "w_spr.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

void DC_FreeAnimation(AnimationFile *animation) {
    if (!animation) return;
    free(animation->dependencies);
    free(animation->labels);
    free(animation->aux_records);
    free(animation->commands);
    memset(animation, 0, sizeof(*animation));
}

bool DC_LoadAnimation(const char *path, AnimationFile *out) {
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
        DC_FreeAnimation(out);
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
        AnimationCommand *cmd = &out->commands[i];
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

const AnimationCommand *DC_FindAnimationCommand(
    const AnimationFile *animation, const char *label_name,
    const char *sprite_name, int frame, int layer) {
    if (!animation || !label_name || !sprite_name) return NULL;

    int command_index = 0;
    for (int frame_index = 0; frame_index < animation->aux_count; ++frame_index) {
        int part_count = read_u16_le(animation->aux_records + (size_t)frame_index * 164);
        for (int label_index = 0; label_index < animation->label_count; ++label_index) {
            const AnimationLabel *label = &animation->labels[label_index];
            if (strcmp(label->name, label_name) != 0 ||
                frame_index < label->start || frame_index > label->end) {
                continue;
            }
            for (int part = 0; part < part_count; ++part) {
                if (command_index + part >= animation->command_count) return NULL;
                const AnimationCommand *command =
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

const AnimationCommand *DC_AnimationFrameCommand(
    const AnimationFile *animation, int frame_index,
    const char *sprite_name, int layer) {
    if (!animation || !sprite_name || frame_index < 0 || frame_index >= animation->aux_count)
        return NULL;

    int command_index = 0;
    for (int i = 0; i < frame_index; ++i)
        command_index += read_u16_le(animation->aux_records + (size_t)i * 164);
    int part_count = read_u16_le(animation->aux_records + (size_t)frame_index * 164);
    for (int part = 0; part < part_count; ++part) {
        if (command_index + part >= animation->command_count) return NULL;
        const AnimationCommand *command = &animation->commands[command_index + part];
        if (strcasecmp(command->sprite, sprite_name) == 0 && command->layer == layer)
            return command;
    }
    return NULL;
}

const AnimationLabel *DC_FindAnimationLabel(const AnimationFile *animation,
                                                  const char *name) {
    if (!animation || !name) return NULL;
    for (int i = 0; i < animation->label_count; ++i) {
        if (strcmp(animation->labels[i].name, name) == 0)
            return &animation->labels[i];
    }
    return NULL;
}

