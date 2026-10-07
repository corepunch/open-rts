#include "w2_local.h"

/* PUD forest slots indexed by occupied corners: SW=1, SE=2, NE=4, NW=8.
 * Wargus tilesets + Stratagus resource borders; see WAR2_EXE_FINDINGS.md. */
static const uint16_t forest_tiles[16] = {
    W2_TILE_REMOVED_TREE, 0x730, 0x770, 0x7b0,
    0x710, 0x750, 0x790, 0x7d0,
    0x700, 0x740, 0x780, 0x7c0,
    0x720, 0x760, 0x7a0, 0x070,
};
enum { SW = 1, SE = 2, NE = 4, NW = 8, TREE_BOTTOM = 16, TREE_TOP = 32 };

static int forest_mask(ivec2_t cell) {
    /* The reference treats the map boundary as continuous forest. */
    if (!L_Contains(&level, cell.x, cell.y)) return SW | SE | NE | NW;
    int tile = level.tile_ids[L_Index(&level, cell.x, cell.y)];
    switch (tile) {
    case W2_TILE_TREE_TOP: return SW | SE | TREE_TOP;
    case W2_TILE_TREE_MIDDLE: return SW | SE | NE | NW | TREE_TOP | TREE_BOTTOM;
    case W2_TILE_TREE_BOTTOM: return NE | NW | TREE_BOTTOM;
    }
    for (int mask = 1; mask < 16; ++mask)
        if ((tile & ~15) == forest_tiles[mask]) return mask;
    return 0;
}

static void clear_tree(ivec2_t cell) {
    int i = L_Index(&level, cell.x, cell.y);
    level.tile_ids[i] = W2_TILE_REMOVED_TREE;
    level.cell_terrain[i] = 0;
    level.blocked[i] = level.cell_solid ? level.cell_solid[i] != 0 : 0;
    for (int v = 0; v < level.resource_vent_count; ++v) {
        resourcevent_t *vent = &level.resource_vents[v];
        if (vent->resource_type != 1 || !ivec2_equal(vent->cell, cell)) continue;
        vent->active = false;
        vent->amount = 0;
    }
}

static void fix_tree(ivec2_t cell) {
    if (!L_Contains(&level, cell.x, cell.y)) return;
    int i = L_Index(&level, cell.x, cell.y);
    if (level.cell_terrain[i] != 2) return;
    int north = forest_mask(ivec2_add(cell, (ivec2_t){0, -1}));
    int east = forest_mask(ivec2_add(cell, (ivec2_t){1, 0}));
    int south = forest_mask(ivec2_add(cell, (ivec2_t){0, 1}));
    int west = forest_mask(ivec2_add(cell, (ivec2_t){-1, 0}));
    int mask = 0;
    /* A corner joins the two touching corners of its cardinal neighbors. */
    if ((north & SW) && (west & NE)) mask |= NW;
    if ((north & SE) && (east & NW)) mask |= NE;
    if ((south & NE) && (east & SW)) mask |= SE;
    if ((south & NW) && (west & SE)) mask |= SW;
    if (south & TREE_BOTTOM) {
        if (west & (SE | NE)) mask |= SW;
        if (east & (SW | NW)) mask |= SE;
    }
    if (north & TREE_TOP) {
        if (west & (SE | NE)) mask |= NW;
        if (east & (SW | NW)) mask |= NE;
    }
    if (mask) level.tile_ids[i] = forest_tiles[mask];
    else {
        bool above = (north & (SW | SE)) != 0;
        bool below = (south & (NW | NE)) != 0;
        if (above && below) level.tile_ids[i] = W2_TILE_TREE_MIDDLE;
        else if (above) level.tile_ids[i] = W2_TILE_TREE_BOTTOM;
        else if (below) level.tile_ids[i] = W2_TILE_TREE_TOP;
        else clear_tree(cell);
    }
}

void w2_remove_tree(ivec2_t cell) {
    if (!L_Contains(&level, cell.x, cell.y) ||
        level.cell_terrain[L_Index(&level, cell.x, cell.y)] != 2) return;
    clear_tree(cell);
    /* Cardinal borders first; diagonals then see the revised corners.
     * This is Stratagus ClearWoodTile/FixNeighbors traversal order. */
    static const ivec2_t neighbors[] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1},
    };
    for (size_t i = 0; i < sizeof(neighbors) / sizeof(*neighbors); ++i)
        fix_tree(ivec2_add(cell, neighbors[i]));
}
