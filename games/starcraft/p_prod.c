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
bool G_ModelProducerHasTech(const mobj_t *u,const StaticProductDefinition *p) { return u&&G_ModelProductAvailable(NULL,u->owner,p); }
int G_ModelRadarLevel(int owner) { (void)owner; return 2; }
const AiGameInterface *G_AiInterface(void) { return NULL; }
