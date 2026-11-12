#define _DEFAULT_SOURCE
#include "z_local.h"
#include "info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fort pieces by world, ZED.EXE 0x117de2 (+0 standing, +10 fallen);
 * 0x126d9 draws them. */
static const int fort_sprites[2][Z_WORLDS] = {
    {809, 2127, 2304, 2800, 2613},
    {1675, 2148, 2431, 2845, 2725},
};

int Z_BuildingSprite(int native, int world) {
    if (world < 0 || world >= Z_WORLDS) return -1;
#define Z_BUILDING(id, type, label, w, h, ...) \
    if (native == id) return ((const int[]){__VA_ARGS__})[world];
#include "buildings.inc"
#undef Z_BUILDING
    return -1;
}

/* World palettes are 6-bit VGA triples. */
static bool load_palette(const char *root, int world, uint32_t palette[256]) {
    static const char *const names[Z_WORLDS] = {
        "DESERT.PAL", "ARCTIC.PAL", "CITY.PAL", "VOLCAN.PAL", "JUNGLE.PAL"};
    char path[1024];
    blob_t blob;
    M_PathJoin(path, sizeof(path), root, "WARDATA.PAC");
    if (!Z_ReadPac(path, names[world], &blob)) return false;
    bool ok = blob.size >= 768;
    for (int i = 0; ok && i < 256; ++i) {
        const uint8_t *c = blob.bytes + i * 3;
        palette[i] = 0xff000000u | (uint32_t)(c[0] * 255 / 63) << 16 |
                     (uint32_t)(c[1] * 255 / 63) << 8 | (uint32_t)(c[2] * 255 / 63);
    }
    W_FreeFile(&blob);
    return ok;
}

/* A SPRITES.RSC picture: height, width / 8, a row table padded to four
 * bytes, then the pixels as four VGA planes (pixel x is in plane x & 3). */
static uint8_t *decode_sprite(const zmap_t *map, int index, isize2_t *size) {
    if (!Z_SpriteSize(map, index, size) || size->w <= 0 || size->h <= 0) return NULL;
    const blob_t *rsc = &map->rsc;
    size_t offset = read_u32_le(rsc->bytes + 2 + (size_t)index * 4);
    size_t pixels = offset + ((2 + (size_t)size->h + 3) & ~(size_t)3);
    size_t count = (size_t)size->w * size->h;
    if (pixels > rsc->size || rsc->size - pixels < count) return NULL;
    uint8_t *out = malloc(count);
    if (!out) return NULL;
    const uint8_t *src = rsc->bytes + pixels;
    int quarter = size->w / 4;
    for (int plane = 0; plane < 4; ++plane)
        for (int y = 0; y < size->h; ++y)
            for (int i = 0; i < quarter; ++i)
                out[y * size->w + i * 4 + plane] = *src++;
    return out;
}

/* Sprites draw their team in 0x10..0x1f; ZED.EXE 0x5fd39 points that run
 * at blue 0x20, green 0x30, yellow 0x40 or, for nobody's, 0x50. */
static bool install_teams(spritesheet_t *sheet, const uint32_t palette[256]) {
    memcpy(sheet->palette, palette, sizeof(sheet->palette));
    sheet->palette[0] = 0;
    memcpy(sheet->source_palette, sheet->palette, sizeof(sheet->palette));
    sheet->indexed = true;
    sheet->palette_maps = calloc(Z_NEUTRAL, sizeof(*sheet->palette_maps));
    if (!sheet->palette_maps) return false;
    sheet->palette_map_count = Z_NEUTRAL;
    for (int team = 1; team <= Z_NEUTRAL; ++team) {
        spritepalettemap_t *map = &sheet->palette_maps[team - 1];
        map->id = team;
        for (int i = 0; i < 256; ++i) map->indices[i] = (uint8_t)i;
        for (int i = 0; i < 16; ++i) map->indices[0x10 + i] = (uint8_t)(0x10 * (team + 1) + i);
    }
    return true;
}

/* Pictures share one ground point (ground.x < 0 centres each) and bound
 * the frame size. */
static void place_cells(spritesheet_t *sheet, ivec2_t ground) {
    for (int i = 0; i < sheet->numlumps; ++i) {
        spritecell_t *cell = &sheet->cells[i];
        cell->ground_point = ground.x >= 0 ? ground : (ivec2_t){cell->rect.w / 2, cell->rect.h / 2};
        if (cell->rect.w > sheet->frame_size.w) sheet->frame_size.w = cell->rect.w;
        if (cell->rect.h > sheet->frame_size.h) sheet->frame_size.h = cell->rect.h;
    }
}

/* Lumps centred on their mobj, one nondirectional frame each. */
static bool finish_sheet(spritesheet_t *sheet, const uint32_t palette[256], ivec2_t ground) {
    if (!R_InitSpriteDef(sheet, sheet->numlumps, 1)) return false;
    place_cells(sheet, ground);
    for (int i = 0; i < sheet->numlumps; ++i) R_InstallSpriteLump(sheet, i, 0, i, false);
    return install_teams(sheet, palette);
}

static bool put_lump(spritesheet_t *sheet, int lump, uint8_t *pixels, isize2_t size) {
    if (!pixels) return false;
    sheet->lumps[lump].indices = pixels;
    sheet->cells[lump].rect = sheet->cells[lump].bounds = (irect_t){0, 0, size.w, size.h};
    return true;
}

static bool load_rsc(const zmap_t *map, const uint32_t palette[256], spritesheet_t *sheet) {
    int count = read_u16_le(map->rsc.bytes);
    if (!R_AllocSpriteCells(sheet, count)) return false;
    for (int i = 0; i < count; ++i) {
        isize2_t size;
        if (!put_lump(sheet, i, decode_sprite(map, i, &size), size)) return false;
    }
    return finish_sheet(sheet, palette, (ivec2_t){-1, -1});
}

/* Interface images have their own owner, outside gameplay sprite frames. */
bool Z_LoadUISprites(const char *root, const int *indices, int count, spritesheet_t *out) {
    zmap_t map = {0};
    char path[1024];
    M_PathJoin(path, sizeof(path), root, "SPRITES.RSC");
    if (!W_ReadFile(path, &map.rsc)) return false;
    bool ok = map.rsc.size >= 2 &&
              map.rsc.size >= 2 + (size_t)read_u16_le(map.rsc.bytes) * 5 &&
              R_AllocSpriteCells(out, count);
    for (int i = 0; ok && i < out->numlumps; ++i) {
        if (indices[i] == -1) continue;
        isize2_t size = {0};
        uint8_t *pixels = decode_sprite(&map, indices[i], &size);
        ok = put_lump(out, i, pixels, size);
    }
    W_FreeFile(&map.rsc);
    if (!ok) R_FreeSprite(out);
    out->indexed = true;
    return ok;
}

bool Z_LoadPanelSprites(const char *root, spritesheet_t *out) {
    int indices[Z_UI_WINDOW + 123];
    for (int i = 0; i < Z_UI_WINDOW + 123; ++i)
        indices[i] = i < Z_UI_BACKGROUND ? 787 + i : i == Z_UI_BACKGROUND ? 1889 : Z_UI_FIRST + i - Z_UI_WINDOW;
    return Z_LoadUISprites(root, indices, Z_UI_WINDOW + 123, out);
}

/* Draw SPRITES.RSC picture `index` into `out` with its top-left at `at`,
 * clipped, keeping what shows through its transparent pixels. */
static bool blit(const zmap_t *map, int index, uint8_t *out, isize2_t size, ivec2_t at) {
    isize2_t piece;
    uint8_t *pixels = decode_sprite(map, index, &piece);
    if (!pixels) return false;
    for (int y = 0; y < piece.h; ++y)
        for (int x = 0; x < piece.w; ++x) {
            uint8_t c = pixels[y * piece.w + x];
            ivec2_t to = {at.x + x, at.y + y};
            if (c && to.x >= 0 && to.x < size.w && to.y >= 0 && to.y < size.h) out[to.y * size.w + to.x] = c;
        }
    free(pixels);
    return true;
}

/* ZED.EXE 0x126d9: the body at y + 16, a strip over it and the front below,
 * both from the second set (pieces + 4) on the fort without a gate. A
 * fallen fort draws the same way from its own pieces (0x12606). */
static uint8_t *compose_fort(const zmap_t *map, bool fallen, int variant, isize2_t *size) {
    static const struct { int piece; ivec2_t at; } parts[] = {
        {0, {0, 16}}, {1, {120, 16}}, {2, {0, 0}}, {3, {120, 0}}, {4, {0, 112}}, {5, {120, 112}},
    };
    int base = fort_sprites[fallen][map->world];
    isize2_t front;
    if (!Z_SpriteSize(map, base + 4 + variant * 4, &front)) return NULL;
    *size = (isize2_t){160, 112 + front.h};
    uint8_t *out = calloc((size_t)size->w * size->h, 1);
    if (!out) return NULL;
    for (size_t p = 0; p < sizeof(parts) / sizeof(*parts); ++p) {
        int index = base + parts[p].piece + (parts[p].piece >= 2 ? variant * 4 : 0);
        if (!blit(map, index, out, *size, parts[p].at)) { free(out); return NULL; }
    }
    return out;
}

/* draw_flags (0x2aade): the flag 480..483 over its shadow 484..487, both
 * with their top-left on the flag's cell, which is the ground point. */
static bool load_flag(const zmap_t *map, const uint32_t palette[256], spritesheet_t *sheet) {
    enum { FLAG = 480, SHADOW = 484, FRAMES = 4 };
    if (!R_AllocSpriteCells(sheet, FRAMES)) return false;
    for (int f = 0; f < FRAMES; ++f) {
        isize2_t flag, shadow;
        if (!Z_SpriteSize(map, FLAG + f, &flag) || !Z_SpriteSize(map, SHADOW + f, &shadow)) return false;
        isize2_t size = {flag.w > shadow.w ? flag.w : shadow.w, flag.h > shadow.h ? flag.h : shadow.h};
        uint8_t *out = calloc((size_t)size.w * size.h, 1);
        if (!out || !put_lump(sheet, f, out, size)) { free(out); return false; }
        if (!blit(map, SHADOW + f, out, size, (ivec2_t){0, 0}) ||
            !blit(map, FLAG + f, out, size, (ivec2_t){0, 0})) return false;
    }
    return finish_sheet(sheet, palette, (ivec2_t){Z_TILE / 2, Z_TILE / 2});
}

/* A run of SPRITES.RSC pictures: with `turn`, eight directions `turn`
 * apart, north first and clockwise, each `count` frames long (ZED.EXE
 * 0x7d761: base + direction * stride + frame); without, `count` pictures
 * that face every way. */
typedef struct { int base, turn, count; } zrun_t;

static bool load_runs(const zmap_t *map, const zrun_t *runs, int nruns,
                      const uint32_t palette[256], spritesheet_t *sheet) {
    int lumps = 0, frames = 0;
    for (int r = 0; r < nruns; ++r) {
        lumps += runs[r].count * (runs[r].turn ? 8 : 1);
        frames += runs[r].count;
    }
    if (!R_AllocSpriteCells(sheet, lumps) || !R_InitSpriteDef(sheet, frames, 8)) return false;
    int lump = 0, frame = 0;
    for (int r = 0; r < nruns; ++r)
        for (int f = 0; f < runs[r].count; ++f, ++frame) {
            spriteframe_t *def = &sheet->spritedef.spriteframes[frame];
            if (!runs[r].turn) {
                free(def->directions);
                def->directions = NULL;
                def->rotations = 0;
                if (!R_AllocSpriteDirections(def, 1)) return false;
            }
            for (int d = 0; d < def->rotations; ++d, ++lump) {
                isize2_t size;
                uint8_t *pixels = decode_sprite(map, runs[r].base + d * runs[r].turn + f, &size);
                /* Renderer slots run counterclockwise from north. */
                if (!put_lump(sheet, lump, pixels, size) ||
                    !R_InstallSpriteLump(sheet, frame, (8 - d) % 8, lump, false)) return false;
            }
        }
    /* ZED.EXE draws every frame from the unit's top-left; centre the first. */
    place_cells(sheet, (ivec2_t){sheet->cells[0].rect.w / 2, sheet->cells[0].rect.h / 2});
    return install_teams(sheet, palette);
}

/* Vehicles are drawn in parts by unit type (ZED.EXE 0x7e2d8, types set
 * by 0x6375a), each part at `at` pixels from the unit's position moved 6
 * left and `lift` up. A part's picture is `sprite` + direction * `turn`,
 * by direction & 3 for the `quarter` hulls that look the same both ways,
 * or `by_dir` for a part some directions do not draw; while moving it
 * steps through `cycle` frames and while standing through the `bounce`
 * ones. A `turret` part turns with the unit's weapon (+0x15), not the
 * hull: it goes on a sheet of its own, by turret direction, `at` pixels
 * from the hull's `mount` for the hull's direction; the turret sheet's
 * `cycle` frames are its firing ones. */
typedef struct {
    int sprite, turn;
    bool quarter;
    int8_t at[8][2];
    int cycle;
    bool bounce, turret;
    int16_t by_dir[8];
} zpart_t;

enum { CRANE_REAR_HOOK, CRANE_HULL, CRANE_ARM, CRANE_FRONT_HOOK };

static const struct {
    const char *name;
    int lift;
    int8_t mount[8][2];
    zpart_t parts[4];
} vehicles[] = {
    /* 6 (0x7e31e): hull 488 (0x14b2c4); turret 513 and barrel 521
     * (0x14b283, 0x14b2b3, 6 right and 3 up) and hatch 533 (0x14b314) by
     * turret from the unit */
    {"MED_TANK", 0, {{0, 0}}, {
        {488, 3, true, {{0, 0}, {-2, -7}, {-2, 0}, {0, -7}, {0, 0}, {-2, -7}, {-2, 0}, {0, -7}}, .cycle = 3},
        {513, 1, .at = {{6, -3}, {4, -3}, {3, -6}, {4, -6}, {6, -6}, {6, -6}, {6, -6}, {5, -3}}, .turret = true},
        {521, 1, .at = {{13, -9}, {24, -3}, {22, -3}, {17, 4}, {12, 5}, {4, 3}, {0, -3}, {3, -7}}, .turret = true},
        {533, 3, .at = {{11, -6}, {11, -6}, {11, -6}, {11, -6}, {11, -6}, {11, -6}, {11, -6}, {11, -6}},
         .turret = true},
    }},
    /* 0x19 (0x7e49f): hull 5305 (0x14b2d4); turret 5329 (0x14b293) and hatch 533 (0x14b334) by turret */
    {"HEAVY_TANK", 6, {{0, 0}}, {
        {5305, 3, true, {{0, 0}}, .cycle = 3},
        {5329, 1, .at = {{-1, -3}, {0, -3}, {4, -1}, {0, -2}, {-1, -1}, {-2, -2}, {-6, -1}, {-2, -3}},
         .turret = true},
        {533, 3, .at = {{15, 6}, {13, 9}, {8, 8}, {8, 5}, {8, 3}, {9, 0}, {14, 2}, {15, 3}}, .turret = true},
    }},
    /* 0x1e (0x7e5df): hull 5719 (0x14b2e4); turret 5731 (0x14b2a3) and hatch 533 (0x14b324) by turret */
    {"LIGHT_TANK", 6, {{0, 0}}, {
        {5719, 3, true, {{-1, 0}, {-4, 0}, {-5, 0}, {-5, 0}, {-1, 0}, {-4, 0}, {-5, 0}, {-5, 0}}, .cycle = 3},
        {5731, 1, .at = {{-3, -2}, {-3, -3}, {-3, -3}, {-3, -3}, {-3, 0}, {-4, -1}, {-3, -1}, {-4, -1}},
         .turret = true},
        {533, 3, .at = {{9, 4}, {8, 3}, {8, 2}, {8, 2}, {9, 3}, {9, 2}, {9, 2}, {10, 3}}, .turret = true},
    }},
    /* 0x20 (0x7e719): hull 5655, dish 5679 by hull (0x14b354; it spins with the game clock) */
    {"APC", 6, {{0, 0}}, {
        {5655, 3, .at = {{0, 0}}, .cycle = 3},
        {5679, 0, .at = {{14, 6}, {8, 12}, {2, 8}, {1, 7}, {4, 4}, {8, 2}, {15, 4}, {16, 6}}},
    }},
    /* 0x22 (0x7e846): wheels and shadow (0x14b444, none facing north or
     * south) turning with the move, the body over them, rocking with the
     * wheels and, standing, every second tick of the clock (0x10e92e) on
     * still wheels; the gunner, firing in his second picture, 6 right and
     * 3 up from the hull's mount (0x14b484) and by turret (0x14b494). */
    {"JEEP", 6, {{2, 8}, {-5, 11}, {-10, 6}, {-6, 0}, {0, 0}, {12, 0}, {14, 5}, {10, 11}}, {
        {.cycle = 4, .by_dir = {0, 5614, 5618, 5622, 0, 5626, 5630, 5634}},
        {5590, 2, .at = {{0, 0}}, .cycle = 2, .bounce = true},
        {5639, 2, .at = {{6, -3}, {9, -3}, {11, -4}, {8, 1}, {5, 3}, {3, 1}, {1, -4}, {3, -3}}, .cycle = 2,
         .turret = true},
    }},
    /* 0x24 (0x7ea2b): body 5794, arm 5818 and resting hook 5826.
     * The hook's second displacement is at 0x14b464. The hook for
     * hull directions 3–5 is placed underneath the hull. */
    {"CRANE", 6, {{0, 0}}, {
        [CRANE_REAR_HOOK] = {.at = {[3] = {19, 21}, [4] = {7, 21}, [5] = {-8, 19}},
                             .by_dir = {[3] = 5826, [4] = 5826, [5] = 5826}},
        [CRANE_HULL] = {5794, 3, .at = {{0, 0}}, .cycle = 3},
        [CRANE_ARM] = {5818, 1, .at = {{-1, -9}, {1, -8}, {6, -6}, {3, -4}, {0, -5}, {-4, -4}, {-6, -6}, {-2, -8}},
                       .turret = true},
        [CRANE_FRONT_HOOK] = {.at = {{16, -5}, {28, -2}, {31, 8}, {25, 18}, {13, 18}, {-2, 16}, {-4, 10}, {8, -4}},
                              .by_dir = {5826, 5826, 5826, 0, 0, 0, 5826, 5826}, .turret = true},
    }},
    /* 0x18 (0x7ee27): hull 1845 (0x14b404); pod 1869 6 right and 3 up by
     * turret (0x1489c8) from a mount 9 left and 5 down by hull (0x148a08) */
    {"M_MISSILE", 8, {{-4, 6}, {-6, 8}, {-10, 2}, {-7, 4}, {-7, 5}, {-3, 3}, {-1, 2}, {-3, 8}}, {
        {1845, 3, .at = {{1, 2}, {2, 0}, {0, -2}, {0, 0}, {-1, 2}, {1, 0}, {0, -2}, {0, 0}}, .cycle = 3},
        {1869, 1, .at = {{8, -5}, {8, -5}, {10, -3}, {7, -6}, {8, -2}, {7, -5}, {9, -6}, {7, -7}},
         .turret = true},
    }},
};

static bool part_used(const zpart_t *part) {
    return part->sprite || memcmp(part->by_dir, (const int16_t[8]){0}, sizeof(part->by_dir));
}

/* The picture of `part` facing `dir` in composed frame `f` of `frames`
 * moving ones and the standing ones after them; -1 draws nothing. */
static int part_sprite(const zpart_t *part, int dir, int f, int frames) {
    int step = f < frames ? (part->cycle ? f % part->cycle : 0) : (part->bounce ? f - frames : 0);
    if (!part->sprite) return part->by_dir[dir] ? part->by_dir[dir] + step : -1;
    return part->sprite + (part->quarter ? dir & 3 : dir) * part->turn + step;
}

static int find_vehicle(const char *name) {
    for (size_t v = 0; v < sizeof(vehicles) / sizeof(*vehicles); ++v)
        if (!strcmp(vehicles[v].name, name)) return (int)v;
    return -1;
}

typedef struct { ivec2_t at; int sprite; } zpiece_t;

/* The layer pointer lists at 0x12ddcc/0x12dde8. Each layer has twelve
 * poses. Identical native layers share a row. */
static const zpiece_t crane_layers[][Z_CRANE_POSES] = {
    { /* 1, 0x12de68 */
        {{0,-2},5911}, {{-6,-2},5911}, {{-12,-1},5911}, {{-18,-1},5911}, {{-24,0},5911}, {{-28,0},5911},
        {{-32,1},5911}, {{-36,1},5911}, {{-40,2},5911}, {{-42,2},5912}, {{-44,2},5912}, {{-45,2},5913},
    },
    { /* 2, 0x12dec8 */
        {{16,-2},5911}, {{22,-2},5911}, {{28,-1},5911}, {{34,-1},5911}, {{40,0},5911}, {{44,0},5911},
        {{48,1},5911}, {{52,1},5911}, {{54,2},5911}, {{56,2},5912}, {{57,2},5912}, {{57,2},5913},
    },
    { /* 3, 0x12df28 */
        {{-8,0},5919}, {{-8,-10},5920}, {{-8,-20},5921}, {{-8,-28},5914}, {{-8,-36},5915}, {{-8,-40},5916},
        {{-8,-42},5917}, {{-8,-40},5918}, {{-8,-38},5919}, {{-8,-34},5920}, {{-8,-32},5921}, {{-8,-30},5914},
    },
    { /* 4, 0x12df88 */
        {{0,0},5907}, {{0,-6},5908}, {{0,-12},5909}, {{0,-15},5902}, {{0,-16},5903}, {{0,-15},5904},
        {{0,-12},5905}, {{0,-6},5906}, {{0,0},5907}, {{0,4},5908}, {{0,8},5909}, {{0,12},5910},
    },
    { /* 5, 0x12dfe8 */
        {{0,0},5932}, {{0,0},5932}, {{0,0},5932}, {{0,0},5932}, {{0,-2},5932}, {{-2,-1},5932},
        {{-5,1},5932}, {{-7,2},5932}, {{-9,4},5932}, {{-10,4},5932}, {{-11,5},5924}, {{-11,5},5924},
    },
    { /* 6, 0x12e048 */
        {{0,0},5930}, {{0,0},5930}, {{0,0},5930}, {{16,-2},5930}, {{18,-4},5930}, {{20,-5},5930},
        {{22,-5},5930}, {{24,-4},5930}, {{26,-2},5930}, {{28,0},5930}, {{30,2},5930}, {{32,5},5922},
    },
    { /* 7, 0x12e168 */
        {{-8,0},5919}, {{-8,-10},5920}, {{-8,-12},5921}, {{-8,-10},5914}, {{-8,-5},5915}, {{-8,-1},5916},
        {{-8,3},5917}, {{-8,6},5918}, {{-8,12},5919}, {{-8,16},5920}, {{-8,18},5921}, {{-8,20},5914},
    },
    { /* 8, 0x12e288 */
        {{0,0},5930}, {{0,0},5930}, {{0,0},5930}, {{16,-2},5930}, {{17,-4},5930}, {{18,-8},5930},
        {{18,-12},5930}, {{19,-14},5930}, {{19,-16},5930}, {{20,-18},5930}, {{20,-20},5930}, {{20,-21},5922},
    },
    { /* 9, 0x12e2e8 */
        {{0,4},5911}, {{-2,6},5911}, {{-4,7},5911}, {{-8,9},5911}, {{-14,14},5911}, {{-17,22},5911},
        {{-20,26},5911}, {{-22,29},5911}, {{-24,31},5911}, {{-26,33},5912}, {{-27,35},5912}, {{-28,36},5913},
    },
    { /* 10, 0x12e348 */
        {{0,8},5911}, {{-2,6},5911}, {{-4,5},5911}, {{-8,3},5911}, {{-14,-2},5911}, {{-17,-10},5911},
        {{-20,-14},5911}, {{-22,-17},5911}, {{-24,-19},5911}, {{-26,-21},5912}, {{-27,-23},5912}, {{-28,-24},5913},
    },
    { /* 11, 0x12e4c8 */
        {{0,0},5930}, {{0,0},5930}, {{0,0},5930}, {{16,-2},5930}, {{17,-4},5930}, {{18,-6},5930},
        {{18,-10},5930}, {{19,-14},5930}, {{19,-14},5930}, {{20,-15},5930}, {{20,-15},5930}, {{20,-16},5922},
    },
    { /* 12, 0x12e528 */
        {{0,-2},5911}, {{-1,-2},5911}, {{-2,-4},5911}, {{-4,-4},5911}, {{-8,-8},5911}, {{-16,-8},5911},
        {{-22,-16},5911}, {{-26,-16},5911}, {{-28,-20},5911}, {{-30,-20},5912}, {{-31,-22},5912}, {{-32,-22},5913},
    },
    { /* 13, 0x12e588 */
        {{16,-2},5911}, {{17,-2},5911}, {{18,-4},5911}, {{20,-4},5911}, {{24,-8},5911}, {{32,-8},5911},
        {{38,-16},5911}, {{42,-16},5911}, {{44,-20},5911}, {{46,-20},5912}, {{47,-22},5912}, {{48,-22},5913},
    },
    { /* 14, 0x12e5e8 */
        {{-8,0},5919}, {{-8,-10},5920}, {{-8,-18},5921}, {{-8,-26},5914}, {{-8,-30},5915}, {{-8,-32},5916},
        {{-8,-31},5917}, {{-8,-32},5918}, {{-8,-30},5919}, {{-8,-28},5920}, {{-8,-27},5921}, {{-8,-26},5914},
    },
    { /* 15, 0x12e648 */
        {{0,0},5907}, {{0,-6},5908}, {{0,-12},5909}, {{0,-15},5902}, {{0,-16},5903}, {{0,-15},5904},
        {{0,-14},5905}, {{0,-14},5906}, {{0,-12},5907}, {{0,-12},5908}, {{0,-10},5909}, {{0,-10},5910},
    },
    { /* 16, 0x12e6a8 */
        {{0,-4},5932}, {{0,-4},5932}, {{0,-8},5932}, {{0,-8},5932}, {{0,-12},5932}, {{-2,-12},5932},
        {{-5,-11},5932}, {{-7,-11},5932}, {{-9,-9},5932}, {{-10,-9},5932}, {{-11,-8},5924}, {{-11,-8},5924},
    },
    { /* 17, 0x12e708 */
        {{0,0},5930}, {{0,0},5930}, {{0,0},5930}, {{16,-2},5930}, {{16,-4},5930}, {{17,2},5930},
        {{17,4},5930}, {{18,8},5930}, {{18,8},5930}, {{18,14},5930}, {{18,14},5930}, {{18,12},5922},
    },
    { /* 18, 0x12e768 */
        {{0,4},5911}, {{2,6},5911}, {{4,7},5911}, {{8,9},5911}, {{14,14},5911}, {{17,22},5911},
        {{20,26},5911}, {{22,29},5911}, {{24,31},5911}, {{26,33},5912}, {{27,35},5912}, {{28,36},5913},
    },
    { /* 19, 0x12e7c8 */
        {{0,8},5911}, {{2,6},5911}, {{4,5},5911}, {{8,3},5911}, {{14,-2},5911}, {{17,-10},5911},
        {{20,-14},5911}, {{22,-17},5911}, {{24,-19},5911}, {{26,-21},5912}, {{27,-23},5912}, {{28,-24},5913},
    },
    { /* 20, 0x12e8e8 */
        {{0,0},5932}, {{0,0},5932}, {{0,0},5932}, {{0,0},5932}, {{0,-2},5932}, {{2,-1},5932},
        {{5,1},5932}, {{7,2},5932}, {{9,4},5932}, {{10,4},5932}, {{11,5},5924}, {{11,5},5924},
    },
    { /* 21, 0x12e948 */
        {{0,0},5930}, {{0,0},5930}, {{0,0},5930}, {{-16,-2},5930}, {{-17,-4},5930}, {{-18,-6},5930},
        {{-18,-10},5930}, {{-19,-14},5930}, {{-19,-14},5930}, {{-20,-15},5930}, {{-20,-15},5930}, {{-20,-16},5922},
    },
};

static const uint8_t crane_sequences[Z_CRANE_SITES][6] = {
    {1, 2, 3, 4, 5, 6},
    {1, 2, 7, 4, 5, 8},
    {9, 10, 5, 11},
    {12, 13, 14, 15, 16, 17},
    {18, 19, 20, 21},
};

static bool compose_pieces(const zmap_t *map, spritesheet_t *sheet, int lump,
                           const zpiece_t *pieces, int count, ivec2_t anchor, bool centered) {
    irect_t bounds = {anchor.x,anchor.y,1,1};
    for (int i = 0; i < count; ++i) {
        isize2_t size;
        if (!Z_SpriteSize(map, pieces[i].sprite, &size)) return false;
        irect_t rect = {pieces[i].at.x,pieces[i].at.y,size.w,size.h};
        SDL_UnionRect(&bounds, &rect, &bounds);
    }
    if (centered) {
        /* The no-blockmap renderer places the canvas midpoint at the mobj. */
        irect_t opposite = {2 * anchor.x - bounds.x - bounds.w, 2 * anchor.y - bounds.y - bounds.h,
                            bounds.w, bounds.h};
        SDL_UnionRect(&bounds, &opposite, &bounds);
    }
    isize2_t size = {bounds.w,bounds.h};
    uint8_t *out = calloc((size_t)size.w * size.h, 1);
    if (!put_lump(sheet, lump, out, size)) return false;
    ivec2_t top_left = {bounds.x,bounds.y};
    sheet->cells[lump].ground_point = ivec2_sub(anchor, top_left);
    if (size.w > sheet->frame_size.w) sheet->frame_size.w = size.w;
    if (size.h > sheet->frame_size.h) sheet->frame_size.h = size.h;
    for (int i = 0; i < count; ++i)
        if (!blit(map, pieces[i].sprite, out, size, ivec2_sub(pieces[i].at, top_left))) return false;
    return true;
}

/* 0x3e005 plays the WARDATA scripts backwards to frame 1, using
 * 0x1430b0 to resolve their world-relative spark sprite indices. */
static bool load_bridge_rebuild(const zmap_t *map, const uint32_t palette[256], spritesheet_t *sheet) {
    static const int spark_base[] = {1138,1186,1174,1150,1162};
    int count = Z_BRIDGE_SCRIPTS * Z_BRIDGE_REBUILD_FRAMES;
    if (!R_AllocSpriteCells(sheet, count) || !R_InitSpriteDef(sheet, count, 1)) return false;
    for (int i = 0; i < Z_BRIDGE_SCRIPTS; ++i) {
        const uint8_t *script = map->bridge_scripts[i].bytes;
        ivec2_t anchor = {read_u16_le(script + 6),read_u16_le(script + 8)};
        for (int f = 0; f < Z_BRIDGE_REBUILD_FRAMES; ++f) {
            zpiece_t pieces[4];
            int used = 0, native_frame = script[0xb] - 1 - f;
            if (native_frame > 0) {
                const uint8_t *r = script + 12 + native_frame * 4 * 10;
                for (int layer = 0; layer < 4; ++layer, r += 10) {
                    ivec2_t at = {read_u16_le(r),read_u16_le(r + 2)};
                    int sprite = read_u16_le(r + 4);
                    if (!at.x && !at.y && !sprite) continue;
                    if (sprite <= 16) sprite += spark_base[map->world];
                    pieces[used++] = (zpiece_t){at, sprite};
                }
            }
            int lump = i * Z_BRIDGE_REBUILD_FRAMES + f;
            if (!compose_pieces(map, sheet, lump, pieces, used, anchor, true) ||
                !R_InstallSpriteLump(sheet, lump, 0, lump, false)) return false;
        }
    }
    return install_teams(sheet, palette);
}

/* 0x24a2c draws the work layers at the recorded unit top-left, then
 * 0x7ea2b queues the hull, the backward-facing arm and the hook. */
static bool crane_picture(const zmap_t *map, spritesheet_t *sheet, int lump, int site, int frame, int dir) {
    const zpart_t *parts = vehicles[find_vehicle("CRANE")].parts;
    bool stow = frame >= Z_CRANE_POSES && frame < Z_CRANE_WORK_FRAME;
    int pose = frame % Z_CRANE_POSES, hook = 0, variant = 0;
    if (frame >= Z_CRANE_WORK_FRAME) {
        pose = Z_CRANE_POSES - 1;
        hook = (frame - Z_CRANE_WORK_FRAME) / Z_CRANE_VARIANTS;
        variant = (frame - Z_CRANE_WORK_FRAME) % Z_CRANE_VARIANTS;
    }
    zpiece_t pieces[9];
    int count = 0;
    for (int i = 0; i < 6 && crane_sequences[site][i]; ++i) {
        zpiece_t piece = crane_layers[crane_sequences[site][i] - 1][pose];
        switch (piece.sprite) {
        case 5922: piece.sprite += variant / 6; break;
        case 5924: piece.sprite += variant % 6; break;
        case 5930: if (stow) piece.sprite = 5932; break;
        case 5932: if (stow) piece.sprite = 5930; break;
        }
        pieces[count++] = piece;
    }
    ivec2_t origin = {-6,-6};
    int arm = (dir + 4) & 7;
    const int8_t *boom_at = parts[CRANE_ARM].at[arm], *hook_at = parts[CRANE_FRONT_HOOK].at[arm];
    zpiece_t boom = {ivec2_add(origin, (ivec2_t){boom_at[0],boom_at[1]}), 5818 + arm};
    zpiece_t cable = {ivec2_add(origin, (ivec2_t){hook_at[0],hook_at[1]}), 5826 + hook};
    bool rear = dir >= 3 && dir <= 5;
    if (rear) {
        cable.at = ivec2_add(cable.at, (ivec2_t){-6,3});
        pieces[count++] = cable;
    }
    pieces[count++] = (zpiece_t){origin, 5794 + 3 * dir};
    pieces[count++] = boom;
    if (!rear) pieces[count++] = cable;

    return compose_pieces(map, sheet, lump, pieces, count, (ivec2_t){Z_TILE / 2,Z_TILE / 2}, false);
}

static bool load_crane_work(const zmap_t *map, const uint32_t palette[256], spritesheet_t *sheet) {
    int frames = Z_CRANE_SITES * Z_CRANE_SITE_FRAMES;
    if (!R_AllocSpriteCells(sheet, (8 + 2 * (Z_CRANE_SITES - 1)) * Z_CRANE_SITE_FRAMES) ||
        !R_InitSpriteDef(sheet, frames, 8)) return false;
    int lump = 0;
    for (int f = 0; f < frames; ++f) {
        int site = f / Z_CRANE_SITE_FRAMES;
        int dirs = site == Z_CRANE_BUILDING ? 8 : 2;
        for (int d = 0; d < dirs; ++d)
            if (!crane_picture(map, sheet, lump + d, site, f % Z_CRANE_SITE_FRAMES,
                               site == Z_CRANE_BUILDING ? d : 2 * (site - Z_CRANE_SOUTH) + d)) return false;
        for (int d = 0; d < 8; ++d)
            if (!R_InstallSpriteLump(sheet, f, (8 - d) % 8, lump + d % dirs, false)) return false;
        lump += dirs;
    }
    return install_teams(sheet, palette);
}

/* Grow [lo, hi) over the part pictures of the hull (or, with `turret`, the
 * turret) at `origin` in `poses` frames, `frames` of them moving. */
static bool part_bounds(const zmap_t *map, const zpart_t *parts, bool turret, int poses, int frames,
                        ivec2_t origin, ivec2_t *lo, ivec2_t *hi) {
    isize2_t size;
    for (int d = 0; d < 8; ++d)
        for (int f = 0; f < poses; ++f)
            for (int p = 0; p < 4 && part_used(&parts[p]); ++p) {
                int sprite = part_sprite(&parts[p], d, f, frames);
                if (parts[p].turret != turret || sprite < 0) continue;
                if (!Z_SpriteSize(map, sprite, &size)) return false;
                ivec2_t at = ivec2_add(origin, (ivec2_t){parts[p].at[d][0], parts[p].at[d][1]});
                if (at.x < lo->x) lo->x = at.x;
                if (at.y < lo->y) lo->y = at.y;
                if (at.x + size.w > hi->x) hi->x = at.x + size.w;
                if (at.y + size.h > hi->y) hi->y = at.y + size.h;
            }
    return true;
}

/* Draw the hull's (or turret's) parts facing `d` in frame `f`. */
static bool blit_parts(const zmap_t *map, const zpart_t *parts, bool turret, int d, int f, int frames,
                       ivec2_t at, uint8_t *out, isize2_t canvas, irect_t *bounds) {
    *bounds = (irect_t){0};
    for (int p = 0; p < 4 && part_used(&parts[p]); ++p) {
        int sprite = part_sprite(&parts[p], d, f, frames);
        ivec2_t to = ivec2_add(at, (ivec2_t){parts[p].at[d][0], parts[p].at[d][1]});
        if (parts[p].turret != turret || sprite < 0) continue;
        isize2_t size;
        if (!Z_SpriteSize(map, sprite, &size) || !blit(map, sprite, out, canvas, to)) return false;
        irect_t rect = {to.x, to.y, size.w, size.h};
        SDL_UnionRect(bounds, &rect, bounds);
    }
    return true;
}

/* Eight directions of `frames` moving and `idle` standing hull pictures,
 * then the wreck centred on the unit's cell. All share one canvas so they
 * share a ground point, the middle of the unit's 16-pixel cell, and each
 * marks where its turret goes. */
static bool load_vehicle(const zmap_t *map, const char *name, int frames, int idle, int dead,
                         const uint32_t palette[256], spritesheet_t *sheet) {
    int v = find_vehicle(name);
    if (v < 0) return false;
    const zpart_t *parts = vehicles[v].parts;
    ivec2_t origin = {-6, -vehicles[v].lift};
    int poses = frames + idle;
    isize2_t size;
    if (!Z_SpriteSize(map, dead, &size)) return false;
    ivec2_t wreck = {Z_TILE / 2 - size.w / 2, Z_TILE / 2 - size.h / 2};
    ivec2_t lo = wreck, hi = {wreck.x + size.w, wreck.y + size.h};
    if (!part_bounds(map, parts, false, poses, frames, origin, &lo, &hi)) return false;
    isize2_t canvas = {hi.x - lo.x, hi.y - lo.y};
    if (!R_AllocSpriteCells(sheet, poses * 8 + 1) || !R_InitSpriteDef(sheet, poses + 1, 8)) return false;
    for (int lump = 0; lump <= poses * 8; ++lump) {
        uint8_t *out = calloc((size_t)canvas.w * canvas.h, 1);
        if (!out || !put_lump(sheet, lump, out, canvas)) { free(out); return false; }
        int d = lump % 8, f = lump / 8;
        if (f == poses) {
            spriteframe_t *def = &sheet->spritedef.spriteframes[poses];
            free(def->directions);
            def->directions = NULL;
            def->rotations = 0;
            if (!R_AllocSpriteDirections(def, 1) || !blit(map, dead, out, canvas, ivec2_sub(wreck, lo)) ||
                !R_InstallSpriteLump(sheet, poses, 0, lump, false)) return false;
            continue;
        }
        if (!blit_parts(map, parts, false, d, f, frames, ivec2_sub(origin, lo), out, canvas,
                        &sheet->cells[lump].bounds)) return false;
        sheet->cells[lump].mount = ivec2_sub(
            ivec2_add(origin, (ivec2_t){vehicles[v].mount[d][0], vehicles[v].mount[d][1]}), lo);
        /* Renderer slots run counterclockwise from north. */
        if (!R_InstallSpriteLump(sheet, f, (8 - d) % 8, lump, false)) return false;
    }
    place_cells(sheet, (ivec2_t){Z_TILE / 2 - lo.x, Z_TILE / 2 - lo.y});
    return install_teams(sheet, palette);
}

/* Eight directions of the turret parts, in as many frames as they fire
 * through, grounded on the hull's mount point. */
static bool load_turret(const zmap_t *map, const char *name, const uint32_t palette[256],
                        spritesheet_t *sheet) {
    int v = find_vehicle(name), frames = 1;
    if (v < 0) return false;
    const zpart_t *parts = vehicles[v].parts;
    for (int p = 0; p < 4 && part_used(&parts[p]); ++p)
        if (parts[p].turret && parts[p].cycle > frames) frames = parts[p].cycle;
    ivec2_t lo = {0, 0}, hi = {0, 0};
    if (!part_bounds(map, parts, true, frames, frames, (ivec2_t){0, 0}, &lo, &hi) ||
        hi.x <= lo.x || hi.y <= lo.y) return false;
    isize2_t canvas = {hi.x - lo.x, hi.y - lo.y};
    if (!R_AllocSpriteCells(sheet, frames * 8) || !R_InitSpriteDef(sheet, frames, 8)) return false;
    for (int lump = 0; lump < frames * 8; ++lump) {
        uint8_t *out = calloc((size_t)canvas.w * canvas.h, 1);
        if (!out || !put_lump(sheet, lump, out, canvas)) { free(out); return false; }
        int t = lump % 8, f = lump / 8;
        if (!blit_parts(map, parts, true, t, f, frames, ivec2_sub((ivec2_t){0, 0}, lo), out, canvas,
                        &sheet->cells[lump].bounds) ||
            !R_InstallSpriteLump(sheet, f, (8 - t) % 8, lump, false)) return false;
    }
    place_cells(sheet, (ivec2_t){-lo.x, -lo.y});
    return install_teams(sheet, palette);
}

/* Sheets a unit kind loads beyond SPRITES.RSC and the robots. */
#define LOAD_ROBOT(type, sprite, turn, frames, idle, dead)
#define LOAD_STILL(type, sprite, turn, frames, idle, dead)
#define LOAD_GUN(type, sprite, turn, frames, idle, dead) \
    if (!strcmp(name, #type)) \
        return load_runs(map, (const zrun_t[]){{sprite, turn, frames}, {dead, 0, 1}}, 2, palette, sheet);
#define LOAD_VEHICLE(type, sprite, turn, frames, idle, dead) \
    if (!strcmp(name, #type)) return load_vehicle(map, #type, frames, idle, dead, palette, sheet); \
    if (!strcmp(name, #type "_TURRET")) return load_turret(map, #type, palette, sheet);

static bool load_named(const zmap_t *map, const char *name, const uint32_t palette[256],
                       spritesheet_t *sheet) {
    if (!strcmp(name, sprnames[SPR_RSC])) return load_rsc(map, palette, sheet);
    if (!strcmp(name, "ROBOT")) {
        /* Actions 0, 2 and 5 of 0x14b10c, the burst (290..295), the
         * grenade throw (0x6360e: 2522 + direction * 4 + frame) and the
         * cheer. */
        static const zrun_t robot[] = {{66, 1, 1}, {74, 4, 4}, {127, 5, 5}, {290, 0, Z_ROBOT_DIE_FRAMES},
                                       {2522, 4, Z_ROBOT_THROW_FRAMES}, {1959, 0, Z_ROBOT_CHEER_FRAMES},
                                       {6022, 3, Z_ROBOT_PYRO_FRAMES}};
        return load_runs(map, robot, 7, palette, sheet);
    }
    if (!strcmp(name, sprnames[SPR_FLAG])) return load_flag(map, palette, sheet);
    if (!strcmp(name, sprnames[SPR_CRANE_WORK])) return load_crane_work(map, palette, sheet);
    if (!strcmp(name, sprnames[SPR_BRIDGE_REBUILD])) return load_bridge_rebuild(map, palette, sheet);
#define Z_UNIT(t, type, label, kind, mobile, hp, dmg, spd, range, reload, sprite, turn, frames, idle, dead, turret) \
    LOAD_##kind(type, sprite, turn, frames, idle, dead)
#include "units.inc"
#undef Z_UNIT
    isize2_t size;
    if (!strcmp(name, sprnames[SPR_FORT])) {
        /* Cells as S_FORT, S_FORT_CPU, S_FORT_WRECK and S_FORT_CPU_WRECK. */
        if (!R_AllocSpriteCells(sheet, 4)) return false;
        for (int cell = 0; cell < 4; ++cell)
            if (!put_lump(sheet, cell, compose_fort(map, cell >= 2, cell & 1, &size), size)) return false;
        return finish_sheet(sheet, palette, (ivec2_t){80, 88});
    }
    int sprite = -1, wreck = -1;
    static const int wrecks[][Z_WORLDS] = {
        [0] = {1879,2825,2964,3097,2739},
        [1] = {1881,2827,2966,3099,2741},
        [4] = {1882,2828,2967,3100,2742},
        [8] = {1880,2826,2965,3098,2740},
    };
#define Z_BUILDING(native, type, label, w, h, ...) \
    if (!strcmp(name, #type)) { sprite = Z_BuildingSprite(native, map->world); wreck = wrecks[native][map->world]; }
#include "buildings.inc"
#undef Z_BUILDING
    if (sprite < 0 || !R_AllocSpriteCells(sheet, 2)) return false;
    uint8_t *pixels = decode_sprite(map, sprite, &size);
    if (!pixels || !put_lump(sheet, 0, pixels, size)) return false;
    pixels = decode_sprite(map, wreck, &size);
    return pixels && put_lump(sheet, 1, pixels, size) && finish_sheet(sheet, palette, (ivec2_t){-1, -1});
}

bool Z_InitSprites(const level_t *map, spritecache_t *cache) {
    const zmap_t *native = map ? map->native_data : NULL;
    uint32_t palette[256];
    if (!native || !cache || !load_palette(native->root, native->world, palette)) return false;
    /* The state sprites, then the turrets units draw over their hulls. */
    const char *names[NUMSPRITES + MT_UNITS_END - MT_UNITS];
    int count = 0;
    for (int i = 0; i < NUMSPRITES; ++i) names[count++] = sprnames[i];
#define Z_UNIT(t, type, label, kind, mobile, hp, dmg, spd, range, reload, sprite, turn, frames, idle, dead, turret) \
    if (turret) names[count++] = #type "_TURRET";
#include "units.inc"
#undef Z_UNIT
    bool ok = true;
    for (int i = 0; i < count; ++i) {
        if (R_CacheFind(cache, names[i])) continue;
        if (cache->count >= MAX_DECORATION_SPRITES) return false;
        cachedsprite_t *entry = &cache->entries[cache->count];
        memset(entry, 0, sizeof(*entry));
        if (!load_named(native, names[i], palette, &entry->sprite)) {
            fprintf(stderr, "z: could not load sprite %s\n", names[i]);
            R_FreeSprite(&entry->sprite);
            ok = false;
            continue;
        }
        snprintf(entry->name, sizeof(entry->name), "%s", names[i]);
        cache->count++;
    }
    return R_BindSprites(cache, gameinfo) && ok;
}

/* BLK: 512 tiles of 16x16, the bank the attribute plane picks first. */
bool Z_LoadAssets(const char *root, const level_t *map, tileset_t *tileset,
                  spritesheet_t *unit_sprite) {
    const zmap_t *native = map ? map->native_data : NULL;
    if (!native) return false;
    char path[1024], name[16];
    M_PathJoin(path, sizeof(path), root, "WARDATA.PAC");
    snprintf(name, sizeof(name), "%s.BLK", map->tileset_name);
    blob_t blk;
    if (!load_palette(root, native->world, tileset->palette) || !Z_ReadPac(path, name, &blk))
        return false;
    if (blk.size != 512 * Z_TILE * Z_TILE) {
        fprintf(stderr, "z: %s in %s is not 512 tiles\n", name, path);
        W_FreeFile(&blk);
        return false;
    }
    tileset->indices = blk.bytes;
    tileset->count = 512;
    tileset->atlas_cols = 32;
    tileset->tile_w = tileset->tile_h = Z_TILE;
    /* The engine's fallback picture: the first object sprite. */
    isize2_t size;
    if (!R_AllocSpriteCells(unit_sprite, 1) ||
        !put_lump(unit_sprite, 0, decode_sprite(native, states[S_LAND_MINE].frame, &size), size) ||
        !finish_sheet(unit_sprite, tileset->palette, (ivec2_t){-1, -1})) {
        R_FreeSprite(unit_sprite);
        R_FreeTileset(tileset);
        return false;
    }
    return true;
}
