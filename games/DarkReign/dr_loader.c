#define _DEFAULT_SOURCE
#include "plugin.h"

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

/* ── FTG archive ────────────────────────────────────────────────────────── */

typedef struct { char name[28]; int32_t offset; int32_t size; } FtgEntry;
typedef struct { uint8_t *bytes; size_t size; FtgEntry *entries; int count; } FtgArchive;

static bool ftg_load(const char *path, FtgArchive *out) {
    memset(out, 0, sizeof(*out));
    Blob blob;
    if (!load_blob(path, &blob)) return false;
    if (blob.size < 12 || memcmp(blob.bytes, "BOTG", 4) != 0) {
        fprintf(stderr, "%s is not a Dark Reign FTG archive\n", path);
        free_blob(&blob); return false;
    }
    int32_t dir_offset = read_i32_le(blob.bytes + 4);
    int32_t count      = read_i32_le(blob.bytes + 8);
    if (dir_offset < 12 || count <= 0 || count > 65536 ||
        (size_t)dir_offset + (size_t)count * 36 > blob.size) {
        fprintf(stderr, "%s has invalid FTG directory\n", path);
        free_blob(&blob); return false;
    }
    out->entries = calloc((size_t)count, sizeof(FtgEntry));
    if (!out->entries) { free_blob(&blob); return false; }
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
    Blob blob;
    if (!load_blob(path, &blob)) return false;
    if (blob.size < 8 + 256 * 3 || memcmp(blob.bytes, "PALS", 4) != 0) {
        fprintf(stderr, "%s is not a Dark Reign PALS palette\n", path);
        free_blob(&blob); return false;
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
    free_blob(&blob);
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

/* ── tileset ────────────────────────────────────────────────────────────── */

static void dark_til_combine_add(const uint8_t *a, const uint8_t *b, uint8_t *out) {
    for (int i = 0; i < TILE_PIX_W * TILE_PIX_H; ++i) {
        int v = (int)a[i] + (int)b[i];
        out[i] = (uint8_t)(v > 255 ? 255 : v);
    }
}

static void dark_til_combine_min(const uint8_t *a, const uint8_t *b, uint8_t *out) {
    for (int i = 0; i < TILE_PIX_W * TILE_PIX_H; ++i)
        out[i] = a[i] < b[i] ? a[i] : b[i];
}

static void dark_til_mask_tile(uint32_t *dst, const uint32_t *src, const uint8_t *mask) {
    for (int i = 0; i < TILE_PIX_W * TILE_PIX_H; ++i) {
        uint32_t rgb   = src[i] & 0x00ffffffu;
        uint32_t alpha = (uint32_t)(mask[i] > 63 ? 255 : mask[i] * 4);
        dst[i] = (alpha << 24) | rgb;
    }
}

static void dark_til_shadow_tile(uint32_t *dst, const uint8_t *mask, uint8_t alpha) {
    for (int i = 0; i < TILE_PIX_W * TILE_PIX_H; ++i) {
        uint32_t a = mask ? (mask[i] < alpha ? mask[i] : alpha) : alpha;
        dst[i] = a << 24;
    }
}

bool load_dark_tileset(SDL_Renderer *renderer, const char *path, const uint32_t palette[256],
                       Tileset *out) {
    memset(out, 0, sizeof(*out));
    Blob blob;
    if (!load_blob(path, &blob)) return false;
    if (blob.size < 8 || memcmp(blob.bytes, "TILE", 4) != 0) {
        fprintf(stderr, "%s is not a Dark Reign TILE tileset\n", path);
        free_blob(&blob); return false;
    }
    size_t tile_bytes = (size_t)read_u32_le(blob.bytes + 4);
    if (tile_bytes != TILE_PIX_W * TILE_PIX_H || blob.size < 8 + tile_bytes) {
        fprintf(stderr, "%s has unsupported tile dimensions/count\n", path);
        free_blob(&blob); return false;
    }

    const int max_frames = 1103;
    uint32_t *frame_pixels = calloc((size_t)max_frames * TILE_PIX_W * TILE_PIX_H, sizeof(uint32_t));
    uint8_t *mask_frames   = calloc((size_t)256 * TILE_PIX_W * TILE_PIX_H, sizeof(uint8_t));
    if (!frame_pixels || !mask_frames) {
        free(frame_pixels); free(mask_frames); free_blob(&blob); return false;
    }

    int count = 0;
    size_t pos = 8;
    const size_t normal_chunk = 1 + tile_bytes;
#define ADD_INDEXED_FRAME(SRC) do { \
    if (count < max_frames) { \
        indexed_to_rgba(frame_pixels + (size_t)count * tile_bytes, (SRC), tile_bytes, palette); \
        count++; \
    } \
} while (0)

    for (int i = 0; i < 128 && pos + normal_chunk <= blob.size; ++i) {
        ADD_INDEXED_FRAME(blob.bytes + pos + 1); pos += normal_chunk;
    }
    for (int i = 0; i < 64 && pos + tile_bytes <= blob.size; ++i) {
        ADD_INDEXED_FRAME(blob.bytes + pos); pos += tile_bytes;
    }
    for (int art = 0; art < 4 && pos < blob.size; ++art) {
        pos += normal_chunk;
        for (int shore = 0; shore < 14 && pos + normal_chunk <= blob.size; ++shore) {
            ADD_INDEXED_FRAME(blob.bytes + pos + 1); pos += normal_chunk;
        }
        pos += normal_chunk;
    }
    for (int mask = 0; mask < 256 && pos + tile_bytes <= blob.size; ++mask) {
        if (mask % 4 == 0 && pos + 4 <= blob.size) pos += 4;
        if (pos + tile_bytes > blob.size) break;
        memcpy(mask_frames + (size_t)mask * tile_bytes, blob.bytes + pos, tile_bytes);
        pos += tile_bytes;
    }
    pos += 4 * tile_bytes;

    const int corner_sets[4][4] = {
        { 0, 1, 2, 3 }, { 64, 65, 66, 67 }, { 128, 129, 130, 131 }, { 192, 193, 194, 195 },
    };
    uint8_t north[TILE_PIX_W*TILE_PIX_H], east[TILE_PIX_W*TILE_PIX_H];
    uint8_t south[TILE_PIX_W*TILE_PIX_H], west[TILE_PIX_W*TILE_PIX_H];
    uint8_t ne_inner[TILE_PIX_W*TILE_PIX_H], nw_inner[TILE_PIX_W*TILE_PIX_H];
    uint8_t sw_inner[TILE_PIX_W*TILE_PIX_H], se_inner[TILE_PIX_W*TILE_PIX_H];
    uint8_t ne_sw_bridge[TILE_PIX_W*TILE_PIX_H], nw_se_bridge[TILE_PIX_W*TILE_PIX_H];
    const uint8_t *masks[14];

    for (int set = 0; set < 4; ++set) {
        const uint8_t *se = mask_frames + (size_t)corner_sets[set][0] * tile_bytes;
        const uint8_t *sw = mask_frames + (size_t)corner_sets[set][1] * tile_bytes;
        const uint8_t *nw = mask_frames + (size_t)corner_sets[set][2] * tile_bytes;
        const uint8_t *ne = mask_frames + (size_t)corner_sets[set][3] * tile_bytes;
        dark_til_combine_add(nw, ne, north); dark_til_combine_add(se, ne, east);
        dark_til_combine_add(sw, se, south); dark_til_combine_add(sw, nw, west);
        dark_til_combine_add(north, east, ne_inner); dark_til_combine_add(north, west, nw_inner);
        dark_til_combine_add(south, west, sw_inner); dark_til_combine_add(south, east, se_inner);
        dark_til_combine_min(ne_inner, sw_inner, ne_sw_bridge);
        dark_til_combine_min(nw_inner, se_inner, nw_se_bridge);
        masks[0]=se; masks[1]=sw; masks[2]=nw; masks[3]=ne;
        masks[4]=south; masks[5]=west; masks[6]=north; masks[7]=east;
        masks[8]=se_inner; masks[9]=sw_inner; masks[10]=nw_inner; masks[11]=ne_inner;
        masks[12]=ne_sw_bridge; masks[13]=nw_se_bridge;
        for (int tile_index = 2; tile_index < 16; ++tile_index) {
            const uint32_t *source = frame_pixels + (size_t)(tile_index * 8) * tile_bytes;
            for (int mask = 0; mask < 14 && count < max_frames; ++mask) {
                dark_til_mask_tile(frame_pixels + (size_t)count * tile_bytes, source, masks[mask]);
                count++;
            }
        }
    }

    const int sea_tiles[14] = { 4, 9, 18, 35, 12, 25, 50, 36, 44, 28, 57, 52, 20, 41 };
    const int sea_tile_masks[14][4] = {
        {192,206,220,234},{193,207,221,235},{195,209,223,237},{199,213,227,241},
        {194,208,222,236},{197,211,225,239},{203,217,231,245},{200,214,228,242},
        {202,216,230,244},{198,212,226,240},{205,219,233,247},{204,218,232,246},
        {196,210,224,238},{201,215,229,243},
    };
    for (int mask_index = 0; mask_index < 14; ++mask_index) {
        const uint8_t *mask = mask_frames + (size_t)sea_tiles[mask_index] * tile_bytes;
        for (int sea = 0; sea < 4 && count < max_frames; ++sea) {
            const uint32_t *source = frame_pixels + (size_t)sea_tile_masks[mask_index][sea] * tile_bytes;
            dark_til_mask_tile(frame_pixels + (size_t)count * tile_bytes, source, mask);
            count++;
        }
    }

    const int shadow_set[4] = { 64, 65, 66, 67 };
    {
        const uint8_t *se2 = mask_frames + (size_t)shadow_set[0] * tile_bytes;
        const uint8_t *sw2 = mask_frames + (size_t)shadow_set[1] * tile_bytes;
        const uint8_t *nw2 = mask_frames + (size_t)shadow_set[2] * tile_bytes;
        const uint8_t *ne2 = mask_frames + (size_t)shadow_set[3] * tile_bytes;
        dark_til_combine_add(nw2, ne2, north); dark_til_combine_add(se2, ne2, east);
        dark_til_combine_add(sw2, se2, south); dark_til_combine_add(sw2, nw2, west);
        dark_til_combine_add(north, east, ne_inner); dark_til_combine_add(north, west, nw_inner);
        dark_til_combine_add(south, west, sw_inner); dark_til_combine_add(south, east, se_inner);
        dark_til_combine_min(ne_inner, sw_inner, ne_sw_bridge);
        dark_til_combine_min(nw_inner, se_inner, nw_se_bridge);
        masks[0]=north; masks[1]=east; masks[2]=south; masks[3]=west;
        masks[4]=nw2; masks[5]=ne2; masks[6]=sw2; masks[7]=se2;
        masks[8]=sw_inner; masks[9]=se_inner; masks[10]=nw_inner; masks[11]=ne_inner;
        masks[12]=nw_se_bridge; masks[13]=ne_sw_bridge;
        for (int i = 0; i < 14 && count < max_frames; ++i) {
            dark_til_shadow_tile(frame_pixels + (size_t)count * tile_bytes, masks[i], 35);
            count++;
        }
        if (count < max_frames) {
            dark_til_shadow_tile(frame_pixels + (size_t)count * tile_bytes, NULL, 35);
            count++;
        }
    }
#undef ADD_INDEXED_FRAME

    int rows = (count + TILE_ATLAS_COLS - 1) / TILE_ATLAS_COLS;
    int atlas_w = TILE_ATLAS_COLS * TILE_PIX_W;
    int atlas_h = rows * TILE_PIX_H;
    uint32_t *rgba = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    if (!rgba) { free(frame_pixels); free(mask_frames); free_blob(&blob); return false; }

    for (int tile = 0; tile < count; ++tile) {
        int tx = (tile % TILE_ATLAS_COLS) * TILE_PIX_W;
        int ty = (tile / TILE_ATLAS_COLS) * TILE_PIX_H;
        const uint32_t *tile_src = frame_pixels + (size_t)tile * tile_bytes;
        for (int y = 0; y < TILE_PIX_H; ++y)
            for (int x = 0; x < TILE_PIX_W; ++x)
                rgba[(ty + y) * atlas_w + tx + x] = tile_src[y * TILE_PIX_W + x];
    }
    out->texture   = rgba_texture(renderer, rgba, atlas_w, atlas_h, false);
    out->count     = count;
    out->atlas_cols = TILE_ATLAS_COLS;
    out->tile_w    = TILE_PIX_W;
    out->tile_h    = TILE_PIX_H;
    out->draw_y_offset = 0;
    free(rgba); free(frame_pixels); free(mask_frames); free_blob(&blob);
    return out->texture != NULL;
}

static void dark_reign_add_water_animations(Tileset *tileset) {
    const int frame_ms = 180;
    for (int variation = 0; variation < 8 && variation < tileset->count; ++variation) {
        int frames[4] = { variation, (variation+1)&7, (variation+2)&7, (variation+1)&7 };
        tileset_add_animation(tileset, variation, frames, 4, frame_ms);
    }
    for (int group = 0; group < 14; ++group) {
        int base = 1032 + group * 4;
        if (base + 3 >= tileset->count) break;
        int frames[4] = { base, base+1, base+2, base+3 };
        for (int i = 0; i < 4; ++i) {
            int rotated[4] = { frames[i], frames[(i+1)&3], frames[(i+2)&3], frames[(i+3)&3] };
            tileset_add_animation(tileset, frames[i], rotated, 4, frame_ms);
        }
    }
}

/* ── sprite loader ──────────────────────────────────────────────────────── */

typedef struct { int first_anim, last_anim, framerate, hotspots; } SprSection;

static void sprite_sheet_add_linear_sequence(SpriteSheet *sheet, const char *name, int start,
                                             int facings, int length, int tick_ms) {
    if (!sheet || !name || sheet->sequence_count >= MAX_SPRITE_SEQUENCES ||
        facings <= 0 || facings > MAX_SEQUENCE_FACINGS || length <= 0) return;
    SpriteSequence *seq = &sheet->sequences[sheet->sequence_count++];
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

static const SpriteSequence *sprite_sheet_find_sequence(const SpriteSheet *sheet, const char *name) {
    if (!sheet || !name) return NULL;
    for (int i = 0; i < sheet->sequence_count; ++i)
        if (strcmp(sheet->sequences[i].name, name) == 0) return &sheet->sequences[i];
    return NULL;
}

static SDL_Rect dark_reign_visible_bounds(const uint32_t *rgba, int atlas_w, SDL_Rect frame) {
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
    if (max_x < min_x || max_y < min_y) return (SDL_Rect){ 0, 0, frame.w, frame.h };
    return (SDL_Rect){ min_x, min_y, max_x - min_x + 1, max_y - min_y + 1 };
}

static bool load_dark_sprite(SDL_Renderer *renderer, const uint8_t *data, size_t size,
                             const uint32_t palette[256], SpriteSheet *out) {
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
        SDL_Rect *frames = calloc((size_t)total_frames, sizeof(SDL_Rect));
        SDL_Rect *bounds = calloc((size_t)total_frames, sizeof(SDL_Rect));
        SDL_Point *ground_points = calloc((size_t)total_frames, sizeof(SDL_Point));
        if (!rgba || !frames || !bounds || !ground_points) {
            free(rgba); free(frames); free(bounds); free(ground_points);
            goto spr_fail;
        }
        indexed_to_rgba(rgba, indices, (size_t)atlas_w * (size_t)atlas_h, palette);
        for (int i = 0; i < total_frames; ++i) {
            frames[i].x = (i % cols) * szx; frames[i].y = (i / cols) * szy;
            frames[i].w = szx; frames[i].h = szy;
            bounds[i] = dark_reign_visible_bounds(rgba, atlas_w, frames[i]);
            /* RSPR canvases are authored around the object's world origin.
               OpenDR likewise exposes a zero frame offset and the full canvas
               size; opaque-pixel bounds are not a ground-contact hotspot. */
            ground_points[i] = (SDL_Point){ szx / 2, szy / 2 };
        }
        out->texture    = rgba_texture(renderer, rgba, atlas_w, atlas_h, true);
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
            const SpriteSequence *source = sprite_sheet_find_sequence(out, "idle");
            if (!source) source = sprite_sheet_find_sequence(out, "run");
            if (!source) source = &out->sequences[out->sequence_count - 1];
            SpriteSequence stand = *source;
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
                             const uint32_t palette[256], SpriteSheet *out) {
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

static bool sprite_cache_load_dark_reign(SpriteCache *cache, SDL_Renderer *renderer,
                                         const char *data_root, const char *tileset_name,
                                         const char *name, const uint32_t sprite_palette[256],
                                         const uint32_t terrain_palette[256]) {
    if (!name || name[0] == '\0') return true;
    if (sprite_cache_find(cache, name)) return true;
    if (cache->count >= MAX_DECORATION_SPRITES) {
        fprintf(stderr, "too many decoration sprites; skipped %s\n", name);
        return false;
    }
    CachedSprite *entry = &cache->entries[cache->count];
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
                                        const GameMap *map, const Mobj *units,
                                        int unit_count, SpriteCache *cache) {
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
        const MapDecoration *dec = &map->decorations[i];
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
        const Mobj *unit = &units[i];
        if (!sprite_cache_load_dark_reign(cache, renderer, data_root, map->tileset_name,
                                          unit->shadow_name, sprite_palette, terrain_palette)) ok = false;
        if (!sprite_cache_load_dark_reign(cache, renderer, data_root, map->tileset_name,
                                          unit->sprite_name, sprite_palette, terrain_palette)) ok = false;
    }
    return ok;
}

/* ── definition-file parser ─────────────────────────────────────────────── */

typedef struct { char *units; char *buildings; char *overlay; char *animate; } DarkReignDefinitions;

static char *load_text_file(const char *path) {
    Blob blob;
    if (!load_blob(path, &blob)) return NULL;
    char *text = malloc(blob.size + 1);
    if (!text) { free_blob(&blob); return NULL; }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';
    free_blob(&blob);
    return text;
}

static void replace_extension(char *dst, size_t dst_size, const char *path, const char *ext) {
    snprintf(dst, dst_size, "%s", path);
    char *dot = strrchr(dst, '.'), *slash = strrchr(dst, '/');
    if (dot && (!slash || dot > slash))
        snprintf(dot, dst_size - (size_t)(dot - dst), "%s", ext);
    else
        strncat(dst, ext, dst_size - strlen(dst) - 1);
}

static void copy_trimmed_token(char *dst, size_t dst_size, const char *src, size_t len) {
    while (len > 0 && isspace((unsigned char)*src)) { src++; len--; }
    while (len > 0 && isspace((unsigned char)src[len-1])) len--;
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len); dst[len] = '\0';
}

static void uppercase_trimmed_token(char *dst, size_t dst_size, const char *src, size_t len) {
    while (len > 0 && isspace((unsigned char)*src)) { src++; len--; }
    while (len > 0 && isspace((unsigned char)src[len-1])) len--;
    if (len >= dst_size) len = dst_size - 1;
    for (size_t i = 0; i < len; ++i) dst[i] = (char)toupper((unsigned char)src[i]);
    dst[len] = '\0';
}

static const char *find_case_insensitive(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) return haystack;
    size_t needle_len = strlen(needle);
    for (const char *p = haystack; *p; ++p)
        if (strncasecmp(p, needle, needle_len) == 0) return p;
    return NULL;
}

static const char *find_case_insensitive_n(const char *haystack, size_t haystack_len,
                                           const char *needle) {
    if (!haystack || !needle || !needle[0]) return haystack;
    size_t needle_len = strlen(needle);
    if (needle_len > haystack_len) return NULL;
    for (size_t i = 0; i + needle_len <= haystack_len; ++i)
        if (strncasecmp(haystack + i, needle, needle_len) == 0) return haystack + i;
    return NULL;
}

static bool dark_reign_is_commented_call(const char *body_start, const char *hit) {
    const char *p = hit;
    while (p > body_start && p[-1] != '\n' && p[-1] != '\r') p--;
    while (p < hit) { if (*p == ';') return true; p++; }
    return false;
}

static void dark_reign_scn_path_from_map(const char *map_path, char *scn_path,
                                         size_t scn_path_size) {
    if (!map_path || !scn_path || scn_path_size == 0) return;
    snprintf(scn_path, scn_path_size, "%s", map_path);
    size_t len = strlen(scn_path);
    if (len >= 4 && strcasecmp(scn_path + len - 4, ".SCN") == 0) return;
    if (len >= 11 && strcasecmp(scn_path + len - 11, "/TACTICS.MM") == 0) {
        char *slash = strrchr(scn_path, '/');
        if (!slash || slash == scn_path) {
            replace_extension(scn_path, scn_path_size, scn_path, ".SCN");
            return;
        }
        *slash = '\0';
        char *parent = strrchr(scn_path, '/');
        const char *stem = parent ? parent + 1 : scn_path;
        char rebuilt[1024];
        if (parent) {
            *parent = '\0';
            snprintf(rebuilt, sizeof(rebuilt), "%s/%s/%s.SCN", scn_path, stem, stem);
        } else {
            snprintf(rebuilt, sizeof(rebuilt), "%s/%s.SCN", stem, stem);
        }
        snprintf(scn_path, scn_path_size, "%s", rebuilt);
        return;
    }
    replace_extension(scn_path, scn_path_size, scn_path, ".SCN");
}

static void dark_reign_mm_path_from_map(const char *map_path, char *mm_path,
                                        size_t mm_path_size) {
    if (!map_path || !mm_path || mm_path_size == 0) return;
    snprintf(mm_path, mm_path_size, "%s", map_path);
    size_t len = strlen(mm_path);
    if (len >= 3 && strcasecmp(mm_path + len - 3, ".MM") == 0) return;
    char *slash = strrchr(mm_path, '/');
    if (!slash) return;
    slash[1] = '\0';
    strncat(mm_path, "TACTICS.MM", mm_path_size - strlen(mm_path) - 1);
}

static void dark_reign_map_path_from_scn(const char *scn_path, char *map_path,
                                         size_t map_path_size) {
    if (!scn_path || !map_path || map_path_size == 0) return;
    snprintf(map_path, map_path_size, "%s", scn_path);
    replace_extension(map_path, map_path_size, map_path, ".MAP");
}

static void dark_reign_root_from_map(const char *map_path, char *root, size_t root_size) {
    const char *scenario = find_case_insensitive(map_path, "/scenario/");
    if (!scenario) { snprintf(root, root_size, "%s", DEFAULT_DATA_ROOT); return; }
    size_t len = (size_t)(scenario - map_path);
    if (len >= root_size) len = root_size - 1;
    memcpy(root, map_path, len); root[len] = '\0';
}

static void dark_reign_load_definitions(const char *map_path, DarkReignDefinitions *defs) {
    memset(defs, 0, sizeof(*defs));
    char root[1024], path[1024];
    dark_reign_root_from_map(map_path, root, sizeof(root));
    path_join(path, sizeof(path), root, "deftxt/UNITS.TXT");   defs->units     = load_text_file(path);
    path_join(path, sizeof(path), root, "deftxt/BUILD.TXT");   defs->buildings = load_text_file(path);
    path_join(path, sizeof(path), root, "deftxt/OVERLAY.TXT"); defs->overlay   = load_text_file(path);
    path_join(path, sizeof(path), root, "deftxt/ANIMATE.TXT"); defs->animate   = load_text_file(path);
}

static void dark_reign_free_definitions(DarkReignDefinitions *defs) {
    free(defs->units); free(defs->buildings); free(defs->overlay); free(defs->animate);
    memset(defs, 0, sizeof(*defs));
}

static bool dark_reign_find_definition_block(const char *text, const char *define_call,
                                             const char *type_name, const char **body,
                                             size_t *body_len) {
    if (!text) return false;
    const char *cursor = text;
    while ((cursor = find_case_insensitive(cursor, define_call)) != NULL) {
        const char *open  = strchr(cursor, '(');
        const char *close = open ? strchr(open + 1, ')') : NULL;
        if (!open || !close) { cursor += strlen(define_call); continue; }
        char candidate[96];
        copy_trimmed_token(candidate, sizeof(candidate), open + 1, (size_t)(close - open - 1));
        if (strcasecmp(candidate, type_name) == 0) {
            const char *brace = strchr(close + 1, '{');
            if (!brace) return false;
            int depth = 0;
            for (const char *p = brace; *p; ++p) {
                if (*p == '{') depth++;
                else if (*p == '}') { if (--depth == 0) { *body = brace + 1; *body_len = (size_t)(p - (brace+1)); return true; } }
            }
            return false;
        }
        cursor = close + 1;
    }
    return false;
}

static bool dark_reign_find_call_arg(const char *body, size_t body_len, const char *call,
                                     char *dst, size_t dst_size) {
    const char *cursor = body;
    size_t remaining = body_len;
    while (remaining > 0) {
        const char *hit = find_case_insensitive_n(cursor, remaining, call);
        if (!hit) return false;
        if (!dark_reign_is_commented_call(body, hit)) {
            const char *open = strchr(hit, '(');
            if (open && open < body + body_len) {
                const char *arg = open + 1;
                while (arg < body + body_len && isspace((unsigned char)*arg)) arg++;
                const char *end = arg;
                while (end < body + body_len && *end != ')' && !isspace((unsigned char)*end)) end++;
                if (end > arg) { copy_trimmed_token(dst, dst_size, arg, (size_t)(end-arg)); return dst[0] != '\0'; }
            }
        }
        const char *next = hit + strlen(call);
        remaining = (size_t)((body + body_len) - next);
        cursor = next;
    }
    return false;
}

static int dark_reign_find_call_args(const char *body, size_t body_len, const char *call,
                                     char args[][32], int max_args) {
    if (!body || !call || !args || max_args <= 0) return 0;
    const char *hit = find_case_insensitive_n(body, body_len, call);
    if (!hit) return 0;
    const char *end = body + body_len;
    const char *cursor = hit + strlen(call);
    while (cursor < end && *cursor != '(') cursor++;
    if (cursor >= end) return 0;
    cursor++;

    int count = 0;
    while (cursor < end && *cursor != ')' && count < max_args) {
        while (cursor < end && isspace((unsigned char)*cursor)) cursor++;
        if (cursor >= end || *cursor == ')') break;
        const char *start = cursor;
        while (cursor < end && *cursor != ')' && !isspace((unsigned char)*cursor)) cursor++;
        if (cursor > start) {
            copy_trimmed_token(args[count], 32, start, (size_t)(cursor - start));
            if (args[count][0] != '\0') count++;
        }
    }
    return count;
}

static bool dark_reign_resolve_animation_sprite(const DarkReignDefinitions *defs,
                                                const char *animation_name,
                                                char *sprite_name, size_t sprite_name_size) {
    const char *body = NULL; size_t body_len = 0;
    if (!dark_reign_find_definition_block(defs->animate, "DefineAnimationType",
                                          animation_name, &body, &body_len)) return false;
    return dark_reign_find_call_arg(body, body_len, "SetSprite", sprite_name, sprite_name_size);
}

typedef struct {
    const char *type_name, *sprite_name, *shadow_name;
    int footprint_w, footprint_h;
    bool solid;
} DarkReignDecorationSpec;

static const DarkReignDecorationSpec DARK_REIGN_DECORATION_SPECS[] = {
    { "clif1", "aoclf000.spr","aoclf0sh.spr",1,4,true  }, { "clif2","aoclf001.spr","aoclf1sh.spr",1,3,true },
    { "clif3", "aoclf002.spr","aoclf2sh.spr",1,3,true  }, { "clif4","aoclf003.spr","aoclf3sh.spr",3,4,true },
    { "clif5", "aoclf004.spr","aoclf4sh.spr",3,5,true  }, { "clif6","aoclf005.spr","aoclf5sh.spr",3,3,true },
    { "plnt1", "aopln000.spr","aopln0sh.spr",1,1,false }, { "plnt2","aopln001.spr","aopln1sh.spr",1,1,false},
    { "plnt3", "aopln002.spr","aopln2sh.spr",1,1,false }, { "rock1","aoroc000.spr","aoroc0sh.spr",1,1,true },
    { "rock2", "aoroc001.spr","aoroc1sh.spr",1,1,true  }, { "rock3","aoroc002.spr","aoroc2sh.spr",1,1,true },
    { "rock4", "aoroc003.spr","aoroc3sh.spr",3,3,true  }, { "rock5","aoroc004.spr","aoroc4sh.spr",3,3,true },
    { "rock6", "aoroc005.spr","aoroc5sh.spr",3,3,true  }, { "tree1","aotre000.spr","aotre0sh.spr",1,1,true },
    { "tree2", "aotre001.spr","aotre1sh.spr",1,1,true  }, { "tree3","aotre002.spr","aotre2sh.spr",1,1,true },
    { "tree4", "aotre003.spr","aotre3sh.spr",1,1,true  }, { "tree5","aotre004.spr","aotre4sh.spr",1,1,true },
    { "tree6", "aotre005.spr","aotre5sh.spr",1,1,true  }, { "rubble1","aorub000.spr","aorub0sh.spr",1,1,false},
    { "rubble2","aorub001.spr","aorub1sh.spr",1,1,false}, { "rubble3","aorub002.spr","aorub2sh.spr",1,1,false},
    { "water1","aowtr000.spr","aowtr0sh.spr",1,1,false }, { "water2","aowtr001.spr","aowtr1sh.spr",1,1,false},
    { "water3","aowtr002.spr","aowtr2sh.spr",1,1,false }, { "impww","ncwel1l0.spr","",3,3,true },
    { "impmn","ncmin1l0.spr","",3,3,true },
};

typedef struct {
    char sprite_name[32], sprite2_name[32], sprite3_name[32], shadow_name[32];
    int footprint_w, footprint_h;
    bool solid;
    bool center_anchor;
    bool has_sprite_pivot;
    int sprite_pivot_x, sprite_pivot_y;
    int frame_index;
} DarkReignVisualSpec;

static bool dark_reign_visual_from_static(const char *type_name, DarkReignVisualSpec *out) {
    size_t count = sizeof(DARK_REIGN_DECORATION_SPECS) / sizeof(DARK_REIGN_DECORATION_SPECS[0]);
    for (size_t i = 0; i < count; ++i) {
        if (strcasecmp(DARK_REIGN_DECORATION_SPECS[i].type_name, type_name) == 0) {
            snprintf(out->sprite_name, sizeof(out->sprite_name), "%s", DARK_REIGN_DECORATION_SPECS[i].sprite_name);
            snprintf(out->shadow_name, sizeof(out->shadow_name), "%s", DARK_REIGN_DECORATION_SPECS[i].shadow_name);
            out->footprint_w = DARK_REIGN_DECORATION_SPECS[i].footprint_w;
            out->footprint_h = DARK_REIGN_DECORATION_SPECS[i].footprint_h;
            out->solid = DARK_REIGN_DECORATION_SPECS[i].solid;
            return true;
        }
    }
    return false;
}

static bool dark_reign_resolve_unit_visual(const DarkReignDefinitions *defs, const char *type_name,
                                           DarkReignVisualSpec *out) {
    memset(out, 0, sizeof(*out));
    const char *body = NULL; size_t body_len = 0;
    if (!dark_reign_find_definition_block(defs->units, "DefineUnitType", type_name, &body, &body_len))
        return dark_reign_visual_from_static(type_name, out);
    if (!dark_reign_find_call_arg(body, body_len, "SetImage", out->sprite_name, sizeof(out->sprite_name)))
        return false;
    dark_reign_find_call_arg(body, body_len, "SetShadowImage", out->shadow_name, sizeof(out->shadow_name));
    out->footprint_w = 1; out->footprint_h = 1; out->solid = false;
    return true;
}

static bool dark_reign_resolve_building_visual(const DarkReignDefinitions *defs, const char *type_name,
                                               DarkReignVisualSpec *out) {
    memset(out, 0, sizeof(*out));
    const char *body = NULL; size_t body_len = 0;
    if (!dark_reign_find_definition_block(defs->buildings, "DefineBuildingType", type_name, &body, &body_len))
        return dark_reign_visual_from_static(type_name, out);
    char images[3][32] = {{ 0 }};
    if (dark_reign_find_call_args(body, body_len, "SetBuildingImages", images, 3) < 2)
        return false;

    /* Completed Dark Reign buildings are composites. The terrain archive
       supplies the ground underlay while the shared archive supplies the body
       and top layer, even though the underlay and body reuse a basename. */
    snprintf(out->sprite_name, sizeof(out->sprite_name), "tileset|%s", images[0]);
    snprintf(out->sprite2_name, sizeof(out->sprite2_name), "base|%s", images[0]);
    snprintf(out->sprite3_name, sizeof(out->sprite3_name), "base|%s", images[1]);
    char shadow[32] = { 0 };
    if (dark_reign_find_call_arg(body, body_len, "SetShadowImage", shadow, sizeof(shadow)))
        snprintf(out->shadow_name, sizeof(out->shadow_name), "base|%s", shadow);

    out->footprint_w = 3;
    out->footprint_h = 3;
    if (strcasecmp(type_name, "fh1") == 0 || strcasecmp(type_name, "fh2") == 0 ||
        strcasecmp(type_name, "fh3") == 0) {
        out->footprint_w = 4; out->footprint_h = 4;
    } else if (strcasecmp(type_name, "fglp") == 0) {
        out->footprint_w = 4; out->footprint_h = 3;
    } else if (strcasecmp(type_name, "fgpp") == 0) {
        out->footprint_w = 3; out->footprint_h = 4;
    } else if (strcasecmp(type_name, "CivilianBridge") == 0 ||
               strcasecmp(type_name, "CivilianVerticalBridge") == 0) {
        out->footprint_w = 4; out->footprint_h = 3;
    } else if (find_case_insensitive_n(type_name, strlen(type_name), "SmallHorizontalBridge") ||
               find_case_insensitive_n(type_name, strlen(type_name), "SmallVerticalBridge") ||
               find_case_insensitive_n(type_name, strlen(type_name), "SmallCentreBridge")) {
        out->footprint_w = 4; out->footprint_h = 4;
    }
    out->solid = true;
    /* AddBuildingAt coordinates identify the top-left of Dark Reign's authored
       RSPR canvas.  They are not the top-left of a collision footprint.  The
       canvas sizes (for example 144x120 for the bridge and 120x144 for the FG
       HQ) deliberately include the structure's complete placement envelope. */
    out->has_sprite_pivot = true;
    out->sprite_pivot_x = 0;
    out->sprite_pivot_y = 0;
    out->frame_index = 1;
    return true;
}

static bool dark_reign_resolve_thing_visual(const DarkReignDefinitions *defs, const char *type_name,
                                            DarkReignVisualSpec *out) {
    memset(out, 0, sizeof(*out));
    if (dark_reign_visual_from_static(type_name, out)) return true;
    const char *body = NULL; size_t body_len = 0;
    if (!dark_reign_find_definition_block(defs->overlay, "DefineThingType", type_name, &body, &body_len))
        return false;
    char animation[64] = { 0 };
    if (!dark_reign_find_call_arg(body, body_len, "SetThingImage", animation, sizeof(animation)) ||
        !dark_reign_resolve_animation_sprite(defs, animation, out->sprite_name, sizeof(out->sprite_name)))
        return false;
    char shadow_animation[64] = { 0 };
    if (dark_reign_find_call_arg(body, body_len, "SetThingShadowImage", shadow_animation, sizeof(shadow_animation)))
        dark_reign_resolve_animation_sprite(defs, shadow_animation, out->shadow_name, sizeof(out->shadow_name));
    out->footprint_w = 1; out->footprint_h = 1;
    out->solid = find_case_insensitive_n(body, body_len, "IsCrater") == NULL &&
                 find_case_insensitive_n(body, body_len, "NoEdit") == NULL;
    return true;
}

static int compare_map_decorations(const void *a, const void *b) {
    const MapDecoration *da = a, *db = b;
    int ya = da->gy + da->footprint_h, yb = db->gy + db->footprint_h;
    if (ya != yb) return ya - yb;
    return da->gx - db->gx;
}

static void add_dark_reign_decoration(GameMap *map, const DarkReignVisualSpec *spec, int gx, int gy) {
    if (!spec || gx < 0 || gy < 0 || gx >= map->width || gy >= map->height ||
        map->decoration_count >= MAX_DECORATIONS) return;
    MapDecoration *dec = &map->decorations[map->decoration_count++];
    dec->gx = gx; dec->gy = gy;
    dec->footprint_w = spec->footprint_w; dec->footprint_h = spec->footprint_h;
    dec->solid = spec->solid;
    dec->center_anchor = spec->center_anchor;
    dec->has_sprite_pivot = spec->has_sprite_pivot;
    dec->sprite_pivot_x = spec->sprite_pivot_x;
    dec->sprite_pivot_y = spec->sprite_pivot_y;
    dec->frame_index = spec->frame_index;
    dec->frame2_index = spec->frame_index;
    dec->frame3_index = spec->frame_index;
    snprintf(dec->sprite_name, sizeof(dec->sprite_name), "%s", spec->sprite_name);
    snprintf(dec->sprite2_name, sizeof(dec->sprite2_name), "%s", spec->sprite2_name);
    snprintf(dec->sprite3_name, sizeof(dec->sprite3_name), "%s", spec->sprite3_name);
    snprintf(dec->shadow_name, sizeof(dec->shadow_name), "%s", spec->shadow_name);
    if (!spec->solid) return;
    for (int y = 0; y < spec->footprint_h; ++y)
        for (int x = 0; x < spec->footprint_w; ++x) {
            int mx = gx + x, my = gy + y;
            if (mx >= 0 && my >= 0 && mx < map->width && my < map->height)
                map->blocked[map_index(map, mx, my)] = 1;
        }
}

static void load_dark_reign_decorations(const char *map_path, GameMap *map) {
    DarkReignDefinitions defs;
    dark_reign_load_definitions(map_path, &defs);
    char scn_path[1024];
    dark_reign_scn_path_from_map(map_path, scn_path, sizeof(scn_path));
    Blob blob;
    if (!load_blob(scn_path, &blob)) { dark_reign_free_definitions(&defs); return; }
    char *text = malloc(blob.size + 1);
    if (!text) { free_blob(&blob); dark_reign_free_definitions(&defs); return; }
    memcpy(text, blob.bytes, blob.size); text[blob.size] = '\0';
    char *cursor = text;
    while (map->decoration_count < MAX_DECORATIONS) {
        char *thing_hit    = strstr(cursor, "AddThingAt(");
        char *building_hit = strstr(cursor, "AddBuildingAt(");
        bool building = false;
        char *hit = thing_hit;
        if (building_hit && (!hit || building_hit < hit)) { hit = building_hit; building = true; }
        if (!hit) break;
        int object_id = 0, gx = 0, gy = 0;
        char type_name[64] = { 0 };
        int parsed = building ?
            sscanf(hit, "AddBuildingAt(%d %63[^ )] %d %d", &object_id, type_name, &gx, &gy) :
            sscanf(hit, "AddThingAt(%d %63[^ )] %d %d",    &object_id, type_name, &gx, &gy);
        if (parsed == 4) {
            (void)object_id;
            DarkReignVisualSpec visual;
            bool resolved = building ?
                dark_reign_resolve_building_visual(&defs, type_name, &visual) :
                dark_reign_resolve_thing_visual(&defs, type_name, &visual);
            if (resolved) add_dark_reign_decoration(map, &visual, gx, gy);
            else fprintf(stderr, "warning: unresolved Dark Reign %s type %s\n",
                         building ? "building" : "thing", type_name);
        }
        cursor = hit + (building ? strlen("AddBuildingAt(") : strlen("AddThingAt("));
    }
    free(text); free_blob(&blob); dark_reign_free_definitions(&defs);
    qsort(map->decorations, (size_t)map->decoration_count, sizeof(MapDecoration),
          compare_map_decorations);
}

static void load_dark_reign_team_credits(const char *map_path, GameMap *map) {
    if (!map) return;
    char scn_path[1024];
    dark_reign_scn_path_from_map(map_path, scn_path, sizeof(scn_path));
    Blob blob;
    if (!load_blob(scn_path, &blob)) return;
    char *text = malloc(blob.size + 1);
    if (!text) { free_blob(&blob); return; }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';

    int current_team = -1;
    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char nl = *next;
            *next++ = '\0';
            if (nl == '\r' && *next == '\n') next++;
        }
        while (isspace((unsigned char)*line)) line++;
        int team = -1;
        int credit = 0;
        if (sscanf(line, "SetTeam(%d", &team) == 1) {
            current_team = team;
        } else if (current_team >= 0 && current_team < 8 &&
                   sscanf(line, "SetCredit(%d", &credit) == 1) {
            map->player_resources[current_team] = credit;
        } else if (current_team == 0) {
            int world_x = 0, world_y = 0;
            if (sscanf(line, "SetStartLocation(%d %d", &world_x, &world_y) == 2) {
                /* Dark Reign stores scenario starts in world pixels. */
                map->has_camera = true;
                map->camera_gx = (float)world_x / 24.0f;
                map->camera_gy = (float)world_y / 24.0f;
            }
        }
        line = next;
    }

    free(text);
    free_blob(&blob);
}

/* ── tileset detection ──────────────────────────────────────────────────── */

static void detect_tileset_from_mm(const char *map_path, char *tileset, size_t tileset_size) {
    strncpy(tileset, "BARREN", tileset_size - 1); tileset[tileset_size-1] = '\0';
    char mm_path[1024];
    dark_reign_mm_path_from_map(map_path, mm_path, sizeof(mm_path));
    Blob blob;
    if (!load_blob(mm_path, &blob)) return;
    if (blob.size >= 28) {
        char raw[17]; memcpy(raw, blob.bytes + 12, 16); raw[16] = '\0';
        size_t n = strnlen(raw, sizeof(raw));
        while (n > 0 && isspace((unsigned char)raw[n-1])) raw[--n] = '\0';
        for (size_t i = 0; i < n; ++i) raw[i] = (char)toupper((unsigned char)raw[i]);
        if (n > 0) { strncpy(tileset, raw, tileset_size - 1); tileset[tileset_size-1] = '\0'; }
    }
    free_blob(&blob);
}

static void detect_tileset_from_scn(const char *map_path, char *tileset, size_t tileset_size) {
    char scn_path[1024];
    dark_reign_scn_path_from_map(map_path, scn_path, sizeof(scn_path));
    Blob blob;
    if (!load_blob(scn_path, &blob)) return;
    char *text = malloc(blob.size + 1);
    if (!text) { free_blob(&blob); return; }
    memcpy(text, blob.bytes, blob.size); text[blob.size] = '\0';
    const char *tag = "SetDefaultTerrain(";
    char *hit = strstr(text, tag);
    if (hit) {
        hit += strlen(tag);
        char *end = strchr(hit, ')');
        if (end && end > hit)
            uppercase_trimmed_token(tileset, tileset_size, hit, (size_t)(end - hit));
    }
    free(text); free_blob(&blob);
}

/* ── edge transition renderer ───────────────────────────────────────────── */

typedef enum {
    EDGE_MATCH_BELOW,
    EDGE_MATCH_EQUAL,
} EdgeMatchType;

typedef struct {
    bool self_below;
    int set_type;
    EdgeMatchType northwest;
    EdgeMatchType north;
    EdgeMatchType west;
} EdgeMatchRule;

typedef struct {
    int layer;
    int frame;
} DarkReignEdgeFrame;

static const EdgeMatchRule DARK_REIGN_EDGE_RULES[] = {
    { false, 36, EDGE_MATCH_BELOW, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL },
    { true,  37, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL },
    { true,  34, EDGE_MATCH_EQUAL, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW },
    { false, 35, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW },
    { false, 32, EDGE_MATCH_BELOW, EDGE_MATCH_BELOW, EDGE_MATCH_BELOW },
    { true,  33, EDGE_MATCH_BELOW, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL },
    { true,  31, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW },
    { true,  30, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW, EDGE_MATCH_BELOW },
    { false, 39, EDGE_MATCH_EQUAL, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW },
    { true,  38, EDGE_MATCH_EQUAL, EDGE_MATCH_EQUAL, EDGE_MATCH_EQUAL },
    { false, 40, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL, EDGE_MATCH_EQUAL },
    { false, 41, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL },
    { true,  43, EDGE_MATCH_BELOW, EDGE_MATCH_EQUAL, EDGE_MATCH_EQUAL },
    { false, 42, EDGE_MATCH_EQUAL, EDGE_MATCH_BELOW, EDGE_MATCH_BELOW },
};

static int dark_reign_terrain_type_from_frame(int frame) {
    if (frame < 8) return 15;
    if (frame < 128) return (frame - 8) / 8;
    return 0;
}

static int dark_reign_base_frame_for_type(int terrain_type, int variation) {
    variation &= 7;
    if (terrain_type == 15) return variation;
    return 8 + terrain_type * 8 + variation;
}

static int dark_reign_edge_frame_for_template(int template_id, int variation) {
    if (template_id >= 226 && template_id <= 239) {
        (void)variation;
        return 1032 + (template_id - 226) * 4;
    }
    return template_id + 218;
}

static int dark_reign_neighbor_type(const GameMap *map, int x, int y, int fallback) {
    if (!map_contains(map, x, y)) return fallback;
    return dark_reign_terrain_type_from_frame(map->tile_ids[map_index(map, x, y)]);
}

static bool dark_reign_rule_matches(EdgeMatchType match, int neighbor_type, int self_value) {
    if (match == EDGE_MATCH_BELOW) return neighbor_type < self_value;
    return neighbor_type >= self_value;
}

static void render_dark_reign_edges_for_cell(App *app, const GameMap *map, const Tileset *tileset,
                                             int x, int y, int dx, int dy) {
    int frame = map->tile_ids[map_index(map, x, y)] % tileset->count;
    int self_type = dark_reign_terrain_type_from_frame(frame);
    int variation = frame < 8 ? frame : (frame - 8) & 7;
    int northwest_type = dark_reign_neighbor_type(map, x - 1, y - 1, self_type);
    int north_type = dark_reign_neighbor_type(map, x, y - 1, self_type);
    int west_type = dark_reign_neighbor_type(map, x - 1, y, self_type);
    int neighbor_types[3] = { northwest_type, north_type, west_type };
    int shim_type = -1;
    bool used[16] = { false };
    DarkReignEdgeFrame edge_frames[32];
    int edge_frame_count = 0;

    SDL_Rect whole = { 0, 0, tileset->tile_w, tileset->tile_h };
    SDL_Rect dst = { dx, dy, tileset->tile_w, tileset->tile_h };

    for (size_t i = 0; i < sizeof(DARK_REIGN_EDGE_RULES) / sizeof(DARK_REIGN_EDGE_RULES[0]); ++i) {
        const EdgeMatchRule *rule = &DARK_REIGN_EDGE_RULES[i];
        EdgeMatchType matches[3] = { rule->northwest, rule->north, rule->west };
        int self_value = self_type;

        if (rule->self_below) {
            bool found_equal_neighbor = false;
            int lowest_equal = 256;
            for (int n = 0; n < 3; ++n) {
                if (matches[n] == EDGE_MATCH_EQUAL && neighbor_types[n] < lowest_equal) {
                    lowest_equal = neighbor_types[n];
                    found_equal_neighbor = true;
                }
            }
            if (found_equal_neighbor) {
                self_value = lowest_equal;
                if (self_value < 1 || self_value == self_type) continue;
            }
        }

        bool all_match = true;
        int lowest_match_value = -1;
        for (int n = 0; n < 3; ++n) {
            int neighbor_type = neighbor_types[n];
            if (!dark_reign_rule_matches(matches[n], neighbor_type, self_value)) {
                all_match = false;
                break;
            }
            if (matches[n] == EDGE_MATCH_BELOW) {
                if (!rule->self_below && (shim_type < 0 || neighbor_type < shim_type)) {
                    shim_type = neighbor_type;
                }
            } else if (neighbor_type >= 1 && (lowest_match_value < 0 || neighbor_type < lowest_match_value)) {
                lowest_match_value = neighbor_type;
            }
        }
        if (!all_match) continue;

        if (!rule->self_below && lowest_match_value > self_type) lowest_match_value = self_type;
        if (lowest_match_value < 0) lowest_match_value = self_type;
        if (lowest_match_value < 1 || lowest_match_value > 15 || used[lowest_match_value]) continue;
        used[lowest_match_value] = true;

        int template_id = rule->set_type + (lowest_match_value - 1) * 14;
        int edge_frame = dark_reign_edge_frame_for_template(template_id, variation);
        if (edge_frame >= 0 && edge_frame < tileset->count &&
            edge_frame_count < (int)(sizeof(edge_frames) / sizeof(edge_frames[0]))) {
            edge_frames[edge_frame_count++] = (DarkReignEdgeFrame){ lowest_match_value, edge_frame };
        }
    }

    if (shim_type >= 0 && shim_type != 15) {
        int shim_frame = dark_reign_base_frame_for_type(shim_type, variation);
        if (shim_frame >= 0 && shim_frame < tileset->count) {
            render_tile_at(app, tileset, shim_frame, whole, dst);
        }
    }

    for (int i = 1; i < edge_frame_count; ++i) {
        DarkReignEdgeFrame edge = edge_frames[i];
        int j = i - 1;
        while (j >= 0 && edge_frames[j].layer > edge.layer) {
            edge_frames[j + 1] = edge_frames[j];
            j--;
        }
        edge_frames[j + 1] = edge;
    }

    for (int i = 0; i < edge_frame_count; ++i) {
        render_tile_at(app, tileset, edge_frames[i].frame, whole, dst);
    }
}

/* ── map loader ─────────────────────────────────────────────────────────── */

bool load_dark_map(const char *map_path, GameMap *out) {
    memset(out, 0, sizeof(*out));
    out->direction_mode = RTS_DIRECTION_DARK_REIGN_8;
    Blob blob;
    if (!load_blob(map_path, &blob)) {
        char fallback_mm[1024];
        dark_reign_mm_path_from_map(map_path, fallback_mm, sizeof(fallback_mm));
        if (strcmp(fallback_mm, map_path) == 0 || !load_blob(fallback_mm, &blob)) {
            return false;
        }
    }

    size_t map_len = strlen(map_path);
    if (map_len >= 4 && strcasecmp(map_path + map_len - 4, ".SCN") == 0) {
        char terrain_path[1024];
        dark_reign_map_path_from_scn(map_path, terrain_path, sizeof(terrain_path));
        Blob map_blob;
        if (!load_blob(terrain_path, &map_blob)) {
            fprintf(stderr, "failed to load sibling Dark Reign MAP terrain %s\n", terrain_path);
            free_blob(&blob);
            return false;
        }
        free_blob(&blob);
        blob = map_blob;
    }

    int width = 0;
    int height = 0;
    size_t record_count = 0;
    bool map_record_format = blob.size >= 20 && memcmp(blob.bytes, "MAP_", 4) == 0;
    if (map_record_format) {
        width  = read_i32_le(blob.bytes + 8);
        height = read_i32_le(blob.bytes + 12);
        record_count = (size_t)width * (size_t)height;
        if (width <= 0 || height <= 0 || width > 512 || height > 512 ||
            blob.size < 20 + record_count * 6) {
            fprintf(stderr, "%s has unsupported map dimensions\n", map_path);
            free_blob(&blob); return false;
        }
    } else {
        if (blob.size < 12) {
            fprintf(stderr, "%s is not a supported Dark Reign map/MM file\n", map_path);
            free_blob(&blob); return false;
        }
        width = read_i32_le(blob.bytes + 4);
        height = read_i32_le(blob.bytes + 8);
        record_count = (size_t)width * (size_t)height;
        if (width <= 0 || height <= 0 || width > 512 || height > 512 || record_count == 0) {
            fprintf(stderr, "%s has unsupported Dark Reign MM dimensions\n", map_path);
            free_blob(&blob); return false;
        }
    }

    out->width  = width;
    out->height = height;
    out->tile_ids   = calloc(record_count, sizeof(uint16_t));
    out->blocked    = calloc(record_count, sizeof(uint8_t));
    out->decorations = calloc(MAX_DECORATIONS, sizeof(MapDecoration));
    if (!out->tile_ids || !out->blocked || !out->decorations) { free_blob(&blob); return false; }
    if (map_record_format) {
        const uint8_t *records = blob.bytes + 20;
        for (size_t i = 0; i < record_count; ++i) {
            const uint8_t *record = records + i * 6;
            uint8_t byte1 = record[0], byte2 = record[1];
            uint8_t subindex  = (uint8_t)(byte1 / 64);
            uint8_t variation = (uint8_t)(subindex * (byte2 + 1));
            if (variation > 7) variation = 7;
            int terrain_type = byte1 % 16 - 1;
            if (terrain_type < 0) terrain_type = 15;
            uint16_t frame = terrain_type == 15 ? variation :
                             (uint16_t)(8 + terrain_type * 8 + variation);
            out->tile_ids[i] = frame;
            out->blocked[i]  = terrain_type == 15 || (terrain_type >= 11 && terrain_type <= 14);
        }
    } else {
        size_t terrain_offset = 0;
        if (blob.size >= 32) {
            int32_t maybe_offset = read_i32_le(blob.bytes + 28);
            if (maybe_offset >= 0 && (size_t)maybe_offset < blob.size) {
                terrain_offset = (size_t)maybe_offset;
            }
        }
        if (terrain_offset == 0) terrain_offset = 32;
        if (terrain_offset >= blob.size) terrain_offset = 0;
        const uint8_t *terrain = blob.bytes + terrain_offset;
        size_t terrain_bytes = blob.size - terrain_offset;
        size_t max_cells = terrain_bytes * 2;
        for (size_t i = 0; i < record_count; ++i) {
            uint8_t terrain_type = 15;
            if (i < max_cells) {
                uint8_t packed = terrain[i / 2];
                terrain_type = (i & 1u) == 0 ? (packed & 0x0fu) : ((packed >> 4) & 0x0fu);
            }
            uint8_t variation = (uint8_t)((i + (size_t)(i / width)) & 7u);
            uint16_t frame = terrain_type == 15 ? variation :
                             (uint16_t)(8 + terrain_type * 8 + variation);
            out->tile_ids[i] = frame;
            out->blocked[i]  = terrain_type == 15 || (terrain_type >= 11 && terrain_type <= 14);
        }
    }
    detect_tileset_from_mm(map_path,  out->tileset_name, sizeof(out->tileset_name));
    detect_tileset_from_scn(map_path, out->tileset_name, sizeof(out->tileset_name));
    out->render_features |= MAP_RENDER_SMOOTH_TRANSITIONS;
    out->render_transitions = render_dark_reign_edges_for_cell;
    load_dark_reign_decorations(map_path, out);
    load_dark_reign_team_credits(map_path, out);
    free_blob(&blob);
    return true;
}

/* ── unit SCN parser ────────────────────────────────────────────────────── */

int load_dark_reign_initial_units(const char *map_path, Mobj *units, int max_units) {
    if (max_units <= 0) return 0;
    DarkReignDefinitions defs;
    dark_reign_load_definitions(map_path, &defs);
    char scn_path[1024];
    dark_reign_scn_path_from_map(map_path, scn_path, sizeof(scn_path));
    Blob blob;
    if (!load_blob(scn_path, &blob)) { dark_reign_free_definitions(&defs); return 0; }
    char *text = malloc(blob.size + 1);
    if (!text) { free_blob(&blob); dark_reign_free_definitions(&defs); return 0; }
    memcpy(text, blob.bytes, blob.size); text[blob.size] = '\0';

    int count = 0;
    bool has_player_unit = false;
    int current_team = 0;
    const char *team_tag = "SetDefaultTeam(";
    const char *unit_tag = "PutUnitAt(";
    char *cursor = text;
    while (count < max_units) {
        char *team_hit = strstr(cursor, team_tag);
        char *hit = strstr(cursor, unit_tag);
        if (team_hit && (!hit || team_hit < hit)) {
            int team = 0;
            if (sscanf(team_hit, "SetDefaultTeam(%d", &team) == 1) current_team = team;
            cursor = team_hit + strlen(team_tag);
            continue;
        }
        if (!hit) break;
        int object_id = 0, gx = 0, gy = 0;
        char unit_type[64] = { 0 };
        if (sscanf(hit, "PutUnitAt(%d %63[^ )] %d %d", &object_id, unit_type, &gx, &gy) == 4) {
            (void)object_id;
            if (gx >= 0 && gy >= 0) {
                units[count].gx = (float)gx + 0.5f;
                units[count].gy = (float)gy + 0.5f;
                units[count].speed = 5.5f;
                units[count].owner = current_team >= 0 && current_team < 8 ?
                    (uint8_t)current_team : 1;
                if (units[count].owner == 0) has_player_unit = true;
                units[count].selected = units[count].owner == 0 && count == 0;
                DarkReignVisualSpec visual;
                if (dark_reign_resolve_unit_visual(&defs, unit_type, &visual)) {
                    snprintf(units[count].sprite_name, sizeof(units[count].sprite_name), "%s", visual.sprite_name);
                    snprintf(units[count].shadow_name, sizeof(units[count].shadow_name), "%s", visual.shadow_name);
                } else {
                    snprintf(units[count].sprite_name, sizeof(units[count].sprite_name), "%s", DEFAULT_UNIT_SPR);
                    units[count].shadow_name[0] = '\0';
                    fprintf(stderr, "warning: unresolved Dark Reign unit type %s\n", unit_type);
                }
                count++;
            }
        }
        cursor = hit + strlen(unit_tag);
    }

    /* Campaign maps may derive their opening freighter from a player's
       AssociatedUnit building declaration rather than PutUnitAt. Resolve that
       relationship through BUILD.TXT and place it at the scenario start. */
    if (!has_player_unit && count < max_units) {
        int team = -1;
        int start_x = 0, start_y = 0;
        bool have_start = false;
        char associated_type[64] = { 0 };
        for (char *line = text; line && *line;) {
            char *next = strpbrk(line, "\r\n");
            if (next) {
                char nl = *next;
                *next++ = '\0';
                if (nl == '\r' && *next == '\n') next++;
            }
            while (isspace((unsigned char)*line)) line++;
            int parsed_team = 0;
            if (sscanf(line, "SetTeam(%d", &parsed_team) == 1) team = parsed_team;
            if (team == 0 && sscanf(line, "SetStartLocation(%d %d", &start_x, &start_y) == 2)
                have_start = true;
            if (sscanf(line, "SetDefaultTeam(%d", &parsed_team) == 1) team = parsed_team;
            if (team == 0 && associated_type[0] == '\0') {
                int object_id = 0, gx = 0, gy = 0;
                char building_type[64] = { 0 };
                if (sscanf(line, "AddBuildingAt(%d %63[^ )] %d %d",
                           &object_id, building_type, &gx, &gy) == 4) {
                    (void)object_id; (void)gx; (void)gy;
                    const char *body = NULL;
                    size_t body_len = 0;
                    if (dark_reign_find_definition_block(defs.buildings, "DefineBuildingType",
                                                         building_type, &body, &body_len)) {
                        dark_reign_find_call_arg(body, body_len, "AssociatedUnit",
                                                 associated_type, sizeof(associated_type));
                    }
                }
            }
            line = next;
        }
        if (have_start && associated_type[0] != '\0') {
            Mobj *unit = &units[count];
            unit->gx = (float)start_x / 24.0f;
            unit->gy = (float)start_y / 24.0f;
            unit->speed = 4.5f;
            unit->owner = 0;
            unit->selected = true;
            DarkReignVisualSpec visual;
            if (dark_reign_resolve_unit_visual(&defs, associated_type, &visual)) {
                snprintf(unit->sprite_name, sizeof(unit->sprite_name), "%s", visual.sprite_name);
                snprintf(unit->shadow_name, sizeof(unit->shadow_name), "%s", visual.shadow_name);
                count++;
            }
        }
    }
    free(text); free_blob(&blob); dark_reign_free_definitions(&defs);
    return count;
}

/* ── plugin asset loader ────────────────────────────────────────────────── */

bool dark_reign_plugin_load_assets(SDL_Renderer *renderer, const char *data_root,
                                   const GameMap *map, const char *sprite_name,
                                   Tileset *tileset, SpriteSheet *unit_sprite) {
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
        destroy_tileset(tileset);
        return false;
    }
    return true;
}
