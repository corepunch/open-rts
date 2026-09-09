#define _DEFAULT_SOURCE
#include "w_drop.h"
#include "w_spr.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool load_dropship_label(const dc_fin_t *fin, const char *label_name,
                                DropshipAnimation *out) {
    const dc_fin_label_t *label = DC_FINLabel(fin, label_name);
    if (!label) return false;
    int start = SDL_SwapLE16(label->start), end = SDL_SwapLE16(label->end);
    if (end < start || end - start + 1 > DROPSHIP_MAX_FRAMES) return false;
    for (int i = start; i <= end; ++i) {
        spritedirection_t direction = {0};
        if (!DC_FINFrame(fin, i, &direction)) return false;
        DropshipFrame *frame = &out->frames[out->frame_count++];
        int raw_ticks = direction.ticks ? direction.ticks : 15;
        int runtime_tics = ((raw_ticks + 3) * 19) / 100;
        frame->duration_ms = (runtime_tics * 1000 + 15) / 30;
        out->duration_ms += frame->duration_ms;
        for (spritelayer_t *layer = direction.layers; layer->sprite_name[0]; ++layer) {
            if (frame->part_count == DROPSHIP_MAX_PARTS) {
                free(direction.layers);
                return false;
            }
            DropshipPart *part = &frame->parts[frame->part_count++];
            snprintf(part->sprite_name, sizeof(part->sprite_name),
                     "SPRITES/%s.SPR", layer->sprite_name);
            for (char *p = part->sprite_name; *p; ++p)
                *p = (char)toupper((unsigned char)*p);
            part->offset = layer->offset;
            part->sprite_frame = layer->lump;
            part->render_remap = layer->remap;
            part->render_intensity = layer->intensity;
            part->render_selector = layer->layer;
            part->flags = layer->flags;
        }
        free(direction.layers);
    }
    out->valid = out->frame_count > 0 && out->duration_ms > 0;
    return out->valid;
}

bool DC_LoadDropshipAnimations(const char *map_path,
                                                 DropshipAnimations *out) {
    if (!map_path || !out) return false;
    memset(out, 0, sizeof(*out));

    const char *scenario = strcasestr(map_path, "/SCENARIO/");
    if (!scenario) return false;
    size_t root_len = (size_t)(scenario - map_path);
    if (root_len == 0 || root_len >= 900) return false;

    char animation_path[1024];
    snprintf(animation_path, sizeof(animation_path), "%.*s/ANIMATE/DROP.FIN",
             (int)root_len, map_path);

    dc_fin_t animation = {0};
    if (!DC_LoadFIN(animation_path, &animation)) return false;

    bool valid = load_dropship_label(&animation, "DROPMOVE0", &out->move) &&
                 load_dropship_label(&animation, "DROPSTAND0", &out->stand) &&
                 load_dropship_label(&animation, "DROPTWO", &out->unload);
    DC_FreeFIN(&animation);
    return valid;
}
