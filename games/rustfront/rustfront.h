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

/* w_starts.c: multiplayer start positions and the map list (README, "Multiplayer").
 * A map's "start N X Y [FACTION]" lines mark where a side may begin: X, Y is the top-left cell
 * of its headquarters and FACTION (directorate, clans, concord) the default when nobody
 * chooses. Before loading such a map, the front end may set a plan saying which owner begins
 * at which start as which faction, and who is allied; RF_LoadThings then spawns each one with
 * RF_SpawnStart. Without a plan, every start is a side of its default faction, owner N at
 * start N. Starts make the map free-for-all (level.player_teams). */
#define RF_MAX_STARTS 8
typedef struct { bool used; ivec2_t cell; int faction; } rf_start_t;
typedef struct {
    int8_t start;    /* index of the start this owner begins at; -1: this owner does not play */
    int8_t faction;  /* rf_faction_id_t; -1: the start's default */
    uint8_t team;    /* 0: on its own; owners with the same non-zero team are allies */
} rf_seat_t;
typedef struct { int count; rf_seat_t seat[RF_MAX_STARTS]; } rf_startplan_t; /* seat[owner] */
void RF_SetStartPlan(const rf_startplan_t *plan);   /* NULL: the defaults; kept until changed */
int  RF_StartCount(void);                           /* of the loaded map; 0: a map with fixed bases */
const rf_start_t *RF_Start(int slot);
bool RF_SpawnStart(int owner, int faction, int x, int y); /* HQ at X, Y, a harvester and a squad */
int  RF_FactionByName(const char *name);            /* "directorate", "clans", "concord"; -1 */
typedef struct {
    char path[256];   /* relative to the game root, e.g. "maps/twin-rivers.txt" */
    char title[64];   /* its "title" line, else the file name */
    int players;      /* start positions; 0 for a map with fixed bases */
    int sides;        /* owners with units of their own on the map */
    int width, height;
} rf_mapinfo_t;
int  RF_ListMaps(const char *root, rf_mapinfo_t *out, int max); /* root NULL: the game's; sorted */
/* For the map loader (w_rustfront.c). */
void RF_ClearStarts(void);
bool RF_ParseStart(const char *line, int width, int height);
int  RF_SpawnStarts(void);
mobj_t *RF_SpawnThing(uint16_t type, int owner, int facing, ivec2_t cell);

/* s_rustfront.c: Rustfront's sounds, synthesised by art/sfx.py into assets/sfx/ and
 * assets/music/ (docs/AUDIO.md). The engine plays weapon fire, impacts, deaths, voices and
 * announcements through rf_soundinfo's event table; the calls below are for the rest. */
typedef enum {
    RF_SND_NONE,
    RF_SND_CLICK,   /* first: soundinfo_t.ui refers to it as sfx 1 */
    RF_SND_UNIT_READY, RF_SND_STRUCTURE_READY, RF_SND_LOW_POWER, RF_SND_NO_FUNDS, RF_SND_UNDER_ATTACK,
    RF_SND_RIFLE, RF_SND_MG, RF_SND_CANNON, RF_SND_ARTILLERY, RF_SND_ROCKET, RF_SND_FLAME, RF_SND_BEAM,
    RF_SND_BEAM_HEAVY,
    RF_SND_HIT_BULLET, RF_SND_HIT_SHELL, RF_SND_BOOM_SMALL, RF_SND_BOOM_LARGE, RF_SND_COLLAPSE, RF_SND_ZAP,
    RF_SND_ZAP_HEAVY, RF_SND_FLAME_HIT, RF_SND_DIE_SOLDIER,
    RF_SND_BUILD_PLACE, RF_SND_BUILD_SCAFFOLD, RF_SND_BUILD_DONE,
    RF_SND_DIR_SELECT, RF_SND_DIR_MOVE, RF_SND_DIR_ATTACK,
    RF_SND_CL_SELECT, RF_SND_CL_MOVE, RF_SND_CL_ATTACK,
    RF_SND_LC_SELECT, RF_SND_LC_MOVE, RF_SND_LC_ATTACK,
    RF_SND_AMB_WIND, RF_SND_AMB_BASE,
    RF_SND_MUS_MENU, RF_SND_MUS_DIRECTORATE, RF_SND_MUS_CLANS, RF_SND_MUS_CONCORD,
    RF_SND_COUNT
} rf_sound_t;
extern const soundinfo_t rf_soundinfo;
void RF_PlaySound(rf_sound_t sound, const mobj_t *origin);  /* at origin, following it; NULL: local */
/* A local announcement (RF_SND_LOW_POWER, RF_SND_NO_FUNDS, ...): under attack and low power at
 * most once every 20 seconds of game time, any other every 2. True when it was let through. */
bool RF_Alert(rf_sound_t sound);
int  RF_SoundId(rf_sound_t sound);                           /* the engine's sfx id; 0 before S_Init */
int  RF_SoundFiles(rf_sound_t sound, const char *out[3]);    /* its variants, under assets/, no .wav */
rf_sound_t RF_EventSound(const mobj_t *actor, soundevent_t event);
rf_sound_t RF_MusicFor(const level_t *map, int faction);     /* NULL map: the menu theme */
rf_sound_t RF_MusicPlaying(void);                            /* the track playing now, or RF_SND_NONE */

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

/* w_rustfront.c: map unit names ("tank", "cl_hall"); RF_SpawnThing (above) spawns one as a map places it. */
uint16_t RF_UnitType(const char *name);              /* 0: no such unit */

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
/* The running mission's state in a saved game (g_save.c puts it after Rustfront's own). */
size_t RF_CampaignSaveSize(void);
void RF_CampaignSave(void *out);
bool RF_CampaignLoad(const void *data, size_t size);
void RF_CampaignBeginLoad(void);
bool RF_CampaignParseLine(const char *line, int width, int height);
void RF_CampaignEndLoad(const char *path);
void RF_CampaignTagged(const char *tag, const mobj_t *thing);
void RF_CampaignTicker(level_t *map, mobj_t *const *mobjs, int count, hudtext_t *hud, int dt_ms);

#endif
