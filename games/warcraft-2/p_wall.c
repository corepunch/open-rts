#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <stdlib.h>
#include <string.h>

/* Tile walls and rocks. In Wargus/Stratagus a wall is a map field whose
 * Value is its hit points (CMap::SetWall/HitWall/RemoveWall, map_wall.cpp),
 * the human and orc wall being unit types 103/104 only for their stats:
 * HitPoints 40, Armor 20 (wargus scripts/human/units.lua, orc/units.lua).
 * A PUD marks walls by MTXM tile alone; there are no wall UNIT records.
 *
 * Wall tile families (wargus tilesets/wargus/layout.lua, summer.lua):
 *   human 0x090 (no neighbors), 0x0B0 (all), mixed 0x800..0x8D0 by neighbors
 *   orc   0x0A0,              0x0C0,        mixed 0x900..0x9D0
 * Each group lists its intact tiles, a separator, its broken tiles, a
 * separator, then its destroyed tiles (CTileset::getHumanWallTileIndex_broken
 * and _destroyed skip to the next section). Groups with two variants are
 * 0x840, 0x890, 0x940, 0x990; every other group has one. */

static const uint16_t human_table[16] = {
    0x090, 0x830, 0x810, 0x850, 0x800, 0x840, 0x820, 0x860,
    0x870, 0x8B0, 0x890, 0x8D0, 0x880, 0x8C0, 0x8A0, 0x0B0,
};
static const uint16_t orc_table[16] = {
    0x0A0, 0x930, 0x910, 0x950, 0x900, 0x940, 0x920, 0x960,
    0x970, 0x9B0, 0x990, 0x9D0, 0x980, 0x9C0, 0x9A0, 0x0C0,
};
enum { NO_WALL, HUMAN, ORC };
enum { INTACT, BROKEN, DESTROYED };

static uint8_t *wall_hp;
static int wall_cells;
/* Sum of hp * (cell + 1) over every cell, kept as walls change so the
 * lockstep checksum covers wall damage without a per-tic map scan. */
static uint32_t wall_digest;

static void set_hp(int i, int hp) {
    wall_digest += (uint32_t)(hp - wall_hp[i]) * (uint32_t)(i + 1);
    wall_hp[i] = (uint8_t)hp;
}

static int wall_max_hp(void) { return mobjinfo[MT_HUMAN_WALL].spawnhealth; }

static int variants(uint16_t base) {
    return base == 0x840 || base == 0x890 || base == 0x940 || base == 0x990 ? 2 : 1;
}

/* Race and condition of a wall tile; NO_WALL for separators and non-walls. */
static int classify(uint16_t tile, int *state) {
    uint16_t base = tile & 0xfff0u;
    int sub = tile & 15, race = NO_WALL;
    if (base == 0x090 || base == 0x0B0 || (base >= 0x800 && base <= 0x8D0)) race = HUMAN;
    else if (base == 0x0A0 || base == 0x0C0 || (base >= 0x900 && base <= 0x9D0)) race = ORC;
    if (race == NO_WALL) return NO_WALL;
    int n = variants(base);
    int s;
    if (sub < n) s = INTACT;
    else if (sub >= n + 1 && sub < 2 * n + 1) s = BROKEN;
    else if (sub >= 2 * n + 2 && sub < 3 * n + 2) s = DESTROYED;
    else return NO_WALL;
    if (state) *state = s;
    return race;
}

bool w2_wall_tile(uint16_t tile) {
    int state;
    return classify(tile, &state) != NO_WALL && state != DESTROYED;
}

bool w2_rock_tile(uint16_t tile) {
    return (tile & 0xfff0u) == 0x080 || (tile & 0xff00u) == 0x400 ||
           tile == W2_TILE_ROCK_TOP || tile == W2_TILE_ROCK_MIDDLE || tile == W2_TILE_ROCK_BOTTOM;
}

void w2_walls_init(const level_t *map) {
    free(wall_hp);
    wall_hp = NULL;
    wall_cells = 0;
    wall_digest = 0;
    if (!map || !map->tile_ids || map->width <= 0 || map->height <= 0) return;
    wall_cells = map->width * map->height;
    wall_hp = calloc((size_t)wall_cells, 1);
    if (!wall_hp) { wall_cells = 0; return; }
    for (int i = 0; i < wall_cells; ++i) {
        int state;
        if (classify(map->tile_ids[i], &state) == NO_WALL || state == DESTROYED) continue;
        /* A PUD tile already drawn broken stands at half hit points. */
        set_hp(i, state == BROKEN ? wall_max_hp() / 2 : wall_max_hp());
    }
}

static bool in_walls(ivec2_t cell) {
    return wall_hp && L_Contains(&level, cell.x, cell.y) && level.width * level.height == wall_cells;
}

int W2_WallHp(ivec2_t cell) { return in_walls(cell) ? wall_hp[L_Index(&level, cell.x, cell.y)] : 0; }

bool W2_WallAt(ivec2_t cell) {
    return in_walls(cell) && wall_hp[L_Index(&level, cell.x, cell.y)] > 0 &&
           w2_wall_tile(level.tile_ids[L_Index(&level, cell.x, cell.y)]);
}

/* getWallTile: the tile for a direction mask and condition. */
static uint16_t wall_tile_for(int race, int dir, int hp) {
    uint16_t base = race == HUMAN ? human_table[dir] : orc_table[dir];
    int n = variants(base);
    if (hp <= 0) return (uint16_t)(base + 2 * n + 2);
    if (hp <= wall_max_hp() / 2) return (uint16_t)(base + n + 1);
    return base;
}

/* GetDirectionFromSurrounding: N, E, S, W bits; the map edge counts as wall. */
static int direction(ivec2_t cell, int race) {
    static const ivec2_t offsets[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    int flags = 0;
    for (int i = 0; i < 4; ++i) {
        ivec2_t at = ivec2_add(cell, offsets[i]);
        int state;
        if (!L_Contains(&level, at.x, at.y) ||
            (classify(level.tile_ids[L_Index(&level, at.x, at.y)], &state) == race && state != DESTROYED))
            flags |= 1 << i;
    }
    return flags;
}

/* MapFixWallTile */
static void fix_wall(ivec2_t cell) {
    if (!in_walls(cell)) return;
    int i = L_Index(&level, cell.x, cell.y), state;
    int race = classify(level.tile_ids[i], &state);
    if (race == NO_WALL || state == DESTROYED) return;
    level.tile_ids[i] = wall_tile_for(race, direction(cell, race), wall_hp[i]);
}

static void fix_neighbors(ivec2_t cell) {
    static const ivec2_t offsets[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int i = 0; i < 4; ++i) fix_wall(ivec2_add(cell, offsets[i]));
}

/* CMap::RemoveWall: Value 0 picks the destroyed tile, then the field stops
 * being a wall and its neighbors re-join around the gap. */
static void remove_wall(ivec2_t cell) {
    int i = L_Index(&level, cell.x, cell.y);
    set_hp(i, 0);
    fix_wall(cell);
    if (level.cell_solid) level.cell_solid[i] = 0;
    level.blocked[i] = level.cell_terrain[i] != 0;
    fix_neighbors(cell);
}

void W2_HitWall(ivec2_t cell, int damage) {
    if (!W2_WallAt(cell) || damage < 0) return;
    int i = L_Index(&level, cell.x, cell.y);
    if (wall_hp[i] <= damage) W2_ClearTile(cell);
    else {
        set_hp(i, wall_hp[i] - damage);
        fix_wall(cell);
    }
}

void W2_ClearTile(ivec2_t cell) {
    if (!L_Contains(&level, cell.x, cell.y)) return;
    int terrain = level.cell_terrain[L_Index(&level, cell.x, cell.y)];
    if (W2_WallAt(cell)) remove_wall(cell);
    else if (terrain == W2_TERRAIN_ROCK) w2_remove_rock(cell);
    else if (terrain == 2) w2_remove_tree(cell);
}

/* CalculateDamageStats against the wall's own stats (armor 20). */
int W2_WallDamage(const mobj_t *attacker, int basic, int piercing, uint32_t roll) {
    (void)attacker;
    int damage = basic - mobjinfo[MT_HUMAN_WALL].w2.armor;
    if (damage < 1) damage = 1;
    damage += piercing;
    damage -= (int)(roll % (uint32_t)((damage + 2) / 2));
    return damage < 1 ? 1 : damage;
}

void w2_demolish_terrain(ivec2_t centre, int range) {
    for (int x = centre.x - range - 2; x <= centre.x + range + 2; ++x)
        for (int y = centre.y - range - 2; y <= centre.y + range + 2; ++y) {
            int dx = x - centre.x, dy = y - centre.y;
            if (dx * dx + dy * dy > range * range || !L_Contains(&level, x, y)) continue;
            W2_ClearTile((ivec2_t){x, y});
        }
}

/* 1 for a human wall, 2 for an orc wall, 0 for no wall. */
int W2_WallRace(ivec2_t cell) {
    if (!W2_WallAt(cell)) return 0;
    return classify(level.tile_ids[L_Index(&level, cell.x, cell.y)], NULL);
}

uint32_t W2_WallHash(uint32_t hash) { return G_HashValue(hash, wall_digest); }

/* Saved games carry one hit-point byte per cell. The engine restores only
 * `blocked`, so loading rebuilds each wall cell's tile and solidity. */
size_t W2_WallSaveSize(void) { return (size_t)level.width * level.height; }

void W2_WallSave(uint8_t *out) {
    size_t cells = W2_WallSaveSize();
    if (wall_hp && (size_t)wall_cells == cells) memcpy(out, wall_hp, cells);
    else memset(out, 0, cells);
}

bool W2_WallLoad(const uint8_t *in, size_t size) {
    if (size != W2_WallSaveSize()) return false;
    bool table = wall_hp && (size_t)wall_cells == size;
    for (size_t i = 0; i < size; ++i)
        if (in[i] > wall_max_hp() || (!table && in[i])) return false;
    if (!table) return true;
    /* Cells whose hit points differ take the saved condition; they and
     * their neighbors then re-join, as a hit would have fixed them. Cells
     * the save agrees with keep their tiles. */
    uint8_t *changed = calloc((size_t)wall_cells, 1);
    if (!changed) return false;
    for (int i = 0; i < wall_cells; ++i) {
        int state, race = classify(level.tile_ids[i], &state);
        if (in[i] == wall_hp[i] || race == NO_WALL) continue;
        set_hp(i, in[i]);
        level.tile_ids[i] = wall_tile_for(race, 0, wall_hp[i]);
        if (level.cell_solid) level.cell_solid[i] = wall_hp[i] > 0;
        changed[i] = 1;
    }
    for (int i = 0; i < wall_cells; ++i) {
        if (!changed[i]) continue;
        ivec2_t cell = {i % level.width, i / level.width};
        int state, race = classify(level.tile_ids[i], &state);
        level.tile_ids[i] = wall_tile_for(race, direction(cell, race), wall_hp[i]);
        fix_neighbors(cell);
    }
    free(changed);
    return true;
}
