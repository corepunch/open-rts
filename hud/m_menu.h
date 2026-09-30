#ifndef __M_MENU_ENGINE__
#define __M_MENU_ENGINE__

#include "m_vec.h"
#include "sprites.h"

#include <SDL.h>
#include <stdbool.h>

typedef enum {
    MI_LABEL, MI_BUTTON, MI_CHECK, MI_PICTURE, MI_TEXTFIELD, MI_LIST,
    MI_SCROLLBAR, MI_SLIDER, MI_SPIN, MI_CUSTOM
} menuitemkind_t;

typedef struct menu_s menu_t;
typedef struct menuitem_s menuitem_t;

typedef enum {
    MA_ACTIVATE, MA_FOCUS, MA_BLUR, MA_PRESS, MA_RELEASE, MA_CHANGE, MA_ROW
} menuaction_t;

typedef void (*menuroutine_t)(menu_t *menu, menuitem_t *item, menuaction_t action);
typedef void (*menudraw_t)(const menu_t *menu, const menuitem_t *item);

struct menuitem_s {
    menuitemkind_t kind;
    irect_t rect;
    bool visible, enabled;
    const spritesheet_t *sheet;
    int cell_normal, cell_pushed, cell_checked;
    const bitmapfont_t *font;
    const uint8_t *remap;
    bool centered;
    char text[128];
    int maxchars;
    int value, min, max, step;
    int row_height, rows, first_row;
    menuroutine_t routine;
    menudraw_t ownerdraw;
    void *userdata;
    int userid;
    bool pressed, hover;
};

struct menu_s {
    menuitem_t *items;
    int numitems;
    int itemOn; /* index into items[]; Dark Colony stores native control ids here */
    int grab;   /* pressed item index, or -1 */
    ivec2_t cursor;
    isize2_t space; /* script coordinate space; {0,0} means app->win */
    const spritesheet_t *background;
    menu_t *prevMenu;
    void (*escape)(menu_t *menu);
    void (*ticker)(menu_t *menu);
    void (*wheel)(menu_t *menu, int delta);
    void (*drawer)(const menu_t *menu);
    void *owner;
    bool inlevel;
};

void M_MenuOpen(menu_t *menu);
void M_MenuClose(void);
bool M_MenuResponder(const SDL_Event *ev);
void M_MenuTicker(void);
void M_MenuDrawer(void);
menuitem_t *M_MenuItemAt(const menu_t *menu, ivec2_t logical);
void M_MenuSetText(menuitem_t *item, const char *text);
menuitem_t *M_MenuFind(const menu_t *menu, int userid);
void M_MenuFocusId(menu_t *menu, int userid);

#endif
