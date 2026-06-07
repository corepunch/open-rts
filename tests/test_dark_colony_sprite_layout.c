#include "../plugins/DarkColony/info.h"

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
    char stem_lower[9];
    FinLabel labels[512];
    int label_count;
    FinCommand commands[4096];
    int command_count;
} FinInfo;

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
    int label_count = layout_read_u16_le(data + 4);
    int deps = layout_read_u16_le(data + 6);
    size_t label_off = 8 + (size_t)deps * 8;
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
    unsigned char pattern[8] = {0};
    memcpy(pattern, out->stem_lower, strlen(out->stem_lower));
    size_t command_off = 0;
    bool found = false;
    for (size_t off = label_off + (size_t)label_count * 20; off + 22 <= size; ++off) {
        if ((size - off) % 22 != 0) continue;
        if (memcmp(data + off, pattern, sizeof(pattern)) == 0) {
            command_off = off;
            found = true;
            break;
        }
    }
    if (!found) {
        free(data);
        return false;
    }
    int command_count = (int)((size - command_off) / 22);
    if (command_count > 4096) {
        free(data);
        return false;
    }
    out->command_count = command_count;
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
    for (int i = range->start; i <= range->end && i < fin->command_count; ++i) {
        const FinCommand *cmd = &fin->commands[i];
        if (strcmp(cmd->sprite, sprite) == 0 && cmd->layer == layer &&
            cmd->frame == frame) {
            return cmd;
        }
    }
    return NULL;
}

static int assert_dark_colony_city_fin_alignment(void) {
    FinInfo hubu_fin, towr_fin;
    if (!load_fin_info("data/DCOLONY/ANIMATE/HUBU.FIN", "HUBU", &hubu_fin) ||
        !load_fin_info("data/DCOLONY/ANIMATE/TOWR.FIN", "TOWR", &towr_fin)) {
        return fail("load Dark Colony city FIN files");
    }
    const FinCommand *exco = fin_command(&hubu_fin, "EXCOPODSTAND0", "hubu", 1, 0);
    const FinCommand *barracks = fin_command(&hubu_fin, "TRSCBUILD0", "hubu", 1, 4);
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
    if (barracks->x != tower->x) {
        return fail("FIN data aligns Barracks and Tower on the same raw X coordinate");
    }
    return 0;
}

void A_DC_TrooperAttackStart(StateContext *ctx, Unit *unit) { (void)ctx; (void)unit; }
void A_DC_MuzzleFlash(StateContext *ctx, Unit *unit) { (void)ctx; (void)unit; }
void A_DC_Attack(StateContext *ctx, Unit *unit) { (void)ctx; (void)unit; }
void A_DC_Fall(StateContext *ctx, Unit *unit) { (void)ctx; (void)unit; }
void A_DC_ReaperDeath(StateContext *ctx, Unit *unit) { (void)ctx; (void)unit; }
void A_DC_Corpse(StateContext *ctx, Unit *unit) { (void)ctx; (void)unit; }

int main(void) {
    if (assert_dark_colony_city_fin_alignment() != 0) return 1;
    printf("PASS: Dark Colony SPR/FIN layout alignment is data-consistent\n");
    return 0;
}
