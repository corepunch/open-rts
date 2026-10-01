#include "engine.h"

/* An active menu owns input. Otherwise Escape first cancels the HUD's
 * pending order or popup, then opens the control panel if still unhandled. */
bool D_MenuResponder(app_t *app, const SDL_Event *event, void *ui,
                      mobj_t *const *units, int unit_count) {
    if (!menuactive && event->type == SDL_KEYDOWN &&
        event->key.keysym.sym == SDLK_ESCAPE &&
        G_CustomUIResponder(ui, app, &level, units, unit_count, event)) return true;
    return M_Responder(app, event, true);
}
