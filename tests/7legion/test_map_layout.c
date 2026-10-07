#include "t_local.h"

int main(void) {
    level_t map = {0};
    RTS_CHECK(G_DoLoadLevel("data/7LEGION/DATA/MAPT.000", &map),
              "7legion terrain", "load native map layers");
    RTS_CHECK(map.width == 128 && map.height == 128 && map.tile_overlay_count == 1,
              "7legion terrain", "native layer dimensions");

    RTS_CHECK(map.tile_ids[84 * 128 + 74] == 982 &&
              map.tile_overlays[0][10 * 128 + 42] == 1 &&
              map.blocked[84 * 128 + 74] == 0,
              "7legion terrain", "layers retain native column-major storage");

    /* Off-diagonal retail landmarks distinguish x*128+y from y*128+x.
       Values were checked against the executable decoder and raw file bytes;
       see docs/7LEGION_EXE_FINDINGS.md, terrain coordinate audit. */
    static const struct {
        ivec2_t cell;
        uint16_t tile, overlay;
        bool passable;
    } landmarks[] = {
        {{84, 74}, 982, 0, true},
        {{74, 84}, 319, 0, false},
        {{80, 70}, 945, 0, true},
        {{70, 80}, 693, 0, false},
        {{10, 42}, 314, 1, false},
        {{22, 19}, 966, 1, false},
        {{26, 28}, 150, 1, false},
    };
    for (size_t i = 0; i < sizeof(landmarks) / sizeof(landmarks[0]); ++i) {
        ivec2_t cell = landmarks[i].cell;
        int index = L_Index(&map, cell.x, cell.y);
        if (map.tile_ids[index] != landmarks[i].tile ||
            map.tile_overlays[0][index] != landmarks[i].overlay ||
            L_IsWalkable(&map, cell.x, cell.y) != landmarks[i].passable) {
            fprintf(stderr, "FAIL: 7legion terrain (%d,%d): tile %u/%u, "
                    "overlay %u/%u, passable %d/%d\n", cell.x, cell.y,
                    map.tile_ids[index], landmarks[i].tile,
                    map.tile_overlays[0][index], landmarks[i].overlay,
                    L_IsWalkable(&map, cell.x, cell.y), landmarks[i].passable);
            P_FreeLevel(&map);
            return 1;
        }
    }
    P_FreeLevel(&map);

    /* A rectangular level catches accidental width/height stride swaps;
       sight must reach the world cell, and projection/input stay upright. */
    level = (level_t){.width = 19, .height = 11};
    consoleplayer = 0;
    RTS_CHECK(P_InitSight(), "7legion terrain", "allocate sight");
    P_RevealSight((ivec2_t){14, 3}, 2, UINT32_C(0x40000000), false);
    RTS_CHECK(P_SightBrightness(&level, (ivec2_t){15, 3}) == 16 &&
              P_SightBrightness(&level, (ivec2_t){14, 4}) == 16 &&
              P_SightBrightness(&level, (ivec2_t){3, 9}) == 0,
              "7legion terrain", "sight uses native cell addresses");
    app_t app = {.cell = {32, 32}, .cam = {17, -9}};
    float sx, sy;
    R_MapToScreen(&app, &level, 14.5f, 3.5f, &sx, &sy);
    RTS_CHECK(sx == 481 && sy == 103 &&
              ivec2_equal(R_ScreenToMapGrid(&app, &level, (int)sx, (int)sy),
                          (ivec2_t){14, 3}),
              "7legion terrain", "native world projects upright and picks the same cell");
    P_FreeLevel(&level);
    puts("PASS: 7legion terrain, overlays and passability match seven retail landmarks");
    return 0;
}
