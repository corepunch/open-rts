#include "t_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II sight", #c)

static int seen(int x, int y) {
    uint32_t bits = level.sight.cells[L_Index(&level, x, y)];
    return (bits & level.sight.allies[consoleplayer]) != 0;
}

static int hidden(int x, int y) {
    return !L_Contains(&level, x, y) || !seen(x, y);
}

/* Peon sight is 4. The ray table's axis cells (0, ±4) and (±4, 0) have no
 * revealed neighbor on three sides, so the shroud mask is empty and the
 * cell draws as a lit square. The circle below is the one Stratagus uses. */
static int test_circle(void) {
    P_FreeLevel(&level);
    level.width = level.height = 32;
    CHECK(P_InitSight());
    P_RevealSight((ivec2_t){16, 16}, 4, level.sight.allies[consoleplayer], false);

    CHECK(seen(16, 16));
    CHECK(seen(20, 16));
    CHECK(seen(16, 12));
    CHECK(seen(20, 17));
    CHECK(seen(20, 18));
    CHECK(seen(18, 12));
    CHECK(seen(12, 14));
    CHECK(seen(19, 19));

    CHECK(!seen(21, 16));
    CHECK(!seen(16, 11));
    CHECK(!seen(20, 19));
    CHECK(!seen(19, 20));

    int count = 0;
    int isolated = 0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            if (!seen(x, y)) continue;
            ++count;
            if (hidden(x - 1, y - 1) && hidden(x, y - 1) && hidden(x + 1, y - 1) &&
                hidden(x - 1, y) && hidden(x + 1, y) &&
                hidden(x - 1, y + 1) && hidden(x, y + 1) && hidden(x + 1, y + 1))
                ++isolated;
        }
    }
    CHECK(count == 69);
    CHECK(isolated == 0);

    P_FreeLevel(&level);
    level.width = level.height = 8;
    CHECK(P_InitSight());
    P_RevealSight((ivec2_t){0, 0}, 2, level.sight.allies[consoleplayer], false);
    CHECK(seen(0, 0));
    CHECK(seen(2, 2));
    CHECK(!seen(3, 0));
    P_FreeLevel(&level);
    return 0;
}

int main(void) {
    G_InitGame();
    RTS_RUN(test_circle());
    puts("PASS: Warcraft II sight circle");
    return 0;
}
