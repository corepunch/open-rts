#ifndef RUSTFRONT_H
#define RUSTFRONT_H
/* Rustfront: an original RTS whose art is rendered with Scener (art/, docs/SCENER.md) and
 * packed into 256-colour BMPs (assets/) by art/pack8.py, which also writes sprites.inc. */

#include "engine.h"

/* One packed sheet: `count` pictures of w x h in `columns` columns, every one grounded at
 * pivot. A picture's index is facing * frames + frame: `frames` animation frames per facing. */
typedef struct {
    const char *unit, *layer, *file;
    int w, h, columns, count, pivot_x, pivot_y, frames;
    const short (*mount)[2]; /* Turret layers: offset of the mount from the hull pivot, per hull facing. */
} rf_sheetdef_t;

/* Infantry animation: lump = start + facing * frames + frame. */
typedef struct {
    const char *unit, *name;
    int start, frames, facings;
} rf_animdef_t;

bool RF_LoadLevel(const char *path, level_t *out);
bool RF_LoadAssets(const char *root, tileset_t *tileset);
int  RF_LoadThings(const char *path);
bool RF_LoadSprites(const char *root, spritecache_t *cache);

/* w_terrain.c: the ground types of assets/terrain.txt (art/terrain.txt, docs/TERRAIN.md):
 * map letters, tiles, blocking, ore, and the transitions where one terrain meets another.
 * level.cell_terrain holds each cell's terrain index. */
bool RF_LoadTerrain(const char *map_path);
int  RF_TerrainOfLetter(char letter);              /* -1: not a terrain letter */
int  RF_DefaultTerrain(void);                      /* what mined-out ore becomes */
bool RF_TerrainBlocks(int terrain);
bool RF_TerrainHoldsOre(int terrain);
uint16_t RF_TerrainTile(int terrain, int x, int y);
void RF_SetCellTerrain(level_t *map, int x, int y, int terrain);
/* Relief: height levels, cliffs and ramps (w_terrain.c, art/relief.py). */
enum { RF_RELIEF_FLAT, RF_RELIEF_CLIFF, RF_RELIEF_RAMP };
bool RF_BeginRelief(const level_t *map);           /* every cell flat at level 0 */
void RF_SetCellLevel(const level_t *map, int x, int y, int level, bool ramp);
void RF_BuildRelief(level_t *map);                 /* after the cells' terrain is set */
int  RF_CellLevel(const level_t *map, int x, int y);
int  RF_CellRelief(const level_t *map, int x, int y);
bool RF_CellBlocks(const level_t *map, int cell);  /* blocking terrain, or a cliff */
void RF_DrawTransitions(app_t *app, const level_t *map, const tileset_t *tileset, int x, int y, int dx, int dy);

/* g_mission.c: building footprints block, idle harvesters find ore, a refinery comes with a
 * harvester. */
void RF_MissionTicker(level_t *map, mobj_t *const *mobjs, int count);
void RF_SyncBlocking(level_t *map);
void RF_ClearMinedOre(level_t *map);

/* g_combat.c: shells and rockets. Tanks, gun turrets, artillery and rocket soldiers fire a
 * projectile that does its damage on arrival; everything else hits at once. */
bool RF_Attack(mobj_t *attacker, const weapondef_t *weapon, mobj_t *target);
void A_RfImpact(mobj_t *projectile);
void RF_MobjTicker(mobj_t *mobj);
void RF_TrailRockets(mobj_t *const *mobjs, int count);
void RF_Detonate(mobj_t *wrecker, mobj_t *target, int damage); /* the Wrecker */
void RF_AegisMend(mobj_t *const *mobjs, int count);
bool RF_ImpactInProgress(void);

/* g_build.c: construction sites and battle damage. The Command Center's structures stand on
 * their site from placement and build themselves up; one site at a time per side. */
const mobj_t *RF_SiteInProgress(int owner);
bool RF_IsSite(const mobj_t *unit);
void RF_StructureDamage(mobj_t *unit);
void RF_CompleteSite(mobj_t *unit);

/* factions.c: three factions filling the same roles with their own types. */
typedef enum { RF_DIRECTORATE, RF_CLANS, RF_CONCORD, RF_FACTION_COUNT } rf_faction_id_t;
typedef enum { RF_ROLE_HQ, RF_ROLE_POWER, RF_ROLE_REFINERY, RF_ROLE_BARRACKS, RF_ROLE_FACTORY,
               RF_ROLE_TURRET, RF_ROLE_HARVESTER, RF_ROLE_COUNT } rf_role_t;
typedef struct {
    const char *name;
    uint16_t first, last;            /* its types are first..last (info.h) */
    uint16_t role[RF_ROLE_COUNT];
} rf_faction_t;
extern const rf_faction_t rf_factions[RF_FACTION_COUNT];
int RF_FactionOfType(uint16_t type);                 /* -1: no faction's (effects, shots) */
int RF_OwnerFaction(int owner);                      /* its HQ's, else any unit's; -1 if none */
uint16_t RF_Role(int faction, rf_role_t role);
bool RF_IsRole(uint16_t type, rf_role_t role);
bool RF_OwnerHasRole(int owner, rf_role_t role);     /* of any faction */
bool RF_IsInfantry(uint16_t type);

/* g_base.c: power, repair and sell, rally points, veterancy. */
enum { RF_ORDER_REPAIR = 0, RF_ORDER_SELL = 1 };      /* TC_REPAIR's product */
enum { RF_VETERAN_KILLS = 3, RF_ELITE_KILLS = 6 };
int  RF_PowerOf(uint16_t type);                      /* + makes, - draws */
void RF_OwnerPower(int owner, int *made, int *used);
bool RF_LowPower(int owner);
bool RF_IsStructure(const mobj_t *unit);
int  RF_Rank(const mobj_t *unit);                    /* 0, 1 veteran, 2 elite */
void RF_CreditKill(mobj_t *victim, mobj_t *killer);
int  RF_RankDamage(const mobj_t *attacker, int damage);
int  RF_ShieldDamage(const mobj_t *victim, int damage); /* g_combat.c: an Aegis field */
int  RF_HitDamage(const mobj_t *attacker, const weapondef_t *weapon, mobj_t *target, int damage, int divisor);
int  RF_SellValue(const mobj_t *unit);
bool RF_HasRally(const mobj_t *producer);
fixed2_t RF_Rally(const mobj_t *producer);
void RF_UnitReleased(mobj_t *unit, const mobj_t *producer);
bool RF_StructureOrder(int product);                 /* the local player's selected structures */
void RF_BaseTicker(mobj_t *const *mobjs, int count);

/* g_ai.c: the computer player's difficulty, per owner, set before the game starts (the front
 * end, or a campaign mission). Normal is the default. */
enum { RF_AI_EASY, RF_AI_NORMAL, RF_AI_HARD, RF_AI_LEVELS };
void RF_SetAIDifficulty(int owner, int level);
int  RF_AIDifficulty(int owner);

/* r_marks.c: selection, health, rank chevrons, repair and rally marks (gameinfo draw_overlays). */
void RF_DrawMarks(const unitoverlaycontext_t *ctx);

/* g_sight.c: fog of war. Each type's sight in cells (0: sees nothing), and the fog pass. */
int  RF_SightCells(uint16_t type);
void RF_DrawFog(app_t *app, const level_t *map, const tileset_t *tileset);

/* hu_rustfront.c: the sidebar. */
#define RF_SIDEBAR_X 480
menu_t *RF_InitHUD(app_t *app, const char *root);
void RF_ShutdownHUD(void);

#endif
