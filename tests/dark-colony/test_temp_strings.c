#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

int main(void) {
    const char *path = M_va("%s/%s.%s", "data/SCENARIO", "DESERT", "BTS");
    for (int i = 0; i < 7; ++i) CHECK(strcmp(M_va("%d", i), "") != 0);
    CHECK(strcmp(path, "data/SCENARIO/DESERT.BTS") == 0);
    CHECK(strcmp(M_va("%s/%s", M_va("%s-%d", "ROOT", 2),
                      M_Upper(M_va("%s", "trooper.spr"))), "ROOT-2/TROOPER.SPR") == 0);
    CHECK(strcmp(M_FileName("ROOT/SPRITES/TRSC.SPR"), "TRSC.SPR") == 0);
    CHECK(strcmp(M_FileName("TRSC.SPR"), "TRSC.SPR") == 0);
    CHECK(strlen(M_va("%04095d", 1)) == 4095);
    CHECK(M_va("%04096d", 1) == NULL);
    CHECK(strcmp(M_va("%s", "after overflow"), "after overflow") == 0);
    puts("PASS: temporary strings, nesting, lifetime, and overflow");
    return 0;
}
