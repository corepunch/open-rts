#ifndef __DR_MENU__
#define __DR_MENU__

#include "engine.h"

/* Shared by the outer shell (m_menu.c) and the multiplayer screens
 * (m_multi.c). Items are built per screen; ids are native control ids. */
enum { DR_MAXITEMS = 160, DR_NUMFONTS = 14 };

typedef struct {
    menu_t menu;
    menuitem_t items[DR_MAXITEMS];
    int count;
} drscreen_t;

extern drscreen_t drscreen;

bool DR_ShellOpen(const char *root);
void DR_ShellClose(void);
bool DR_ShellImage(const char *name, spritesheet_t *out);
bool DR_ShellFont(const char *name, bitmapfont_t *out);
bool DR_StripFont(const spritesheet_t *strip, int top, bitmapfont_t *out);

void DR_ScreenClear(void);
/* Text in the native TEXT widget's normal, hover and pressed fonts. */
menuitem_t *DR_Text(irect_t rect, int align, const char *text, const bitmapfont_t *normal,
                    const bitmapfont_t *hover, const bitmapfont_t *pressed);
menuitem_t *DR_Button(irect_t rect, int id, const char *text, const bitmapfont_t *normal,
                      const bitmapfont_t *hover, const bitmapfont_t *pressed);

/* Entry points of the multiplayer and instant-action screens. */
bool DR_MultiOpen(app_t *app, const char *root, bool instant);
void DR_MultiClose(void);
bool DR_MultiActive(void);
void DR_MultiTicker(void);
void DR_ShellReturn(app_t *app);

#endif
