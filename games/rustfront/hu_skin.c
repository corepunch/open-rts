#include "engine.h"
#include "rustfront.h"

#include <stdio.h>

#include "sprites.inc"

/* The interface's metal skins (art/ui.py, assets/ui.pcx): a panel plate, a recessed screen and
 * a button in four states, each drawn as a nine-slice so one picture fits any rectangle. The
 * sidebar (hu_rustfront.c) and the front end (m_menu.c) share them. Without the sheet every
 * call does nothing and the items keep their flat fills. */

static spritesheet_t skin;
static bool skin_loaded;

bool RF_LoadSkin(const char *root) {
    if (skin_loaded) return true;
    char path[1024];
    snprintf(path, sizeof(path), "%s/assets/ui.pcx", root ? root : g_game_default_root);
    skin_loaded = W_LoadIndexedSheet(path, &skin);
    if (!skin_loaded) fprintf(stderr, "rustfront: cannot load %s; the interface is drawn flat\n", path);
    return skin_loaded;
}

void RF_FreeSkin(void) {
    if (skin_loaded) R_FreeSprite(&skin);
    skin_loaded = false;
}

bool RF_HaveSkin(void) { return skin_loaded; }

static irect_t part_rect(rf_skinpart_t part, int *slice) {
    switch (part) {
    case RF_SKIN_PANEL: *slice = RF_UI_PANEL_SLICE; return RF_UI_PANEL;
    case RF_SKIN_WELL: *slice = RF_UI_WELL_SLICE; return RF_UI_WELL;
    case RF_SKIN_BUTTON_FOCUS: *slice = RF_UI_BUTTON_FOCUS_SLICE; return RF_UI_BUTTON_FOCUS;
    case RF_SKIN_BUTTON_PRESSED: *slice = RF_UI_BUTTON_PRESSED_SLICE; return RF_UI_BUTTON_PRESSED;
    case RF_SKIN_BUTTON_DISABLED: *slice = RF_UI_BUTTON_DISABLED_SLICE; return RF_UI_BUTTON_DISABLED;
    case RF_SKIN_BUTTON: default: *slice = RF_UI_BUTTON_SLICE; return RF_UI_BUTTON;
    }
}

/* A menu item takes the skin: a button its four states, anything else one picture. */
void RF_SkinItem(menuitem_t *item, rf_skinpart_t part) {
    if (!skin_loaded || !item) return;
    int slice;
    item->sheet = &skin;
    item->stretch = true;
    item->slice_scale = RF_UI_SCALE;
    item->fill = item->border = 0;
    if (part == RF_SKIN_BUTTON) {
        static const rf_skinpart_t STATES[MS_STATES] = {
            [MS_NORMAL] = RF_SKIN_BUTTON, [MS_FOCUS] = RF_SKIN_BUTTON_FOCUS,
            [MS_PUSHED] = RF_SKIN_BUTTON_PRESSED, [MS_DISABLED] = RF_SKIN_BUTTON_DISABLED};
        for (int s = 0; s < MS_STATES; ++s) {
            item->look[s].cell = 0;
            item->look[s].palette = -1;
            item->look[s].part = part_rect(STATES[s], &slice);
        }
        item->disabled_look = true;
        item->color = 0; /* the focus art is the outline */
    } else {
        for (int s = 0; s < MS_STATES; ++s) {
            item->look[s].cell = 0;
            item->look[s].palette = -1;
            item->look[s].part = part_rect(part, &slice);
        }
    }
    item->slice = slice;
}

/* A skin part drawn into dst (framebuffer pixels) at `scale` framebuffer pixels a layout pixel. */
void RF_DrawSkin(rf_skinpart_t part, irect_t dst, int scale) {
    if (!skin_loaded) return;
    int slice;
    irect_t src = part_rect(part, &slice);
    int edge = slice * (scale > 0 ? scale : 1) / RF_UI_SCALE;
    M_DrawNineSlice(&skin, 0, -1, src, dst, slice, edge > 0 ? edge : 1, 0, 16);
}
