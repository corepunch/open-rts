#include "w_spr.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(dc_fin_header_t) == 8, "FIN header layout");
_Static_assert(sizeof(dc_fin_dependency_t) == 8, "FIN dependency layout");
_Static_assert(sizeof(dc_fin_label_t) == 20, "FIN label layout");
_Static_assert(offsetof(dc_fin_label_t, start) == 16, "FIN label range layout");
_Static_assert(sizeof(dc_fin_frame_t) == 164, "FIN frame layout");
_Static_assert(sizeof(dc_fin_command_t) == 22, "FIN command layout");
_Static_assert(offsetof(dc_fin_command_t, offset) == 10, "FIN command offset layout");
_Static_assert(sizeof(dc_spr_header_t) == 776, "SPR header and palette layout");
_Static_assert(sizeof(dc_spr_cell_t) == 8, "SPR cell layout");
_Static_assert(offsetof(dc_spr_header_t, palette) == 8, "SPR palette layout");
_Static_assert(offsetof(dc_spr_cell_t, displacement) == 4, "SPR displacement layout");
_Static_assert(offsetof(dc_fin_command_t, flags) == 20, "FIN flags layout");

const void *DC_TakeRecords(blob_t *cursor, size_t count, size_t record_size) {
    if (!record_size || count > cursor->size / record_size) return NULL;
    const void *records = cursor->bytes;
    size_t bytes = count * record_size;
    cursor->bytes += bytes;
    cursor->size -= bytes;
    return records;
}

void DC_FreeFIN(dc_fin_t *fin) {
    free(fin->frame_commands);
    W_FreeFile(&fin->file);
    memset(fin, 0, sizeof(*fin));
}

bool DC_LoadFIN(const char *path, dc_fin_t *out) {
    memset(out, 0, sizeof(*out));
    if (!W_ReadFile(path, &out->file)) return false;
    blob_t cursor = out->file;
    out->header = DC_TakeRecords(&cursor, 1, sizeof(*out->header));
    if (!out->header) goto fail;
    int frames = SDL_SwapLE16(out->header->frame_count);
    int labels = SDL_SwapLE16(out->header->label_count);
    int dependencies = SDL_SwapLE16(out->header->dependency_count);
    out->dependencies = DC_TakeRecords(&cursor, dependencies, sizeof(*out->dependencies));
    out->labels = DC_TakeRecords(&cursor, labels, sizeof(*out->labels));
    out->frames = DC_TakeRecords(&cursor, frames, sizeof(*out->frames));
    if (!out->dependencies || !out->labels || !out->frames ||
        cursor.size % sizeof(*out->commands) ||
        cursor.size / sizeof(*out->commands) > INT32_MAX) goto fail;
    out->command_count = (int)(cursor.size / sizeof(*out->commands));
    out->commands = DC_TakeRecords(&cursor, out->command_count, sizeof(*out->commands));
    out->frame_commands = calloc(frames ? (size_t)frames : 1, sizeof(*out->frame_commands));
    if (!out->frame_commands) goto fail;
    int command = 0;
    for (int i = 0; i < frames; ++i) {
        int count = SDL_SwapLE16(out->frames[i].part_count);
        if (count > out->command_count - command) goto fail;
        out->frame_commands[i] = &out->commands[command];
        command += count;
    }
    return true;
fail:
    DC_FreeFIN(out);
    return false;
}

const dc_fin_label_t *DC_FINLabel(const dc_fin_t *fin, const char *name) {
    if (!fin->header || strlen(name) > sizeof(fin->labels->name)) return NULL;
    for (int i = 0; i < SDL_SwapLE16(fin->header->label_count); ++i)
        if (strncmp(fin->labels[i].name, name, sizeof(fin->labels[i].name)) == 0)
            return &fin->labels[i];
    return NULL;
}

static bool read_layer(const dc_fin_command_t *command, spritelayer_t *out) {
    int cell = (int16_t)SDL_SwapLE16(command->cell);
    int remap = SDL_SwapLE16(command->remap);
    int intensity = SDL_SwapLE16(command->intensity);
    int layer = SDL_SwapLE16(command->layer);
    int flags = SDL_SwapLE16(command->flags);
    if (cell < 0 || remap > UINT8_MAX || intensity > UINT8_MAX ||
        layer > UINT8_MAX || flags > UINT8_MAX) return false;
    *out = (spritelayer_t){
        .lump = cell,
        .offset = { (int16_t)SDL_SwapLE16(command->offset.x),
                    (int16_t)SDL_SwapLE16(command->offset.y) },
        .remap = remap, .intensity = intensity, .layer = layer, .flags = flags,
    };
    snprintf(out->sprite_name, sizeof(out->sprite_name), "%.8s", command->sprite);
    return true;
}

bool DC_FINLayer(const dc_fin_t *fin, int index, spritelayer_t *out) {
    return index >= 0 && index < fin->command_count && read_layer(&fin->commands[index], out);
}

bool DC_FINFrame(const dc_fin_t *fin, int index, spritedirection_t *out) {
    if (!fin->header || index < 0 || index >= SDL_SwapLE16(fin->header->frame_count))
        return false;
    const dc_fin_frame_t *frame = &fin->frames[index];
    int count = SDL_SwapLE16(frame->part_count);
    spritelayer_t *layers = calloc((size_t)count + 1, sizeof(*layers));
    if (!layers) return false;
    for (int i = 0; i < count; ++i) {
        if (!read_layer(&fin->frame_commands[index][i], &layers[i])) {
            free(layers);
            return false;
        }
    }
    free(out->layers);
    *out = (spritedirection_t){ .ticks = SDL_SwapLE16(frame->ticks), .layers = layers };
    return true;
}
