#include "dr_menu.h"

/* Multiplayer and instant action: pending the native screen layouts. */
bool DR_MultiOpen(app_t *app, const char *root, bool instant) {
    (void)app; (void)root; (void)instant;
    return true;
}
void DR_MultiClose(void) {}
bool DR_MultiActive(void) { return false; }
bool DR_MultiResponder(app_t *app, const SDL_Event *event) { (void)app; (void)event; return true; }
void DR_MultiTicker(void) {}
void DR_MultiDrawer(void) {}
