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

/* Lumps centred on their mobj, one nondirectional frame each. */
static bool finish_sheet(spritesheet_t *sheet, const uint32_t palette[256], ivec2_t ground) {
    if (!R_InitSpriteDef(sheet, sheet->numlumps, 1)) return false;
    for (int i = 0; i < sheet->numlumps; ++i) {
        spritecell_t *cell = &sheet->cells[i];
        cell->ground_point = ground.x >= 0 ? ground : (ivec2_t){cell->rect.w / 2, cell->rect.h / 2};
        if (cell->rect.w > sheet->frame_size.w) sheet->frame_size.w = cell->rect.w;
        if (cell->rect.h > sheet->frame_size.h) sheet->frame_size.h = cell->rect.h;
        R_InstallSpriteLump(sheet, i, 0, i, false);
    }
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
        isize2_t piece;
        uint8_t *pixels = decode_sprite(map, index, &piece);
        if (!pixels) { free(out); return NULL; }
        for (int y = 0; y < piece.h && parts[p].at.y + y < size->h; ++y)
            for (int x = 0; x < piece.w && parts[p].at.x + x < size->w; ++x) {
                uint8_t c = pixels[y * piece.w + x];
                if (c) out[(parts[p].at.y + y) * size->w + parts[p].at.x + x] = c;
            }
        free(pixels);
    }
    return out;
}

static bool load_named(const zmap_t *map, const char *name, const uint32_t palette[256],
                       spritesheet_t *sheet) {
    if (!strcmp(name, sprnames[SPR_RSC])) return load_rsc(map, palette, sheet);
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
