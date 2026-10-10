#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "Warcraft II ruleset patch", #c)

static void put16(uint8_t *at, unsigned v) { at[0] = (uint8_t)v; at[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *at, uint32_t v) { put16(at, v & 0xffff); put16(at + 2, v >> 16); }

/* A UDTA / UGRD that overrides the footman, the first Sword upgrade and the
 * ranger training. Every other unit is authored as zero. */
static void synthetic(uint8_t *udta, uint8_t *ugrd) {
    memset(udta, 0, W2_UDTA_SIZE);
    memset(ugrd, 0, W2_UGRD_SIZE);
    put32(udta + 1238, 6);             /* footman sight */
    put16(udta + 1678, 77);            /* hit points */
    udta[2008] = 99;                   /* build time */
    udta[2118] = 70;                   /* gold, in tenths */
    udta[2228] = 5;                    /* lumber */
    udta[3328] = 2;                    /* attack range */
    udta[3658] = 4;                    /* armor */
    udta[3988] = 8;                    /* basic damage */
    udta[4098] = 5;                    /* piercing damage */
    ugrd[2] = 123;                     /* sword 1 time */
    put16(ugrd + 54, 900);             /* gold */
    put16(ugrd + 158, 40);             /* lumber */
}

static int test_decode_and_apply(void) {
    G_InitGame();
    static mobjinfo_t before[NUMMOBJTYPES];
    memcpy(before, mobjinfo, sizeof(before));
    w2_upgrade_t sword = *W2_Upgrade(W2_UPGRADE_SWORD1);
    CHECK(mobjinfo[MT_FOOTMAN].spawnhealth == 60 && actor_types[MT_FOOTMAN - 1].max_hp == 60);

    uint8_t udta[W2_UDTA_SIZE], ugrd[W2_UGRD_SIZE];
    synthetic(udta, ugrd);
    static rulepatchset_t set;
    memset(&set, 0, sizeof(set));
    /* "Use default data" adds nothing, and short sections are ignored. */
    put16(udta, 1);
    put16(ugrd, 1);
    CHECK(w2_decode_rules(udta, sizeof(udta), ugrd, sizeof(ugrd), &set) == 0);
    put16(udta, 0);
    put16(ugrd, 0);
    CHECK(w2_decode_rules(udta, W2_UDTA_SIZE - 1, ugrd, W2_UGRD_SIZE - 1, &set) == 0);
    CHECK(w2_decode_rules(NULL, 0, NULL, 0, &set) == 0);
    int added = w2_decode_rules(udta, sizeof(udta), ugrd, sizeof(ugrd), &set);
    CHECK(added == 110 * 11 + 48 * 4 && set.count == added);

    uint32_t clean = R_PatchHash(UINT32_C(2166136261)), vector[CONSISTENCY_COUNT], patched_vector[CONSISTENCY_COUNT];
    G_ConsistencyVector(vector);
    R_PatchApply(&set);
    CHECK(R_PatchHash(UINT32_C(2166136261)) != clean);
    /* Only the game slot of the lockstep checksum sees it. */
    G_ConsistencyVector(patched_vector);
    CHECK(patched_vector[CONSISTENCY_GAME] != vector[CONSISTENCY_GAME]);
    CHECK(patched_vector[CONSISTENCY_THINKERS] == vector[CONSISTENCY_THINKERS]);
    const mobjinfo_t *footman = &mobjinfo[MT_FOOTMAN];
    CHECK(footman->spawnhealth == 77 && footman->w2.sight == 6 && footman->w2.costs.time == 99);
    CHECK(footman->w2.costs.resources[0] == 700 && footman->w2.costs.resources[1] == 50);
    CHECK(footman->w2.attack_range == 2 && footman->w2.armor == 4);
    /* The ruleset prices the footman from the patched table. */
    product_t priced;
    CHECK(R_ProductByUiId(1, &priced) && priced.cost[0] == 700 && priced.cost[1] == 50);
    CHECK(footman->w2.basic_damage == 8 && footman->w2.piercing_damage == 5);
    CHECK(footman->damage == 9 + (8 - 6) + (5 - 3));
    /* The engine's actor type sees the new hit points and damage. */
    CHECK(actor_types[MT_FOOTMAN - 1].max_hp == 77 && actor_types[MT_FOOTMAN - 1].attack.damage == footman->damage);
    const w2_upgrade_t *patched = W2_Upgrade(W2_UPGRADE_SWORD1);
    CHECK(patched->time == 123 && patched->gold == 900 && patched->lumber == 40);
    CHECK(patched->icon == sword.icon && patched->tier == sword.tier);
    /* Units the map zeroed are zero. */
    CHECK(mobjinfo[MT_GRUNT].spawnhealth == 0);

    /* The checked-in table is the authority: the next level restores it all. */
    R_PatchApply(NULL);
    CHECK(R_PatchHash(UINT32_C(2166136261)) == clean);
    CHECK(!memcmp(mobjinfo, before, sizeof(before)));
    CHECK(!memcmp(W2_Upgrade(W2_UPGRADE_SWORD1), &sword, sizeof(sword)));
    CHECK(actor_types[MT_FOOTMAN - 1].max_hp == 60);
    return 0;
}

/* The same through a real PUD: ALAMO with its UDTA switched to custom data. */
static int test_pud_file(void) {
    blob_t file;
    CHECK(W_ReadFile("data/WAR2/ALAMO.PUD", &file));
    uint8_t *copy = malloc(file.size);
    CHECK(copy);
    memcpy(copy, file.bytes, file.size);
    size_t udta_at = 0;
    for (size_t at = 0; at + 8 <= file.size;) {
        if (!memcmp(copy + at, "UDTA", 4)) { udta_at = at + 8; break; }
        at += 8 + read_u32_le(copy + at + 4);
    }
    CHECK(udta_at);
    put16(copy + udta_at, 0);                 /* custom data, as authored: */
    put16(copy + udta_at + 1678 + 0, 88);     /* footman hit points */
    const char *path = "/private/tmp/open-rts-w2-patched.pud";
    FILE *out = fopen(path, "wb");
    CHECK(out && fwrite(copy, 1, file.size, out) == file.size && fclose(out) == 0);
    free(copy);
    W_FreeFile(&file);

    P_InitThinkers();
    G_InitGame();
    CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level) && g_rulepatch.count == 0);
    int stock = mobjinfo[MT_FOOTMAN].spawnhealth;
    uint32_t clean = R_PatchHash(UINT32_C(2166136261));
    CHECK(G_DoLoadLevel(path, &level) && g_rulepatch.count > 0);
    CHECK(mobjinfo[MT_FOOTMAN].spawnhealth == 88 && R_PatchHash(UINT32_C(2166136261)) != clean);
    /* Loading a stock map afterwards restores the stock stats and hash. */
    CHECK(G_DoLoadLevel("data/WAR2/ALAMO.PUD", &level) && g_rulepatch.count == 0);
    CHECK(mobjinfo[MT_FOOTMAN].spawnhealth == stock && R_PatchHash(UINT32_C(2166136261)) == clean);
    remove(path);
    return 0;
}

int main(void) {
    RTS_RUN(test_decode_and_apply());
    RTS_RUN(test_pud_file());
    puts("PASS: PUD UDTA and UGRD overlay the checked-in tables, hash into the game slot and revert on the next load");
    return 0;
}
