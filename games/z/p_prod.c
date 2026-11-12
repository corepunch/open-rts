#include "z_local.h"
#include "info.h"

#include <string.h>
#include <limits.h>

/* Production: forts and factories make units on timers (products.inc).
 * Z has no money, so a product costs nothing and a factory, once
 * started, keeps making the same unit until it is stopped. A team's
 * factories work faster the more territories it holds. */

static const struct { int group, type, stock; } catalog[] = {
#define Z_PRODUCT(group, type, stock) {group, MT_UNIT_##type, stock},
#include "products.inc"
#undef Z_PRODUCT
};
enum { CATALOG = (int)(sizeof(catalog) / sizeof(*catalog)) };
_Static_assert(RTS_MODEL_MAX_PRODUCT_PREREQUISITES >= 3, "three makers per product");

static StaticProductDefinition products[CATALOG];
static bool built;

int Z_ProductId(int type) { return type - MT_UNITS + 1; }

static void build(void) {
    if (built) return;
    for (int i = 0; i < CATALOG; ++i) {
        const mobjtype_t *actor = P_ActorType((uint16_t)catalog[i].type);
        StaticProductDefinition *p = &products[i];
        *p = (StaticProductDefinition){
            .row_id = i, .ui_id = Z_ProductId(catalog[i].type), .label = actor ? actor->name : "?",
            .product_class = RTS_PRODUCT_UNIT, .product_type = catalog[i].type,
            .maker_count = 3, .makers = {MT_FORT, MT_ROBOT_FACTORY, MT_TANK_FACTORY},
        };
    }
    built = true;
}

static bool live(const mobj_t *mo) { return mo && !mo->remove && mo->hp > 0; }

/* Zod's AddBuildingQueue accepts upcoming products independently of the
 * one being built; the retail selection still gates each product. */
bool G_QueueLocksProduct(const StaticProductDefinition *product) { (void)product; return false; }

/* A fort or factory of a side; a fort that is falling or fell (p_fort.c
 * counts the fall in z.hold) makes nothing more. */
bool Z_Producer(const mobj_t *mo) {
    if (!live(mo) || mo->team >= Z_NEUTRAL) return false;
    if (mo->type_id == MT_FORT)
        return !mo->z.hold && mo->core.state_id != S_FORT_WRECK && mo->core.state_id != S_FORT_CPU_WRECK;
    return mo->type_id == MT_ROBOT_FACTORY || mo->type_id == MT_TANK_FACTORY;
}

int Z_ProductsFor(const mobj_t *producer, const StaticProductDefinition **out, int max) {
    build();
    int count = 0;
    for (int i = 0; producer && i < CATALOG && count < max; ++i)
        if (G_ModelProducerHasTech(producer, &products[i])) out[count++] = &products[i];
    return count;
}

/* Initial selectors 0x9e275/0x9e2b5/0x9e2f5. 0x9e33a walks increasing
 * technology, ascending group ID, exchanging the last pair if the new
 * group's stock is lower. Forts then sort the whole list by stock. */
static const StaticProductDefinition *ai_product(const mobj_t *producer, bool guns) {
    const zmap_t *map = level.native_data;
    int list[CATALOG], count = 0, human = 0, cpu = 0;
    static const uint8_t robots[] = {0,3,15,16,17,18};
    static const uint8_t tanks[] = {1,7,8,9,11};
    static const uint8_t cannons[] = {2,4,5,6};
    const uint8_t *classes = guns ? cannons : producer->type_id == MT_TANK_FACTORY ? tanks : robots;
    int class_count = guns ? sizeof(cannons) : producer->type_id == MT_TANK_FACTORY ? sizeof(tanks) : sizeof(robots);
    int field = guns && producer->type_id == MT_FORT ? 74 : producer->type_id == MT_TANK_FACTORY ? 73 : producer->type_id == MT_ROBOT_FACTORY ? 74 : 75;
    build();
    for (unsigned tech = 1; tech * 1000 <= producer->z.rating && tech <= 255; ++tech)
        for (int group = 0; group < 32; ++group)
            for (int i = 0; i < CATALOG; ++i) {
                if (catalog[i].group != group || !G_ModelProducerHasTech(producer, &products[i]) ||
                    map->roster.bytes[18000 + group * 82 + field] != tech) continue;
                int t = Z_GroupType(map, group, 0), cls = map->roster.bytes[22000 + t * 42 + 1];
                bool allowed = false;
                for (int c = 0; c < class_count; ++c) allowed |= cls == classes[c];
                if (!guns && producer->type_id == MT_FORT)
                    for (size_t c = 0; c < sizeof(tanks); ++c) allowed |= cls == tanks[c];
                if (!allowed) continue;
                list[count++] = i;
                if (count > 1 && catalog[list[count - 1]].stock < catalog[list[count - 2]].stock) {
                    int swap = list[count - 1]; list[count - 1] = list[count - 2]; list[count - 2] = swap;
                }
            }
    if (!count) return NULL;
    if (!guns && producer->type_id == MT_FORT)
        for (int i = 1; i < count; ++i)
            for (int j = i; j && catalog[list[j]].stock < catalog[list[j - 1]].stock; --j) {
                int swap = list[j]; list[j] = list[j - 1]; list[j - 1] = swap;
            }
    for (int t = 0; t < map->territory_count; ++t) {
        human += map->ai.territories[t].strength[0];
        cpu += map->ai.territories[t].strength[1];
    }
    int index = human + cpu ? cpu * count / (human + cpu) : 0;
    index += index / 4;
    if (index >= count) index = count - 1;
    return &products[list[index]];
}

const StaticProductDefinition *Z_AIProduct(const mobj_t *producer) { return ai_product(producer, false); }

const StaticProductDefinition *Z_ProductInProduction(const mobj_t *producer) {
    if (!producer || !producer->production || producer->production->queue_count <= 0) return NULL;
    return G_ModelProductByClassType(NULL, producer->production->product_class,
                                     producer->production->product_type);
}

/* 0x42405 counts every active territory, even one without a flag.
 * Capturable territories borrow their current ownership from the flag. */
static int territory_counts(int held[Z_NEUTRAL + 1]) {
    const zmap_t *map = level.native_data;
    if (!map) return 0;
    for (int i = 0; i < map->territory_count; ++i) {
        int team = map->territories[i].team;
        held[team < Z_NEUTRAL ? team : Z_NEUTRAL]++;
    }
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function != P_MobjThinker || mo->type_id != MT_FLAG || mo->remove ||
            !mo->z.territory || mo->z.territory > map->territory_count) continue;
        int initial = map->territories[mo->z.territory - 1].team;
        held[initial < Z_NEUTRAL ? initial : Z_NEUTRAL]--;
        held[mo->team < Z_NEUTRAL ? mo->team : Z_NEUTRAL]++;
    }
    return map->territory_count;
}

int Z_TerritoriesHeld(int team, int *total) {
    int held[Z_NEUTRAL + 1] = {0};
    int all = territory_counts(held);
    if (total) *total = all;
    return team < 0 ? 0 : held[team < Z_NEUTRAL ? team : Z_NEUTRAL];
}

/* ── the catalog the engine reads ─────────────────────────────────────── */

int G_ModelGetProducts(const RtsGameModel *model, int owner, StaticProductDefinition *out, int max_products) {
    (void)model; (void)owner;
    build();
    int count = CATALOG < max_products ? CATALOG : max_products;
    if (!out || count < 0) return 0;
    memcpy(out, products, (size_t)count * sizeof(*out));
    return count;
}

const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *model, int ui_id) {
    (void)model;
    build();
    for (int i = 0; i < CATALOG; ++i)
        if (products[i].ui_id == ui_id) return &products[i];
    return NULL;
}

const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *model,
                                                         int product_class, int product_type) {
    (void)model;
    build();
    for (int i = 0; i < CATALOG; ++i)
        if ((int)products[i].product_class == product_class && products[i].product_type == product_type)
            return &products[i];
    return NULL;
}

/* Available while the owner has a maker of it standing. */
bool G_ModelProductAvailable(const RtsGameModel *model, int owner, const StaticProductDefinition *product) {
    (void)model;
    if (!product) return false;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function == P_MobjThinker && mo->owner == owner && G_ModelProducerHasTech(mo, product)) return true;
    }
    return false;
}

uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *product) {
    return product ? (uint16_t)product->product_type : 0;
}

int G_ModelBuildingFrameForProduct(const StaticProductDefinition *product) {
    (void)product;
    return 0;
}

int G_ModelBuildingStateForProduct(const gameinfo_t *game_info, const StaticProductDefinition *product) {
    (void)game_info; (void)product;
    return -1;
}

int G_ModelProductTrainingTimeMs(const StaticProductDefinition *product) {
    if (!product || product->row_id < 0 || product->row_id >= CATALOG ||
        catalog[product->row_id].type != product->product_type) return 0;
    int row = product->row_id;
    const zmap_t *map = level.native_data;
    return map && map->production_speed > 0 ?
        (catalog[row].stock * 221 / map->production_speed) * 1000 / 17 : 0;
}

int Z_ProductionRate(const mobj_t *producer, int held, int total) {
    int rate = total > 0 ? (100 * held / total) / 4 + 1 : 1;
    return producer->max_hp > 0 ? (100 * producer->hp / producer->max_hp) * rate / 100 : 0;
}

int Z_ProductTimeMs(const mobj_t *producer, const StaticProductDefinition *product) {
    if (!producer || !product) return 0;
    int time = Z_ProductInProduction(producer) == product ? producer->production->time_left_ms :
               G_ModelProductTrainingTimeMs(product);
    if (!time) return 0;
    int total;
    int held = Z_TerritoriesHeld(producer->team, &total);
    int rate = Z_ProductionRate(producer, held, total);
    return rate > 0 ? time / rate : INT_MAX;
}

static bool gun_product(const StaticProductDefinition *product) {
    const mobjtype_t *actor = product ? P_ActorType((uint16_t)product->product_type) : NULL;
    return actor && !(actor->traits & MF_MOBILE);
}

/* 0x26b82 counts guns, stored guns and other factories' current gun
 * production in the territory; 0x26ad0 rejects a count of four or more. */
static bool gun_capacity(const mobj_t *producer) {
    const zmap_t *map = level.native_data;
    if (!map || !producer || producer->z.gun_count > Z_GUN_STOCK) return false;
    irect_t base = P_MobjCells(producer);
    int territory = Z_AITerritory(map, fixed2_from_cell((ivec2_t){base.x, base.y}));
    if (territory < 0) return false;
    int count = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function != P_MobjThinker || !live(mo)) continue;
        int t = Z_UnitType(mo);
        if (t >= 0 && mo->team == producer->team && !Z_IsRobot(t) && mo->info && !(mo->info->traits & MF_MOBILE)) {
            if (Z_AITerritory(map, fixed3_xy(mo->core.position)) == territory) ++count;
        } else if (Z_Producer(mo)) {
            irect_t cells = P_MobjCells(mo);
            if (Z_AITerritory(map, fixed2_from_cell((ivec2_t){cells.x, cells.y})) != territory) continue;
            if (mo->z.gun_count > Z_GUN_STOCK) return false;
            count += mo->z.gun_count;
            if (mo != producer && gun_product(Z_ProductInProduction(mo))) ++count;
        }
        if (count >= Z_GUN_STOCK) return false;
    }
    return true;
}

/* Subsequent CPU selections (0x9de4a/0x9de9b/0x9def5) can prefer a
 * gun when local reserve value and the native two capacity checks allow it. */
static const StaticProductDefinition *next_ai_product(const mobj_t *producer) {
    const zmap_t *map = level.native_data;
    irect_t base = P_MobjCells(producer);
    int territory = map ? Z_AITerritory(map, fixed2_from_cell((ivec2_t){base.x, base.y})) : -1;
    int deployed = 0;
    if (territory < 0 || (producer->type_id != MT_TANK_FACTORY && producer->z.rating <= 1000) ||
        Z_AIReserveValue(&map->ai, territory) < 500 || !gun_capacity(producer)) return Z_AIProduct(producer);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *mo = (mobj_t *)th;
        int t = Z_UnitType(mo);
        if (live(mo) && mo->team == producer->team && t >= 0 && !Z_IsRobot(t) &&
            mo->info && !(mo->info->traits & MF_MOBILE) &&
            Z_AITerritory(map, fixed3_xy(mo->core.position)) == territory) ++deployed;
    }
    int limit = producer->type_id == MT_FORT ? 4 : 2;
    const StaticProductDefinition *gun = deployed < limit ? ai_product(producer, true) : NULL;
    return gun ? gun : Z_AIProduct(producer);
}

bool G_ModelStartProductionRelease(RtsGameModel *model, mobj_t *producer,
                                   const StaticProductDefinition *product, uint16_t actor_id) {
    (void)model; (void)actor_id;
    if (!gun_product(product)) return false;
    if (producer->z.gun_count >= Z_GUN_STOCK || !gun_capacity(producer)) {
        producer->production->release_active = true;
        producer->production->release_ready = false;
        producer->production->time_left_ms = 0;
        return true;
    }
    memmove(producer->z.guns + 1, producer->z.guns, producer->z.gun_count);
    producer->z.guns[0] = (uint8_t)(product->product_type - MT_UNITS + 1);
    producer->z.gun_count++;
    bool computer = producer->team == 1 && !netgame;
    if (producer->production->queue_count == 1 && (computer || gun_capacity(producer))) {
        const StaticProductDefinition *next = computer ? Z_AIProduct(producer) : product;
        if (next) G_QueueProduct(producer, next);
    }
    G_RemoveProductionAt(producer, 0);
    return true;
}

/* Guns are stored by the producer until a placement order, never released
 * through its door (ZED.EXE 0x4209f / 0x430b2). */
bool Z_GunReady(const mobj_t *producer) {
    return Z_Producer(producer) && producer->z.gun_count > 0 && producer->z.gun_count <= Z_GUN_STOCK;
}

const StaticProductDefinition *Z_StoredGun(const mobj_t *producer, int index) {
    if (!Z_GunReady(producer) || index < 0 || index >= producer->z.gun_count) return NULL;
    return G_ModelProductByClassType(NULL, RTS_PRODUCT_UNIT, MT_UNITS + producer->z.guns[index] - 1);
}

bool Z_PlaceGun(mobj_t *producer, int type, ivec2_t cell) {
    fixed3_t position;
    int index = 0;
    while (index < (Z_GunReady(producer) ? producer->z.gun_count : 0) &&
           MT_UNITS + producer->z.guns[index] - 1 != type) ++index;
    if (!Z_GunReady(producer) || index >= producer->z.gun_count ||
        !Z_GunPosition(producer, type, cell, &position)) return false;
    mobj_t *gun = P_SpawnMobj(position, (uint16_t)type);
    if (!gun) return false;
    Z_SetTeam(gun, producer->team);
    G_UnitProduced(gun, NULL);
    producer->z.gun_count--;
    memmove(producer->z.guns + index, producer->z.guns + index + 1, producer->z.gun_count - index);
    producer->z.guns[producer->z.gun_count] = 0;
    const StaticProductDefinition *pending = Z_ProductInProduction(producer);
    if (pending && producer->production->release_active)
        G_ModelStartProductionRelease(NULL, producer, pending, producer->production->actor_id);
    return true;
}

/* Another unit already stands at `at`. */
static bool occupied(fixed2_t at, const mobj_t *self) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function != P_MobjThinker || mo == self || !live(mo) || Z_UnitType(mo) < 0) continue;
        if (fixed2_distance_squared64(fixed3_xy(mo->core.position), at) < fixed_sq64(FIXED_ONE * 3 / 4))
            return true;
    }
    return false;
}

/* A new unit leaves by the door: a factory's is below it, a fort's is its
 * territory's flag cell, which the levels put at the gate. It stands on
 * the nearest free cell around the building's footprint. */
bool G_ModelSpecialReleaseSpawnPoint(const RtsGameModel *model, const mobj_t *producer,
                                     const StaticProductDefinition *product,
                                     const mobj_t *new_unit, fixed2_t *out) {
    (void)model; (void)product;
    if (!producer || !new_unit || !out || !Z_Producer(producer)) return false;
    const zmap_t *map = level.native_data;
    irect_t cells = P_MobjCells(producer);
    fixed2_t door = fixed2_cell_center((ivec2_t){cells.x + cells.w / 2, cells.y + cells.h});
    if (producer->type_id == MT_FORT && map) {
        ivec2_t cell = fixed2_cell(fixed3_xy(producer->core.position));
        for (int i = 0; i < map->territory_count; ++i) {
            const zterritory_t *t = &map->territories[i];
            if (cell.x >= t->min.x && cell.x < t->max.x && cell.y >= t->min.y && cell.y < t->max.y) {
                door = fixed2_cell_center(t->flag);
                break;
            }
        }
    }
    int cls = P_MobjMoveClass(new_unit);
    ivec2_t ring[128];
    int count = P_NavRing(&level, cls, (ivec2_t){cells.x, cells.y}, (isize2_t){cells.w, cells.h}, ring, 128);
    bool found = false;
    int64_t best = 0;
    for (int pass = 0; pass < 2 && !found; ++pass)
        for (int i = 0; i < count; ++i) {
            fixed2_t at = P_NavNodePoint(&level, cls, ring[i]);
            ivec2_t cell = fixed2_cell(at);
            if (L_MoveSpeed(&level, cls, cell.x, cell.y) <= 0 || (pass == 0 && occupied(at, new_unit))) continue;
            int64_t d2 = fixed2_distance_squared64(at, door);
            if (found && d2 >= best) continue;
            *out = at;
            best = d2;
            found = true;
        }
    return found;
}

void G_ModelBuildUIScript(const RtsGameModel *model, const RtsRenderSnapshot *snapshot,
                          char *dst, size_t dst_size) {
    (void)model; (void)snapshot;
    if (dst && dst_size) dst[0] = '\0';
}

bool G_PlayerBuildProduct(mobj_t *producer, const StaticProductDefinition *product) {
    return G_QueueProduct(producer, product);
}

bool G_ModelProducerHasTech(const mobj_t *producer, const StaticProductDefinition *product) {
    const zmap_t *map = level.native_data;
    if (!map || !Z_Producer(producer) || !product || product->row_id < 0 || product->row_id >= CATALOG ||
        product->product_class != RTS_PRODUCT_UNIT || catalog[product->row_id].type != product->product_type)
        return false;
    int group = catalog[product->row_id].group;
    int t = Z_GroupType(map, group, 0);
    if (t < 0 || MT_UNITS + t != product->product_type) return false;
    size_t weapon = 22000 + (size_t)t * 42;
    if (weapon + 42 > map->roster.size) return false;
    int kind = map->roster.bytes[weapon + 1];
    bool robot = kind == 0 || kind == 17 || kind == 18 || kind == 3 || kind == 15 || kind == 16;
    bool gun = kind == 2 || kind == 4 || kind == 5 || kind == 6;
    bool vehicle = kind == 1 || kind == 8 || kind == 7 || kind == 9 ||
                   kind == 11 || kind == 10 || kind == 12 || kind == 13;
    if ((!robot && !gun && !vehicle) || (gun && !gun_capacity(producer)) || (producer->type_id == MT_TANK_FACTORY && robot) ||
        (producer->type_id == MT_ROBOT_FACTORY && !robot && !gun)) return false;
    int field = producer->type_id == MT_TANK_FACTORY ? 73 : producer->type_id == MT_ROBOT_FACTORY ? 74 : 75;
    unsigned minimum = map->roster.bytes[18000 + group * 82 + field];
    return minimum && minimum * 1000u <= producer->z.rating;
}

int G_ModelRadarLevel(int owner) {
    (void)owner;
    return 2;
}

/* ── hooks ────────────────────────────────────────────────────────────── */

/* 0x42405 / 0x422a7: territory rate, then health percentage, for every
 * product. Compensate for the shared ticker's one millisecond per ms. */
void G_ProductionBegin(int elapsed_ms) {
    int held[Z_NEUTRAL + 1] = {0};
    int total = territory_counts(held);
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        production_t *queue = mo->production;
        if (th->function != P_MobjThinker || !Z_Producer(mo) || !queue || queue->queue_count <= 0) continue;
        if (queue->release_active) {
            const StaticProductDefinition *product = Z_ProductInProduction(mo);
            if (gun_product(product) && gun_capacity(mo))
                G_ModelStartProductionRelease(NULL, mo, product, queue->actor_id);
            continue;
        }
        int rate = Z_ProductionRate(mo, held[mo->team], total);
        queue->time_left_ms -= elapsed_ms * (rate - 1);
    }
}

/* A manufactured vehicle comes with its driver, and the factory starts
 * the next one of the same kind at once. */
void G_UnitProduced(mobj_t *unit, const mobj_t *producer) {
    int t = Z_UnitType(unit);
    Z_InitUnitPhase(unit);
    const zmap_t *map = level.native_data;
    int group = -1;
    for (int i = 0; map && i < CATALOG; ++i)
        if (unit->type_id == catalog[i].type) {
            group = catalog[i].group;
            unit->z.robot = (uint16_t)(Z_GroupRobot(map, catalog[i].group, 0) + 1);
        }
    if (t >= 0 && !Z_IsRobot(t)) unit->z.driver = (uint8_t)(MT_UNIT_GRUNT - MT_UNITS + 1);
    unit->z.manufactured = t >= 0 && unit->team == 1 && !netgame;
    unit->core.angle = P_PointToAngle(0, FIXED_ONE);
    if (producer && group >= 0 && Z_IsRobot(t)) {
        uint8_t squad = Z_AllocateSquad(unit->team);
        unit->z.squad = squad;
        unit->z.squad_slot = squad ? UINT8_MAX : 0;
        unit->z.formation = (uint8_t)Z_GroupFormation(map, group);
        ivec2_t origin = ivec2_sub(Z_BlastAnchor(unit), (ivec2_t){8,8});
        for (int member = 1; squad && member < Z_CREW_MAX; ++member) {
            int robot = Z_GroupRobot(map, group, member);
            int type = Z_GroupType(map, group, member);
            if (robot < 0 || type < 0) break;
            if (!Z_IsRobot(type)) break;
            fixed2_t centre;
            if (!Z_UnitCentre(map, type, origin, &centre)) break;
            mobj_t *follower = P_SpawnMobj(fixed3_from_fixed2(centre, unit->core.position.z), MT_UNITS + type);
            if (!follower) break;
            Z_InitUnitPhase(follower);
            Z_SetTeam(follower, unit->team);
            follower->z.robot = (uint16_t)(robot + 1);
            follower->z.squad = squad;
            follower->z.squad_slot = (uint8_t)member;
            follower->z.home = unit->z.home;
            follower->z.manufactured = unit->z.manufactured;
            follower->core.angle = unit->core.angle;
        }
    }
    /* Zod appends a factory's rally route to each new mobile unit. */
    if ((unit->traits & MF_MOBILE) && producer) Z_RouteProduced(unit, producer);
    if (unit->z.squad_slot == UINT8_MAX) unit->z.formation_active = P_HasMoveOrder(unit);
    mobj_t *factory = producer ? P_MobjById(producer->id) : NULL;
    const StaticProductDefinition *again = Z_ProductInProduction(factory);
    if (again && factory->production->queue_count == 1) {
        const StaticProductDefinition *next = factory->team == 1 && !netgame ? next_ai_product(factory) : again;
        if (next) G_QueueProduct(factory, next);
    }
}
