#define _DEFAULT_SOURCE
#include "plugin.h"
#include "sl_types.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── BIM / COL helpers ──────────────────────────────────────────────────── */

/* COL file: 256 colours × 3 bytes (R,G,B), 6-bit VGA range (0-63).
   Multiply by 4 to get 8-bit values.  Index 0 is transparent. */
static bool sl_load_col_palette(const char *path, uint32_t palette[256]) {
    Blob blob;
    if (!load_blob(path, &blob)) return false;
    if (blob.size < 256 * 3) {
        fprintf(stderr, "7legion: %s too short for 256-colour palette\n", path);
        free_blob(&blob);
        return false;
    }
    const uint8_t *p = (const uint8_t *)blob.bytes;
    for (int i = 0; i < 256; ++i) {
        int r = (int)p[i * 3 + 0] * 4;
        int g = (int)p[i * 3 + 1] * 4;
        int b = (int)p[i * 3 + 2] * 4;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        palette[i] = (i == 0) ? 0x00000000u
                               : (0xff000000u | ((uint32_t)r << 16) |
                                  ((uint32_t)g << 8) | (uint32_t)b);
    }
    free_blob(&blob);
    return true;
}

/* BIM file layout:
     [offset table]  N × uint32_le, where N = first_offset / 4
     [frame data]    each frame: uint16_le width + uint16_le height + w*h bytes
   Tiles in TILES*.BIM are always 32×32 uncompressed palette-indexed pixels. */

#define SL_ATLAS_COLS 64

static bool sl_load_bim_tileset(SDL_Renderer *renderer, const char *path,
                                const uint32_t palette[256], Tileset *out) {
    memset(out, 0, sizeof(*out));

    Blob blob;
    if (!load_blob(path, &blob)) return false;
    if (blob.size < 4) { free_blob(&blob); return false; }

    const uint8_t *data = (const uint8_t *)blob.bytes;
    uint32_t first_offset = read_u32_le(data);
    if (first_offset == 0 || first_offset % 4 != 0 || first_offset > blob.size) {
        fprintf(stderr, "7legion: %s: bad BIM offset table\n", path);
        free_blob(&blob);
        return false;
    }
    int tile_count = (int)(first_offset / 4);

    /* Validate and count usable tiles. */
    int usable = 0;
    for (int i = 0; i < tile_count; ++i) {
        uint32_t off = read_u32_le(data + (size_t)i * 4);
        if (off + 4 > blob.size) break;
        uint16_t w = read_u16_le(data + off);
        uint16_t h = read_u16_le(data + off + 2);
        if (w != SL_TILE_W || h != SL_TILE_H) break;
        if (off + 4 + (size_t)w * h > blob.size) break;
        usable++;
    }
    if (usable == 0) {
        fprintf(stderr, "7legion: %s: no usable 32×32 tiles\n", path);
        free_blob(&blob);
        return false;
    }

    int atlas_rows = (usable + SL_ATLAS_COLS - 1) / SL_ATLAS_COLS;
    int atlas_w    = SL_ATLAS_COLS * SL_TILE_W;
    int atlas_h    = atlas_rows  * SL_TILE_H;

    uint32_t *rgba = calloc((size_t)atlas_w * (size_t)atlas_h, sizeof(uint32_t));
    if (!rgba) { free_blob(&blob); return false; }

    for (int i = 0; i < usable; ++i) {
        uint32_t off = read_u32_le(data + (size_t)i * 4);
        const uint8_t *pixels = data + off + 4; /* skip 4-byte header */

        int tx = (i % SL_ATLAS_COLS) * SL_TILE_W;
        int ty = (i / SL_ATLAS_COLS) * SL_TILE_H;
        for (int py = 0; py < SL_TILE_H; ++py) {
            for (int px = 0; px < SL_TILE_W; ++px) {
                uint8_t idx = pixels[py * SL_TILE_W + px];
                rgba[(ty + py) * atlas_w + tx + px] = palette[idx];
            }
        }
    }

    out->texture    = rgba_texture(renderer, rgba, atlas_w, atlas_h, false);
    out->count      = usable;
    out->atlas_cols = SL_ATLAS_COLS;
    out->tile_w     = SL_TILE_W;
    out->tile_h     = SL_TILE_H;

    free(rgba);
    free_blob(&blob);
    return out->texture != NULL;
}

/* ── unit sprite placeholder ────────────────────────────────────────────── */

/* 7th Legion BIM unit sprites use a proprietary RLE format that has not yet
   been fully reverse-engineered.  Load a solid-colour placeholder instead so
   the plugin is functional while the decoder is a TODO. */
static bool sl_make_placeholder_sprite(SDL_Renderer *renderer,
                                       int w, int h, uint32_t colour,
                                       SpriteSheet *out) {
    memset(out, 0, sizeof(*out));

    uint32_t *rgba = malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!rgba) return false;
    for (int i = 0; i < w * h; ++i) rgba[i] = colour;

    out->texture = rgba_texture(renderer, rgba, w, h, true);
    free(rgba);
    if (!out->texture) return false;

    out->frames = calloc(1, sizeof(SDL_Rect));
    if (!out->frames) { SDL_DestroyTexture(out->texture); out->texture = NULL; return false; }
    out->frames[0] = (SDL_Rect){ 0, 0, w, h };
    out->frame_count = 1;
    out->frame_w = w;
    out->frame_h = h;
    out->rotations = 1;
    out->primary_frames_per_rotation = 1;
    return true;
}

/* ── public loaders ─────────────────────────────────────────────────────── */

bool sl_load_map(const char *map_path, GameMap *out) {
    (void)map_path;
    /* No map files ship with the current data/7LEGION installation.
       Generate a flat 64×64 grass map so the engine can start. */
    memset(out, 0, sizeof(*out));

    const int W = 64, H = 64;
    out->width  = W;
    out->height = H;

    out->tile_ids = calloc((size_t)W * H, sizeof(uint16_t));
    out->blocked  = calloc((size_t)W * H, sizeof(uint8_t));
    if (!out->tile_ids || !out->blocked) {
        free(out->tile_ids);
        free(out->blocked);
        return false;
    }
    /* All cells use tile 0 (first ground tile) and are passable. */
    memset(out->tile_ids, 0, (size_t)W * H * sizeof(uint16_t));
    memset(out->blocked,  0, (size_t)W * H * sizeof(uint8_t));

    out->direction_mode = RTS_DIRECTION_DARK_REIGN_8;
    out->has_camera = true;
    out->camera_gx  = W / 2.0f;
    out->camera_gy  = H / 2.0f;
    return true;
}

bool sl_load_assets(SDL_Renderer *renderer, const char *data_root,
                    const GameMap *map,
                    const char *sprite_name,
                    Tileset *tileset, SpriteSheet *unit_sprite) {
    (void)map;
    (void)sprite_name;

    /* Palette */
    uint32_t palette[256];
    char col_path[512];
    snprintf(col_path, sizeof(col_path), "%s/GFX/REMAP.COL", data_root);
    if (!sl_load_col_palette(col_path, palette)) {
        fprintf(stderr, "7legion: failed to load palette %s\n", col_path);
        return false;
    }

    /* Tileset */
    char til_path[512];
    snprintf(til_path, sizeof(til_path), "%s/GFX/TILES.BIM", data_root);
    if (!sl_load_bim_tileset(renderer, til_path, palette, tileset)) {
        fprintf(stderr, "7legion: failed to load tileset %s\n", til_path);
        return false;
    }

    /* Unit sprite placeholder (olive-green soldier colour) */
    if (!sl_make_placeholder_sprite(renderer, 24, 24, 0xff3a6e28u, unit_sprite)) {
        fprintf(stderr, "7legion: failed to create placeholder sprite\n");
        destroy_tileset(tileset);
        return false;
    }
    return true;
}

int sl_load_initial_units(const char *map_path, Unit *units, int max_units) {
    (void)map_path;
    (void)units;
    (void)max_units;
    /* No scenario files yet — start with an empty battlefield. */
    return 0;
}
