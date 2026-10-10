#include "sc_local.h"
/* What a StarCraft computer player does with its units' abilities, once per
 * think, as Brood War's AI does: casters with a wave spend their energy
 * where it pays, tanks siege when enemies come within reach and unsiege to
 * move, Marines man the Bunkers, Carriers and Reavers keep their hangars
 * full, and research starts for the abilities its units have. */
#define SC_CLUMP (FIXED_ONE * 3 / 2) /* Cells around a unit that count as its clump. */
#define SC_SIEGE_REACH FIXED_FROM_INT(12)
/* Squared distance of n cells, as dist2 returns it. */
#define SQ(n) fixed_sq64(FIXED_FROM_INT(n))

static fixed2_t where(const mobj_t *mo) { return fixed3_xy(mo->core.position); }
static bool alive(const mobj_t *mo) { return mo && !mo->remove && mo->hp > 0 && sc_unit(mo); }
static int64_t dist2(const mobj_t *a, const mobj_t *b) { return fixed2_distance_squared64(where(a), where(b)); }
static bool building(const mobj_t *mo) { return (sc_unit(mo)->flags & SC_UNIT_BUILDING) != 0; }
/* An enemy fighting unit or building of another player that u can see. */
static bool foe(const mobj_t *u, const mobj_t *e) {
    return alive(e) && e->owner < 8 && !(e->traits & (MF_NOBLOCKMAP | MF_MISSILE)) && !P_IsAlly(u, e) &&
        P_VisibleTo(u, e);
}
static int clump(mobj_t *const *units, int count, const mobj_t *u, const mobj_t *at, bool enemies) {
    int n = 0;
    for (int i = 0; i < count; i++) {
        const mobj_t *v = units[i];
        if (!alive(v) || (v->traits & MF_NOBLOCKMAP) || building(v) || dist2(v, at) > fixed_sq64(SC_CLUMP)) continue;
        if (enemies ? foe(u, v) : P_IsAlly(u, v)) ++n;
    }
    return n;
}
static fixed_t reach_of(int tech) {
    /* Spell reach plus a little: casters walk the rest. */
    switch (tech) {
    case SC_TECH_DEFENSIVE_MATRIX: return FIXED_FROM_INT(10);
    case SC_TECH_PSIONIC_STORM: case SC_TECH_IRRADIATE: case SC_TECH_ENSNARE: case SC_TECH_DARK_SWARM:
    case SC_TECH_PLAGUE: return FIXED_FROM_INT(9);
    case SC_TECH_YAMATO_GUN: return FIXED_FROM_INT(10);
    default: return FIXED_FROM_INT(8);
    }
}
static bool ready(const mobj_t *u, int tech) {
    return sc_has_tech(u->owner, tech) && sc_energy(u) >= sc_techs[tech].energy;
}

/* The enemy in reach whose spot catches the most enemies and none of ours. */
static mobj_t *best_clump(mobj_t *const *units, int count, const mobj_t *u, fixed_t reach, int least) {
    mobj_t *best = NULL;
    int most = least - 1;
    for (int i = 0; i < count; i++) {
        mobj_t *e = units[i];
        if (!foe(u, e) || building(e) || dist2(u, e) > fixed_sq64(reach) || clump(units, count, u, e, false)) continue;
        int n = clump(units, count, u, e, true);
        if (n > most) { most = n; best = e; }
    }
    return best;
}
/* The enemy in reach worth the most hit points that the spell may hit. */
static mobj_t *best_target(mobj_t *const *units, int count, const mobj_t *u, fixed_t reach, uint32_t need, int least) {
    mobj_t *best = NULL;
    for (int i = 0; i < count; i++) {
        mobj_t *e = units[i];
        if (!foe(u, e) || dist2(u, e) > fixed_sq64(reach) || e->max_hp < least ||
            (need && !(sc_unit(e)->flags & need))) continue;
        if (!best || e->max_hp > best->max_hp) best = e;
    }
    return best;
}

static bool storm(mobj_t *const *units, int count, mobj_t *u) {
    if (!ready(u, SC_TECH_PSIONIC_STORM)) return false;
    mobj_t *e = best_clump(units, count, u, reach_of(SC_TECH_PSIONIC_STORM), 3);
    return e && sc_cast(u, SC_TECH_PSIONIC_STORM, NULL, where(e));
}
static bool vessel(mobj_t *const *units, int count, mobj_t *u) {
    /* A Defensive Matrix on a frontliner under fire that has lost a third. */
    if (ready(u, SC_TECH_DEFENSIVE_MATRIX))
        for (int i = 0; i < count; i++) {
            mobj_t *f = units[i];
            if (alive(f) && f->owner == u->owner && f != u && !building(f) && (f->traits & MF_ATTACK) &&
                !f->sc.matrix && (f->sc.flags & SC_HIT) && f->hp * 3 < f->max_hp * 2 &&
                dist2(u, f) <= SQ(10) && sc_cast(u, SC_TECH_DEFENSIVE_MATRIX, f, where(f))) return true;
        }
    /* EMP where Protoss shields are thick. */
    if (ready(u, SC_TECH_EMP))
        for (int i = 0; i < count; i++) {
            mobj_t *e = units[i];
            if (!foe(u, e) || dist2(u, e) > SQ(8) || clump(units, count, u, e, false)) continue;
            int shields = 0;
            for (int j = 0; j < count; j++)
                if (foe(u, units[j]) && dist2(units[j], e) <= SQ(2)) shields += sc_shields(units[j]);
            if (shields >= 150 && sc_cast(u, SC_TECH_EMP, NULL, where(e))) return true;
        }
    /* Irradiate the biggest organic body. */
    if (ready(u, SC_TECH_IRRADIATE)) {
        mobj_t *e = best_target(units, count, u, FIXED_FROM_INT(9), SC_UNIT_ORGANIC, 80);
        if (e && !building(e) && !e->sc.timers[SC_TIMER_IRRADIATE] && sc_cast(u, SC_TECH_IRRADIATE, e, where(e)))
            return true;
    }
    return false;
}
static bool queen(mobj_t *const *units, int count, mobj_t *u) {
    if (ready(u, SC_TECH_SPAWN_BROODLING)) {
        mobj_t *e = best_target(units, count, u, FIXED_FROM_INT(8), 0, 100);
        if (e && sc_cast(u, SC_TECH_SPAWN_BROODLING, e, where(e))) return true;
    }
    if (ready(u, SC_TECH_ENSNARE)) {
        mobj_t *e = best_clump(units, count, u, FIXED_FROM_INT(9), 3);
        if (e && sc_cast(u, SC_TECH_ENSNARE, NULL, where(e))) return true;
    }
    return false;
}
static bool defiler(mobj_t *const *units, int count, mobj_t *u) {
    /* Dark Swarm over our melee fighters that ranged enemies are shooting at. */
    if (ready(u, SC_TECH_DARK_SWARM))
        for (int i = 0; i < count; i++) {
            mobj_t *f = units[i];
            if (!alive(f) || f->owner != u->owner || building(f) || (f->traits & MF_FLY) || !(f->traits & MF_ATTACK) ||
                f->info->attack.range > FIXED_ONE || dist2(u, f) > SQ(9)) continue;
            bool shot = false, covered = false;
            for (int j = 0; j < count && !shot; j++) {
                const mobj_t *e = units[j];
                shot = foe(u, e) && e->info->attack.range > FIXED_FROM_INT(2) && e->attack.target == f;
            }
            for (thinker_t *th = thinkercap.next; th != &thinkercap && !covered; th = th->next) {
                const mobj_t *cloud = (const mobj_t *)th;
                covered = th->function == P_MobjThinker && !cloud->remove && cloud->type_id == MT_DARK_SWARM &&
                    dist2(cloud, f) <= SQ(2);
            }
            if (shot && !covered && sc_cast(u, SC_TECH_DARK_SWARM, NULL, where(f))) return true;
        }
    if (ready(u, SC_TECH_PLAGUE)) {
        mobj_t *e = best_clump(units, count, u, FIXED_FROM_INT(9), 4);
        if (e && sc_cast(u, SC_TECH_PLAGUE, NULL, where(e))) return true;
    }
    /* Short of a Dark Swarm: eat a zergling nearby. */
    if (sc_has_tech(u->owner, SC_TECH_CONSUME) && sc_energy(u) < sc_techs[SC_TECH_DARK_SWARM].energy)
        for (int i = 0; i < count; i++)
            if (alive(units[i]) && units[i]->owner == u->owner && units[i]->type_id == MT_ZERGLING &&
                dist2(u, units[i]) <= SQ(6) && sc_cast(u, SC_TECH_CONSUME, units[i], where(units[i]))) return true;
    return false;
}
static bool sweep(mobj_t *const *units, int count, mobj_t *u) {
    if (!ready(u, SC_TECH_SCANNER_SWEEP)) return false;
    for (int i = 0; i < count; i++) {
        mobj_t *e = units[i];
        if (!alive(e) || e->owner >= 8 || P_IsAlly(u, e) || !(e->traits & MF_CLOAKED) || P_VisibleTo(u, e)) continue;
        for (int j = 0; j < count; j++)
            if (alive(units[j]) && units[j]->owner == u->owner && dist2(units[j], e) <= SQ(8))
                return sc_cast(u, SC_TECH_SCANNER_SWEEP, NULL, where(e));
    }
    return false;
}
static bool cast(mobj_t *const *units, int count, mobj_t *u) {
    if (u->sc.order.kind) return false;
    switch (u->type_id) {
    case MT_HIGH_TEMPLAR: return storm(units, count, u);
    case MT_SCIENCE_VESSEL: return vessel(units, count, u);
    case MT_QUEEN: return queen(units, count, u);
    case MT_DEFILER: return defiler(units, count, u);
    case MT_COMSAT_STATION: return sweep(units, count, u);
    case MT_GHOST:
        if (ready(u, SC_TECH_LOCKDOWN)) {
            mobj_t *e = best_target(units, count, u, FIXED_FROM_INT(8), SC_UNIT_MECHANICAL, 100);
            return e && !building(e) && !e->sc.timers[SC_TIMER_LOCKDOWN] && sc_cast(u, SC_TECH_LOCKDOWN, e, where(e));
        }
        return false;
    case MT_BATTLECRUISER:
        if (ready(u, SC_TECH_YAMATO_GUN)) {
            mobj_t *e = best_target(units, count, u, FIXED_FROM_INT(10), 0, 200);
            return e && sc_cast(u, SC_TECH_YAMATO_GUN, e, where(e));
        }
        return false;
    default: return false;
    }
}

/* Siege when an enemy on the ground comes within the cannon's reach, but
 * not on top of the tank; unsiege once nothing is left to shell. */
static void siege(mobj_t *const *units, int count, mobj_t *u) {
    if (!sc_has_tech(u->owner, SC_TECH_SIEGE_MODE) || u->core.state_id == SC_SIEGE_STATE ||
        u->core.state_id == SC_UNSIEGE_STATE) return;
    bool sieged = u->type_id == MT_SIEGE_MODE, near = false, close = false;
    fixed_t reach = SC_SIEGE_REACH + (sieged ? FIXED_ONE : 0);
    for (int i = 0; i < count; i++) {
        const mobj_t *e = units[i];
        if (!foe(u, e) || (e->traits & MF_FLY)) continue;
        int64_t d = dist2(u, e);
        near |= d <= fixed_sq64(reach);
        close |= d <= SQ(3);
    }
    if (sieged ? !near : near && !close) sc_cast(u, SC_TECH_SIEGE_MODE, NULL, where(u));
}

/* Idle Marines go into a Bunker of ours that has room. */
static void man_bunker(mobj_t *const *units, int count, mobj_t *u) {
    if (P_HasMoveOrder(u) || u->sc.order.kind || (u->attack.target && u->attack.target->hp > 0)) return;
    for (int i = 0; i < count; i++)
        if (units[i]->type_id == MT_BUNKER && alive(units[i]) && dist2(u, units[i]) <= SQ(20) &&
            sc_board(u, units[i])) return;
}

static void refill(mobj_t *u) {
    if (u->production || sc_hangar_count(u) >= sc_hangar_capacity(u)) return;
    G_QueueProduct(u, G_ModelProductByUIId(NULL, sc_hangar_type(u->type_id)));
}

/* An idle research building starts an ability some unit of ours casts. */
static void research(mobj_t *const *units, int count, mobj_t *u) {
    if (u->production || !building(u)) return;
    for (int tech = 0; tech < SC_TECHS; tech++) {
        const StaticProductDefinition *p = G_ModelProductByUIId(NULL, SC_TECH_UI + tech);
        if (!p || p->makers[0] != u->type_id || sc_has_tech(u->owner, tech)) continue;
        bool wanted = false;
        for (int j = 0; j < count && !wanted; j++) {
            int techs[8], k = alive(units[j]) && units[j]->owner == u->owner ? sc_unit_techs(units[j]->type_id, techs, 8) : 0;
            while (k-- > 0 && !wanted) wanted = techs[k] == tech;
        }
        if (wanted && G_QueueProduct(u, p)) return;
    }
}

/* Idle units of ours standing where a building's add-on is still to go
 * step off that place, below it. */
static void clear_addon_place(level_t *map, mobj_t *const *units, int count, const mobj_t *u) {
    uint16_t addon;
    irect_t at = P_MobjCells(u), place;
    if (sc_addon_of(u) || !sc_addon_place(u->type_id, (ivec2_t){at.x, at.y}, &addon, &place)) return;
    for (int i = 0; i < count; i++) {
        mobj_t *v = units[i];
        ivec2_t cell = fixed2_cell(where(v));
        if (!alive(v) || v->owner != u->owner || building(v) || (v->traits & MF_FLY) || P_HasMoveOrder(v) ||
            v->harvest.phase != HARVEST_PHASE_NONE || !irect_contains(place, cell)) continue;
        P_MoveUnitTo(map, v, fixed2_cell_center((ivec2_t){cell.x, place.y + place.h + 1}));
    }
}

void sc_ai_tactics(level_t *map, int owner, mobj_t *const *units, int count) {
    for (int i = 0; i < count; i++) {
        mobj_t *u = units[i];
        if (!alive(u) || u->owner != owner || (u->sc.flags & (SC_HALLUCINATION | SC_LOADED))) continue;
        if (cast(units, count, u)) continue;
        switch (u->type_id) {
        case MT_CARRIER: case MT_REAVER: refill(u); break;
        case MT_SIEGE_TANK: case MT_SIEGE_MODE: siege(units, count, u); break;
        case MT_MARINE: man_bunker(units, count, u); break;
        default:
            research(units, count, u);
            if (building(u)) clear_addon_place(map, units, count, u);
            break;
        }
    }
}
