#ifndef __M_MENU__
#define __M_MENU__

#include "engine.h"

extern bool menuactive;
extern bool menuerror;
extern const char *menumap;

bool M_Init(app_t *app, const char *root);
void M_StartControlPanel(app_t *app);
bool M_Responder(app_t *app, const SDL_Event *event, bool inlevel);
void M_Drawer(const app_t *app);
void M_Ticker(void);
void M_Shutdown(void);

#endif
