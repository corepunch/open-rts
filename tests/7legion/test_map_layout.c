#include "t_local.h"

int main(void) {
    level_t map = {0};
    RTS_CHECK(G_DoLoadLevel("data/7LEGION/DATA/MAPT.000", &map),
              "7legion terrain", "load native map layers");
    RTS_CHECK(map.width == 128 && map.height == 128 && map.tile_overlay_count == 1,
              "7legion terrain", "native layer dimensions");

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
    puts("PASS: 7legion terrain, overlays and passability match seven retail landmarks");
    return 0;
}
