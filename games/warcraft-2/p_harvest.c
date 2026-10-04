#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <stdlib.h>

/* PUD deposits and SQM forests share the level's resource table. Workers
 * remain ordinary thinkers; entry waits and axe swings use state tics. */
bool w2_init_resources(level_t *map) {
    const w2_pud_t *pud = map->native_data;
    int count = 0;
    for (int i = 0; i < map->width * map->height; ++i)
        count += map->cell_terrain[i] == 2;
    for (int i = 0; i < pud->unit_count; ++i)
        count += pud->units[i].type == 92;
    map->resource_vents = calloc((size_t)count, sizeof(*map->resource_vents));
    if (count && !map->resource_vents) return false;
    for (int y = 0; y < map->height; ++y)
        for (int x = 0; x < map->width; ++x) {
            if (map->cell_terrain[L_Index(map, x, y)] != 2) continue;
            ivec2_t cell = {x, y};
            map->resource_vents[map->resource_vent_count++] = (resourcevent_t){
                .cell = cell, .attachment = fvec2_cell_center(cell), .footprint = {1, 1},
                .amount = 100, .rate = 100, .resource_type = 1, .active = true,
            };
        }
    return true;
}

static bool worker(const mobj_t *unit) {
    return unit && !unit->remove && unit->hp > 0 &&
        (unit->type_id == MT_PEASANT || unit->type_id == MT_PEON);
}

int W2_ResourceIncome(int owner, int resource) {
    int bonus = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *base = (const mobj_t *)th;
        if (base->remove || base->hp <= 0 || base->owner != owner) continue;
        int value = 0;
        if (resource == 0)
            value = base->type_id == 89 || base->type_id == 90 ? 10 :
                    base->type_id == 91 || base->type_id == 92 ? 20 : 0;
        else if (resource == 1 && (base->type_id == 77 || base->type_id == 78)) value = 25;
        else if (resource == 2 && (base->type_id == 85 || base->type_id == 86)) value = 25;
        if (value > bonus) bonus = value;
    }
    return 100 + bonus;
}

static resourcevent_t *deposit(const mobj_t *unit) {
    int index = unit->harvest.target;
    return index >= 0 && index < level.resource_vent_count ? &level.resource_vents[index] : NULL;
}

/* Approach the actual footprint, never the blocked building centre. The
 * nav component check excludes banks and trees behind an enclosing wall. */
static bool approach(mobj_t *unit, ivec2_t cell, isize2_t size, fvec2_t *bay) {
    fvec2_t from = fixed3_xy_to_fvec2(unit->core.position);
    bool found = false;
    float distance = 0;
    for (int y = -1; y <= size.h; ++y)
        for (int x = -1; x <= size.w; ++x) {
            if (x >= 0 && y >= 0 && x < size.w && y < size.h) continue;
            ivec2_t candidate = ivec2_add(cell, (ivec2_t){x, y});
            fvec2_t at = fvec2_cell_center(candidate);
            float d = fvec2_distance_squared(from, at);
            if ((found && d >= distance) || !P_CheckPosition(&level, unit, at.x, at.y)) continue;
            bool occupied = false;
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                if (th->function != P_MobjThinker) continue;
                const mobj_t *other = (const mobj_t *)th;
                if (other == unit || other->remove || other->hp <= 0 || !(other->traits & MF_MOBILE)) continue;
                float radius = P_MobjRadius(unit) + P_MobjRadius(other);
                if (fvec2_distance_squared(at, fixed3_xy_to_fvec2(other->core.position)) < radius * radius) {
                    occupied = true;
                    break;
                }
            }
            if (occupied) continue;
            if (!P_NavReachable(&level, P_MobjMoveClass(unit), fvec2_cell(from), candidate)) continue;
            *bay = at;
            distance = d;
            found = true;
        }
    return found;
}

static bool go_to_deposit(mobj_t *unit, resourcevent_t *vent) {
    fvec2_t bay;
    if (!P_VentOpenTo(&level, vent, unit) ||
        !approach(unit, vent->cell, vent->footprint, &bay) || !P_MoveUnitTo(&level, unit, bay)) return false;
    unit->movement.order_id = 0;
    unit->harvest.phase = HARVEST_PHASE_TO_MINE;
    return true;
}

void W2_InterruptHarvest(mobj_t *unit) {
    if (!worker(unit)) return;
    unit->traits |= MF_RENDERABLE | MF_SELECTABLE | MF_MOBILE;
    unit->traits &= ~MF_NOBLOCKMAP;
    P_MobjSetHidden(unit, false);
    unit->w2.chops = 0;
    P_SetMobjState(unit, gameinfo->mobjinfo[unit->type_id].spawnstate);
}

bool W2_ReturnGoods(mobj_t *unit) {
    if (!worker(unit) || !unit->harvest.cargo) return false;
    mobj_t *best = NULL;
    fvec2_t bay = {0};
    float distance = 0;
    fvec2_t from = fixed3_xy_to_fvec2(unit->core.position);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *base = (mobj_t *)th;
        if (base->remove || base->hp <= 0 || base->owner != unit->owner) continue;
        int pud = base->type_id - 1;
        if (pud < 0 || pud >= W2_TYPE_COUNT) continue;
        const w2_unit_t *type = &w2_units[pud];
        if (!(type->flags & W2_HALL) && !(unit->harvest.resource_type == 1 && (pud == 76 || pud == 77))) continue;
        isize2_t size = {type->tw, type->th};
        ivec2_t cell = fvec2_cell(fvec2_sub(fixed3_xy_to_fvec2(base->core.position),
                                         (fvec2_t){size.w * 0.5f, size.h * 0.5f}));
        fvec2_t at;
        if (!approach(unit, cell, size, &at)) continue;
        float d = fvec2_distance_squared(from, at);
        if (best && d >= distance) continue;
        best = base;
        bay = at;
        distance = d;
    }
    W2_InterruptHarvest(unit);
    P_ClearMove(unit);
    unit->attack.target = NULL;
    unit->harvest.base = best;
    unit->harvest.phase = HARVEST_PHASE_TO_BASE;
    if (!best || !P_MoveUnitTo(&level, unit, bay)) return false;
    unit->movement.order_id = 0;
    unit->harvest.return_position = bay;
    return true;
}

bool W2_HarvestOrder(mobj_t *unit, fvec2_t goal) {
    if (!worker(unit)) return false;
    for (int i = 0; i < level.resource_vent_count; ++i) {
        resourcevent_t *vent = &level.resource_vents[i];
        if (!P_ResourceVentContainsCell(vent, fvec2_cell(goal)) || !P_VentOpenTo(&level, vent, unit)) continue;
        fvec2_t bay;
        if (!approach(unit, vent->cell, vent->footprint, &bay)) return false;
        W2_InterruptHarvest(unit);
        unit->attack.target = NULL;
        unit->harvest.target = i;
        unit->harvest.base = NULL;
        if (unit->harvest.cargo) {
            W2_ReturnGoods(unit);
            return true;
        }
        unit->harvest.resource_type = vent->resource_type;
        return go_to_deposit(unit, vent);
    }
    return false;
}

static bool next_tree(mobj_t *unit, fvec2_t origin) {
    int best = -1;
    float distance = 0;
    fvec2_t bay;
    for (int i = 0; i < level.resource_vent_count; ++i) {
        resourcevent_t *vent = &level.resource_vents[i];
        if (!vent->active || vent->resource_type != 1) continue;
        float d = fvec2_distance_squared(origin, vent->attachment);
        if ((best >= 0 && d >= distance) || !approach(unit, vent->cell, vent->footprint, &bay)) continue;
        best = i;
        distance = d;
    }
    unit->w2.chops = 0;
    unit->harvest.target = best;
    if (best < 0) { unit->harvest.phase = HARVEST_PHASE_NONE; return false; }
    return go_to_deposit(unit, &level.resource_vents[best]);
}

static void exhaust(resourcevent_t *vent) {
    vent->active = false;
    if (vent->resource_type == 1) {
        int cell = L_Index(&level, vent->cell.x, vent->cell.y);
        level.cell_terrain[cell] = level.blocked[cell] = 0;
        level.tile_ids[cell] = W2_TILE_LOOKUP; /* Native removed-tree megatile 126. */
    } else {
        mobj_t *mine = P_MobjById(vent->source_id);
        if (!mine) return;
        for (int y = 0; y < vent->footprint.h; ++y)
            for (int x = 0; x < vent->footprint.w; ++x) {
                int cell = L_Index(&level, vent->cell.x + x, vent->cell.y + y);
                level.cell_solid[cell] = 0;
                level.blocked[cell] = level.cell_terrain[cell] != 0;
            }
        P_RemoveMobj(mine);
    }
}

static void wait_inside(mobj_t *unit, int phase) {
    P_ClearMove(unit);
    unit->harvest.phase = phase;
    unit->traits &= ~(MF_RENDERABLE | MF_SELECTABLE | MF_MOBILE);
    unit->traits |= MF_NOBLOCKMAP;
    P_MobjSetHidden(unit, true);
    P_SetMobjState(unit, W2_WAIT_STATE(unit->type_id - 1));
}

bool W2_TickHarvest(mobj_t *unit) {
    if (!worker(unit) || unit->harvest.phase == HARVEST_PHASE_NONE) return false;
    resourcevent_t *vent = deposit(unit);
    int group = gameinfo->states[unit->core.state_id].group;
    if (unit->harvest.phase == HARVEST_PHASE_TO_BASE) {
        mobj_t *base = unit->harvest.base;
        if (!base || base->remove || base->hp <= 0) { W2_ReturnGoods(unit); return false; }
        if (P_HasMoveOrder(unit)) return false;
        if (!unit->movement.order_arrived || !fvec2_near(fixed3_xy_to_fvec2(unit->core.position),
                                                        unit->harvest.return_position, 0.001f)) {
            W2_ReturnGoods(unit);
            return false;
        }
        wait_inside(unit, HARVEST_PHASE_UNLOADING);
        return true;
    }
    if (unit->harvest.phase == HARVEST_PHASE_UNLOADING) {
        if (group == 5) return true;
        mobj_t *base = unit->harvest.base;
        if (!base || base->remove || base->hp <= 0) { W2_ReturnGoods(unit); return true; }
        if (unit->owner < 8)
            level.player_resources[unit->owner][unit->harvest.resource_type] +=
                unit->harvest.cargo * W2_ResourceIncome(unit->owner, unit->harvest.resource_type) / 100;
        unit->harvest.cargo = 0;
        W2_InterruptHarvest(unit);
        unit->harvest.base = NULL;
        if (vent && P_VentOpenTo(&level, vent, unit)) {
            unit->harvest.resource_type = vent->resource_type;
            go_to_deposit(unit, vent);
        } else if (vent && vent->resource_type == 1) next_tree(unit, vent->attachment);
        else unit->harvest.phase = HARVEST_PHASE_NONE;
        return true;
    }
    if (!vent || !P_VentOpenTo(&level, vent, unit)) {
        W2_InterruptHarvest(unit);
        if (unit->harvest.cargo) W2_ReturnGoods(unit);
        else if (vent && vent->resource_type == 1) next_tree(unit, vent->attachment);
        else unit->harvest.phase = HARVEST_PHASE_NONE;
        return false;
    }
    if (unit->harvest.phase == HARVEST_PHASE_TO_MINE) {
        if (P_HasMoveOrder(unit)) return false;
        if (!unit->movement.order_arrived ||
            !fvec2_near(fixed3_xy_to_fvec2(unit->core.position), unit->movement.goal, 0.001f)) {
            go_to_deposit(unit, vent);
            return false;
        }
        if (vent->resource_type == 0) wait_inside(unit, HARVEST_PHASE_MINING);
        else {
            fvec2_t delta = fvec2_sub(vent->attachment, fixed3_xy_to_fvec2(unit->core.position));
            unit->core.angle = angle_from_screen_vector(delta.x, delta.y);
            unit->harvest.phase = HARVEST_PHASE_MINING;
            P_SetMobjState(unit, W2_WORK_STATE(unit->type_id - 1));
        }
        return true;
    }
    if (group == 5) return true;
    if (vent->resource_type == 1 && ++unit->w2.chops < 51) {
        P_SetMobjState(unit, W2_WORK_STATE(unit->type_id - 1));
        return true;
    }
    int take = vent->amount < 100 ? vent->amount : 100;
    unit->harvest.cargo = take;
    unit->harvest.resource_type = vent->resource_type;
    vent->amount -= take;
    if (!vent->amount) exhaust(vent);
    W2_ReturnGoods(unit);
    return true;
}

void W2_WorkerPose(mobj_t *unit) {
    if (!worker(unit)) return;
    int group = gameinfo->states[unit->core.state_id].group;
    if (group != 0 && group != 2) return;
    int pud = unit->type_id - 1;
    int state = unit->harvest.cargo ? W2_CARRY_STATE((pud - 2) * 2 + unit->harvest.resource_type) : 1 + pud * 2;
    if (group == 2) ++state;
    if (state != unit->core.state_id) P_SetMobjState(unit, state);
}
