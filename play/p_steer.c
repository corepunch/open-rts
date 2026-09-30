#include "engine.h"
#include "p_nav.h"

/* Locomotion and crowd handling shared by every game. The planner (p_nav.c)
 * only knows terrain; moving units are dealt with here, per tic:
 *   1. follow the waypoint list, skipping corners the disc can already cut;
 *   2. bend the heading around units ahead, always passing on the right so
 *      two units meeting head-on never choose the same side;
 *   3. resolve overlap by pushing, with idle units yielding to moving ones;
 *   4. if a unit makes no progress, replan from where it stands. */

enum {
    STUCK_TICS = 20,   /* about 0.7 s of no progress before replanning */
    MAX_REPLANS = 8,
    SKIP_LOOKAHEAD_CELLS = 12,
};

static bool ground_mover(const mobj_t *unit) {
    return unit && !unit->remove && unit->hp > 0 &&
           (unit->traits & (MF_MOBILE | MF_FLY)) == MF_MOBILE;
}

bool P_ReplanUnit(const level_t *map, mobj_t *unit) {
    navpath_t path;
    fvec2_t position = fixed3_xy_to_fvec2(unit->core.position);
    if (!P_NavPlan(map, P_MobjRadius(unit), position, unit->movement.goal, &path)) return false;
    unit->movement.path = path;
    return true;
}

bool P_SteerTarget(const level_t *map, mobj_t *unit, fvec2_t *target, bool *final) {
    navpath_t *path = &unit->movement.path;
    fvec2_t position = fixed3_xy_to_fvec2(unit->core.position);
    float radius = P_MobjRadius(unit);
    if (path->current >= path->count) {
        /* Plan was truncated (or the unit was knocked off it): continue from here. */
        if (path->complete && path->count) path->current = path->count - 1;
        else if (!P_ReplanUnit(map, unit)) return false;
        else path->current = 0;
    }
    while (path->current + 1 < path->count) {
        fvec2_t here = path->points[path->current], next = path->points[path->current + 1];
        bool near = fvec2_distance_squared(position, here) < 0.3f * 0.3f;
        bool cut = !near && fvec2_distance_squared(position, next) <
                   (float)(SKIP_LOOKAHEAD_CELLS * SKIP_LOOKAHEAD_CELLS) &&
                   P_NavLineClear(map, position, next, radius);
        if (!near && !cut) break;
        ++path->current;
    }
    *final = path->current + 1 >= path->count && path->complete;
    *target = *final ? unit->movement.goal : path->points[path->current];
    return true;
}

fvec2_t P_SteerAvoid(const mobj_t *unit, fvec2_t direction) {
    fvec2_t position = fixed3_xy_to_fvec2(unit->core.position), bend = {0, 0};
    float radius = P_MobjRadius(unit);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *other = (const mobj_t *)th;
        if (other == unit || !ground_mover(other) || P_HarvesterDocked(other)) continue;
        fvec2_t rel = fvec2_sub(fixed3_xy_to_fvec2(other->core.position), position);
        float need = radius + P_MobjRadius(other) + 0.1f, look = need + 1.2f;
        float ahead = rel.x * direction.x + rel.y * direction.y;
        if (ahead <= 0.0f || ahead > look) continue;
        float lateral = direction.x * rel.y - direction.y * rel.x; /* >0: other is to one side */
        float clearance = fabsf(lateral);
        if (clearance >= need) continue;
        /* Steer to the side the other unit is not on; dead ahead passes on the right. */
        float side = clearance < 0.05f ? 1.0f : (lateral > 0.0f ? -1.0f : 1.0f);
        float push = ((need - clearance) / need) * (1.0f - ahead / look);
        bend = fvec2_add(bend, fvec2_scale((fvec2_t){-direction.y, direction.x}, side * push));
    }
    fvec2_t steered = fvec2_add(direction, fvec2_scale(bend, 1.5f));
    float length = sqrtf(fvec2_length_squared(steered));
    if (length < 0.001f) return direction;
    steered = fvec2_scale(steered, 1.0f / length);
    /* Bound the deviation so facing stays readable and the unit still progresses. */
    if (steered.x * direction.x + steered.y * direction.y < 0.75f) {
        steered = fvec2_add(fvec2_scale(steered, 0.5f), fvec2_scale(direction, 0.5f));
        length = sqrtf(fvec2_length_squared(steered));
        steered = fvec2_scale(steered, 1.0f / length);
    }
    return steered;
}

/* Returns true if the order is still alive. Called after each movement attempt. */
bool P_SteerProgress(const level_t *map, mobj_t *unit, bool moved) {
    navpath_t *path = &unit->movement.path;
    if (moved) { unit->movement.stuck_tics = 0; return true; }
    if (++unit->movement.stuck_tics < STUCK_TICS) return true;
    unit->movement.stuck_tics = 0;
    if (++unit->movement.replans > MAX_REPLANS) return false;
    if (!P_ReplanUnit(map, unit)) return false;
    path->current = 0;
    return true;
}

void P_SeparateUnits(const level_t *map) {
    for (int iter = 0; iter < 3; ++iter) {
        for (thinker_t *tha = thinkercap.next; tha != &thinkercap; tha = tha->next) {
            mobj_t *a = (mobj_t *)tha;
            if (!ground_mover(a)) continue;
            for (thinker_t *thb = tha->next; thb != &thinkercap; thb = thb->next) {
                mobj_t *b = (mobj_t *)thb;
                if (!ground_mover(b)) continue;
                float min_dist = P_MobjRadius(a) + P_MobjRadius(b);
                fvec2_t a_position = fixed3_xy_to_fvec2(a->core.position);
                fvec2_t b_position = fixed3_xy_to_fvec2(b->core.position);
                fvec2_t delta = fvec2_sub(b_position, a_position);
                float dist2 = fvec2_length_squared(delta);
                if (dist2 >= min_dist * min_dist) continue;
                float dist = sqrtf(dist2);
                if (dist < 0.0001f) {
                    float angle = (float)((a->id * 37 + b->id * 17) % 360) * 0.01745329252f;
                    delta = (fvec2_t){ cosf(angle), sinf(angle) };
                    dist = 1.0f;
                }
                bool docked_a = P_HarvesterDocked(a), docked_b = P_HarvesterDocked(b);
                if (docked_a && docked_b) continue;
                /* Whoever is going somewhere keeps most of its ground. */
                bool moving_a = P_HasMoveOrder(a), moving_b = P_HasMoveOrder(b);
                float share_a = moving_a == moving_b ? 0.5f : (moving_a ? 0.2f : 0.8f);
                float push = min_dist - dist;
                fvec2_t unit_push = fvec2_scale(delta, push / dist);
                float weight_a = docked_a ? 0.0f : (docked_b ? 1.0f : share_a);
                float weight_b = docked_b ? 0.0f : (docked_a ? 1.0f : 1.0f - share_a);
                fvec2_t separated_a = fvec2_sub(a_position, fvec2_scale(unit_push, weight_a));
                fvec2_t separated_b = fvec2_add(b_position, fvec2_scale(unit_push, weight_b));
                if (weight_a > 0.0f && P_CheckPosition(map, a, separated_a.x, separated_a.y)) {
                    fixed3_t before = a->core.position;
                    a->core.position = fixed3_with_xy(a->core.position, separated_a);
                    P_ClampToLevel(map, a);
                    a->core.momentum = fixed3_add(a->core.momentum,
                        fixed3_planar_displacement(before, a->core.position));
                }
                if (weight_b > 0.0f && P_CheckPosition(map, b, separated_b.x, separated_b.y)) {
                    fixed3_t before = b->core.position;
                    b->core.position = fixed3_with_xy(b->core.position, separated_b);
                    P_ClampToLevel(map, b);
                    b->core.momentum = fixed3_add(b->core.momentum,
                        fixed3_planar_displacement(before, b->core.position));
                }
            }
        }
    }
}
