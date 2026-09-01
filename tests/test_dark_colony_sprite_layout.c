#include "../games/dark-colony/info.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[17];
    int start;
    int end;
} FinLabel;

typedef struct {
    char sprite[9];
    int frame;
    int x;
    int y;
    int remap;
    int intensity;
    int layer;
    int flags;
} FinCommand;

typedef struct {
    int part_count;
    int ticks;
    int command_start;
} FinFrame;

typedef struct {
    char stem_lower[9];
    FinLabel labels[512];
    int label_count;
    FinFrame frames[4096];
    int frame_count;
    FinCommand commands[4096];
    int command_count;
} FinInfo;

typedef struct {
    int width;
    int height;
    int dis_x;
    int dis_y;
} SprFrameInfo;

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static uint16_t layout_read_u16_le(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int16_t layout_read_i16_le(const unsigned char *p) {
    return (int16_t)layout_read_u16_le(p);
}

static unsigned char *read_file(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    unsigned char *data = malloc((size_t)size);
    if (!data) {
        fclose(f);
        return NULL;
    }
    if (size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size_out = (size_t)size;
    return data;
}

static void copy_padded_name(char *out, size_t out_size, const unsigned char *src, size_t src_size) {
    size_t n = 0;
    while (n + 1 < out_size && n < src_size && src[n] != '\0') {
        out[n] = (char)src[n];
        n++;
    }
    out[n] = '\0';
}

static bool load_fin_info(const char *path, const char *stem, FinInfo *out) {
    size_t size = 0;
    unsigned char *data = read_file(path, &size);
    if (!data) return false;
    if (size < 8) {
        free(data);
        return false;
    }
    memset(out, 0, sizeof(*out));
    for (size_t i = 0; stem[i] && i < sizeof(out->stem_lower) - 1; ++i) {
        out->stem_lower[i] = (char)tolower((unsigned char)stem[i]);
    }
    int default_tics = layout_read_u16_le(data + 0);
    int aux_count = layout_read_u16_le(data + 2);
    int label_count = layout_read_u16_le(data + 4);
    int deps = layout_read_u16_le(data + 6);
    size_t label_off = 8 + (size_t)deps * 8;
    (void)default_tics;
    int frame_count = aux_count;
    if (frame_count > 4096) {
        free(data);
        return false;
    }
    if (label_count < 0 || label_count > 512 ||
        label_off + (size_t)label_count * 20 > size) {
        free(data);
        return false;
    }
    out->label_count = label_count;
    for (int i = 0; i < label_count; ++i) {
        size_t off = label_off + (size_t)i * 20;
        copy_padded_name(out->labels[i].name, sizeof(out->labels[i].name), data + off, 16);
        out->labels[i].start = layout_read_u16_le(data + off + 16);
        out->labels[i].end = layout_read_u16_le(data + off + 18);
    }
    size_t frame_off = label_off + (size_t)label_count * 20;
    size_t command_off = frame_off + (size_t)aux_count * 164;
    if (command_off > size || (size - command_off) % 22 != 0) {
        free(data);
        return false;
    }
    int command_count = (int)((size - command_off) / 22);
    if (command_count > 4096) {
        free(data);
        return false;
    }
    out->command_count = command_count;
    out->frame_count = frame_count;
    int command_start = 0;
    for (int i = 0; i < frame_count; ++i) {
        size_t off = frame_off + (size_t)i * 164;
        int part_count = layout_read_u16_le(data + off);
        if (part_count > 100) {
            free(data);
            return false;
        }
        out->frames[i].part_count = part_count;
        out->frames[i].ticks = layout_read_u16_le(data + off + 2);
        out->frames[i].command_start = command_start;
        command_start += part_count;
    }
    if (command_start > command_count) {
        free(data);
        return false;
    }
    for (int i = 0; i < command_count; ++i) {
        size_t off = command_off + (size_t)i * 22;
        FinCommand *cmd = &out->commands[i];
        copy_padded_name(cmd->sprite, sizeof(cmd->sprite), data + off, 8);
        cmd->frame = layout_read_i16_le(data + off + 8);
        cmd->x = layout_read_i16_le(data + off + 10);
        cmd->y = layout_read_i16_le(data + off + 12);
        cmd->remap = layout_read_i16_le(data + off + 14);
        cmd->intensity = layout_read_i16_le(data + off + 16);
        cmd->layer = layout_read_i16_le(data + off + 18);
        cmd->flags = layout_read_i16_le(data + off + 20);
    }
    free(data);
    return true;
}

static const FinLabel *fin_label(const FinInfo *fin, const char *label) {
    for (int i = 0; i < fin->label_count; ++i) {
        if (strcmp(fin->labels[i].name, label) == 0) return &fin->labels[i];
    }
    return NULL;
}

static const FinCommand *fin_command(const FinInfo *fin, const char *label,
                                     const char *sprite, int layer, int frame) {
    const FinLabel *range = fin_label(fin, label);
    if (!range) return NULL;
    if (range->start < 0 || range->end < range->start || range->end >= fin->frame_count)
        return NULL;
    for (int frame_index = range->start; frame_index <= range->end; ++frame_index) {
        const FinFrame *fin_frame = &fin->frames[frame_index];
        for (int part = 0; part < fin_frame->part_count; ++part) {
            int command_index = fin_frame->command_start + part;
            if (command_index < 0 || command_index >= fin->command_count) return NULL;
            const FinCommand *cmd = &fin->commands[command_index];
            if (strcmp(cmd->sprite, sprite) == 0 && cmd->layer == layer &&
                cmd->frame == frame) {
                return cmd;
            }
        }
    }
    return NULL;
}

static const FinCommand *fin_layer_command_at(const FinInfo *fin, const char *label,
                                               const char *sprite, int layer, int index) {
    const FinLabel *range = fin_label(fin, label);
    if (!range || index < 0 || range->start < 0 || range->end < range->start ||
        range->end >= fin->frame_count) {
        return NULL;
    }
    int match = 0;
    const FinCommand *last = NULL;
    for (int frame_index = range->start; frame_index <= range->end; ++frame_index) {
        const FinFrame *fin_frame = &fin->frames[frame_index];
        for (int part = 0; part < fin_frame->part_count; ++part) {
            const FinCommand *cmd = &fin->commands[fin_frame->command_start + part];
            if (strcmp(cmd->sprite, sprite) != 0 || cmd->layer != layer) continue;
            last = cmd;
            if (match++ == index) return cmd;
        }
    }
    return last;
}

static const FinCommand *fin_frame_command(const FinInfo *fin, int frame_index,
                                           const char *sprite, int layer) {
    if (!fin || !sprite || frame_index < 0 || frame_index >= fin->frame_count) return NULL;
    const FinFrame *frame = &fin->frames[frame_index];
    for (int part = 0; part < frame->part_count; ++part) {
        const FinCommand *cmd = &fin->commands[frame->command_start + part];
        if (strcmp(cmd->sprite, sprite) == 0 && cmd->layer == layer) return cmd;
    }
    return NULL;
}

static bool load_spr_frame_info(const char *path, int frame, SprFrameInfo *out) {
    size_t size = 0;
    unsigned char *data = read_file(path, &size);
    if (!data) return false;
    int frame_count = size >= 4 ? layout_read_u16_le(data + 2) : 0;
    size_t desc_off = 8 + 256 * 3;
    if (frame < 0 || frame >= frame_count || desc_off + (size_t)(frame + 1) * 8 > size) {
        free(data);
        return false;
    }
    const unsigned char *desc = data + desc_off + (size_t)frame * 8;
    out->width = layout_read_u16_le(desc + 0);
    out->height = layout_read_u16_le(desc + 2);
    out->dis_x = layout_read_u16_le(desc + 4);
    out->dis_y = layout_read_u16_le(desc + 6);
    free(data);
    return true;
}

static int assert_dark_colony_city_fin_alignment(void) {
    FinInfo hubu_fin, towr_fin, expl_fin, vent_fin, drop_fin;
    if (!load_fin_info("data/DCOLONY/ANIMATE/HUBU.FIN", "HUBU", &hubu_fin) ||
        !load_fin_info("data/DCOLONY/ANIMATE/TOWR.FIN", "TOWR", &towr_fin) ||
        !load_fin_info("data/DCOLONY/ANIMATE/EXPL.FIN", "EXPL", &expl_fin) ||
        !load_fin_info("data/DCOLONY/ANIMATE/VENT.FIN", "VENT", &vent_fin) ||
        !load_fin_info("data/DCOLONY/ANIMATE/DROP.FIN", "DROP", &drop_fin)) {
        return fail("load Dark Colony FIN files");
    }
    const FinCommand *exco = fin_command(&hubu_fin, "EXCOPODSTAND0", "hubu", 1, 0);
    const FinCommand *barracks = fin_command(&hubu_fin, "BRRKPODSTAND0", "hubu", 1, 4);
    const FinCommand *tower = fin_command(&towr_fin, "TOWRSTAND0", "towr", 1, 0);
    if (!exco || !barracks || !tower) return fail("resolve Dark Colony city FIN commands");

    if (states[S_DC_EXCOPOD_STND].offset_x[0] != exco->x ||
        states[S_DC_EXCOPOD_STND].offset_y[0] != exco->y ||
        states[S_DC_BRRKPOD_STND].offset_x[0] != barracks->x ||
        states[S_DC_BRRKPOD_STND].offset_y[0] != barracks->y ||
        states[S_DC_TOWR_STND].offset_x[0] != tower->x ||
        states[S_DC_TOWR_STND].offset_y[0] != tower->y) {
        return fail("city building state offsets are raw FIN draw-command coordinates");
    }
    if (barracks->x != -36 || barracks->y != 37) {
        return fail("Barracks state uses raw BRRKPODSTAND0 FIN placement");
    }

    SprFrameInfo expl0, expl6, expl14, expl15, expl34, hubu4, towr0, vent0, beac0, beac1;
    if (!load_spr_frame_info("data/DCOLONY/SPRITES/EXPL.SPR", 0, &expl0) ||
        !load_spr_frame_info("data/DCOLONY/SPRITES/EXPL.SPR", 6, &expl6) ||
        !load_spr_frame_info("data/DCOLONY/SPRITES/EXPL.SPR", 14, &expl14) ||
        !load_spr_frame_info("data/DCOLONY/SPRITES/EXPL.SPR", 15, &expl15) ||
        !load_spr_frame_info("data/DCOLONY/SPRITES/EXPL.SPR", 34, &expl34) ||
        !load_spr_frame_info("data/DCOLONY/SPRITES/HUBU.SPR", 4, &hubu4) ||
        !load_spr_frame_info("data/DCOLONY/SPRITES/TOWR.SPR", 0, &towr0) ||
        !load_spr_frame_info("data/DCOLONY/SPRITES/VENT2.SPR", 0, &vent0) ||
        !load_spr_frame_info("data/DCOLONY/SPRITES/BEAC.SPR", 0, &beac0) ||
        !load_spr_frame_info("data/DCOLONY/SPRITES/BEAC.SPR", 1, &beac1)) {
        return fail("load raw Dark Colony SPR frame descriptors");
    }
    if (expl0.width != 46 || expl0.height != 49 || expl0.dis_x != 137 || expl0.dis_y != 104) {
        return fail("EXPL frame 0 keeps raw SPR descriptor values");
    }
    if (expl6.width != 61 || expl6.height != 60 || expl6.dis_x != 129 || expl6.dis_y != 102) {
        return fail("EXPL frame 6 keeps raw SPR descriptor values");
    }
    if (expl14.width != 61 || expl14.height != 53 || expl14.dis_x != 130 || expl14.dis_y != 106 ||
        expl15.width != 28 || expl15.height != 17 || expl15.dis_x != 149 || expl15.dis_y != 117) {
        return fail("EXPL deploy frames keep raw SPR descriptor values");
    }
    if (expl34.width != 62 || expl34.height != 79 ||
        expl34.dis_x != 130 || expl34.dis_y != 80) {
        return fail("EXPL deployed body keeps raw SPR descriptor values");
    }
    if (hubu4.width != 72 || hubu4.height != 105 || hubu4.dis_x != 4 || hubu4.dis_y != 2) {
        return fail("HUBU frame 4 keeps raw SPR descriptor values");
    }
    if (towr0.width != 77 || towr0.height != 257 || towr0.dis_x != 0 || towr0.dis_y != 0) {
        return fail("TOWR frame 0 keeps raw SPR descriptor values");
    }
    if (barracks->x + hubu4.dis_x != -32 || barracks->y - hubu4.height != -68 ||
        tower->x + towr0.dis_x != -36 || tower->y - towr0.height != -266) {
        return fail("city FIN parts use queued-world displacement and height placement");
    }
    if (beac0.width != 38 || beac0.height != 91 || beac0.dis_x != 39 || beac0.dis_y != 30 ||
        beac1.width != 20 || beac1.height != 40 || beac1.dis_x != 42 || beac1.dis_y != 27 ||
        beac1.dis_x - beac0.dis_x != 3 || beac1.dis_y - beac0.dis_y != -3) {
        return fail("BEAC glow frame stays anchored in frame 0 SPR canvas");
    }
    const FinCommand *expl_right = fin_command(&expl_fin, "EXPLSTAND0", "expl", 1, 0);
    const FinCommand *expl_left = fin_command(&expl_fin, "EXPLSTAND6", "expl", 1, 6);
    const FinCommand *expl_deploy_body = fin_command(&expl_fin, "EXPLDEPLOY14", "expl", 1, 14);
    const FinCommand *expl_deploy_top = fin_command(&expl_fin, "EXPLDEPLOY14", "expl", 0, 15);
    const FinCommand *expl_deployed_body = fin_command(&expl_fin, "EDPLYSTAND14", "expl", 1, 34);
    if (!expl_right || !expl_left || !expl_deploy_body || !expl_deploy_top ||
        !expl_deployed_body)
        return fail("resolve Exploiter FIN commands");
    int right_draw_x = expl_right->x + expl0.dis_x;
    int right_draw_y = expl_right->y - expl0.height;
    int left_draw_x = expl_left->x;
    int left_draw_y = expl_left->y - expl6.height;
    int body_draw_y = expl_deploy_body->y - expl14.height;
    int top_draw_x = expl_deploy_top->x + expl15.dis_x;
    int top_draw_y = expl_deploy_top->y - expl15.height;
    if (right_draw_x != -22 || right_draw_y != -30 ||
        left_draw_x != -30 || left_draw_y != -32 ||
        body_draw_y != -28 || top_draw_x != -10 || top_draw_y != -17) {
        return fail("Exploiter FIN/SPR placement matches the native draw pipeline");
    }
    if (expl_deployed_body->x != -159 || expl_deployed_body->y != 25) {
        return fail("Exploiter deployed body preserves its raw FIN placement");
    }
    const FinCommand *vent = fin_command(&vent_fin, "VENTSTAND0", "vent2", 0, 0);
    if (!vent || vent0.width != 23 || vent0.height != 16 ||
        vent0.dis_x != 31 || vent0.dis_y != 37 ||
        vent->remap != 1 || vent->intensity != 16 || vent->layer != 0 || vent->flags != 0 ||
        vent->x + vent0.dis_x != -9 || -vent->y + vent0.dis_y != 25) {
        return fail("Petra-7 glow preserves its complete FIN/SPR placement metadata");
    }

    static const int expected_cells[] = {
        0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    };
    static const int expected_left[] = {
        -5, -5, -6, -7, -8, -10, -11, -13, -13, -15,
        -14, -15, -15, -17, -17, -18, -17, -18, -17, -18,
    };
    static const int expected_top[] = {
        19, 19, 20, 17, 16, 17, 14, 11, 12, 14,
        15, 15, 15, 15, 16, 17, 18, 24, 26, 28,
    };
    const FinLabel *vent_stand = fin_label(&vent_fin, "VENTSTAND0");
    if (!vent_stand || vent_stand->start != 19 || vent_stand->end != 38)
        return fail("Petra-7 smoke uses the expected VENTSTAND0 frame range");
    for (int i = 0; i < 20; ++i) {
        const FinCommand *puff = fin_frame_command(&vent_fin, vent_stand->start + i, "puff", 5);
        SprFrameInfo puff_frame;
        if (!puff || puff->frame != expected_cells[i] ||
            !load_spr_frame_info("data/DCOLONY/SPRITES/PUFF.SPR", puff->frame, &puff_frame) ||
            puff->x + puff_frame.dis_x != expected_left[i] ||
            -puff->y + puff_frame.dis_y != expected_top[i] ||
            puff->remap != (i < 2 ? 0 : 2) || puff->intensity != 16 ||
            puff->layer != 5 || puff->flags != 0) {
            return fail("Petra-7 smoke uses the remapped VENT.FIN placement");
        }
        int expected_ticks = i < 2 ? 26 : 15;
        int raw_ticks = vent_fin.frames[vent_stand->start + i].ticks;
        if ((raw_ticks == 0 ? 15 : raw_ticks) != expected_ticks)
            return fail("Petra-7 smoke preserves its authored FIN timing");
    }

    const FinLabel *drop_two = fin_label(&drop_fin, "DROPTWO");
    if (!drop_two || drop_two->start != 0 || drop_two->end != 9 ||
        drop_fin.frames[drop_two->start].ticks != 0) {
        return fail("dropship reinforcement uses the native DROPTWO timeline");
    }
    const FinFrame *drop_frame = &drop_fin.frames[drop_two->start];
    const FinCommand *dust = fin_frame_command(&drop_fin, drop_two->start, "duts", 5);
    const FinCommand *hull_top = fin_frame_command(&drop_fin, drop_two->start, "drop", 1);
    const FinCommand *hull_bottom = fin_frame_command(&drop_fin, drop_two->start, "drop", 0);
    const FinCommand *cloud = fin_frame_command(&drop_fin, drop_two->start, "clod", 5);
    const FinCommand *ordered = &drop_fin.commands[drop_frame->command_start];
    if (drop_fin.frames[drop_two->start].part_count != 12 || !dust || !hull_top ||
        !hull_bottom || !cloud || dust->frame != 0 || dust->x != -112 || dust->y != 85 ||
        hull_top->frame != 0 || hull_top->x != -64 || hull_top->y != 69 ||
        hull_bottom->frame != 1 || hull_bottom->x != -64 || hull_bottom->y != 34 ||
        drop_frame->part_count != 12 || strcmp(ordered[0].sprite, "clod") != 0 ||
        strcmp(ordered[1].sprite, "clod") != 0 || strcmp(ordered[2].sprite, "duts") != 0 ||
        strcmp(ordered[3].sprite, "glit") != 0 || strcmp(ordered[4].sprite, "drop") != 0 ||
        ordered[0].layer != 5 || ordered[4].layer != 1 || ordered[5].layer != 0) {
        return fail("dropship frame preserves native hull, fumes, and dust commands");
    }
    return 0;
}

void A_DC_TrooperAttackStart(statecontext_t *ctx, mobj_t *unit) { (void)ctx; (void)unit; }
void A_DC_MuzzleFlash(statecontext_t *ctx, mobj_t *unit) { (void)ctx; (void)unit; }
void A_DC_Attack(statecontext_t *ctx, mobj_t *unit) { (void)ctx; (void)unit; }
void A_DC_Fall(statecontext_t *ctx, mobj_t *unit) { (void)ctx; (void)unit; }
void A_DC_ReaperDeath(statecontext_t *ctx, mobj_t *unit) { (void)ctx; (void)unit; }
void A_DC_Corpse(statecontext_t *ctx, mobj_t *unit) { (void)ctx; (void)unit; }

static int assert_reaper_move_timing(void) {
    static const int expected_tics[] = {4, 3, 3, 4, 1, 3, 3, 1};
    for (int i = 0; i < 8; ++i) {
        if (states[S_DC_REAP_RUN1 + i].tics != expected_tics[i]) {
            fprintf(stderr, "Reaper run state %d has %d tics, expected %d\n",
                    i + 1, states[S_DC_REAP_RUN1 + i].tics, expected_tics[i]);
            return fail("Reaper movement preserves native FIN timing");
        }
    }
    return 0;
}

static int assert_barracks_trooper_release_timing(void) {
    int total_tics = 0;
    int state_id = S_DC_BRRKPOD_BUILD_TRSC1;
    for (int frame = 0; frame < 22; ++frame) {
        const state_t *state = &states[state_id];
        if (state->misc1 != 6 || state->tics < 1 || state->tics > 2)
            return fail("Barracks Trooper release uses native FIN runtime timing");
        total_tics += state->tics;
        state_id = state->nextstate;
    }
    if (total_tics != 35 || state_id != S_DC_BRRKPOD_STND)
        return fail("Barracks Trooper release preserves native time and returns to stand");
    return 0;
}

static int assert_exploiter_16_direction_states(void) {
    FinInfo expl_fin;
    if (!load_fin_info("data/DCOLONY/ANIMATE/EXPL.FIN", "EXPL", &expl_fin))
        return fail("load EXPL.FIN for direction validation");
    if (states[S_DC_EXPL_STND].facings != 16 ||
        states[S_DC_EXPL_RUN1].facings != 16 ||
        states[S_DC_EXPL_RUN2].facings != 16) {
        return fail("Exploiter stand and run states expose all 16 directions");
    }

    for (int code = 0; code < 16; ++code) {
        char stand_label[32];
        char move_label[32];
        int suffix = (16 - code) & 15;
        snprintf(stand_label, sizeof(stand_label), "EXPL%s%d",
                 code & 1 ? "SHUF" : "STAND", suffix);
        snprintf(move_label, sizeof(move_label), "EXPLMOVE%d", suffix);
        const FinCommand *stand = fin_layer_command_at(&expl_fin, stand_label, "expl", 1, 0);
        const FinCommand *move1 = fin_layer_command_at(&expl_fin, move_label, "expl", 1, 0);
        const FinCommand *move2 = fin_layer_command_at(&expl_fin, move_label, "expl", 1, 1);
        if (!stand || !move1 || !move2 ||
            states[S_DC_EXPL_STND].facing_frames[code] != stand->frame ||
            states[S_DC_EXPL_RUN1].facing_frames[code] != move1->frame ||
            states[S_DC_EXPL_RUN2].facing_frames[code] != move2->frame) {
            return fail("Exploiter 16-direction states match EXPL.FIN labels");
        }
    }
    return 0;
}

int main(void) {
    if (assert_dark_colony_city_fin_alignment() != 0) return 1;
    if (assert_reaper_move_timing() != 0) return 1;
    if (assert_barracks_trooper_release_timing() != 0) return 1;
    if (assert_exploiter_16_direction_states() != 0) return 1;
    printf("PASS: Dark Colony SPR/FIN layout alignment is data-consistent\n");
    return 0;
}
