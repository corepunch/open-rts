#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"

#include <stdio.h>

static int fail(const char *msg) { return rts_fail("z panel", msg); }

static bool sized(const spritesheet_t *sheet, int piece, int w, int h) {
    return piece < sheet->numlumps && sheet->lumps[piece].indices &&
           sheet->cells[piece].rect.w == w && sheet->cells[piece].rect.h == h;
}

/* SHEADFX.PAC's picture lists decode to the pieces the panel is built
 * from, at the sizes ZED.EXE's 640x480 screen needs. */
static int test_pieces(void) {
    zpanel_t panel;
    if (!Z_LoadPanel("data/Z", &panel)) return fail("load SHEADFX.PAC");
    if (panel.pieces.numlumps != 129) return fail("NSNEWPAN.DAT has 129 pictures");
    if (!sized(&panel.pieces, 0, 96, 480)) return fail("the panel is 96x480");
    if (!sized(&panel.pieces, 111, 544, 32) || !sized(&panel.pieces, 66, 544, 32) || !sized(&panel.pieces, 110, 544, 32))
        return fail("the bottom bars are 544x32");
    if (!sized(&panel.pieces, 50, 12, 18) || !sized(&panel.pieces, 59, 12, 18)) return fail("digits are 12x18");
    if (!sized(&panel.pieces, 13, 24, 20) || !sized(&panel.pieces, 34, 56, 20)) return fail("buttons are 24x20, Menu 56x20");
    if (!sized(&panel.pieces, 79, 96, 14) || !sized(&panel.pieces, 97, 96, 14)) return fail("name labels are 96x14");
    if (!sized(&panel.pieces, 60, 74, 10)) return fail("the health bar is 74x10");
    if (!sized(&panel.pieces, 69, 80, 6) || !sized(&panel.pieces, 73, 54, 6) ||
        !sized(&panel.pieces, 78, 40, 6)) return fail("authored empty share bars for two, three and four sides");
    if (!sized(&panel.window, Z_UI_WINDOW, 112, 80) ||
        !sized(&panel.window, Z_UI_WINDOW + 8, 40, 14) ||
        !sized(&panel.window, Z_UI_WINDOW + 10, 40, 15) ||
        !sized(&panel.window, Z_UI_WINDOW + 12, 16, 8) ||
        !sized(&panel.window, Z_UI_WINDOW + 16, 16, 50) ||
        !sized(&panel.window, Z_UI_LABEL + 5, 48, 8) ||
        !sized(&panel.window, Z_UI_BACKGROUND, 48, 37))
        return fail("native window, Cancel/OK, arrows, meter, label and preview sizes");
    /* The sidebar's black is the palette's 80, so the piece writes its own
     * black; its slots sit where the pieces go. */
    const uint8_t *side = panel.pieces.lumps[0].indices;
    if (side[80 * 96 + 48] != 80 || side[210 * 96 + 48] != 80 || side[340 * 96 + 48] != 80)
        return fail("the portrait, health and radar slots are black");
    if (panel.backs.numlumps != 6 || !sized(&panel.backs, 0, 96, 80)) return fail("six 96x80 face backgrounds");
    for (int i = 0; i < Z_FACES; ++i)
        if (panel.heads[i].numlumps < 3 || panel.heads[i].cells[2].rect.w != 80 ||
            panel.heads[i].lumps[2].indices[0] != 0)
            return fail("each face is 80 wide and shows the background at its corner");
    Z_FreePanel(&panel);
    puts("PASS: z panel art decodes from SHEADFX.PAC");
    return 0;
}

int main(void) {
    return test_pieces() ? 1 : 0;
}
