#include "sc_local.h"
#include <string.h>
/* Stargus command cards, with native DAT costs and build times.
 * Orders, queues, payment, placement and spawning belong to the engine. */
static const struct { int type, maker, prerequisite; } recipes[] = {
    {7,106,-1}, {0,111,-1}, {32,111,112}, {1,111,116},
    {106,7,-1}, {109,7,-1}, {110,7,-1}, {111,7,106}, {112,7,111},
    {122,7,106}, {124,7,122}, {125,7,111},
    {113,7,111}, {114,7,113}, {116,7,114}, {123,7,113},
    {2,113,-1}, {5,113,-1}, {3,113,123},
    {8,114,-1}, {11,114,-1}, {9,114,116}, {12,114,116},
    {154,64,-1}, {156,64,-1}, {157,64,-1}, {160,64,154},
    {166,64,154}, {162,64,166}, {164,64,160}, {172,64,160},
    {155,64,164}, {167,64,164}, {163,64,164}, {171,64,155},
    {169,64,167}, {165,64,163}, {159,64,155}, {170,64,165},
    {64,154,-1}, {65,160,-1}, {66,160,164}, {67,160,165},
    {69,155,-1}, {83,155,171}, {84,155,159}, {70,167,-1},
    {72,167,169}, {71,167,170},
    {131,41,-1}, {143,41,-1}, {149,41,-1}, {142,41,131},
    {139,41,131}, {135,41,142}, {141,41,132}, {138,41,132},
    {134,41,133}, {140,41,133}, {136,41,133},
    /* Larva and eggs are not simulated. A hatchery trains the army directly. */
    {41,131,-1}, {42,131,-1}, {37,131,142}, {38,131,135}, {43,131,141},
};
static StaticProductDefinition products[sizeof(recipes)/sizeof(*recipes)];
static void init_products(void) {
    static bool initialized;
    if(initialized)return;
    for(unsigned i=0;i<sizeof(recipes)/sizeof(*recipes);i++) {
        int type=recipes[i].type; const sc_unit_t *u=&sc_units[type];
        products[i]=(StaticProductDefinition){.row_id=type+1,.ui_id=type+1,.label=u->name,
            .cost=u->minerals,.extra_costs={u->gas},.icon_frame=type,
            .product_class=(u->flags&1)?RTS_PRODUCT_BUILDING:RTS_PRODUCT_UNIT,
            .product_type=type+1,.makers={recipes[i].maker+1},.maker_count=1,
            .worker_build=(u->flags&1)!=0,
            .prerequisites={recipes[i].prerequisite+1},.prerequisite_count=recipes[i].prerequisite>=0};
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
typedef struct { int product, count; } sc_step_t;
static const sc_step_t terran_ladder[]={
    {8,6},{110,1},{112,1},{1,6},{111,1},{8,8},{110,2},{114,1},{3,4},{1,12}
},zerg_ladder[]={
    {42,6},{43,1},{143,1},{38,8},{150,1},{42,8},{136,1},{39,6},{43,2}
},protoss_ladder[]={
    {65,6},{157,1},{161,1},{66,4},{158,1},{65,8},{157,2},{66,8}
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
