#include "engine.h"

#include <string.h>

static bool item_live(const menuitem_t *item) {
    return item && item->visible && item->enabled;
}

static menuitem_t *focused(const menu_t *menu) {
    return menu->itemOn >= 0 && menu->itemOn < menu->numitems ? &menu->items[menu->itemOn] : NULL;
}

static menuitem_t *item_at(const menu_t *menu, ivec2_t point) {
    for (int i = 0; i < menu->numitems; ++i) {
        menuitem_t *item = &menu->items[i];
        if (item_live(item) && irect_contains(item->rect, point)) return item;
    }
    return NULL;
}

static void call_routine(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (item && item->routine) item->routine(menu, item, action);
}

static void focus_step(menu_t *menu, int delta) {
    if (menu->numitems <= 0) return;
    int start = menu->itemOn;
    for (int n = 0; n < menu->numitems; ++n) {
        menu->itemOn = (menu->itemOn + delta + menu->numitems * 2) % menu->numitems;
        if (item_live(&menu->items[menu->itemOn])) return;
    }
    menu->itemOn = start;
}

static void type_text(menu_t *menu, menuitem_t *item, const char *text, bool erase) {
    if (!item_live(item) || item->kind != MI_TEXTFIELD) return;
    size_t length = strlen(item->text);
    int limit = item->maxchars > 0 ? item->maxchars : (int)sizeof(item->text) - 1;
    if (limit > (int)sizeof(item->text) - 1) limit = (int)sizeof(item->text) - 1;
    if (erase) {
        if (length) item->text[length - 1] = '\0';
    } else if (text) {
        for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
            if (*p < 32 || *p >= 127 || (int)length >= limit) continue;
            item->text[length++] = (char)*p;
        }
        item->text[length] = '\0';
    }
    call_routine(menu, item, MA_CHANGE);
}

static int page_rows(const menuitem_t *list) {
    return list->row_height > 0 ? list->rect.h / list->row_height : 0;
}

/* A list scrolls by rows and keeps its view inside them. Prose scrolls by
 * lines; its length is not known here. */
static void scroll_to(menuitem_t *item, int first) {
    int last = item->rows - page_rows(item);
    if (item->kind == MI_LIST && first > last) first = last;
    item->first_row = first < 0 ? 0 : first;
}

void M_MenuSetRows(menuitem_t *list, int rows) {
    list->rows = rows;
    scroll_to(list, list->first_row);
}

static void select_row(menu_t *menu, menuitem_t *list, int row) {
    if (row < 0 || row >= list->rows) return;
    list->value = row;
    if (row < list->first_row) list->first_row = row;
    if (row >= list->first_row + page_rows(list)) list->first_row = row - page_rows(list) + 1;
    call_routine(menu, list, MA_CHANGE);
}

static menuitem_t *linked(const menu_t *menu, const menuitem_t *item) {
    return item->link >= 0 && item->link < menu->numitems ? &menu->items[item->link] : NULL;
}

/* The visible range centres on the pointer, as Dark Colony's scroll bars do
 * (DC.EXE 0x42805f..0x4280d6). */
static void drag_scrollbar(menu_t *menu, const menuitem_t *bar) {
    menuitem_t *list = linked(menu, bar);
    if (!list || bar->rect.h <= 0) return;
    scroll_to(list, (menu->cursor.y - bar->rect.y) * list->rows / bar->rect.h -
                    page_rows(list) / 2);
}

static void activate(menu_t *menu, menuitem_t *item) {
    if (!item_live(item)) return;
    if (item->kind == MI_CHECK && !item->group) item->value = !item->value;
    else if (item->kind == MI_CHECK)
        for (int i = 0; i < menu->numitems; ++i) {
            menuitem_t *other = &menu->items[i];
            if (other->kind == MI_CHECK && other->group == item->group) other->value = other == item;
        }
    menuitem_t *target = item->kind == MI_BUTTON && item->step ? linked(menu, item) : NULL;
    if (target) scroll_to(target, target->first_row + item->step);
    call_routine(menu, item, MA_ACTIVATE);
}

/* The wheel scrolls the screen's list or prose. */
static void wheel(menu_t *menu, int delta) {
    for (int i = 0; i < menu->numitems; ++i) {
        menuitem_t *item = &menu->items[i];
        if (!item->visible || !(item->kind == MI_LIST || (item->kind == MI_STATIC && item->prose)))
            continue;
        scroll_to(item, item->first_row - delta);
        return;
    }
}

static void respond(menu_t *menu, const app_t *app, const SDL_Event *event) {
    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        menuitem_t *focus = focused(menu);
        if (key == SDLK_ESCAPE) {
            if (!event->key.repeat && menu->escape) menu->escape(menu);
        } else if ((key == SDLK_UP || key == SDLK_DOWN) && item_live(focus) &&
                   focus->kind == MI_LIST) {
            select_row(menu, focus, focus->value < 0 ? 0 : focus->value + (key == SDLK_UP ? -1 : 1));
        } else if (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_TAB) {
            focus_step(menu, key == SDLK_UP ? -1 : 1);
        } else if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && !event->key.repeat) {
            activate(menu, focus);
        } else if (key == SDLK_BACKSPACE) {
            type_text(menu, focus, NULL, true);
        }
    } else if (event->type == SDL_TEXTINPUT) {
        type_text(menu, focused(menu), event->text.text, false);
    } else if (event->type == SDL_MOUSEMOTION ||
               (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT)) {
        bool motion = event->type == SDL_MOUSEMOTION;
        R_WindowToRenderPt(app, motion ? event->motion.x : event->button.x,
                           motion ? event->motion.y : event->button.y,
                           &menu->cursor.x, &menu->cursor.y);
        if (motion && menu->held && menu->held->kind == MI_SCROLLBAR) {
            drag_scrollbar(menu, menu->held);
            return;
        }
        menuitem_t *hit = item_at(menu, menu->cursor);
        if (!hit) return;
        menu->itemOn = (int)(hit - menu->items);
        if (motion) return;
        menu->held = hit;
        if (hit->kind == MI_LIST)
            select_row(menu, hit, hit->first_row +
                       (menu->cursor.y - hit->rect.y) / (hit->row_height > 0 ? hit->row_height : 1));
        else if (hit->kind == MI_SCROLLBAR) drag_scrollbar(menu, hit);
        else activate(menu, hit);
    } else if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
        menu->held = NULL;
    } else if (event->type == SDL_MOUSEWHEEL) {
        wheel(menu, event->wheel.y);
    }
}

bool M_MenuResponder(menu_t *menu, const app_t *app, const SDL_Event *event) {
    respond(menu, app, event);
    /* The keyboard types into the focused field and nowhere else. */
    const menuitem_t *focus = focused(menu);
    bool typing = item_live(focus) && focus->kind == MI_TEXTFIELD;
    if (typing != (SDL_IsTextInputActive() == SDL_TRUE)) {
        if (typing) SDL_StartTextInput();
        else SDL_StopTextInput();
    }
    return true;
}

void M_MenuAnimate(menuitem_t *item, menuanimmode_t mode) {
    item->anim.mode = mode;
    item->anim.frame = item->anim.first;
    item->anim.delay = 0;
}

void M_MenuTicker(menu_t *menu) {
    for (int i = 0; i < menu->numitems; ++i) {
        menuitem_t *item = &menu->items[i];
        menuanim_t *anim = &item->anim;
        if (!item->visible || anim->mode == MANIM_STOPPED) continue;
        if (!anim->delay) {
            if (++anim->frame > anim->last) {
                if (anim->mode == MANIM_ONCE) {
                    anim->frame = anim->last;
                    anim->mode = MANIM_STOPPED;
                    continue;
                }
                anim->frame = anim->first;
            }
            anim->delay = menu->frametics ? menu->frametics(item) : 1;
        }
        if (anim->delay) --anim->delay;
    }
}

static menustate_t item_state(const menu_t *menu, const menuitem_t *item) {
    if (menu->held == item || (item->kind == MI_CHECK && item->value)) return MS_PUSHED;
    return item == focused(menu) ? MS_FOCUS : MS_NORMAL;
}

static void draw_picture(const menuitem_t *item, menustate_t state) {
    int cell = item->cell[state];
    if (!item->sheet || cell < 0 || cell >= item->sheet->numlumps) return;
    const spritecell_t *source = &item->sheet->cells[cell];
    irect_t dst = {item->rect.x + source->displacement.x, item->rect.y + source->displacement.y,
                   source->rect.w, source->rect.h};
    R_DrawSprite(item->sheet, cell, item->palette[state], &source->rect, &dst, 0, 16);
}

static void draw_text(const menuitem_t *item, menustate_t state, bool caret) {
    const bitmapfont_t *font = item->font;
    if (!font) return;
    if (item->prose) {
        V_DrawTextWrapped(item->rect, font, item->prose, NULL, item->first_row * font->line_h);
        return;
    }
    const uint8_t *remap = R_PaletteMap(&font->sprite, item->palette[state]);
    ivec2_t at = ivec2_add((ivec2_t){item->rect.x, item->rect.y}, item->inset);
    if (item->centered)
        at = ivec2_add(at, (ivec2_t){(item->rect.w - V_TextWidth(font, item->text)) / 2,
                                     (item->rect.h - font->glyph_size.h) / 2});
    V_DrawText(at, font, item->text, remap);
    /* Engine behaviour: the field being edited ends in an underscore. */
    if (caret && font->glyph_index['_'] >= 0)
        V_DrawText((ivec2_t){at.x + V_TextWidth(font, item->text), at.y}, font, "_", remap);
}

static void draw_list(const menuitem_t *item) {
    const bitmapfont_t *font = item->font;
    if (!font || item->row_height <= 0) return;
    if (!item->rows) {
        if (item->prose) V_DrawTextWrapped(item->rect, font, item->prose, NULL, 0);
        return;
    }
    irect_t clip = V_GetClip();
    V_SetClip(item->rect);
    int visible = item->rect.h / item->row_height;
    for (int i = 0; i < visible && item->first_row + i < item->rows; ++i) {
        int row = item->first_row + i;
        bool selected = row == item->value;
        irect_t line = {item->rect.x, item->rect.y + i * item->row_height,
                        item->rect.w, item->row_height};
        if (selected) V_FillRect(line, V_NearestIndex(item->color));
        V_DrawText((ivec2_t){line.x, line.y}, font, item->row(item, row),
                   R_PaletteMap(&font->sprite, item->palette[selected ? MS_PUSHED : MS_NORMAL]));
    }
    V_SetClip(clip);
}

/* The thumb spans the visible rows of the linked list. */
static void draw_scrollbar(const menu_t *menu, const menuitem_t *item) {
    if (item->link < 0 || item->link >= menu->numitems) return;
    const menuitem_t *list = &menu->items[item->link];
    if (list->rows <= 0 || list->row_height <= 0) return;
    int end = list->first_row + list->rect.h / list->row_height;
    if (end > list->rows) end = list->rows;
    irect_t bar = item->rect;
    int top = bar.h * list->first_row / list->rows;
    uint8_t color = V_NearestIndex(item->color);
    V_DrawRectOutline(bar, color);
    V_FillRect((irect_t){bar.x, bar.y + top, bar.w, bar.h * end / list->rows - top}, color);
}

static bool is_button(const menuitem_t *item) {
    return item->kind == MI_BUTTON || item->kind == MI_CHECK;
}

static void draw_chrome(const menu_t *menu, const menuitem_t *item) {
    if (!item->visible || item->kind == MI_LIST || item->kind == MI_SCROLLBAR) return;
    if (item->fill) V_FillRect(item->rect, V_NearestIndex(item->fill));
    if (item->ownerdraw) {
        item->ownerdraw(menu, item);
        return;
    }
    menustate_t state = item_state(menu, item);
    draw_picture(item, state);
    if (is_button(item)) draw_text(item, state, false);
}

static void draw_content(const menu_t *menu, const menuitem_t *item) {
    if (!item->visible || item->ownerdraw || is_button(item)) return;
    if (item->kind == MI_LIST || item->kind == MI_SCROLLBAR) {
        if (item->fill) V_FillRect(item->rect, V_NearestIndex(item->fill));
        if (item->kind == MI_LIST) draw_list(item);
        else draw_scrollbar(menu, item);
        return;
    }
    draw_text(item, item_state(menu, item), item->kind == MI_TEXTFIELD && item->enabled &&
                                             item == focused(menu));
}

/* Pictures and buttons draw first, in table order; loose text, lists and
 * scroll bars draw over them. */
void M_MenuDrawer(const menu_t *menu) {
    if (menu->palette) I_SetPalette(menu->palette);
    if (menu->background && menu->background->numlumps) {
        irect_t dst = menu->background->cells[0].rect;
        R_DrawSprite(menu->background, 0, -1, NULL, &dst, V_OPAQUE, 16);
    }
    for (int i = 0; i < menu->numitems; ++i) draw_chrome(menu, &menu->items[i]);
    for (int i = 0; i < menu->numitems; ++i) draw_content(menu, &menu->items[i]);
}
