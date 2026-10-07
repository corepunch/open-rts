#include "engine.h"

#include <ctype.h>
#include <string.h>

static char message[256];

void M_StartMessage(const char *text) {
    snprintf(message, sizeof(message), "%s", text);
    SDL_StopTextInput();
}

void M_StopMessage(void) {
    message[0] = '\0';
}

/* ── geometry ───────────────────────────────────────────────────────────── */

static isize2_t screen_size(const menu_t *menu) {
    isize2_t size = menu->size;
    if (menu->app && menu->app->win.w > 0) size = menu->app->win;
    else if (screens[0].w > 0) size = (isize2_t){screens[0].w, screens[0].h};
    int scale = R_UIScale(menu->app);
    return (isize2_t){size.w / scale, size.h / scale};
}

irect_t M_MenuItemRect(const menu_t *menu, const menuitem_t *item) {
    irect_t r = item->rect;
    if (menu->size.w > 0 && menu->size.h > 0) {
        isize2_t screen = screen_size(menu);
        if (menu->stretch)
            r = (irect_t){r.x * screen.w / menu->size.w, r.y * screen.h / menu->size.h,
                           r.w * screen.w / menu->size.w, r.h * screen.h / menu->size.h};
        else {
            int dx = screen.w - menu->size.w, dy = screen.h - menu->size.h;
            if (item->anchor & MANCHOR_RIGHT) r.x += dx;
            if (item->anchor & MANCHOR_BOTTOM) r.y += dy;
            if (item->anchor & MANCHOR_GROW) r.h += dy;
            if (item->anchor & MANCHOR_WIDE) r.w += dx;
        }
    }
    int scale = R_UIScale(menu->app), drawing = V_GetDrawScale();
    return (irect_t){r.x * scale / drawing, r.y * scale / drawing,
                     r.w * scale / drawing, r.h * scale / drawing};
}

static bool item_live(const menuitem_t *item) {
    return item && item->kind != MI_STATIC && item->visible && item->enabled;
}

static menuitem_t *focused(const menu_t *menu) {
    return menu->itemOn >= 0 && menu->itemOn < menu->numitems ? &menu->items[menu->itemOn] : NULL;
}

menuitem_t *M_MenuHover(const menu_t *menu) {
    menuitem_t *item = focused(menu);
    return item_live(item) ? item : NULL;
}

menuitem_t *M_MenuFind(const menu_t *menu, int id) {
    for (int i = 0; i < menu->numitems; ++i)
        if (menu->items[i].id == id) return &menu->items[i];
    return NULL;
}

static menuitem_t *item_at(const menu_t *menu, ivec2_t point) {
    for (int i = 0; i < menu->numitems; ++i) {
        menuitem_t *item = &menu->items[i];
        if (item_live(item) && irect_contains(M_MenuItemRect(menu, item), point)) return item;
    }
    return NULL;
}

static bool visible_at(const menu_t *menu, ivec2_t point) {
    for (int i = 0; i < menu->numitems; ++i)
        if (menu->items[i].visible && irect_contains(M_MenuItemRect(menu, &menu->items[i]), point))
            return true;
    return false;
}

static void call_routine(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (item && item->routine) item->routine(menu, item, action);
}

/* ── targets, edits and the view ────────────────────────────────────────── */

void M_MenuTarget(menu_t *menu, menuitem_t *item) {
    menu->target = item;
}

void M_MenuEdit(menu_t *menu, menuitem_t *item) {
    if (item) {
        menu->editing = item;
        menu->itemOn = (int)(item - menu->items);
        SDL_StartTextInput();
    } else if (menu->editing) {
        menu->editing = NULL;
        SDL_StopTextInput();
    }
}

static void cancel(menu_t *menu) {
    menuitem_t *item = menu->target ? menu->target : menu->editing;
    menu->target = NULL;
    M_MenuEdit(menu, NULL);
    call_routine(menu, item, MA_CANCEL);
}

void M_CentreView(app_t *app, fvec2_t cell) {
    float sx = 0.0f, sy = 0.0f;
    R_GridToScreen(app, cell.x, cell.y, &sx, &sy);
    irect_t view = G_WorldViewport(app);
    app->cam = fvec2_add(app->cam, (fvec2_t){view.x + view.w / 2.0f - sx,
                                             view.y + view.h / 2.0f - sy});
}

/* The pointer's pixel, sampled at its centre, as a level cell. */
static void minimap_centre(menu_t *menu, const menuitem_t *item) {
    irect_t r = M_MenuItemRect(menu, item);
    if (!menu->app || r.w <= 0 || r.h <= 0 || level.width <= 0 || level.height <= 0) return;
    M_CentreView(menu->app, (fvec2_t){
        (float)level.width * (2 * (menu->cursor.x - r.x) + 1) / (2.0f * r.w),
        (float)level.height * (2 * (menu->cursor.y - r.y) + 1) / (2.0f * r.h)});
}

/* ── editing and lists ──────────────────────────────────────────────────── */

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

static irect_t dropdown_rect(const menu_t *menu) {
    const menuitem_t *item = menu->dropdown;
    irect_t r = M_MenuItemRect(menu, item);
    int rows = item->popup_rows > 0 && item->popup_rows < item->rows ? item->popup_rows : item->rows;
    int height = item->row_height * R_UIScale(menu->app) / V_GetDrawScale();
    isize2_t size = screen_size(menu);
    int bottom = size.h * R_UIScale(menu->app) / V_GetDrawScale();
    int room = bottom - r.y - r.h;
    bool above = room < height && r.y > room;
    if (above) room = r.y;
    if (height <= 0) return (irect_t){0};
    if (rows > room / height) rows = room / height;
    if (rows < 1) rows = 1;
    r.y = above ? r.y - rows * height : r.y + r.h;
    r.h = rows * height;
    return r;
}

static void dropdown_view(menu_t *menu) {
    menuitem_t *item = menu->dropdown;
    int height = item->row_height * R_UIScale(menu->app) / V_GetDrawScale();
    if (height <= 0) return;
    int rows = dropdown_rect(menu).h / height;
    if (menu->dropdown_row < item->first_row) item->first_row = menu->dropdown_row;
    if (menu->dropdown_row >= item->first_row + rows) item->first_row = menu->dropdown_row - rows + 1;
}

static void dropdown_accept(menu_t *menu) {
    menuitem_t *item = menu->dropdown;
    int row = menu->dropdown_row;
    menu->dropdown = NULL;
    menu->held = NULL;
    if (item && row >= 0 && row < item->rows && row != item->value) {
        item->value = row;
        call_routine(menu, item, MA_CHANGE);
    }
}

/* The visible range centres on the pointer, as Dark Colony's scroll bars do
 * (DC.EXE 0x42805f..0x4280d6). */
static void drag_scrollbar(menu_t *menu, const menuitem_t *bar) {
    menuitem_t *list = linked(menu, bar);
    irect_t r = M_MenuItemRect(menu, bar);
    if (!list || r.h <= 0) return;
    if (bar->thumb.part.h > 0) {
        int travel = r.h - bar->thumb.part.h * R_UIScale(menu->app);
        int last = list->rows - page_rows(list);
        if (travel > 0 && last > 0)
            scroll_to(list, (menu->cursor.y - r.y - bar->thumb.part.h * R_UIScale(menu->app) / 2) * last / travel);
    } else scroll_to(list, (menu->cursor.y - r.y) * list->rows / r.h - page_rows(list) / 2);
}

static void slider_value(menu_t *menu, menuitem_t *item, int64_t value) {
    if (item->range.max <= item->range.min) return;
    if (value < item->range.min) value = item->range.min;
    if (value > item->range.max) value = item->range.max;
    if (item->value == value) return;
    item->value = (int)value;
    call_routine(menu, item, MA_CHANGE);
}

static void drag_slider(menu_t *menu, menuitem_t *item) {
    irect_t rect = M_MenuItemRect(menu, item);
    int width = item->thumb.part.w * R_UIScale(menu->app);
    int travel = rect.w - width;
    if (travel <= 0) return;
    int64_t span = (int64_t)item->range.max - item->range.min;
    slider_value(menu, item, item->range.min +
                 (menu->cursor.x - rect.x - width / 2) * span / travel);
}

static void activate(menu_t *menu, menuitem_t *item);

static void press_key(menu_t *menu, menuitem_t *item, SDL_Keycode key) {
    if (item->release && item->kind == MI_BUTTON) {
        menu->keyheld = item;
        menu->keycode = key;
    } else activate(menu, item);
}

static void activate(menu_t *menu, menuitem_t *item) {
    if (item->kind == MI_DROPDOWN) {
        if (item->rows <= 0 || item->row_height <= 0) return;
        menu->dropdown = item;
        menu->dropdown_row = item->value >= 0 ? item->value : 0;
        dropdown_view(menu);
        return;
    }
    if (item->kind == MI_CHECK && !item->group) item->value = !item->value;
    else if (item->kind == MI_CHECK)
        for (int i = 0; i < menu->numitems; ++i) {
            menuitem_t *other = &menu->items[i];
            if (other->kind == MI_CHECK && other->group == item->group) other->value = other == item;
        }
    menuitem_t *target = item->kind == MI_BUTTON && item->step ? linked(menu, item) : NULL;
    if (target) scroll_to(target, target->first_row + item->step);
    if (!item->quiet) S_StartUISound(UI_SOUND_CLICK);
    call_routine(menu, item, MA_ACTIVATE);
}

/* ── input ──────────────────────────────────────────────────────────────── */

/* A hotkey works on an enabled item even while it is not shown. */
static bool hotkey(menu_t *menu, const SDL_Event *event) {
    if (event->key.repeat || (event->key.keysym.mod & (KMOD_CTRL | KMOD_ALT))) return false;
    for (int i = 0; i < menu->numitems; ++i) {
        menuitem_t *item = &menu->items[i];
        if (!item->enabled || !item->hotkey || item->hotkey != event->key.keysym.sym) continue;
        press_key(menu, item, event->key.keysym.sym);
        return true;
    }
    return false;
}

/* The key of a release button shows it pressed; its release activates it. */
static bool key_up(menu_t *menu, const SDL_Event *event) {
    menuitem_t *item = menu->keyheld;
    if (!item || event->key.keysym.sym != menu->keycode) return false;
    menu->keyheld = NULL;
    if (item->enabled) activate(menu, item);
    return true;
}

static bool key_down(menu_t *menu, const SDL_Event *event) {
    SDL_Keycode key = event->key.keysym.sym;
    menuitem_t *focus = focused(menu);
    bool typing = menu->modal && item_live(focus) && focus->kind == MI_TEXTFIELD;
    menu->keymod = event->key.keysym.mod;
    if (menu->dropdown) {
        if (key == SDLK_ESCAPE || key == SDLK_TAB) {
            menu->dropdown = NULL;
            menu->held = NULL;
            if (key == SDLK_TAB) focus_step(menu, event->key.keysym.mod & KMOD_SHIFT ? -1 : 1);
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
            if (!event->key.repeat) dropdown_accept(menu);
        } else {
            int row = menu->dropdown_row;
            if (key == SDLK_UP) --row;
            if (key == SDLK_DOWN) ++row;
            if (key == SDLK_HOME) row = 0;
            if (key == SDLK_END) row = menu->dropdown->rows - 1;
            if (row >= 0 && row < menu->dropdown->rows) menu->dropdown_row = row;
            dropdown_view(menu);
        }
        return true;
    }
    if (!typing && hotkey(menu, event)) return true;
    if (!menu->modal) {
        if (key != SDLK_ESCAPE || event->key.repeat || !menu->target) return false;
        cancel(menu);
        return true;
    }
    if (key == SDLK_ESCAPE) {
        if (!event->key.repeat && menu->escape) menu->escape(menu);
    } else if (item_live(focus) && focus->kind == MI_SLIDER &&
               (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_HOME || key == SDLK_END)) {
        int64_t value = focus->value;
        if (key == SDLK_LEFT) --value;
        if (key == SDLK_RIGHT) ++value;
        if (key == SDLK_HOME) value = focus->range.min;
        if (key == SDLK_END) value = focus->range.max;
        slider_value(menu, focus, value);
    } else if (item_live(focus) && focus->kind == MI_LIST &&
               (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_HOME || key == SDLK_END ||
                key == SDLK_PAGEUP || key == SDLK_PAGEDOWN)) {
        int row = focus->value < 0 ? 0 : focus->value;
        if (key == SDLK_UP) --row;
        if (key == SDLK_DOWN) ++row;
        if (key == SDLK_HOME) row = 0;
        if (key == SDLK_END) row = focus->rows - 1;
        if (key == SDLK_PAGEUP) row -= page_rows(focus);
        if (key == SDLK_PAGEDOWN) row += page_rows(focus);
        if (row < 0) row = 0;
        if (row >= focus->rows) row = focus->rows - 1;
        select_row(menu, focus, row);
    } else if (key == SDLK_UP || key == SDLK_DOWN || key == SDLK_TAB ||
               ((key == SDLK_LEFT || key == SDLK_RIGHT) && !typing)) {
        bool back = key == SDLK_UP || key == SDLK_LEFT ||
                    (key == SDLK_TAB && (event->key.keysym.mod & KMOD_SHIFT));
        if (back && menu->itemOn < 0) menu->itemOn = 0;
        focus_step(menu, back ? -1 : 1);
    } else if ((key == SDLK_RETURN || key == SDLK_KP_ENTER || (key == SDLK_SPACE && !typing)) && !event->key.repeat) {
        if (item_live(focus)) press_key(menu, focus, key);
    } else if (key == SDLK_BACKSPACE) {
        type_text(menu, focus, NULL, true);
    }
    return true;
}

/* The field being edited has the keyboard: Enter sends it, Escape drops it. */
static void edit_key(menu_t *menu, const SDL_Event *event) {
    SDL_Keycode key = event->key.keysym.sym;
    menu->keymod = event->key.keysym.mod;
    if (key == SDLK_ESCAPE) {
        if (!event->key.repeat) cancel(menu);
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (!event->key.repeat && item_live(menu->editing)) activate(menu, menu->editing);
    } else if (key == SDLK_BACKSPACE) {
        type_text(menu, menu->editing, NULL, true);
    }
}

static bool mouse(menu_t *menu, const app_t *app, const SDL_Event *event) {
    bool motion = event->type == SDL_MOUSEMOTION, down = event->type == SDL_MOUSEBUTTONDOWN;
    R_WindowToRenderPt(app, motion ? event->motion.x : event->button.x,
                       motion ? event->motion.y : event->button.y,
                       &menu->cursor.x, &menu->cursor.y);
    if (menu->dropdown) {
        menuitem_t *item = menu->dropdown;
        irect_t popup = dropdown_rect(menu);
        bool inside = irect_contains(popup, menu->cursor);
        if (inside) {
            int row = item->first_row + (menu->cursor.y - popup.y) /
                      (item->row_height * R_UIScale(app));
            if (row < item->rows) menu->dropdown_row = row;
        }
        if (down) {
            if (event->button.button == SDL_BUTTON_LEFT && inside) dropdown_accept(menu);
            else { menu->dropdown = NULL; menu->held = NULL; }
        }
        return true;
    }
    if (motion && menu->held && menu->held->kind == MI_SCROLLBAR) {
        drag_scrollbar(menu, menu->held);
        return true;
    }
    if (motion && menu->held && menu->held->kind == MI_SLIDER) {
        drag_slider(menu, menu->held);
        return true;
    }
    if (motion && menu->held && menu->held->kind == MI_MINIMAP) {
        minimap_centre(menu, menu->held);
        return true;
    }
    /* A modal screen keeps its focus while the pointer is off every item. */
    menuitem_t *hit = item_at(menu, menu->cursor);
    if (hit || !menu->modal) menu->itemOn = hit ? (int)(hit - menu->items) : -1;
    menu->over = hit && hit == menu->held;
    if (motion) return menu->modal;
    bool left = event->button.button == SDL_BUTTON_LEFT;
    if (!menu->modal && menu->target && down && event->button.button == SDL_BUTTON_RIGHT) {
        menu->held = NULL;
        cancel(menu);
        return true;
    }
    /* The world under a waiting item takes its click, and the release. */
    if (!menu->modal && left && !visible_at(menu, menu->cursor)) {
        if (!down && menu->lifted) {
            menu->lifted = false;
            return true;
        }
        if (down && menu->target) {
            menuitem_t *item = menu->target;
            menu->target = NULL;
            menu->lifted = true;
            call_routine(menu, item, MA_TARGET);
            return true;
        }
    }
    bool taken = menu->modal || visible_at(menu, menu->cursor) || (left && !down && menu->held);
    menuitem_t *pressed = menu->held;
    if (left) menu->held = NULL;
    if (left && !down && pressed && pressed->release && pressed->kind == MI_BUTTON) {
        if (pressed == hit) activate(menu, pressed);
        return true;
    }
    if (!down || !hit) return taken;
    if (!left) {
        if (event->button.button == SDL_BUTTON_RIGHT) call_routine(menu, hit, MA_SECONDARY);
        return true;
    }
    menu->held = hit;
    menu->over = true;
    if (hit->release && hit->kind == MI_BUTTON) return true;
    if (hit->kind == MI_LIST) {
        int row = hit->first_row + (menu->cursor.y - M_MenuItemRect(menu, hit).y) /
                  ((hit->row_height > 0 ? hit->row_height : 1) * R_UIScale(app));
        if (row >= 0 && row < hit->rows) {
            select_row(menu, hit, row);
            if (event->button.clicks > 1) activate(menu, hit);
        }
    }
    else if (hit->kind == MI_SCROLLBAR) drag_scrollbar(menu, hit);
    else if (hit->kind == MI_SLIDER) drag_slider(menu, hit);
    else if (hit->kind == MI_MINIMAP) {
        S_StartUISound(UI_SOUND_CLICK);
        minimap_centre(menu, hit);
        call_routine(menu, hit, MA_ACTIVATE);
    } else activate(menu, hit);
    return true;
}

/* The wheel scrolls a list or prose: the one on a modal screen, the one under
 * the pointer on a HUD. Over any other live HUD item it goes to the routine. */
static bool wheel(menu_t *menu, int delta) {
    if (menu->dropdown) {
        int row = menu->dropdown_row - delta;
        if (row < 0) row = 0;
        if (row >= menu->dropdown->rows) row = menu->dropdown->rows - 1;
        menu->dropdown_row = row;
        dropdown_view(menu);
        return true;
    }
    for (int i = 0; i < menu->numitems; ++i) {
        menuitem_t *item = &menu->items[i];
        if (!item->visible ||
            (!menu->modal && !irect_contains(M_MenuItemRect(menu, item), menu->cursor))) continue;
        if (item->kind == MI_LIST || (item->kind == MI_STATIC && item->prose)) {
            scroll_to(item, item->first_row - delta);
            return true;
        }
        if (!menu->modal && item->enabled) {
            menu->wheel = delta;
            call_routine(menu, item, MA_WHEEL);
            return true;
        }
    }
    return menu->modal || visible_at(menu, menu->cursor);
}

bool M_MenuResponder(menu_t *menu, app_t *app, const SDL_Event *event) {
    menu->app = app;
    if (menu->refresh) menu->refresh(menu);
    if (menu->modal && message[0]) {
        menu->held = NULL;
        if ((event->type == SDL_KEYDOWN && !event->key.repeat &&
             (event->key.keysym.sym == SDLK_RETURN || event->key.keysym.sym == SDLK_KP_ENTER ||
              event->key.keysym.sym == SDLK_ESCAPE || event->key.keysym.sym == SDLK_SPACE)) ||
            (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT))
            M_StopMessage();
        return true;
    }
    if (menu->editing) {
        if (event->type == SDL_KEYDOWN) edit_key(menu, event);
        else if (event->type == SDL_TEXTINPUT) type_text(menu, menu->editing, event->text.text, false);
        else if (event->type == SDL_MOUSEMOTION || event->type == SDL_MOUSEBUTTONDOWN ||
                 event->type == SDL_MOUSEBUTTONUP) mouse(menu, app, event);
        else if (event->type == SDL_MOUSEWHEEL) wheel(menu, event->wheel.y);
        return true;
    }
    bool taken = menu->modal;
    if (event->type == SDL_KEYDOWN) taken = key_down(menu, event);
    else if (event->type == SDL_KEYUP) taken = key_up(menu, event) || menu->modal;
    else if (event->type == SDL_TEXTINPUT && menu->modal)
        type_text(menu, focused(menu), event->text.text, false);
    else if (event->type == SDL_MOUSEMOTION || event->type == SDL_MOUSEBUTTONDOWN ||
             event->type == SDL_MOUSEBUTTONUP) taken = mouse(menu, app, event);
    else if (event->type == SDL_MOUSEWHEEL) taken = wheel(menu, event->wheel.y);
    if (!menu->modal) return taken;
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

/* ── drawing ────────────────────────────────────────────────────────────── */

static menustate_t item_state(const menu_t *menu, const menuitem_t *item) {
    if (!item->enabled && item->kind != MI_STATIC && item->disabled_look) return MS_DISABLED;
    if (item->kind == MI_CHECK && item->value) return MS_PUSHED;
    if (!item_live(item)) return MS_NORMAL;
    if (menu->keyheld == item) return MS_PUSHED;
    if (menu->held == item &&
        (!item->release || menu->over)) return MS_PUSHED;
    return item == focused(menu) || menu->target == item ? MS_FOCUS : MS_NORMAL;
}

static void draw_picture(const menuitem_t *item, menustate_t state, irect_t rect) {
    const menulook_t *look = &item->look[state];
    if (!item->sheet || look->cell < 0 || look->cell >= item->sheet->numlumps) return;
    const spritecell_t *cell = &item->sheet->cells[look->cell];
    irect_t src = look->part.w > 0 ? look->part : cell->rect;
    irect_t dst = {rect.x + cell->displacement.x, rect.y + cell->displacement.y, src.w, src.h};
    if (item->stretch) dst = rect;
    R_DrawSprite(item->sheet, look->cell, look->palette, &src, &dst,
                 item->opaque ? V_OPAQUE : 0, item->light ? item->light : 16);
}

static const bitmapfont_t *state_font(const menuitem_t *item, menustate_t state) {
    return item->look[state].font ? item->look[state].font : item->font;
}

/* Text takes its colour from the ink, or from the state's palette map; a
 * font with its own palette is matched into the screen's. */
static const uint8_t *text_remap(const menuitem_t *item, const bitmapfont_t *font,
                                 menustate_t state, uint32_t ink, uint8_t tint[256]) {
    const uint32_t *colours = font->sprite.source_palette;
    if (ink) {
        V_ModulateRemap(tint, colours, ink);
        return tint;
    }
    const uint8_t *row = R_PaletteMap(&font->sprite, item->look[state].palette);
    if (!font->own_palette) return row;
    if (!row) return V_RemapPalette(colours);
    for (int i = 0; i < 256; ++i) tint[i] = V_NearestIndex(colours[row[i]] | 0xff000000u);
    return tint;
}

static ivec2_t text_origin(const menuitem_t *item, irect_t rect, isize2_t text) {
    ivec2_t at = ivec2_add((ivec2_t){rect.x, rect.y}, item->inset);
    if (item->align & MALIGN_HCENTER) at.x += (rect.w - text.w) / 2;
    else if (item->align & MALIGN_RIGHT) at.x += rect.w - text.w;
    if (item->align & MALIGN_VCENTER) at.y += (rect.h - text.h) / 2;
    else if (item->align & MALIGN_BOTTOM) at.y += rect.h - text.h;
    return at;
}

static void draw_text(const menuitem_t *item, menustate_t state, irect_t rect, bool caret) {
    const bitmapfont_t *font = state_font(item, state);
    if (!font) {
        if (!item->text[0]) return;
        ivec2_t at = text_origin(item, rect, (isize2_t){(int)strlen(item->text) * 6, 7});
        V_DrawSmallText((irect_t){at.x, at.y, rect.w - item->inset.x, 7}, item->text,
                        item->ink ? item->ink : 0xffdce6dcu,
                        V_DrawSize());
        return;
    }
    if (item->prose) {
        V_DrawTextWrapped(rect, font, item->prose,
                          font->own_palette ? V_RemapPalette(font->sprite.source_palette) : NULL,
                          item->first_row * font->line_h);
        return;
    }
    uint8_t tint[256];
    const menulook_t *look = &item->look[state];
    uint32_t ink = look->ink ? look->ink : item->ink;
    const uint8_t *remap = text_remap(item, font, state, ink, tint);
    irect_t bounds = V_TextBounds(font, item->text);
    ivec2_t at = text_origin(item, rect, (isize2_t){bounds.w, bounds.h});
    if (item->align & (MALIGN_VCENTER | MALIGN_BOTTOM)) at.y -= bounds.y;
    at = ivec2_add(at, look->shift);
    V_DrawText(at, font, item->text, remap);
    /* The hotkey's letter is redrawn over itself in its own colour: the first
     * capital that matches, else the first small letter ("Scenario Objectives",
     * "Select Scenario"). */
    const char *key = NULL;
    int key_length = 1;
    if (item->hotkey_ink && item->mark_len > 0 &&
        item->mark_at + item->mark_len <= (int)strlen(item->text)) {
        key = item->text + item->mark_at; /* the game marked its own span */
        key_length = item->mark_len;
    } else if (item->hotkey_ink && item->hotkey > 0 && item->hotkey < 128 && isalpha((int)item->hotkey)) {
        key = strchr(item->text, toupper((int)item->hotkey));
        if (!key) key = strchr(item->text, tolower((int)item->hotkey));
    }
    if (key && ink != item->hotkey_ink) {
        char prefix[sizeof(item->text)], letter[sizeof(item->text)];
        snprintf(prefix, sizeof(prefix), "%.*s", (int)(key - item->text), item->text);
        snprintf(letter, sizeof(letter), "%.*s", key_length, key);
        uint8_t key_tint[256];
        V_DrawText((ivec2_t){at.x + V_TextWidth(font, prefix), at.y}, font, letter,
                   text_remap(item, font, state, item->hotkey_ink, key_tint));
    }
    /* Engine behaviour: the field being edited ends in an underscore. */
    if (caret && font->glyph_index['_'] >= 0)
        V_DrawText((ivec2_t){at.x + V_TextWidth(font, item->text), at.y}, font, "_", remap);
}

static void draw_list(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    uint8_t tint[256];
    irect_t clip = V_GetClip();
    V_SetClip(rect);
    if (!item->rows && item->prose && item->font) {
        V_DrawTextWrapped(rect, item->font, item->prose, NULL, 0);
    } else if (item->row_height > 0) {
        int visible = rect.h / item->row_height;
        for (int i = 0; i < visible && item->first_row + i < item->rows; ++i) {
            int row = item->first_row + i;
            bool selected = row == item->value;
            menustate_t state = selected ? MS_PUSHED : MS_NORMAL;
            const bitmapfont_t *font = state_font(item, state);
            irect_t line = {rect.x, rect.y + i * item->row_height, rect.w, item->row_height};
            draw_picture(item, state, line);
            if (selected && item->color) V_FillRect(line, V_NearestIndex(item->color));
            if (font && item->row)
                V_DrawText(ivec2_add((ivec2_t){line.x, line.y}, item->inset), font,
                           item->row(item, row), text_remap(item, font, state,
                           item->look[state].ink ? item->look[state].ink : item->ink, tint));
        }
    }
    if (item->ownerdraw) item->ownerdraw(menu, item, rect);
    V_SetClip(clip);
}

/* The thumb spans the visible rows of the linked list. */
static void draw_scrollbar(const menu_t *menu, const menuitem_t *item, irect_t bar) {
    if (item->link < 0 || item->link >= menu->numitems) return;
    const menuitem_t *list = &menu->items[item->link];
    if (item->sheet && item->thumb.part.h > 0) {
        draw_picture(item, item_state(menu, item), bar);
        int last = list->rows - page_rows(list);
        int top = last > 0 ? (bar.h - item->thumb.part.h) * list->first_row / last : 0;
        irect_t dst = {bar.x + (bar.w - item->thumb.part.w) / 2, bar.y + top,
                       item->thumb.part.w, item->thumb.part.h};
        R_DrawSprite(item->sheet, item->thumb.cell, item->thumb.palette, &item->thumb.part,
                     &dst, item->opaque ? V_OPAQUE : 0, 16);
        return;
    }
    if (list->rows <= 0 || list->row_height <= 0) return;
    int end = list->first_row + list->rect.h / list->row_height;
    if (end > list->rows) end = list->rows;
    int top = bar.h * list->first_row / list->rows;
    uint8_t color = V_NearestIndex(item->color);
    V_DrawRectOutline(bar, color);
    V_FillRect((irect_t){bar.x, bar.y + top, bar.w, bar.h * end / list->rows - top}, color);
}

static bool is_button(const menuitem_t *item) {
    return item->kind == MI_BUTTON || item->kind == MI_CHECK || item->kind == MI_DROPDOWN;
}

static void draw_slider(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    draw_picture(item, item_state(menu, item), rect);
    int64_t span = (int64_t)item->range.max - item->range.min;
    int width = item->thumb.part.w;
    if (span <= 0 || width <= 0 || width > rect.w) return;
    int64_t value = item->value;
    if (value < item->range.min) value = item->range.min;
    if (value > item->range.max) value = item->range.max;
    irect_t thumb = {rect.x + (int)((rect.w - width) * (value - item->range.min) / span),
                     rect.y + (rect.h - item->thumb.part.h) / 2, width, item->thumb.part.h};
    if (item->sheet)
        R_DrawSprite(item->sheet, item->thumb.cell, item->thumb.palette, &item->thumb.part,
                     &thumb, item->opaque ? V_OPAQUE : 0, 16);
    else V_FillRect(thumb, V_NearestIndex(item->color));
}

static void draw_chrome(const menu_t *menu, const menuitem_t *item) {
    if (!item->visible || item->kind == MI_LIST || item->kind == MI_SCROLLBAR || item->kind == MI_SLIDER) return;
    irect_t rect = M_MenuItemRect(menu, item);
    if (item->fill) V_FillRect(rect, V_NearestIndex(item->fill));
    menustate_t state = item_state(menu, item);
    draw_picture(item, state, rect);
    if (item->kind == MI_DROPDOWN) {
        const menulook_t *arrow = &item->arrow[state];
        if (item->sheet && arrow->part.w > 0) {
            irect_t dst = {rect.x + rect.w - arrow->part.w, rect.y, arrow->part.w, arrow->part.h};
            R_DrawSprite(item->sheet, arrow->cell, arrow->palette, &arrow->part, &dst, V_OPAQUE, 16);
        }
    }
    if (item->frame.outer)
        V_DrawRectOutline((irect_t){rect.x - 2, rect.y - 2, rect.w + 4, rect.h + 4},
                          V_NearestIndex(item->frame.outer));
    if (item->frame.inner)
        V_DrawRectOutline((irect_t){rect.x - 1, rect.y - 1, rect.w + 2, rect.h + 2},
                          V_NearestIndex(item->frame.inner));
    if (item->border) V_DrawRectOutline(rect, V_NearestIndex(item->border));
    if (is_button(item) && item->color && state != MS_NORMAL)
        V_DrawRectOutline(item->frame.outer ?
                          (irect_t){rect.x - 2, rect.y - 2, rect.w + 4, rect.h + 4} : rect,
                          V_NearestIndex(item->color));
    if (item->ownerdraw) item->ownerdraw(menu, item, rect);
    else if (is_button(item)) {
        menuitem_t text = *item;
        if (item->kind == MI_DROPDOWN && item->row && item->value >= 0 && item->value < item->rows)
            snprintf(text.text, sizeof(text.text), "%s", item->row(item, item->value));
        irect_t clip = V_GetClip();
        if (item->kind == MI_DROPDOWN)
            V_SetClip((irect_t){rect.x, rect.y, rect.w - item->arrow[state].part.w, rect.h});
        draw_text(&text, state, rect, false);
        V_SetClip(clip);
    }
}

static void draw_content(const menu_t *menu, const menuitem_t *item) {
    if (!item->visible || is_button(item)) return;
    irect_t rect = M_MenuItemRect(menu, item);
    if (item->kind == MI_LIST || item->kind == MI_SCROLLBAR || item->kind == MI_SLIDER) {
        if (item->fill) V_FillRect(rect, V_NearestIndex(item->fill));
        if (item->kind == MI_LIST) draw_list(menu, item, rect);
        else if (item->kind == MI_SCROLLBAR) draw_scrollbar(menu, item, rect);
        else draw_slider(menu, item, rect);
        if (item->border) V_DrawRectOutline(rect, V_NearestIndex(item->border));
        return;
    }
    if (item->ownerdraw) return;
    bool caret = item->kind == MI_TEXTFIELD && item->enabled &&
                 (item == menu->editing || (menu->modal && item == focused(menu)));
    draw_text(item, item_state(menu, item), rect, caret);
}

static void draw_message(void) {
    isize2_t space = V_DrawSize();
    irect_t box = {space.w / 8, space.h / 3, space.w * 3 / 4, space.h / 3};
    V_FillRect(box, V_NearestIndex(0xff0c1216u));
    V_DrawRectOutline(box, V_NearestIndex(0xffdce6dcu));
    V_DrawSmallText((irect_t){box.x + 12, box.y + 12, box.w - 24, 7},
                    "NETWORK GAME ENDED", 0xffdce6dcu, space);
    int columns = (box.w - 24) / 6;
    const char *text = message;
    for (int y = box.y + 36; *text && columns > 0 && y < box.y + box.h - 28; y += 12) {
        char line[256];
        size_t count = strcspn(text, "\n");
        if (count > (size_t)columns) {
            count = (size_t)columns;
            while (count && text[count] != ' ') --count;
            if (!count) count = (size_t)columns;
        }
        if (count >= sizeof(line)) count = sizeof(line) - 1;
        memcpy(line, text, count);
        line[count] = '\0';
        V_DrawSmallText((irect_t){box.x + 12, y, box.w - 24, 7}, line, 0xffdce6dcu, space);
        text += count;
        while (*text == ' ' || *text == '\n') ++text;
    }
    V_DrawSmallText((irect_t){box.x + 12, box.y + box.h - 19, box.w - 24, 7},
                    "ENTER / ESC / CLICK TO CONTINUE", 0xffdce6dcu, space);
}

/* Pictures and buttons draw first, in table order; loose text, lists and
 * scroll bars draw over them, and the tooltip over everything. A layer item
 * starts the same again over all that came before it. */
void M_MenuDrawer(menu_t *menu) {
    int previous = V_GetDrawScale();
    irect_t clip = V_GetClip();
    V_SetDrawScale(R_UIScale(menu->app));
    if (menu->refresh) menu->refresh(menu);
    if (menu->palette) I_SetPalette(menu->palette);
    if (menu->background && menu->background->numlumps) {
        irect_t dst = menu->background->cells[0].rect;
        R_DrawSprite(menu->background, 0, -1, NULL, &dst, V_OPAQUE, 16);
    }
    for (int start = 0, end; start < menu->numitems; start = end) {
        for (end = start + 1; end < menu->numitems && !menu->items[end].layer; ++end) {}
        for (int i = start; i < end; ++i) draw_chrome(menu, &menu->items[i]);
        for (int i = start; i < end; ++i) draw_content(menu, &menu->items[i]);
    }
    if (menu->dropdown && item_live(menu->dropdown)) {
        menuitem_t popup = *menu->dropdown;
        popup.value = menu->dropdown_row;
        irect_t rect = dropdown_rect(menu);
        if (popup.fill) V_FillRect(rect, V_NearestIndex(popup.fill));
        draw_list(menu, &popup, rect);
        if (popup.border) V_DrawRectOutline(rect, V_NearestIndex(popup.border));
    }
    const menuitem_t *hover = M_MenuHover(menu);
    if (hover && hover->tooltip && menu->drawtip) menu->drawtip(menu, hover);
    if (menu->modal && message[0]) draw_message();
    V_SetDrawScale(previous);
    V_SetClip(clip);
}
