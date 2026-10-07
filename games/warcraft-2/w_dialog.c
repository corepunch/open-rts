#include "w2_local.h"

#include <stdio.h>
#include <string.h>

/* Dialog controls drawn as WAR2.EXE draws them (docs/WAR2_EXE_FINDINGS.md,
 * "Native dialog drawing"). The engine keeps input and layout; every
 * Warcraft II menu hands its items here, so one routine places the text of
 * buttons, captions, lists and dropdowns alike. Coordinates are the native
 * 640x480 ones; right and bottom are inclusive, as in the executable.
 *
 * Draw-handler table 0x5dd70, by native kind: buttons 0x5e83c, captions
 * 0x5ed78 / 0x5eda4 / 0x5ede8, scroll bar 0x5ebd8, list 0x5a9ec,
 * dropdown 0x5b4d0. Text: layout 0x4fee8, glyph walk 0x50108, widths 0x4fbcc.
 * Widget metrics come from the widget sheet's frames (0x5910c). */

static const w2_menu_art_t *art;

void w2_dialog_art(const w2_menu_art_t *menu_art) { art = menu_art; }

/* Font colour maps 0x99580 (2), 0x99560 (3), 0x99568 (4), 0x99570 (5), as
 * 0x10c20 fills them for the front end. A glyph pixel n indexes map[n]; the
 * decoder stores n + 1, and n = 0 writes palette index 0. */
static const uint8_t colour_maps[6][6] = {
    [2] = {0, 0xc8, 0xc7, 0xc5, 0xc0, 0xef},
    [3] = {0, 0xbf, 0xbf, 0xa8, 0xa7, 0xef},
    [4] = {0, 0xf6, 0xf6, 0x6c, 0x68, 0xef},
    [5] = {0, 0x6c, 0x6c, 0x69, 0x66, 0xef},
};

static const uint8_t *colour_remap(int colour) {
    static uint8_t remaps[6][256];
    static bool built;
    if (!built) {
        for (int c = 0; c < 6; ++c)
            for (int n = 0; n < 6; ++n) remaps[c][n + 1] = colour_maps[c][n];
        built = true;
    }
    return remaps[colour >= 2 && colour <= 5 ? colour : 2];
}

/* Flag 0x0800 picks MAINDAT 281 and 0x0400 MAINDAT 283; otherwise the dialog
 * redraw 0x58e8c has set MAINDAT 282 (0x5e148, 0x2913c). */
static const bitmapfont_t *font_for(unsigned flags) {
    if (!art) return NULL;
    const bitmapfont_t *font = flags & 0x0800 ? &art->font : flags & 0x0400 ? &art->tiny_font : &art->small_font;
    return font->sprite.numlumps ? font : art->small_font.sprite.numlumps ? &art->small_font : NULL;
}

/* 0x4fbcc: a glyph advances its width plus one; a space half the cell plus one. */
static int advance(const bitmapfont_t *font, unsigned char ch) {
    if (ch == ' ') return font->glyph_size.w / 2 + 1;
    if (ch < ' ' || font->glyph_index[ch] < 0) return 0;
    return font->glyph_width[ch] + 1;
}

static int line_width(const bitmapfont_t *font, const char *text, size_t length) {
    int width = 0;
    for (size_t i = 0; i < length && text[i]; ++i) width += advance(font, (unsigned char)text[i]);
    return width;
}

enum { LINE_GAP = 3 }; /* 0x995c2, set by 0x4fde8 */
enum { TEXT_LEFT = 0x01, TEXT_HCENTER = 0x02, TEXT_RIGHT = 0x04, TEXT_TOP = 0x10, TEXT_VCENTER = 0x20 };

/* 0x4fee8 then 0x50108. The block is as wide as its widest line and each line
 * starts at the block's left edge. A marked span takes colour 4, as the
 * string's 0x04 ... 0x01 codes do; a disabled control ignores them. */
static void draw_text(const bitmapfont_t *font, int mode, ivec2_t at, const char *text, int colour,
                      int mark_at, int mark_len) {
    if (!font || !text || !text[0]) return;
    int width = 0, lines = 1;
    for (const char *line = text;; ++lines) {
        size_t length = strcspn(line, "\n");
        int w = line_width(font, line, length);
        if (w > width) width = w;
        if (!line[length]) break;
        line += length + 1;
    }
    int height = lines * (font->glyph_size.h + LINE_GAP);
    if (mode & (TEXT_HCENTER | TEXT_RIGHT)) at.x -= mode & TEXT_HCENTER ? width / 2 : width;
    if (mode & TEXT_VCENTER) at.y -= (height - LINE_GAP) / 2;
    int x = at.x, y = at.y;
    for (int i = 0; text[i]; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == '\n') {
            x = at.x;
            y += font->glyph_size.h + LINE_GAP;
            continue;
        }
        bool marked = colour != 5 && mark_len > 0 && i >= mark_at && i < mark_at + mark_len;
        if (ch != ' ' && advance(font, ch) > 0) {
            char glyph[2] = {(char)ch, 0};
            V_DrawText((ivec2_t){x, y}, font, glyph, colour_remap(marked ? 4 : colour));
        }
        x += advance(font, ch);
    }
}

/* 0x5e338 / 0x5e280: a frame drawn at its place, cropped to the room it has. */
static void draw_frame(const spritesheet_t *sheet, int cell, ivec2_t at, isize2_t room) {
    if (!sheet || cell < 0 || cell >= sheet->numlumps) return;
    irect_t src = sheet->cells[cell].rect;
    if (room.w < src.w) src.w = room.w;
    if (room.h < src.h) src.h = room.h;
    if (src.w <= 0 || src.h <= 0) return;
    irect_t dst = {at.x, at.y, src.w, src.h};
    R_DrawSprite(sheet, cell, -1, &src, &dst, V_OPAQUE, 16);
}

static isize2_t frame_size(const spritesheet_t *sheet, int cell) {
    if (!sheet || cell < 0 || cell >= sheet->numlumps) return (isize2_t){0, 0};
    return (isize2_t){sheet->cells[cell].rect.w, sheet->cells[cell].rect.h};
}

/* 0x5dfa8: the outer rim in 0xf8, the rim inside it in the given colour. */
static void draw_rims(irect_t rect, uint8_t inner) {
    V_DrawRectOutline(rect, 0xf8);
    V_DrawRectOutline((irect_t){rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2}, inner);
}

/* The native flags of the item in this state: 0x0002 disabled (drawn so only
 * with the item's disabled_look), 0x0080 hot, 0x1000 focused, 0x4000 pressed. */
static unsigned state_flags(const menuitem_t *item, menustate_t state) {
    unsigned flags = item->flags & 0xffffu;
    if (state == MS_DISABLED) flags |= 0x0002;
    if (state == MS_PUSHED) flags |= 0x4000 | 0x0080;
    if (state == MS_FOCUS) flags |= item->kind == MI_BUTTON ? 0x0080 : 0x1000;
    return flags;
}

static int text_colour(unsigned flags, bool caption) {
    if (flags & 0x0002) return 5;
    if (caption ? (flags & 0x8000) : (flags & 0x0080)) return 4;
    return 2;
}

/* 0x5e83c; the default button (native kind 1) gets a rim in 0xf7 (0x5e77c). */
static void draw_button(const menuitem_t *item, menustate_t state, irect_t rect, unsigned flags) {
    if (item->fill) V_FillRect(rect, V_NearestIndex(item->fill));
    M_MenuDrawPicture(item, state, rect);
    if (W2_ITEM_KIND(item->flags) == 1) V_DrawRectOutline(rect, 0xf7);
    if (!item->text[0]) return;
    int shift = flags & 0x4000 ? 3 : 1; /* 0x88cf4 / 0x88cf8 */
    ivec2_t centre = {rect.x + shift + (rect.w + 1) / 2, rect.y + shift + (rect.h + 1) / 2};
    draw_text(font_for(flags), TEXT_HCENTER | TEXT_VCENTER, centre, item->text,
              text_colour(flags, false), item->mark_at, item->mark_len);
}

/* 0x5ed78 / 0x5eda4 / 0x5ede8: captions hang from the rect's top. */
static void draw_caption(const menuitem_t *item, irect_t rect, unsigned flags) {
    if (item->sheet) M_MenuDrawPicture(item, MS_NORMAL, rect);
    if (!item->text[0]) return;
    int mode = TEXT_TOP | TEXT_LEFT;
    ivec2_t at = {rect.x, rect.y};
    if (item->align & MALIGN_HCENTER) { mode = TEXT_TOP | TEXT_HCENTER; at.x += (rect.w - 1) / 2; }
    else if (item->align & MALIGN_RIGHT) { mode = TEXT_TOP | TEXT_RIGHT; at.x += rect.w - 1; }
    draw_text(font_for(flags), mode, at, item->text, text_colour(flags, true), item->mark_at, item->mark_len);
}

/* 0x5a9ec. rect holds the rows; the control's own rect is two pixels larger
 * on each side. A row's text is cut until it fits the control's width. */
static void draw_list(const menuitem_t *item, irect_t rect, unsigned flags) {
    irect_t outer = {rect.x - 2, rect.y - 2, rect.w + 4, rect.h + 4};
    draw_rims(outer, flags & 0x1000 ? 0xfb : 0);
    const bitmapfont_t *font = font_for(flags);
    int height = item->row_height > 0 ? item->row_height : 18;
    for (int i = 0; i < rect.h / height; ++i) {
        int row = item->first_row + i;
        ivec2_t at = {rect.x, rect.y + i * height};
        draw_frame(item->sheet, 0x2e, at, (isize2_t){rect.w, height + 1});
        if (row >= item->rows || !item->row || !font) continue;
        char text[160];
        snprintf(text, sizeof(text), "%s", item->row(item, row));
        for (size_t length = strlen(text); length && line_width(font, text, length) > outer.w;)
            text[--length] = '\0';
        draw_text(font, TEXT_TOP | TEXT_LEFT, (ivec2_t){at.x + 2, at.y + 2}, text,
                  row == item->value ? 4 : 2, 0, 0);
    }
}

/* 0x5b4d0: the closed control is one row tall plus three, whatever its rect. */
static void draw_dropdown(const menuitem_t *item, irect_t rect, unsigned flags) {
    const spritesheet_t *sheet = item->sheet;
    bool disabled = flags & 0x0002;
    int height = item->row_height > 0 ? item->row_height : 18;
    draw_frame(sheet, disabled ? 0x2d : 0x2e, (ivec2_t){rect.x + 2, rect.y + 2}, (isize2_t){rect.w - 4, height});
    isize2_t arrow = frame_size(sheet, 0x1c);
    draw_frame(sheet, disabled ? 0x1f : 0x20, (ivec2_t){rect.x + rect.w - 1 - arrow.w, rect.y + 1},
               frame_size(sheet, 0x20));
    draw_rims((irect_t){rect.x, rect.y, rect.w, height + 4}, flags & 0x1000 ? 0xfb : 0);
    if (item->row && item->value >= 0 && item->value < item->rows)
        draw_text(font_for(flags), TEXT_TOP | TEXT_LEFT, (ivec2_t){rect.x + 4, rect.y + 4},
                  item->row(item, item->value), disabled ? 5 : 2, 0, 0);
}

/* 0x5ebd8, between its arrows (buttons of their own, unless the bar has
 * W2_ITEM_ARROWS). The
 * track's frame starts below the up arrow; the knob's position routine
 * 0x64548 is not traced, so it moves evenly over the track. */
static void draw_scrollbar(const menu_t *menu, const menuitem_t *item, irect_t rect, unsigned flags) {
    bool disabled = flags & 0x0002;
    if (item->flags & W2_ITEM_ARROWS) {
        isize2_t arrow = frame_size(item->sheet, 0x1c);
        draw_frame(item->sheet, disabled ? 0x1c : 0x1d, (ivec2_t){rect.x, rect.y}, arrow);
        draw_frame(item->sheet, disabled ? 0x1f : 0x20, (ivec2_t){rect.x, rect.y + rect.h - arrow.h}, arrow);
        rect = (irect_t){rect.x, rect.y + arrow.h, rect.w, rect.h - 2 * arrow.h};
    }
    draw_frame(item->sheet, disabled ? 0x29 : 0x2a, (ivec2_t){rect.x, rect.y}, (isize2_t){rect.w, rect.h});
    if (disabled || item->link < 0 || item->link >= menu->numitems) return;
    const menuitem_t *list = &menu->items[item->link];
    isize2_t knob = frame_size(item->sheet, 0x28);
    int page = list->row_height > 0 ? list->rect.h / list->row_height : 1;
    int last = list->rows - page;
    int top = last > 0 ? (rect.h - knob.h) * list->first_row / last : 0;
    draw_frame(item->sheet, 0x28, (ivec2_t){rect.x + (frame_size(item->sheet, 0x1c).w - knob.w) / 2, rect.y + top},
               knob);
}

bool w2_draw_item(const menu_t *menu, const menuitem_t *item, menustate_t state, irect_t rect) {
    if (!art || item->prose || item->ownerdraw) return false;
    unsigned flags = state_flags(item, state);
    switch (item->kind) {
    case MI_BUTTON:
        draw_button(item, state, rect, flags);
        return true;
    case MI_STATIC:
        draw_caption(item, rect, flags);
        return true;
    case MI_LIST:
        if (!item->sheet) return false;
        draw_list(item, rect, flags);
        return true;
    case MI_DROPDOWN:
        if (!item->sheet) return false;
        draw_dropdown(item, rect, flags);
        return true;
    case MI_SCROLLBAR:
        if (!item->sheet) return false;
        draw_scrollbar(menu, item, rect, flags);
        return true;
    default:
        return false;
    }
}
