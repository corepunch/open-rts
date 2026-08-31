#define _DEFAULT_SOURCE
#include "engine.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DEFAULT_DATA_ROOT "data/REIGN/dark"
#define DEFAULT_UNIT_SPR  "ucfcnst0.spr"

bool load_dark_tileset(SDL_Renderer *renderer, const char *path,
                       const uint32_t palette[256], tileset_t *out);
void dark_reign_add_water_animations(tileset_t *tileset);

/* ── FTG archive ────────────────────────────────────────────────────────── */

typedef struct { char name[28]; int32_t offset; int32_t size; } FtgEntry;
typedef struct { uint8_t *bytes; size_t size; FtgEntry *entries; int count; } FtgArchive;

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
    out->entries = calloc((size_t)count, sizeof(FtgEntry));
    if (!out->entries) { W_FreeFile(&blob); return false; }
    out->bytes = blob.bytes;
    out->size  = blob.size;
    out->count = count;
    for (int i = 0; i < count; ++i) {
        const uint8_t *e = out->bytes + dir_offset + i * 36;
        memcpy(out->entries[i].name, e, 27);
        out->entries[i].name[27] = '\0';
        out->entries[i].offset = read_i32_le(e + 28);
        out->entries[i].size   = read_i32_le(e + 32);
    }
    return true;
}

static void ftg_free(FtgArchive *ftg) {
    free(ftg->bytes); free(ftg->entries);
    memset(ftg, 0, sizeof(*ftg));
}

static const FtgEntry *ftg_find(const FtgArchive *ftg, const char *name) {
    for (int i = 0; i < ftg->count; ++i)
        if (strcasecmp(ftg->entries[i].name, name) == 0) return &ftg->entries[i];
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

typedef struct { int first_anim, last_anim, framerate, hotspots; } SprSection;

static void sprite_sheet_add_linear_sequence(spritesheet_t *sheet, const char *name, int start,
                                             int facings, int length, int tick_ms) {
    if (!sheet || !name || sheet->sequence_count >= MAX_SPRITE_SEQUENCES ||
        facings <= 0 || facings > MAX_SEQUENCE_FACINGS || length <= 0) return;
    spritesequence_t *seq = &sheet->sequences[sheet->sequence_count++];
    memset(seq, 0, sizeof(*seq));
    snprintf(seq->name, sizeof(seq->name), "%s", name);
    seq->facings = facings;
    seq->length  = length;
    seq->frame_stride = 1;
    seq->tick_ms = tick_ms > 0 ? tick_ms : 120;
    for (int i = 0; i < facings; ++i) {
        seq->frame_starts[i]   = start + i * length;
        /* RSPR rotations start at North and advance counter-clockwise in screen
           space (N, NW, W, SW, S, SE, E, NE for 8-rot sprites).  Map each
           frame index back to the clockwise DR direction code accordingly. */
        seq->direction_codes[i] = ((facings - i) % facings) * 16 / facings;
    }
}

static const spritesequence_t *sprite_sheet_find_sequence(const spritesheet_t *sheet, const char *name) {
    if (!sheet || !name) return NULL;
    for (int i = 0; i < sheet->sequence_count; ++i)
        if (strcmp(sheet->sequences[i].name, name) == 0) return &sheet->sequences[i];
    return NULL;
}

static irect_t dark_reign_visible_bounds(const uint32_t *rgba, int atlas_w, irect_t frame) {
    int min_x = frame.w, min_y = frame.h, max_x = -1, max_y = -1;
    for (int y = 0; y < frame.h; ++y) {
        for (int x = 0; x < frame.w; ++x) {
            uint32_t px = rgba[(frame.y + y) * atlas_w + frame.x + x];
            if ((px >> 24) == 0) continue;
            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x > max_x) max_x = x;
            if (y > max_y) max_y = y;
        }
    }
    if (max_x < min_x || max_y < min_y) return (irect_t){ 0, 0, frame.w, frame.h };
    return (irect_t){ min_x, min_y, max_x - min_x + 1, max_y - min_y + 1 };
}

static bool load_dark_sprite(SDL_Renderer *renderer, const uint8_t *data, size_t size,
                             const uint32_t palette[256], spritesheet_t *out) {
    memset(out, 0, sizeof(*out));
    if (size < 32 || (memcmp(data, "RSPR", 4) != 0 && memcmp(data, "SSPR", 4) != 0 &&
                      memcmp(data, "LSPR", 4) != 0)) return false;
    bool shadow = memcmp(data, "SSPR", 4) == 0;
    int version = read_i32_le(data + 4);
    int nanims  = read_i32_le(data + 8);
    int nrots   = read_i32_le(data + 12);
    int szx     = read_i32_le(data + 16);
    int szy     = read_i32_le(data + 20);
    int npics   = read_i32_le(data + 24);
    int nsects  = read_i32_le(data + 28);
    if ((version != 0x0210 && version != 0x0200) || nanims <= 0 || nrots <= 0 ||
        szx <= 0 || szy <= 0 || npics <= 0 || nsects <= 0) return false;

    int off_sections = 32 + 4 * nanims * nrots;
    int off_anims    = off_sections + 16 * nsects;
    int off_picoffs  = off_anims + 4 * nanims;
    int off_bits     = off_picoffs + 8 * npics + 4;
    if (off_bits <= 0 || (size_t)off_bits > size) return false;

    SprSection *sects = calloc((size_t)nsects, sizeof(*sects));
    if (!sects) return false;
    int total_frames = 0;
    for (int s = 0; s < nsects; ++s) {
        const uint8_t *base = data + off_sections + s * 16;
        sects[s].first_anim = read_i32_le(base + 0);
        sects[s].last_anim  = read_i32_le(base + 4);
        sects[s].framerate  = read_i32_le(base + 8);
        sects[s].hotspots   = read_i32_le(base + 12);
        int sf = sects[s].last_anim - sects[s].first_anim + 1;
        if (sf > 0) total_frames += nrots * sf;
    }
    if (total_frames <= 0) { free(sects); return false; }

    int cols    = (int)ceilf(sqrtf((float)total_frames));
    int rows    = (total_frames + cols - 1) / cols;
    int atlas_w = cols * szx;
    int atlas_h = rows * szy;
    uint8_t *indices = calloc((size_t)atlas_w * (size_t)atlas_h, 1);
    if (!indices) { free(sects); return false; }

    int rot_offset = nrots >= 4 ? nrots / 4 : 0;
    int frame_cursor = 0;
    for (int s = 0; s < nsects; ++s) {
        for (int r = 0; r < nrots; ++r) {
            int disk_r = (r + rot_offset) % nrots;
            for (int a = sects[s].first_anim; a <= sects[s].last_anim; ++a) {
                int picindex = a * nrots + disk_r;
                if (picindex < 0 || picindex >= nanims * nrots) goto spr_fail;
                int picnr = read_i32_le(data + 32 + picindex * 4);
                if (picnr < 0 || picnr >= npics) goto spr_fail;
                int poff      = off_picoffs + 8 * picnr;
                int pic_start = read_i32_le(data + poff);
                int pic_end   = read_i32_le(data + poff + 8);
                if (pic_start < 0 || pic_end < pic_start ||
                    (size_t)(off_bits + pic_end) > size) goto spr_fail;
                const uint8_t *compressed = data + off_bits + pic_start;
                size_t comp_size = (size_t)(pic_end - pic_start);
                size_t comp_pos  = 0;
                int ox = (frame_cursor % cols) * szx;
                int oy = (frame_cursor / cols) * szy;
                for (int y = 0; y < szy; ++y) {
                    int x = 0, step = 0;
                    while (x < szx) {
                        if (comp_pos >= comp_size) goto spr_fail;
                        int cnt = compressed[comp_pos++];
                        if (step & 1) cnt &= 0x7f;
                        if (cnt < 0 || x + cnt > szx) goto spr_fail;
                        if (step & 1) {
                            uint8_t *dst = indices + (oy + y) * atlas_w + ox + x;
                            if (shadow) {
                                memset(dst, 47, (size_t)cnt);
                            } else {
                                if (comp_pos + (size_t)cnt > comp_size) goto spr_fail;
                                memcpy(dst, compressed + comp_pos, (size_t)cnt);
                                comp_pos += (size_t)cnt;
                            }
                        }
                        x += cnt; step++;
                    }
                }
                frame_cursor++;
            }
        }
    }

    {
        uint32_t *rgba   = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
        irect_t *frames = calloc((size_t)total_frames, sizeof(irect_t));
        irect_t *bounds = calloc((size_t)total_frames, sizeof(irect_t));
        SDL_Point *ground_points = calloc((size_t)total_frames, sizeof(SDL_Point));
        if (!rgba || !frames || !bounds || !ground_points) {
            free(rgba); free(frames); free(bounds); free(ground_points);
            goto spr_fail;
        }
        V_IndexedToRGBA(rgba, indices, (size_t)atlas_w * (size_t)atlas_h, palette);
        for (int i = 0; i < total_frames; ++i) {
            frames[i].x = (i % cols) * szx; frames[i].y = (i / cols) * szy;
            frames[i].w = szx; frames[i].h = szy;
            bounds[i] = dark_reign_visible_bounds(rgba, atlas_w, frames[i]);
            /* RSPR canvases are authored around the object's world origin.
               OpenDR likewise exposes a zero frame offset and the full canvas
               size; opaque-pixel bounds are not a ground-contact hotspot. */
            ground_points[i] = (SDL_Point){ szx / 2, szy / 2 };
        }
        out->texture    = I_CreateTexture(renderer, rgba, atlas_w, atlas_h, true);
        out->frames     = frames;
        out->frame_bounds = bounds;
        out->frame_ground_points = ground_points;
        out->frame_count = total_frames;
        out->frame_w    = szx;
        out->frame_h    = szy;
        out->rotations  = nrots;
        out->primary_frames_per_rotation = sects[0].last_anim - sects[0].first_anim + 1;
        int sequence_start = 0;
        static const char *section_names[] = { "run", "shoot", "idle", "stand" };
        for (int s = 0; s < nsects && s < (int)(sizeof(section_names)/sizeof(section_names[0])); ++s) {
            int sf = sects[s].last_anim - sects[s].first_anim + 1;
            if (sf > 0) {
                sprite_sheet_add_linear_sequence(out, section_names[s], sequence_start,
                                                 nrots, sf, sects[s].framerate);
                sequence_start += nrots * sf;
            }
        }
        if (!sprite_sheet_find_sequence(out, "stand") && out->sequence_count > 0) {
            const spritesequence_t *source = sprite_sheet_find_sequence(out, "idle");
            if (!source) source = sprite_sheet_find_sequence(out, "run");
            if (!source) source = &out->sequences[out->sequence_count - 1];
            spritesequence_t stand = *source;
            snprintf(stand.name, sizeof(stand.name), "stand");
            if (out->sequence_count < MAX_SPRITE_SEQUENCES)
                out->sequences[out->sequence_count++] = stand;
        }
        free(rgba); free(indices); free(sects);
        return out->texture != NULL;
    }

spr_fail:
    free(indices); free(sects);
    return false;
}

static bool load_unit_sprite(SDL_Renderer *renderer, const char *data_root,
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
        if (entry->offset < 0 || entry->size <= 0 ||
            (size_t)entry->offset + (size_t)entry->size > ftg.size) {
            ftg_free(&ftg); return false;
        }
        bool ok = load_dark_sprite(renderer, ftg.bytes + entry->offset,
                                   (size_t)entry->size, palette, out);
        ftg_free(&ftg);
        return ok;
    }
    if (strncasecmp(sprite_name, "tileset|", 8) != 0)
        fprintf(stderr, "sprite %s not found in Dark Reign FTG archives\n", sprite_name);
    return false;
}

/* ── sprite cache ───────────────────────────────────────────────────────── */

static bool sprite_cache_load_dark_reign(spritecache_t *cache, SDL_Renderer *renderer,
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
    if (!load_unit_sprite(renderer, data_root, tileset_name, name, palette, &entry->sprite)) {
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
                                        const level_t *map, const mobj_t *units,
                                        int unit_count, spritecache_t *cache) {
    memset(cache, 0, sizeof(*cache));
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
        if (!sprite_cache_load_dark_reign(cache, renderer, data_root, map->tileset_name,
                                          dec->shadow_name, sprite_palette, terrain_palette)) ok = false;
        if (!sprite_cache_load_dark_reign(cache, renderer, data_root, map->tileset_name,
                                          dec->sprite_name, sprite_palette, terrain_palette)) ok = false;
        if (!sprite_cache_load_dark_reign(cache, renderer, data_root, map->tileset_name,
                                          dec->sprite2_name, sprite_palette, terrain_palette)) ok = false;
        if (!sprite_cache_load_dark_reign(cache, renderer, data_root, map->tileset_name,
                                          dec->sprite3_name, sprite_palette, terrain_palette)) ok = false;
    }
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *unit = &units[i];
        if (!sprite_cache_load_dark_reign(cache, renderer, data_root, map->tileset_name,
                                          unit->shadow_name, sprite_palette, terrain_palette)) ok = false;
        if (!sprite_cache_load_dark_reign(cache, renderer, data_root, map->tileset_name,
                                          unit->core.sprite_name, sprite_palette, terrain_palette)) ok = false;
    }
    return ok;
}

/* ── plugin asset loader ────────────────────────────────────────────────── */

bool dark_reign_plugin_load_assets(SDL_Renderer *renderer, const char *data_root,
                                   const level_t *map, const char *sprite_name,
                                   tileset_t *tileset, spritesheet_t *unit_sprite) {
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
    if (!load_dark_tileset(renderer, til_path, terrain_palette, tileset)) {
        snprintf(til_path, sizeof(til_path), "%s/graphics/BARREN.TIL", data_root);
        if (!load_dark_tileset(renderer, til_path, terrain_palette, tileset)) return false;
    }
    if (strcasecmp(map->tileset_name, "SNOW") != 0)
        dark_reign_add_water_animations(tileset);

    if (!load_unit_sprite(renderer, data_root, map->tileset_name, sprite_name, sprite_palette, unit_sprite)) {
        fprintf(stderr, "failed to load %s\n", sprite_name);
        R_FreeTileset(tileset);
        return false;
    }
    return true;
}
