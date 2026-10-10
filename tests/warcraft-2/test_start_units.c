#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "Warcraft II start units", #c)

static int owned(int player, uint16_t type) {
    int count = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && ((mobj_t *)th)->owner == player && ((mobj_t *)th)->type_id == type) ++count;
    return count;
}

/* ALAMO with the units of player 3 (an orc computer) cut from its UNIT
 * section and its start location left: the slot is a melee start and its
 * side's faction says what stands there. */
int main(void) {
    blob_t file;
    CHECK(W_ReadFile("data/WAR2/ALAMO.PUD", &file));
    uint8_t *copy = malloc(file.size);
    CHECK(copy);
    size_t out = 0;
    for (size_t at = 0; at + 8 <= file.size;) {
        uint32_t length = read_u32_le(file.bytes + at + 4);
        if (memcmp(file.bytes + at, "UNIT", 4)) {
            memcpy(copy + out, file.bytes + at, 8 + length);
            out += 8 + length;
        } else {
            memcpy(copy + out, file.bytes + at, 8);
            size_t head = out;
            out += 8;
            for (uint32_t r = 0; r + 8 <= length; r += 8) {
                const uint8_t *rec = file.bytes + at + 8 + r;
                bool start = rec[4] == 94 || rec[4] == 95;
                if (!start && rec[5] == 3) continue;
                memcpy(copy + out, rec, 8);
                out += 8;
            }
            uint32_t kept = (uint32_t)(out - head - 8);
            copy[head + 4] = (uint8_t)kept; copy[head + 5] = (uint8_t)(kept >> 8);
            copy[head + 6] = (uint8_t)(kept >> 16); copy[head + 7] = (uint8_t)(kept >> 24);
        }
        at += 8 + length;
    }
    const char *path = "/private/tmp/open-rts-w2-start.pud";
    FILE *f = fopen(path, "wb");
    CHECK(f && fwrite(copy, 1, out, f) == out && fclose(f) == 0);
    free(copy);
    W_FreeFile(&file);

    G_InitGame();
    P_InitThinkers();
    CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level) && P_LoadThings(NULL) > 0);
    CHECK(owned(3, MT_GREAT_HALL) == 1 && owned(3, MT_PEON) == 2);

    P_InitThinkers();
    CHECK(G_DoLoadLevel(path, &level) && P_LoadThings(NULL) > 0);
    /* The orc computer: one great hall and one peon. */
    CHECK(owned(3, MT_GREAT_HALL) == 1 && owned(3, MT_PEON) == 1);
    /* Slots that kept their units are untouched. */
    CHECK(owned(2, MT_GREAT_HALL) == 1 && owned(1, MT_TOWN_HALL) == 1);
    remove(path);
    puts("PASS: a Warcraft II start location without units gets its faction's start units");
    return 0;
}
