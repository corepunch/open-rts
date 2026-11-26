#define _DEFAULT_SOURCE
#include "engine.h"
#include "rustfront.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── terrain ───────────────────────────────────────────────────────────────
 * assets/terrain.txt (written by art/pack8.py from art/terrain.txt) names every ground type:
 * its map letter, its layer, its tiles in assets/terrain.pcx, and whether it blocks, holds
 * ore or is a solid model tile. A cell shows one of its terrain's variants, picked by a hash
 * of its position, or for a terrain painted as a sheet of period x period cells, its slice of
 * the sheet (x mod period, y mod period); and then the transitions: every higher-layer terrain among its eight
 * neighbours spills over its edges, lowest layer first, as an edge tile (the sides that
 * terrain touches: N 1, E 2, S 4, W 8) and a corner tile (the diagonals it touches alone:
 * NE 1, SE 2, SW 4, NW 8). A model terrain with a blend= ground spills as that ground.
 *
 * Relief (art/relief.py). The map gives each cell a level; a grid corner takes the lowest
 * level of the cells around it, and a cell whose corners differ is a cliff (or a ramp where
 * the map marks one) whose shape is its high corners. Such a cell shows the low ground's tile,
 * its own terrain through the shape's top mask, and the rock: piece k of a cliff hangs over
 * the cell k rows south of it, drawn last so the nearer rock covers the farther. Spills only
 * pass between cells on one level. */

#define RF_MAX_TERRAINS 32

typedef struct {
    char name[16];
    char letter;
    int layer, base, variants, period, edges, corners, blend, tops;
    bool blocked, ore, solid;
} rfterrain_t;

static rfterrain_t terrains[RF_MAX_TERRAINS];
static int terrain_count, default_terrain = -1;
static int by_layer[RF_MAX_TERRAINS]; /* terrains that spill, lowest layer first */
static int spill_count;

/* relief tiles: pieces for the cliff cell and the two cells south of it, -1 where empty */
#define RF_MAX_RELIEF_VARIANTS 8
#define RF_CLIFF_SHAPES 14
enum { RF_FLAT, RF_CLIFF, RF_RAMP };
static int relief_variants, relief_height;
static int16_t cliff_pieces[16][RF_MAX_RELIEF_VARIANTS][3];
static int16_t ramp_pieces[4][4][3];
static const uint8_t ramp_shapes[4] = {9, 3, 6, 12}; /* high side N, E, S, W */

typedef struct {
    uint8_t level, low_level, kind, shape, variant, dir, sides, ramp;
    uint8_t low; /* the low ground's terrain under a cliff or ramp */
} rfcell_t;
static rfcell_t *relief;
static int relief_w, relief_h;

static int find_name(const char *name) {
    for (int i = 0; i < terrain_count; ++i)
        if (!strcmp(terrains[i].name, name)) return i;
    return -1;
}

static bool parse(char *text, const char *path) {
    char blends[RF_MAX_TERRAINS][16] = {{0}};
    char fallback[16] = "";
    terrain_count = spill_count = 0;
    default_terrain = -1;
    relief_variants = relief_height = 0;
    memset(cliff_pieces, 0xff, sizeof(cliff_pieces));
    memset(ramp_pieces, 0xff, sizeof(ramp_pieces));
    int n = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        n++;
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r') line[--len] = '\0';
        if (!len || line[0] == '#') continue; /* whole-line comments: '#' is also a map letter */
        if (sscanf(line, "default %15s", fallback) == 1) continue;
        int a, b, k0, k1, k2;
        if (sscanf(line, "relief height=%d variants=%d", &a, &b) == 2) {
            relief_height = a;
            relief_variants = b < 1 ? 1 : b > RF_MAX_RELIEF_VARIANTS ? RF_MAX_RELIEF_VARIANTS : b;
            continue;
        }
        if (sscanf(line, "cliff %d %d %d %d %d", &a, &b, &k0, &k1, &k2) == 5) {
            if (a >= 1 && a <= RF_CLIFF_SHAPES && b >= 0 && b < RF_MAX_RELIEF_VARIANTS) {
                cliff_pieces[a][b][0] = (int16_t)k0;
                cliff_pieces[a][b][1] = (int16_t)k1;
                cliff_pieces[a][b][2] = (int16_t)k2;
            }
            continue;
        }
        if (sscanf(line, "ramp %d %d %d %d %d", &a, &b, &k0, &k1, &k2) == 5) {
            if (a >= 0 && a < 4 && b >= 0 && b < 4) {
                ramp_pieces[a][b][0] = (int16_t)k0;
                ramp_pieces[a][b][1] = (int16_t)k1;
                ramp_pieces[a][b][2] = (int16_t)k2;
            }
            continue;
        }
        if (strncmp(line, "terrain ", 8)) continue; /* "tiles N" and anything newer */
        if (terrain_count == RF_MAX_TERRAINS) {
            fprintf(stderr, "rustfront: %s has more than %d terrains\n", path, RF_MAX_TERRAINS);
            return false;
        }
        rfterrain_t *t = &terrains[terrain_count];
        memset(t, 0, sizeof(*t));
        t->edges = t->corners = t->blend = t->tops = -1;
        t->variants = 1;
        char letter[2];
        int used = 0;
        if (sscanf(line, "terrain %15s %1s%n", t->name, letter, &used) != 2) {
            fprintf(stderr, "rustfront: %s:%d: bad terrain line\n", path, n);
            return false;
        }
        t->letter = letter[0];
        bool has_base = false;
        /* key=value words and flags after the letter */
        for (char *p = line + used; *p;) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            char word[48];
            int wl = 0;
            while (p[wl] && p[wl] != ' ' && p[wl] != '\t') wl++;
            snprintf(word, sizeof(word), "%.*s", wl, p);
            p += wl;
            int v;
            if (sscanf(word, "layer=%d", &v) == 1) t->layer = v;
            else if (sscanf(word, "base=%d", &v) == 1) { t->base = v; has_base = true; }
            else if (sscanf(word, "variants=%d", &v) == 1 && v > 0) t->variants = v;
            else if (sscanf(word, "period=%d", &v) == 1 && v > 0) t->period = v;
            else if (sscanf(word, "edges=%d", &v) == 1) t->edges = v;
            else if (sscanf(word, "corners=%d", &v) == 1) t->corners = v;
            else if (sscanf(word, "tops=%d", &v) == 1) t->tops = v;
            else if (!strncmp(word, "blend=", 6)) snprintf(blends[terrain_count], sizeof(blends[0]), "%s", word + 6);
            else if (!strcmp(word, "blocked")) t->blocked = true;
            else if (!strcmp(word, "ore")) t->ore = true;
            else if (!strcmp(word, "solid")) t->solid = true;
        }
        if (!has_base) {
            fprintf(stderr, "rustfront: %s:%d: terrain %s has no base tile\n", path, n, t->name);
            return false;
        }
        terrain_count++;
    }
    for (int i = 0; i < terrain_count; ++i) {
        terrains[i].blend = blends[i][0] ? find_name(blends[i]) : i;
        if (terrains[i].blend < 0) {
            fprintf(stderr, "rustfront: %s: %s blends as unknown terrain %s\n", path, terrains[i].name, blends[i]);
            return false;
        }
    }
    default_terrain = find_name(fallback);
    if (!terrain_count || default_terrain < 0) {
        fprintf(stderr, "rustfront: %s needs terrains and a 'default NAME' line\n", path);
        return false;
    }
    for (int i = 0; i < terrain_count; ++i)
        if (terrains[i].edges >= 0 && terrains[i].corners >= 0) by_layer[spill_count++] = i;
    for (int i = 1; i < spill_count; ++i)
        for (int j = i; j > 0 && terrains[by_layer[j]].layer < terrains[by_layer[j - 1]].layer; --j) {
            int k = by_layer[j];
            by_layer[j] = by_layer[j - 1];
            by_layer[j - 1] = k;
        }
    return true;
}

static bool load_from(const char *path) {
    blob_t file;
    if (!W_ReadFile(path, &file)) return false;
    char *text = malloc(file.size + 1);
    if (!text) { W_FreeFile(&file); return false; }
    memcpy(text, file.bytes, file.size);
    text[file.size] = '\0';
    W_FreeFile(&file);
    bool ok = parse(text, path);
    free(text);
    return ok;
}

/* The table beside a map's game: <root>/assets/terrain.txt for a map in <root>/maps/, else
 * the default root's. */
bool RF_LoadTerrain(const char *map_path) {
    /* The game root is the map's directory's parent (maps/x.txt), or one further up for a
     * campaign mission (maps/campaign/x.txt). */
    char path[1024], file[1100];
    snprintf(path, sizeof(path), "%s", map_path);
    for (int up = 0; up < 3; ++up) {
        char *slash = strrchr(path, '/');
        if (!slash) break;
        *slash = '\0';
        if (up == 0) continue;
        snprintf(file, sizeof(file), "%s/assets/terrain.txt", path);
        FILE *probe = fopen(file, "r");
        if (probe) {
            fclose(probe);
            if (load_from(file)) return true;
        }
    }
    snprintf(file, sizeof(file), "%s/assets/terrain.txt", g_game_default_root);
    if (load_from(file)) return true;
    fprintf(stderr, "rustfront: cannot load the terrain table for %s\n", map_path);
    return false;
}

int RF_TerrainOfLetter(char letter) {
    for (int i = 0; i < terrain_count; ++i)
        if (terrains[i].letter == letter) return i;
    return -1;
}

int RF_DefaultTerrain(void) { return default_terrain; }
bool RF_TerrainBlocks(int terrain) { return terrain >= 0 && terrain < terrain_count && terrains[terrain].blocked; }
bool RF_TerrainHoldsOre(int terrain) { return terrain >= 0 && terrain < terrain_count && terrains[terrain].ore; }

uint16_t RF_TerrainTile(int terrain, int x, int y) {
    if (terrain < 0 || terrain >= terrain_count) return 0;
    /* A sheet of period x period cells: each cell shows its own slice, so the sheet wraps. */
    int p = terrains[terrain].period;
    if (p > 1 && p * p <= terrains[terrain].variants)
        return (uint16_t)(terrains[terrain].base + (y % p) * p + x % p);
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
    h ^= h >> 13;
    return (uint16_t)(terrains[terrain].base + (int)(h % (uint32_t)terrains[terrain].variants));
}

static const rfcell_t *relief_at(const level_t *map, int x, int y) {
    if (!relief || relief_w != map->width || relief_h != map->height || !L_Contains(map, x, y)) return NULL;
    return &relief[L_Index(map, x, y)];
}

void RF_SetCellTerrain(level_t *map, int x, int y, int terrain) {
    if (!L_Contains(map, x, y) || terrain < 0 || terrain >= terrain_count) return;
    int i = L_Index(map, x, y);
    if (map->cell_terrain) map->cell_terrain[i] = (uint8_t)terrain;
    const rfcell_t *r = relief_at(map, x, y);
    /* A cliff or ramp shows its low ground's tile; its own terrain is drawn through a mask. */
    map->tile_ids[i] = RF_TerrainTile(r && r->kind != RF_FLAT ? r->low : terrain, x, y);
}

/* ── relief ─────────────────────────────────────────────────────────────── */

bool RF_BeginRelief(const level_t *map) {
    free(relief);
    relief = calloc((size_t)map->width * map->height, sizeof(*relief));
    relief_w = relief ? map->width : 0;
    relief_h = relief ? map->height : 0;
    return relief != NULL;
}

void RF_SetCellLevel(const level_t *map, int x, int y, int level, bool ramp) {
    rfcell_t *r = (rfcell_t *)relief_at(map, x, y);
    if (!r) return;
    r->level = (uint8_t)(level < 0 ? 0 : level > 9 ? 9 : level);
    r->ramp = ramp;
}

int RF_CellLevel(const level_t *map, int x, int y) {
    const rfcell_t *r = relief_at(map, x, y);
    return r ? r->level : 0;
}

int RF_CellRelief(const level_t *map, int x, int y) {
    const rfcell_t *r = relief_at(map, x, y);
    return !r ? RF_RELIEF_FLAT : r->kind == RF_CLIFF ? RF_RELIEF_CLIFF : r->kind == RF_RAMP ? RF_RELIEF_RAMP : RF_RELIEF_FLAT;
}

bool RF_CellBlocks(const level_t *map, int i) {
    if (relief && relief_w == map->width && relief_h == map->height && relief[i].kind == RF_CLIFF) return true;
    return map->cell_terrain && RF_TerrainBlocks(map->cell_terrain[i]);
}

/* The lowest level of the cells that meet at grid corner (cx, cy). */
static int corner_level(const level_t *map, int cx, int cy) {
    int low = 255;
    for (int y = cy - 1; y <= cy; ++y)
        for (int x = cx - 1; x <= cx; ++x)
            if (L_Contains(map, x, y) && relief[L_Index(map, x, y)].level < low) low = relief[L_Index(map, x, y)].level;
    return low;
}

static uint32_t cell_hash(int x, int y) {
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
    return h ^ (h >> 13);
}

/* Shapes every cell from the levels RF_SetCellLevel gave: a ramp stands at the level of its
 * highest plain neighbour, a cell with uneven corners is a cliff or a ramp, and a cliff takes
 * the lowest-layer terrain of its low neighbours for the ground under its rock, which the
 * others then spill over as they would on a flat cell. */
void RF_BuildRelief(level_t *map) {
    if (!relief || relief_w != map->width || relief_h != map->height) return;
    static const int8_t side[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (int y = 0; y < map->height; ++y)
        for (int x = 0; x < map->width; ++x) {
            rfcell_t *r = &relief[L_Index(map, x, y)];
            if (!r->ramp) continue;
            int top = 0;
            for (int s = 0; s < 4; ++s) {
                int nx = x + side[s][0], ny = y + side[s][1];
                if (L_Contains(map, nx, ny) && !relief[L_Index(map, nx, ny)].ramp && relief[L_Index(map, nx, ny)].level > top)
                    top = relief[L_Index(map, nx, ny)].level;
            }
            r->level = (uint8_t)top;
        }
    int cw = map->width + 1;
    uint8_t *corners = malloc((size_t)cw * (map->height + 1));
    if (!corners) return;
    for (int cy = 0; cy <= map->height; ++cy)
        for (int cx = 0; cx < cw; ++cx) corners[cy * cw + cx] = (uint8_t)corner_level(map, cx, cy);
    for (int y = 0; y < map->height; ++y)
        for (int x = 0; x < map->width; ++x) {
            rfcell_t *r = &relief[L_Index(map, x, y)];
            int c[4] = {corners[y * cw + x + 1], corners[(y + 1) * cw + x + 1],
                        corners[(y + 1) * cw + x], corners[y * cw + x]}; /* NE SE SW NW */
            int lo = c[0], hi = c[0];
            for (int k = 1; k < 4; ++k) {
                if (c[k] < lo) lo = c[k];
                if (c[k] > hi) hi = c[k];
            }
            r->kind = RF_FLAT;
            r->shape = r->sides = r->dir = 0;
            r->low_level = (uint8_t)lo;
            if (lo == hi) {
                r->level = (uint8_t)lo; /* a lone raised cell has no corner up: it stays low */
                continue;
            }
            r->level = (uint8_t)hi;
            for (int k = 0; k < 4; ++k)
                if (c[k] > lo) r->shape |= (uint8_t)(1 << k);
            r->kind = RF_CLIFF;
            r->variant = (uint8_t)(cell_hash(x, y) % (uint32_t)(relief_variants ? relief_variants : 1));
            if (r->ramp)
                for (int d = 0; d < 4; ++d)
                    if (ramp_shapes[d] == r->shape) { r->kind = RF_RAMP; r->dir = (uint8_t)d; }
        }
    free(corners);
    for (int y = 0; y < map->height; ++y)
        for (int x = 0; x < map->width; ++x) {
            rfcell_t *r = &relief[L_Index(map, x, y)];
            if (r->kind == RF_RAMP) { /* rock flanks where a cliff runs on beside the ramp */
                int a = r->dir & 1 ? 0 : 3, b = r->dir & 1 ? 2 : 1; /* N/S ramps: W, E; E/W: N, S */
                const rfcell_t *na = relief_at(map, x + side[a][0], y + side[a][1]);
                const rfcell_t *nb = relief_at(map, x + side[b][0], y + side[b][1]);
                r->sides = (uint8_t)((na && na->kind == RF_CLIFF) | ((nb && nb->kind == RF_CLIFF) << 1));
            }
            if (r->kind == RF_FLAT) continue;
            int best = -1; /* the lowest ground beside it: the others spill onto it */
            for (int ny = y - 1; ny <= y + 1; ++ny)
                for (int nx = x - 1; nx <= x + 1; ++nx) {
                    const rfcell_t *n = relief_at(map, nx, ny);
                    if (!n || n->kind != RF_FLAT || n->level != r->low_level) continue;
                    int t = map->cell_terrain[L_Index(map, nx, ny)];
                    if (t >= terrain_count) continue;
                    t = terrains[t].blend;
                    if (best < 0 || terrains[t].layer < terrains[best].layer) best = t;
                }
            r->low = (uint8_t)(best >= 0 ? best : default_terrain);
            RF_SetCellTerrain(map, x, y, map->cell_terrain[L_Index(map, x, y)]);
        }
}

/* The terrain a cell spills as (a model tile's ground) to a cell on `level`, or -1: off the
 * map, or on another level. A cliff shows its own terrain on its high side, its low ground's
 * on its low side. */
static int blend_at(const level_t *map, int x, int y, int level) {
    if (!L_Contains(map, x, y)) return -1;
    int t = map->cell_terrain[L_Index(map, x, y)];
    const rfcell_t *r = relief_at(map, x, y);
    if (r && r->level != level) {
        if (r->kind == RF_FLAT || r->low_level != level) return -1;
        t = r->low;
    }
    return t < terrain_count ? terrains[t].blend : -1;
}

/* The terrain whose tops= tiles show a plateau of `t`: its own, its ground's, or the default's. */
static int top_terrain(int t) {
    if (terrains[t].tops >= 0) return t;
    if (terrains[terrains[t].blend].tops >= 0) return terrains[t].blend;
    return terrains[default_terrain].tops >= 0 ? default_terrain : -1;
}

/* Spills onto a cell showing terrain `own` on `level`. */
static void draw_spills(app_t *app, const level_t *map, const tileset_t *tileset, int x, int y, int own, int level,
                        irect_t dst) {
    if (own >= terrain_count || terrains[own].solid) return;
    int layer = terrains[terrains[own].blend].layer;
    static const int8_t ring[8][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
    int around[8];
    bool higher = false;
    for (int i = 0; i < 8; ++i) {
        around[i] = blend_at(map, x + ring[i][0], y + ring[i][1], level);
        higher |= around[i] >= 0 && terrains[around[i]].layer > layer;
    }
    if (!higher) return;
    irect_t whole = {0, 0, tileset->tile_w, tileset->tile_h};
    for (int s = 0; s < spill_count; ++s) {
        int t = by_layer[s];
        if (terrains[t].layer <= layer) continue;
        int edges = 0, corners = 0;
        for (int side = 0; side < 4; ++side) edges |= (around[side] == t) << side;
        /* A corner counts only where neither side beside it is the same terrain: the side's
         * edge already covers it. Corner c lies between sides c and c + 1. */
        for (int c = 0; c < 4; ++c)
            if (around[4 + c] == t && around[c] != t && around[(c + 1) & 3] != t) corners |= 1 << c;
        if (edges) R_DrawTile(app, tileset, terrains[t].edges + edges - 1, whole, dst);
        if (corners) R_DrawTile(app, tileset, terrains[t].corners + corners - 1, whole, dst);
    }
}

static int relief_piece(const rfcell_t *r, int k) {
    if (r->kind == RF_CLIFF) return cliff_pieces[r->shape][r->variant][k];
    if (r->kind == RF_RAMP) return ramp_pieces[r->dir][r->sides][k];
    return -1;
}

void RF_DrawTransitions(app_t *app, const level_t *map, const tileset_t *tileset, int x, int y, int dx, int dy) {
    if (!map->cell_terrain || !terrain_count) return;
    irect_t whole = {0, 0, tileset->tile_w, tileset->tile_h};
    irect_t dst = {dx, dy, tileset->tile_w, tileset->tile_h};
    const rfcell_t *self = relief_at(map, x, y);
    if (self && self->kind != RF_FLAT) {
        draw_spills(app, map, tileset, x, y, self->low, self->low_level, dst);
        int own = map->cell_terrain[L_Index(map, x, y)];
        int t = own < terrain_count ? top_terrain(own) : -1;
        int V = relief_variants ? relief_variants : 1;
        if (t >= 0)
            R_DrawTile(app, tileset, terrains[t].tops + (self->kind == RF_CLIFF ?
                (self->shape - 1) * V + self->variant : RF_CLIFF_SHAPES * V + self->dir * 4 + self->sides), whole, dst);
    } else {
        draw_spills(app, map, tileset, x, y, map->cell_terrain[L_Index(map, x, y)], self ? self->level : 0, dst);
    }
    if (!relief_at(map, x, y)) return;
    for (int k = 2; k >= 0; --k) { /* the rock of the cliffs north of here, farthest first */
        const rfcell_t *r = relief_at(map, x, y - k);
        int piece = r ? relief_piece(r, k) : -1;
        if (piece >= 0) R_DrawTile(app, tileset, piece, whole, dst);
    }
}
