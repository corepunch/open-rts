#include "v_video.h"

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

typedef struct {
    uint32_t hash;
    uint8_t map[256];
    bool used;
} remap_slot_t;

static remap_slot_t remap_cache[8];

static void ensure_identity(void) {
    if (identity_ready) return;
    for (int i = 0; i < 256; ++i) identity_map[i] = (uint8_t)i;
    identity_ready = true;
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
    memcpy(vpalette, argb, sizeof(vpalette));
    palette_set = true;
    memset(remap_cache, 0, sizeof(remap_cache));
}

bool I_ReadScreen(uint8_t *dst) {
    if (!dst || !screens[0].pixels) return false;
    memcpy(dst, screens[0].pixels, (size_t)screens[0].w * (size_t)screens[0].h);
    return true;
}

uint8_t V_NearestIndex(uint32_t argb) {
    int r = (int)((argb >> 16) & 255);
    int g = (int)((argb >> 8) & 255);
    int b = (int)(argb & 255);
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
    return (uint8_t)best;
}

static uint32_t hash_palette(const uint32_t colors[256]) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 256; ++i) {
        hash ^= colors[i];
        hash *= 16777619u;
    }
    return hash ? hash : 1u;
}

const uint8_t *V_RemapPalette(const uint32_t palette[256]) {
    ensure_identity();
    if (!palette || !palette_set) return identity_map;
    if (memcmp(palette, vpalette, sizeof(vpalette)) == 0) return identity_map;
    uint32_t hash = hash_palette(palette);
    for (int i = 0; i < 8; ++i) {
        if (remap_cache[i].used && remap_cache[i].hash == hash) return remap_cache[i].map;
    }
    int slot = 0;
    for (int i = 0; i < 8; ++i) if (!remap_cache[i].used) { slot = i; break; }
    remap_slot_t *entry = &remap_cache[slot];
    entry->used = true;
    entry->hash = hash;
    entry->map[0] = 0;
    for (int i = 1; i < 256; ++i) {
        uint32_t saved = vpalette[0];
        /* Nearest is against the screen palette already installed. */
        (void)saved;
        int r = (int)((palette[i] >> 16) & 255);
        int g = (int)((palette[i] >> 8) & 255);
        int b = (int)(palette[i] & 255);
        int best = 1;
        int best_distance = 1 << 30;
        for (int k = 1; k < 256; ++k) {
            int dr = r - (int)((vpalette[k] >> 16) & 255);
            int dg = g - (int)((vpalette[k] >> 8) & 255);
            int db = b - (int)(vpalette[k] & 255);
            int distance = dr * dr + dg * dg + db * db;
            if (distance < best_distance) {
                best_distance = distance;
                best = k;
                if (distance == 0) break;
            }
        }
        entry->map[i] = (uint8_t)best;
    }
    return entry->map;
}

void V_ComposeRemap(uint8_t out[256], const uint32_t palette[256],
                    const uint8_t *team, int intensity) {
    if (!out) return;
    if (intensity <= 0 || intensity > 16) intensity = 16;
    const uint8_t *to_screen = V_RemapPalette(palette);
    if (intensity == 16) {
        for (int i = 0; i < 256; ++i) {
            uint8_t index = team ? team[i] : (uint8_t)i;
            out[i] = to_screen[index];
        }
        out[0] = 0;
        return;
    }
    int factor = (intensity * 255 + 8) / 16;
    out[0] = 0;
    for (int i = 1; i < 256; ++i) {
        uint8_t index = team ? team[i] : (uint8_t)i;
        uint32_t color = palette ? palette[index] : vpalette[index];
        int r = ((int)((color >> 16) & 255) * factor) / 255;
        int g = ((int)((color >> 8) & 255) * factor) / 255;
        int b = ((int)(color & 255) * factor) / 255;
        uint32_t saved0 = vpalette[0];
        vpalette[0] = 0xff000000u;
        out[i] = V_NearestIndex(0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
        vpalette[0] = saved0;
        if (!palette_set) out[i] = index;
    }
}

void V_ModulateRemap(uint8_t out[256], const uint32_t palette[256], uint32_t color) {
    if (!out) return;
    int cr = (int)((color >> 16) & 255);
    int cg = (int)((color >> 8) & 255);
    int cb = (int)(color & 255);
    const uint8_t *to_screen = V_RemapPalette(palette);
    out[0] = 0;
    if (cr == 255 && cg == 255 && cb == 255) {
        for (int i = 1; i < 256; ++i)
            out[i] = to_screen ? to_screen[i] : (uint8_t)i;
        return;
    }
    for (int i = 1; i < 256; ++i) {
        uint32_t src = palette ? palette[i] : vpalette[i];
        int r = ((int)((src >> 16) & 255) * cr) / 255;
        int g = ((int)((src >> 8) & 255) * cg) / 255;
        int b = ((int)(src & 255) * cb) / 255;
        uint32_t saved0 = vpalette[0];
        vpalette[0] = 0xff000000u;
        out[i] = V_NearestIndex(0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
        vpalette[0] = saved0;
    }
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

static uint8_t sample_index(const uint8_t *src, isize2_t size, int pitch,
                            int x, int y, const uint8_t *remap, uint32_t flags) {
    if (flags & V_FLIP_X) x = size.w - 1 - x;
    if (flags & V_FLIP_Y) y = size.h - 1 - y;
    if (x < 0 || y < 0 || x >= size.w || y >= size.h) return 0;
    uint8_t index = src[(size_t)y * (size_t)pitch + (size_t)x];
    if (remap) index = remap[index];
    return index;
}

void V_DrawBlock(ivec2_t at, const uint8_t *src, isize2_t size, int src_pitch,
                 const uint8_t *remap, uint32_t flags) {
    if (!src || size.w <= 0 || size.h <= 0 || src_pitch <= 0) return;
    irect_t area;
    if (!intersect_bounds((irect_t){at.x, at.y, size.w, size.h}, &area)) return;
    bool opaque = (flags & V_OPAQUE) != 0;
    for (int y = 0; y < area.h; ++y) {
        int sy = area.y - at.y + y;
        uint8_t *dst = screens[0].pixels + ((size_t)(area.y + y) * (size_t)screens[0].w + (size_t)area.x);
        for (int x = 0; x < area.w; ++x) {
            uint8_t index = sample_index(src, size, src_pitch, area.x - at.x + x, sy, remap, flags);
            if (!opaque && index == 0) continue;
            dst[x] = index;
        }
    }
}

void V_DrawBlockScaled(irect_t dst_rect, const uint8_t *src, isize2_t size, int src_pitch,
                       const uint8_t *remap, uint32_t flags) {
    if (!src || size.w <= 0 || size.h <= 0 || dst_rect.w <= 0 || dst_rect.h <= 0) return;
    irect_t area;
    if (!intersect_bounds(dst_rect, &area)) return;
    bool opaque = (flags & V_OPAQUE) != 0;
    for (int y = 0; y < area.h; ++y) {
        int local_y = area.y - dst_rect.y + y;
        int sy = local_y * size.h / dst_rect.h;
        uint8_t *dst = screens[0].pixels + ((size_t)(area.y + y) * (size_t)screens[0].w + (size_t)area.x);
        for (int x = 0; x < area.w; ++x) {
            int local_x = area.x - dst_rect.x + x;
            int sx = local_x * size.w / dst_rect.w;
            uint8_t index = sample_index(src, size, src_pitch, sx, sy, remap, flags);
            if (!opaque && index == 0) continue;
            dst[x] = index;
        }
    }
}

void V_DrawBlockTranslucent(ivec2_t at, const uint8_t *src, isize2_t size, int src_pitch,
                            const uint8_t *table, uint32_t flags) {
    if (!src || !table || size.w <= 0 || size.h <= 0) return;
    irect_t area;
    if (!intersect_bounds((irect_t){at.x, at.y, size.w, size.h}, &area)) return;
    for (int y = 0; y < area.h; ++y) {
        int sy = area.y - at.y + y;
        uint8_t *dst = screens[0].pixels + ((size_t)(area.y + y) * (size_t)screens[0].w + (size_t)area.x);
        for (int x = 0; x < area.w; ++x) {
            uint8_t index = sample_index(src, size, src_pitch, area.x - at.x + x, sy, NULL, flags);
            if (index == 0) continue;
            dst[x] = table[((size_t)index << 8) | dst[x]];
        }
    }
}

void V_DrawSilhouetteColormap(ivec2_t at, const uint8_t *src, isize2_t size, int src_pitch,
                              const uint8_t *colormap, uint32_t flags) {
    if (!src || !colormap || size.w <= 0 || size.h <= 0) return;
    irect_t area;
    if (!intersect_bounds((irect_t){at.x, at.y, size.w, size.h}, &area)) return;
    for (int y = 0; y < area.h; ++y) {
        int sy = area.y - at.y + y;
        uint8_t *dst = screens[0].pixels + ((size_t)(area.y + y) * (size_t)screens[0].w + (size_t)area.x);
        for (int x = 0; x < area.w; ++x) {
            uint8_t index = sample_index(src, size, src_pitch, area.x - at.x + x, sy, NULL, flags);
            if (index == 0) continue;
            dst[x] = colormap[dst[x]];
        }
    }
}

void V_CopyRect(irect_t src, irect_t dst) {
    if (!screens[0].pixels || src.w <= 0 || src.h <= 0 || dst.w <= 0 || dst.h <= 0) return;
    int width = src.w < dst.w ? src.w : dst.w;
    int height = src.h < dst.h ? src.h : dst.h;
    uint8_t *temp = malloc((size_t)width);
    if (!temp) return;
    for (int y = 0; y < height; ++y) {
        int sy = src.y + y;
        int dy = dst.y + y;
        if (sy < 0 || dy < 0 || sy >= screens[0].h || dy >= screens[0].h) continue;
        int row_w = width;
        int sx = src.x;
        int dx = dst.x;
        if (sx < 0) { row_w += sx; dx -= sx; sx = 0; }
        if (dx < 0) { row_w += dx; sx -= dx; dx = 0; }
        if (sx + row_w > screens[0].w) row_w = screens[0].w - sx;
        if (dx + row_w > screens[0].w) row_w = screens[0].w - dx;
        if (row_w <= 0) continue;
        const uint8_t *from = screens[0].pixels + (size_t)sy * (size_t)screens[0].w + (size_t)sx;
        uint8_t *to = screens[0].pixels + (size_t)dy * (size_t)screens[0].w + (size_t)dx;
        memcpy(temp, from, (size_t)row_w);
        memcpy(to, temp, (size_t)row_w);
    }
    free(temp);
}

static const uint8_t *sprite_team_map(const spritesheet_t *sprite, int palette) {
    if (!sprite || palette < 0) return NULL;
    for (int i = 0; i < sprite->palette_map_count; ++i)
        if (sprite->palette_maps[i].id == palette) return sprite->palette_maps[i].indices;
    return NULL;
}

static bool sprite_source(const spritesheet_t *sprite, int frame, const irect_t *src,
                          const uint8_t **indices, isize2_t *size, irect_t *local) {
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
    *local = rect;
    return true;
}

void V_DrawSpriteCell(ivec2_t at, const spritesheet_t *sheet, int cell,
                      const uint8_t *remap, uint32_t flags) {
    const uint8_t *indices;
    isize2_t size;
    irect_t local;
    if (!sprite_source(sheet, cell, NULL, &indices, &size, &local)) return;
    V_DrawBlock(at, indices, size, sheet->cells[cell].rect.w, remap, flags);
}

void V_DrawSpriteCellScaled(irect_t dst, const spritesheet_t *sheet, int cell,
                            const irect_t *src, const uint8_t *remap, uint32_t flags) {
    const uint8_t *indices;
    isize2_t size;
    irect_t local;
    if (!sprite_source(sheet, cell, src, &indices, &size, &local)) return;
    int pitch = sheet->cells[cell].rect.w;
    if (dst.w == size.w && dst.h == size.h)
        V_DrawBlock((ivec2_t){dst.x, dst.y}, indices, size, pitch, remap, flags);
    else
        V_DrawBlockScaled(dst, indices, size, pitch, remap, flags);
}

bool R_DrawIndexed(const uint8_t *indices, isize2_t size, const uint32_t palette[256],
                   const irect_t *src, const irect_t *dst, uint32_t flags) {
    if (!indices || !palette || size.w <= 0 || size.h <= 0) return false;
    if (!palette_set) I_SetPalette(palette);
    irect_t rect = src ? *src : (irect_t){0, 0, size.w, size.h};
    if (rect.x < 0 || rect.y < 0 || rect.w <= 0 || rect.h <= 0 ||
        rect.x + rect.w > size.w || rect.y + rect.h > size.h) return false;
    if (!dst) return false;
    const uint8_t *remap = V_RemapPalette(palette);
    const uint8_t *source = indices + (size_t)rect.y * (size_t)size.w + (size_t)rect.x;
    if (dst->w == rect.w && dst->h == rect.h)
        V_DrawBlock((ivec2_t){dst->x, dst->y}, source, (isize2_t){rect.w, rect.h}, size.w, remap, flags);
    else
        V_DrawBlockScaled(*dst, source, (isize2_t){rect.w, rect.h}, size.w, remap, flags);
    return true;
}

bool R_DrawSprite(const spritesheet_t *sprite, int frame, int palette,
                  const irect_t *src, const irect_t *dst, uint32_t flags, int intensity) {
    const uint8_t *indices;
    isize2_t size;
    irect_t local;
    if (!sprite_source(sprite, frame, src, &indices, &size, &local) || !dst) return false;
    if (!palette_set) I_SetPalette(sprite->source_palette);
    const uint8_t *team = sprite_team_map(sprite, palette);
    int pitch = sprite->cells[frame].rect.w;
    uint8_t composed[256];
    const uint8_t *remap = team;
    /* Matching palettes at full brightness are one table lookup per pixel. */
    bool full = intensity <= 0 || intensity >= 16;
    bool same = palette_set &&
        memcmp(sprite->source_palette, vpalette, sizeof(vpalette)) == 0;
    if (!(full && same)) {
        uint32_t colors[256];
        for (int i = 0; i < 256; ++i) {
            uint8_t mapped = team ? team[i] : (uint8_t)i;
            colors[i] = (sprite->source_palette[mapped] & 0x00ffffffu) |
                        (sprite->source_palette[i] & 0xff000000u);
        }
        V_ComposeRemap(composed, colors, NULL, intensity);
        remap = composed;
    } else if (team && team[0] != 0) {
        memcpy(composed, team, sizeof(composed));
        composed[0] = 0;
        remap = composed;
    }
    if ((flags & V_OPAQUE) == 0 && remap == composed) composed[0] = 0;
    if (dst->w == size.w && dst->h == size.h)
        V_DrawBlock((ivec2_t){dst->x, dst->y}, indices, size, pitch, remap, flags);
    else
        V_DrawBlockScaled(*dst, indices, size, pitch, remap, flags);
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

void V_DrawTextWrapped(irect_t box, const bitmapfont_t *font, const char *text,
                       const uint8_t *remap, int scroll_px) {
    if (!font || !text || box.w <= 0) return;
    irect_t previous = V_GetClip();
    bool had_clip = clip_set;
    V_SetClip(box);
    char line[256];
    int line_len = 0;
    int cy = box.y - scroll_px;
    int line_h = font->line_h > 0 ? font->line_h : font->glyph_size.h;
    const char *word = text;
    while (*word) {
        while (*word == ' ' || *word == '\r' || *word == '\n') {
            if (*word == '\n' && line_len > 0) {
                V_DrawText((ivec2_t){box.x, cy}, font, line, remap);
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
            V_DrawText((ivec2_t){box.x, cy}, font, line, remap);
            cy += line_h;
            snprintf(line, sizeof(line), "%.*s", (int)word_len, word);
        } else {
            snprintf(line, sizeof(line), "%s", candidate);
        }
        line_len = (int)strlen(line);
        word = end;
    }
    if (line_len > 0) V_DrawText((ivec2_t){box.x, cy}, font, line, remap);
    if (had_clip) V_SetClip(previous);
    else V_SetClip((irect_t){0});
}
