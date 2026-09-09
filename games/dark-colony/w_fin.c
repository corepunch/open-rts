#include "w_spr.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* FIN spans stay in the file buffer; commands decode directly into sprite layers. */
static size_t fin_labels(const blob_t *fin) {
    return 8 + (size_t)read_u16_le(fin->bytes + 6) * 8;
}

static size_t fin_frames(const blob_t *fin) {
    return fin_labels(fin) + (size_t)read_u16_le(fin->bytes + 4) * 20;
}

static size_t fin_commands(const blob_t *fin) {
    return fin_frames(fin) + (size_t)read_u16_le(fin->bytes + 2) * 164;
}

bool DC_LoadFIN(const char *path, blob_t *out) {
    memset(out, 0, sizeof(*out));
    if (!W_ReadFile(path, out)) return false;
    if (out->size < 8) goto fail;
    if (read_u16_le(out->bytes + 4) > 1024 ||
        read_u16_le(out->bytes + 6) > 1024 ||
        read_u16_le(out->bytes + 2) > 4096) goto fail;
    size_t commands = fin_commands(out);
    if (commands > out->size || (out->size - commands) % 22) goto fail;
    size_t remaining = (out->size - commands) / 22;
    for (int i = 0; i < read_u16_le(out->bytes + 2); ++i) {
        int count = read_u16_le(out->bytes + fin_frames(out) + (size_t)i * 164);
        if ((size_t)count > remaining) goto fail;
        remaining -= count;
    }
    return true;
fail:
    W_FreeFile(out);
    return false;
}

const uint8_t *DC_FINLabel(const blob_t *fin, const char *name) {
    if (!fin->bytes) return NULL;
    for (int i = 0; i < read_u16_le(fin->bytes + 4); ++i) {
        const uint8_t *label = fin->bytes + fin_labels(fin) + (size_t)i * 20;
        if (strlen(name) <= 16 && strncmp((const char *)label, name, 16) == 0)
            return label;
    }
    return NULL;
}

int DC_FINCommandCount(const blob_t *fin) {
    return fin->bytes ? (int)((fin->size - fin_commands(fin)) / 22) : 0;
}

bool DC_FINLayer(const blob_t *fin, int index, spritelayer_t *out) {
    if (index < 0 || index >= DC_FINCommandCount(fin)) return false;
    const uint8_t *command = fin->bytes + fin_commands(fin) + (size_t)index * 22;
    if ((int16_t)read_u16_le(command + 8) < 0) return false;
    for (int offset = 14; offset <= 20; offset += 2)
        if (read_u16_le(command + offset) > UINT8_MAX) return false;
    *out = (spritelayer_t){
        .lump = read_u16_le(command + 8),
        .offset = { (int16_t)read_u16_le(command + 10),
                    (int16_t)read_u16_le(command + 12) },
        .remap = read_u16_le(command + 14),
        .intensity = read_u16_le(command + 16),
        .layer = read_u16_le(command + 18),
        .flags = read_u16_le(command + 20),
    };
    snprintf(out->sprite_name, sizeof(out->sprite_name), "%.8s", (const char *)command);
    return true;
}

bool DC_FINFrame(const blob_t *fin, int index, spritedirection_t *out) {
    if (!fin->bytes || index < 0 || index >= read_u16_le(fin->bytes + 2)) return false;
    const uint8_t *frame = fin->bytes + fin_frames(fin);
    int command = 0;
    for (int i = 0; i < index; ++i, frame += 164)
        command += read_u16_le(frame);
    int count = read_u16_le(frame);
    spritelayer_t *layers = calloc((size_t)count + 1, sizeof(*layers));
    if (!layers) return false;
    for (int i = 0; i < count; ++i) {
        if (!DC_FINLayer(fin, command + i, &layers[i])) {
            free(layers);
            return false;
        }
    }
    free(out->layers);
    *out = (spritedirection_t){ .ticks = read_u16_le(frame + 2), .layers = layers };
    return true;
}
