#include "w2_local.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* IMG, GFU and FONT are the retail UI records. GRP icons go through the
 * sprite decoder. Index 0 stays opaque on chrome and transparent on icons
 * and the font, which is what the blitter keys on. */

static unsigned u16_at(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned u32_at(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static bool open_data(w2_archive_t *arc, const char *root, const char *file) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/DATA/%s", root && root[0] ? root : "data/WAR2", file);
    if (w2_archive_open(arc, path)) return true;
    fprintf(stderr, "warcraft-2: cannot open %s\n", path);
    return false;
}

static bool take_entry(const w2_archive_t *arc, int index, w2_blob_t *out) {
    if (w2_archive_extract(arc, index, out)) return true;
    fprintf(stderr, "warcraft-2: entry %d failed\n", index);
    return false;
}

static bool install_raw(spritesheet_t *out, uint8_t *pixels, int w, int h,
                        const uint32_t palette[256]) {
    memset(out, 0, sizeof(*out));
    if (!pixels || w < 1 || h < 1 || !R_AllocSpriteCells(out, 1)) {
        free(pixels);
        return false;
    }
    out->lumps[0].indices = pixels;
    out->cells[0].rect = (irect_t){0, 0, w, h};
    out->cells[0].bounds = out->cells[0].rect;
    out->frame_size = (isize2_t){w, h};
    out->indexed = true;
    memcpy(out->palette, palette, sizeof(out->palette));
    memcpy(out->source_palette, palette, sizeof(out->source_palette));
    return true;
}

static bool decode_img(const w2_blob_t *entry, const uint32_t palette[256],
                       spritesheet_t *out) {
    memset(out, 0, sizeof(*out));
    if (!entry || entry->size < 4) return false;
    int w = (int)u16_at(entry->data);
    int h = (int)u16_at(entry->data + 2);
    if (w < 1 || h < 1 || w > 2048 || h > 2048) return false;
    if (entry->size < 4u + (size_t)w * (size_t)h) return false;
    uint8_t *pixels = malloc((size_t)w * (size_t)h);
    if (!pixels) return false;
    memcpy(pixels, entry->data + 4, (size_t)w * (size_t)h);
    return install_raw(out, pixels, w, h, palette);
}

static bool gfu_header(const w2_blob_t *entry, int index, unsigned *xoff, unsigned *yoff,
                       unsigned *width, unsigned *height, unsigned *offset) {
    const uint8_t *hdr = entry->data + 6 + (size_t)index * 8;
    *xoff = hdr[0];
    *yoff = hdr[1];
    *width = hdr[2];
    *height = hdr[3];
    *offset = u32_at(hdr + 4);
    if (*offset & 0x80000000u) {
        *offset &= 0x7fffffffu;
        *width += 256;
    }
    if (*width > 2048 || *height > 2048) return false;
    if (*width == 0 || *height == 0) return true;
    return (size_t)*offset + (size_t)*width * (size_t)*height <= entry->size;
}

static bool decode_gfu(const w2_blob_t *entry, const uint32_t palette[256],
                       spritesheet_t *out) {
    memset(out, 0, sizeof(*out));
    if (!entry || entry->size < 6) return false;
    int count = (int)u16_at(entry->data);
    int max_w = (int)u16_at(entry->data + 2);
    int max_h = (int)u16_at(entry->data + 4);
    if (count < 1 || count > 256 || entry->size < 6u + (size_t)count * 8u) return false;
    if (!R_AllocSpriteCells(out, count)) return false;
    out->frame_size = (isize2_t){max_w, max_h};
    out->indexed = true;
    memcpy(out->palette, palette, sizeof(out->palette));
    memcpy(out->source_palette, palette, sizeof(out->source_palette));
    int kept = 0;
    for (int i = 0; i < count; ++i) {
        unsigned xoff, yoff, width, height, offset;
        if (!gfu_header(entry, i, &xoff, &yoff, &width, &height, &offset)) continue;
        if (width == 0 || height == 0) continue;
        uint8_t *pixels = malloc((size_t)width * (size_t)height);
        if (!pixels) continue;
        memcpy(pixels, entry->data + offset, (size_t)width * (size_t)height);
        out->lumps[i].indices = pixels;
        out->cells[i].rect = (irect_t){0, 0, (int)width, (int)height};
        out->cells[i].bounds = out->cells[i].rect;
        (void)xoff;
        (void)yoff;
        kept++;
    }
    if (kept) return true;
    R_FreeSprite(out);
    return false;
}

static void font_rle(uint8_t *dst, int pitch, int width, int height,
                     const uint8_t *sp, const uint8_t *end) {
    int x = 0, y = 0;
    while (sp < end && y < height) {
        unsigned ctrl = *sp++;
        x += (int)((ctrl >> 3) & 0x1fu);
        while (x >= width) {
            x -= width;
            if (++y >= height) return;
        }
        if (x >= 0 && x < width) dst[y * pitch + x] = (uint8_t)((ctrl & 7u) + 1);
        if (++x >= width) {
            x -= width;
            if (++y >= height) return;
        }
    }
}

static bool decode_font(const w2_blob_t *entry, const uint32_t colours[256], bitmapfont_t *font) {
    memset(font, 0, sizeof(*font));
    for (int i = 0; i < 128; ++i) font->glyph_index[i] = -1;
    if (!entry || entry->size < 8 || memcmp(entry->data, "FONT ", 5) != 0) return false;
    int count = (int)entry->data[5] - 32;
    int max_w = entry->data[6];
    int max_h = entry->data[7];
    if (count < 1 || count > 256 || max_w < 1 || max_h < 1 || max_w > 128 || max_h > 128)
        return false;
    if (entry->size < 8u + (size_t)count * 4u) return false;
    if (!R_AllocSpriteCells(&font->sprite, 96)) return false;
    uint32_t palette[256] = { 0 };
    static const uint8_t white[] = {239, 246, 246, 246, 104, 239, 239, 239};
    static const uint8_t yellow[] = {246, 200, 199, 197, 192, 239, 104, 239};
    for (int i = 0; i < 8; ++i) {
        palette[i + 1] = colours[white[i]];
        palette[i + 9] = colours[yellow[i]];
    }
    font->sprite.indexed = true;
    font->sprite.frame_size = (isize2_t){max_w, max_h};
    memcpy(font->sprite.palette, palette, sizeof(palette));
    memcpy(font->sprite.source_palette, palette, sizeof(palette));
    font->glyph_size = (isize2_t){max_w, max_h};
    font->line_h = max_h;
    font->native_origin = true;
    font->own_palette = true;
    font->sprite.palette_maps = calloc(1, sizeof(*font->sprite.palette_maps));
    if (!font->sprite.palette_maps) { HU_FreeFont(font); return false; }
    font->sprite.palette_map_count = 1;
    spritepalettemap_t *map = font->sprite.palette_maps;
    map->id = 1;
    for (int i = 0; i < 256; ++i) map->indices[i] = (uint8_t)i;
    for (int i = 1; i <= 8; ++i) map->indices[i] = (uint8_t)(i + 8);
    int cell = 0;
    int indexed = count < 96 ? count : 96;
    for (int i = 0; i < indexed && cell < 96; ++i) {
        int ch = 32 + i;
        unsigned offset = u32_at(entry->data + 8 + (size_t)i * 4u);
        if (!offset || offset + 4 > entry->size) {
            if (ch != ' ') continue;
            int gap = max_w / 2;
            if (gap < 1) gap = 1;
            uint8_t *blank = calloc((size_t)gap * (size_t)max_h, 1);
            if (!blank) break;
            font->sprite.lumps[cell].indices = blank;
            font->sprite.cells[cell].rect = (irect_t){0, 0, gap, max_h};
            font->glyph_index[ch] = cell;
            font->glyph_width[ch] = (uint8_t)gap;
            cell++;
            continue;
        }
        const uint8_t *gp = entry->data + offset;
        int width = gp[0];
        int height = gp[1];
        int xoff = gp[2];
        int yoff = gp[3];
        if (width < 1 || height < 1 || width > 128 || height > 128) continue;
        int advance = xoff + width;
        if (advance > max_w || yoff >= max_h) continue;
        uint8_t *image = calloc((size_t)advance * (size_t)max_h, 1);
        if (!image) break;
        int room = max_h - yoff;
        if (room > 0)
            font_rle(image + (size_t)yoff * (size_t)advance + xoff, advance, width,
                     height < room ? height : room, gp + 4, entry->data + entry->size);
        font->sprite.lumps[cell].indices = image;
        font->sprite.cells[cell].rect = (irect_t){0, 0, advance, max_h};
        font->sprite.cells[cell].bounds = font->sprite.cells[cell].rect;
        font->glyph_index[ch] = cell;
        font->glyph_width[ch] = (uint8_t)advance;
        cell++;
    }
    if (font->glyph_index[' '] < 0 && cell < 96) {
        int gap = max_w / 2;
        if (gap < 1) gap = 1;
        uint8_t *blank = calloc((size_t)gap * (size_t)max_h, 1);
        if (blank) {
            font->sprite.lumps[cell].indices = blank;
            font->sprite.cells[cell].rect = (irect_t){0, 0, gap, max_h};
            font->glyph_index[' '] = cell;
            font->glyph_width[' '] = (uint8_t)gap;
            cell++;
        }
    }
    if (cell > 0) return true;
    HU_FreeFont(font);
    return false;
}

static int icon_entry(int era) {
    if (era == 1) return 357;
    if (era == 2 || era == 3) return 358;
    return 356;
}

static bool load_font(const w2_archive_t *arc, int entry, bitmapfont_t *font) {
    w2_blob_t blob = { 0 };
    w2_blob_t pal = {0};
    uint32_t palette[256];
    if (!take_entry(arc, entry, &blob)) return false;
    bool ok = take_entry(arc, 2, &pal) && w2_decode_palette(&pal, palette) &&
              decode_font(&blob, palette, font);
    w2_blob_free(&pal);
    w2_blob_free(&blob);
    if (!ok) fprintf(stderr, "warcraft-2: font entry %d failed\n", entry);
    return ok;
}

bool HU_LoadFont(const char *root, bitmapfont_t *font) {
    if (!font) return false;
    HU_FreeFont(font);
    w2_archive_t arc;
    if (!open_data(&arc, root, "MAINDAT.WAR")) return false;
    bool ok = load_font(&arc, 282, font);
    w2_archive_close(&arc);
    return ok;
}

static bool load_img_entry(const w2_archive_t *arc, int index, const uint32_t palette[256],
                           spritesheet_t *out) {
    w2_blob_t blob = { 0 };
    if (!take_entry(arc, index, &blob)) return false;
    bool ok = decode_img(&blob, palette, out);
    w2_blob_free(&blob);
    if (!ok) fprintf(stderr, "warcraft-2: entry %d failed\n", index);
    return ok;
}

static bool load_gfu_entry(const w2_archive_t *arc, int index, const uint32_t palette[256],
                           spritesheet_t *out) {
    w2_blob_t blob = { 0 };
    if (!take_entry(arc, index, &blob)) return false;
    bool ok = decode_gfu(&blob, palette, out);
    if (!ok) fprintf(stderr, "warcraft-2: entry %d failed\n", index);
    w2_blob_free(&blob);
    return ok;
}

void w2_free_menu_art(w2_menu_art_t *art) {
    if (!art) return;
    for (int i = 0; i < 2; ++i) {
        R_FreeSprite(&art->widgets[i]);
        R_FreeSprite(&art->panel[i]);
    }
    R_FreeSprite(&art->title);
    HU_FreeFont(&art->font);
    memset(art, 0, sizeof(*art));
}

void w2_free_hud_art(w2_hud_art_t *art) {
    if (!art) return;
    R_FreeSprite(&art->menu_button);
    R_FreeSprite(&art->menu_widgets);
    R_FreeSprite(&art->minimap);
    R_FreeSprite(&art->info);
    R_FreeSprite(&art->buttons);
    R_FreeSprite(&art->resource);
    R_FreeSprite(&art->status);
    R_FreeSprite(&art->filler);
    R_FreeSprite(&art->icons);
    R_FreeSprite(&art->resource_icons);
    HU_FreeFont(&art->font);
    HU_FreeFont(&art->small_font);
    memset(art, 0, sizeof(*art));
}

bool w2_load_menu_art(const char *root, w2_menu_art_t *art) {
    if (!art) return false;
    w2_free_menu_art(art);
    w2_archive_t rez;
    if (!open_data(&rez, root, "REZDAT.WAR")) return false;
    w2_blob_t pal = { 0 };
    uint32_t palette[256];
    bool colors = take_entry(&rez, 14, &pal) && w2_decode_palette(&pal, palette);
    w2_blob_free(&pal);
    if (!colors) fprintf(stderr, "warcraft-2: entry 14 failed\n");
    bool widgets = colors &&
        load_gfu_entry(&rez, 0, palette, &art->widgets[0]) &&
        load_gfu_entry(&rez, 1, palette, &art->widgets[1]);
    bool panels = colors &&
        load_img_entry(&rez, 3, palette, &art->panel[0]) &&
        load_img_entry(&rez, 4, palette, &art->panel[1]);
    bool title = colors && load_img_entry(&rez, 13, palette, &art->title);
    w2_archive_close(&rez);
    w2_archive_t maindat;
    bool font = false;
    if (open_data(&maindat, root, "MAINDAT.WAR")) {
        font = load_font(&maindat, 282, &art->font);
        w2_archive_close(&maindat);
    }
    art->ready = widgets && panels && title && font;
    return art->ready;
}

bool w2_load_hud_art(const char *root, int era, bool orc, w2_hud_art_t *art) {
    if (!art) return false;
    w2_free_hud_art(art);
    art->orc = orc;
    w2_archive_t arc;
    if (!open_data(&arc, root, "MAINDAT.WAR")) return false;
    w2_blob_t pal = { 0 };
    uint32_t palette[256];
    int pal_i = w2_era_palette(era);
    bool colors = take_entry(&arc, pal_i, &pal) && w2_decode_palette(&pal, palette);
    w2_blob_free(&pal);
    if (!colors) fprintf(stderr, "warcraft-2: entry %d failed\n", pal_i);
    int side = orc ? 1 : 0;
    static const int chrome[] = { 293, 295, 297, 287, 291, 289 };
    spritesheet_t *slots[] = {
        &art->menu_button, &art->minimap, &art->buttons,
        &art->resource, &art->status, &art->filler
    };
    bool plates = colors;
    bool info = false;
    bool icon_ok = false;
    bool font = false;
    if (colors) {
        for (int i = 0; i < 6; ++i)
            plates = load_img_entry(&arc, chrome[i] + side, palette, slots[i]) && plates;
        info = load_gfu_entry(&arc, 354 + side, palette, &art->info);
        w2_blob_t icons = { 0 };
        int icon_i = icon_entry(era);
        if (take_entry(&arc, icon_i, &icons))
            icon_ok = w2_decode_grp(&icons, palette, &art->icons, false, NULL);
        if (!icon_ok) fprintf(stderr, "warcraft-2: entry %d failed\n", icon_i);
        w2_blob_free(&icons);
        font = load_font(&arc, 282, &art->font) && load_font(&arc, 283, &art->small_font);
        plates = load_gfu_entry(&arc, 187, palette, &art->resource_icons) && plates;
    }
    w2_archive_close(&arc);
    bool widgets = false;
    w2_archive_t rez;
    if (open_data(&rez, root, "REZDAT.WAR")) {
        w2_blob_t pal = {0};
        widgets = take_entry(&rez, 14, &pal) && w2_decode_palette(&pal, palette) &&
            load_gfu_entry(&rez, side, palette, &art->menu_widgets);
        w2_blob_free(&pal);
        w2_archive_close(&rez);
    }
    art->ready = plates && info && icon_ok && font && widgets;
    return art->ready;
}
