#include "p_local.h"
#include "game.h"

/* Called by the ordinary mobj thinker: routes survive a local combat stop. */
void P_TickWaypoints(mobj_t *actor) {
    waypoints_t *path = &actor->waypoints;
    if (!path->count || !(actor->traits & MF_MOBILE)) return;
    if (gameinfo && actor->core.state_id > 0 && actor->core.state_id < gameinfo->state_count &&
        gameinfo->states[actor->core.state_id].group == 3) return;
    mobj_t *target = actor->attack.target;
    float range = actor->info ? actor->info->attack.range : 0;
    if (P_CanTarget(actor, target) &&
        fvec2_distance_squared(fixed3_xy_to_fvec2(actor->core.position),
                               fixed3_xy_to_fvec2(target->core.position)) <= range * range)
        return;
    actor->attack.target = NULL;
    if (actor->movement.order_arrived) {
        if (path->mode == WP_BACKTRACK && path->count > 1) {
            if (path->current == path->count - 1) path->backwards = true;
            else if (!path->current) path->backwards = false;
            path->current += path->backwards ? -1 : 1;
        } else if (path->current + 1 < path->count) {
            ++path->current;
        } else if (path->mode == WP_LOOP) {
            path->current = 0;
        } else {
            *path = (waypoints_t){0};
            return;
        }
        actor->movement.order_arrived = false;
        P_ClearMove(actor);
        actor->movement.order_id = 0;
    }
    if (!P_HasMoveOrder(actor))
        P_MoveUnitTo(&level, actor, fvec2_cell_center(path->points[path->current]));
}
