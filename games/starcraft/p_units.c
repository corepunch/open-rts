#include "sc_local.h"
#include <math.h>
/* StarCraft units that carry, launch or join others: Carrier interceptors
 * and Reaver scarabs from a hangar, Terran add-ons beside their building,
 * Bunkers that hold infantry, Nuclear Silos, and High Templar merging into
 * an Archon. Everything is an ordinary mobj with a parent id. */
enum {
    SC_UPGRADE_U238 = 16, SC_UPGRADE_GROOVED_SPINES = 30, SC_UPGRADE_SINGULARITY = 33,
    SC_UPGRADE_REAVER_CAPACITY = 36, SC_UPGRADE_CARRIER_CAPACITY = 43,
    SC_SCARAB_FRAMES = 90, /* A scarab that has not struck by then fizzles. */
};
#define SC_LEASH FIXED_FROM_INT(12) /* Interceptors turn home past this many cells from the Carrier. */

static fixed2_t where(const mobj_t *mo) { return fixed3_xy(mo->core.position); }
static bool live(const thinker_t *th) {
    const mobj_t *mo = (const mobj_t *)th;
    return th->function == P_MobjThinker && !mo->remove && mo->hp > 0;
}
static ivec2_t pixel(fixed2_t at) { return (ivec2_t){(at.x + FIXED_ONE / 64) >> (FIXED_FRAC_BITS - 5), (at.y + FIXED_ONE / 64) >> (FIXED_FRAC_BITS - 5)}; }
/* A unit of type on owner's side at a spot. */
static mobj_t *spawn_for(const mobj_t *owner, uint16_t type, fixed2_t at) {
    mobj_t *mo = sc_spawn_actor(type - 1u, pixel(at), owner->owner);
    if (!mo) return NULL;
    mo->team = owner->team;
    mo->allegiance = owner->allegiance;
    return mo;
}

/* ── hangars ─────────────────────────────────────────────────────────── */

uint16_t sc_hangar_type(uint16_t maker) {
    return maker == MT_CARRIER ? MT_INTERCEPTOR : maker == MT_REAVER ? MT_SCARAB :
           maker == MT_NUCLEAR_SILO ? MT_NUCLEAR_MISSILE : MT_NONE;
}
/* Retail: four interceptors (eight with Carrier Capacity), five scarabs
 * (ten with Reaver Capacity), one nuke. */
int sc_hangar_capacity(const mobj_t *maker) {
    switch (maker->type_id) {
    case MT_CARRIER: return sc_upgrade_level(maker->owner, SC_UPGRADE_CARRIER_CAPACITY) ? 8 : 4;
    case MT_REAVER: return sc_upgrade_level(maker->owner, SC_UPGRADE_REAVER_CAPACITY) ? 10 : 5;
    case MT_NUCLEAR_SILO: return 1;
    default: return 0;
    }
}
int sc_hangar_count(const mobj_t *maker) {
    int count = maker->sc.hangar;
    uint16_t child = sc_hangar_type(maker->type_id);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (live(th) && mo->type_id == child && mo->sc.parent == maker->id) ++count;
    }
    return count;
}

/* A Carrier sends out every docked interceptor and turns back the ones on
 * their way home; a Reaver fires one scarab, if it has one. */
bool sc_launch(mobj_t *attacker, const weapondef_t *weapon, mobj_t *target) {
    (void)weapon;
    if (attacker->type_id == MT_CARRIER) {
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *mo = (mobj_t *)th;
            if (!live(th) || mo->type_id != MT_INTERCEPTOR || mo->sc.parent != attacker->id ||
                (mo->attack.target && mo->attack.target->hp > 0 && !(mo->sc.flags & SC_RETURNING))) continue;
            mo->sc.flags &= ~SC_RETURNING;
            mo->move_only = false;
            P_ClearMove(mo);
            mo->attack.target = target;
        }
        for (; attacker->sc.hangar > 0; attacker->sc.hangar--) {
            mobj_t *mo = spawn_for(attacker, MT_INTERCEPTOR, where(attacker));
            if (!mo) break;
            mo->traits &= ~MF_SELECTABLE;
            mo->sc.parent = attacker->id;
            mo->attack.target = target;
        }
        return true;
    }
    if (attacker->type_id != MT_REAVER) return false;
    if (attacker->sc.hangar <= 0) return true;
    fixed2_t at = where(attacker), to = fixed2_sub(where(target), at);
    fixed_t d = fixed2_length(to);
    mobj_t *mo = spawn_for(attacker, MT_SCARAB, d > FIXED_ONE / 2 ? fixed2_add(at, fixed2_rescale(to, d, FIXED_ONE / 2)) : at);
    if (!mo) return true;
    attacker->sc.hangar--;
    /* Nobody can target a scarab. */
    mo->traits = (mo->traits & ~MF_SELECTABLE) | MF_NOBLOCKMAP;
    mo->sc.parent = attacker->id;
    mo->sc.timers[SC_TIMER_LIFE] = SC_SCARAB_FRAMES;
    mo->attack.target = target;
    return true;
}

static void interceptor(mobj_t *mo) {
    mobj_t *carrier = P_MobjById(mo->sc.parent);
    if (!carrier) { P_DamageMobj(mo, NULL, mo->hp); return; }
    fixed2_t home = where(carrier);
    int64_t d2 = fixed2_distance_squared64(where(mo), home);
    if (!(mo->sc.flags & SC_RETURNING)) {
        mobj_t *t = mo->attack.target;
        if ((!t || t->remove || t->hp <= 0) && carrier->attack.target && carrier->attack.target->hp > 0)
            mo->attack.target = t = carrier->attack.target;
        if (d2 <= fixed_sq64(SC_LEASH) && t && !t->remove && t->hp > 0) return;
        mo->sc.flags |= SC_RETURNING;
        mo->attack.target = NULL;
        mo->move_only = true;
        P_MoveUnitTo(&level, mo, home);
        return;
    }
    /* Docked interceptors come back whole: the hangar repairs them. */
    if (d2 <= fixed_sq64(FIXED_ONE)) { carrier->sc.hangar++; P_RemoveMobj(mo); return; }
    if (!P_HasMoveOrder(mo) || !fixed2_near(mo->movement.goal, home, FIXED_ONE)) P_MoveUnitTo(&level, mo, home);
}

/* ── range ───────────────────────────────────────────────────────────── */

/* A Bunker adds a cell to what it holds; range upgrades add theirs. */
fixed_t sc_range_bonus(const mobj_t *attacker, const weapondef_t *weapon) {
    (void)weapon;
    fixed_t bonus = (attacker->sc.flags & SC_LOADED) ? FIXED_ONE : 0;
    int owner = attacker->owner;
    switch (attacker->type_id) {
    case MT_MARINE: return bonus + FIXED_FROM_INT(sc_upgrade_level(owner, SC_UPGRADE_U238));
    case MT_HYDRALISK: return bonus + FIXED_FROM_INT(sc_upgrade_level(owner, SC_UPGRADE_GROOVED_SPINES));
    case MT_DRAGOON: return bonus + FIXED_FROM_INT(2 * sc_upgrade_level(owner, SC_UPGRADE_SINGULARITY));
    default: return bonus;
    }
}

/* ── add-ons ─────────────────────────────────────────────────────────── */

static const struct { mobjtype_id_t addon, parent; } addons[] = {
    {MT_COMSAT_STATION, MT_COMMAND_CENTER}, {MT_NUCLEAR_SILO, MT_COMMAND_CENTER}, {MT_MACHINE_SHOP, MT_FACTORY},
    {MT_CONTROL_TOWER, MT_STARPORT}, {MT_COVERT_OPS, MT_SCIENCE_FACILITY}, {MT_PHYSICS_LAB, MT_SCIENCE_FACILITY},
};
uint16_t sc_addon_parent(uint16_t type) {
    for (unsigned i = 0; i < sizeof(addons) / sizeof(*addons); i++) if (addons[i].addon == type) return addons[i].parent;
    return MT_NONE;
}
mobj_t *sc_addon_of(const mobj_t *building) {
    mobj_t *addon = building ? P_MobjById(building->sc.addon) : NULL;
    return addon && addon->sc.parent == building->id ? addon : NULL;
}
/* units.dat places an add-on's top-left this many pixels from its
 * building's: right of it, level with its lower edge. */
bool sc_addon_site(const mobj_t *building, uint16_t type, ivec2_t *cell) {
    irect_t at = P_MobjCells(building);
    ivec2_t offset = sc_units[type - 1].addon;
    *cell = (ivec2_t){at.x + offset.x / 32, at.y + offset.y / 32};
    return P_CanPlaceBuilding(type, *cell, building);
}
bool sc_addon_place(uint16_t type, ivec2_t cell, uint16_t *addon, irect_t *out) {
    for (unsigned i = 0; i < sizeof(addons) / sizeof(*addons); i++) {
        if (addons[i].parent != type) continue;
        ivec2_t offset = sc_units[addons[i].addon - 1].addon;
        isize2_t size = actor_types[addons[i].addon - 1].footprint;
        *addon = addons[i].addon;
        *out = (irect_t){cell.x + offset.x / 32, cell.y + offset.y / 32, size.w, size.h};
        return true;
    }
    return false;
}
mobj_t *sc_attach_addon(mobj_t *building, uint16_t type) {
    ivec2_t cell;
    if (sc_addon_of(building) || sc_addon_parent(type) != building->type_id || !sc_addon_site(building, type, &cell))
        return NULL;
    mobj_t *addon = spawn_for(building, type, P_BuildingPosition(type, cell));
    if (!addon) return NULL;
    addon->sc.parent = building->id;
    building->sc.addon = addon->id;
    P_SyncBuildingBlocking();
    return addon;
}

/* ── bunkers ─────────────────────────────────────────────────────────── */

int sc_cargo_space(const mobj_t *bunker) {
    int used = 0;
    for (int i = 0; i < 4; i++) {
        const mobj_t *mo = P_MobjById(bunker->sc.cargo[i]);
        if (mo && (mo->sc.flags & SC_LOADED) && mo->sc.parent == bunker->id) used += sc_units[mo->type_id - 1].space;
    }
    return used;
}
/* Terran infantry: organic walkers of one slot that do not gather. */
bool sc_can_board(const mobj_t *unit, const mobj_t *bunker) {
    const sc_unit_t *u = sc_unit(unit), *b = sc_unit(bunker);
    return u && b && unit != bunker && unit->owner == bunker->owner && bunker->hp > 0 && !bunker->remove &&
        b->space_provided > 0 && !(unit->sc.flags & (SC_LOADED | SC_HALLUCINATION)) && (unit->traits & MF_MOBILE) &&
        (u->race & 2) && (u->flags & SC_UNIT_ORGANIC) && !(u->flags & SC_UNIT_WORKER) && u->space == 1 &&
        sc_cargo_space(bunker) + u->space <= b->space_provided;
}
static fixed_t gap(const mobj_t *unit, irect_t r) {
    fixed2_t at = where(unit);
    fixed_t x0 = FIXED_FROM_INT(r.x), x1 = FIXED_FROM_INT(r.x + r.w), y0 = FIXED_FROM_INT(r.y), y1 = FIXED_FROM_INT(r.y + r.h);
    fixed_t dx = at.x < x0 ? x0 - at.x : at.x > x1 ? at.x - x1 : 0;
    fixed_t dy = at.y < y0 ? y0 - at.y : at.y > y1 ? at.y - y1 : 0;
    return dx > dy ? dx : dy;
}
static void load(mobj_t *unit, mobj_t *bunker) {
    for (int i = 0; i < 4; i++) {
        const mobj_t *in = P_MobjById(bunker->sc.cargo[i]);
        if (in && (in->sc.flags & SC_LOADED) && in->sc.parent == bunker->id) continue;
        bunker->sc.cargo[i] = unit->id;
        unit->sc.parent = bunker->id;
        unit->sc.flags |= SC_LOADED;
        unit->sc.order = (sc_order_t){0};
        unit->traits = (unit->traits & ~(MF_SELECTABLE | MF_SELECTED | MF_MOBILE)) | MF_NOBLOCKMAP | MF_DONTDRAW;
        P_ClearMove(unit);
        unit->movement.order_arrived = true;
        unit->attack.target = NULL;
        unit->move_only = false;
        unit->core.position = bunker->core.position;
        return;
    }
}
bool sc_board(mobj_t *unit, mobj_t *bunker) {
    if (!sc_can_board(unit, bunker)) return false;
    irect_t r = P_MobjCells(bunker);
    if (gap(unit, r) <= FIXED_ONE) { load(unit, bunker); return true; }
    fixed2_t bay;
    if (!P_ApproachFootprint(unit, (ivec2_t){r.x, r.y}, (isize2_t){r.w, r.h}, &bay) || !P_MoveUnitTo(&level, unit, bay))
        return false;
    unit->sc.order = (sc_order_t){.kind = SC_ORDER_BOARD, .target = bunker->id};
    unit->attack.target = NULL;
    return true;
}
/* Out beside the cells it was in, nearest the middle of their lower edge,
 * wherever the ground is open and nobody stands. */
static void pop_out(mobj_t *unit, irect_t r) {
    unit->sc.flags &= ~SC_LOADED;
    unit->sc.parent = 0;
    unit->traits = (unit->traits & ~(MF_NOBLOCKMAP | MF_DONTDRAW)) | (unit->info->traits & (MF_SELECTABLE | MF_MOBILE));
    fixed2_t door = {FIXED_FROM_INT(r.x) + FIXED_FROM_INT(r.w) / 2, FIXED_FROM_INT(r.y + r.h)}, best = where(unit);
    int64_t best_d = -1;
    for (int y = -1; y <= r.h; y++)
        for (int x = -1; x <= r.w; x++) {
            if (x >= 0 && y >= 0 && x < r.w && y < r.h) continue;
            fixed2_t at = fixed2_cell_center((ivec2_t){r.x + x, r.y + y});
            int64_t d = fixed2_distance_squared64(at, door);
            if ((best_d >= 0 && d >= best_d) || !P_CheckPosition(&level, unit, at)) continue;
            bool taken = false;
            for (thinker_t *th = thinkercap.next; th != &thinkercap && !taken; th = th->next) {
                const mobj_t *mo = (const mobj_t *)th;
                fixed_t room = P_MobjRadius(unit) + P_MobjRadius(mo);
                taken = live(th) && mo != unit && (mo->traits & MF_MOBILE) && !(mo->traits & MF_FLY) &&
                    fixed2_distance_squared64(where(mo), at) < fixed_sq64(room);
            }
            if (!taken) { best = at; best_d = d; }
        }
    unit->core.position = fixed3_with_xy(unit->core.position, best);
    P_ClearMove(unit);
    unit->movement.goal = where(unit);
    unit->movement.order_arrived = true;
}
void sc_unload(mobj_t *bunker) {
    for (int i = 0; i < 4; i++) {
        mobj_t *mo = P_MobjById(bunker->sc.cargo[i]);
        bunker->sc.cargo[i] = 0;
        if (mo && (mo->sc.flags & SC_LOADED) && mo->sc.parent == bunker->id) pop_out(mo, P_MobjCells(bunker));
    }
}
/* Survivors of a fallen Bunker step out of its rubble. */
static void loaded(mobj_t *mo) {
    const mobj_t *bunker = P_MobjById(mo->sc.parent);
    if (bunker) return;
    isize2_t foot = actor_types[MT_BUNKER - 1].footprint;
    fixed2_t at = where(mo);
    pop_out(mo, (irect_t){(at.x - FIXED_FROM_INT(foot.w) / 2 + 66) >> FIXED_FRAC_BITS,
                          (at.y - FIXED_FROM_INT(foot.h) / 2 + 66) >> FIXED_FRAC_BITS, foot.w, foot.h});
}

/* ── archons ─────────────────────────────────────────────────────────── */

bool sc_merge(mobj_t *a, mobj_t *b) {
    if (!a || !b || a == b || a->type_id != MT_HIGH_TEMPLAR || b->type_id != MT_HIGH_TEMPLAR ||
        a->owner != b->owner || a->hp <= 0 || b->hp <= 0 || ((a->sc.flags | b->sc.flags) & SC_HALLUCINATION) ||
        !sc_has_tech(a->owner, SC_TECH_ARCHON_WARP)) return false;
    int time = sc_units[MT_ARCHON - 1].build_time;
    a->sc.order = (sc_order_t){.kind = SC_ORDER_MERGE, .tech = SC_TECH_ARCHON_WARP, .target = b->id, .time = time};
    b->sc.order = (sc_order_t){.kind = SC_ORDER_MERGE, .tech = SC_TECH_ARCHON_WARP, .target = a->id, .time = time};
    P_MoveUnitTo(&level, a, where(b));
    P_MoveUnitTo(&level, b, where(a));
    return true;
}
/* They walk together, stand while the warp takes the Archon's build time,
 * and the lower id becomes the Archon with full shields. */
static void merging(mobj_t *mo, int frames) {
    mobj_t *partner = P_MobjById(mo->sc.order.target);
    if (!partner || partner->sc.order.kind != SC_ORDER_MERGE || partner->sc.order.target != mo->id) {
        mo->sc.order = (sc_order_t){0};
        return;
    }
    mo->attack.target = NULL;
    if (fixed2_distance_squared64(where(mo), where(partner)) > fixed_sq64(FIXED_ONE + FIXED_ONE / 4)) {
        if (!P_HasMoveOrder(mo)) P_MoveUnitTo(&level, mo, where(partner));
        return;
    }
    P_ClearMove(mo);
    mo->movement.order_arrived = true;
    if (mo->id > partner->id || (mo->sc.order.time -= frames) > 0) return;
    if (!P_MorphMobj(mo, MT_ARCHON)) return;
    mo->hp = mo->max_hp;
    mo->sc.shields = sc_units[MT_ARCHON - 1].shields << 8;
    mo->sc.energy = 0;
    mo->sc.order = (sc_order_t){0};
    P_RemoveMobj(partner);
}

/* ── nukes ───────────────────────────────────────────────────────────── */

mobj_t *sc_armed_silo(int owner) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (live(th) && mo->owner == owner && mo->type_id == MT_NUCLEAR_SILO && mo->sc.hangar > 0) return mo;
    }
    return NULL;
}

/* ── upkeep and orders ───────────────────────────────────────────────── */

void sc_unit_ticker(mobj_t *mo, int frames) {
    if (mo->sc.flags & SC_LOADED) { loaded(mo); return; }
    if (mo->type_id == MT_INTERCEPTOR && mo->sc.parent) interceptor(mo);
    else if (mo->type_id == MT_SCARAB && mo->sc.parent &&
             (mo->attack.cooldown_left_ms > 0 || !mo->attack.target || mo->attack.target->hp <= 0)) P_RemoveMobj(mo);
    if (mo->remove || mo->hp <= 0) return;
    if (mo->sc.order.kind == SC_ORDER_MERGE) merging(mo, frames);
    else if (mo->sc.order.kind == SC_ORDER_BOARD) {
        mobj_t *bunker = P_MobjById(mo->sc.order.target);
        if (!bunker || !sc_can_board(mo, bunker)) mo->sc.order = (sc_order_t){0};
        else if (gap(mo, P_MobjCells(bunker)) <= FIXED_ONE) load(mo, bunker);
        else if (!P_HasMoveOrder(mo)) mo->sc.order = (sc_order_t){0};
    }
}

bool sc_order(ticorder_t order, mobj_t *const *units, int count, int tech, mobj_t *target, fixed2_t at) {
    bool any = false;
    switch (order) {
    case TC_DEPLOY:
        for (int i = 0; i < count; i++) sc_cast(units[i], SC_TECH_SIEGE_MODE, NULL, at);
        return true;
    case TC_SPELL:
        if (tech == SC_TECH_ARCHON_WARP) {
            mobj_t *first = NULL;
            for (int i = 0; i < count; i++) {
                if (units[i]->type_id != MT_HIGH_TEMPLAR || units[i]->sc.order.kind == SC_ORDER_MERGE) continue;
                if (!first) first = units[i];
                else if (sc_merge(first, units[i])) first = NULL;
            }
            return true;
        }
        /* One caster serves an aimed spell; toggles switch every caster. */
        for (int i = 0; i < count && (!any || !sc_tech_aimed(tech)); i++) any |= sc_cast(units[i], tech, target, at);
        return true;
    case TC_BOARD:
        for (int i = 0; i < count; i++) sc_board(units[i], target);
        return true;
    case TC_UNLOAD:
        for (int i = 0; i < count; i++) if (sc_unit(units[i])->space_provided) sc_unload(units[i]);
        return true;
    case TC_ORDER:
        /* A right click on an own Bunker sends in whoever fits. */
        if (!target || target->type_id != MT_BUNKER) return false;
        for (int i = 0; i < count; i++) any |= sc_board(units[i], target);
        return any;
    default:
        return false;
    }
}
