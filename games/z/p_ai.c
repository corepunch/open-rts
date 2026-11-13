#include "z_local.h"
#include "info.h"

#include <string.h>

/* Native single-player commander. Task records refer to squads;
 * thinker-owned mobjs are the only units. */
enum {
    CPU = 1, MEMBERS = 21,
    RECAPTURE = 2, USABLE = 8, MOBILE = 16,             /* Task +0x10 bits. */
    IDLE = -1, MARCH = 0, FLAG = 1, STAGED = 2, ASSAULT = 3, GARRISON = 4,
    VEHICLE = 5, BUILDING_SUPPORT = 7, RETURN = 8, CRANE_WORK = 9, FRONTIER = 10, REPAIR = 11, /* Task +0x11. */
    LOCKOUT = 0x1fe,                                     /* 0x79e69 */
    WORTH_HOLDING = 500,                                 /* 0x99171, 0x998f4 */
};

static bool live(const mobj_t *mo) { return mo && !mo->remove && mo->hp > 0; }
static bool soldier(const mobj_t *mo) {
    return live(mo) && mo->team < Z_NEUTRAL && Z_UnitType(mo) >= 0;
}
static bool same_squad(const mobj_t *a, const mobj_t *b) {
    return a == b || (a->z.squad && a->z.squad == b->z.squad && a->team == b->team &&
                      Z_IsRobot(Z_UnitType(a)) && Z_IsRobot(Z_UnitType(b)));
}
static bool vehicle_candidate(const mobj_t *mo) {
    if (!Z_Enterable(mo)) return false;
    /* 0x98f70's native list includes dead-heavy 25, not heavy 26. The
     * shared admission rule still prevents boarding an actual wreck. */
    switch ((int)mo->z.object - 1) {
    case 5: case 8: case 21: case 22: case 23: case 24: case 25:
    case 29: case 31: case 33: case 35: case 37: return true;
    default: return false;
    }
}
static int members(const zaitask_t *task, mobj_t *const *units, int count, mobj_t **out) {
    int n = 0;
    for (int i = 0; i < count && n < MEMBERS; ++i)
        if (soldier(units[i]) && units[i]->team == CPU &&
            (task->squad ? units[i]->z.squad == task->squad && Z_IsRobot(Z_UnitType(units[i])) :
                           units[i]->id == task->unit))
            out[n++] = units[i];
    return n;
}

static void move_group(mobj_t **group, int count, fixed2_t goal) {
    if (!count) return;
    mobj_t *leader = Z_SquadLeader(group[0]);
    if (leader->z.squad_slot == UINT8_MAX) {
        ticcmd_t cmd = {.order = TC_ORDER, .count = 1, .units = {leader->id},
                        .position = fixed3_from_fixed2(goal, 0)};
        G_RunTiccmd(leader->owner, &cmd);
    } else {
        P_MoveUnitsAt(&level, group, count, goal);
        P_MoveUnitTo(&level, group[0], goal);
    }
}
static bool reachable(const mobj_t *mo, fixed2_t goal) {
    int cls = P_MobjMoveClass(mo);
    return (mo->traits & MF_MOBILE) &&
        P_NavReachable(&level, cls, P_NavNodeAt(&level, cls, fixed3_xy(mo->core.position)),
                       P_NavNodeAt(&level, cls, goal));
}
static bool allowed(const mobj_t *mo, uint8_t flags, bool attack_pool) {
    if ((flags & 32) && !Z_IsRobot(Z_UnitType(mo))) return false;
    if (!(flags & 64)) return true;
    /* 0x9a189 permits an APC only with class-3 (Tough) crew. */
    if (attack_pool && mo->type_id == MT_UNIT_APC) return mo->z.driver == MT_UNIT_TOUGH - MT_UNITS + 1;
    if (attack_pool && mo->type_id == MT_UNIT_JEEP) return false;
    if (!Z_CanCarryGrenades(Z_UnitType(mo))) return true;
    /* 0x7a776 sums grenade counts for all occupied group slots. */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *member = (const mobj_t *)th;
        if (live(member) && same_squad(mo, member) && member->z.grenades) return true;
    }
    return false;
}
static fixed2_t pixel_point(ivec2_t pixel) {
    return (fixed2_t){pixel.x * (FIXED_ONE / Z_TILE),pixel.y * (FIXED_ONE / Z_TILE)};
}
static int at_unit(const zmap_t *map, const mobj_t *mo) {
    return Z_AITerritory(map, pixel_point(Z_AITopLeft(mo)));
}
static void queue(zai_t *ai, zaitask_t *task, fixed2_t goal, uint32_t vehicle) {
    task->order = goal;
    task->boarding = vehicle;
    task->service = 0;
    task->enemy = 0;
    task->execute_at = ai->clock + 2 * (127 - ai->intelligence) + ai->order_tail;
    ai->order_tail += 127 - ai->intelligence;
    task->pending = true;
}

/* 0x9d42c/0x9d4f4: fort staging coordinates and the eight authored
 * 48-pixel offsets, starting at a random offset and testing each in turn. */
static bool send(zmap_t *map, zaitask_t *task, int territory) {
    if (territory < 0 || territory >= map->territory_count) return false;
    mobj_t *mo = P_MobjById(task->unit);
    if (!live(mo)) return false;
    if (!allowed(mo, map->ai.territories[territory].flags, false)) {
        if (!(mo->traits & MF_MOBILE)) return false; /* Native class-2 guns return at 0x9d430. */
        queue(&map->ai, task, fixed3_xy(mo->core.position), 0);
        return true;
    }
    ivec2_t pixel = ivec2_scale(map->territories[territory].flag, Z_TILE);
    uint8_t flags = map->ai.territories[territory].flags;
    if ((flags & 1) && map->fort_count) pixel = ivec2_add(map->forts[0], (ivec2_t){80,220});
    if ((flags & 2) && map->fort_count > 1) pixel = ivec2_add(map->forts[1], (ivec2_t){80,-80});
    static const ivec2_t offsets[] = {{-48,-48},{0,-48},{48,-48},{48,0},{48,48},{0,48},{-48,48},{-48,0}};
    int start = Z_NextRandom(&map->ai.seed) & 7;
    for (int n = 0; n < 8; ++n) {
        fixed2_t goal = pixel_point(ivec2_add(pixel, offsets[(start + n) & 7]));
        if (!reachable(mo, goal)) continue;
        bool reserved = false;
        for (int i = 0; i < Z_AI_TASKS; ++i) {
            const zaitask_t *other = &map->ai.tasks[i];
            if (other == task || !other->unit) continue;
            const mobj_t *lead = P_MobjById(other->unit);
            if (!other->pending && (!live(lead) || !P_HasMoveOrder(lead))) continue;
            fixed2_t destination = other->pending ? other->order : lead->movement.goal;
            if (ivec2_equal(fixed2_cell(destination), fixed2_cell(goal))) reserved = true;
        }
        if (reserved) continue;
        queue(&map->ai, task, goal, 0);
        return true;
    }
    return false;
}

static void sync_tasks(zmap_t *map, mobj_t *const *units, int count) {
    zai_t *ai = &map->ai;
    mobj_t *group[MEMBERS];
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        if (!task->unit) continue;
        int n = members(task, units, count, group);
        if (!n) { *task = (zaitask_t){0}; continue; }
        task->unit = group[0]->id;
        task->current = (int16_t)at_unit(map, group[0]);
        task->flags &= ~USABLE;
        if ((group[0]->traits & MF_MOBILE) && !group[0]->z.service && !task->service) task->flags |= USABLE;
        bool moving = false, fighting = false;
        mobj_t *enemy = NULL;
        for (int j = 0; j < n; ++j) {
            moving |= P_HasMoveOrder(group[j]) || group[j]->z.board || group[j]->z.service;
            if (live(group[j]->attack.target)) enemy = group[j]->attack.target;
        }
        for (int j = 0; enemy && j < n; ++j)
            if (!group[j]->z.service && P_CanTarget(group[j], enemy)) {
                group[j]->z.board = 0;
                group[j]->attack.target = enemy;
                fighting = true;
            }
        if (fighting) task->flags &= ~USABLE;
        /* 0x991c4 retires a completed order after three heavy passes. */
        if (moving || fighting || task->pending) task->idle = 0;
        if (task->boarding && !Z_Enterable(P_MobjById(task->boarding))) {
            task->boarding = 0;
            task->pending = false;
            task->task = -1;
            task->target = task->destination = -1;
        }
    }
    for (int u = 0; u < count; ++u) {
        mobj_t *mo = units[u];
        if (!soldier(mo) || mo->team != CPU) continue;
        uint8_t squad = Z_IsRobot(Z_UnitType(mo)) ? mo->z.squad : 0;
        bool found = false;
        zaitask_t *free = NULL;
        for (int i = 0; i < Z_AI_TASKS; ++i) {
            zaitask_t *task = &ai->tasks[i];
            if (!task->unit && !free) free = task;
            if (task->unit == mo->id || (squad && task->squad == squad)) found = true;
        }
        if (found || !free) continue;
        *free = (zaitask_t){.unit = mo->id,.squad = squad,.task = IDLE,.home = -1,
            .current = (int16_t)at_unit(map, mo),.target = -1,.destination = -1,
            .flags = (mo->traits & MF_MOBILE) ? USABLE : 0};
        if (mo->z.home && mo->z.home <= map->territory_count) {
            free->flags |= MOBILE;
            free->task = 0;
            free->target = free->destination = mo->z.home - 1;
            send(map, free, free->target);
        }
    }
}

static void observations(zmap_t *map, mobj_t *const *units, int count) {
    zai_t *ai = &map->ai;
    uint8_t owners[Z_MAX_TERRITORIES];
    for (int t = 0; t < map->territory_count; ++t) {
        zai_territory_t *out = &ai->territories[t];
        memset(out->strength, 0, sizeof(out->strength));
        owners[t] = (uint8_t)map->territories[t].team;
        out->known = 0;
    }
    /* A taken flag sets the territory's lockout to 510 frames (0x79e69). */
    for (int t = 0; t < map->territory_count; ++t) {
        if (owners[t] != ai->territories[t].owner) ai->territories[t].lockout = LOCKOUT;
        ai->territories[t].owner = owners[t];
    }
    int held = 0, owned = 0;
    for (int t = 0; t < map->territory_count; ++t) {
        held += ai->territories[t].owner < 2;
        owned += ai->territories[t].owner == CPU;
    }
    /* 0x98280/0x982c8 ignore neutral territories in this ratio. */
    int intel = map->computer.intelligence;
    if (held && owned * 100 / held <= 40) intel += intel / 2;
    ai->intelligence = (uint8_t)(intel > 127 ? 127 : intel);
    for (int u = 0; u < count; ++u) {
        const mobj_t *mo = units[u];
        if (!live(mo) || mo->team != CPU || mo->type_id != MT_RADAR) continue;
        /* 0x9ba72 advances 30 bytes, checking alignment to an 80-byte
         * building. Only original slots 0,3,6,9,12 reach its radar test. */
        int slot = (int)mo->z.building - 1;
        if (slot < 0 || slot > 12 || slot % 3) continue;
        int radius = held ? (100 * owned / map->territory_count) / 4 + 1 : 1;
        radius = mo->max_hp ? (100 * mo->hp / mo->max_hp) * radius / 100 : 0;
        if (radius > 25) radius = 25;
        /* 0x9ba79 tests flag origins and excludes both fort territories. */
        irect_t cells = P_MobjCells(mo);
        ivec2_t origin = ivec2_scale((ivec2_t){cells.x,cells.y}, Z_TILE);
        for (int t = 0; t < map->territory_count; ++t)
            if (!(ai->territories[t].flags & 3) &&
                ivec2_distance_squared64(origin, ivec2_scale(map->territories[t].flag, Z_TILE)) <=
                (int64_t)radius * radius * 80 * 80) ai->territories[t].known = 1;
    }
    /* Native 0x986dc counts a squad's sum once for EVERY live member. */
    for (int u = 0; u < count; ++u) {
        const mobj_t *mo = units[u];
        if (!soldier(mo) || mo->z.service_work < 0 || mo->type_id == MT_UNIT_CRANE || mo->type_id == MT_UNIT_FIRE_TRUCK) continue;
        int t = at_unit(map, mo), strength = 0;
        if (t < 0) continue;
        for (int v = 0; v < count; ++v)
            if (soldier(units[v]) && same_squad(mo, units[v])) strength += Z_AIUnitValue(units[v]);
        strength = Z_AIEstimate(strength, ai->intelligence);
        if (!ai->territories[t].known) strength = Z_AIEstimate(strength, ai->intelligence);
        if (strength > 65535) strength = 65535;
        ai->territories[t].strength[mo->team] += (uint16_t)strength;
    }
    for (int t = 0; t < map->territory_count; ++t) {
        zai_territory_t *out = &ai->territories[t];
        if (!ai->phase && !out->known) continue;
        out->value = (int16_t)(-out->strength[0]) +
            16 * map->computer.territory_weight * map->territories[t].importance;
        for (int u = 0; u < count; ++u) {
            const mobj_t *mo = units[u];
            if (!live(mo)) continue;
            if (Z_Enterable(mo) && at_unit(map, mo) == t)
                out->value += Z_AIEstimate(Z_AIEmptyValue(mo), ai->intelligence);
            else if (mo->type_id > MT_FORT && mo->type_id < MT_UNITS) {
                irect_t cells = P_MobjCells(mo);
                if (Z_AITerritory(map, fixed2_from_cell((ivec2_t){cells.x,cells.y})) == t)
                    out->value += Z_AIEstimate((int)mo->z.rating, ai->intelligence);
            }
        }
    }
}

static void split_army(zmap_t *map, mobj_t *const *units, int count) {
    zai_t *ai = &map->ai;
    int human = 0, computer = 0, home_human = 0, home_computer = 0, total = 0, assigned = 0;
    for (int t = 0; t < map->territory_count; ++t) {
        const zai_territory_t *area = &ai->territories[t];
        human += area->strength[0]; computer += area->strength[1];
        if (area->flags & 128) { home_human += area->strength[0]; home_computer += area->strength[1]; }
    }
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        mobj_t *mo = P_MobjById(task->unit), *group[MEMBERS];
        task->strength = 0;
        if (!live(mo) || mo->z.service || mo->type_id == MT_UNIT_CRANE || mo->type_id == MT_UNIT_FIRE_TRUCK) continue;
        int n = members(task, units, count, group);
        for (int j = 0; j < n; ++j) task->strength += Z_AIUnitValue(group[j]);
        task->strength = Z_AIEstimate(task->strength, ai->intelligence);
        if (!(mo->traits & MF_MOBILE) && (task->current < 0 || !(ai->territories[task->current].flags & 1)))
            task->strength = 0;
        total += task->strength;
    }
    ai->total = total;
    /* 0x97f8f clears its percentage but retains the last absolute credit
     * when the computer falls behind. Keep that native state as well. */
    if (computer && computer >= human) ai->imbalance = (computer - human) / 2;
    ai->mobile = Z_AIMobileBudget(total, ai->imbalance, home_human, home_computer, map->computer.allocation);
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        bool was_mobile = task->flags & MOBILE;
        task->flags &= ~MOBILE;
        if ((task->flags & USABLE) && task->strength && assigned + task->strength <= ai->mobile) {
            task->flags |= MOBILE;
            assigned += task->strength;
        } else if (was_mobile && task->unit && task->task >= 0 && task->task <= 3) {
            if (task->task == 1 && task->current >= 0 && (ai->territories[task->current].flags & 2))
                task->flags |= MOBILE;
            else for (int t = 0; t < map->territory_count; ++t)
                if (ai->territories[t].flags & 1) {
                    task->task = -1;
                    task->target = task->destination = -1;
                    send(map, task, t);
                    break;
                }
        }
    }
}

static bool reserved(const zai_t *ai, int territory, bool boarding) {
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        const zaitask_t *task = &ai->tasks[i];
        if (!task->unit) continue;
        if (boarding ? task->task == 5 && task->target == territory :
            task->task >= 0 && task->task <= 3 && task->destination == territory) return true;
    }
    return false;
}

static bool offensive(const zaitask_t *task) {
    return task->unit && (task->flags & (USABLE | MOBILE)) == (USABLE | MOBILE);
}
static bool reserve(const zaitask_t *task) {
    return task->unit && !task->service && (task->flags & (USABLE | MOBILE)) == USABLE;
}
static bool owned(const zai_t *ai, int territory) {
    return territory >= 0 && ai->territories[territory].owner == CPU;
}
/* 0x9a054: the force an attack needs. */
static int required(const zai_t *ai, int territory) {
    const zai_territory_t *t = &ai->territories[territory];
    return t->strength[0] ? 2 * t->strength[0] + t->value : 1;
}
/* 0x995de: offensive strength already bound for a territory. */
static bool bound_for(const zaitask_t *task, int territory) {
    return offensive(task) && task->task != IDLE && task->task != GARRISON &&
        task->task != VEHICLE && task->destination == territory;
}
static int heading(const zai_t *ai, int territory) {
    int sum = 0;
    for (int i = 0; i < Z_AI_TASKS; ++i)
        if (bound_for(&ai->tasks[i], territory)) sum += ai->tasks[i].strength;
    return sum;
}
/* 0x99a28: another offensive task in `state` bound for the territory. */
static bool other_in(const zai_t *ai, const zaitask_t *self, int state, int territory) {
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        const zaitask_t *task = &ai->tasks[i];
        if (task != self && offensive(task) && task->task == state && task->destination == territory) return true;
    }
    return false;
}
/* 0x9d641: same-side equality is checked before opposite-side overlap. */
static bool adjacent(const zmap_t *map, int a, int b) {
    if (a < 0 || b < 0 || a == b) return false;
    const zterritory_t *p = &map->territories[a], *q = &map->territories[b];
    if (p->min.x == q->min.x || p->max.x == q->max.x)
        return p->min.y == q->max.y || p->max.y == q->min.y;
    if (p->max.x == q->min.x || p->min.x == q->max.x)
        return p->max.y >= q->min.y && p->min.y <= q->max.y;
    if (p->min.y == q->min.y || p->max.y == q->max.y)
        return p->min.x == q->max.x || p->max.x == q->min.x;
    if (p->max.y == q->min.y || p->min.y == q->max.y)
        return p->max.x >= q->min.x && p->min.x <= q->max.x;
    return false;
}
/* 0x999e5: a territory's value scaled by the share of the army held back. */
int Z_AIReserveValue(const zai_t *ai, int territory) {
    int total = ai->mobile + (ai->total - ai->mobile);
    int share = 100 - ai->mobile * 100 / (total ? total : 1);
    int64_t value = ai->territories[territory].value;
    return value > 0 ? (int)(value * share / 100) : 0;
}
/* 0x99976: garrisons in a territory; the loop also counts every gun. */
static int guards(const zai_t *ai, int territory) {
    int sum = 0;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        const zaitask_t *task = &ai->tasks[i];
        const mobj_t *mo = P_MobjById(task->unit);
        if (!live(mo)) continue;
        if (!(mo->traits & MF_MOBILE) || (offensive(task) && task->task == GARRISON && task->target == territory))
            sum += task->strength;
    }
    return sum;
}
static void clear(zaitask_t *task) {
    task->task = IDLE;
    task->target = task->destination = -1;
}
/* 0x99c4d/0x99df0: onto the flag, or into the human fort. */
static bool to_flag(zmap_t *map, zaitask_t *task, int territory) {
    mobj_t *mo = P_MobjById(task->unit);
    if (!live(mo)) return false;
    ivec2_t pixel = ivec2_add(ivec2_scale(map->territories[territory].flag, Z_TILE), (ivec2_t){4,4});
    if ((map->ai.territories[territory].flags & 2) && map->fort_count > 1)
        pixel = ivec2_add(map->forts[1], (ivec2_t){80,72});
    fixed2_t goal = pixel_point(pixel);
    if (!reachable(mo, goal)) return false;
    queue(&map->ai, task, goal, 0);
    return true;
}
/* 0x99502: re-stage in the current territory (or stop) and forget. */
static void release(zmap_t *map, zaitask_t *task) {
    mobj_t *mo = P_MobjById(task->unit);
    if (live(mo)) send(map, task, task->current);
    clear(task);
}
/* 0x999c2: hold a territory. */
static void garrison(zmap_t *map, zaitask_t *task, int territory) {
    task->task = GARRISON;
    task->target = (int16_t)territory;
    task->destination = -1;
    send(map, task, territory);
}

typedef struct { zaitask_t *task; int64_t distance; } pooled_t;
/* 0x9a126/0x9a080: idle offensive tasks that may go, nearest first. */
static int idle_pool(zmap_t *map, int territory, pooled_t *pool) {
    int n = 0;
    fixed2_t goal = fixed2_cell_center(map->territories[territory].flag);
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &map->ai.tasks[i];
        mobj_t *mo = P_MobjById(task->unit);
        if (!live(mo) || !offensive(task) || task->task != IDLE || task->pending ||
            !allowed(mo, map->ai.territories[territory].flags, true) || !reachable(mo, goal)) continue;
        int64_t distance = ivec2_distance_squared64(Z_AITopLeft(mo), ivec2_scale(map->territories[territory].flag, Z_TILE));
        int at = n++;
        while (at && pool[at - 1].distance > distance) { pool[at] = pool[at - 1]; at--; }
        pool[at].task = task; pool[at].distance = distance;
    }
    return n;
}
/* 0x9a027: how many of the pool, in order, make up `need`; 0 if not enough. */
static int enough(const pooled_t *pool, int n, int need) {
    int sum = 0;
    for (int i = 0; i < n; ++i)
        if ((sum += pool[i].task->strength) >= need) return i + 1;
    return 0;
}
/* 0x99f95: attacking the human fort also draws mobile tasks from every
 * adjacent territory directly into that assault. */
void Z_AIFortFollowup(zmap_t *map, int territory) {
    if (territory < 0 || territory >= map->territory_count || !(map->ai.territories[territory].flags & 2)) return;
    for (int t = 0; t < map->territory_count; ++t) {
        if (!adjacent(map, territory, t)) continue;
        for (int i = 0; i < Z_AI_TASKS; ++i) {
            zaitask_t *task = &map->ai.tasks[i];
            if (!task->unit || task->current != t || (task->flags & (USABLE | MOBILE)) != (USABLE | MOBILE) ||
                (task->task == ASSAULT && task->destination == territory)) continue;
            task->task = ASSAULT;
            task->target = task->destination = (int16_t)territory;
            send(map, task, territory);
        }
    }
}

/* 0x99f46: march the first `n` on the territory. */
static void commit(zmap_t *map, pooled_t *pool, int n, int territory) {
    for (int i = 0; i < n; ++i) {
        pool[i].task->task = MARCH;
        pool[i].task->target = pool[i].task->destination = (int16_t)territory;
        send(map, pool[i].task, territory);
    }
    Z_AIFortFollowup(map, territory);
}

/* 0x9a1dd/0x99e3f: this first attack pass visits the entire stable ranking.
 * Later retail retargeting passes use quantized scores and tie selection. */
static void attack_territories(zmap_t *map) {
    zai_t *ai = &map->ai;
    struct { int territory; uint32_t rank; } ranked[Z_MAX_TERRITORIES];
    int count = 0;
    ivec2_t origin = {0};
    for (int t = 0; t < map->territory_count; ++t)
        if (ai->territories[t].flags & 1) { origin = map->territories[t].flag; break; }
    for (int t = 0; t < map->territory_count; ++t) {
        if (ai->territories[t].owner == CPU || reserved(ai, t, false)) continue;
        uint32_t rank = Z_AIRank(ai->territories[t].value, origin, map->territories[t].flag, map->square_roots.bytes);
        int at = count++;
        while (at && ranked[at - 1].rank < rank) { ranked[at] = ranked[at - 1]; at--; }
        ranked[at].territory = t;
        ranked[at].rank = rank;
    }
    for (int r = 0; r < count; ++r) {
        int t = ranked[r].territory;
        pooled_t pool[Z_AI_TASKS];
        int n = enough(pool, idle_pool(map, t, pool), required(ai, t));
        if (n) commit(map, pool, n, t);
    }
}

/* 0x99e60: more force for an attack falling short; a garrison of a
 * territory worth no more than the target may join (0x9937f). */
static bool reinforce(zmap_t *map, int territory) {
    zai_t *ai = &map->ai;
    pooled_t pool[Z_AI_TASKS + 1];
    int n = idle_pool(map, territory, pool), need = required(ai, territory), use = enough(pool, n, need);
    if (!use) {
        zaitask_t *cheapest = NULL;
        int32_t value = INT32_MAX;
        for (int i = 0; i < Z_AI_TASKS; ++i) {
            zaitask_t *task = &ai->tasks[i];
            if (offensive(task) && task->task == GARRISON && task->target >= 0 &&
                ai->territories[task->target].value <= value) {
                value = ai->territories[task->target].value;
                cheapest = task;
            }
        }
        if (!cheapest || value > ai->territories[territory].value) return false;
        pool[n++] = (pooled_t){cheapest, 0};
        if (!(use = enough(pool, n, need))) return false;
    }
    commit(map, pool, use, territory);
    return true;
}

/* 0x993ff: an attack that is too weak gets help or falls back; one with
 * more than it needs lets the surplus go (0x9942a). */
static void check_forces(zmap_t *map) {
    zai_t *ai = &map->ai;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        int t = task->destination;
        if (!task->unit || task->task != MARCH || t < 0) continue;
        int need = required(ai, t);
        if (heading(ai, t) < need) {
            for (int j = 0; j < Z_AI_TASKS; ++j)
                if (bound_for(&ai->tasks[j], t)) ai->tasks[j].task = MARCH;
            if (!reinforce(map, t))
                for (int j = 0; j < Z_AI_TASKS; ++j)
                    if (offensive(&ai->tasks[j]) && ai->tasks[j].destination == t) release(map, &ai->tasks[j]);
        } else if (!(ai->territories[t].flags & 2)) {
            int sum = 0;
            bool spare = false;
            for (int j = 0; j < Z_AI_TASKS; ++j) {
                zaitask_t *other = &ai->tasks[j];
                if (!bound_for(other, t)) continue;
                if (spare) release(map, other);
                else if ((sum += other->strength) > need) spare = true;
            }
        }
    }
}

/* 0x998c5: a garrison leaves a territory lost or no longer worth holding. */
static void release_garrisons(zmap_t *map) {
    zai_t *ai = &map->ai;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        if (offensive(task) && task->task == GARRISON &&
            (!owned(ai, task->target) || Z_AIReserveValue(ai, task->target) < WORTH_HOLDING)) {
            task->task = IDLE;
            task->target = -1;
        }
    }
}

/* 0x99910: after a capture, idle forces standing there hold it. */
static void hold_captured(zmap_t *map, int territory) {
    zai_t *ai = &map->ai;
    int left = Z_AIReserveValue(ai, territory);
    if (ai->territories[territory].value < WORTH_HOLDING || !left || guards(ai, territory)) return;
    bool first = true;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        if (!offensive(task) || task->current != territory || task->task != IDLE) continue;
        bool last = !first && left - task->strength < 0;
        garrison(map, task, territory);
        left -= task->strength;
        first = false;
        if (last) break;
    }
}

/* 0x9962c: each offensive task's march. 0x99818 notices a capture or an
 * arrival; 0x99708 waits beside the target for the others, then all go. */
static void advance(zmap_t *map) {
    zai_t *ai = &map->ai;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        if (!offensive(task)) continue;
        int t = task->destination;
        if (t < 0 || task->task == GARRISON || task->task == VEHICLE) continue;
        if (task->task == MARCH || task->task == ASSAULT || task->task == FLAG || task->task == STAGED) {
            if (owned(ai, t)) {
                task->task = IDLE;
                hold_captured(map, t);
                continue;
            }
            bool taken = false;
            for (int j = 0; j < Z_AI_TASKS; ++j)
                taken |= offensive(&ai->tasks[j]) && ai->tasks[j].task == FLAG && ai->tasks[j].target == t;
            if (task->current == t && !taken && !task->pending) {
                to_flag(map, task, t);
                task->task = FLAG;
                task->target = (int16_t)t;
            }
        }
        if (task->task == MARCH) {
            int staged = 0;
            for (int j = 0; j < Z_AI_TASKS; ++j) {
                const zaitask_t *other = &ai->tasks[j];
                if (offensive(other) && other->task == STAGED && other->destination == t) staged += other->strength;
            }
            if (staged >= required(ai, t)) {
                /* 0x996c1: everyone marching or waiting goes in. */
                for (int j = 0; j < Z_AI_TASKS; ++j) {
                    zaitask_t *other = &ai->tasks[j];
                    if (offensive(other) && other->destination == t &&
                        (other->task == MARCH || other->task == STAGED)) {
                        other->task = ASSAULT;
                        to_flag(map, other, t);
                    }
                }
            } else if (other_in(ai, task, MARCH, t) || other_in(ai, task, FLAG, t)) {
                if (adjacent(map, task->current, t)) {
                    send(map, task, task->current);
                    task->task = STAGED;
                    task->target = task->current;
                }
            } else if (adjacent(map, task->current, t)) {
                task->task = ASSAULT;
                to_flag(map, task, t);
            }
        } else if (task->task == STAGED && !other_in(ai, task, MARCH, t)) {
            /* 0x997d5: nobody is still on the way, so go in as they are. */
            send(map, task, t);
            task->task = ASSAULT;
        }
    }
}

/* 0x99149: send idle offensive force to hold the first territory worth it. */
static void hold_territories(zmap_t *map) {
    zai_t *ai = &map->ai;
    for (int t = 0; t < map->territory_count; ++t) {
        if (!owned(ai, t) || (ai->territories[t].flags & 3)) continue;
        int have = guards(ai, t), worth = Z_AIReserveValue(ai, t);
        if (worth < WORTH_HOLDING || have > worth) continue;
        pooled_t pool[Z_AI_TASKS];
        int n = enough(pool, idle_pool(map, t, pool), worth - have);
        for (int i = 0; i < n; ++i) garrison(map, pool[i].task, t);
        return;
    }
}

/* 0x98d95: an idle force deep in its own land moves up to the first
 * territory that borders someone else's, after two quiet heavy passes. */
static void to_frontier(zmap_t *map, mobj_t *const *units, int count) {
    zai_t *ai = &map->ai;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        if (!offensive(task)) continue;
        if (task->task != IDLE || task->current < 0) { task->passes = 0; continue; }
        bool inland = true;
        for (int t = 0; t < map->territory_count && inland; ++t)
            if (adjacent(map, task->current, t) && !owned(ai, t)) inland = false;
        if (!inland) { task->passes = 0; continue; }
        mobj_t *group[MEMBERS];
        int n = members(task, units, count, group);
        bool busy = task->pending;
        for (int j = 0; j < n; ++j) busy |= P_HasMoveOrder(group[j]);
        if (busy || ++task->passes < 2) continue;
        for (int t = 0; t < map->territory_count; ++t) {
            if (!owned(ai, t) || (ai->territories[t].flags & 3) || !n ||
                !allowed(group[0], ai->territories[t].flags, false)) continue;
            bool border = false;
            for (int a = 0; a < map->territory_count && !border; ++a)
                border = adjacent(map, t, a) && !owned(ai, a);
            if (!border) continue;
            fixed2_t goal = pixel_point(ivec2_add(ivec2_scale(map->territories[t].flag, Z_TILE), (ivec2_t){8,8}));
            if (!reachable(group[0], goal)) break;
            queue(ai, task, goal, 0);
            task->passes = 0;
            task->task = FRONTIER;
            task->destination = (int16_t)t;
            break;
        }
    }
}

/* 0x9c174: a reserve standing in a territory just lost, with no enemy
 * left there, runs back onto the flag; 0x9cab4 clears the mark. */
static void recapture(zmap_t *map) {
    zai_t *ai = &map->ai;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        if ((task->flags & RECAPTURE) && owned(ai, task->current)) task->flags &= ~RECAPTURE;
    }
    for (int t = 0; t < map->territory_count; ++t) {
        const zai_territory_t *area = &ai->territories[t];
        if (!area->lockout || !area->strength[CPU] || area->strength[0] || area->owner == CPU) continue;
        for (int i = 0; i < Z_AI_TASKS; ++i) {
            zaitask_t *task = &ai->tasks[i];
            if (!reserve(task) || task->current != t) continue;
            if (task->flags & RECAPTURE) break;
            fixed2_t goal = pixel_point(ivec2_add(ivec2_scale(map->territories[t].flag, Z_TILE), (ivec2_t){8,8}));
            mobj_t *mo = P_MobjById(task->unit);
            if (live(mo) && reachable(mo, goal)) {
                queue(ai, task, goal, 0);
                task->flags |= RECAPTURE;
            }
            break;
        }
    }
}

/* 0x9cb5e: a reserve sent away goes home once its territory is quiet. */
static void go_home(zmap_t *map) {
    zai_t *ai = &map->ai;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        mobj_t *mo = P_MobjById(task->unit);
        if (!task->unit || (task->flags & MOBILE) || task->home < 0 || task->home == task->current ||
            !live(mo) || !(mo->traits & MF_MOBILE) || task->pending || (task->flags & RECAPTURE) ||
            mo->z.service || mo->type_id == MT_UNIT_CRANE || mo->type_id == MT_UNIT_FIRE_TRUCK || task->current < 0) continue;
        const zai_territory_t *here = &ai->territories[task->current];
        int enemy = 0;
        for (int team = 0; team < 4; ++team) if (team != CPU) enemy += here->strength[team];
        if (!enemy) send(map, task, task->home);
    }
}

/* 0x9d330/0x9d390: the strongest squad a territory can spare while still
 * keeping 16 * weight * importance of its own strength there. */
static zaitask_t *donor(zmap_t *map, int territory, mobj_t *const *units, int count) {
    zai_t *ai = &map->ai;
    zaitask_t *best = NULL;
    int most = -1, keep = 16 * map->computer.territory_weight * map->territories[territory].importance;
    if (keep > 0xffff) return NULL;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        mobj_t *group[MEMBERS], *mo = P_MobjById(task->unit);
        if (!task->unit || task->current != territory || !live(mo) || !(mo->traits & MF_MOBILE) || mo->z.service) continue;
        int n = members(task, units, count, group), value = 0;
        for (int j = 0; j < n; ++j) value += Z_AIUnitValue(group[j]);
        if (!value) continue;
        if (value > 0xffff) value = 0xffff;
        int left = (int16_t)(ai->territories[territory].strength[CPU] - value);
        if (left < 0 || left < keep || value < most) continue;
        most = value;
        best = task;
    }
    return best;
}
static int16_t surplus(const zai_t *ai, int territory) {
    return (int16_t)(ai->territories[territory].strength[CPU] - ai->territories[territory].strength[0]);
}
/* 0x9cf33: the nearest other territory of the team, by flag. */
static int nearest_own(const zmap_t *map, int team, int except, ivec2_t pixel) {
    int best = -1;
    int64_t distance = INT64_MAX;
    for (int t = 0; t < map->territory_count; ++t) {
        if (map->ai.territories[t].owner != team || t == except) continue;
        int64_t d = ivec2_distance_squared64(ivec2_scale(map->territories[t].flag, Z_TILE), pixel);
        if (d < distance) { distance = d; best = t; }
    }
    return best;
}

/* 0x9d115: an enemy that a computer reserve has seen (0x9dc25), in a
 * territory where the enemy is at least as strong, draws a reserve from
 * the neighbour with most to spare (0x9d166), then from any territory no
 * more important (0x9d1c5); failing both, reserves there fall back. */
static void defend(zmap_t *map, mobj_t *const *units, int count) {
    zai_t *ai = &map->ai;
    uint32_t seen[MEMBERS * 4];
    int seen_count = 0;
    for (int u = 0; u < count && seen_count < (int)(sizeof(seen) / sizeof(seen[0])); ++u) {
        const mobj_t *mo = units[u], *enemy = mo->attack.target;
        if (!soldier(mo) || mo->team != CPU || !soldier(enemy) || enemy->team == CPU) continue;
        bool mobile = false, known = false;
        for (int i = 0; i < Z_AI_TASKS; ++i)
            if (ai->tasks[i].unit && (ai->tasks[i].unit == mo->id || (mo->z.squad && ai->tasks[i].squad == mo->z.squad)))
                mobile = ai->tasks[i].flags & MOBILE;
        for (int i = 0; i < seen_count; ++i) known |= seen[i] == enemy->id;
        if (!mobile && !known) seen[seen_count++] = enemy->id;
    }
    for (int s = 0; s < seen_count; ++s) {
        const mobj_t *enemy = P_MobjById(seen[s]);
        int t = live(enemy) ? at_unit(map, enemy) : -1;
        if (t < 0 || ai->territories[t].owner == enemy->team ||
            ai->territories[t].strength[0] < ai->territories[t].strength[CPU]) continue;
        zaitask_t *help = NULL;
        int16_t best = surplus(ai, t);
        for (int a = 0; a < map->territory_count; ++a) {
            if (!adjacent(map, t, a) || !ai->territories[a].strength[CPU] || surplus(ai, a) < best) continue;
            zaitask_t *d = donor(map, a, units, count);
            if (d) { help = d; best = surplus(ai, a); }
        }
        if (!help) {
            best = surplus(ai, t);
            for (int i = 0; i < Z_AI_TASKS; ++i) {
                zaitask_t *task = &ai->tasks[i];
                int c = task->current;
                if (!reserve(task) || c < 0 || c == t ||
                    map->territories[c].importance > map->territories[t].importance ||
                    surplus(ai, c) < 0 || surplus(ai, c) < best) continue;
                zaitask_t *d = donor(map, c, units, count);
                if (d) { help = d; best = surplus(ai, c); }
            }
        }
        if (help) {
            if (help->home < 0) help->home = help->current;
            send(map, help, t);
            continue;
        }
        for (int i = 0; i < Z_AI_TASKS; ++i) {
            zaitask_t *task = &ai->tasks[i];
            mobj_t *mo = P_MobjById(task->unit);
            if (!reserve(task) || task->current != t || !live(mo)) continue;
            if (task->home < 0 || task->home == t) task->home = (int16_t)nearest_own(map, CPU, t, Z_AITopLeft(mo));
            if (task->home >= 0) send(map, task, task->home);
        }
    }
}

/* 0x98ee1/0x98f32: idle offensive robots, one vehicle task per territory.
 * Outside radar coverage, a random bit decides whether an object is seen. */
static void seek_vehicles(zmap_t *map, mobj_t *const *units, int count) {
    zai_t *ai = &map->ai;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        mobj_t *mo = P_MobjById(task->unit), *best = NULL;
        if (!live(mo) || (task->flags & (USABLE | MOBILE)) != (USABLE | MOBILE) ||
            task->task != -1 || task->pending || !Z_IsRobot(Z_UnitType(mo))) continue;
        int64_t distance = INT64_MAX;
        int target = -1;
        for (int u = 0; u < count; ++u) {
            mobj_t *vehicle = units[u];
            if (!vehicle_candidate(vehicle)) continue;
            int t = at_unit(map, vehicle);
            if (t < 0 || reserved(ai, t, true) || (!ai->territories[t].known && !(Z_NextRandom(&ai->seed) & 1))) continue;
            fixed2_t goal = fixed3_xy(vehicle->core.position);
            int64_t d = ivec2_distance_squared64(Z_AITopLeft(mo), Z_AITopLeft(vehicle));
            if (d < distance && reachable(mo, goal)) { best = vehicle; distance = d; target = t; }
        }
        if (best) {
            task->task = 5;
            task->target = (int16_t)target;
            queue(ai, task, fixed3_xy(best->core.position), best->id);
        }
    }
}

static void local_orders(zmap_t *map, mobj_t *const *units, int count) {
    zai_t *ai = &map->ai;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        mobj_t *mo = P_MobjById(task->unit);
        if (!live(mo) || mo->z.service || task->service || task->current < 0) continue;
        zai_territory_t *t = &ai->territories[task->current];
        /* 0x9a49a: a mobile force in the second fort territory enters it. */
        if ((t->flags & 2) && (task->flags & (USABLE | MOBILE)) == (USABLE | MOBILE) &&
            task->task != VEHICLE && task->task != FLAG && !task->pending && map->fort_count > 1) {
            fixed2_t goal = pixel_point(ivec2_add(map->forts[1], (ivec2_t){80,72}));
            if (reachable(mo, goal)) {
                /* 0x99d42: the force is now taking that territory. */
                queue(ai, task, goal, 0);
                task->task = FLAG; task->target = task->current;
            }
        }
        if (--task->retry >= 0) continue;
        task->retry = 0;
        if (t->owner == CPU || (t->flags & 3) || mo->type_id == MT_UNIT_SNIPER ||
            !(task->flags & USABLE) || task->boarding || live(mo->attack.target)) continue;
        fixed2_t goal = fixed2_cell_center(map->territories[task->current].flag);
        if (ivec2_distance_squared64(Z_AITopLeft(mo), ivec2_scale(map->territories[task->current].flag, Z_TILE)) < 16384) {
            /* The native flag correction is immediate (0x7ba9a). */
            if (reachable(mo, goal)) {
                mobj_t *group[MEMBERS];
                int n = members(task, units, count, group);
                move_group(group, n, goal);
            }
            task->retry = 100;
        }
    }
}

static void execute_orders(zai_t *ai, mobj_t *const *units, int count) {
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        if (!task->pending || task->execute_at > ai->clock) continue;
        mobj_t *group[MEMBERS];
        int n = members(task, units, count, group);
        if (n) {
            if (task->enemy) {
                mobj_t *enemy = P_MobjById(task->enemy);
                for (int j = 0; j < n; ++j) {
                    mobj_t *unit = group[j];
                    if (unit->z.service || !P_CanTarget(unit, enemy)) continue;
                    P_ClearMove(unit);
                    unit->z.board = 0;
                    unit->move_only = false;
                    unit->attack.target = enemy;
                }
            } else if (task->service) Z_ServiceOrder(group[0], Z_ServiceTarget(task->service), task->order);
            else if (task->boarding) Z_BoardOrder(group, n, P_MobjById(task->boarding));
            else {
                move_group(group, n, task->order);
            }
        }
        task->pending = false;
        if (task->task != REPAIR) task->service = 0;
        task->enemy = 0;
    }
}

static bool return_service(zmap_t *map, zaitask_t *task, mobj_t *unit) {
    if (!map->fort_count) return false;
    fixed2_t goal = pixel_point(ivec2_add(map->forts[0], (ivec2_t){80,200}));
    task->task = RETURN;
    task->pending = false;
    task->service = task->boarding = task->enemy = 0;
    if (reachable(unit, goal)) queue(&map->ai, task, goal, 0);
    return true;
}

static int64_t service_distance(const mobj_t *unit, const mobj_t *target) {
    const zmap_t *map = level.native_data;
    ivec2_t point;
    if (target->type_id == MT_BRIDGE) {
        const zbridge_t *bridge = &map->bridges[target->z.bridge - 1];
        irect_t r = bridge->bounds;
        point = (ivec2_t){r.x + r.w / 2 + (bridge->vertical ? 0 : 8), r.y + r.h / 2 - 8};
    } else {
        isize2_t size = target->info->footprint;
        point = ivec2_add(Z_BuildingOrigin(target), (ivec2_t){size.w * 8 - 8, size.h * 8 - 8});
    }
    return ivec2_distance_squared64(Z_AITopLeft(unit), point);
}

static bool order_service(zai_t *ai, zaitask_t *task, mobj_t *unit, mobj_t *target, int type) {
    fixed2_t goal;
    if (!Z_ServiceApproach(unit, target, &goal)) {
        task->pending = false;
        task->boarding = task->enemy = 0;
        task->service = type == CRANE_WORK ? target->id : 0;
        task->task = (int8_t)type;
        if (type == REPAIR) task->execute_at = ai->clock;
        return true;
    }
    if (type == CRANE_WORK) clear(task);
    queue(ai, task, goal, 0);
    task->service = target->id;
    task->task = (int8_t)type;
    return true;
}

/* 0x9c58d, then 0x9c68b. Work on owned ruins first. A closer crane can
 * take over a bridge while the first crane is still travelling. */
static void send_cranes(zmap_t *map, mobj_t *const *units, int count, bool bridges) {
    zai_t *ai = &map->ai;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        mobj_t *unit = P_MobjById(task->unit);
        if (!unit || unit->type_id != MT_UNIT_CRANE || !(task->flags & USABLE) ||
            task->task == CRANE_WORK || unit->z.service) continue;
        int64_t distance = INT64_MAX;
        mobj_t *best = NULL, *displaced = NULL;
        zaitask_t *previous = NULL;
        for (int u = 0; u < count; ++u) {
            mobj_t *target = units[u];
            if (target->remove || target->hp > 0) continue;
            if (bridges) {
                if (target->type_id != MT_BRIDGE) continue;
                const zbridge_t *b = &map->bridges[target->z.bridge - 1];
                int territory = Z_BridgeTerritory(map, target);
                if (!b->cpu_rebuild || target->z.service_ticks || !owned(ai, territory)) continue;
            } else if (!target->z.building || target->team != CPU) continue;
            int64_t d = service_distance(unit, target);
            if (d >= distance) continue;
            mobj_t *other = NULL;
            zaitask_t *other_task = NULL;
            for (int j = 0; j < Z_AI_TASKS; ++j) {
                zaitask_t *candidate = &ai->tasks[j];
                mobj_t *crane = P_MobjById(candidate->unit);
                if (candidate->task == CRANE_WORK && crane &&
                    (candidate->service == target->id || crane->z.service == target->id)) {
                    other = crane;
                    other_task = candidate;
                    break;
                }
            }
            if (other && (!bridges || other->core.state_id == S_CRANE_WORK ||
                          target->z.service_work || service_distance(other, target) < d)) continue;
            best = target;
            distance = d;
            displaced = other;
            previous = other_task;
        }
        if (best && order_service(ai, task, unit, best, CRANE_WORK) && displaced) {
            Z_CancelService(displaced);
            P_ClearMove(displaced);
            previous->pending = false;
            previous->service = 0;
            if (!return_service(map, previous, displaced)) clear(previous);
        }
    }
}

/* 0x9c4f2 and 0x9bc08 send idle cranes/fire trucks and released crane
 * assignments back to the CPU fort. Finished vehicle repairs go idle. */
static void service_returns(zmap_t *map, bool validate) {
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &map->ai.tasks[i];
        mobj_t *unit = P_MobjById(task->unit);
        if (!unit) continue;
        /* 0x9bc34/0x9bdb9: keep task 7 while its building is owned and
         * alive below maximum HP. No native order assigning 7 is known. */
        if (task->task == BUILDING_SUPPORT) {
            if (!validate) continue;
            const mobj_t *target = unit->target;
            if (!target || target->remove || target->type_id <= MT_FORT || target->type_id >= MT_UNITS) continue;
            int t = Z_AITerritory(map, pixel_point(Z_BuildingOrigin(target)));
            if (t >= 0 && map->ai.territories[t].owner == CPU && target->hp > 0 && target->hp < target->max_hp)
                continue;
            unit->target = NULL;
            if (!return_service(map, task, unit)) clear(task);
            continue;
        }
        if (validate && task->task == CRANE_WORK && task->service && !task->pending && !unit->z.service) {
            mobj_t *target = Z_ServiceTarget(task->service);
            int territory = target && target->type_id == MT_BRIDGE ? Z_BridgeTerritory(map, target) : -1;
            if (target && !target->remove && target->hp <= 0 &&
                (target->type_id == MT_BRIDGE ? owned(&map->ai, territory) : target->team == CPU)) continue;
            task->service = 0;
            if (!return_service(map, task, unit)) clear(task);
            continue;
        }
        if (validate && task->task == REPAIR && task->service && !task->pending && !unit->z.service) {
            task->service = 0;
            clear(task);
            continue;
        }
        if (unit->z.service || task->service) continue;
        if (validate) {
            if (task->task == CRANE_WORK) {
                if (P_HasMoveOrder(unit)) continue; /* Egress from a completed repair. */
                if (!return_service(map, task, unit)) clear(task);
            } else if (task->task == REPAIR &&
                       (unit->hp >= unit->max_hp || map->ai.clock != task->execute_at)) clear(task);
        } else if (task->task == IDLE && !task->pending && task->current >= 0 &&
                   !(map->ai.territories[task->current].flags & 1) &&
                   (unit->type_id == MT_UNIT_CRANE || unit->type_id == MT_UNIT_FIRE_TRUCK)) {
            return_service(map, task, unit);
        }
    }
}

/* 0x9c39b/0x9c42c: idle reserve vehicles below half HP, friendly live
 * facilities and a strict 256-pixel limit on distance to their ranking point. */
static void send_damaged_vehicles(zmap_t *map, mobj_t *const *units, int count) {
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &map->ai.tasks[i];
        mobj_t *unit = P_MobjById(task->unit);
        if (!unit || task->task != IDLE || !reserve(task) || task->pending || unit->z.service ||
            !Z_RepairVehicle(unit) || unit->hp >= unit->max_hp / 2) continue;
        mobj_t *best = NULL;
        int64_t distance = 256 * 256;
        for (int u = 0; u < count; ++u) {
            mobj_t *facility = units[u];
            if (facility->type_id != MT_REPAIR || !live(facility) || facility->team != CPU) continue;
            int64_t d = service_distance(unit, facility);
            if (d < distance) { best = facility; distance = d; }
        }
        if (best) order_service(&map->ai, task, unit, best, REPAIR);
    }
}

/* 0x9d084 issues an attack on a unit or its squad. The shared thinker
 * handles pursuit and firing once given the target. */
static bool respond(zai_t *ai, zaitask_t *task, const mobj_t *enemy, mobj_t *const *units, int count) {
    if (task->service) return false;
    if (task->enemy == enemy->id) return true;
    mobj_t *group[MEMBERS];
    int n = members(task, units, count, group);
    for (int j = 0; j < n; ++j) {
        mobj_t *unit = group[j];
        if (unit->z.service || unit->type_id == MT_UNIT_FIRE_TRUCK ||
            unit->attack.target == enemy || !P_CanTarget(unit, enemy)) continue;
        queue(ai, task, (fixed2_t){0}, 0);
        task->enemy = enemy->id;
        return true;
    }
    return false;
}

/* 0x9be16 picks the first owned fort or area with importance >32. When
 * it contains enemy strength, reserves may respond from other territories. */
static void priority_territory(zmap_t *map, mobj_t *const *units, int count) {
    zai_t *ai = &map->ai;
    for (int t = 0; t < map->territory_count; ++t) {
        if (!owned(ai, t) || (!(ai->territories[t].flags & 1) && map->territories[t].importance <= 32)) continue;
        if (ai->territories[t].strength[0])
            for (int u = 0; u < count; ++u) {
                mobj_t *enemy = units[u];
                if (!soldier(enemy) || enemy->team != 0 || at_unit(map, enemy) != t) continue;
                for (int i = 0; i < Z_AI_TASKS; ++i) {
                    zaitask_t *task = &ai->tasks[i];
                    if (task->unit && !(task->flags & MOBILE)) respond(ai, task, enemy, units, count);
                }
                break;
            }
        return;
    }
}

/* 0x9bf90 responds to the first owned territory with a live attacker.
 * It consumes the alert only when an eligible local squad takes it. */
static void protect_buildings(zmap_t *map, mobj_t *const *units, int count) {
    zai_t *ai = &map->ai;
    for (int t = 0; t < map->territory_count; ++t) {
        zai_territory_t *area = &ai->territories[t];
        if (!area->attacker || !owned(ai, t)) continue;
        mobj_t *enemy = P_MobjById(area->attacker);
        if (!soldier(enemy) || enemy->team != 0) { area->attacker = 0; continue; }
        for (int i = 0; i < Z_AI_TASKS; ++i) {
            zaitask_t *task = &ai->tasks[i];
            if (task->unit && task->current == t && respond(ai, task, enemy, units, count)) {
                area->attacker = 0;
                break;
            }
        }
        return;
    }
}

/* 0x9cc83 visits 20 native slots per heavy pass. Manufactured reserves
 * outside their own land fall back to the nearest owned territory. */
static void return_strays(zmap_t *map, mobj_t *const *units, int count) {
    zai_t *ai = &map->ai;
    int u = 0, slots = 0;
    for (; u < count && slots < 20; ++u) {
        mobj_t *unit = units[u];
        if (unit->id <= ai->stray_cursor || Z_UnitType(unit) < 0) continue;
        ai->stray_cursor = unit->id;
        slots++;
        if (!soldier(unit) || unit->team != CPU || !unit->z.manufactured || unit->z.service ||
            unit->type_id == MT_UNIT_CRANE || unit->type_id == MT_UNIT_FIRE_TRUCK || live(unit->attack.target)) continue;
        for (int i = 0; i < Z_AI_TASKS; ++i) {
            zaitask_t *task = &ai->tasks[i];
            if (!reserve(task) || task->unit != unit->id || task->pending || (task->flags & RECAPTURE)) continue;
            int t = task->current;
            if (t < 0 || owned(ai, t)) break;
            int keep = 16 * map->computer.territory_weight * map->territories[t].importance;
            if (ai->territories[t].lockout && (int16_t)(surplus(ai, t) - keep) >= 0) break;
            int home = nearest_own(map, CPU, t, Z_AITopLeft(unit));
            if (home >= 0 && (!P_HasMoveOrder(unit) || Z_AITerritory(map, unit->movement.goal) != home)) {
                send(map, task, home);
                if (task->home == t) task->home = (int16_t)home;
            }
            break;
        }
    }
    if (u == count) ai->stray_cursor = 0;
}

/* 0x9bba5 resolves an offensive group's target territory. A target beyond
 * CPU land with no capture lockout makes the group return to its home. */
static void check_unit_targets(zmap_t *map, mobj_t *const *units, int count) {
    zai_t *ai = &map->ai;
    for (int i = 0; i < Z_AI_TASKS; ++i) {
        zaitask_t *task = &ai->tasks[i];
        mobj_t *unit = P_MobjById(task->unit);
        if (!unit || !(task->flags & MOBILE) || task->home < 0 || unit->z.service) continue;
        const mobj_t *enemy = unit->attack.target;
        int t = soldier(enemy) ? at_unit(map, enemy) :
            P_HasMoveOrder(unit) ? Z_AITerritory(map, unit->movement.goal) : -1;
        if (t < 0 || owned(ai, t) || ai->territories[t].lockout || !send(map, task, task->home)) continue;
        mobj_t *group[MEMBERS];
        int n = members(task, units, count, group);
        for (int j = 0; j < n; ++j) group[j]->attack.target = NULL;
    }
}

void Z_AIPlaceGun(mobj_t *factory) {
    static const char order[] =
        "ABCDEFEDAFCBABEFCDABEFCDABEFCDEDAFCBABEFCDABEFCDABEFCDABEFCD"
        "ABEFCDABEFCDABEFCDABEFCDABEFCDABEFCDABEFCDABEFCDABEFCDABEFCD"
        "ABEFCDABEFCDABEFCDABEFCDABEFCDABEFCDABEFCDABEFCDABEFCDABEFCD"
        "ABEFCDABEFCDABEFCDABEFCDABEFCDABEFCDABEFCDABEFCDABEFCDABEFCD"
        "ABEFCD";
    /* 0x11d299 remaps the letters to 0x145088's six fort mounts.
     * These cells select the same six sectors in Z_GunPosition. */
    static const ivec2_t sector[] = {{1,0},{5,0},{1,3},{5,3},{1,6},{5,6}};
    static const uint8_t markers[] = {0x2d, 0x35, 0x36, 0x37};
    const StaticProductDefinition *stored = Z_StoredGun(factory, 0);
    zmap_t *map = level.native_data;
    if (!stored || !map) return;
    int type = stored->product_type;
    irect_t base = P_MobjCells(factory);
    int territory = Z_AITerritory(map, fixed2_from_cell((ivec2_t){base.x,base.y}));
    if (territory < 0 || territory >= map->territory_count) return;
    if ((map->ai.territories[territory].flags & 1) &&
        map->number >= 0 && map->number < (int)(sizeof(order) - 1) / 6) {
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *fort = (mobj_t *)th;
            if (th->function != P_MobjThinker || fort->type_id != MT_FORT || !live(fort) || fort->team != factory->team) continue;
            irect_t cells = P_MobjCells(fort);
            if (Z_AITerritory(map, fixed2_from_cell((ivec2_t){cells.x,cells.y})) != territory) continue;
            for (int i = 0; i < 6; ++i) {
                int slot = order[map->number * 6 + i] - 'A';
                ivec2_t at = ivec2_add((ivec2_t){cells.x,cells.y}, sector[slot]);
                if (Z_PlaceGun(factory, type, at)) return;
            }
            break;
        }
    }
    const zterritory_t *zone = &map->territories[territory];
    ivec2_t chosen = {-1,-1};
    for (size_t i = 0; i < sizeof(markers); ++i) {
        bool found = false;
        for (int y = zone->min.y; y < zone->max.y && !found; ++y)
            for (int x = zone->min.x; x < zone->max.x; ++x) {
                if (!L_Contains(&level, x, y)) continue;
                int index = y * Z_MAP_STRIDE + x;
                if ((map->terrain[index] & 0x7f) != markers[i]) continue;
                fixed3_t position;
                if (!Z_AIGunPosition(factory, type, (ivec2_t){x,y}, &position)) continue;
                map->ground_attr[index] = map->terrain[index] &= 0x80;
                level.blocked[L_Index(&level, x, y)] = 0;
                chosen = (ivec2_t){x,y}; found = true;
                break;
            }
        if (found && (Z_NextRandom(&map->ai.seed) & 127) < map->ai.intelligence) break;
    }
    if (chosen.x >= 0) Z_PlaceAIGun(factory, type, chosen);
}
static void run_factories(mobj_t *const *units, int count) {
    for (int i = 0; i < count; ++i) {
        mobj_t *factory = units[i];
        if (factory->owner != CPU || !Z_Producer(factory)) continue;
        if (Z_GunReady(factory)) Z_AIPlaceGun(factory);
        if (!Z_ProductInProduction(factory)) {
            const StaticProductDefinition *product = Z_AIProduct(factory);
            if (product) G_QueueProduct(factory, product);
        }
    }
}

static void z_ai_tactics(level_t *level_map, int owner, mobj_t *const *units, int count) {
    zmap_t *map = level_map->native_data;
    if (!map || owner != CPU || netgame) return;
    zai_t *ai = &map->ai;
    if (!ai->initialized) {
        ai->initialized = true; ai->last_tic = leveltime; ai->phase = 1;
        ai->seed = 0x3039; /* Retail network seed; deterministic front-end policy. */
        ai->intelligence = map->computer.intelligence;
        for (int t = 0; t < map->territory_count; ++t) {
            ai->territories[t].flags = map->territories[t].options & 0xe0;
            for (int f = 0; f < map->fort_count; ++f)
                if (Z_AITerritory(map, pixel_point(map->forts[f])) == t) ai->territories[t].flags |= 1 << f;
            ai->territories[t].owner = (uint8_t)map->territories[t].team;
        }
    }
    sync_tasks(map, units, count);
    ai->fraction += (leveltime - ai->last_tic) * Z_NATIVE_HZ;
    ai->last_tic = leveltime;
    for (; ai->fraction >= RTS_TICRATE; ai->fraction -= RTS_TICRATE) {
        ai->clock++;
        /* 0x9cb40 counts every lockout down once a frame. */
        for (int t = 0; t < map->territory_count; ++t)
            if (ai->territories[t].lockout) ai->territories[t].lockout--;
        local_orders(map, units, count);
        observations(map, units, count);
        if (--ai->delay < 0) {
            ai->delay = 2 * (127 - ai->intelligence);
            ai->phase ^= 1;
            ai->order_tail = 0;
            observations(map, units, count);
            for (int i = 0; i < Z_AI_TASKS; ++i) {
                zaitask_t *task = &ai->tasks[i];
                if (!task->unit) continue;
                mobj_t *group[MEMBERS];
                int n = members(task, units, count, group);
                bool busy = task->pending;
                for (int j = 0; j < n; ++j)
                    busy |= P_HasMoveOrder(group[j]) || group[j]->z.board || group[j]->z.service || live(group[j]->attack.target);
                /* 0x991c4 spares waiting (staged) forces. */
                if ((task->flags & USABLE) && ((task->flags & MOBILE) || task->task == BUILDING_SUPPORT) &&
                    task->task != IDLE && task->task != VEHICLE && task->task != STAGED &&
                    task->task != CRANE_WORK &&
                    !busy && ++task->idle >= 3) {
                    clear(task); task->idle = 0;
                }
            }
            split_army(map, units, count);
            /* 0x98d58, in the native order of its passes. */
            attack_territories(map);
            check_forces(map);
            release_garrisons(map);
            advance(map);
            hold_territories(map);
            seek_vehicles(map, units, count);
            to_frontier(map, units, count);
            /* The rest of the 0x9b1e3 dispatcher. */
            send_cranes(map, units, count, false);
            send_cranes(map, units, count, true);
            service_returns(map, false);
            priority_territory(map, units, count);
            protect_buildings(map, units, count);
            recapture(map);
            return_strays(map, units, count);
            go_home(map);
            send_damaged_vehicles(map, units, count);
            check_unit_targets(map, units, count);
            service_returns(map, true);
            defend(map, units, count);
            run_factories(units, count);
        }
        execute_orders(ai, units, count);
    }
}
static int z_ai_level(const level_t *map, int owner) {
    return !netgame && owner == CPU ? P_AiLevelNonHuman(map, owner) : AI_LEVEL_NONE;
}
static const AiGameInterface z_ai_interface = {
    .name = "z", .features = AI_FEATURE_ATTACK, .think_interval_tics = 1,
    .player_level = z_ai_level, .tactics = z_ai_tactics,
};
const AiGameInterface *G_AiInterface(void) { return &z_ai_interface; }
