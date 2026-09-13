#define _DEFAULT_SOURCE
#include "engine.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

bool load_dark_tileset(const char *path, const uint32_t palette[256], tileset_t *out);
void add_water_animations(tileset_t *tileset);

/* ── FTG archive ────────────────────────────────────────────────────────── */

typedef struct { char name[28]; uint8_t offset[4], size[4]; } FtgEntry;
typedef struct { blob_t file; const FtgEntry *entries; int count; } FtgArchive;

static bool ftg_load(const char *path, FtgArchive *out) {
    memset(out, 0, sizeof(*out));
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return false;
    if (blob.size < 12 || memcmp(blob.bytes, "BOTG", 4) != 0) {
        fprintf(stderr, "%s is not a Dark Reign FTG archive\n", path);
        W_FreeFile(&blob); return false;
    }
    int32_t dir_offset = read_i32_le(blob.bytes + 4);
    int32_t count      = read_i32_le(blob.bytes + 8);
    if (dir_offset < 12 || count <= 0 || count > 65536 ||
        (size_t)dir_offset + (size_t)count * 36 > blob.size) {
        fprintf(stderr, "%s has invalid FTG directory\n", path);
        W_FreeFile(&blob); return false;
    }
    out->file = blob;
    out->entries = (const void *)(blob.bytes + dir_offset);
    out->count = count;
    return true;
}

static void ftg_free(FtgArchive *ftg) {
    W_FreeFile(&ftg->file);
    memset(ftg, 0, sizeof(*ftg));
}

static const FtgEntry *ftg_find(const FtgArchive *ftg, const char *name) {
    for (int i = 0; i < ftg->count; ++i)
        if (strlen(name) <= 27 && strncasecmp(ftg->entries[i].name, name, 27) == 0) return &ftg->entries[i];
    return NULL;
}

/* ── palette ────────────────────────────────────────────────────────────── */

static bool load_dark_palette_with_multipliers(const char *path, uint32_t colors[256],
                                               int standard_multiplier, int terrain_multiplier) {
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return false;
    if (blob.size < 8 + 256 * 3 || memcmp(blob.bytes, "PALS", 4) != 0) {
        fprintf(stderr, "%s is not a Dark Reign PALS palette\n", path);
        W_FreeFile(&blob); return false;
    }
    const uint8_t *p = blob.bytes + 8;
    const uint8_t *r = p, *g = p + 256, *b = p + 512;
    for (int i = 0; i < 256; ++i) {
        if (i == 0) { colors[i] = 0x00000000u; continue; }
        int mult = (i < 160 || i == 255) ? standard_multiplier : terrain_multiplier;
        uint8_t rr = (uint8_t)clamp255((int)r[i] * mult + 1);
        uint8_t gg = (uint8_t)clamp255((int)g[i] * mult + 1);
        uint8_t bb = (uint8_t)clamp255((int)b[i] * mult + 1);
        colors[i] = 0xff000000u | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | bb;
    }
    colors[47] = 0x70000000u;
    W_FreeFile(&blob);
    return true;
}

static bool load_dark_sprite_palette(const char *path, uint32_t colors[256]) {
    if (!load_dark_palette_with_multipliers(path, colors, 6, 6)) return false;
    /* SPR art uses the purple authoring ramp (32..39) as its remappable team
       band. Team zero in the shipped campaigns is Freedom Guard orange. */
    for (int i = 0; i < 8; ++i) colors[32 + i] = colors[48 + i];
    return true;
}

static bool load_dark_terrain_palette(const char *path, uint32_t colors[256]) {
    int terrain_multiplier = (strstr(path, "BARREN") || strstr(path, "JUNGLE")) ? 6 : 4;
    return load_dark_palette_with_multipliers(path, colors, 4, terrain_multiplier);
}

/* ── sprite loader ──────────────────────────────────────────────────────── */

typedef struct {
    uint32_t fourcc;
    int32_t  version;
    int32_t  nanims;
    int32_t  nrots;
    int32_t  szx;
    int32_t  szy;
    int32_t  npics;
    int32_t  nsects;
} dr_spr_header_t;

#define DR_SPR_RSPR ((uint32_t)'R'|((uint32_t)'S'<<8)|((uint32_t)'P'<<16)|((uint32_t)'R'<<24))
#define DR_SPR_SSPR ((uint32_t)'S'|((uint32_t)'S'<<8)|((uint32_t)'P'<<16)|((uint32_t)'R'<<24))
#define DR_SPR_LSPR ((uint32_t)'L'|((uint32_t)'S'<<8)|((uint32_t)'P'<<16)|((uint32_t)'R'<<24))

static irect_t visible_bounds_indexed(const uint8_t *indices, int w, int h) {
    int min_x = w, min_y = h, max_x = -1, max_y = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!indices[(size_t)y * w + x]) continue;
            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x > max_x) max_x = x;
            if (y > max_y) max_y = y;
        }
    if (max_x < min_x || max_y < min_y) return (irect_t){ 0, 0, w, h };
    return (irect_t){ min_x, min_y, max_x - min_x + 1, max_y - min_y + 1 };
}

static bool load_dark_sprite(const uint8_t *data, size_t size,
                             const uint32_t palette[256], spritesheet_t *out) {
    memset(out, 0, sizeof(*out));
    if (size < sizeof(dr_spr_header_t)) return false;
    dr_spr_header_t hdr;
    memcpy(&hdr, data, sizeof(hdr));
    uint32_t *fields = (uint32_t *)&hdr;
    for (size_t i = 0; i < sizeof(hdr) / sizeof(uint32_t); ++i)
        fields[i] = SDL_SwapLE32(fields[i]);
    if (hdr.fourcc != DR_SPR_RSPR && hdr.fourcc != DR_SPR_SSPR && hdr.fourcc != DR_SPR_LSPR) return false;
    bool shadow = (hdr.fourcc == DR_SPR_SSPR);
    if ((hdr.version != 0x0210 && hdr.version != 0x0200) || hdr.nanims <= 0 || hdr.nrots <= 0 ||
        hdr.szx <= 0 || hdr.szy <= 0 || hdr.npics <= 0 || hdr.nsects <= 0) return false;

    if (hdr.nrots > MAX_SPRITE_ROTATIONS) return false;
    size_t off_sections = 32 + 4 * (size_t)hdr.nanims * hdr.nrots;
    size_t off_picoffs  = off_sections + 16 * (size_t)hdr.nsects + 4 * (size_t)hdr.nanims;
    size_t off_bits     = off_picoffs + 8 * (size_t)hdr.npics + 4;
    if (off_bits > size) return false;

    int logical_frames = 0;
    for (int s = 0; s < hdr.nsects; ++s) {
        const uint8_t *section = data + off_sections + (size_t)s * 16;
        int first = read_i32_le(section), last = read_i32_le(section + 4);
        if (first < 0 || last < first || last >= hdr.nanims ||
            last - first + 1 > INT_MAX / hdr.nrots - logical_frames) return false;
        logical_frames += last - first + 1;
    }
    if (!R_AllocSpriteCells(out, logical_frames * hdr.nrots) ||
        !R_InitSpriteDef(out, logical_frames, hdr.nrots)) goto fail;
    out->frame_size = (isize2_t){ hdr.szx, hdr.szy };
    if ((size_t)hdr.szx > SIZE_MAX / (size_t)hdr.szy) goto fail;
    size_t pixels = (size_t)hdr.szx * hdr.szy;
    uint8_t *frame_indices = malloc(pixels);
    if (!frame_indices) goto fail;

    memcpy(out->palette,        palette, sizeof(out->palette));
    memcpy(out->source_palette, palette, sizeof(out->source_palette));
    out->indexed = true;

    int rot_offset = hdr.nrots >= 4 ? hdr.nrots / 4 : 0;
    int lump = 0, logical_frame = 0;
    for (int s = 0; s < hdr.nsects; ++s) {
        const uint8_t *section = data + off_sections + (size_t)s * 16;
        int first = read_i32_le(section), last = read_i32_le(section + 4);
        for (int r = 0; r < hdr.nrots; ++r) {
            int disk_r = (r + rot_offset) % hdr.nrots;
            for (int a = first; a <= last; ++a, ++lump) {
                size_t picindex = (size_t)a * hdr.nrots + disk_r;
                int picnr = read_i32_le(data + 32 + picindex * 4);
                if (picnr < 0 || picnr >= hdr.npics) goto decode_fail;
                const uint8_t *picture = data + off_picoffs + 8 * (size_t)picnr;
                int start = read_i32_le(picture), end = read_i32_le(picture + 8);
                if (start < 0 || end < start || (size_t)end > size - off_bits) goto decode_fail;
                const uint8_t *compressed = data + off_bits + start;
                size_t remaining = (size_t)(end - start);
                memset(frame_indices, 0, pixels);
                for (int y = 0; y < hdr.szy; ++y) {
                    int x = 0, step = 0;
                    while (x < hdr.szx) {
                        if (!remaining) goto decode_fail;
                        int count = *compressed++; remaining--;
                        if (step & 1) count &= 0x7f;
                        if (count > hdr.szx - x) goto decode_fail;
                        if (step & 1) {
                            uint8_t *dst = frame_indices + (size_t)y * hdr.szx + x;
                            if (shadow) {
                                memset(dst, 47, count);
                            } else {
                                if ((size_t)count > remaining) goto decode_fail;
                                memcpy(dst, compressed, count);
                                compressed += count; remaining -= count;
                            }
                        }
                        x += count; step++;
                    }
                }
                spritecell_t *cell = &out->cells[lump];
                cell->rect = (irect_t){ 0, 0, hdr.szx, hdr.szy };
                cell->bounds = visible_bounds_indexed(frame_indices, hdr.szx, hdr.szy);
                /* RSPR canvases are centered on the object's world origin. */
                cell->ground_point = (ivec2_t){ hdr.szx / 2, hdr.szy / 2 };
                out->lumps[lump].indices = malloc(pixels);
                if (!out->lumps[lump].indices) goto decode_fail;
                memcpy(out->lumps[lump].indices, frame_indices, pixels);
                R_InstallSpriteLump(out, logical_frame + a - first, r, lump, false);
            }
        }
        logical_frame += last - first + 1;
    }
    free(frame_indices);
    return true;
decode_fail:
    free(frame_indices);
fail:
    R_FreeSprite(out);
    return false;
}

static bool load_unit_sprite(const char *data_root,
                             const char *tileset_name, const char *sprite_name,
                             const uint32_t palette[256], spritesheet_t *out) {
    const char *asset_name = sprite_name;
    int first_archive = 0;
    int last_archive = 1;
    if (strncasecmp(sprite_name, "tileset|", 8) == 0) {
        asset_name = sprite_name + 8;
        last_archive = 0;
    } else if (strncasecmp(sprite_name, "base|", 5) == 0) {
        asset_name = sprite_name + 5;
        first_archive = 1;
    }
    char themed_path[1024], shared_path[1024];
    snprintf(themed_path, sizeof(themed_path), "%s/graphics/%s/SPRITES.FTG", data_root, tileset_name);
    snprintf(shared_path, sizeof(shared_path), "%s/graphics/SPRITES.FTG", data_root);
    const char *archives[2] = { themed_path, shared_path };
    for (int i = first_archive; i <= last_archive; ++i) {
        FtgArchive ftg;
        if (!ftg_load(archives[i], &ftg)) continue;
        const FtgEntry *entry = ftg_find(&ftg, asset_name);
        if (!entry) { ftg_free(&ftg); continue; }
        int offset = read_i32_le(entry->offset), size = read_i32_le(entry->size);
        if (offset < 0 || size <= 0 ||
            (size_t)offset + (size_t)size > ftg.file.size) {
            ftg_free(&ftg); return false;
        }
        bool ok = load_dark_sprite(ftg.file.bytes + offset,
                                   (size_t)size, palette, out);
        ftg_free(&ftg);
        return ok;
    }
    if (strncasecmp(sprite_name, "tileset|", 8) != 0)
        fprintf(stderr, "sprite %s not found in Dark Reign FTG archives\n", sprite_name);
    return false;
}

/* ── sprite cache ───────────────────────────────────────────────────────── */

bool G_LoadMenuSprite(SDL_Renderer *renderer, const char *root,
                      const char *name, spritesheet_t *out) {
    (void)renderer;
    char path[1024];
    uint32_t palette[256];
    M_PathJoin(path, sizeof(path), root, "graphics/BARREN.PAL");
    return load_dark_sprite_palette(path, palette) &&
           load_unit_sprite(root, "BARREN", name, palette, out);
}

static bool sprite_cache_load_dark_reign(spritecache_t *cache,
                                         const char *data_root, const char *tileset_name,
                                         const char *name, const uint32_t sprite_palette[256],
                                         const uint32_t terrain_palette[256]) {
    if (!name || name[0] == '\0') return true;
    if (R_CacheFind(cache, name)) return true;
    if (cache->count >= MAX_DECORATION_SPRITES) {
        fprintf(stderr, "too many decoration sprites; skipped %s\n", name);
        return false;
    }
    cachedsprite_t *entry = &cache->entries[cache->count];
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    const uint32_t *palette = strncasecmp(name, "tileset|", 8) == 0 ?
        terrain_palette : sprite_palette;
    if (!load_unit_sprite(data_root, tileset_name, name, palette, &entry->sprite)) {
        if (strncasecmp(name, "tileset|", 8) == 0) {
            /* Not every building has a terrain-specific underlay. Cache the
               absence so repeated instances do not retry or report failure. */
            cache->count++;
            return true;
        }
        memset(entry, 0, sizeof(*entry)); return false;
    }
    cache->count++;
    return true;
}

bool load_dark_reign_decoration_sprites(SDL_Renderer *renderer, const char *data_root,
                                        const level_t *map, mobj_t *const *units,
                                        int unit_count, spritecache_t *cache) {
    (void)renderer;
    uint32_t sprite_palette[256];
    uint32_t terrain_palette[256];
    char palette_path[1024];
    snprintf(palette_path, sizeof(palette_path), "%s/graphics/BARREN.PAL", data_root);
    if (!load_dark_sprite_palette(palette_path, sprite_palette)) return false;
    snprintf(palette_path, sizeof(palette_path), "%s/graphics/%s.PAL",
             data_root, map->tileset_name);
    if (!load_dark_terrain_palette(palette_path, terrain_palette)) return false;

    bool ok = true;
    for (int i = 0; i < map->decoration_count; ++i) {
        const mapdecoration_t *dec = &map->decorations[i];
        if (!sprite_cache_load_dark_reign(cache, data_root, map->tileset_name,
                                          dec->shadow_name, sprite_palette, terrain_palette)) ok = false;
        if (!sprite_cache_load_dark_reign(cache, data_root, map->tileset_name,
                                          dec->sprite_name, sprite_palette, terrain_palette)) ok = false;
        if (!sprite_cache_load_dark_reign(cache, data_root, map->tileset_name,
                                          dec->sprite2_name, sprite_palette, terrain_palette)) ok = false;
        if (!sprite_cache_load_dark_reign(cache, data_root, map->tileset_name,
                                          dec->sprite3_name, sprite_palette, terrain_palette)) ok = false;
    }
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *unit = units[i];
        const char *shadow_name = unit->info ? unit->info->shadow_name : NULL;
        if (!sprite_cache_load_dark_reign(cache, data_root, map->tileset_name,
                          shadow_name, sprite_palette, terrain_palette)) ok = false;
        if (!sprite_cache_load_dark_reign(cache, data_root, map->tileset_name,
                                          unit->core.sprite_name, sprite_palette, terrain_palette)) ok = false;
    }
    return ok;
}

/* ── plugin asset loader ────────────────────────────────────────────────── */

bool plugin_load_assets(SDL_Renderer *renderer, const char *data_root,
                                   const level_t *map, const char *sprite_name,
                                   tileset_t *tileset, spritesheet_t *unit_sprite) {
    (void)renderer;
    uint32_t terrain_palette[256], sprite_palette[256];
    char palette_path[1024];
    snprintf(palette_path, sizeof(palette_path), "%s/graphics/%s.PAL", data_root, map->tileset_name);
    if (!load_dark_terrain_palette(palette_path, terrain_palette)) {
        snprintf(palette_path, sizeof(palette_path), "%s/graphics/BARREN.PAL", data_root);
        if (!load_dark_terrain_palette(palette_path, terrain_palette)) return false;
    }
    snprintf(palette_path, sizeof(palette_path), "%s/graphics/BARREN.PAL", data_root);
    if (!load_dark_sprite_palette(palette_path, sprite_palette)) return false;

    char til_path[1024];
    snprintf(til_path, sizeof(til_path), "%s/graphics/%s.TIL", data_root, map->tileset_name);
    if (!load_dark_tileset(til_path, terrain_palette, tileset)) {
        snprintf(til_path, sizeof(til_path), "%s/graphics/BARREN.TIL", data_root);
        if (!load_dark_tileset(til_path, terrain_palette, tileset)) return false;
    }
    if (strcasecmp(map->tileset_name, "SNOW") != 0)
        add_water_animations(tileset);

    if (!load_unit_sprite(data_root, map->tileset_name, sprite_name, sprite_palette, unit_sprite)) {
        fprintf(stderr, "failed to load %s\n", sprite_name);
        R_FreeTileset(tileset);
        return false;
    }
    return true;
}
