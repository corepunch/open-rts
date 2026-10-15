#include "z_local.h"
#include "info.h"

/* The computer player. Z has nothing to buy or harvest, so the engine's
 * scheduler only thinks for each computer team and hands the army to this
 * commander through the tactics hook. ZED.EXE's own routines
 * (attack_that_fort and its neighbours) and the CPUPLRnn.DAT settings are
 * not traced, so the plan is authored: each squad with nothing to do goes
 * for the nearest flag its team does not hold, or into an enemy fort,
 * which falls to the first unit inside (p_fort.c). The commander keeps no state of its own; what a squad is doing is
 * read back from its orders, so a saved game resumes it. */

enum {
    MAX_ARMY = 256,
    FORT_DETOUR = 8,      /* Cells a fort counts as further off than a flag. */
    CLAIM_CELLS = 2,      /* A squad headed within this of a flag has claimed it. */
};

static bool live(const mobj_t *mo) { return mo && !mo->remove && mo->hp > 0; }

/* A robot or driven vehicle of `owner` that can walk somewhere. */
static bool soldier(const mobj_t *mo, int owner) {
    return live(mo) && mo->owner == owner && Z_UnitType(mo) >= 0 && (mo->traits & MF_MOBILE);
}

/* On the way somewhere, fighting, or getting into a vehicle. */
static bool busy(const mobj_t *mo) {
    return P_HasMoveOrder(mo) || live(mo->attack.target) || mo->z.board;
}

static bool same_squad(const mobj_t *a, const mobj_t *b) {
    return a == b || (a->z.squad && a->z.squad == b->z.squad);
}

static int64_t cells2(fixed2_t a, fixed2_t b) { return fixed2_distance_squared64(a, b); }

static fixed2_t centroid(mobj_t *const *members, int count) {
    int64_t x = 0, y = 0;
    for (int i = 0; i < count; ++i) {
        x += members[i]->core.position.x;
        y += members[i]->core.position.y;
    }
    return (fixed2_t){(fixed_t)(x / count), (fixed_t)(y / count)};
}

/* Where a robot stands to take a flag; the flag itself is drawn two cells
 * lower (Z_LoadThings). */
static bool flag_goal(const mobj_t *flag, fixed2_t *goal) {
    const zmap_t *map = level.native_data;
    if (!map || !flag->z.territory || flag->z.territory > map->territory_count) return false;
    *goal = fixed2_cell_center(map->territories[flag->z.territory - 1].flag);
    return true;
}

/* Another squad of the team is already headed for `goal`. */
static bool claimed(mobj_t *const *units, int unit_count, int owner, const mobj_t *squad, fixed2_t goal) {
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *mo = units[i];
        if (!soldier(mo, owner) || same_squad(mo, squad) || !P_HasMoveOrder(mo)) continue;
        if (cells2(mo->movement.goal, goal) <= fixed_sq64(CLAIM_CELLS * FIXED_ONE)) return true;
    }
    return false;
}

/* Flag or fort: everyone heads there and the member nearest walks onto
 * the very cell, which is what takes it. */
static void take(level_t *map, mobj_t *const *members, int count, fixed2_t goal) {
    mobj_t *taker = members[0];
    for (int i = 1; i < count; ++i)
        if (cells2(fixed3_xy(members[i]->core.position), goal) <
            cells2(fixed3_xy(taker->core.position), goal)) taker = members[i];
    P_MoveUnitsAt(map, members, count, goal);
    P_MoveUnitTo(map, taker, goal);
}

/* One idle squad's next objective: the nearest flag no other squad is
 * taking (else the nearest flag), or an enemy fort, which counts as
 * FORT_DETOUR cells further off so nearby flags go first. */
static void command_squad(level_t *map, int owner, mobj_t *const *units, int unit_count,
                          mobj_t *const *members, int count) {
    fixed2_t at = centroid(members, count), flag_at = {0}, fort_at = {0};
    mobj_t *flag = NULL, *fort = NULL;
    fixed_t flag_far = 0, fort_far = 0;
    bool flag_claimed = false;
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *mo = units[i];
        fixed2_t goal;
        if (mo->remove || mo->team == members[0]->team) continue;
        if (mo->type_id == MT_FLAG && flag_goal(mo, &goal)) {
            bool taken = claimed(units, unit_count, owner, members[0], goal);
            fixed_t far = fixed2_length(fixed2_sub(goal, at));
            if (flag && (taken > flag_claimed || (taken == flag_claimed && far >= flag_far))) continue;
            flag = mo;
            flag_at = goal;
            flag_far = far;
            flag_claimed = taken;
        } else if (mo->type_id == MT_FORT && live(mo) && Z_FortEntrance(mo, &goal)) {
            fixed_t far = fixed2_length(fixed2_sub(goal, at)) + FORT_DETOUR * FIXED_ONE;
            if (fort && far >= fort_far) continue;
            fort = mo;
            fort_at = goal;
            fort_far = far;
        }
    }
    if (fort && (!flag || fort_far < flag_far)) take(map, members, count, fort_at);
    else if (flag) take(map, members, count, flag_at);
}

/* A squad on its way somewhere joins a member's fight; p_alert.c does
 * this only for squads standing idle, which leaves the player's orders be. */
static void rally(mobj_t *const *members, int count) {
    mobj_t *enemy = NULL;
    for (int i = 0; i < count && !enemy; ++i)
        if (live(members[i]->attack.target) && Z_UnitType(members[i]->attack.target) >= 0)
            enemy = members[i]->attack.target;
    for (int i = 0; enemy && i < count; ++i)
        if (!live(members[i]->attack.target) && !members[i]->z.board && P_CanTarget(members[i], enemy))
            members[i]->attack.target = enemy;
}

/* Each computer team's squads, every think: a squad joins any fight one
 * of its members is in, and an idle squad gets an objective. */
static void z_ai_tactics(level_t *map, int owner, mobj_t *const *units, int unit_count) {
    mobj_t *members[MAX_ARMY];
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *lead = units[i];
        if (!soldier(lead, owner)) continue;
        /* The squad's first unit in the list speaks for it. */
        bool first = true;
        for (int j = 0; j < i && first; ++j) first = !(soldier(units[j], owner) && same_squad(units[j], lead));
        if (!first) continue;
        int count = 0;
        bool idle = true;
        for (int j = i; j < unit_count && count < MAX_ARMY; ++j) {
            if (!soldier(units[j], owner) || !same_squad(units[j], lead)) continue;
            idle = idle && !busy(units[j]);
            members[count++] = units[j];
        }
        rally(members, count);
        if (idle) command_squad(map, owner, units, unit_count, members, count);
    }
}

/* Teams 0..3 that no human plays; nobody's things (owner 7) never think. */
static int z_ai_level(const level_t *map, int owner) {
    return owner < Z_NEUTRAL ? P_AiLevelNonHuman(map, owner) : AI_LEVEL_NONE;
}

/* ATTACK only lets the scheduler run tactics: with no faction plan and no
 * anchor, its own waves, scouting and retreats have nothing to act on. */
static const AiGameInterface z_ai_interface = {
    .name = "z",
    .features = AI_FEATURE_ATTACK,
    .player_level = z_ai_level,
    .tactics = z_ai_tactics,
};

const AiGameInterface *G_AiInterface(void) { return &z_ai_interface; }
