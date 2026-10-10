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
};

static bool ground_mover(const mobj_t *unit) {
    return unit && !unit->remove && unit->hp > 0 &&
           (unit->traits & (MF_MOBILE | MF_FLY)) == MF_MOBILE;
}

bool P_ReplanUnit(const level_t *map, mobj_t *unit) {
    navpath_t path;
    fixed2_t position = fixed3_xy(unit->core.position);
    /* A unit jammed behind idle ones routes around them, as DC's bounded local
     * detour and DR's blocker-aware search both do. */
    uint8_t *soft = P_IdleBlockers(map, unit, 0);
    bool planned = P_NavPlan(map, P_MobjMoveClass(unit), P_MobjRadius(unit), position,
                             unit->movement.goal, soft, &path);
    free(soft);
    if (!planned) return false;
    unit->movement.path = path;
    return true;
}

bool P_SteerTarget(const level_t *map, mobj_t *unit, fixed2_t *target, bool *final) {
    navpath_t *path = &unit->movement.path;
    fixed2_t position = fixed3_xy(unit->core.position);
    fixed_t radius = P_MobjRadius(unit);
    if (path->current >= path->count) {
        /* Plan was truncated (or the unit was knocked off it): continue from here. */
        if (path->complete && path->count) path->current = path->count - 1;
        else if (!P_ReplanUnit(map, unit)) return false;
        else path->current = 0;
    }
    while (path->current + 1 < path->count) {
        fixed2_t here = path->points[path->current], next = path->points[path->current + 1];
        bool near = fixed2_distance_squared64(position, here) < FIXED_LIT_64(0.3 * 0.3);
        /* Skip a corner only when the next leg clears terrain, even when near
         * the waypoint. Throttle the long-leg checks, but check every tic near
         * a bend so the group can turn as soon as its discs fit. */
        bool cut = (near || ((uint32_t)leveltime + unit->id) % 2 == 0) &&
                   P_NavLineClear(map, P_MobjMoveClass(unit), position, next, radius);
        if (!cut) break;
        ++path->current;
    }
    *final = path->current + 1 >= path->count && path->complete;
    *target = *final ? unit->movement.goal : path->points[path->current];
    /* Crowds shove units off their line. Every few tics, confirm the segment ahead is
     * still clear and replan the moment it is not, instead of grinding against a wall. */
    if (((uint32_t)leveltime + unit->id) % 4 == 0 &&
        !P_NavLineClear(map, P_MobjMoveClass(unit), position, *target, radius)) {
        if (!P_ReplanUnit(map, unit)) return false;
        path->current = 0;
        *final = path->count == 1 && path->complete;
        *target = *final ? unit->movement.goal : path->points[0];
    }
    return true;
}

static fixed2_t mobj_xy(const mobj_t *unit) {
    return fixed3_xy(unit->core.position);
}

/* Bend `direction` (a 16.16 unit vector) around units ahead. Integer-only so
 * every peer steers identically. `step` is the 16.16 distance of this move. */
fixed2_t P_SteerAvoid(const mobj_t *unit, fixed2_t direction, fixed_t step) {
    fixed2_t position = mobj_xy(unit), bend = {0, 0};
    fixed_t radius = P_MobjRadius(unit);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *other = (const mobj_t *)th;
        if (other == unit || !ground_mover(other) || P_HarvesterDocked(other) ||
            P_HarvesterSharingVent(other)) continue;
        fixed2_t rel = fixed2_sub(mobj_xy(other), position);
        fixed_t need = radius + P_MobjRadius(other) + FIXED_LIT(0.1);
        fixed_t look = need + FIXED_LIT(1.2);
        fixed_t ahead = fixed2_dot(rel, direction);
        if (ahead <= 0 || ahead > look) continue;
        fixed_t lateral = (fixed_t)(((int64_t)direction.x * rel.y -
                                                        (int64_t)direction.y * rel.x) >> 16);
        fixed_t clearance = lateral < 0 ? -lateral : lateral;
        if (clearance >= need) continue;
        /* Steer to the side the other unit is not on; dead ahead passes on the right. */
        fixed_t side = clearance < FIXED_LIT(0.05) ? FIXED_ONE : (lateral > 0 ? -FIXED_ONE : FIXED_ONE);
        fixed_t push = fixed_mul32(fixed_div32(need - clearance, need),
                                   FIXED_ONE - fixed_div32(ahead, look));
        fixed2_t perp = { -direction.y, direction.x };
        bend = fixed2_add(bend, fixed2_scale(perp, fixed_mul32(side, push)));
    }
    fixed2_t steered = fixed2_add(direction, fixed2_scale(bend, FIXED_LIT(1.5)));
    fixed_t length = fixed2_length(steered);
    if (length < FIXED_LIT(0.001)) return direction;
    steered = fixed2_normalize(steered);
    /* Bound the deviation so facing stays readable and the unit still progresses. */
    if (fixed2_dot(steered, direction) < FIXED_LIT(0.75)) {
        steered = fixed2_add(fixed2_scale(steered, FIXED_LIT(0.5)), fixed2_scale(direction, FIXED_LIT(0.5)));
        steered = fixed2_normalize(steered);
    }
    /* Avoidance must not steer a clear route into terrain. Check the actual
     * fixed-point step before the mover can fall back to sliding along a wall. */
    fixed3_t candidate = fixed3_add_planar(unit->core.position,
                                           (fixed3_t){ fixed_mul32(steered.x, step),
                                                       fixed_mul32(steered.y, step), 0 });
    fixed2_t from = fixed3_xy(unit->core.position);
    return P_MapCircleWalkable(&level, P_MobjMoveClass(unit), fixed3_xy(candidate),
                               radius, &from) ? steered : direction;
}

/* Returns true if the order is still alive. Called after each movement attempt. */
bool P_SteerProgress(const level_t *map, mobj_t *unit, bool moved) {
    navpath_t *path = &unit->movement.path;
    bool last_leg = path->complete && path->current + 1 >= path->count;
    if (moved && (unit->movement.order_id || !last_leg)) { unit->movement.stuck_tics = 0; return true; }
    if (moved) {
        /* Walking in place against a crowd that shoves back is not progress. */
        fixed2_t to_goal = fixed2_sub(unit->movement.goal, mobj_xy(unit));
        fixed_t dist = fixed2_length(to_goal);
        fixed_t *best = &unit->movement.best_goal_dist;
        if (*best <= 0 || dist < *best - FIXED_LIT(0.02)) { *best = dist; unit->movement.stuck_tics = 0; return true; }
    }
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
                fixed_t min_dist = P_MobjRadius(a) + P_MobjRadius(b);
                fixed2_t a_position = mobj_xy(a), b_position = mobj_xy(b);
                fixed2_t delta = fixed2_sub(b_position, a_position);
                int64_t dist2 = fixed2_length_squared64(delta); /* 32.32 */
                if (dist2 >= (int64_t)min_dist * min_dist) continue;
                fixed_t dist = fixed2_length(delta);
                if (dist < FIXED_LIT(0.0001) + 1) {
                    /* Coincident: separate along a deterministic pseudo-random heading. */
                    uint32_t degrees = (uint32_t)((a->id * 37 + b->id * 17) % 360);
                    angle_t angle = (angle_t)(((uint64_t)degrees << 32) / 360u);
                    delta = (fixed2_t){ fixed_cos_bam(angle), fixed_sin_bam(angle) };
                    dist = FIXED_ONE;
                }
                bool docked_a = P_HarvesterDocked(a), docked_b = P_HarvesterDocked(b);
                if (docked_a && docked_b) continue;
                if (P_HarvesterSharingVent(a) || P_HarvesterSharingVent(b)) continue;
                /* Whoever is going somewhere keeps most of its ground. */
                bool moving_a = P_HasMoveOrder(a), moving_b = P_HasMoveOrder(b);
                fixed_t share_a = moving_a == moving_b ? FIXED_LIT(0.5) :
                                  (moving_a ? FIXED_LIT(0.2) : FIXED_LIT(0.8));
                if (moving_a && moving_b) {
                    /* Right of way to whoever is nearer its goal; the other gives way, so a
                     * packed group drains from the front instead of locking in place. */
                    int64_t da = fixed2_length_squared64(fixed2_sub(a_position, a->movement.goal));
                    int64_t db = fixed2_length_squared64(fixed2_sub(b_position, b->movement.goal));
                    share_a = da < db ? FIXED_LIT(0.25) : da > db ? FIXED_LIT(0.75) :
                              (a->id < b->id ? FIXED_LIT(0.4) : FIXED_LIT(0.6));
                }
                fixed_t push = min_dist - dist;
                fixed2_t unit_push = fixed2_scale(delta, fixed_div32(push, dist));
                fixed_t weight_a = docked_a ? 0 : (docked_b ? FIXED_ONE : share_a);
                fixed_t weight_b = docked_b ? 0 : (docked_a ? FIXED_ONE : FIXED_ONE - share_a);
                fixed2_t separated_a = fixed2_sub(a_position, fixed2_scale(unit_push, weight_a));
                fixed2_t separated_b = fixed2_add(b_position, fixed2_scale(unit_push, weight_b));
                if (weight_a > 0 && P_CheckPosition(map, a, separated_a)) {
                    fixed3_t before = a->core.position;
                    a->core.position = (fixed3_t){ separated_a.x, separated_a.y, before.z };
                    P_ClampToLevel(map, a);
                    a->core.momentum = fixed3_add(a->core.momentum,
                        fixed3_planar_displacement(before, a->core.position));
                }
                if (weight_b > 0 && P_CheckPosition(map, b, separated_b)) {
                    fixed3_t before = b->core.position;
                    b->core.position = (fixed3_t){ separated_b.x, separated_b.y, before.z };
                    P_ClampToLevel(map, b);
                    b->core.momentum = fixed3_add(b->core.momentum,
                        fixed3_planar_displacement(before, b->core.position));
                }
            }
        }
    }
}
