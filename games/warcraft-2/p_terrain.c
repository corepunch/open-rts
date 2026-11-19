#include "w2_local.h"

/* Forest and rock borders after a tile is removed. Stratagus CMap::FixTile
 * serves both with one routine and two lookup tables (woodTable, rockTable);
 * rocks use the mixed group 0x400 and solid 0x80 where forests use 0x700 and
 * 0x70. See WAR2_EXE_FINDINGS.md. Corners: SW=1, SE=2, NE=4, NW=8. */
typedef struct {
    int terrain;            /* cell_terrain value. */
    uint16_t tiles[16];     /* Occupied corners to PUD slot. */
    uint16_t top, middle, bottom, removed;
} kind_t;

static const kind_t forest = {
    2, {
        W2_TILE_REMOVED_TREE, 0x730, 0x770, 0x7b0,
        0x710, 0x750, 0x790, 0x7d0,
        0x700, 0x740, 0x780, 0x7c0,
        0x720, 0x760, 0x7a0, 0x070,
    }, W2_TILE_TREE_TOP, W2_TILE_TREE_MIDDLE, W2_TILE_TREE_BOTTOM, W2_TILE_REMOVED_TREE,
};
static const kind_t rock = {
    W2_TERRAIN_ROCK, {
        W2_TILE_REMOVED_ROCK, 0x430, 0x470, 0x4b0,
        0x410, 0x450, 0x490, 0x4d0,
        0x400, 0x440, 0x480, 0x4c0,
        0x420, 0x460, 0x4a0, 0x080,
    }, W2_TILE_ROCK_TOP, W2_TILE_ROCK_MIDDLE, W2_TILE_ROCK_BOTTOM, W2_TILE_REMOVED_ROCK,
};
enum { SW = 1, SE = 2, NE = 4, NW = 8, TREE_BOTTOM = 16, TREE_TOP = 32 };

static int mask_of(const kind_t *kind, ivec2_t cell) {
    /* The reference treats the map boundary as continuous forest. */
    if (!L_Contains(&level, cell.x, cell.y)) return SW | SE | NE | NW;
    int tile = level.tile_ids[L_Index(&level, cell.x, cell.y)];
    if (tile == kind->top) return SW | SE | TREE_TOP;
    if (tile == kind->middle) return SW | SE | NE | NW | TREE_TOP | TREE_BOTTOM;
    if (tile == kind->bottom) return NE | NW | TREE_BOTTOM;
    for (int mask = 1; mask < 16; ++mask)
        if ((tile & ~15) == kind->tiles[mask]) return mask;
    return 0;
}

static void clear_cell(const kind_t *kind, ivec2_t cell) {
    int i = L_Index(&level, cell.x, cell.y);
    level.tile_ids[i] = kind->removed;
    level.cell_terrain[i] = 0;
    level.blocked[i] = level.cell_solid ? level.cell_solid[i] != 0 : 0;
    if (kind != &forest) return;
    for (int v = 0; v < level.resource_vent_count; ++v) {
        resourcevent_t *vent = &level.resource_vents[v];
        if (vent->resource_type != 1 || !ivec2_equal(vent->cell, cell)) continue;
        vent->active = false;
        vent->amount = 0;
    }
}

static void fix_cell(const kind_t *kind, ivec2_t cell) {
    if (!L_Contains(&level, cell.x, cell.y)) return;
    int i = L_Index(&level, cell.x, cell.y);
    if (level.cell_terrain[i] != kind->terrain) return;
    int north = mask_of(kind, ivec2_add(cell, (ivec2_t){0, -1}));
    int east = mask_of(kind, ivec2_add(cell, (ivec2_t){1, 0}));
    int south = mask_of(kind, ivec2_add(cell, (ivec2_t){0, 1}));
    int west = mask_of(kind, ivec2_add(cell, (ivec2_t){-1, 0}));
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
    if (mask) level.tile_ids[i] = kind->tiles[mask];
    else {
        bool above = (north & (SW | SE)) != 0;
        bool below = (south & (NW | NE)) != 0;
        if (above && below) level.tile_ids[i] = kind->middle;
        else if (above) level.tile_ids[i] = kind->bottom;
        else if (below) level.tile_ids[i] = kind->top;
        else clear_cell(kind, cell);
    }
}

static void remove_cell(const kind_t *kind, ivec2_t cell) {
    if (!L_Contains(&level, cell.x, cell.y) ||
        level.cell_terrain[L_Index(&level, cell.x, cell.y)] != kind->terrain) return;
    clear_cell(kind, cell);
    /* Cardinal borders first; diagonals then see the revised corners.
     * This is Stratagus ClearWoodTile/FixNeighbors traversal order. */
    static const ivec2_t neighbors[] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1},
    };
    for (size_t i = 0; i < sizeof(neighbors) / sizeof(*neighbors); ++i)
        fix_cell(kind, ivec2_add(cell, neighbors[i]));
}

void w2_remove_tree(ivec2_t cell) { remove_cell(&forest, cell); }
/* Stratagus ClearRockTile: the removed-rock megatile, then FixNeighbors. */
void w2_remove_rock(ivec2_t cell) { remove_cell(&rock, cell); }
