#include "sc_local.h"
/* No economy or combat rules are claimed by the inspection sandbox. */
int G_ModelGetProducts(const RtsGameModel *m,int owner,StaticProductDefinition *out,int cap) { (void)m;(void)owner;(void)out;(void)cap; return 0; }
const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *m,int id) { (void)m;(void)id; return NULL; }
const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *m,int cls,int type) { (void)m;(void)cls;(void)type; return NULL; }
bool G_ModelProductAvailable(const RtsGameModel *m,int owner,const StaticProductDefinition *p) { (void)m;(void)owner;(void)p; return false; }
uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *p) { (void)p; return 0; }
int G_ModelBuildingFrameForProduct(const StaticProductDefinition *p) { (void)p; return 0; }
int G_ModelBuildingStateForProduct(const gameinfo_t *g,const StaticProductDefinition *p) { (void)g;(void)p; return -1; }
int G_ModelProductTrainingTimeMs(const StaticProductDefinition *p) { (void)p; return 0; }
bool G_ModelStartProductionRelease(RtsGameModel *m,mobj_t *u,const StaticProductDefinition *p,uint16_t id) { (void)m;(void)u;(void)p;(void)id; return false; }
bool G_ModelSpecialReleaseSpawnPoint(const RtsGameModel *m,const mobj_t *u,const StaticProductDefinition *p,const mobj_t *n,float *x,float *y) { (void)m;(void)u;(void)p;(void)n;(void)x;(void)y; return false; }
void G_ModelBuildUIScript(const RtsGameModel *m,const RtsRenderSnapshot *s,char *out,size_t n) { (void)m;(void)s; if(n) *out=0; }
bool G_PlayerBuildProduct(mobj_t *u,const StaticProductDefinition *p) { (void)u;(void)p; return false; }
bool G_ModelProducerHasTech(const mobj_t *u,const StaticProductDefinition *p) { (void)u;(void)p; return false; }
int G_ModelRadarLevel(int owner) { (void)owner; return 2; }
const AiGameInterface *G_AiInterface(void) { return NULL; }
