#include "engine.h"

bool SB_SelectedOrder(ticorder_t order, fvec2_t goal, uint32_t target) {
    mobjlist_t units = P_ListMobjs();
    bool ok = G_SelectedTiccmd(order, units.items, units.count, goal, target);
    P_FreeMobjList(&units);
    return ok;
}

bool SB_ActivateAction(sb_state_t *st, const uiaction_t *action) {
    switch (action->action) {
    case UI_PAGE:
        st->page = action->product;
        st->order = UI_UNAVAILABLE;
        return true;
    case UI_OPTIONS: st->options_visible = true; return true;
    case UI_STOP: return SB_SelectedOrder(TC_STOP, (fvec2_t){0}, 0);
    case UI_MOVE: case UI_ATTACK: st->order = action->action; return true;
    case UI_WAYPOINT:
        st->order = st->order == UI_WAYPOINT ? UI_UNAVAILABLE : UI_WAYPOINT;
        return true;
    case UI_PATH_CLEAR: st->path.count = st->path.current = 0; return true;
    case UI_PATH_DELETE:
        if (!st->path.count) return false;
        memmove(st->path.points + st->path.current, st->path.points + st->path.current + 1,
                (--st->path.count - st->path.current) * sizeof(*st->path.points));
        if (st->path.current && st->path.current == st->path.count) --st->path.current;
        return true;
    case UI_PATH_MODE: st->path.mode = action->product; return true;
    case UI_PATH_ADVANCED: st->path_advanced = action->product != 0; return true;
    case UI_PATH_GO: {
        mobjlist_t units = P_ListMobjs();
        bool ok = G_PathOrder(units.items, units.count, &st->path);
        P_FreeMobjList(&units);
        if (ok) st->order = UI_UNAVAILABLE;
        return ok;
    }
    case UI_PATH_SAVE:
        if (!st->path.count || st->saved_path_count == MAXSAVEDPATHS) return false;
        st->saved_paths[st->saved_path_count++] = st->path;
        return true;
    case UI_PATH_DESELECT:
        st->saved_path_selection = -1;
        st->path = (waypoints_t){.mode = WP_ONCE};
        st->order = UI_UNAVAILABLE;
        return true;
    default: return false;
    }
}

bool SB_PathResponder(sb_state_t *st, const app_t *app, const SDL_Event *event) {
    if (st->order != UI_WAYPOINT || event->type != SDL_MOUSEBUTTONDOWN) return false;
    if (event->button.button == SDL_BUTTON_RIGHT) {
        st->order = UI_UNAVAILABLE;
        return true;
    }
    if (event->button.button != SDL_BUTTON_LEFT) return true;
    ivec2_t mouse;
    R_WindowToRenderPt(app, event->button.x, event->button.y, &mouse.x, &mouse.y);
    ivec2_t cell = R_ScreenToMapGrid(app, &level, mouse.x, mouse.y);
    if (!L_Contains(&level, cell.x, cell.y)) return true;
    for (int i = 0; i < st->path.count; ++i)
        if (ivec2_equal(cell, st->path.points[i])) { st->path.current = i; return true; }
    if (st->path.count < MAXWAYPOINTS) {
        st->path.current = st->path.count;
        st->path.points[st->path.count++] = cell;
    }
    return true;
}
