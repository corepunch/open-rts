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
bool W2_CancelProduction(mobj_t *producer);
int W2_ResourceIncome(int owner, int resource);

/* Research and hall upgrades (games/warcraft-2/p_prod.c). */
typedef struct {
    const char *name;   /* Wargus upgrade id. */
    int icon;           /* Icon of the level being researched (Wargus icons.lua). */
    int time, gold, lumber, oil;
    bool armor;         /* Raises armor; otherwise piercing damage. */
    int tier, bonus;
    uint16_t maker;     /* The building that researches it. */
    uint16_t units[9];  /* Types it applies to, including native heroes. */
} w2_upgrade_t;
const w2_upgrade_t *W2_Upgrade(int id);
int W2_UpgradeLevel(int owner, const w2_upgrade_t *upgrade);
void W2_ApplyUpgrade(int owner, int id);
bool W2_HasResearch(int owner, int id);
void W2_UpgradeUnit(mobj_t *unit);
float W2_AttackRange(const mobj_t *unit);
int W2_SightRange(const mobj_t *unit);
enum { W2_SPELL_NONE, W2_SPELL_VISION, W2_SPELL_HEAL, W2_SPELL_EXORCISM,
    W2_SPELL_EYE, W2_SPELL_BLOODLUST, W2_SPELL_RUNES, W2_SPELL_FIREBALL,
    W2_SPELL_SLOW, W2_SPELL_FLAME_SHIELD, W2_SPELL_INVISIBILITY, W2_SPELL_POLYMORPH,
    W2_SPELL_BLIZZARD, W2_SPELL_DEATH_COIL, W2_SPELL_HASTE, W2_SPELL_RAISE_DEAD,
    W2_SPELL_WHIRLWIND, W2_SPELL_UNHOLY_ARMOR, W2_SPELL_DECAY, W2_SPELL_DEMOLISH,
    W2_SPELL_COUNT };
enum { W2_BUFF_BLOODLUST, W2_BUFF_HASTE, W2_BUFF_SLOW, W2_BUFF_INVISIBLE, W2_BUFF_ARMOR, W2_BUFF_COUNT };
typedef struct {
    const char *name, *label;
    int mana, range, icon, research;
    bool unit_target, repeat;
} w2_spell_t;
extern const w2_spell_t w2_spells[W2_SPELL_COUNT];
bool W2_CanCast(const mobj_t *unit, int spell);
bool W2_CastOrder(mobj_t *unit, int spell, mobj_t *target, fixed3_t position);
void A_W2_Cast(mobj_t *unit);
void W2_TickSpells(mobj_t *unit);
bool W2_VisibleTo(const mobj_t *unit, int owner);
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
bool W2_RepairOrder(mobj_t *worker, mobj_t *target);
bool W2_TickRepair(mobj_t *worker);
void W2_InterruptRepair(mobj_t *worker);
void A_W2_Repair(mobj_t *worker);
int W2_Distance(const mobj_t *a, const mobj_t *b);

/* Victory and defeat (games/warcraft-2/p_victory.c). */
void W2_VictoryReset(void);
void W2_CheckVictory(mobj_t *const *units, int count);

/* Combat (games/warcraft-2/p_combat.c). */
void W2_SeedCombat(uint32_t seed);
uint32_t W2_CombatState(void);
void W2_SetCombatState(uint32_t state);
uint32_t W2_SyncRand(void);
int W2_PiercingDamage(const mobj_t *unit);
int W2_Armor(const mobj_t *unit);
int W2_AttackDamage(const mobj_t *attacker, const mobj_t *target, uint32_t roll);
void A_W2_Attack(mobj_t *unit);
void A_W2_Collapse(mobj_t *unit);
void W2_Burning(mobj_t *unit);
void W2_RestoreOilPatch(mobj_t *site);
bool W2_BoardOrder(mobj_t *unit, mobj_t *ship);
bool W2_UnloadOrder(mobj_t *ship, fvec2_t goal);
bool W2_TickTransport(mobj_t *unit);

#endif
