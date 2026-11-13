#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

/* Orc campaign objectives, missions 2..14. Each mission's victory and defeat
 * triggers come from Wargus campaigns/orc/level*o_c.sms, in script order, so
 * victory is checked before defeat. Losing every unit is ELIMINATE_DEFEAT,
 * the engine's TotalNumUnits == 0 trigger. Approximations are noted per case. */

/* PUD owner slots the scripts name: player 2 holds the rescue units of mission 2,
 * player 3 the Cho'gall of mission 6, player 1 the castle and 7 the runestone of mission 8. */
enum { SLOT_RESCUE = 2, SLOT_CHOGALL_FOE = 3, SLOT_CASTLE = 1, SLOT_RUNESTONE = 7 };

static bool live(const mobj_t *unit) { return unit && !unit->remove && unit->hp > 0; }

/* A foundation is not yet a finished structure for the objective counts. */
static bool finished(const mobj_t *unit) { return live(unit) && !W2_UnderConstruction(unit); }

static isize2_t footprint_of(const mobj_t *unit) {
    isize2_t foot = mobjinfo[unit->type_id].w2.footprint;
    if (foot.w < 1) foot.w = 1;
    if (foot.h < 1) foot.h = 1;
    return foot;
}

static ivec2_t corner_of(const mobj_t *unit) {
    return fixed2_footprint_corner(fixed3_xy(unit->core.position), footprint_of(unit));
}

/* UnitTypesCount for an owner. Enemy counts include foundations; ours count
 * only finished buildings, so a foundation cannot win the mission. */
static int count_of(mobj_t *const *units, int count, int owner, int type, bool finished_only) {
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const mobj_t *unit = units[i];
        if (unit && unit->owner == owner && unit->type_id == type &&
            (finished_only ? finished(unit) : live(unit))) ++n;
    }
    return n;
}

/* GetNumUnitsAt: finished units of `type` whose footprint touches the
 * inclusive cell rectangle. */
static int count_in(mobj_t *const *units, int count, int owner, int type,
                    int x1, int y1, int x2, int y2) {
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const mobj_t *unit = units[i];
        if (!unit || unit->owner != owner || unit->type_id != type || !finished(unit)) continue;
        ivec2_t c = corner_of(unit);
        isize2_t f = footprint_of(unit);
        if (c.x <= x2 && c.x + f.w - 1 >= x1 && c.y <= y2 && c.y + f.h - 1 >= y1) ++n;
    }
    return n;
}

/* IfNearUnit(this, ==, 1, hero, circle-of-power). Stratagus selects units
 * within one cell of the circle's footprint, so the hero's cell must lie in
 * the footprint grown by one on every side. Any owner's circle counts. */
static bool near_circle(mobj_t *const *units, int count, int owner, int hero_type) {
    for (int i = 0; i < count; ++i) {
        const mobj_t *hero = units[i];
        if (!live(hero) || hero->owner != owner || hero->type_id != hero_type) continue;
        ivec2_t cell = fixed2_cell(fixed3_xy(hero->core.position));
        for (int j = 0; j < count; ++j) {
            const mobj_t *circle = units[j];
            if (!live(circle) || circle->type_id != MT_CIRCLE_OF_POWER) continue;
            ivec2_t c = corner_of(circle);
            isize2_t f = footprint_of(circle);
            if (cell.x >= c.x - 1 && cell.x <= c.x + f.w && cell.y >= c.y - 1 && cell.y <= c.y + f.h)
                return true;
        }
    }
    return false;
}

/* Hostile to `me` the way combat judges it: another side that is not allied. */
static bool hostile(const mobj_t *me, const mobj_t *unit) {
    return unit->owner != me->owner && G_PlayerSide(unit->owner) && G_UnitHoldsSide(unit) &&
           !(P_IsAlly(me, unit) && P_IsAlly(unit, me));
}

/* GetNumOpponents(this) > 0: some enemy side has a live non-wall unit. The
 * local player's own unit supplies the allegiance; with none left the defeat
 * rule decides, so no opponent check is needed. */
static bool opponents_remain(mobj_t *const *units, int count, int owner) {
    const mobj_t *me = NULL;
    for (int i = 0; i < count && !me; ++i)
        if (live(units[i]) && units[i]->owner == owner) me = units[i];
    if (!me) return false;
    for (int i = 0; i < count; ++i) {
        const mobj_t *unit = units[i];
        if (!live(unit) || !hostile(me, unit)) continue;
        if (unit->type_id == MT_HUMAN_WALL || unit->type_id == MT_ORC_WALL) continue;
        return true;
    }
    return false;
}

/* Mission 9's shipyard zones (inclusive cell rectangles), each paired with a
 * fortress zone that must hold a finished fortress. */
static const struct { int x1, y1, x2, y2; } shipyard_zones[] = {
    {48, 20, 69, 42}, {69, 31, 74, 39}, {73, 13, 76, 34}, {51, 16, 58, 20}, {55, 12, 75, 16},
};
enum { FORTRESS_X1 = 52, FORTRESS_Y1 = 15, FORTRESS_X2 = 72, FORTRESS_Y2 = 40 };

unsigned W2_OrcObjectives(w2_mission_t *mission, mobj_t *const *units, int count) {
    const int me = consoleplayer;
    if (me < 0 || me >= 8) return ELIMINATE_DEFEAT;
    switch (mission->campaign.number) {
    case 2:
        /* level02o: Zuljin is rescued to a circle of power once player 2 has
         * no Zuljin left. Victory: no player 2 Zuljin and ours near a circle.
         * Approximation: a rescued unit is the local player's Zuljin, with no
         * rescue transfer simulated here. Losing him after holding him is defeat. */
        if (count_of(units, count, SLOT_RESCUE, MT_ZULJIN, false) == 0 &&
            near_circle(units, count, me, MT_ZULJIN)) {
            G_EndLevel(OUTCOME_VICTORY);
            break;
        }
        if (count_of(units, count, me, MT_ZULJIN, false) > 0) mission->hero_seen = true;
        else if (mission->hero_seen) G_EndLevel(OUTCOME_DEFEAT);
        break;
    case 3:
        /* level03o: one shipyard and four oil platforms. */
        if (count_of(units, count, me, MT_ORC_SHIPYARD, true) >= 1 &&
            count_of(units, count, me, MT_ORC_OIL_PLATFORM, true) >= 4)
            G_EndLevel(OUTCOME_VICTORY);
        break;
    case 4: case 5: case 7: case 10: case 11: case 12: case 13: case 14:
        /* Destroy every opponent. */
        if (!opponents_remain(units, count, me)) G_EndLevel(OUTCOME_VICTORY);
        break;
    case 6:
        /* level06o: Cho'gall (unit-double-head) rescued to a circle of power.
         * Defeat: neither the local player nor player 3 holds Cho'gall. */
        if (near_circle(units, count, me, MT_CHOGALL)) G_EndLevel(OUTCOME_VICTORY);
        if (count_of(units, count, me, MT_CHOGALL, false) == 0 &&
            count_of(units, count, SLOT_CHOGALL_FOE, MT_CHOGALL, false) == 0)
            G_EndLevel(OUTCOME_DEFEAT);
        break;
    case 8:
        /* level08o: player 7's runestone and player 1's castle are both gone. */
        if (count_of(units, count, SLOT_RUNESTONE, MT_RUNESTONE, false) == 0 &&
            count_of(units, count, SLOT_CASTLE, MT_CASTLE, false) == 0)
            G_EndLevel(OUTCOME_VICTORY);
        break;
    case 9:
        /* level09o: a fortress in its zone and a shipyard in any shipyard zone. */
        for (size_t z = 0; z < sizeof(shipyard_zones) / sizeof(*shipyard_zones); ++z) {
            if (count_in(units, count, me, MT_FORTRESS, FORTRESS_X1, FORTRESS_Y1, FORTRESS_X2, FORTRESS_Y2) > 0 &&
                count_in(units, count, me, MT_ORC_SHIPYARD, shipyard_zones[z].x1, shipyard_zones[z].y1,
                         shipyard_zones[z].x2, shipyard_zones[z].y2) > 0) {
                G_EndLevel(OUTCOME_VICTORY);
                break;
            }
        }
        break;
    default:
        break;
    }
    return ELIMINATE_DEFEAT;
}
