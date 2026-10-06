#ifndef __WARCRAFT_2__
#define __WARCRAFT_2__

#define TILE_W 32
#define TILE_H 32

bool W2_HarvestOrder(mobj_t *unit, fvec2_t goal);
bool W2_ReturnGoods(mobj_t *unit);
bool W2_TickHarvest(mobj_t *unit);
void W2_WorkerPose(mobj_t *unit);
void W2_InterruptHarvest(mobj_t *unit);
int W2_ProductLumber(const StaticProductDefinition *product);
int W2_ResourceIncome(int owner, int resource);

#endif
