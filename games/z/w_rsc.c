#define _DEFAULT_SOURCE
#include "z_local.h"
#include "info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fort pieces by world, ZED.EXE 0x117de2; 0x126d9 draws them. */
static const int fort_sprites[Z_WORLDS] = {809, 2127, 2304, 2800, 2613};

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
 * both from the second set (pieces + 4) on the fort without a gate. */
static uint8_t *compose_fort(const zmap_t *map, int variant, isize2_t *size) {
    static const struct { int piece; ivec2_t at; } parts[] = {
        {0, {0, 16}}, {1, {120, 16}}, {2, {0, 0}}, {3, {120, 0}}, {4, {0, 112}}, {5, {120, 112}},
    };
    int base = fort_sprites[map->world];
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
 * by direction & 3 for the `quarter` hulls that look the same both ways;
 * the first part adds the track frame. Turrets turn on their own in
 * ZED.EXE (unit's weapon +0x15); here they face the way the hull does, so
 * the offset tables by turret and by hull are summed per direction. */
typedef struct {
    int sprite, turn;
    bool quarter;
    int8_t at[8][2];
} zpart_t;

static const struct {
    const char *name;
    int lift;
    zpart_t parts[4];
} vehicles[] = {
    /* 6 (0x7e31e): hull 488 (0x14b2c4), turret 513 and barrel 521
     * (0x14b283, 0x14b2b3, 6 right and 3 up), hatch 533 (0x14b314) */
    {"MED_TANK", 0, {
        {488, 3, true, {{0, 0}, {-2, -7}, {-2, 0}, {0, -7}, {0, 0}, {-2, -7}, {-2, 0}, {0, -7}}},
        {513, 1, false, {{6, -3}, {4, -3}, {3, -6}, {4, -6}, {6, -6}, {6, -6}, {6, -6}, {5, -3}}},
        {521, 1, false, {{13, -9}, {24, -3}, {22, -3}, {17, 4}, {12, 5}, {4, 3}, {0, -3}, {3, -7}}},
        {533, 3, false, {{11, -6}, {11, -6}, {11, -6}, {11, -6}, {11, -6}, {11, -6}, {11, -6}, {11, -6}}},
    }},
    /* 0x19 (0x7e49f): hull 5305 (0x14b2d4), turret 5329 (0x14b293), hatch 533 (0x14b334) */
    {"HEAVY_TANK", 6, {
        {5305, 3, true, {{0, 0}}},
        {5329, 1, false, {{-1, -3}, {0, -3}, {4, -1}, {0, -2}, {-1, -1}, {-2, -2}, {-6, -1}, {-2, -3}}},
        {533, 3, false, {{15, 6}, {13, 9}, {8, 8}, {8, 5}, {8, 3}, {9, 0}, {14, 2}, {15, 3}}},
    }},
    /* 0x1e (0x7e5df): hull 5719 (0x14b2e4), turret 5731 (0x14b2a3), hatch 533 (0x14b324) */
    {"LIGHT_TANK", 6, {
        {5719, 3, true, {{-1, 0}, {-4, 0}, {-5, 0}, {-5, 0}, {-1, 0}, {-4, 0}, {-5, 0}, {-5, 0}}},
        {5731, 1, false, {{-3, -2}, {-3, -3}, {-3, -3}, {-3, -3}, {-3, 0}, {-4, -1}, {-3, -1}, {-4, -1}}},
        {533, 3, false, {{9, 4}, {8, 3}, {8, 2}, {8, 2}, {9, 3}, {9, 2}, {9, 2}, {10, 3}}},
    }},
    /* 0x20 (0x7e719): hull 5655, dish 5679 (0x14b354; it spins with the game clock) */
    {"APC", 6, {
        {5655, 3, false, {{0, 0}}},
        {5679, 0, false, {{14, 6}, {8, 12}, {2, 8}, {1, 7}, {4, 4}, {8, 2}, {15, 4}, {16, 6}}},
    }},
    /* 0x22 (0x7e846): body 5590, gun 5639 (0x14b484 by body, 0x14b494 by gun) */
    {"JEEP", 6, {
        {5590, 2, false, {{0, 0}}},
        {5639, 2, false, {{8, 5}, {4, 8}, {1, 2}, {2, 1}, {5, 3}, {15, 1}, {15, 1}, {13, 8}}},
    }},
    /* 0x24 (0x7ea2b): body 5794, arm 5818 (0x14b454); the hook is left out,
     * ZED.EXE reads its second offset from a stray address. */
    {"CRANE", 6, {
        {5794, 3, false, {{0, 0}}},
        {5818, 1, false, {{-1, -9}, {1, -8}, {6, -6}, {3, -4}, {0, -5}, {-4, -4}, {-6, -6}, {-2, -8}}},
    }},
    /* 0x18 (0x7ee27): hull 1845 (0x14b404), pod 1869 (0x1489c8 by pod, 0x148a08 by hull) */
    {"M_MISSILE", 8, {
        {1845, 3, false, {{1, 2}, {2, 0}, {0, -2}, {0, 0}, {-1, 2}, {1, 0}, {0, -2}, {0, 0}}},
        {1869, 1, false, {{4, 1}, {2, 3}, {0, -1}, {0, -2}, {1, 3}, {4, -2}, {8, -4}, {4, 1}}},
    }},
};

static int part_sprite(const zpart_t *part, int dir, int frame) {
    return part->sprite + (part->quarter ? dir & 3 : dir) * part->turn + frame;
}

/* Eight directions of `frames` composed pictures, then the wreck centred
 * on the unit's cell. All share one canvas so they share a ground point:
 * the middle of the unit's 16-pixel cell. */
static bool load_vehicle(const zmap_t *map, const char *name, int frames, int dead,
                         const uint32_t palette[256], spritesheet_t *sheet) {
    size_t v = 0;
    while (v < sizeof(vehicles) / sizeof(*vehicles) && strcmp(vehicles[v].name, name)) ++v;
    if (v == sizeof(vehicles) / sizeof(*vehicles)) return false;
    const zpart_t *parts = vehicles[v].parts;
    ivec2_t origin = {-6, -vehicles[v].lift};
    isize2_t size;
    if (!Z_SpriteSize(map, dead, &size)) return false;
    ivec2_t wreck = {Z_TILE / 2 - size.w / 2, Z_TILE / 2 - size.h / 2};
    ivec2_t lo = wreck, hi = {wreck.x + size.w, wreck.y + size.h};
    for (int d = 0; d < 8; ++d)
        for (int f = 0; f < frames; ++f)
            for (int p = 0; p < 4 && parts[p].sprite; ++p) {
                if (!Z_SpriteSize(map, part_sprite(&parts[p], d, p ? 0 : f), &size)) return false;
                ivec2_t at = ivec2_add(origin, (ivec2_t){parts[p].at[d][0], parts[p].at[d][1]});
                if (at.x < lo.x) lo.x = at.x;
                if (at.y < lo.y) lo.y = at.y;
                if (at.x + size.w > hi.x) hi.x = at.x + size.w;
                if (at.y + size.h > hi.y) hi.y = at.y + size.h;
            }
    isize2_t canvas = {hi.x - lo.x, hi.y - lo.y};
    if (!R_AllocSpriteCells(sheet, frames * 8 + 1) || !R_InitSpriteDef(sheet, frames + 1, 8)) return false;
    for (int lump = 0; lump <= frames * 8; ++lump) {
        uint8_t *out = calloc((size_t)canvas.w * canvas.h, 1);
        if (!out || !put_lump(sheet, lump, out, canvas)) { free(out); return false; }
        int d = lump % 8, f = lump / 8;
        if (f == frames) {
            spriteframe_t *def = &sheet->spritedef.spriteframes[frames];
            free(def->directions);
            def->directions = NULL;
            def->rotations = 0;
            if (!R_AllocSpriteDirections(def, 1) || !blit(map, dead, out, canvas, ivec2_sub(wreck, lo)) ||
                !R_InstallSpriteLump(sheet, frames, 0, lump, false)) return false;
            continue;
        }
        for (int p = 0; p < 4 && parts[p].sprite; ++p) {
            ivec2_t at = ivec2_add(origin, (ivec2_t){parts[p].at[d][0], parts[p].at[d][1]});
            if (!blit(map, part_sprite(&parts[p], d, p ? 0 : f), out, canvas, ivec2_sub(at, lo)))
                return false;
        }
        /* Renderer slots run counterclockwise from north. */
        if (!R_InstallSpriteLump(sheet, f, (8 - d) % 8, lump, false)) return false;
    }
    place_cells(sheet, (ivec2_t){Z_TILE / 2 - lo.x, Z_TILE / 2 - lo.y});
    return install_teams(sheet, palette);
}

/* Sheets a unit kind loads beyond SPRITES.RSC and the robots. */
#define LOAD_ROBOT(type, sprite, turn, frames, dead)
#define LOAD_STILL(type, sprite, turn, frames, dead)
#define LOAD_GUN(type, sprite, turn, frames, dead) \
    if (!strcmp(name, #type)) \
        return load_runs(map, (const zrun_t[]){{sprite, turn, frames}, {dead, 0, 1}}, 2, palette, sheet);
#define LOAD_VEHICLE(type, sprite, turn, frames, dead) \
    if (!strcmp(name, #type)) return load_vehicle(map, #type, frames, dead, palette, sheet);

static bool load_named(const zmap_t *map, const char *name, const uint32_t palette[256],
                       spritesheet_t *sheet) {
    if (!strcmp(name, sprnames[SPR_RSC])) return load_rsc(map, palette, sheet);
    if (!strcmp(name, "ROBOT")) {
        /* Actions 0, 2 and 5 of 0x14b10c, a fall (304..314), the grenade
         * throw (0x6360e: 2522 + direction * 4 + frame) and the cheer. */
        static const zrun_t robot[] = {{66, 1, 1}, {74, 4, 4}, {127, 5, 5}, {304, 0, Z_ROBOT_DIE_FRAMES},
                                       {2522, 4, Z_ROBOT_THROW_FRAMES}, {1959, 0, Z_ROBOT_CHEER_FRAMES}};
        return load_runs(map, robot, 6, palette, sheet);
    }
    if (!strcmp(name, sprnames[SPR_FLAG])) return load_flag(map, palette, sheet);
#define Z_UNIT(t, type, label, kind, mobile, hp, dmg, spd, range, reload, sprite, turn, frames, dead) \
    LOAD_##kind(type, sprite, turn, frames, dead)
#include "units.inc"
#undef Z_UNIT
    isize2_t size;
    if (!strcmp(name, sprnames[SPR_FORT])) {
        if (!R_AllocSpriteCells(sheet, 2)) return false;
        for (int variant = 0; variant < 2; ++variant)
            if (!put_lump(sheet, variant, compose_fort(map, variant, &size), size)) return false;
        return finish_sheet(sheet, palette, (ivec2_t){80, 88});
    }
    int sprite = -1;
#define Z_BUILDING(native, type, label, w, h, ...) \
    if (!strcmp(name, #type)) sprite = Z_BuildingSprite(native, map->world);
#include "buildings.inc"
#undef Z_BUILDING
    return sprite >= 0 && R_AllocSpriteCells(sheet, 1) &&
           put_lump(sheet, 0, decode_sprite(map, sprite, &size), size) &&
           finish_sheet(sheet, palette, (ivec2_t){-1, -1});
}

bool Z_InitSprites(const level_t *map, spritecache_t *cache) {
    const zmap_t *native = map ? map->native_data : NULL;
    uint32_t palette[256];
    if (!native || !cache || !load_palette(native->root, native->world, palette)) return false;
    bool ok = true;
    for (int i = 0; i < NUMSPRITES; ++i) {
        if (R_CacheFind(cache, sprnames[i])) continue;
        if (cache->count >= MAX_DECORATION_SPRITES) return false;
        cachedsprite_t *entry = &cache->entries[cache->count];
        memset(entry, 0, sizeof(*entry));
        if (!load_named(native, sprnames[i], palette, &entry->sprite)) {
            fprintf(stderr, "z: could not load sprite %s\n", sprnames[i]);
            R_FreeSprite(&entry->sprite);
            ok = false;
            continue;
        }
        snprintf(entry->name, sizeof(entry->name), "%s", sprnames[i]);
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
