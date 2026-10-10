#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

/* Wargus transports carry six land units. Passengers remain ordinary mobjs
 * with a stable carrier id, hidden from the map until they disembark. */
static int passengers(const mobj_t *ship) {
    int count = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        if (th->function == P_MobjThinker && !unit->remove && unit->hp > 0 &&
            unit->w2.boarded && unit->w2.carrier == ship->id) ++count;
    }
    return count;
}

static bool can_board(const mobj_t *unit, const mobj_t *ship) {
    return unit && ship && !unit->remove && !ship->remove && unit->hp > 0 && ship->hp > 0 &&
        unit->owner == ship->owner && (unit->traits & MF_MOBILE) &&
        mobjinfo[unit->type_id].w2.domain == W2_DOMAIN_LAND &&
        passengers(ship) < mobjinfo[ship->type_id].w2.transport_capacity;
}

bool W2_BoardOrder(mobj_t *unit, mobj_t *ship) {
    if (!can_board(unit, ship) || unit->w2.boarded) return false;
    W2_InterruptHarvest(unit); W2_InterruptBuild(unit); W2_InterruptRepair(unit);
    unit->harvest.phase = HARVEST_PHASE_NONE;
    unit->w2.cast.spell = 0;
    unit->w2.stand_ground = false;
    unit->attack.target = NULL;
    unit->waypoints = (waypoints_t){0};
    P_ClearMove(unit);
    unit->w2.carrier = ship->id;
    return true;
}

bool W2_UnloadOrder(mobj_t *ship, fixed2_t goal) {
    if (!ship || !mobjinfo[ship->type_id].w2.transport_capacity || !passengers(ship)) return false;
    if (!P_MoveUnitTo(&level, ship, goal)) return false;
    ship->w2.unloading = true;
    return true;
}

static bool free_bay(mobj_t *unit, const mobj_t *ship, fixed2_t *bay) {
    isize2_t foot = mobjinfo[ship->type_id].w2.footprint;
    ivec2_t origin = fixed2_foot_origin_cell(fixed3_xy(ship->core.position), foot);
    for (int y = -1; y <= foot.h; ++y) for (int x = -1; x <= foot.w; ++x) {
        if (x >= 0 && x < foot.w && y >= 0 && y < foot.h) continue;
        fixed2_t at = fixed2_cell_center(ivec2_add(origin, (ivec2_t){x, y}));
        if (!P_CheckPosition(&level, unit, at)) continue;
        bool occupied = false;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *other = (mobj_t *)th;
            if (th->function != P_MobjThinker || other == unit || other == ship || other->remove ||
                other->hp <= 0 || (other->traits & (MF_FLY | MF_NOBLOCKMAP))) continue;
            fixed_t radius = P_MobjRadius(unit) + P_MobjRadius(other);
            if (fixed2_distance_squared64(at, fixed3_xy(other->core.position)) < fixed_sq64(radius)) {
                occupied = true; break;
            }
        }
        if (!occupied) { *bay = at; return true; }
    }
    return false;
}

bool W2_TickTransport(mobj_t *unit) {
    if (unit->w2.carrier) {
        mobj_t *ship = P_MobjById(unit->w2.carrier);
        if (unit->w2.boarded) {
            if (!ship) {
                unit->w2.buffs[W2_BUFF_ARMOR] = 0;
                P_DamageMobj(unit, NULL, unit->hp);
                P_RemoveMobj(unit);
            } else unit->core.position = ship->core.position;
            return true;
        }
        if (!can_board(unit, ship)) { unit->w2.carrier = 0; P_ClearMove(unit); return false; }
        if (W2_Distance(unit, ship) <= 1) {
            unit->w2.boarded = true;
            P_ClearMove(unit); P_MobjSetSelected(unit, false); P_MobjSetHidden(unit, true);
            unit->traits &= ~(MF_MOBILE | MF_SELECTABLE | MF_RENDERABLE);
            unit->traits |= MF_NOBLOCKMAP;
            unit->core.position = ship->core.position;
            return true;
        }
        if (!P_HasMoveOrder(unit)) {
            isize2_t foot = mobjinfo[ship->type_id].w2.footprint;
            ivec2_t cell = fixed2_foot_origin_cell(fixed3_xy(ship->core.position), foot);
            fixed2_t bay;
            if (!P_ApproachFootprint(unit, cell, foot, &bay) || !P_MoveUnitTo(&level, unit, bay)) unit->w2.carrier = 0;
        }
    }
    if (unit->w2.unloading && !P_HasMoveOrder(unit)) {
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *passenger = (mobj_t *)th;
            if (th->function != P_MobjThinker || passenger->remove || passenger->hp <= 0 ||
                !passenger->w2.boarded || passenger->w2.carrier != unit->id) continue;
            fixed2_t bay;
            if (!free_bay(passenger, unit, &bay)) continue;
            passenger->w2.carrier = 0; passenger->w2.boarded = false;
            passenger->core.position = fixed3_from_fixed2(bay, 0);
            passenger->traits = passenger->info->traits;
            P_MobjSetHidden(passenger, false);
            P_SetMobjState(passenger, mobjinfo[passenger->type_id].spawnstate);
        }
        unit->w2.unloading = false;
    }
    return false;
}
