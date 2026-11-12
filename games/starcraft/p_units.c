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
    return maker == MT_CARRIER || maker == MT_GANTRITHOR ? MT_INTERCEPTOR :
           maker == MT_REAVER || maker == MT_WARBRINGER ? MT_SCARAB :
           maker == MT_NUCLEAR_SILO ? MT_NUCLEAR_MISSILE : MT_NONE;
}
/* Retail: four interceptors (eight with Carrier Capacity), five scarabs
 * (ten with Reaver Capacity), one nuke. */
int sc_hangar_capacity(const mobj_t *maker) {
    switch (maker->type_id) {
    case MT_GANTRITHOR: return 8;
    case MT_WARBRINGER: return 10;
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
/* Scourge and Infested Terrans spend themselves on their one blow: the
 * Scourge on its target, the Infested Terran's blast around it with
 * weapons.dat's three splash rings. */
static bool suicide(mobj_t *attacker, const weapondef_t *weapon, mobj_t *target) {
    const sc_weapon_t *w = sc_weapon(weapon->native_id);
    if (!w || (attacker->type_id != MT_SCOURGE && attacker->type_id != MT_INFESTED_TERRAN)) return false;
    if (attacker->sc.flags & SC_HALLUCINATION) { P_DamageMobj(attacker, NULL, attacker->hp); return true; }
    if (w->splash[0] <= 0) {
        int damage = sc_hit(attacker->owner, w, target, w->damage, 1);
        if (damage > 0) P_DamageMobj(target, attacker, damage);
    } else {
        fixed2_t at = where(target);
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *v = (mobj_t *)th;
            if (!live(th) || v == attacker || !sc_unit(v) || (v->traits & (MF_FLY | MF_NOBLOCKMAP)) ||
                (v->sc.flags & SC_LOADED)) continue;
            fixed_t d = fixed2_length(fixed2_sub(where(v), at)) - P_MobjRadius(v);
            int divisor = d <= SC_PIXELS(w->splash[0]) ? 1 : d <= SC_PIXELS(w->splash[1]) ? 2 :
                          d <= SC_PIXELS(w->splash[2]) ? 4 : 0;
            int damage = divisor ? sc_hit(attacker->owner, w, v, w->damage, divisor) : 0;
            if (damage > 0) P_DamageMobj(v, attacker, damage);
        }
    }
    P_DamageMobj(attacker, NULL, attacker->hp);
    return true;
}

bool sc_launch(mobj_t *attacker, const weapondef_t *weapon, mobj_t *target) {
    if (suicide(attacker, weapon, target)) return true;
    if (sc_hangar_type(attacker->type_id) == MT_INTERCEPTOR) {
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
    if (sc_hangar_type(attacker->type_id) != MT_SCARAB) return sc_projectile_fire(attacker,weapon,target);
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
void sc_detach_addon(mobj_t *building) {
    mobj_t *addon = sc_addon_of(building);
    if (addon) {
        sc_interrupt(addon);
        if (addon->production) sc_cancel_production(addon, -1);
        addon->sc.parent = 0; addon->owner = addon->team = 11;
        addon->allegiance = ALLEGIANCE_NEUTRAL;
        P_MobjSetSelected(addon, false);
    }
    building->sc.addon = 0;
}
void sc_reconnect_addon(mobj_t *building, ivec2_t at) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *addon = (mobj_t *)th;
        if (!live(th) || addon->sc.parent || (addon->owner != 11 && addon->owner != building->owner) ||
            sc_addon_parent(addon->type_id) != building->type_id) continue;
        ivec2_t offset = sc_unit(addon)->addon;
        ivec2_t cell = ivec2_add(at, (ivec2_t){offset.x / 32, offset.y / 32});
        if (!fixed2_equal(P_BuildingPosition(addon->type_id, cell), fixed3_xy(addon->core.position))) continue;
        addon->sc.parent = building->id; building->sc.addon = addon->id;
        addon->owner = building->owner; addon->team = building->team; addon->allegiance = building->allegiance;
        break;
    }
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

/* ── bunkers and transports ──────────────────────────────────────────── */

enum { SC_UPGRADE_VENTRAL_SACS = 24, SC_UNLOAD_FRAMES = 15 };
static bool is_building(const mobj_t *mo) { return (sc_units[mo->type_id - 1].flags & SC_UNIT_BUILDING) != 0; }
/* OpenBW unit_provides_space: an Overlord carries once Ventral Sacs are
 * evolved; hallucinations carry nothing. */
bool sc_carries(const mobj_t *carrier) {
    const sc_unit_t *b = sc_unit(carrier);
    return b && b->space_provided > 0 && carrier->hp > 0 && !carrier->remove &&
        !(carrier->sc.flags & SC_HALLUCINATION) && !sc_disabled(carrier) &&
        (carrier->type_id != MT_OVERLORD || sc_upgrade_level(carrier->owner, SC_UPGRADE_VENTRAL_SACS));
}
static bool aboard(const mobj_t *mo, const mobj_t *carrier) {
    return mo && (mo->sc.flags & SC_LOADED) && mo->sc.parent == carrier->id;
}
int sc_cargo_space(const mobj_t *carrier) {
    int used = 0;
    for (int i = 0; i < SC_CARGO; i++) {
        const mobj_t *mo = P_MobjById(carrier->sc.cargo[i]);
        if (aboard(mo, carrier)) used += sc_units[mo->type_id - 1].space;
    }
    return used;
}
/* OpenBW unit_can_load_target: an own walker that fits, not burrowed; a
 * Bunker takes only one-slot Terrans. */
bool sc_can_board(const mobj_t *unit, const mobj_t *carrier) {
    const sc_unit_t *u = sc_unit(unit), *b = sc_unit(carrier);
    if (!u || !b || unit == carrier || unit->owner != carrier->owner || !sc_carries(carrier) ||
        (unit->sc.flags & (SC_LOADED | SC_HALLUCINATION | SC_BURROWED)) || !(unit->traits & MF_MOBILE) ||
        (unit->traits & MF_FLY) || (u->flags & SC_UNIT_BUILDING) || u->space < 1 || u->space > 8 ||
        sc_disabled(unit) || unit->sc.order.kind == SC_ORDER_INFEST) return false;
    if (is_building(carrier) && (!(u->race & 2) || u->space > 1)) return false;
    return sc_cargo_space(carrier) + u->space <= b->space_provided;
}
static fixed_t gap(const mobj_t *unit, irect_t r) {
    fixed2_t at = where(unit);
    fixed_t x0 = FIXED_FROM_INT(r.x), x1 = FIXED_FROM_INT(r.x + r.w), y0 = FIXED_FROM_INT(r.y), y1 = FIXED_FROM_INT(r.y + r.h);
    fixed_t dx = at.x < x0 ? x0 - at.x : at.x > x1 ? at.x - x1 : 0;
    fixed_t dy = at.y < y0 ? y0 - at.y : at.y > y1 ? at.y - y1 : 0;
    return dx > dy ? dx : dy;
}
/* Close enough to step in: beside a Bunker, under a transport. */
static bool at_door(const mobj_t *unit, const mobj_t *carrier) {
    return is_building(carrier) ? gap(unit, P_MobjCells(carrier)) <= FIXED_ONE : sc_unit_distance(unit, carrier) <= 4;
}
static void load(mobj_t *unit, mobj_t *carrier) {
    for (int i = 0; i < SC_CARGO; i++) {
        if (aboard(P_MobjById(carrier->sc.cargo[i]), carrier)) continue;
        carrier->sc.cargo[i] = unit->id;
        unit->sc.parent = carrier->id;
        unit->sc.flags |= SC_LOADED;
        unit->sc.order = (sc_order_t){0};
        unit->traits = (unit->traits & ~(MF_SELECTABLE | MF_SELECTED | MF_MOBILE)) | MF_NOBLOCKMAP | MF_DONTDRAW;
        P_ClearMove(unit);
        unit->movement.order_arrived = true;
        unit->attack.target = NULL;
        unit->move_only = false;
        unit->harvest.target = -1;
        unit->harvest.base = NULL;
        unit->harvest.phase = unit->harvest.timer_ms = 0;
        unit->core.position = fixed3_with_xy(unit->core.position, where(carrier));
        /* Whatever was coming to fetch it may stop. */
        if (!is_building(carrier) && carrier->sc.order.kind == SC_ORDER_BOARD && carrier->sc.order.target == unit->id) {
            carrier->sc.order = (sc_order_t){0};
            P_ClearMove(carrier);
        }
        return;
    }
}
/* A transport with nothing else to do flies over to fetch a boarder. */
static void fetch(mobj_t *carrier, const mobj_t *unit) {
    if (is_building(carrier) || !(carrier->traits & MF_MOBILE) || P_HasMoveOrder(carrier) ||
        carrier->sc.order.kind != SC_ORDER_NONE) return;
    carrier->sc.order = (sc_order_t){.kind = SC_ORDER_BOARD, .target = unit->id};
    P_MoveUnitTo(&level, carrier, where(unit));
}
bool sc_board(mobj_t *unit, mobj_t *carrier) {
    if (!carrier || !sc_can_board(unit, carrier)) return false;
    sc_interrupt(unit);
    unit->waypoints=(waypoints_t){0};
    if (at_door(unit, carrier)) { load(unit, carrier); return true; }
    fixed2_t bay = where(carrier);
    if (is_building(carrier)) {
        irect_t r = P_MobjCells(carrier);
        if (!P_ApproachFootprint(unit, (ivec2_t){r.x, r.y}, (isize2_t){r.w, r.h}, &bay)) return false;
    }
    if (!P_MoveUnitTo(&level, unit, bay)) return false;
    unit->sc.order = (sc_order_t){.kind = SC_ORDER_BOARD, .target = carrier->id};
    unit->attack.target = NULL;
    unit->move_only = true; /* Walking in, it does not stop to fight. */
    fetch(carrier, unit);
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
/* Down from a transport onto open ground beneath it; false when there is
 * none, and the passenger stays aboard. */
static bool drop(mobj_t *unit, mobj_t *carrier) {
    unit->traits = (unit->traits & ~(MF_NOBLOCKMAP | MF_DONTDRAW)) | (unit->info->traits & (MF_SELECTABLE | MF_MOBILE));
    unit->sc.flags &= ~SC_LOADED;
    if (!sc_place_near(unit, where(carrier))) {
        unit->sc.flags |= SC_LOADED;
        unit->traits = (unit->traits & ~(MF_SELECTABLE | MF_MOBILE)) | MF_NOBLOCKMAP | MF_DONTDRAW;
        return false;
    }
    for (int i = 0; i < SC_CARGO; i++) if (carrier->sc.cargo[i] == unit->id) carrier->sc.cargo[i] = 0;
    unit->sc.parent = 0;
    P_ClearMove(unit);
    unit->movement.goal = where(unit);
    unit->movement.order_arrived = true;
    return true;
}
/* A Bunker empties at once. */
void sc_unload(mobj_t *bunker) {
    for (int i = 0; i < SC_CARGO; i++) {
        mobj_t *mo = P_MobjById(bunker->sc.cargo[i]);
        bunker->sc.cargo[i] = 0;
        if (aboard(mo, bunker)) pop_out(mo, P_MobjCells(bunker));
    }
}
/* A transport flies to the spot and sets its passengers down there, one
 * every 15 frames (OpenBW order_MoveUnload and unit_unload). */
void sc_unload_at(mobj_t *carrier, fixed2_t at) {
    if (is_building(carrier)) { sc_unload(carrier); return; }
    if (!sc_cargo_space(carrier)) return;
    sc_interrupt(carrier);
    carrier->waypoints = (waypoints_t){0};
    carrier->sc.order = (sc_order_t){.kind = SC_ORDER_UNLOAD, .at = pixel(at)};
    if (!fixed2_near(where(carrier), at, SC_PIXELS(16))) P_MoveUnitTo(&level, carrier, at);
}
static void unloading(mobj_t *carrier, int frames) {
    sc_order_t *o = &carrier->sc.order;
    fixed2_t at = {SC_PIXELS(o->at.x), SC_PIXELS(o->at.y)};
    if (!fixed2_near(where(carrier), at, SC_PIXELS(16))) {
        if (!P_HasMoveOrder(carrier)) P_MoveUnitTo(&level, carrier, at);
        return;
    }
    P_ClearMove(carrier);
    if ((o->time -= frames) > 0) return;
    for (int i = 0; i < SC_CARGO; i++) {
        mobj_t *mo = P_MobjById(carrier->sc.cargo[i]);
        if (!aboard(mo, carrier)) continue;
        if (drop(mo, carrier)) o->time = SC_UNLOAD_FRAMES;
        return;
    }
    *o = (sc_order_t){0};
}
/* Passengers ride along; a transport's death is theirs, while a Bunker's
 * survivors step out of its rubble. */
static void loaded(mobj_t *mo) {
    mobj_t *carrier = P_MobjById(mo->sc.parent);
    if (carrier && carrier->hp > 0 && !carrier->remove) {
        mo->core.position = fixed3_with_xy(mo->core.position, where(carrier));
        return;
    }
    if (carrier && !is_building(carrier)) { mo->sc.flags &= ~SC_LOADED; P_DamageMobj(mo, NULL, mo->hp); return; }
    isize2_t foot = actor_types[MT_BUNKER - 1].footprint;
    fixed2_t at = where(mo);
    pop_out(mo, (irect_t){(at.x - FIXED_FROM_INT(foot.w) / 2 + 66) >> FIXED_FRAC_BITS,
                          (at.y - FIXED_FROM_INT(foot.h) / 2 + 66) >> FIXED_FRAC_BITS, foot.w, foot.h});
}
/* A transport shot down takes its passengers with it. */
void sc_transport_killed(mobj_t *carrier) {
    if (!sc_unit(carrier) || is_building(carrier)) return;
    for (int i = 0; i < SC_CARGO; i++) {
        mobj_t *mo = P_MobjById(carrier->sc.cargo[i]);
        carrier->sc.cargo[i] = 0;
        if (aboard(mo, carrier)) { mo->sc.flags &= ~SC_LOADED; P_DamageMobj(mo, NULL, mo->hp); }
    }
}

/* ── Nydus Canal ─────────────────────────────────────────────────────── */

/* A finished Nydus Canal places its one exit on creep anywhere it can be
 * built; walkers that enter one end come out of the other (OpenBW
 * order_BuildNydusExit and order_EnterNydusCanal). */
bool sc_nydus_exit(mobj_t *canal, ivec2_t cell) {
    if (!canal || canal->type_id != MT_NYDUS_CANAL || P_MobjById(canal->sc.parent) ||
        !P_CanPlaceBuilding(MT_NYDUS_CANAL, cell, NULL)) return false;
    mobj_t *exit = spawn_for(canal, MT_NYDUS_CANAL, P_BuildingPosition(MT_NYDUS_CANAL, cell));
    if (!exit) return false;
    exit->sc.parent = canal->id;
    canal->sc.parent = exit->id;
    P_SyncBuildingBlocking();
    return true;
}
mobj_t *sc_nydus_other(const mobj_t *canal) {
    mobj_t *other = canal && canal->type_id == MT_NYDUS_CANAL ? P_MobjById(canal->sc.parent) : NULL;
    return other && other->hp > 0 && !other->remove && other->sc.parent == canal->id ? other : NULL;
}
bool sc_enter_nydus(mobj_t *unit, mobj_t *canal) {
    if (!unit || !sc_nydus_other(canal) || unit->owner != canal->owner || !(unit->traits & MF_MOBILE) ||
        (unit->traits & MF_FLY) || (unit->sc.flags & (SC_LOADED | SC_BURROWED)) || sc_unit(unit)->flags & SC_UNIT_BUILDING)
        return false;
    sc_interrupt(unit);
    unit->waypoints = (waypoints_t){0};
    irect_t r = P_MobjCells(canal);
    fixed2_t bay;
    if (!P_ApproachFootprint(unit, (ivec2_t){r.x, r.y}, (isize2_t){r.w, r.h}, &bay) || !P_MoveUnitTo(&level, unit, bay))
        return false;
    unit->sc.order = (sc_order_t){.kind = SC_ORDER_NYDUS, .target = canal->id};
    unit->attack.target = NULL;
    unit->move_only = true;
    return true;
}
static void tunnelling(mobj_t *unit) {
    mobj_t *canal = P_MobjById(unit->sc.order.target), *exit = sc_nydus_other(canal);
    if (!exit) { unit->sc.order = (sc_order_t){0}; return; }
    if (gap(unit, P_MobjCells(canal)) > FIXED_ONE) {
        if (!P_HasMoveOrder(unit)) unit->sc.order = (sc_order_t){0};
        return;
    }
    fixed3_t was = unit->core.position;
    if (!sc_place_near(unit, where(exit))) { unit->core.position = was; return; }
    unit->sc.order = (sc_order_t){0};
    P_ClearMove(unit);
    unit->movement.goal = where(unit);
    unit->movement.order_arrived = true;
}

/* ── Spider Mines ────────────────────────────────────────────────────── */

/* units.dat marks hovering units (byte 0xc1 in its tenth column, OpenBW's
 * "hovering?"): Spider Mines leave them alone. */
static bool hovers(uint16_t type) {
    static const uint8_t rows[] = {2, 7, 13, 19, 41, 64, 68, 76, 88, 94};
    for (unsigned i = 0; i < sizeof(rows); i++) if (type == rows[i] + 1u) return true;
    return false;
}
enum { SC_MINE_REACH = 3 * 32, SC_MINE_CHASE = 576, SC_MINE_BLAST = 30, SC_WEAPON_SPIDER_MINE = 6 };
static void blast(mobj_t *mine) {
    const sc_weapon_t *w = sc_weapon(SC_WEAPON_SPIDER_MINE);
    fixed2_t at = where(mine);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *v = (mobj_t *)th;
        if (!live(th) || v == mine || !sc_unit(v) || (v->traits & (MF_FLY | MF_NOBLOCKMAP)) ||
            (v->sc.flags & SC_LOADED)) continue;
        fixed_t d = fixed2_length(fixed2_sub(where(v), at)) - P_MobjRadius(v);
        int divisor = d <= SC_PIXELS(w->splash[0]) ? 1 : d <= SC_PIXELS(w->splash[1]) ? 2 :
                      d <= SC_PIXELS(w->splash[2]) ? 4 : 0;
        if (!divisor) continue;
        int damage = sc_hit(mine->owner, w, v, w->damage, divisor);
        if (damage > 0) P_DamageMobj(v, mine, damage);
    }
    P_DamageMobj(mine, NULL, mine->hp);
}
/* OpenBW order_SpiderMine: a dug-in mine wakes for the nearest enemy walker
 * in three cells that does not hover, chases it and bursts within 30
 * pixels; it goes back under when the prey gets away. */
static void spider_mine(mobj_t *mine) {
    if (mine->sc.order.kind == SC_ORDER_BURROW || mine->sc.order.kind == SC_ORDER_UNBURROW) return;
    mobj_t *prey = P_MobjById(mine->sc.order.target);
    if (mine->sc.flags & SC_BURROWED) {
        int best = SC_MINE_REACH + 1;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *v = (mobj_t *)th;
            if (!live(th) || !sc_unit(v) || P_IsAlly(mine, v) || v->allegiance == ALLEGIANCE_NEUTRAL ||
                (v->traits & (MF_FLY | MF_NOBLOCKMAP)) || (sc_units[v->type_id - 1].flags & SC_UNIT_BUILDING) ||
                (v->sc.flags & (SC_INVINCIBLE | SC_LOADED)) || v->sc.timers[SC_TIMER_STASIS] || hovers(v->type_id))
                continue;
            int d = sc_unit_distance(mine, v);
            if (d < best) { best = d; prey = v; }
        }
        if (best > SC_MINE_REACH) return;
        sc_unburrow(mine);
        mine->sc.order.target = prey->id;
        return;
    }
    if (!prey || prey->hp <= 0 || prey->remove || sc_unit_distance(mine, prey) > SC_MINE_CHASE) {
        mine->sc.order = (sc_order_t){0};
        sc_burrow(mine);
        return;
    }
    if (sc_unit_distance(mine, prey) <= SC_MINE_BLAST) { blast(mine); return; }
    if (!P_HasMoveOrder(mine) || !fixed2_near(mine->movement.goal, where(prey), FIXED_ONE / 2))
        P_MoveUnitTo(&level, mine, where(prey));
}

/* ── archons ─────────────────────────────────────────────────────────── */

bool sc_merge(mobj_t *a, mobj_t *b) {
    if (!a || !b || a == b || a->type_id != MT_HIGH_TEMPLAR || b->type_id != MT_HIGH_TEMPLAR ||
        a->owner != b->owner || a->hp <= 0 || b->hp <= 0 || ((a->sc.flags | b->sc.flags) & SC_HALLUCINATION) ||
        !sc_has_tech(a->owner, SC_TECH_ARCHON_WARP)) return false;
    int time = sc_units[MT_ARCHON - 1].build_time;
    sc_interrupt(a);sc_interrupt(b);
    a->waypoints=b->waypoints=(waypoints_t){0};
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

/* ── mining ──────────────────────────────────────────────────────────── */

/* A Terran worker at a mineral field, cutting. Its gas run happens inside
 * the Refinery and shows nothing. */
bool sc_mining(const mobj_t *mo) {
    if (mo->type_id != MT_SCV || mo->hp <= 0 || mo->harvest.resource_type != 0 ||
        !P_HarvesterSharingVent(mo) || mo->harvest.target < 0 ||
        mo->harvest.target >= level.resource_vent_count) return false;
    const resourcevent_t *vent = &level.resource_vents[mo->harvest.target];
    return P_VentYields(vent);
}

/* Each swing faces the field and plays the cutter strike; the miners of one
 * field are out of step, staggered by their ids. */
static void mining(mobj_t *mo) {
    if (!sc_mining(mo) || (leveltime + (int)mo->id) % SC_WORK_PERIOD) return;
    const resourcevent_t *vent = &level.resource_vents[mo->harvest.target];
    fixed2_t to = fixed2_sub(vent->attachment, fixed3_xy(mo->core.position));
    if (fixed2_length_squared64(to) > FIXED_LIT_64(0.0001)) mo->core.angle = P_PointToAngle(to.x, to.y);
    if (mo->info->harvest.state_id > 0) P_SetMobjState(mo, mo->info->harvest.state_id);
}

void sc_unit_ticker(mobj_t *mo, int frames) {
    if (mo->type_id == MT_SCV) {
        bool walking = gameinfo && mo->core.state_id > 0 &&
            mo->core.state_id < gameinfo->state_count &&
            gameinfo->states[mo->core.state_id].group == 2;
        if (!walking) mo->sc.glow = (sc_glow_t){0};
        else if (!mo->sc.glow.active) mo->sc.glow = (sc_glow_t){.active = 1};
        else mo->sc.glow.phase ^= frames & 1;
    }
    if (mo->sc.flags & SC_LOADED) { loaded(mo); return; }
    if (mo->type_id == MT_INTERCEPTOR && mo->sc.parent) interceptor(mo);
    else if (mo->type_id == MT_SCARAB && mo->sc.parent &&
             (mo->attack.cooldown_left_ms > 0 || !mo->attack.target || mo->attack.target->hp <= 0)) P_RemoveMobj(mo);
    if (mo->remove || mo->hp <= 0) return;
    mining(mo);
    if (mo->type_id == MT_SPIDER_MINE) { spider_mine(mo); return; }
    sc_order_t *o = &mo->sc.order;
    if (o->kind == SC_ORDER_MERGE) merging(mo, frames);
    else if (o->kind == SC_ORDER_UNLOAD) unloading(mo, frames);
    else if (o->kind == SC_ORDER_NYDUS) tunnelling(mo);
    else if (o->kind == SC_ORDER_BOARD && sc_unit(mo)->space_provided && !(mo->sc.flags & SC_LOADED)) {
        /* A transport fetching a boarder flies to where it now is. */
        mobj_t *unit = P_MobjById(o->target);
        if (!unit || unit->sc.order.kind != SC_ORDER_BOARD || unit->sc.order.target != mo->id) { *o = (sc_order_t){0}; return; }
        if ((o->time -= frames) <= 0) { o->time = 8; P_MoveUnitTo(&level, mo, where(unit)); }
    } else if (o->kind == SC_ORDER_BOARD) {
        mobj_t *carrier = P_MobjById(o->target);
        if (!carrier || !sc_can_board(mo, carrier)) *o = (sc_order_t){0};
        else if (at_door(mo, carrier)) load(mo, carrier);
        else if (!is_building(carrier) && (o->time -= frames) <= 0) {
            /* Chase a transport that moves. */
            o->time = 8;
            if (!fixed2_near(mo->movement.goal, where(carrier), FIXED_ONE / 2)) P_MoveUnitTo(&level, mo, where(carrier));
        } else if (is_building(carrier) && !P_HasMoveOrder(mo)) *o = (sc_order_t){0};
    }
}

bool sc_order(ticorder_t order, mobj_t *const *units, int count, int tech, mobj_t *target, fixed2_t at) {
    bool any = false;
    switch (order) {
    case TC_CANCEL_PRODUCTION:
        for(int i=0;i<count;i++)sc_cancel_production(units[i],tech);
        return true;
    case TC_SET_RALLY:
        for(int i=0;i<count;i++) {
            mobj_t *u=units[i];
            const sc_unit_t *type=sc_unit(u);
            if(!type||!(type->flags&SC_UNIT_BUILDING))continue;
            fixed2_t point=target?fixed3_xy(target->core.position):at;
            if(!L_Contains(&level,fixed_floor_int(point.x),fixed_floor_int(point.y)))continue;
            u->sc.rally=(sc_rally_t){.at=point,.target=target?target->id:0,.set=true};
        }
        return true;
    case TC_DEPLOY:
        for (int i = 0; i < count; i++) if(sc_cast(units[i], SC_TECH_SIEGE_MODE, NULL, at))
            units[i]->sc.queue=(sc_order_queue_t){0};
        return true;
    case TC_SPELL:
        if (tech == SC_TECH_NYDUS_EXIT) {
            isize2_t foot = actor_types[MT_NYDUS_CANAL - 1].footprint;
            ivec2_t c = fixed2_cell(at);
            for (int i = 0; i < count; i++)
                if (sc_nydus_exit(units[i], (ivec2_t){c.x - foot.w / 2, c.y - foot.h / 2})) break;
            return true;
        }
        if (tech == SC_TECH_ARCHON_WARP) {
            mobj_t *first = NULL;
            for (int i = 0; i < count; i++) {
                if (units[i]->type_id != MT_HIGH_TEMPLAR || units[i]->sc.order.kind == SC_ORDER_MERGE) continue;
                if (!first) first = units[i];
                else if (sc_merge(first, units[i])) {
                    first->sc.queue=(sc_order_queue_t){0};units[i]->sc.queue=(sc_order_queue_t){0};first = NULL;
                }
            }
            return true;
        }
        /* One caster serves an aimed spell; toggles switch every caster. */
        for (int i = 0; i < count && (!any || !sc_tech_aimed(tech)); i++) {
            bool accepted=sc_cast(units[i], tech, target, at);
            if(accepted && tech!=SC_TECH_STIM_PACKS && tech!=SC_TECH_CLOAKING_FIELD && tech!=SC_TECH_PERSONNEL_CLOAKING)
                units[i]->sc.queue=(sc_order_queue_t){.active=TC_SPELL,.owner=units[i]->owner};
            any |= accepted;
        }
        return true;
    case TC_BOARD:
        /* Walkers board the target; a selected transport picks it up. */
        for (int i = 0; i < count; i++) {
            if (sc_unit(units[i])->space_provided && target && sc_can_board(target, units[i])) sc_board(target, units[i]);
            else sc_board(units[i], target);
        }
        return true;
    case TC_UNLOAD:
        for (int i = 0; i < count; i++) if (sc_unit(units[i])->space_provided) {
            if(!target) {
                bool here=!at.x&&!at.y;
                sc_unload_at(units[i],here?where(units[i]):at);
            } else if(aboard(target,units[i])) {
                if(is_building(units[i])) {
                    for(int j=0;j<SC_CARGO;j++)if(units[i]->sc.cargo[j]==target->id)units[i]->sc.cargo[j]=0;
                    pop_out(target,P_MobjCells(units[i]));
                } else drop(target,units[i]);
            }
        }
        return true;
    case TC_ORDER:
        if (!target || target->owner != units[0]->owner) return false;
        /* A right click on an own Bunker or transport sends in whoever
         * fits, on a Nydus Canal through it; a transport given an own
         * walker picks it up. */
        if (sc_nydus_other(target)) {
            for (int i = 0; i < count; i++) any |= sc_enter_nydus(units[i], target);
            return any;
        }
        if (sc_unit(target)->space_provided && sc_carries(target)) {
            for (int i = 0; i < count; i++) any |= sc_board(units[i], target);
            return any;
        }
        for (int i = 0; i < count; i++)
            if (sc_unit(units[i])->space_provided && sc_can_board(target, units[i])) { any = sc_board(target, units[i]); break; }
        return any;
    default:
        return false;
    }
}
