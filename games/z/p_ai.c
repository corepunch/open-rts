#include "z_local.h"
#include "info.h"

#include <string.h>

/* Native single-player scoring, force allocation and offensive orders.
 * See docs/Z_EXE_FINDINGS.md for the unported repair/defense branches.
 * Task records refer to squads; thinker-owned mobjs are the only units. */
enum { CPU = 1, NATIVE_HZ = 70, USABLE = 8, MOBILE = 16, MEMBERS = 21 };

static bool live(const mobj_t *mo) { return mo && !mo->remove && mo->hp > 0; }
static bool soldier(const mobj_t *mo) {
    return live(mo) && mo->team < Z_NEUTRAL && Z_UnitType(mo) >= 0;
}
static bool same_squad(const mobj_t *a, const mobj_t *b) {
    return a == b || (a->z.squad && a->z.squad == b->z.squad && a->team == b->team);
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
            (task->squad ? units[i]->z.squad == task->squad : units[i]->id == task->unit))
            out[n++] = units[i];
    return n;
}
static bool reachable(const mobj_t *mo, fixed2_t goal) {
    int cls = P_MobjMoveClass(mo);
    return (mo->traits & MF_MOBILE) &&
        P_NavReachable(&level, cls, P_NavNodeAt(&level, cls, fixed3_xy(mo->core.position)),
                       P_NavNodeAt(&level, cls, goal));
}
static bool allowed(const mobj_t *mo, uint8_t flags) {
    if ((flags & 32) && !Z_IsRobot(Z_UnitType(mo))) return false;
    if (!(flags & 64)) return true;
    if (mo->type_id == MT_UNIT_JEEP || mo->type_id == MT_UNIT_APC) return false;
    return !Z_IsRobot(Z_UnitType(mo)) || mo->type_id == MT_UNIT_TOUGH || mo->z.grenades;
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
    task->execute_at = ai->clock + 2 * (127 - ai->intelligence) + ai->order_tail;
    ai->order_tail += 127 - ai->intelligence;
    task->pending = true;
}

/* 0x9d42c/0x9d4f4: fort staging coordinates and the eight authored
 * 48-pixel offsets, starting at a random offset and testing each in turn. */
static bool send(zmap_t *map, zaitask_t *task, int territory) {
    if (territory < 0 || territory >= map->territory_count) return false;
    mobj_t *mo = P_MobjById(task->unit);
    if (!live(mo) || !allowed(mo, map->ai.territories[territory].flags)) return false;
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
        if (group[0]->traits & MF_MOBILE) task->flags |= USABLE;
        bool moving = false, fighting = false;
        mobj_t *enemy = NULL;
        for (int j = 0; j < n; ++j) {
            moving |= P_HasMoveOrder(group[j]) || group[j]->z.board;
            if (live(group[j]->attack.target)) enemy = group[j]->attack.target;
        }
        for (int j = 0; enemy && j < n; ++j)
            if (P_CanTarget(group[j], enemy)) {
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
        bool found = false;
        zaitask_t *free = NULL;
        for (int i = 0; i < Z_AI_TASKS; ++i) {
            zaitask_t *task = &ai->tasks[i];
            if (!task->unit && !free) free = task;
            if (task->unit == mo->id || (mo->z.squad && task->squad == mo->z.squad)) found = true;
        }
        if (found || !free) continue;
        *free = (zaitask_t){.unit = mo->id,.squad = mo->z.squad,.task = -1,
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
    for (int t = 0; t < map->territory_count; ++t) {
        zai_territory_t *out = &ai->territories[t];
        memset(out->strength, 0, sizeof(out->strength));
        out->owner = (uint8_t)map->territories[t].team;
        out->known = 0;
    }
    for (int u = 0; u < count; ++u) {
        const mobj_t *mo = units[u];
        if (live(mo) && mo->type_id == MT_FLAG && mo->z.territory)
            ai->territories[mo->z.territory - 1].owner = mo->team;
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
        if (!soldier(mo) || mo->type_id == MT_UNIT_CRANE || mo->type_id == MT_UNIT_FIRE_TRUCK) continue;
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
        if (!live(mo) || mo->type_id == MT_UNIT_CRANE || mo->type_id == MT_UNIT_FIRE_TRUCK) continue;
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
        int t = ranked[r].territory, n = 0, sum = 0;
        struct { zaitask_t *task; int64_t distance; } pool[Z_AI_TASKS];
        fixed2_t goal = fixed2_cell_center(map->territories[t].flag);
        for (int i = 0; i < Z_AI_TASKS; ++i) {
            zaitask_t *task = &ai->tasks[i];
            mobj_t *mo = P_MobjById(task->unit);
            if (!live(mo) || (task->flags & (USABLE | MOBILE)) != (USABLE | MOBILE) ||
                task->task != -1 || task->pending || !allowed(mo, ai->territories[t].flags) || !reachable(mo, goal)) continue;
            int64_t distance = ivec2_distance_squared64(Z_AITopLeft(mo), ivec2_scale(map->territories[t].flag, Z_TILE));
            int at = n++;
            while (at && pool[at - 1].distance > distance) { pool[at] = pool[at - 1]; at--; }
            pool[at].task = task; pool[at].distance = distance;
        }
        int required = ai->territories[t].strength[0] ?
            2 * ai->territories[t].strength[0] + ai->territories[t].value : 1;
        int needed = 0;
        while (needed < n && sum < required) sum += pool[needed++].task->strength;
        if (!needed || sum < required) continue;
        for (int i = 0; i < needed; ++i)
            if (send(map, pool[i].task, t)) {
                pool[i].task->task = 0;
                pool[i].task->target = pool[i].task->destination = (int16_t)t;
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
        if (!live(mo) || task->current < 0) continue;
        zai_territory_t *t = &ai->territories[task->current];
        /* 0x9a49a: a mobile force in the second fort territory enters it. */
        if ((t->flags & 2) && (task->flags & (USABLE | MOBILE)) == (USABLE | MOBILE) &&
            task->task != 5 && !task->pending && map->fort_count > 1) {
            fixed2_t goal = pixel_point(ivec2_add(map->forts[1], (ivec2_t){80,72}));
            if (reachable(mo, goal)) {
                queue(ai, task, goal, 0);
                task->task = 5; task->target = task->current;
            }
        }
        if (--task->retry >= 0) continue;
        task->retry = 0;
        if (t->owner == CPU || (t->flags & 3) || mo->type_id == MT_UNIT_SNIPER ||
            !(task->flags & USABLE) || task->boarding || live(mo->attack.target)) continue;
        fixed2_t goal = fixed2_cell_center(map->territories[task->current].flag);
        if (ivec2_distance_squared64(Z_AITopLeft(mo), ivec2_scale(map->territories[task->current].flag, Z_TILE)) < 16384 &&
            reachable(mo, goal)) {
            /* The native flag correction is immediate (0x7ba9a). */
            mobj_t *group[MEMBERS];
            int n = members(task, units, count, group);
            P_MoveUnitsAt(&level, group, n, goal);
            P_MoveUnitTo(&level, mo, goal);
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
            if (task->boarding) Z_BoardOrder(group, n, P_MobjById(task->boarding));
            else {
                P_MoveUnitsAt(&level, group, n, task->order);
                P_MoveUnitTo(&level, group[0], task->order);
            }
        }
        task->pending = false;
    }
}

static void place_gun(mobj_t *factory) {
    int type = Z_ProductInProduction(factory)->product_type;
    irect_t base = P_MobjCells(factory);
    if (factory->type_id == MT_FORT)
        for (int y = base.y; y < base.y + 9 && Z_GunReady(factory); ++y)
            for (int x = base.x; x < base.x + 9 && Z_GunReady(factory); ++x)
                Z_PlaceGun(factory, type, (ivec2_t){x,y});
    for (int y = 0; y < level.height && Z_GunReady(factory); ++y)
        for (int x = 0; x < level.width && Z_GunReady(factory); ++x)
            Z_PlaceGun(factory, type, (ivec2_t){x,y});
}
static void run_factories(mobj_t *const *units, int count) {
    for (int i = 0; i < count; ++i) {
        mobj_t *factory = units[i];
        if (factory->owner != CPU || !Z_Producer(factory)) continue;
        if (Z_GunReady(factory)) place_gun(factory);
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
        }
    }
    sync_tasks(map, units, count);
    ai->fraction += (leveltime - ai->last_tic) * NATIVE_HZ;
    ai->last_tic = leveltime;
    for (; ai->fraction >= RTS_TICRATE; ai->fraction -= RTS_TICRATE) {
        ai->clock++;
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
                    busy |= P_HasMoveOrder(group[j]) || group[j]->z.board || live(group[j]->attack.target);
                if (task->task != -1 && task->task != 5 && !busy && ++task->idle >= 3) {
                    task->task = -1; task->target = task->destination = -1; task->idle = 0;
                }
            }
            split_army(map, units, count);
            attack_territories(map);
            seek_vehicles(map, units, count);
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
