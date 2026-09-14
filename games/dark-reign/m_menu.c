#include "m_menu.h"

/* This game has no front end yet. */
bool menuactive;
bool menuerror;
const char *menumap;
bool M_Init(app_t *app, const char *root) { (void)app; (void)root; return true; }
void M_StartControlPanel(app_t *app) { (void)app; }
bool M_Responder(app_t *app, const SDL_Event *event, bool inlevel) {
    (void)app; (void)event; (void)inlevel;
    return false;
}
void M_Drawer(const app_t *app) { (void)app; }
void M_Shutdown(void) {}
void M_Ticker(void) {}
