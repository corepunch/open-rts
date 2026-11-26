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

/* hu_rustfront.c: the sidebar. */
#define RF_SIDEBAR_X 480
menu_t *RF_InitHUD(app_t *app, const char *root);
void RF_ShutdownHUD(void);

/* w_rustfront.c: map unit names ("tank", "cl_hall") and a thing spawned the way a map places it. */
uint16_t RF_UnitType(const char *name);              /* 0: no such unit */
mobj_t *RF_SpawnThing(uint16_t type, int owner, ivec2_t cell, int facing);

/* ── g_campaign.c: the single-player campaign (docs/CAMPAIGN.md) ─────────────
 * Three campaigns, one per faction, of RF_CAMPAIGN_MISSIONS missions each. A mission is a map
 * (maps/campaign/<faction>-<n>.txt) with mission keys: a title, a briefing, objectives, tech
 * limits, and triggers that spawn units or show messages. Indices here are 0-based. */
#define RF_CAMPAIGN_MISSIONS 5
#define RF_MAX_OBJECTIVES 8
#define RF_OBJECTIVE_TEXT 96
#define RF_BRIEFING_TEXT 1536
typedef struct {
    int faction, index;                                  /* rf_faction_id_t, 0..4 */
    char map[96];                                        /* relative to the data root */
    char title[64];
    char briefing[RF_BRIEFING_TEXT];                     /* paragraphs joined with '\n' */
    int objective_count;
    char objectives[RF_MAX_OBJECTIVES][RF_OBJECTIVE_TEXT];
} rf_missioninfo_t;

/* The campaign list for the front end. */
const char *RF_CampaignName(int faction);             /* "Directorate"; NULL out of range */
const char *RF_CampaignSlug(int faction);             /* "directorate", "clans", "concord" */
bool RF_MissionPath(int faction, int index, char *out, size_t size);  /* "maps/campaign/clans-2.txt" */
bool RF_MissionInfo(const char *root, int faction, int index, rf_missioninfo_t *out);

/* Progress, kept in campaign.txt under D_UserDirectory(): how many missions of each campaign
 * are won. The first mission is always open; winning mission i opens mission i + 1. */
int  RF_CampaignWon(int faction);                     /* 0..RF_CAMPAIGN_MISSIONS */
bool RF_MissionUnlocked(int faction, int index);
bool RF_CampaignRecordWin(int faction, int index);    /* false if it cannot be saved */
bool RF_CampaignResetProgress(void);

/* The running mission (none on a skirmish map). The level records its own win. */
enum { RF_OBJECTIVE_PENDING, RF_OBJECTIVE_DONE, RF_OBJECTIVE_FAILED };
bool RF_InMission(int *faction, int *index);
const char *RF_MissionTitle(void);
int  RF_ObjectiveCount(void);
const char *RF_ObjectiveText(int i);
int  RF_ObjectiveState(int i);
int  RF_MissionElapsedMs(void);
/* An objective's terms, for an objectives panel or a test: its kind, the cells to hold, the
 * seconds to survive or hold, the credits or count, the unit type to build, the tagged id. */
enum { RF_OBJ_DESTROY_ALL, RF_OBJ_DESTROY, RF_OBJ_PROTECT, RF_OBJ_SURVIVE, RF_OBJ_HOLD, RF_OBJ_CREDITS,
       RF_OBJ_BUILD };
typedef struct { int kind, seconds, amount, count, held_ms; irect_t area; uint16_t type; uint32_t tag_id; } rf_objective_t;
bool RF_ObjectiveDetail(int i, rf_objective_t *out);
const char *RF_LastMissionMessage(void);              /* the latest trigger or objective message */
int  RF_MissionLoadErrors(void);                      /* bad mission lines in the loaded map */
bool RF_CampaignAllows(int owner, uint16_t type);     /* the mission's tech limit for the player */
/* Called by the map loader and the mission ticker. */
void RF_CampaignBeginLoad(void);
bool RF_CampaignParseLine(const char *line, int width, int height);
void RF_CampaignEndLoad(const char *path);
void RF_CampaignTagged(const char *tag, const mobj_t *thing);
void RF_CampaignTicker(level_t *map, mobj_t *const *mobjs, int count, hudtext_t *hud, int dt_ms);

#endif
