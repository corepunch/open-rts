#include "z_local.h"

/* The five 15x15 native formation grids at 0xe63d4 are indexed by follower
 * slot and scaled by 16 at 0x77f98. An absent grid number leaves {0,0}.
 * The eight sine/cosine pairs are SINCOS.DAT words after native >> 5. */
static const int8_t formation[5][20][2] = {
    {{-16,16},{16,16},{-32,32},{0,32},{32,32},{-48,48},{-16,48},{16,48},{48,48},
     {-16,32},{16,32},{-32,48},{32,48},{-48,64},{-32,64},{-16,64},{0,64},{16,64},{32,64},{48,64}},
    {{-16,16},{0,16},{16,16},{-16,32},{0,32},{16,32},{-16,48},{0,48},{16,48},
     {-16,64},{0,64},{16,64},{-16,80},{0,80},{16,80},{-16,96},{0,96},{16,96},{-16,112},{16,112}},
    {{-32,48},{32,48},{-32,-48},{32,-48},{0,48},{0,-48},{-32,0},{32,0},{-16,48},{16,48},
     {-16,-48},{16,-48},{-32,32},{32,32},{-32,-32},{32,-32},{-32,16},{32,16},{0,0},{32,-16}},
    {{-16,0},{16,0},{-32,0},{32,0},{-48,0},{48,0},{-64,0},{64,0},{-80,0},{80,0},
     {-96,0},{96,0},{-112,0},{112,0},{-96,-64},{96,-64},{-96,64},{96,64},{0,-112},{0,112}},
    {{-32,48},{32,48},{-32,-48},{32,-48},{0,48},{0,-48},{-32,0},{32,0},{-16,48},{16,48},
     {-16,-48},{16,-48},{-32,32},{32,32},{-32,-32},{32,-32},{-32,16},{32,16},{0,0},{32,-16}},
};
static const int16_t trig[8][2] = {
    {0,1023},{724,724},{1023,0},{724,-725},{0,-1024},{-725,-725},{-1024,0},{-725,724}
};

/* 0x78ee2..0x78f2b shifts each signed product separately. */
ivec2_t Z_FormationOffset(int shape, int slot, int direction) {
    if ((unsigned)shape >= 5 || slot < 1 || slot > 20 || (unsigned)direction >= 8) return (ivec2_t){0};
    int x = formation[shape][slot - 1][0], y = formation[shape][slot - 1][1];
    int s = trig[direction][0], c = trig[direction][1];
    return (ivec2_t){((x * c) >> 10) + ((y * s) >> 10),
                     ((y * c) >> 10) - ((x * s) >> 10)};
}

mobj_t *Z_SquadLeader(mobj_t *unit) {
    if (!unit || !unit->z.squad || !unit->z.squad_slot) return unit;
    if (unit->z.squad_slot == UINT8_MAX) return unit;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *mo = (mobj_t *)th;
        if (!mo->remove && mo->hp > 0 && mo->team == unit->team && mo->z.squad == unit->z.squad &&
            mo->z.squad_slot == UINT8_MAX && Z_IsRobot(Z_UnitType(mo))) return mo;
    }
    return unit;
}

void Z_SquadOrder(mobj_t *leader, bool active) {
    if (!leader || !leader->z.squad || leader->z.squad_slot != UINT8_MAX) return;
    leader->z.formation_active = active;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *mo = (mobj_t *)th;
        if (mo == leader || mo->remove || mo->hp <= 0 || mo->team != leader->team ||
            mo->z.squad != leader->z.squad || !mo->z.squad_slot || mo->z.squad_slot == UINT8_MAX ||
            !Z_IsRobot(Z_UnitType(mo))) continue;
        Z_DetourCancel(mo);
        mo->z.formation_rejoin = false;
        mo->z.board = 0;
        mo->z.service = 0;
        mo->z.route = (zroute_t){0};
        mo->waypoints = (waypoints_t){0};
        mo->attack.target = NULL;
        mo->move_only = leader->move_only;
        P_ClearMove(mo);
    }
}

/* 0x5a648 chooses the eight movement directions with a half-axis boundary;
 * 0x61eed stores its negation as the group's formation rotation. */
static int direction(fixed2_t delta) {
    int64_t x = delta.x, y = delta.y, ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    int d;
    if (ax >= ay && ay * 2 <= ax) d = x > 0 ? 2 : 6;
    else if (ay > ax && ax * 2 < ay) d = y < 0 ? 0 : 4;
    else if (x > 0) d = y < 0 ? 1 : 3;
    else d = y < 0 ? 7 : 5;
    return (8 - d) & 7;
}

void Z_SquadTick(mobj_t *unit) {
    if (!unit || unit->remove || unit->hp <= 0 || !unit->z.squad || !unit->z.squad_slot) return;
    if (unit->z.squad_slot == UINT8_MAX) {
        if (unit->z.formation_active && P_HasMoveOrder(unit)) {
            fixed2_t goal = unit->z.detour.phase ? unit->z.detour.resume : unit->movement.goal;
            const navpath_t *path = &unit->movement.path;
            if (!unit->z.detour.phase && path->current >= 0 && path->current < path->count)
                goal = path->points[path->current];
            fixed2_t delta = fixed2_sub(goal, fixed3_xy(unit->core.position));
            if (delta.x || delta.y) unit->z.formation_direction = (uint8_t)direction(delta);
        }
        return;
    }
    mobj_t *leader = Z_SquadLeader(unit);
    if (leader == unit || !leader->z.formation_active || (!P_HasMoveOrder(leader) && !unit->z.formation_rejoin) ||
        unit->z.board || unit->z.service || unit->z.detour.phase || unit->z.route.active ||
        unit->z.route.count || P_HasMoveOrder(unit) || P_CanTarget(unit, unit->attack.target)) return;
    ivec2_t offset = Z_FormationOffset(leader->z.formation, unit->z.squad_slot, leader->z.formation_direction);
    fixed2_t goal = fixed2_add(fixed3_xy(leader->core.position),
                               (fixed2_t){offset.x * (FIXED_ONE / Z_TILE), offset.y * (FIXED_ONE / Z_TILE)});
    unit->z.formation_rejoin = false;
    if (fixed2_distance_squared64(fixed3_xy(unit->core.position), goal) < fixed_sq64(2 * FIXED_ONE)) return;
    P_MoveUnitTo(&level, unit, goal);
}

/* Native 0x77b97 promotes the first occupied follower slot and transfers
 * the leader's order. Replan from the survivor, not the departed path origin. */
void Z_SquadLeave(mobj_t *unit) {
    if (!unit || !unit->z.squad || !unit->z.squad_slot) return;
    mobj_t *next = NULL;
    if (unit->z.squad_slot == UINT8_MAX) {
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            mobj_t *mo = (mobj_t *)th;
            if (mo == unit || mo->remove || mo->hp <= 0 || mo->team != unit->team ||
                mo->z.squad != unit->z.squad || !mo->z.squad_slot || mo->z.squad_slot == UINT8_MAX ||
                !Z_IsRobot(Z_UnitType(mo))) continue;
            if (!next || mo->z.squad_slot < next->z.squad_slot) next = mo;
        }
    }
    if (next) {
        next->z.squad_slot = UINT8_MAX;
        next->z.formation = unit->z.formation;
        next->z.formation_direction = unit->z.formation_direction;
        next->z.formation_active = unit->z.formation_active;
        next->z.board = 0;
        next->z.service = 0;
        P_ClearMove(next);
        next->z.route = unit->z.route;
        next->waypoints = unit->waypoints;
        next->move_only = unit->move_only;
        next->attack.target = unit->attack.target;
        if (unit->z.detour.phase || P_HasMoveOrder(unit))
            P_MoveUnitTo(&level, next, unit->z.detour.phase ? unit->z.detour.resume : unit->movement.goal);
        if (P_MobjIsSelected(unit)) P_MobjSetSelected(next, true);
    }
    unit->z.squad = unit->z.squad_slot = 0;
    unit->z.formation_active = unit->z.formation_rejoin = false;
}

/* Crew can leave at different times, and the departing leader may have
 * promoted a member still outside. Reuse only slots not occupied there. */
void Z_SquadEjected(mobj_t **units, int count) {
    if (!count || !units[0]->z.squad) return;
    bool used[Z_CREW_MAX] = {false}, leader = false;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *mo = (mobj_t *)th;
        if (mo->remove || mo->hp <= 0 || mo->team != units[0]->team ||
            mo->z.squad != units[0]->z.squad || !Z_IsRobot(Z_UnitType(mo))) continue;
        if (mo->z.squad_slot == UINT8_MAX) leader = true;
        else if (mo->z.squad_slot < Z_CREW_MAX) used[mo->z.squad_slot] = true;
    }
    for (int i = 0; i < count; ++i) {
        if (!leader) { units[i]->z.squad_slot = UINT8_MAX; leader = true; continue; }
        int slot = 1;
        while (slot < Z_CREW_MAX && used[slot]) ++slot;
        if (slot == Z_CREW_MAX) units[i]->z.squad = units[i]->z.squad_slot = 0;
        else { units[i]->z.squad_slot = (uint8_t)slot; used[slot] = true; }
    }
}
