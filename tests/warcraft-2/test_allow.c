#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "Warcraft II ALOW", #c)

static void put32(uint8_t *at, uint32_t v) { for (int i = 0; i < 4; ++i) at[i] = (uint8_t)(v >> (8 * i)); }

/* Sets: units, units forced, upgrades, upgrades forced; 16 players of 32 bits. */
static void everything_allowed(uint8_t *alow) {
    for (int i = 0; i < 4 * 16; ++i) put32(alow + i * 4, i / 16 % 2 ? 0 : 0xFFFFFFFFu);
}

static int test_decode(void) {
    G_InitGame();
    uint8_t alow[256];
    everything_allowed(alow);
    static rulepatchset_t set;
    memset(&set, 0, sizeof(set));
    CHECK(w2_decode_allow(alow, sizeof(alow), &set) == 0);
    /* Player 1 starts without peasants (unit 2) and the first sword; player 2's
     * forced set brings its grunt back. */
    put32(alow + (0 * 16 + 1) * 4, ~(1u << 2));
    put32(alow + (2 * 16 + 1) * 4, ~(1u << 0));
    put32(alow + (0 * 16 + 2) * 4, ~(1u << 1));
    put32(alow + (1 * 16 + 2) * 4, 1u << 1);
    CHECK(w2_decode_allow(alow, sizeof(alow), &set) == 2 && set.count == 2);
    CHECK(w2_decode_allow(alow, sizeof(alow) - 1, &set) == 0 && w2_decode_allow(NULL, 0, &set) == 0);
    R_PatchApply(&set);
    const StaticProductDefinition *peasant = G_ModelProductByUIId(NULL, 3), *sword = G_ModelProductByUIId(NULL, W2_UI_SWORD1);
    const StaticProductDefinition *grunt = G_ModelProductByUIId(NULL, 2);
    CHECK(peasant && sword && grunt);
    CHECK(W2_Banned(1, peasant) && W2_Banned(1, sword) && !W2_Banned(0, peasant) && !W2_Banned(1, grunt));
    CHECK(!W2_Banned(2, grunt)); /* forced allowed */
    CHECK(!G_ModelProductAvailable(NULL, 1, sword));
    uint32_t patched = R_PatchHash(UINT32_C(2166136261));
    R_PatchApply(NULL);
    CHECK(!W2_Banned(1, peasant) && !W2_Banned(1, sword) && R_PatchHash(UINT32_C(2166136261)) != patched);
    return 0;
}

/* The same through a real PUD: ALAMO with an ALOW appended banning player 1's peasant. */
static int test_pud_file(void) {
    blob_t file;
    CHECK(W_ReadFile("data/WAR2/ALAMO.PUD", &file));
    uint8_t *copy = malloc(file.size + 8 + 256);
    CHECK(copy);
    memcpy(copy, file.bytes, file.size);
    memcpy(copy + file.size, "ALOW", 4);
    put32(copy + file.size + 4, 256);
    everything_allowed(copy + file.size + 8);
    put32(copy + file.size + 8 + (0 * 16 + 1) * 4, ~(1u << 2));
    const char *path = "/private/tmp/open-rts-w2-allow.pud";
    FILE *out = fopen(path, "wb");
    CHECK(out && fwrite(copy, 1, file.size + 8 + 256, out) == file.size + 8 + 256 && fclose(out) == 0);
    free(copy);
    W_FreeFile(&file);

    P_InitThinkers();
    G_InitGame();
    CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level));
    const StaticProductDefinition *peasant = G_ModelProductByUIId(NULL, 3);
    P_LoadThings(NULL);
    CHECK(G_ModelProductAvailable(NULL, 1, peasant)); /* human town hall stands */
    CHECK(G_DoLoadLevel(path, &level) && g_rulepatch.count == 1);
    P_InitThinkers();
    P_LoadThings(NULL);
    CHECK(W2_Banned(1, peasant) && !G_ModelProductAvailable(NULL, 1, peasant));
    /* Loading a stock map afterwards lifts it. */
    CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level) && g_rulepatch.count == 0 && !W2_Banned(1, peasant));
    remove(path);
    return 0;
}

int main(void) {
    RTS_RUN(test_decode());
    RTS_RUN(test_pud_file());
    puts("PASS: PUD ALOW bans units and research per player, hashes into the patch and lifts on the next load");
    return 0;
}
