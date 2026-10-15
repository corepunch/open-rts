#include "z_local.h"
#include "info.h"

#include <string.h>

/* Production: forts and factories make units on timers (products.inc).
 * Z has no money, so a product costs nothing and a factory, once
 * started, keeps making the same unit until it is stopped. A team's
 * factories work faster the more territories it holds. */

enum { ZM_FORT = 1, ZM_ROBOTS = 2, ZM_VEHICLES = 4 };

static const struct { int type; unsigned makers; int seconds; } catalog[] = {
#define Z_PRODUCT(type, makers, seconds) {MT_UNIT_##type, makers, seconds},
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
        };
        if (catalog[i].makers & ZM_FORT) p->makers[p->maker_count++] = MT_FORT;
        if (catalog[i].makers & ZM_ROBOTS) p->makers[p->maker_count++] = MT_ROBOT_FACTORY;
        if (catalog[i].makers & ZM_VEHICLES) p->makers[p->maker_count++] = MT_TANK_FACTORY;
    }
    built = true;
}

static bool live(const mobj_t *mo) { return mo && !mo->remove && mo->hp > 0; }

/* A fort or factory of a side; a fort that is falling or fell (p_fort.c
 * counts the fall in z.hold) makes nothing more. */
bool Z_Producer(const mobj_t *mo) {
    if (!live(mo) || mo->team >= Z_NEUTRAL) return false;
    if (mo->type_id == MT_FORT)
        return !mo->z.hold && mo->core.state_id != S_FORT_WRECK && mo->core.state_id != S_FORT_CPU_WRECK;
    return mo->type_id == MT_ROBOT_FACTORY || mo->type_id == MT_TANK_FACTORY;
}

static bool makes(const StaticProductDefinition *p, const mobj_t *producer) {
    for (int i = 0; i < p->maker_count; ++i)
        if (p->makers[i] == producer->type_id) return true;
    return false;
}

int Z_ProductsFor(const mobj_t *producer, const StaticProductDefinition **out, int max) {
    build();
    int count = 0;
    for (int i = 0; producer && i < CATALOG && count < max; ++i)
        if (makes(&products[i], producer)) out[count++] = &products[i];
    return count;
}

const StaticProductDefinition *Z_ProductInProduction(const mobj_t *producer) {
    if (!producer || !producer->production || producer->production->queue_count <= 0) return NULL;
    return G_ModelProductByClassType(NULL, producer->production->product_class,
                                     producer->production->product_type);
}

/* Flags standing for `team` (Z_NEUTRAL for nobody's), and all of them. */
int Z_TerritoriesHeld(int team, int *total) {
    int held = 0, all = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function != P_MobjThinker || mo->type_id != MT_FLAG || mo->remove || !mo->z.territory) continue;
        all++;
        if (mo->team == team || (team >= Z_NEUTRAL && mo->team >= Z_NEUTRAL)) held++;
    }
    if (total) *total = all;
    return held;
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
        if (th->function == P_MobjThinker && mo->owner == owner && Z_Producer(mo) && makes(product, mo)) return true;
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
    return catalog[product->row_id].seconds * 1000;
}

bool G_ModelStartProductionRelease(RtsGameModel *model, mobj_t *producer,
                                   const StaticProductDefinition *product, uint16_t actor_id) {
    (void)model; (void)producer; (void)product; (void)actor_id;
    return false;
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
    (void)producer; (void)product;
    return true;
}

int G_ModelRadarLevel(int owner) {
    (void)owner;
    return 2;
}

/* ── hooks ────────────────────────────────────────────────────────────── */

/* Every tic a team's factories gain, on top of the tic, its share of the
 * flags: holding them all builds twice as fast. */
void G_ProductionBegin(int elapsed_ms) {
    int held[Z_NEUTRAL + 1] = {0}, total = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function != P_MobjThinker || mo->type_id != MT_FLAG || mo->remove || !mo->z.territory) continue;
        held[mo->team < Z_NEUTRAL ? mo->team : Z_NEUTRAL]++;
        total++;
    }
    if (!total) return;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        production_t *queue = mo->production;
        if (th->function != P_MobjThinker || !Z_Producer(mo) || !queue || queue->queue_count <= 0 ||
            queue->release_active) continue;
        queue->time_left_ms -= elapsed_ms * held[mo->team] / total;
    }
}

/* A manufactured vehicle comes with its driver, and the factory starts
 * the next one of the same kind at once. */
void G_UnitProduced(mobj_t *unit, const mobj_t *producer) {
    int t = Z_UnitType(unit);
    if (t >= 0 && !Z_IsRobot(t)) unit->z.driver = (uint8_t)(MT_UNIT_GRUNT - MT_UNITS + 1);
    unit->core.angle = P_PointToAngle(0, FIXED_ONE);
    mobj_t *factory = producer ? P_MobjById(producer->id) : NULL;
    const StaticProductDefinition *again = Z_ProductInProduction(factory);
    if (again && factory->production->queue_count == 1) G_QueueProduct(factory, again);
}
