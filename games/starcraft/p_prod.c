#include "sc_local.h"
#include <string.h>
/* Stargus command cards, with native DAT costs and build times.
 * Orders, queues, payment, placement and spawning belong to the engine. */
static const struct { mobjtype_id_t type, maker, prerequisite; } recipes[] = {
    {MT_SCV, MT_COMMAND_CENTER, MT_NONE},
    {MT_MARINE, MT_BARRACKS, MT_NONE},
    {MT_FIREBAT, MT_BARRACKS, MT_ACADEMY},
    {MT_GHOST, MT_BARRACKS, MT_SCIENCE_FACILITY},
    {MT_COMMAND_CENTER, MT_SCV, MT_NONE},
    {MT_SUPPLY_DEPOT, MT_SCV, MT_NONE},
    {MT_REFINERY, MT_SCV, MT_NONE},
    {MT_BARRACKS, MT_SCV, MT_COMMAND_CENTER},
    {MT_ACADEMY, MT_SCV, MT_BARRACKS},
    {MT_ENGINEERING_BAY, MT_SCV, MT_COMMAND_CENTER},
    {MT_MISSILE_TURRET, MT_SCV, MT_ENGINEERING_BAY},
    {MT_BUNKER, MT_SCV, MT_BARRACKS},
    {MT_FACTORY, MT_SCV, MT_BARRACKS},
    {MT_STARPORT, MT_SCV, MT_FACTORY},
    {MT_SCIENCE_FACILITY, MT_SCV, MT_STARPORT},
    {MT_ARMORY, MT_SCV, MT_FACTORY},
    {MT_VULTURE, MT_FACTORY, MT_NONE},
    {MT_SIEGE_TANK, MT_FACTORY, MT_NONE},
    {MT_GOLIATH, MT_FACTORY, MT_ARMORY},
    {MT_WRAITH, MT_STARPORT, MT_NONE},
    {MT_DROPSHIP, MT_STARPORT, MT_NONE},
    {MT_SCIENCE_VESSEL, MT_STARPORT, MT_SCIENCE_FACILITY},
    {MT_BATTLECRUISER, MT_STARPORT, MT_SCIENCE_FACILITY},
    {MT_NEXUS, MT_PROBE, MT_NONE},
    {MT_PYLON, MT_PROBE, MT_NONE},
    {MT_ASSIMILATOR, MT_PROBE, MT_NONE},
    {MT_GATEWAY, MT_PROBE, MT_NEXUS},
    {MT_FORGE, MT_PROBE, MT_NEXUS},
    {MT_PHOTON_CANNON, MT_PROBE, MT_FORGE},
    {MT_CYBERNETICS_CORE, MT_PROBE, MT_GATEWAY},
    {MT_SHIELD_BATTERY, MT_PROBE, MT_GATEWAY},
    {MT_ROBOTICS_FACILITY, MT_PROBE, MT_CYBERNETICS_CORE},
    {MT_STARGATE, MT_PROBE, MT_CYBERNETICS_CORE},
    {MT_CITADEL_OF_ADUN, MT_PROBE, MT_CYBERNETICS_CORE},
    {MT_ROBOTICS_SUPPORT_BAY, MT_PROBE, MT_ROBOTICS_FACILITY},
    {MT_FLEET_BEACON, MT_PROBE, MT_STARGATE},
    {MT_TEMPLAR_ARCHIVES, MT_PROBE, MT_CITADEL_OF_ADUN},
    {MT_OBSERVATORY, MT_PROBE, MT_ROBOTICS_FACILITY},
    {MT_ARBITER_TRIBUNAL, MT_PROBE, MT_TEMPLAR_ARCHIVES},
    {MT_PROBE, MT_NEXUS, MT_NONE},
    {MT_ZEALOT, MT_GATEWAY, MT_NONE},
    {MT_DRAGOON, MT_GATEWAY, MT_CYBERNETICS_CORE},
    {MT_HIGH_TEMPLAR, MT_GATEWAY, MT_TEMPLAR_ARCHIVES},
    {MT_SHUTTLE, MT_ROBOTICS_FACILITY, MT_NONE},
    {MT_REAVER, MT_ROBOTICS_FACILITY, MT_ROBOTICS_SUPPORT_BAY},
    {MT_OBSERVER, MT_ROBOTICS_FACILITY, MT_OBSERVATORY},
    {MT_SCOUT, MT_STARGATE, MT_NONE},
    {MT_CARRIER, MT_STARGATE, MT_FLEET_BEACON},
    {MT_ARBITER, MT_STARGATE, MT_ARBITER_TRIBUNAL},
    {MT_HATCHERY, MT_DRONE, MT_NONE},
    {MT_CREEP_COLONY, MT_DRONE, MT_NONE},
    {MT_EXTRACTOR, MT_DRONE, MT_NONE},
    {MT_SPAWNING_POOL, MT_DRONE, MT_HATCHERY},
    {MT_EVOLUTION_CHAMBER, MT_DRONE, MT_HATCHERY},
    {MT_HYDRALISK_DEN, MT_DRONE, MT_SPAWNING_POOL},
    {MT_SPIRE, MT_DRONE, MT_LAIR},
    {MT_QUEENS_NEST, MT_DRONE, MT_LAIR},
    {MT_NYDUS_CANAL, MT_DRONE, MT_HIVE},
    {MT_ULTRALISK_CAVERN, MT_DRONE, MT_HIVE},
    {MT_DEFILER_MOUND, MT_DRONE, MT_HIVE},
    /* Larva and eggs are not simulated. A hatchery trains the army directly. */
    {MT_DRONE, MT_HATCHERY, MT_NONE},
    {MT_OVERLORD, MT_HATCHERY, MT_NONE},
    {MT_ZERGLING, MT_HATCHERY, MT_SPAWNING_POOL},
    {MT_HYDRALISK, MT_HATCHERY, MT_HYDRALISK_DEN},
    {MT_MUTALISK, MT_HATCHERY, MT_SPIRE},
};
static StaticProductDefinition products[sizeof(recipes)/sizeof(*recipes)];
static void init_products(void) {
    static bool initialized;
    if(initialized)return;
    for(unsigned i=0;i<sizeof(recipes)/sizeof(*recipes);i++) {
        mobjtype_id_t type=recipes[i].type; const sc_unit_t *u=&sc_units[type-1];
        products[i]=(StaticProductDefinition){.row_id=type,.ui_id=type,.label=u->name,
            .cost=u->minerals,.extra_costs={u->gas},.icon_frame=type-1,
            .product_class=(u->flags&1)?RTS_PRODUCT_BUILDING:RTS_PRODUCT_UNIT,
            .product_type=type,.makers={recipes[i].maker},.maker_count=1,
            .worker_build=(u->flags&1)!=0,
            .prerequisites={recipes[i].prerequisite},.prerequisite_count=recipes[i].prerequisite!=MT_NONE};
    }
    initialized=true;
}
int G_ModelGetProducts(const RtsGameModel *m,int owner,StaticProductDefinition *out,int cap) {
    (void)m;(void)owner;init_products();int n=(int)(sizeof(recipes)/sizeof(*recipes));
    if(!out||cap<1)return 0;if(n>cap)n=cap;memcpy(out,products,(size_t)n*sizeof(*out));return n;
}
const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *m,int id) {
    (void)m;init_products();for(unsigned i=0;i<sizeof(recipes)/sizeof(*recipes);i++)if(products[i].ui_id==id)return &products[i];return NULL;
}
const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *m,int cls,int type) {
    const StaticProductDefinition *p=G_ModelProductByUIId(m,type);return p&&(int)p->product_class==cls?p:NULL;
}
bool G_ModelProductAvailable(const RtsGameModel *m,int owner,const StaticProductDefinition *p) {
    if(!p)return false;
    for(int i=0;i<p->prerequisite_count;i++)if(!G_ModelHasActorType(m,owner,p->prerequisites[i]))return false;
    return true;
}
uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *p) { return p?p->product_type:0; }
int G_ModelBuildingFrameForProduct(const StaticProductDefinition *p) { (void)p;return 0; }
int G_ModelBuildingStateForProduct(const gameinfo_t *g,const StaticProductDefinition *p) { return p?g->mobjinfo[p->product_type].spawnstate:0; }
int G_ModelProductTrainingTimeMs(const StaticProductDefinition *p) { return p?(sc_units[p->product_type-1].build_time*1000+23)/24:0; }
bool G_ModelStartProductionRelease(RtsGameModel *m,mobj_t *u,const StaticProductDefinition *p,uint16_t id) { (void)m;(void)u;(void)p;(void)id; return false; }
bool G_ModelSpecialReleaseSpawnPoint(const RtsGameModel *m,const mobj_t *u,const StaticProductDefinition *p,const mobj_t *n,float *x,float *y) { (void)m;(void)u;(void)p;(void)n;(void)x;(void)y; return false; }
void G_ModelBuildUIScript(const RtsGameModel *m,const RtsRenderSnapshot *s,char *out,size_t n) { (void)m;(void)s;if(n)*out=0; }
bool G_PlayerBuildProduct(mobj_t *u,const StaticProductDefinition *p) { return G_QueueProduct(u,p); }
bool G_ModelProducerHasTech(const mobj_t *u,const StaticProductDefinition *p) {
    return u&&G_ModelProductAvailable(NULL,u->owner,p)&&sc_supply_ok(u->owner,p);
}
int G_ModelRadarLevel(int owner) { (void)owner; return 2; }

/* Supply is stored in halves. 400 halves is the retail 200 cap. A queued unit
 * reserves its cost so a full queue cannot slip past the cap. */
void sc_supply_counts(int owner,int *used,int *provided) {
    int have=0,need=0;
    if(thinkercap.next) for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=owner||mo->remove||mo->hp<=0) continue;
        if(mo->type_id&&mo->type_id<=SC_TYPES) {
            have+=sc_units[mo->type_id-1].supply_provided;
            need+=sc_units[mo->type_id-1].supply_required;
        }
        if(mo->production&&mo->production->product_class==RTS_PRODUCT_UNIT&&
           mo->production->product_type>0&&mo->production->product_type<=SC_TYPES)
            need+=sc_units[mo->production->product_type-1].supply_required*mo->production->queue_count;
    }
    if(have>400) have=400;
    if(used) *used=need;
    if(provided) *provided=have;
}
bool sc_supply_ok(int owner,const StaticProductDefinition *product) {
    if(!product||product->product_class!=RTS_PRODUCT_UNIT) return true;
    int type=product->product_type-1;
    if(type<0||type>=SC_TYPES) return true;
    int cost=sc_units[type].supply_required;
    if(cost<=0) return true;
    int used,have; sc_supply_counts(owner,&used,&have);
    return used+cost<=have;
}

static int sc_ai_level(const level_t *map,int owner) {
    (void)map;
    if(owner<0||owner>=8||sc_mission_result()) return AI_LEVEL_NONE;
    if(netgame?D_PlayerIsHuman(owner):owner==consoleplayer) return AI_LEVEL_NONE;
    int kind=sc_owner_kind(owner);
    if(kind==1||kind==5||sc_player_ai(owner)) return AI_LEVEL_NORMAL;
    return AI_LEVEL_NONE;
}
typedef struct { mobjtype_id_t product; int count; } sc_step_t;
static const sc_step_t terran_ladder[]={
    {MT_SCV,6},{MT_SUPPLY_DEPOT,1},{MT_BARRACKS,1},{MT_MARINE,6},{MT_REFINERY,1},{MT_SCV,8},{MT_SUPPLY_DEPOT,2},{MT_FACTORY,1},{MT_VULTURE,4},{MT_MARINE,12}
},zerg_ladder[]={
    {MT_DRONE,6},{MT_OVERLORD,1},{MT_SPAWNING_POOL,1},{MT_ZERGLING,8},{MT_EXTRACTOR,1},{MT_DRONE,8},{MT_HYDRALISK_DEN,1},{MT_HYDRALISK,6},{MT_OVERLORD,2}
},protoss_ladder[]={
    {MT_PROBE,6},{MT_PYLON,1},{MT_GATEWAY,1},{MT_ZEALOT,4},{MT_ASSIMILATOR,1},{MT_PROBE,8},{MT_PYLON,2},{MT_ZEALOT,8}
};
static bool sc_ai_plan(const level_t *map,int owner,int level,AiPlan *out) {
    (void)map;(void)level;
    const sc_step_t *ladder=terran_ladder;
    unsigned n=sizeof(terran_ladder)/sizeof(*terran_ladder);
    int side=sc_player_side(owner);
    if(side==0){ladder=zerg_ladder;n=sizeof(zerg_ladder)/sizeof(*zerg_ladder);}
    else if(side==2){ladder=protoss_ladder;n=sizeof(protoss_ladder)/sizeof(*protoss_ladder);}
    out->wave_interval_ms=45000; out->wave_min_size=4; out->wave_max_size=10;
    for(unsigned i=0;i<n;i++) P_AiPlanAdd(out,ladder[i].product,ladder[i].count);
    return true;
}
static int sc_ai_can_purchase(const level_t *map,int owner,int ui) {
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,ui);
    if(!product||!G_ModelProductAvailable(NULL,owner,product)||!G_FindProducer(owner,product))
        return AI_BUY_BLOCKED;
    if(!sc_supply_ok(owner,product)) return AI_BUY_BLOCKED;
    if(product->extra_costs[0]>map->player_resources[owner][1]) return AI_BUY_BLOCKED;
    return map->player_resources[owner][0]<product->cost?AI_BUY_NEED_CREDITS:AI_BUY_OK;
}
static mobj_t *idle_maker(int owner,int maker) {
    if(!thinkercap.next) return NULL;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        mobj_t *mo=(mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=owner||mo->remove||mo->hp<=0) continue;
        if(mo->type_id==maker&&!mo->production) return mo;
    }
    return NULL;
}
static bool find_site(int owner,uint16_t type,ivec2_t *out) {
    fvec2_t origin={level.width*0.5f,level.height*0.5f};
    bool found=false;
    if(thinkercap.next) for(thinker_t *th=thinkercap.next;th!=&thinkercap&&!found;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function==P_MobjThinker&&mo->owner==owner&&mo->hp>0&&!mo->remove) {
            origin=fixed3_xy_to_fvec2(mo->core.position); found=true;
        }
    }
    for(int radius=2;radius<48;radius++) for(int y=-radius;y<=radius;y++) for(int x=-radius;x<=radius;x++) {
        if(abs(x)!=radius&&abs(y)!=radius) continue;
        ivec2_t cell={(int)origin.x+x,(int)origin.y+y};
        if(P_CanPlaceBuilding(type,cell,NULL)) { *out=cell; return true; }
    }
    return false;
}
static bool sc_ai_purchase(level_t *map,int owner,int ui) {
    (void)map;
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,ui);
    if(!product) return false;
    if(!product->worker_build) return G_AiCatalogPurchase(map,owner,ui);
    mobj_t *worker=idle_maker(owner,product->makers[0]);
    ivec2_t cell;
    return worker&&find_site(owner,G_ModelActorIdForProduct(product),&cell)&&
        G_PlaceProduct(worker,product,cell);
}
static bool sc_ai_busy(const mobj_t *unit) {
    return unit&&unit->production&&unit->production->placed;
}
static const AiGameInterface sc_ai={
    .name="starcraft",.features=AI_FEATURE_ALL,.player_level=sc_ai_level,.plan=sc_ai_plan,
    .owned=G_AiCatalogOwned,.can_purchase=sc_ai_can_purchase,.purchase=sc_ai_purchase,
    .is_anchor=G_AiIsStructure,.is_busy=sc_ai_busy,
};
const AiGameInterface *G_AiInterface(void) { return &sc_ai; }
