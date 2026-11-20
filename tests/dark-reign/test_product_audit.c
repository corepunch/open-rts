#include "t_local.h"
#include "engine.h"
#include "info.h"
#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "Dark Reign product audit", #c)

/* Every product's cost, build time and SetPrereqs agree with retail
 * UNITS.TXT and BUILD.TXT (training mirrors 40000..40999 aside). */
typedef struct { int type, cost, seconds, prereqs[8], prereq_count; } retail_t;

static int read_retail(const char *path, retail_t *out, int max, int count) {
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    char line[512];
    retail_t cur = {0};
    bool open = false;
    while (fgets(line, sizeof(line), file)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == ';') continue;
        if (!strncmp(p, "DefineUnitType(", 15) || !strncmp(p, "DefineBuildingType(", 19)) {
            if (open && cur.type && count < max) out[count++] = cur;
            cur = (retail_t){0};
            open = true;
            continue;
        }
        int a, b;
        if (sscanf(p, "SetCost(%d %d)", &a, &b) == 2) { cur.cost = a; cur.seconds = b; }
        if (!cur.type && sscanf(p, "SetType(%d)", &a) == 1) cur.type = a;
        if (!strncmp(p, "SetPrereqs(", 11)) {
            char *q = p + 11;
            while (cur.prereq_count < 8) {
                char *end;
                long v = strtol(q, &end, 10);
                if (end == q) break;
                cur.prereqs[cur.prereq_count++] = (int)v;
                q = end;
            }
        }
    }
    if (open && cur.type && count < max) out[count++] = cur;
    fclose(file);
    return count;
}

int main(void) {
    static retail_t retail[512];
    int n = read_retail("data/REIGN/dark/deftxt/UNITS.TXT", retail, 512, 0);
    CHECK(n > 0);
    n = read_retail("data/REIGN/dark/deftxt/BUILD.TXT", retail, 512, n);
    CHECK(n > 100);
    static StaticProductDefinition products[256];
    int count = G_ModelGetProducts(NULL, 0, products, 256);
    CHECK(count > 60);
    int checked = 0, failures = 0;
    for (int i = 0; i < count; ++i) {
        const StaticProductDefinition *p = &products[i];
        if (!G_ModelActorIdForProduct(p) || !P_ActorType(G_ModelActorIdForProduct(p))) {
            fprintf(stderr, "%s (%d) has no actor\n", p->label, p->product_type);
            ++failures;
        }
        const retail_t *r = NULL;
        for (int k = 0; k < n; ++k)
            if (retail[k].type == p->product_type && !(retail[k].type >= 40000 && retail[k].type < 41000)) {
                r = &retail[k];
                break;
            }
        if (!r) continue;
        bool ok = p->cost == r->cost && G_ModelProductTrainingTimeMs(p) == r->seconds * 1000 &&
                  p->prerequisite_count == r->prereq_count;
        for (int k = 0; ok && k < r->prereq_count; ++k) {
            bool found = false;
            for (int j = 0; j < p->prerequisite_count; ++j) found |= p->prerequisites[j] == r->prereqs[k];
            ok = found;
        }
        if (!ok) {
            fprintf(stderr, "%s (%d): cost %d/%d time %d/%d prereqs %d/%d\n", p->label, p->product_type,
                    p->cost, r->cost, G_ModelProductTrainingTimeMs(p), r->seconds * 1000,
                    p->prerequisite_count, r->prereq_count);
            ++failures;
        }
        ++checked;
    }
    CHECK(failures == 0);
    CHECK(checked > 60);
    printf("PASS: %d products match retail cost, build time and prerequisites\n", checked);
    return 0;
}
