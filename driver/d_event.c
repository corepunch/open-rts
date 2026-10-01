#include "engine.h"

/* The driver releases the level first; this restarts the offline front end. */
void D_NetGameError(app_t *app) {
    M_StartMessage(neterror);
    if (netactive) {
        D_QuitNetGame();
        I_CancelNetGame();
    } else {
        /* Release a reserved lobby slot before closing its socket. */
        I_CancelNetGame();
        D_QuitNetGame();
    }
    menumap = NULL;
    menuactive = false;
    M_StartControlPanel(app);
    if (menuactive && app->running)
        fprintf(stderr, "Network session ended; returned to main menu.\n");
}

/* An active menu owns input. Otherwise Escape first cancels the HUD's
 * pending order or popup, then opens the control panel if still unhandled. */
bool D_MenuResponder(app_t *app, const SDL_Event *event, void *ui,
                      mobj_t *const *units, int unit_count) {
    if (!menuactive && event->type == SDL_KEYDOWN &&
        event->key.keysym.sym == SDLK_ESCAPE &&
        G_CustomUIResponder(ui, app, &level, units, unit_count, event)) return true;
    return M_Responder(app, event, true);
}
