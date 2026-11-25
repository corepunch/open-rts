#include "engine.h"
#include "info.h"
#include "rustfront.h"

/* Every structure's states: its loop, its scorched loop and its construction stages. */
static const struct { uint16_t type; int healthy, damaged, build; } STRUCTURES[] = {
    {MT_HQ, S_HQ, S_HQ_DAMAGED, S_HQ_BUILD},
    {MT_POWER, S_POWER, S_POWER_DAMAGED, S_POWER_BUILD},
    {MT_REFINERY, S_REFINERY, S_REFINERY_DAMAGED, S_REFINERY_BUILD},
    {MT_BARRACKS, S_BARRACKS, S_BARRACKS_DAMAGED, S_BARRACKS_BUILD},
    {MT_FACTORY, S_FACTORY, S_FACTORY_DAMAGED, S_FACTORY_BUILD},
    {MT_GUN_TURRET, S_GUN_TURRET, 0, S_GUN_TURRET_BUILD},
};

static int structure_index(uint16_t type) {
    for (int i = 0; i < (int)(sizeof(STRUCTURES) / sizeof(*STRUCTURES)); ++i)
        if (STRUCTURES[i].type == type) return i;
    return -1;
}

bool RF_IsSite(const mobj_t *unit) {
    return unit && unit->production && unit->production->placed && unit->production->queue_count > 0 &&
           unit->production->product_class == RTS_PRODUCT_BUILDING && unit->production->actor_id == unit->type_id;
}

const mobj_t *RF_SiteInProgress(int owner) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *unit = (const mobj_t *)th;
        if (th->function == P_MobjThinker && unit->owner == owner && !unit->remove && unit->hp > 0 &&
            RF_IsSite(unit)) return unit;
    }
    return NULL;
}

/* Placing a Command Center structure: the structure itself stands on the site at once, with a
 * tenth of its hit points and without the traits it earns on completion (a turret does not
 * shoot, a refinery takes no ore). The shared production ticker counts its own queue down. */
bool G_ModelPlaceConstruction(RtsGameModel *model, mobj_t *builder, const StaticProductDefinition *p,
                              ivec2_t cell, bool *handled) {
    (void)model;
    *handled = builder && p && p->product_class == RTS_PRODUCT_BUILDING && builder->type_id == MT_HQ;
    if (!*handled) return false;
    int s = structure_index((uint16_t)p->product_type);
    const mobjtype_t *type = P_ActorType((uint16_t)p->product_type);
    if (s < 0 || !type || RF_SiteInProgress(builder->owner)) return false;
    mobj_t *site = P_SpawnMobj(fixed3_from_fixed2(P_BuildingPosition(type->id, cell), 0), type->id);
    if (!site) return false;
    site->owner = builder->owner;
    site->team = builder->team;
    site->allegiance = builder->allegiance;
    site->hp = site->max_hp / 10 > 0 ? site->max_hp / 10 : 1;
    site->rf_built = 0;
    site->traits &= ~(MF_ATTACK | MF_TURRET | MF_RESOURCE_BASE);
    production_t *queue = P_EnsureMobjProduction(site);
    if (!queue) { P_RemoveMobj(site); return false; }
    int time = G_ModelProductTrainingTimeMs(p);
    *queue = (production_t){.actor_id = type->id, .product_class = (uint8_t)p->product_class,
                            .product_type = p->product_type, .queue_count = 1, .time_ms = time,
                            .time_left_ms = time, .placed = true, .cell = cell};
    P_SetMobjStateFrame(site, STRUCTURES[s].build, 0);
    level.player_resources[site->owner][0] -= p->cost;
    RF_SyncBlocking(&level);
    return true;
}

bool G_ModelIsConstructionSite(const mobj_t *unit, const StaticProductDefinition *p) {
    return RF_IsSite(unit) && p && p->product_type == unit->type_id;
}

/* Each tic a site gains the hit points its progress earns, and shows the matching stage. */
void G_ModelUpdateConstruction(RtsGameModel *model, mobj_t *site, const StaticProductDefinition *p) {
    (void)model; (void)p;
    production_t *queue = site->production;
    int s = structure_index(site->type_id);
    if (!queue || queue->time_ms <= 0 || s < 0) return;
    int left = queue->time_left_ms > 0 ? queue->time_left_ms : 0;
    int built = 1000 - (int)((int64_t)left * 1000 / queue->time_ms);
    if (built > site->rf_built) {
        site->hp += (int)((int64_t)site->max_hp * 9 * (built - site->rf_built) / 10000);
        if (site->hp > site->max_hp) site->hp = site->max_hp;
        site->rf_built = built;
    }
    int stage = built * BUILD_STAGE_COUNT / 1000;
    if (stage >= BUILD_STAGE_COUNT) stage = BUILD_STAGE_COUNT - 1;
    if (site->core.state_id != STRUCTURES[s].build || site->core.state_frame != stage)
        P_SetMobjStateFrame(site, STRUCTURES[s].build, stage);
}

/* Called from G_UnitProduced when a site completes: the structure takes up its traits and its
 * working loop. */
static void complete_site(mobj_t *unit) {
    int s = structure_index(unit->type_id);
    if (s < 0 || unit->core.state_id != STRUCTURES[s].build) return;
    if (unit->info) unit->traits |= unit->info->traits & (MF_ATTACK | MF_TURRET | MF_RESOURCE_BASE);
    unit->rf_built = 0;
    P_SetMobjState(unit, STRUCTURES[s].healthy);
}

/* Below half its hit points a structure shows its scorched loop, at the same point of the
 * loop; repaired above half, its clean one. */
void RF_StructureDamage(mobj_t *unit) {
    int s = structure_index(unit->type_id);
    if (s < 0 || !STRUCTURES[s].damaged || unit->hp <= 0 || RF_IsSite(unit)) return;
    bool hurt = unit->hp * 2 < unit->max_hp;
    if (hurt && unit->core.state_id == STRUCTURES[s].healthy)
        P_SetMobjStateFrame(unit, STRUCTURES[s].damaged, unit->core.state_frame);
    else if (!hurt && unit->core.state_id == STRUCTURES[s].damaged)
        P_SetMobjStateFrame(unit, STRUCTURES[s].healthy, unit->core.state_frame);
}

void RF_CompleteSite(mobj_t *unit) { complete_site(unit); }
