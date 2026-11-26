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
 * of its position, and then the transitions: every higher-layer terrain among its eight
 * neighbours spills over its edges, lowest layer first, as an edge tile (the sides that
 * terrain touches: N 1, E 2, S 4, W 8) and a corner tile (the diagonals it touches alone:
 * NE 1, SE 2, SW 4, NW 8). A model terrain with a blend= ground spills as that ground. */

#define RF_MAX_TERRAINS 32

typedef struct {
    char name[16];
    char letter;
    int layer, base, variants, edges, corners, blend;
    bool blocked, ore, solid;
} rfterrain_t;

static rfterrain_t terrains[RF_MAX_TERRAINS];
static int terrain_count, default_terrain = -1;
static int by_layer[RF_MAX_TERRAINS]; /* terrains that spill, lowest layer first */
static int spill_count;

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
    int n = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        n++;
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r') line[--len] = '\0';
        if (!len || line[0] == '#') continue; /* whole-line comments: '#' is also a map letter */
        if (sscanf(line, "default %15s", fallback) == 1) continue;
        if (strncmp(line, "terrain ", 8)) continue; /* "tiles N" and anything newer */
        if (terrain_count == RF_MAX_TERRAINS) {
            fprintf(stderr, "rustfront: %s has more than %d terrains\n", path, RF_MAX_TERRAINS);
            return false;
        }
        rfterrain_t *t = &terrains[terrain_count];
        memset(t, 0, sizeof(*t));
        t->edges = t->corners = t->blend = -1;
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
            else if (sscanf(word, "edges=%d", &v) == 1) t->edges = v;
            else if (sscanf(word, "corners=%d", &v) == 1) t->corners = v;
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
    char path[1024];
    snprintf(path, sizeof(path), "%s", map_path);
    for (int up = 0; up < 2; ++up) {
        char *slash = strrchr(path, '/');
        if (slash) *slash = '\0';
        else snprintf(path, sizeof(path), ".");
    }
    char file[1100];
    snprintf(file, sizeof(file), "%s/assets/terrain.txt", path);
    if (load_from(file)) return true;
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
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
    h ^= h >> 13;
    return (uint16_t)(terrains[terrain].base + (int)(h % (uint32_t)terrains[terrain].variants));
}

void RF_SetCellTerrain(level_t *map, int x, int y, int terrain) {
    if (!L_Contains(map, x, y) || terrain < 0 || terrain >= terrain_count) return;
    int i = L_Index(map, x, y);
    if (map->cell_terrain) map->cell_terrain[i] = (uint8_t)terrain;
    map->tile_ids[i] = RF_TerrainTile(terrain, x, y);
}

/* The terrain a cell spills as (a model tile's ground), or -1 off the map. */
static int blend_at(const level_t *map, int x, int y) {
    if (!L_Contains(map, x, y)) return -1;
    int t = map->cell_terrain[L_Index(map, x, y)];
    return t < terrain_count ? terrains[t].blend : -1;
}

void RF_DrawTransitions(app_t *app, const level_t *map, const tileset_t *tileset, int x, int y, int dx, int dy) {
    if (!map->cell_terrain || !terrain_count) return;
    int own = map->cell_terrain[L_Index(map, x, y)];
    if (own >= terrain_count || terrains[own].solid) return;
    int layer = terrains[terrains[own].blend].layer;
    static const int8_t ring[8][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
    int around[8];
    bool higher = false;
    for (int i = 0; i < 8; ++i) {
        around[i] = blend_at(map, x + ring[i][0], y + ring[i][1]);
        higher |= around[i] >= 0 && terrains[around[i]].layer > layer;
    }
    if (!higher) return;
    irect_t whole = {0, 0, tileset->tile_w, tileset->tile_h};
    irect_t dst = {dx, dy, tileset->tile_w, tileset->tile_h};
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
