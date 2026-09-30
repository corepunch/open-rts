#include "m_menu.h"
#include "app.h"
#include "engine.h"

#include <stdio.h>
#include <string.h>

static menu_t *current;

static bool item_live(const menuitem_t *item) {
    return item && item->visible && item->enabled;
}

menuitem_t *M_MenuFind(const menu_t *menu, int userid) {
    if (!menu) return NULL;
    for (int i = 0; i < menu->numitems; ++i)
        if (menu->items[i].userid == userid) return &menu->items[i];
    return NULL;
}

void M_MenuFocusId(menu_t *menu, int userid) {
    if (!menu) return;
    for (int i = 0; i < menu->numitems; ++i) {
        if (menu->items[i].userid != userid) continue;
        menu->itemOn = i;
        return;
    }
}

void M_MenuSetText(menuitem_t *item, const char *text) {
    if (!item) return;
    snprintf(item->text, sizeof(item->text), "%s", text ? text : "");
}

void M_MenuOpen(menu_t *menu) {
    current = menu;
    if (menu && menu->grab < -1) menu->grab = -1;
}

void M_MenuClose(void) {
    current = NULL;
}

menuitem_t *M_MenuItemAt(const menu_t *menu, ivec2_t logical) {
    if (!menu) return NULL;
    for (int i = 0; i < menu->numitems; ++i) {
        menuitem_t *item = &menu->items[i];
        if (!item_live(item) || !irect_contains(item->rect, logical)) continue;
        return item;
    }
    return NULL;
}

static ivec2_t event_point(const menu_t *menu, int x, int y) {
    ivec2_t point = {x, y};
    if (menu && menu->owner) {
        const app_t *app = menu->owner;
        R_WindowToRenderPt(app, x, y, &point.x, &point.y);
        if (menu->space.w > 0 && menu->space.h > 0 && app->win.w > 0 && app->win.h > 0)
            point = (ivec2_t){point.x * menu->space.w / app->win.w,
                              point.y * menu->space.h / app->win.h};
    }
    return point;
}

static void call_routine(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (item && item->routine) item->routine(menu, item, action);
}

static bool focus_step(menu_t *menu, int delta) {
    if (!menu || menu->numitems <= 0) return false;
    int start = menu->itemOn;
    for (int n = 0; n < menu->numitems; ++n) {
        menu->itemOn = (menu->itemOn + delta + menu->numitems * 2) % menu->numitems;
        if (item_live(&menu->items[menu->itemOn])) return true;
    }
    menu->itemOn = start;
    return false;
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

bool M_MenuResponder(const SDL_Event *event) {
    menu_t *menu = current;
    if (!menu || !event) return false;
    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        menuitem_t *focus = menu->itemOn >= 0 && menu->itemOn < menu->numitems ?
            &menu->items[menu->itemOn] : NULL;
        if (key == SDLK_ESCAPE) {
            if (!event->key.repeat && menu->escape) menu->escape(menu);
            return true;
        }
        if ((key == SDLK_UP || key == SDLK_DOWN) && focus && focus->kind == MI_LIST &&
            item_live(focus)) {
            focus->step = key == SDLK_UP ? -1 : 1;
            call_routine(menu, focus, MA_ROW);
            focus->step = 0;
            return true;
        }
        if (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_TAB)
            return focus_step(menu, key == SDLK_UP ? -1 : 1) || true;
        if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && !event->key.repeat) {
            call_routine(menu, focus, MA_ACTIVATE);
            return true;
        }
        if (key == SDLK_BACKSPACE) {
            type_text(menu, focus, NULL, true);
            return true;
        }
        return true;
    }
    if (event->type == SDL_TEXTINPUT) {
        menuitem_t *focus = menu->itemOn >= 0 && menu->itemOn < menu->numitems ?
            &menu->items[menu->itemOn] : NULL;
        type_text(menu, focus, event->text.text, false);
        return true;
    }
    if (event->type == SDL_MOUSEMOTION ||
        (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT)) {
        int x = event->type == SDL_MOUSEMOTION ? event->motion.x : event->button.x;
        int y = event->type == SDL_MOUSEMOTION ? event->motion.y : event->button.y;
        menu->cursor = event_point(menu, x, y);
        if (menu->grab >= 0 && menu->grab < menu->numitems && event->type == SDL_MOUSEMOTION) {
            menuitem_t *held = &menu->items[menu->grab];
            if (held->kind == MI_SCROLLBAR || held->kind == MI_SLIDER) {
                held->value = menu->cursor.y - held->rect.y;
                call_routine(menu, held, MA_CHANGE);
                return true;
            }
        }
        menuitem_t *hit = M_MenuItemAt(menu, menu->cursor);
        if (!hit) return true;
        menu->itemOn = (int)(hit - menu->items);
        if (event->type == SDL_MOUSEBUTTONDOWN) {
            menu->grab = menu->itemOn;
            hit->pressed = true;
            call_routine(menu, hit, MA_PRESS);
            if (hit->kind == MI_LIST) {
                int row_h = hit->row_height > 0 ? hit->row_height : 1;
                hit->step = 0;
                hit->value = hit->first_row + (menu->cursor.y - hit->rect.y) / row_h;
                call_routine(menu, hit, MA_ROW);
            } else if (hit->kind == MI_SCROLLBAR || hit->kind == MI_SLIDER) {
                hit->value = menu->cursor.y - hit->rect.y;
                call_routine(menu, hit, MA_CHANGE);
            } else {
                call_routine(menu, hit, MA_ACTIVATE);
            }
        }
        return true;
    }
    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
        if (menu->grab >= 0 && menu->grab < menu->numitems) {
            menu->items[menu->grab].pressed = false;
            call_routine(menu, &menu->items[menu->grab], MA_RELEASE);
        }
        menu->grab = -1;
        return true;
    }
    if (event->type == SDL_MOUSEWHEEL) {
        if (menu->wheel) menu->wheel(menu, event->wheel.y);
        return true;
    }
    return true;
}

void M_MenuTicker(void) {
    if (current && current->ticker) current->ticker(current);
}

static void draw_item(const menu_t *menu, const menuitem_t *item) {
    if (!item->visible) return;
    if (item->ownerdraw) {
        item->ownerdraw(menu, item);
        return;
    }
    bool down = item->pressed || (item->kind == MI_CHECK && item->value);
    int cell = down && item->cell_pushed >= 0 ? item->cell_pushed : item->cell_normal;
    if (item->kind == MI_CHECK && item->value && item->cell_checked >= 0)
        cell = item->cell_checked;
    if (item->sheet && cell >= 0)
        V_DrawSpriteCell((ivec2_t){item->rect.x, item->rect.y}, item->sheet, cell, item->remap, 0);
    if (item->font && item->text[0] &&
        (item->kind == MI_LABEL || item->kind == MI_BUTTON || item->kind == MI_TEXTFIELD ||
         item->kind == MI_CHECK)) {
        ivec2_t at = {item->rect.x, item->rect.y};
        if (item->centered)
            at.x += (item->rect.w - V_TextWidth(item->font, item->text)) / 2;
        V_DrawText(at, item->font, item->text, item->remap);
    }
}

void M_MenuDrawer(void) {
    if (!current) return;
    if (current->background && current->background->numlumps) {
        irect_t dst = {0, 0, current->space.w > 0 ? current->space.w : screens[0].w,
                       current->space.h > 0 ? current->space.h : screens[0].h};
        V_DrawSpriteCellScaled(dst, current->background, 0, NULL, NULL, V_OPAQUE);
    }
    if (current->drawer) {
        current->drawer(current);
        return;
    }
    for (int i = 0; i < current->numitems; ++i) draw_item(current, &current->items[i]);
}
