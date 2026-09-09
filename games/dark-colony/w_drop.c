#define _DEFAULT_SOURCE
#include "w_drop.h"
#include "w_spr.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static bool load_dropship_label(const AnimationFile *animation,
                                            const char *label_name,
                                            DropshipAnimation *out) {
    const AnimationLabel *label = NULL;
    int command_index = 0;

    for (int i = 0; i < animation->label_count; ++i) {
        if (strcmp(animation->labels[i].name, label_name) == 0) {
            label = &animation->labels[i];
            break;
        }
    }
    if (!label || label->end < label->start ||
        label->end - label->start + 1 > DROPSHIP_MAX_FRAMES) return false;

    for (int frame_index = 0; frame_index < animation->aux_count; ++frame_index) {
        int part_count = read_u16_le(animation->aux_records + (size_t)frame_index * 164);
        if (frame_index >= label->start && frame_index <= label->end) {
            if (part_count > DROPSHIP_MAX_PARTS ||
                command_index + part_count > animation->command_count) return false;
            DropshipFrame *frame = &out->frames[out->frame_count++];
            int raw_ticks = read_u16_le(
                animation->aux_records + (size_t)frame_index * 164 + 2);
            if (raw_ticks == 0) raw_ticks = 15;
            int runtime_tics = ((raw_ticks + 3) * 19) / 100;
            frame->duration_ms = (runtime_tics * 1000 + 15) / 30;
            frame->part_count = part_count;
            out->duration_ms += frame->duration_ms;
            for (int part_index = 0; part_index < part_count; ++part_index) {
                const AnimationCommand *command =
                    &animation->commands[command_index + part_index];
                DropshipPart *part = &frame->parts[part_index];
                snprintf(part->sprite_name, sizeof(part->sprite_name),
                         "SPRITES/%s.SPR", command->sprite);
                for (char *p = part->sprite_name; *p; ++p)
                    *p = (char)toupper((unsigned char)*p);
                part->offset = (ivec2_t){ command->x, command->y };
                part->sprite_frame = command->frame;
                part->render_remap = command->remap;
                part->render_intensity = command->intensity;
                part->render_selector = command->layer;
                part->flags = command->flags;
            }
        }
        command_index += part_count;
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

    AnimationFile animation = {0};
    if (!DC_LoadAnimation(animation_path, &animation)) return false;

    bool valid = load_dropship_label(&animation, "DROPMOVE0", &out->move) &&
                 load_dropship_label(&animation, "DROPSTAND0", &out->stand) &&
                 load_dropship_label(&animation, "DROPTWO", &out->unload);
    DC_FreeAnimation(&animation);
    return valid;
}
