#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <math.h>
#include <stdlib.h>

/* The computer player's battle orders the shared AI cannot give: a wave
 * split by what can reach its goal (Wargus sea_attack.lua and
 * air_attack.lua forces), transports ferrying soldiers across water, and
 * spells cast in battle as Wargus spells.lua's ai-cast conditions. */

enum { FERRY_NONE, FERRY_GATHER, FERRY_SAIL };
enum {
    FERRY_REACH = 8,         /* Cells around a waiting transport it boards from. */
    FERRY_SEARCH = 14,       /* Cells searched for a shore to load or land at. */
    FERRY_SHORE = 150,       /* Thinks (20 s) a transport takes to reach the shore. */
    FERRY_PATIENCE = 600,    /* Thinks a transport waits in all for its first soldier. */
    COMBAT_REACH = 8,        /* An enemy fighter this close means battle. */
    RALLY_DISTANCE = 10,     /* Cells from the hall to where idle soldiers wait. */
    RALLY_SPREAD = 4,
    RALLY_HOME = 24,         /* Idle soldiers this near the hall are in town. */
};

static bool alive(const mobj_t *unit) {
    return unit && !unit->remove && unit->hp > 0 && unit->type_id >= 1 && unit->type_id <= W2_TYPE_COUNT;
}

static const w2_stats_t *stats(const mobj_t *unit) { return &mobjinfo[unit->type_id].w2; }

static ivec2_t cell_of(const mobj_t *unit) { return fixed2_cell(fixed3_xy(unit->core.position)); }

static bool enemy_of(const mobj_t *unit, const mobj_t *other) {
    return alive(other) && other->owner < 8 && other->allegiance != ALLEGIANCE_NEUTRAL &&
        !(other->traits & MF_NOBLOCKMAP) && !P_IsAlly(unit, other);
}

static bool land_unit(const mobj_t *unit) {
    return (unit->traits & MF_MOBILE) && stats(unit)->domain == W2_DOMAIN_LAND;
}

/* A cell `unit` can stand on within `radius` of `goal`. */
static bool reach(const mobj_t *unit, ivec2_t goal, int radius, ivec2_t *out) {
    ivec2_t spot;
    return P_NavNearestReachable(&level, P_MobjMoveClass(unit), cell_of(unit), goal, radius, out ? out : &spot);
}

/* Water by open land: the cell nearest `near` that `ship` can sail to and
 * that has a dry neighbour from which soldiers can walk to `inland`. */
static bool shore(const mobj_t *ship, ivec2_t near, ivec2_t inland, ivec2_t *out) {
    int best = INT32_MAX;
    for (int dy = -FERRY_SEARCH; dy <= FERRY_SEARCH; ++dy)
        for (int dx = -FERRY_SEARCH; dx <= FERRY_SEARCH; ++dx) {
            ivec2_t c = {near.x + dx, near.y + dy}, spot;
            int d = dx * dx + dy * dy;
            if (d >= best || !L_Contains(&level, c.x, c.y) ||
                !P_NavReachable(&level, P_MobjMoveClass(ship), cell_of(ship), c)) continue;
            bool dry = false;
            for (int k = 0; k < 9 && !dry; ++k) {
                ivec2_t n = {c.x + k % 3 - 1, c.y + k / 3 - 1};
                dry = L_Contains(&level, n.x, n.y) && level.cell_terrain[L_Index(&level, n.x, n.y)] == 0 &&
                      !level.cell_solid[L_Index(&level, n.x, n.y)] &&
                      P_NavNearestReachable(&level, 1, n, inland, 3, &spot);
            }
            if (dry) { best = d; *out = c; }
        }
    return best != INT32_MAX;
}

static bool fighting(const mobj_t *unit) {
    return unit->attack.target && alive(unit->attack.target) && !P_IsAlly(unit, unit->attack.target);
}

/* ── waves ────────────────────────────────────────────────────────────── */

/* A ship shoots what it can reach from the water: the enemy nearest the
 * wave's goal that has open water within the ship's range. */
static void send_ship(mobj_t *ship, const mobj_t *goal) {
    int range = (int)W2_AttackRange(ship);
    fixed2_t aim = fixed3_xy(goal->core.position);
    mobj_t *best = NULL;
    ivec2_t best_spot = {0};
    int64_t best_d = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *enemy = (mobj_t *)th;
        ivec2_t spot;
        if (th->function != P_MobjThinker || !enemy_of(ship, enemy) || (enemy->traits & MF_FLY)) continue;
        int64_t d = fixed2_distance_squared64(aim, fixed3_xy(enemy->core.position));
        if ((best && d >= best_d) || !reach(ship, cell_of(enemy), range, &spot)) continue;
        best = enemy; best_d = d; best_spot = spot;
    }
    /* Nothing on the shore in reach: hold the water nearest the goal,
     * where the landing will come. */
    if (!best && reach(ship, cell_of(goal), FERRY_REACH * 2, &best_spot)) {
        P_MoveUnitTo(&level, ship, fixed2_cell_center(best_spot));
        return;
    }
    if (!best || !P_MoveUnitTo(&level, ship, fixed2_cell_center(best_spot))) return;
    ship->attack.target = best;
}

/* Soldiers that cannot walk to the goal go to the shore by the nearest
 * free transport, which sails there to wait for them. */
static void call_transport(mobj_t *const *troops, int count, ivec2_t goal) {
    if (count <= 0) return;
    int64_t sum_x = 0, sum_y = 0;
    for (int i = 0; i < count; ++i) {
        sum_x += troops[i]->core.position.x;
        sum_y += troops[i]->core.position.y;
    }
    fixed2_t centre = {(fixed_t)(sum_x / count), (fixed_t)(sum_y / count)};
    mobj_t *ship = NULL;
    int64_t best = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        if (th->function != P_MobjThinker || !alive(unit) || unit->owner != troops[0]->owner ||
            !stats(unit)->transport_capacity || unit->w2.ferry.phase || unit->w2.unloading) continue;
        int64_t d = fixed2_distance_squared64(centre, fixed3_xy(unit->core.position));
        if (!ship || d < best) { ship = unit; best = d; }
    }
    ivec2_t pickup, home = fixed2_cell(centre);
    if (!ship || !shore(ship, home, cell_of(troops[0]), &pickup)) return;
    P_MoveUnitTo(&level, ship, fixed2_cell_center(pickup));
    ship->w2.ferry.phase = FERRY_GATHER;
    ship->w2.ferry.wait = 0;
    ship->w2.ferry.to = goal;
    P_MoveUnitsAt(&level, troops, count, fixed2_cell_center(pickup));
}

/* Flyers and soldiers that can walk go straight at the goal, ships to the
 * coast nearest it, the rest wait for a transport. Heroes stay home. */
bool w2_ai_dispatch(level_t *map, int owner, mobj_t *const *wave, int count, mobj_t *goal) {
    (void)owner;
    mobj_t *march[count > 0 ? count : 1], *ferry[count > 0 ? count : 1];
    int marching = 0, ferried = 0;
    ivec2_t at = cell_of(goal);
    for (int i = 0; i < count; ++i) {
        mobj_t *unit = wave[i];
        if (stats(unit)->attributes & W2_HERO) continue;
        if (stats(unit)->domain == W2_DOMAIN_SEA) send_ship(unit, goal);
        else if ((unit->traits & MF_FLY) || reach(unit, at, 3, NULL)) march[marching++] = unit;
        else ferry[ferried++] = unit;
    }
    for (int i = 0; i < marching; ++i) march[i]->attack.target = goal;
    P_MoveUnitsAt(map, march, marching, fixed3_xy(goal->core.position));
    for (int i = 0; i < marching; ++i) march[i]->attack.target = goal;
    call_transport(ferry, ferried, at);
    return true;
}

/* ── ferries ──────────────────────────────────────────────────────────── */

static int aboard(const mobj_t *ship, bool *boarding) {
    int count = 0;
    *boarding = false;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *unit = (const mobj_t *)th;
        if (th->function != P_MobjThinker || !alive(unit) || unit->w2.carrier != ship->id) continue;
        if (unit->w2.boarded) ++count;
        else *boarding = true;
    }
    return count;
}

/* The enemy nearest the landing, for the troops to fight. */
static mobj_t *nearest_enemy(const mobj_t *unit, ivec2_t to) {
    mobj_t *best = NULL;
    int64_t best_d = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *enemy = (mobj_t *)th;
        if (th->function != P_MobjThinker || !enemy_of(unit, enemy)) continue;
        int64_t d = fixed2_distance_squared64(fixed2_cell_center(to), fixed3_xy(enemy->core.position));
        if (!best || d < best_d) { best = enemy; best_d = d; }
    }
    return best;
}

/* Gather: idle soldiers by the shore who cannot walk to the goal board;
 * sail once full, or once nobody else is coming. Sail: unload on the
 * coast nearest the goal and send the landed troops at it. */
static void run_ferry(mobj_t *ship, mobj_t *const *units, int count) {
    bool boarding;
    int loaded = aboard(ship, &boarding);
    int room = stats(ship)->transport_capacity - loaded;
    if (ship->w2.ferry.phase == FERRY_SAIL) {
        if (ship->w2.unloading || loaded) return;
        mobj_t *landed[count > 0 ? count : 1];
        int n = 0;
        for (int i = 0; i < count; ++i)
            if (alive(units[i]) && units[i]->owner == ship->owner && land_unit(units[i]) &&
                (units[i]->traits & MF_ATTACK) && !P_HasMoveOrder(units[i]) && W2_Distance(units[i], ship) <= 2)
                landed[n++] = units[i];
        mobj_t *target = n ? nearest_enemy(landed[0], ship->w2.ferry.to) : NULL;
        if (target) {
            for (int i = 0; i < n; ++i) landed[i]->attack.target = target;
            P_MoveUnitsAt(&level, landed, n, fixed3_xy(target->core.position));
            for (int i = 0; i < n; ++i) landed[i]->attack.target = target;
        }
        ship->w2.ferry.phase = FERRY_NONE;
        return;
    }
    /* A transport jammed on its way to the shore boards from where it is. */
    ++ship->w2.ferry.wait;
    if (P_HasMoveOrder(ship)) {
        if (ship->w2.ferry.wait < FERRY_SHORE) return;
        P_ClearMove(ship);
    }
    for (int i = 0; i < count && room > 0; ++i) {
        mobj_t *unit = units[i];
        if (!alive(unit) || unit->owner != ship->owner || !land_unit(unit) || !(unit->traits & MF_ATTACK) ||
            (unit->traits & MF_NOAUTOTARGET) || unit->w2.carrier || unit->w2.build_phase ||
            P_HasMoveOrder(unit) || fighting(unit) || W2_Distance(unit, ship) > FERRY_REACH ||
            (stats(unit)->attributes & W2_HERO) || reach(unit, ship->w2.ferry.to, 3, NULL)) continue;
        if (W2_BoardOrder(unit, ship)) { boarding = true; --room; }
    }
    bool late = ship->w2.ferry.wait > FERRY_PATIENCE;
    if (boarding && room > 0 && !late) return;
    /* Out of patience: sail with whoever is aboard and let the rest go. */
    for (int i = 0; i < count && late; ++i)
        if (units[i]->w2.carrier == ship->id && !units[i]->w2.boarded) {
            units[i]->w2.carrier = 0;
            P_ClearMove(units[i]);
        }
    if (!loaded) {
        if (late) ship->w2.ferry.phase = FERRY_NONE;
        return;
    }
    ivec2_t landing;
    if (!shore(ship, ship->w2.ferry.to, ship->w2.ferry.to, &landing) ||
        !W2_UnloadOrder(ship, fixed2_cell_center(landing))) return;
    ship->w2.ferry.phase = FERRY_SAIL;
}

/* ── spells ───────────────────────────────────────────────────────────── */

/* Enemies around `at` against own or allied units there: Wargus' blizzard
 * position-autocast weighs the same, so area spells spare the caster's
 * side. */
static int clump(const mobj_t *caster, mobj_t *const *units, int count, ivec2_t at) {
    int score = 0;
    for (int i = 0; i < count; ++i) {
        const mobj_t *unit = units[i];
        if (!alive(unit) || (unit->traits & MF_NOBLOCKMAP)) continue;
        ivec2_t cell = cell_of(unit);
        if (abs(cell.x - at.x) > 2 || abs(cell.y - at.y) > 2) continue;
        if (P_IsAlly(caster, unit)) return 0;
        if (enemy_of(caster, unit) && !(stats(unit)->flags & W2_STRUCTURE)) ++score;
    }
    return score;
}

static bool flame_shielded(const mobj_t *unit) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *fx = (const mobj_t *)th;
        if (th->function == P_MobjThinker && !fx->remove && fx->type_id == MT_W2_EFFECT &&
            fx->w2.cast.spell == W2_SPELL_FLAME_SHIELD && fx->w2.fx.subject == unit->id) return true;
    }
    return false;
}

/* How much `spell` is worth cast on `target` (or at its cell), 0 when it
 * is not: wounded organic allies to heal, attacking allies to lust or
 * haste, melee frontliners to shield, the strongest organic enemy to
 * polymorph, the fastest threat to slow, undead to exorcise, buildings
 * to whirl, clumps of enemies away from friends to freeze or decay. */
static int worth(const mobj_t *caster, int spell, const mobj_t *target, mobj_t *const *units, int count) {
    const w2_stats_t *s = stats(target);
    bool ally = P_IsAlly(caster, target), enemy = enemy_of(caster, target);
    bool structure = (s->flags & W2_STRUCTURE) != 0, organic = (s->attributes & W2_ORGANIC) != 0;
    bool warrior = (target->traits & MF_ATTACK) && !(target->traits & MF_NOAUTOTARGET) && !structure;
    switch (spell) {
    case W2_SPELL_HEAL:
        return ally && organic && !structure && target->hp * 10 <= target->max_hp * 9 ?
            target->max_hp - target->hp : 0;
    case W2_SPELL_EXORCISM: return enemy && (s->attributes & W2_UNDEAD) ? target->hp : 0;
    case W2_SPELL_BLOODLUST:
        return ally && organic && warrior && fighting(target) && !target->w2.buffs[W2_BUFF_BLOODLUST] ?
            target->info->attack.damage : 0;
    case W2_SPELL_HASTE:
        return ally && warrior && fighting(target) && !target->w2.buffs[W2_BUFF_HASTE] ?
            target->info->attack.damage : 0;
    case W2_SPELL_FLAME_SHIELD:
        return ally && warrior && fighting(target) && !(s->flags & W2_AIR) && s->attack_range <= 1 &&
            !flame_shielded(target) ? target->hp : 0;
    case W2_SPELL_POLYMORPH:
        return enemy && organic && warrior && !(s->attributes & W2_HERO) &&
            target->hp * 2 >= target->max_hp ? target->hp : 0;
    case W2_SPELL_SLOW:
        return enemy && warrior && !target->w2.buffs[W2_BUFF_SLOW] ? s->speed : 0;
    case W2_SPELL_WHIRLWIND: return enemy && structure ? s->priority + 1 : 0;
    case W2_SPELL_BLIZZARD: case W2_SPELL_DECAY: {
        int n = enemy ? clump(caster, units, count, cell_of(target)) : 0;
        return n >= 3 ? n : 0;
    }
    default: return 0;
    }
}

static bool cast_best(mobj_t *caster, mobj_t *const *units, int count) {
    static const w2_spell_id_t order[] = {
        W2_SPELL_HEAL, W2_SPELL_EXORCISM, W2_SPELL_BLIZZARD, W2_SPELL_DECAY, W2_SPELL_POLYMORPH,
        W2_SPELL_BLOODLUST, W2_SPELL_HASTE, W2_SPELL_SLOW, W2_SPELL_FLAME_SHIELD, W2_SPELL_WHIRLWIND,
    };
    bool battle = false;
    for (int i = 0; i < count && !battle; ++i)
        battle = enemy_of(caster, units[i]) && (units[i]->traits & MF_ATTACK) &&
            W2_Distance(caster, units[i]) <= COMBAT_REACH && P_VisibleTo(caster, units[i]);
    for (size_t k = 0; k < sizeof(order) / sizeof(*order); ++k) {
        w2_spell_id_t spell = order[k];
        if (!W2_CanCast(caster, spell) || caster->w2.mana < w2_spells[spell].mana) continue;
        /* Healing waits for no battle; the rest are battle spells. */
        if (!battle && spell != W2_SPELL_HEAL) continue;
        mobj_t *best = NULL;
        int best_worth = 0, range = w2_spells[spell].range;
        for (int i = 0; i < count; ++i) {
            mobj_t *target = units[i];
            if (!alive(target) || (target->traits & MF_NOBLOCKMAP) || W2_Distance(caster, target) > range ||
                !P_VisibleTo(caster, target)) continue;
            int value = worth(caster, spell, target, units, count);
            if (value > best_worth) { best_worth = value; best = target; }
        }
        if (!best) continue;
        bool at_unit = w2_spells[spell].unit_target;
        if (W2_CastOrder(caster, spell, at_unit ? best : NULL, best->core.position)) return true;
    }
    return false;
}

/* ── rally ────────────────────────────────────────────────────────────── */

/* Idle soldiers gather on the most open ground about RALLY_DISTANCE from
 * the hall, off the miners' way, as a Wargus AI force waits at home:
 * standing among the buildings they wall the workers' lanes off. */
static bool rally_spot(int owner, const mobj_t *hall, const mobj_t *walker, ivec2_t *out) {
    ivec2_t home = cell_of(hall);
    int best = 0;
    /* Sixteen directions, RALLY_DISTANCE cells out: round(cos, sin * 10). */
    static const ivec2_t ring[16] = {
        {10, 0}, {9, 4}, {7, 7}, {4, 9}, {0, 10}, {-4, 9}, {-7, 7}, {-9, 4},
        {-10, 0}, {-9, -4}, {-7, -7}, {-4, -9}, {0, -10}, {4, -9}, {7, -7}, {9, -4},
    };
    for (int k = 0; k < 16; ++k) {
        ivec2_t want = {home.x + ring[k].x, home.y + ring[k].y}, spot;
        if (!L_Contains(&level, want.x, want.y) || !reach(walker, want, 2, &spot) ||
            w2_blocks_mining(owner, (ivec2_t){spot.x - 1, spot.y - 1}, (isize2_t){3, 3})) continue;
        int open = 0;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                ivec2_t c = {spot.x + dx, spot.y + dy};
                open += L_Contains(&level, c.x, c.y) && !level.cell_solid[L_Index(&level, c.x, c.y)] &&
                        level.cell_terrain[L_Index(&level, c.x, c.y)] == 0;
            }
        if (open > best) { best = open; *out = spot; }
    }
    return best > 0;
}

static void rally(level_t *map, int owner, mobj_t *const *units, int count) {
    const mobj_t *hall = NULL, *walker = NULL;
    for (int i = 0; i < count && (!hall || !walker); ++i) {
        const mobj_t *unit = units[i];
        if (!alive(unit) || unit->owner != owner) continue;
        if (!hall && (stats(unit)->flags & W2_HALL)) hall = unit;
        if (!walker && land_unit(unit) && !unit->w2.boarded) walker = unit;
    }
    ivec2_t spot;
    if (!hall || !walker || !rally_spot(owner, hall, walker, &spot)) return;
    /* Soldiers walking to a transport are not idle in town. */
    for (int i = 0; i < count; ++i)
        if (alive(units[i]) && units[i]->owner == owner && units[i]->w2.ferry.phase == FERRY_GATHER) return;
    fixed2_t home = fixed3_xy(hall->core.position);
    mobj_t *idle[count > 0 ? count : 1];
    int n = 0;
    for (int i = 0; i < count; ++i) {
        mobj_t *unit = units[i];
        if (!alive(unit) || unit->owner != owner || !land_unit(unit) || !(unit->traits & MF_ATTACK) ||
            (unit->traits & MF_NOAUTOTARGET) || unit->w2.carrier || unit->w2.build_phase || unit->w2.cast.spell ||
            P_HasMoveOrder(unit) || fighting(unit)) continue;
        fixed2_t at = fixed3_xy(unit->core.position);
        if (fixed2_distance_squared64(at, fixed2_cell_center(spot)) > fixed_sq64(FIXED_FROM_INT(RALLY_SPREAD)) &&
            fixed2_distance_squared64(at, home) < fixed_sq64(FIXED_FROM_INT(RALLY_HOME)) && reach(unit, spot, 1, NULL))
            idle[n++] = unit;
    }
    if (n) P_MoveUnitsAt(map, idle, n, fixed2_cell_center(spot));
}

void w2_ai_tactics(level_t *map, int owner, mobj_t *const *units, int count) {
    rally(map, owner, units, count);
    for (int i = 0; i < count; ++i) {
        mobj_t *unit = units[i];
        if (!alive(unit) || unit->owner != owner) continue;
        if (unit->w2.ferry.phase) run_ferry(unit, units, count);
        else if (stats(unit)->mana.max && !unit->w2.cast.spell && !unit->w2.boarded &&
                 (unit->traits & MF_MOBILE)) cast_best(unit, units, count);
    }
}
