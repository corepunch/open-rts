/* Unit production time is the produced type's native release animation, read
 * here independently from the FIN files listed in ANIM.DAT (DC.EXE loader
 * 0x438c95: <sprite>BUILDSTAND<even facing>, else <sprite>BUILD<even facing>;
 * delay 0x42354b..0x42358d; 66 ms native tick at 0x41a728). */
#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include "gamestat.h"
#include "t_local.h"
#include <stdio.h>
#include <string.h>

#define REQUIRE(cond, msg) do { if (!(cond)) return rts_fail("release_times", msg); } while (0)

static char fins[256][16];
static int fin_count;

static int frame_ticks(int raw) { return ((raw ? raw : 15) + 3) * 15 / 100; }

/* Native ticks from mode-1 start until the release channel goes inactive. */
static int release_ticks(const char *sprite) {
    for (int pass = 0; pass < 2; ++pass)
        for (int facing = 0; facing <= 30; facing += 2) {
            char label[32];
            snprintf(label, sizeof(label), "%s%s%d", sprite, pass ? "BUILD" : "BUILDSTAND", facing);
            for (int i = 0; i < fin_count; ++i) {
                dc_fin_t fin;
                char path[256];
                snprintf(path, sizeof(path), "data/DCOLONY/ANIMATE/%s", fins[i]);
                if (!DC_LoadFIN(path, &fin)) continue;
                const dc_fin_label_t *l = DC_FINLabel(&fin, label);
                int ticks = -1;
                if (l) {
                    ticks = 1;
                    for (int k = SDL_SwapLE16(l->start) + 1; k <= SDL_SwapLE16(l->end); ++k)
                        ticks += frame_ticks(SDL_SwapLE16(fin.frames[k].ticks));
                }
                DC_FreeFIN(&fin);
                if (ticks > 0) return ticks;
            }
        }
    return -1;
}

int main(void) {
    FILE *list = fopen("data/DCOLONY/ANIM.DAT", "r");
    REQUIRE(list, "ANIM.DAT opens");
    char line[64];
    while (fin_count < 256 && fgets(line, sizeof(line), list)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0]) snprintf(fins[fin_count++], sizeof(fins[0]), "%s", M_Upper(line));
    }
    fclose(list);

    REQUIRE(release_ticks("TRSC") == 22, "Trooper release is the documented 22 native ticks (44 engine tics)");
    int checked = 0;
    bool seen[GAMESTAT_UNIT_COUNT] = {false};
    for (int ui = 0; ui < 256; ++ui) {
        const StaticProductDefinition *product = G_ModelProductByUIId(NULL, ui);
        if (!product || product->product_class != RTS_PRODUCT_UNIT || seen[product->product_type]) continue;
        seen[product->product_type] = true;
        const char *sprite = dc_gamestat_units[product->product_type].sprite;
        int ticks = release_ticks(sprite);
        int ms = G_ModelProductTrainingTimeMs(product);
        if (product->product_type == 0) {
            REQUIRE(ms == 1, "the Barracks release state alone delays the Trooper");
        } else if (ticks != -1 && ms != ticks * 66) {
            fprintf(stderr, "%s (%s): %d ms, FIN release %d ticks = %d ms\n",
                    product->label, sprite, ms, ticks, ticks * 66);
            return rts_fail("release_times", "production time must equal the FIN release");
        }
        REQUIRE(ticks > 0, "every purchasable unit has a native release sequence");
        REQUIRE(ms < 3000, "no product waits longer than its release animation");
        ++checked;
    }
    REQUIRE(checked >= 18, "all human and alien unit products are checked");
    const StaticProductDefinition *exploiter = G_ModelProductByUIId(NULL, 87);
    REQUIRE(exploiter && G_ModelProductTrainingTimeMs(exploiter) == 9 * 66,
            "the Exploiter leaves the Exo-Ctr after its five-frame, 594 ms release");
    printf("PASS: %d unit production times equal their native FIN release animations\n", checked);
    return 0;
}
