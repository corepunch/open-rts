#include "engine.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

vscreen_t screens[1];
uint32_t vpalette[256];

static uint8_t *owned_pixels;
static bool palette_set;
static bool clip_set;
static irect_t clip_rect;
static uint8_t identity_map[256];
static bool identity_ready;

/* Exact RGB -> nearest screen index, direct mapped. Bit 24 marks a used slot. */
enum { NEAREST_SLOTS = 4096 };
static uint32_t nearest_key[NEAREST_SLOTS];
static uint8_t nearest_value[NEAREST_SLOTS];

/* Destination colormaps for palette entries with partial alpha. */
enum { BLEND_SLOTS = 8, BLEND_SKIP = 255 };
static struct {
    uint32_t key; /* alpha << 24 | rgb */
    uint8_t map[256];
} blend_cache[BLEND_SLOTS];
static int blend_count;

/* One source palette (plus team map and tint) resolved to the screen palette. */
typedef struct {
    uint32_t palette[256];
    uint8_t team[256];
    uint8_t map[256];
    uint8_t blend[256]; /* 0 opaque, BLEND_SKIP, or 1 + blend_cache slot */
    uint64_t hash;
    uint32_t tint;
    bool has_team, has_blend, identity, zero_transparent, used;
} remap_slot_t;

enum { REMAP_SLOTS = 64 };
static remap_slot_t remap_cache[REMAP_SLOTS];
static remap_slot_t *remap_last;
static int remap_next;

static void ensure_identity(void) {
    if (identity_ready) return;
    for (int i = 0; i < 256; ++i) identity_map[i] = (uint8_t)i;
    identity_ready = true;
}

static void flush_caches(void) {
    memset(nearest_key, 0, sizeof(nearest_key));
    for (int i = 0; i < REMAP_SLOTS; ++i) remap_cache[i].used = false;
    remap_last = NULL;
    remap_next = 0;
    blend_count = 0;
}

void V_AllocScreen(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (owned_pixels && screens[0].w == w && screens[0].h == h &&
        screens[0].pixels == owned_pixels) {
        return;
    }
    uint8_t *pixels = calloc((size_t)w * (size_t)h, 1);
    if (!pixels) return;
    free(owned_pixels);
    owned_pixels = pixels;
    screens[0].pixels = pixels;
    screens[0].w = w;
    screens[0].h = h;
}

void V_FreeScreen(void) {
    free(owned_pixels);
    owned_pixels = NULL;
    memset(screens, 0, sizeof(screens));
}

void V_BeginFrame(uint32_t clear_argb) {
    if (!screens[0].pixels) return;
    uint8_t index = palette_set ? V_NearestIndex(clear_argb) : 0;
    memset(screens[0].pixels, index, (size_t)screens[0].w * (size_t)screens[0].h);
    clip_set = false;
}

void I_SetPalette(const uint32_t argb[256]) {
    if (!argb) return;
    /* The world installs its palette every frame; keep the caches when it is unchanged. */
    if (palette_set && memcmp(vpalette, argb, sizeof(vpalette)) == 0) return;
    memcpy(vpalette, argb, sizeof(vpalette));
    palette_set = true;
    flush_caches();
}

bool I_ReadScreen(uint8_t *dst) {
    if (!dst || !screens[0].pixels) return false;
    memcpy(dst, screens[0].pixels, (size_t)screens[0].w * (size_t)screens[0].h);
    return true;
}

uint8_t V_NearestIndex(uint32_t argb) {
    uint32_t rgb = argb & 0x00ffffffu;
    uint32_t key = rgb | 0x01000000u;
    uint32_t slot = (rgb * 2654435761u) >> 20;
    if (nearest_key[slot] == key) return nearest_value[slot];
    int r = (int)(rgb >> 16);
    int g = (int)((rgb >> 8) & 255);
    int b = (int)(rgb & 255);
    int best = 0;
    int best_distance = 1 << 30;
    for (int i = 0; i < 256; ++i) {
        int dr = r - (int)((vpalette[i] >> 16) & 255);
        int dg = g - (int)((vpalette[i] >> 8) & 255);
        int db = b - (int)(vpalette[i] & 255);
        int distance = dr * dr + dg * dg + db * db;
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
            if (distance == 0) break;
        }
    }
    nearest_key[slot] = key;
    nearest_value[slot] = (uint8_t)best;
    return (uint8_t)best;
}

static uint32_t scale_rgb(uint32_t rgb, uint32_t tint) {
    int r = (int)((rgb >> 16) & 255) * (int)((tint >> 16) & 255) / 255;
    int g = (int)((rgb >> 8) & 255) * (int)((tint >> 8) & 255) / 255;
    int b = (int)(rgb & 255) * (int)(tint & 255) / 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* dst = src * alpha + dst * (1 - alpha), nearest-matched per destination index. */
static int blend_slot(uint32_t rgb, int alpha) {
    uint32_t key = ((uint32_t)alpha << 24) | rgb;
    for (int i = 0; i < blend_count; ++i)
        if (blend_cache[i].key == key) return i;
    if (blend_count == BLEND_SLOTS) return -1;
    int slot = blend_count++;
    blend_cache[slot].key = key;
    int sr = (int)((rgb >> 16) & 255) * alpha;
    int sg = (int)((rgb >> 8) & 255) * alpha;
    int sb = (int)(rgb & 255) * alpha;
    for (int i = 0; i < 256; ++i) {
        int r = (sr + (int)((vpalette[i] >> 16) & 255) * (255 - alpha)) / 255;
        int g = (sg + (int)((vpalette[i] >> 8) & 255) * (255 - alpha)) / 255;
        int b = (sb + (int)(vpalette[i] & 255) * (255 - alpha)) / 255;
        blend_cache[slot].map[i] =
            V_NearestIndex(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
    }
    return slot;
}

static uint64_t hash_remap(const uint32_t palette[256], const uint8_t *team, uint32_t tint) {
    uint64_t hash = 1469598103934665603ull ^ tint;
    for (int i = 0; i < 256; i += 2) {
        hash ^= (uint64_t)palette[i] | ((uint64_t)palette[i + 1] << 32);
        hash *= 1099511628211ull;
    }
    if (team) {
        for (int i = 0; i < 256; i += 8) {
            uint64_t word;
            memcpy(&word, team + i, sizeof(word));
            hash ^= word;
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

static bool remap_matches(const remap_slot_t *slot, const uint32_t palette[256],
                          const uint8_t *team, uint32_t tint) {
    return slot->used && slot->tint == tint && slot->has_team == (team != NULL) &&
           memcmp(slot->palette, palette, sizeof(slot->palette)) == 0 &&
           (!team || memcmp(slot->team, team, sizeof(slot->team)) == 0);
}

/* A source index whose colour already sits at the same screen index keeps
 * that index. Palette alpha carries over from the ARGB renderer: 0 skips the
 * pixel and a partial value blends it over the destination. */
static const remap_slot_t *remap_for(const uint32_t palette[256], const uint8_t *team,
                                     uint32_t tint) {
    tint &= 0x00ffffffu;
    if (remap_last && remap_matches(remap_last, palette, team, tint)) return remap_last;
    uint64_t hash = hash_remap(palette, team, tint);
    for (int i = 0; i < REMAP_SLOTS; ++i) {
        remap_slot_t *slot = &remap_cache[i];
        if (slot->hash == hash && remap_matches(slot, palette, team, tint))
            return remap_last = slot;
    }
    remap_slot_t *slot = &remap_cache[remap_next];
    remap_next = (remap_next + 1) % REMAP_SLOTS;
    memcpy(slot->palette, palette, sizeof(slot->palette));
    slot->has_team = team != NULL;
    if (team) memcpy(slot->team, team, sizeof(slot->team));
    slot->hash = hash;
    slot->tint = tint;
    slot->has_blend = false;
    slot->identity = true;
    slot->zero_transparent = (palette[0] >> 24) == 0;
    for (int i = 0; i < 256; ++i) {
        int index = team ? team[i] : i;
        uint32_t rgb = palette[index] & 0x00ffffffu;
        if (tint != 0x00ffffffu) rgb = scale_rgb(rgb, tint);
        slot->map[i] = (vpalette[index] & 0x00ffffffu) == rgb ? (uint8_t)index
                                                               : V_NearestIndex(rgb);
        if (slot->map[i] != i) slot->identity = false;
        int alpha = (int)(palette[i] >> 24);
        slot->blend[i] = 0;
        if (i == 0) continue; /* Index 0 is handled by the blit's opaque flag. */
        if (alpha == 0) {
            slot->blend[i] = BLEND_SKIP;
        } else if (alpha < 255) {
            int blend = blend_slot(rgb, alpha);
            if (blend >= 0) slot->blend[i] = (uint8_t)(blend + 1);
        }
        if (slot->blend[i]) slot->has_blend = true;
    }
    slot->used = true;
    return remap_last = slot;
}

const uint8_t *V_RemapPalette(const uint32_t palette[256]) {
    ensure_identity();
    if (!palette || !palette_set) return identity_map;
    const remap_slot_t *slot = remap_for(palette, NULL, 0x00ffffffu);
    return slot->identity ? identity_map : slot->map;
}

void V_ModulateRemap(uint8_t out[256], const uint32_t palette[256], uint32_t color) {
    if (!out) return;
    ensure_identity();
    if (!palette_set) {
        memcpy(out, identity_map, sizeof(identity_map));
        return;
    }
    memcpy(out, remap_for(palette ? palette : vpalette, NULL, color)->map, 256);
}

void V_ReadPixels(uint32_t *dst, int dst_pitch_bytes) {
    if (!dst || !screens[0].pixels || dst_pitch_bytes < screens[0].w * 4) return;
    for (int y = 0; y < screens[0].h; ++y) {
        uint32_t *row = (uint32_t *)((uint8_t *)dst + (size_t)y * (size_t)dst_pitch_bytes);
        const uint8_t *src = screens[0].pixels + (size_t)y * (size_t)screens[0].w;
        for (int x = 0; x < screens[0].w; ++x) {
            uint32_t color = vpalette[src[x]];
            row[x] = color | 0xff000000u;
        }
    }
}

void V_SetClip(irect_t next) {
    if (next.w <= 0 || next.h <= 0) {
        clip_set = false;
        clip_rect = (irect_t){0};
        return;
    }
    clip_rect = next;
    clip_set = true;
}

irect_t V_GetClip(void) {
    if (!clip_set) return (irect_t){0, 0, screens[0].w, screens[0].h};
    return clip_rect;
}

static bool screen_bounds(irect_t *bounds) {
    if (!screens[0].pixels || screens[0].w <= 0 || screens[0].h <= 0) return false;
    *bounds = (irect_t){0, 0, screens[0].w, screens[0].h};
    if (!clip_set) return true;
    int x0 = bounds->x > clip_rect.x ? bounds->x : clip_rect.x;
    int y0 = bounds->y > clip_rect.y ? bounds->y : clip_rect.y;
    int x1 = bounds->x + bounds->w;
    int y1 = bounds->y + bounds->h;
    int cx1 = clip_rect.x + clip_rect.w;
    int cy1 = clip_rect.y + clip_rect.h;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    if (x0 >= x1 || y0 >= y1) return false;
    *bounds = (irect_t){x0, y0, x1 - x0, y1 - y0};
    return true;
}

static bool intersect_bounds(irect_t r, irect_t *out) {
    irect_t bounds;
    if (!screen_bounds(&bounds)) return false;
    int x0 = r.x > bounds.x ? r.x : bounds.x;
    int y0 = r.y > bounds.y ? r.y : bounds.y;
    int x1 = r.x + r.w;
    int y1 = r.y + r.h;
    int bx1 = bounds.x + bounds.w;
    int by1 = bounds.y + bounds.h;
    if (x1 > bx1) x1 = bx1;
    if (y1 > by1) y1 = by1;
    if (x0 >= x1 || y0 >= y1) return false;
    *out = (irect_t){x0, y0, x1 - x0, y1 - y0};
    return true;
}

void V_DrawPoint(ivec2_t p, uint8_t color) {
    irect_t pixel = {p.x, p.y, 1, 1}, clipped;
    if (!intersect_bounds(pixel, &clipped)) return;
    screens[0].pixels[(size_t)p.y * (size_t)screens[0].w + (size_t)p.x] = color;
}

void V_FillRect(irect_t r, uint8_t color) {
    irect_t area;
    if (!intersect_bounds(r, &area)) return;
    for (int y = 0; y < area.h; ++y) {
        memset(screens[0].pixels + ((size_t)(area.y + y) * (size_t)screens[0].w + (size_t)area.x),
               color, (size_t)area.w);
    }
}

void V_DrawLine(ivec2_t a, ivec2_t b, uint8_t color) {
    if (a.y == b.y) {
        int x0 = a.x < b.x ? a.x : b.x;
        int x1 = a.x < b.x ? b.x : a.x;
        V_FillRect((irect_t){x0, a.y, x1 - x0 + 1, 1}, color);
        return;
    }
    if (a.x == b.x) {
        int y0 = a.y < b.y ? a.y : b.y;
        int y1 = a.y < b.y ? b.y : a.y;
        V_FillRect((irect_t){a.x, y0, 1, y1 - y0 + 1}, color);
        return;
    }
    int dx = abs(b.x - a.x), sx = a.x < b.x ? 1 : -1;
    int dy = -abs(b.y - a.y), sy = a.y < b.y ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        V_DrawPoint(a, color);
        if (a.x == b.x && a.y == b.y) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; a.x += sx; }
        if (e2 <= dx) { err += dx; a.y += sy; }
    }
}

void V_DrawRectOutline(irect_t r, uint8_t color) {
    if (r.w <= 0 || r.h <= 0) return;
    ivec2_t tl = {r.x, r.y};
    ivec2_t tr = {r.x + r.w - 1, r.y};
    ivec2_t bl = {r.x, r.y + r.h - 1};
    ivec2_t br = {r.x + r.w - 1, r.y + r.h - 1};
    V_DrawLine(tl, tr, color);
    V_DrawLine(tl, bl, color);
    V_DrawLine(bl, br, color);
    V_DrawLine(tr, br, color);
}

/* How one source index reaches the screen. Transparency is decided on the
 * source index, before any remap, so a remap may land on screen index 0. */
typedef struct {
    const uint8_t *remap;  /* source index -> screen index, or NULL */
    const uint8_t *blend;  /* per source index: 0, BLEND_SKIP, or 1 + blend slot */
    const uint8_t *table;  /* 256x256 [source << 8 | destination], or NULL */
    const uint8_t *colormap; /* destination -> destination under the silhouette */
    bool opaque;
} blit_t;

static inline void put_pixel(uint8_t *dst, uint8_t index, const blit_t *blit) {
    if (index == 0 && !blit->opaque) return;
    if (blit->table) {
        *dst = blit->table[((size_t)index << 8) | *dst];
    } else if (blit->colormap) {
        *dst = blit->colormap[*dst];
    } else if (blit->blend && blit->blend[index]) {
        if (blit->blend[index] != BLEND_SKIP)
            *dst = blend_cache[blit->blend[index] - 1].map[*dst];
    } else {
        *dst = blit->remap ? blit->remap[index] : index;
    }
}

static void blit_block(ivec2_t at, const uint8_t *src, isize2_t size, int src_pitch,
                       uint32_t flags, const blit_t *blit) {
    if (!src || size.w <= 0 || size.h <= 0 || src_pitch <= 0) return;
    irect_t area;
    if (!intersect_bounds((irect_t){at.x, at.y, size.w, size.h}, &area)) return;
    int first_x = area.x - at.x;
    int step = 1;
    if (flags & V_FLIP_X) {
        first_x = size.w - 1 - first_x;
        step = -1;
    }
    bool plain = !blit->table && !blit->colormap && !blit->blend;
    for (int y = 0; y < area.h; ++y) {
        int sy = area.y - at.y + y;
        if (flags & V_FLIP_Y) sy = size.h - 1 - sy;
        const uint8_t *from = src + (size_t)sy * (size_t)src_pitch + first_x;
        uint8_t *dst = screens[0].pixels + ((size_t)(area.y + y) * (size_t)screens[0].w + (size_t)area.x);
        if (plain && blit->opaque && !blit->remap && step == 1) {
            memcpy(dst, from, (size_t)area.w);
        } else if (plain && !blit->opaque) {
            const uint8_t *remap = blit->remap;
            for (int x = 0; x < area.w; ++x, from += step) {
                uint8_t index = *from;
                if (index) dst[x] = remap ? remap[index] : index;
            }
        } else if (blit->blend && !blit->table && !blit->colormap) {
            const uint8_t *remap = blit->remap ? blit->remap : identity_map;
            const uint8_t *blend = blit->blend;
            bool opaque = blit->opaque;
            for (int x = 0; x < area.w; ++x, from += step) {
                uint8_t index = *from;
                if (!index && !opaque) continue;
                uint8_t mode = blend[index];
                if (!mode) dst[x] = remap[index];
                else if (mode != BLEND_SKIP) dst[x] = blend_cache[mode - 1].map[dst[x]];
            }
        } else {
            for (int x = 0; x < area.w; ++x, from += step) put_pixel(&dst[x], *from, blit);
        }
    }
}

static void blit_scaled(irect_t dst_rect, const uint8_t *src, isize2_t size, int src_pitch,
                        uint32_t flags, const blit_t *blit) {
    if (!src || size.w <= 0 || size.h <= 0 || src_pitch <= 0 ||
        dst_rect.w <= 0 || dst_rect.h <= 0) return;
    if (dst_rect.w == size.w && dst_rect.h == size.h) {
        blit_block((ivec2_t){dst_rect.x, dst_rect.y}, src, size, src_pitch, flags, blit);
        return;
    }
    irect_t area;
    if (!intersect_bounds(dst_rect, &area)) return;
    for (int y = 0; y < area.h; ++y) {
        int sy = (area.y - dst_rect.y + y) * size.h / dst_rect.h;
        if (flags & V_FLIP_Y) sy = size.h - 1 - sy;
        const uint8_t *from = src + (size_t)sy * (size_t)src_pitch;
        uint8_t *dst = screens[0].pixels + ((size_t)(area.y + y) * (size_t)screens[0].w + (size_t)area.x);
        for (int x = 0; x < area.w; ++x) {
            int sx = (area.x - dst_rect.x + x) * size.w / dst_rect.w;
            if (flags & V_FLIP_X) sx = size.w - 1 - sx;
            put_pixel(&dst[x], from[sx], blit);
        }
    }
}

void V_DrawBlock(ivec2_t at, const uint8_t *src, isize2_t size, int src_pitch,
                 const uint8_t *remap, uint32_t flags) {
    blit_t blit = {.remap = remap, .opaque = (flags & V_OPAQUE) != 0};
    blit_block(at, src, size, src_pitch, flags, &blit);
}

void V_DrawBlockScaled(irect_t dst_rect, const uint8_t *src, isize2_t size, int src_pitch,
                       const uint8_t *remap, uint32_t flags) {
    blit_t blit = {.remap = remap, .opaque = (flags & V_OPAQUE) != 0};
    blit_scaled(dst_rect, src, size, src_pitch, flags, &blit);
}

void V_DrawBlockTranslucent(ivec2_t at, const uint8_t *src, isize2_t size, int src_pitch,
                            const uint8_t *table, uint32_t flags) {
    if (!table) return;
    blit_t blit = {.table = table};
    blit_block(at, src, size, src_pitch, flags, &blit);
}

void V_DrawSilhouetteColormap(ivec2_t at, const uint8_t *src, isize2_t size, int src_pitch,
                              const uint8_t *colormap, uint32_t flags) {
    if (!colormap) return;
    blit_t blit = {.colormap = colormap};
    blit_block(at, src, size, src_pitch, flags, &blit);
}

static bool sprite_source(const spritesheet_t *sprite, int frame, const irect_t *src,
                          const uint8_t **indices, isize2_t *size) {
    if (!sprite || !sprite->cells || !sprite->lumps || frame < 0 || frame >= sprite->numlumps)
        return false;
    irect_t cell = sprite->cells[frame].rect;
    if (cell.w <= 0 || cell.h <= 0 || !sprite->lumps[frame].indices) return false;
    irect_t rect = src ? *src : cell;
    if (rect.x < cell.x || rect.y < cell.y || rect.w <= 0 || rect.h <= 0 ||
        rect.x + rect.w > cell.x + cell.w || rect.y + rect.h > cell.y + cell.h)
        return false;
    *indices = sprite->lumps[frame].indices +
        ((size_t)(rect.y - cell.y) * (size_t)cell.w + (size_t)(rect.x - cell.x));
    *size = (isize2_t){rect.w, rect.h};
    return true;
}

void V_DrawSpriteCell(ivec2_t at, const spritesheet_t *sheet, int cell,
                      const uint8_t *remap, uint32_t flags) {
    const uint8_t *indices;
    isize2_t size;
    if (!sprite_source(sheet, cell, NULL, &indices, &size)) return;
    V_DrawBlock(at, indices, size, sheet->cells[cell].rect.w, remap, flags);
}

void V_DrawSpriteCellScaled(irect_t dst, const spritesheet_t *sheet, int cell,
                            const irect_t *src, const uint8_t *remap, uint32_t flags) {
    const uint8_t *indices;
    isize2_t size;
    if (!sprite_source(sheet, cell, src, &indices, &size)) return;
    V_DrawBlockScaled(dst, indices, size, sheet->cells[cell].rect.w, remap, flags);
}

static void blit_remapped(const irect_t *dst, const uint8_t *indices, isize2_t size, int pitch,
                          const remap_slot_t *slot, uint32_t flags) {
    ensure_identity();
    blit_t blit = {
        .remap = slot->identity ? NULL : slot->map,
        .blend = slot->has_blend ? slot->blend : NULL,
        /* A transparent entry 0 stays see-through even on an opaque draw. */
        .opaque = (flags & V_OPAQUE) != 0 && !slot->zero_transparent,
    };
    blit_scaled(*dst, indices, size, pitch, flags, &blit);
}

bool R_DrawIndexed(const uint8_t *indices, isize2_t size, const uint32_t palette[256],
                   const uint8_t *remap, const irect_t *src, const irect_t *dst,
                   uint32_t flags) {
    if (!indices || !palette || size.w <= 0 || size.h <= 0 || !dst) return false;
    if (!palette_set) I_SetPalette(palette);
    irect_t rect = src ? *src : (irect_t){0, 0, size.w, size.h};
    if (rect.x < 0 || rect.y < 0 || rect.w <= 0 || rect.h <= 0 ||
        rect.x + rect.w > size.w || rect.y + rect.h > size.h) return false;
    const uint8_t *source = indices + (size_t)rect.y * (size_t)size.w + (size_t)rect.x;
    blit_remapped(dst, source, (isize2_t){rect.w, rect.h}, size.w,
                  remap_for(palette, remap, 0x00ffffffu), flags);
    return true;
}

static const uint8_t *sprite_team_map(const spritesheet_t *sprite, int palette) {
    if (!sprite || palette < 0) return NULL;
    for (int i = 0; i < sprite->palette_map_count; ++i)
        if (sprite->palette_maps[i].id == palette) return sprite->palette_maps[i].indices;
    return NULL;
}

bool R_DrawSprite(const spritesheet_t *sprite, int frame, int palette,
                  const irect_t *src, const irect_t *dst, uint32_t flags, int intensity) {
    const uint8_t *indices;
    isize2_t size;
    if (!sprite_source(sprite, frame, src, &indices, &size) || !dst) return false;
    if (!palette_set) I_SetPalette(sprite->source_palette);
    uint32_t tint = 0x00ffffffu;
    if (intensity > 0 && intensity < 16)
        tint = (uint32_t)((intensity * 255 + 8) / 16) * 0x010101u;
    blit_remapped(dst, indices, size, sprite->cells[frame].rect.w,
                  remap_for(sprite->source_palette, sprite_team_map(sprite, palette), tint),
                  flags);
    return true;
}

static int glyph_advance(const bitmapfont_t *font, unsigned char ch) {
    return font->glyph_width[ch] > 0 ? font->glyph_width[ch] : font->glyph_size.w;
}

int V_TextWidth(const bitmapfont_t *font, const char *text) {
    if (!font || !text) return 0;
    int width = 0, line_width = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '\r') continue;
        if (*p == '\n') {
            if (line_width > width) width = line_width;
            line_width = 0;
            continue;
        }
        unsigned char ch = *p;
        if (ch >= 128 || font->glyph_index[ch] < 0) ch = '?';
        line_width += glyph_advance(font, ch);
    }
    return line_width > width ? line_width : width;
}

static void draw_glyphs(ivec2_t at, const bitmapfont_t *font, const char *text,
                        const uint8_t *remap, int scale) {
    if (!font || !font->sprite.lumps || !text || scale <= 0) return;
    int line_h = (font->line_h > 0 ? font->line_h : font->glyph_size.h) * scale;
    int cx = at.x, cy = at.y;
    int divisor = font->draw_divisor > 0 ? font->draw_divisor : 1;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '\r') continue;
        if (*p == '\n') {
            cx = at.x;
            cy += line_h;
            continue;
        }
        unsigned char ch = *p;
        if (ch >= 128 || font->glyph_index[ch] < 0) ch = '?';
        int frame = font->glyph_index[ch];
        int advance = glyph_advance(font, ch) * scale;
        if (frame >= 0 && frame < font->sprite.numlumps) {
            const spritecell_t *cell = &font->sprite.cells[frame];
            irect_t src = cell->rect;
            if (!font->native_origin && cell->bounds.w > 0 && cell->bounds.h > 0) {
                src.x += cell->bounds.x;
                src.y += cell->bounds.y;
                src.w = cell->bounds.w;
                src.h = cell->bounds.h;
            }
            if (src.w > 0 && src.h > 0) {
                irect_t dst = {
                    cx + (font->native_origin ? cell->displacement.x * scale : 0),
                    cy + (font->native_origin ? cell->displacement.y * scale : 0),
                    (src.w * scale + divisor - 1) / divisor,
                    (src.h * scale + divisor - 1) / divisor,
                };
                V_DrawSpriteCellScaled(dst, &font->sprite, frame, &src, remap, 0);
            }
        }
        cx += advance;
    }
}

void V_DrawText(ivec2_t at, const bitmapfont_t *font, const char *text, const uint8_t *remap) {
    draw_glyphs(at, font, text, remap, 1);
}

void V_DrawTextScaled(ivec2_t at, const bitmapfont_t *font, const char *text,
                      const uint8_t *remap, int scale) {
    draw_glyphs(at, font, text, remap, scale);
}

static int wrapped_text(irect_t box, const bitmapfont_t *font, const char *text,
                        const uint8_t *remap, int scroll_px, bool draw) {
    if (!font || !text || box.w <= 0) return 0;
    irect_t previous = V_GetClip();
    bool had_clip = clip_set;
    if (draw) V_SetClip(box);
    char line[256];
    int line_len = 0;
    int cy = box.y - scroll_px;
    int line_h = font->line_h > 0 ? font->line_h : font->glyph_size.h;
    const char *word = text;
    while (*word) {
        while (*word == ' ' || *word == '\r' || *word == '\n') {
            if (*word == '\n' && line_len > 0) {
                if (draw) V_DrawText((ivec2_t){box.x, cy}, font, line, remap);
                cy += line_h;
                line[0] = '\0';
                line_len = 0;
            }
            word++;
        }
        if (!*word) break;
        const char *end = word;
        while (*end && *end != ' ' && *end != '\r' && *end != '\n') end++;
        size_t word_len = (size_t)(end - word);
        if (word_len >= sizeof(line)) word_len = sizeof(line) - 1;
        char candidate[256];
        if (line_len > 0)
            snprintf(candidate, sizeof(candidate), "%s %.*s", line, (int)word_len, word);
        else
            snprintf(candidate, sizeof(candidate), "%.*s", (int)word_len, word);
        if (line_len > 0 && V_TextWidth(font, candidate) > box.w) {
            if (draw) V_DrawText((ivec2_t){box.x, cy}, font, line, remap);
            cy += line_h;
            snprintf(line, sizeof(line), "%.*s", (int)word_len, word);
        } else {
            snprintf(line, sizeof(line), "%s", candidate);
        }
        line_len = (int)strlen(line);
        word = end;
    }
    if (line_len > 0) {
        if (draw) V_DrawText((ivec2_t){box.x, cy}, font, line, remap);
        cy += line_h;
    }
    if (draw) {
        if (had_clip) V_SetClip(previous);
        else V_SetClip((irect_t){0});
    }
    return cy - box.y + scroll_px;
}

void V_DrawTextWrapped(irect_t box, const bitmapfont_t *font, const char *text,
                       const uint8_t *remap, int scroll_px) {
    wrapped_text(box, font, text, remap, scroll_px, true);
}

int V_TextWrappedHeight(int width, const bitmapfont_t *font, const char *text) {
    return wrapped_text((irect_t){.w = width}, font, text, NULL, 0, false);
}

/* Five-column glyphs for labels and prices; no game font is required. */
void V_DrawSmallText(irect_t box, const char *text, uint32_t argb, isize2_t space) {
    if (!text || space.w <= 0 || space.h <= 0) return;
    ivec2_t point = {box.x, box.y};
    int width = box.w;
    static const uint8_t glyphs[][5] = {
        {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},
        {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},
        {6,0x49,0x49,0x29,0x1e},
        {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
        {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},
        {0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
        {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
        {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
        {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
        {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},
    };
    uint8_t color = V_NearestIndex(argb);
    for (int n = 0; text[n] && n * 6 + 5 <= width; ++n) {
        int ch = toupper((unsigned char)text[n]);
        int glyph = ch >= '0' && ch <= '9' ? ch - '0' :
                    ch >= 'A' && ch <= 'Z' ? ch - 'A' + 10 : -1;
        static const unsigned char punctuation[][5] = {
            {0,0x36,0x36,0,0}, {8,8,8,8,8}, {0x63,0x13,8,0x64,0x63}, {0x40,0x20,0x10,8,4},
        };
        const unsigned char *bits = glyph >= 0 ? glyphs[glyph] :
            ch == ':' ? punctuation[0] : ch == '-' ? punctuation[1] :
            ch == '%' ? punctuation[2] : ch == '/' ? punctuation[3] : NULL;
        if (!bits) continue;
        for (int x = 0; x < 5; ++x)
            for (int y = 0; y < 7; ++y)
                if (bits[x] & (1u << y)) {
                    irect_t pixel = { (point.x + n * 6 + x) * screens[0].w / space.w,
                        (point.y + y) * screens[0].h / space.h,
                        screens[0].w / space.w, screens[0].h / space.h };
                    if (pixel.w < 1) pixel.w = 1;
                    if (pixel.h < 1) pixel.h = 1;
                    V_FillRect(pixel, color);
                }
    }
}
