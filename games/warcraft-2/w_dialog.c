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

/* Checkbox 0x5e8cc and radio 0x5e990. A checked box is MS_PUSHED even at
 * rest, so the pressed frame follows the pointer, and the on/off frame
 * follows the value. Text is mode 0x21 at the measured frame width plus 4:
 * checkbox frame 22, radio frame 27 (0x5910c). */
static bool item_focused(const menu_t *menu, const menuitem_t *item) {
    return menu && menu->itemOn >= 0 && menu->itemOn < menu->numitems &&
           &menu->items[menu->itemOn] == item;
}

static bool pointer_down(const menu_t *menu, const menuitem_t *item) {
    return menu && (menu->keyheld == item || (menu->held == item && menu->over));
}

static void draw_check(const menu_t *menu, const menuitem_t *item, menustate_t state, irect_t rect) {
    bool radio = W2_ITEM_KIND(item->flags) == 3;
    bool disabled = state == MS_DISABLED;
    bool pressed = pointer_down(menu, item);
    int on = item->value ? 1 : 0;
    int frame = radio ? (disabled ? 0x17 : pressed ? on * 2 + 0x19 : on * 2 + 0x18)
                      : (disabled ? 0x12 : pressed ? on * 2 + 0x14 : on * 2 + 0x13);
    draw_frame(item->sheet, frame, (ivec2_t){rect.x, rect.y}, (isize2_t){rect.w, rect.h});
    int indent = frame_size(item->sheet, radio ? 0x1b : 0x16).w + 4;
    if (item->text[0])
        draw_text(font_for(item->flags), TEXT_LEFT | TEXT_VCENTER,
                  (ivec2_t){rect.x + indent, rect.y + (rect.h + 1) / 2}, item->text,
                  disabled ? 5 : 2, item->mark_at, item->mark_len);
    if (item_focused(menu, item)) V_DrawRectOutline(rect, 0xf7);
}

/* Horizontal slider 0x5ea8c. Caps are frames 34..39, the track is 43/44
 * placed at the cap width (the sheet's x offset of 20, which the loader does
 * not keep), and the knob is frame 40. 0x64548's travel starts as
 * (right − left) − knob − 2×cap; its value multiply was not traced. The grab
 * is two caps plus the 17px knob so the engine's drag centre matches a knob
 * drawn between the caps. */
static void draw_hslider(const menu_t *menu, const menuitem_t *item, menustate_t state, irect_t rect) {
    bool disabled = state == MS_DISABLED;
    bool pressed = !disabled && pointer_down(menu, item);
    int enabled = disabled ? 0 : 1;
    int down = pressed ? 1 : 0;
    isize2_t cap = frame_size(item->sheet, 0x22);
    if (cap.w <= 0) cap = (isize2_t){20, 19};
    draw_frame(item->sheet, 0x22 + enabled + down, (ivec2_t){rect.x, rect.y}, cap);
    if (rect.w > 2 * cap.w)
        draw_frame(item->sheet, 0x2b + enabled, (ivec2_t){rect.x + cap.w, rect.y},
                   (isize2_t){rect.w - 2 * cap.w, cap.h});
    draw_frame(item->sheet, 0x25 + enabled + down, (ivec2_t){rect.x + rect.w - cap.w, rect.y}, cap);
    if (!disabled) {
        isize2_t knob = frame_size(item->sheet, 0x28);
        if (knob.w <= 0) knob = (isize2_t){17, 17};
        int grab = item->thumb.part.w > 0 ? item->thumb.part.w : cap.w * 2 + knob.w;
        int span = item->range.max - item->range.min;
        int value = item->value;
        if (value < item->range.min) value = item->range.min;
        if (value > item->range.max) value = item->range.max;
        int travel = rect.w - grab;
        int thumb = rect.x + (span > 0 && travel > 0 ? travel * (value - item->range.min) / span : 0);
        draw_frame(item->sheet, 0x28,
                   (ivec2_t){thumb + (grab - knob.w) / 2, rect.y + (cap.h - knob.h) / 2}, knob);
    }
    if (item_focused(menu, item)) V_DrawRectOutline(rect, 0xfb);
}

/* Text field 0x5ed24. The save name's flags are 0x0018, so it has no 0xfb
 * focus rim (that needs 0x8000 and 0x1000). The caret is ours because this
 * drawer replaces the engine's. */
static void draw_field(const menu_t *menu, const menuitem_t *item, irect_t rect, unsigned flags) {
    bool focus = item_focused(menu, item);
    if ((item->flags & 0x8000) && focus) V_DrawRectOutline(rect, 0xfb);
    const bitmapfont_t *font = font_for(flags);
    if (!font) return;
    char shown[140];
    snprintf(shown, sizeof(shown), "%s%s", item->text, focus ? "_" : "");
    draw_text(font, TEXT_LEFT | TEXT_VCENTER, (ivec2_t){rect.x + 3, rect.y + (rect.h + 1) / 2},
              shown, flags & 0x0002 ? 5 : 2, 0, 0);
}

/* Long text uses the dialog advance (glyph width + 1) and the dialog colour
 * maps. The engine's wrapped text does neither. first_row scrolls by lines. */
static void draw_prose(const menuitem_t *item, irect_t rect, unsigned flags) {
    const bitmapfont_t *font = font_for(flags);
    const char *text = item->prose;
    if (!font || !text || !*text || rect.w <= 0 || rect.h <= 0) return;
    irect_t clip = V_GetClip();
    V_SetClip(rect);
    int line_h = font->glyph_size.h + LINE_GAP;
    int skip = item->first_row > 0 ? item->first_row : 0;
    int y = rect.y;
    int colour = text_colour(flags, true);
    const char *p = text;
    while (*p) {
        const char *end = p, *space = NULL;
        int width = 0;
        while (*end && *end != '\n') {
            int step = advance(font, (unsigned char)*end);
            if (end > p && width + step > rect.w) break;
            if (*end == ' ') space = end;
            width += step;
            ++end;
        }
        const char *draw_end = end, *next = end;
        if (*end == '\n') next = end + 1;
        else if (*end && space) { draw_end = space; next = space + 1; }
        if (next == p) next = p + 1;
        if (skip > 0) --skip;
        else if (y + font->glyph_size.h <= rect.y + rect.h) {
            char line[256];
            size_t n = (size_t)(draw_end - p);
            if (n >= sizeof(line)) n = sizeof(line) - 1;
            memcpy(line, p, n);
            line[n] = '\0';
            draw_text(font, TEXT_TOP | TEXT_LEFT, (ivec2_t){rect.x, y}, line, colour, 0, 0);
            y += line_h;
        } else break;
        p = next;
    }
    V_SetClip(clip);
}

/* 0x5ebd8, between its arrows (buttons of their own, unless the bar has
 * W2_ITEM_ARROWS). The track's frame starts below the up arrow. The knob
 * moves evenly over the track; 0x64548's vertical multiply was not traced. */
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
    if (!art || item->ownerdraw) return false;
    unsigned flags = state_flags(item, state);
    switch (item->kind) {
    case MI_BUTTON:
        draw_button(item, state, rect, flags);
        return true;
    case MI_STATIC:
        if (item->prose) draw_prose(item, rect, flags);
        else draw_caption(item, rect, flags);
        return true;
    case MI_CHECK:
        draw_check(menu, item, state, rect);
        return true;
    case MI_SLIDER:
        draw_hslider(menu, item, state, rect);
        return true;
    case MI_TEXTFIELD:
        draw_field(menu, item, rect, flags);
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
