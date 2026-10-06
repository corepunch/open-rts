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
int W2_ProductOil(const StaticProductDefinition *product);
int W2_ResourceIncome(int owner, int resource);

/* Research and hall upgrades (games/warcraft-2/p_prod.c). */
typedef struct {
    const char *name;   /* Wargus upgrade id. */
    int icon;           /* Icon of the level being researched (Wargus icons.lua). */
    int time, gold, lumber, oil;
    bool armor;         /* Raises armor; otherwise piercing damage. */
    int tier, bonus;
    uint16_t maker;     /* The building that researches it. */
    uint16_t units[4];  /* Types it applies to; zero ends the list. */
} w2_upgrade_t;
const w2_upgrade_t *W2_Upgrade(int id);
int W2_UpgradeLevel(int owner, const w2_upgrade_t *upgrade);
void W2_ApplyUpgrade(int owner, int id);
bool W2_TransformUnit(mobj_t *unit, uint16_t type);
void W2_EnsureUnitSprite(int pud); /* HUD: load art for a type that appeared mid-game. */

/* Construction (games/warcraft-2/p_build.c). A worker walks to a clear
 * footprint, pays on arrival, steps inside, and the structure rises through
 * the Wargus stages over its build time; the builder steps out when done. */
enum { W2_BUILD_NONE = 0, W2_BUILD_TO_SITE = 1, W2_BUILD_WORKING = 2 };
bool W2_Buildable(uint16_t type);          /* A structure a worker can place. */
bool W2_CanPlace(uint16_t type, ivec2_t cell, const mobj_t *builder);
bool W2_ConstructOrder(mobj_t *builder, uint16_t type, ivec2_t cell);
bool W2_TickBuild(mobj_t *unit);
void W2_InterruptBuild(mobj_t *unit);       /* A new order drops a walk to a site. */
bool W2_CancelConstruction(mobj_t *site);  /* Refunds the price; the builder steps out. */
bool W2_UnderConstruction(const mobj_t *unit);
int W2_BuildProgress(const mobj_t *site);   /* Percent complete. */
bool W2_FindBuildSite(int owner, uint16_t type, ivec2_t *out);
bool W2_CountsAs(uint16_t type, uint16_t wanted); /* A keep is a town hall or better. */

/* Combat (games/warcraft-2/p_combat.c). */
void W2_SeedCombat(uint32_t seed);
uint32_t W2_SyncRand(void);
int W2_PiercingDamage(const mobj_t *unit);
int W2_Armor(const mobj_t *unit);
int W2_AttackDamage(const mobj_t *attacker, const mobj_t *target, uint32_t roll);
void A_W2_Attack(mobj_t *unit);
void A_W2_Collapse(mobj_t *unit);

#endif
