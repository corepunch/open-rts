#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <limits.h>

/* Human campaign objectives for missions 2-14: the victory and defeat triggers
 * of reference/wargus/campaigns/human/levelNNh_c.sms. The victory trigger is
 * checked first, as the script lists it. Defeat by TotalNumUnits == 0 is the
 * engine's ELIMINATE_DEFEAT, so it is not repeated here.
 *
 * Stratagus runs triggers every cycle. Checks here run every 30 tics, so a
 * count that the script holds for 120 cycles is four checks. */

enum { W2_HUMAN_HOLD_CHECKS = 4 };

static bool live(const mobj_t *unit) { return unit && !unit->remove && unit->hp > 0; }

static bool is_wall(const mobj_t *unit) {
    return unit->type_id == MT_HUMAN_WALL || unit->type_id == MT_ORC_WALL;
}

/* GetPlayerData(owner, "UnitTypesCount", type). Stratagus counts a building
 * from the moment it is placed, so a site under construction counts too. */
static int type_count(mobj_t *const *units, int count, int owner, int type) {
    int n = 0;
    for (int i = 0; i < count; ++i)
        if (live(units[i]) && units[i]->owner == owner && units[i]->type_id == type) ++n;
    return n;
}

/* GetPlayerData(owner, "TotalNumUnits"): the owner's units that hold its side. */
static int total_count(mobj_t *const *units, int count, int owner) {
    int n = 0;
    for (int i = 0; i < count; ++i)
        if (live(units[i]) && units[i]->owner == owner && G_UnitHoldsSide(units[i])) ++n;
    return n;
}

/* IfNearUnit and IfRescuedNearUnit: some unit of target type has between min
 * and max units of the given type within one cell of it, the scan Stratagus
 * makes with SelectAroundUnit(centre, 1). Rescue is approximated: a unit of
 * this owner counts as rescued, since the port does not convert rescued units. */
static bool around_target(mobj_t *const *units, int count, int owner, int type,
                          int target, int min, int max) {
    for (int c = 0; c < count; ++c) {
        const mobj_t *centre = units[c];
        if (!live(centre) || centre->type_id != target) continue;
        int n = 0;
        for (int i = 0; i < count; ++i) {
            const mobj_t *unit = units[i];
            if (live(unit) && unit->owner == owner && unit->type_id == type &&
                W2_Distance(unit, centre) <= 1) ++n;
        }
        if (n >= min && n <= max) return true;
    }
    return false;
}

/* GetNumOpponents(this) > 0: a side that is not allied with this player keeps
 * a unit that is not a wall. Allies are judged by P_IsAlly, as combat judges them. */
static bool opponents_left(mobj_t *const *units, int count, int me) {
    const mobj_t *mine = NULL;
    for (int i = 0; i < count && !mine; ++i)
        if (live(units[i]) && units[i]->owner == me && G_UnitHoldsSide(units[i])) mine = units[i];
    for (int i = 0; i < count; ++i) {
        const mobj_t *unit = units[i];
        if (!live(unit) || unit->owner == me || !G_PlayerSide(unit->owner) ||
            !G_UnitHoldsSide(unit) || is_wall(unit)) continue;
        if (!P_IsAlly(mine, unit)) return true;
    }
    return false;
}

unsigned W2_HumanObjectives(w2_mission_t *mission, mobj_t *const *units, int count) {
    const int me = consoleplayer;
    if (me < 0 || me >= 8) return ELIMINATE_DEFEAT;
    bool won = false, lost = false;
    switch (mission->campaign.number) {
    case 2:
        /* Player 6 has no rangers or archers, and one of ours was rescued onto
         * a circle of power. Held for 120 cycles. */
        if (!type_count(units, count, 6, MT_RANGER) && !type_count(units, count, 6, MT_ARCHER) &&
            (around_target(units, count, me, MT_RANGER, MT_CIRCLE_OF_POWER, 1, INT_MAX) ||
             around_target(units, count, me, MT_ARCHER, MT_CIRCLE_OF_POWER, 1, INT_MAX)))
            won = ++mission->hold >= W2_HUMAN_HOLD_CHECKS;
        break;
    case 3:
        /* A shipyard and four oil platforms. */
        won = type_count(units, count, me, MT_HUMAN_SHIPYARD) >= 1 &&
              type_count(units, count, me, MT_HUMAN_OIL_PLATFORM) >= 4;
        break;
    case 4: case 5: case 6: case 13:
        /* GetNumOpponents == 0: destroy every enemy. */
        won = !opponents_left(units, count, me);
        break;
    case 7:
        /* Player 2 has no orc refinery left. */
        won = type_count(units, count, 2, MT_ORC_REFINERY) == 0;
        break;
    case 8:
        /* A castle, no peasants left for players 2 and 4, and no opponents. */
        won = type_count(units, count, me, MT_CASTLE) >= 1 &&
              type_count(units, count, 2, MT_PEASANT) == 0 &&
              type_count(units, count, 4, MT_PEASANT) == 0 &&
              !opponents_left(units, count, me);
        break;
    case 9:
        /* Exactly one Uther beside a circle of power. Lost when neither this
         * player nor player 3 has Uther left. */
        won = around_target(units, count, me, MT_UTHER_LIGHTBRINGER, MT_CIRCLE_OF_POWER, 1, 1);
        lost = type_count(units, count, me, MT_UTHER_LIGHTBRINGER) == 0 &&
               type_count(units, count, 3, MT_UTHER_LIGHTBRINGER) == 0;
        break;
    case 10:
        /* Four attack peasants rescued onto a circle of power. Lost when fewer
         * than four attack peasants remain between this player and player 4. */
        won = around_target(units, count, me, MT_ATTACK_PEASANT, MT_CIRCLE_OF_POWER, 4, INT_MAX);
        lost = type_count(units, count, me, MT_ATTACK_PEASANT) +
               type_count(units, count, 4, MT_ATTACK_PEASANT) < 4;
        break;
    case 11:
        /* Player 3 has no units left, and no opponents. */
        won = total_count(units, count, 3) == 0 && !opponents_left(units, count, me);
        break;
    case 12:
        /* Player 0 has no transport, refinery or shipyard. */
        won = type_count(units, count, 0, MT_ORC_TRANSPORT) == 0 &&
              type_count(units, count, 0, MT_ORC_REFINERY) == 0 &&
              type_count(units, count, 0, MT_ORC_SHIPYARD) == 0;
        break;
    case 14:
        /* The neutral dark portal (owner 15) is gone. */
        won = type_count(units, count, 15, MT_DARK_PORTAL) == 0;
        break;
    default:
        break;
    }
    if (won) G_EndLevel(OUTCOME_VICTORY);
    else if (lost) G_EndLevel(OUTCOME_DEFEAT);
    return ELIMINATE_DEFEAT;
}
