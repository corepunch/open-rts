/* Generate 7th Legion's Doom-style state and mobjinfo tables.
   The initial catalog is intentionally limited to the native BIM actors used by
   the current playable vertical slice. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "info_gen.h"

#define MOBILE(type, sprite, asset, id, hp, speed, damage, extra) \
    { type, sprite, asset, id, hp, speed, 16, 32, 100, damage, \
      "MF_SELECTABLE|MF_MOBILE|MF_RENDERABLE" extra }

static const info_entry_t entries[] = {
    MOBILE("TROOPER", "LTROOP", "GFX/LTROOP.BIM", "1", 100, 4, 15, "|MF_ATTACK"),
    MOBILE("SLAVE", "SLAVEN1", "GFX/SLAVEN1.BIM", "2", 60, 4, 0, "|MF_HARVESTER"),
    MOBILE("SPIDER_MECH", "SPIDER", "GFX/SPIDER.BIM", "3", 300, 3, 35, "|MF_ATTACK"),
    MOBILE("TANK", "TANKBASE", "GFX/TANKBASE.BIM", "4", 500, 5, 50, "|MF_ATTACK"),
    MOBILE("ROCK_MECH", "ROCKMECH", "GFX/ROCKMECH.BIM", "5", 800, 3, 70, "|MF_ATTACK"),
    MOBILE("TRUCK", "TRUCK", "GFX/TRUCK.BIM", "6", 200, 5, 0, "|MF_HARVESTER"),
    MOBILE("MOBILE_BASE", "MOBBASE", "GFX/MOBBASE.BIM", "7", 1000, 3, 0, ""),
};

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: 7legion_info_gen <7legion-root> <info.h> <info.c>\n");
        return 1;
    }
    int count = (int)(sizeof(entries) / sizeof(*entries));
    for (int i = 0; i < count; ++i) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", argv[1], entries[i].asset);
        if (access(path, R_OK) != 0) {
            fprintf(stderr, "7legion_info_gen: missing native asset %s: %s\n", path, strerror(errno));
            return 1;
        }
    }
    if (!write_info_h(argv[2], "the verified 7th Legion BIM actor catalog", entries, count) ||
        !write_info_c(argv[3], "the verified 7th Legion BIM actor catalog", NULL,
                      "SELECTION_STYLE_CIRCLE", false, entries, count)) {
        fprintf(stderr, "7legion_info_gen: cannot write output: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
