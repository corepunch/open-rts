#ifndef __Z_LOCAL__
#define __Z_LOCAL__

#include "engine.h"
#include "z.h"

enum {
    Z_WORLDS = 5,     /* desert, arctic, city, volcano, jungle */
    Z_NEUTRAL = 4,    /* Team of a territory nobody holds. */
    Z_MAX_TERRITORIES = 64,
    Z_MAX_FORTS = 2,
    Z_LEVELS = 51,    /* LEVELS.DAT entries */
    Z_MAX_GROUPS = 12, /* Starting groups per level, LEVELS.DAT +0x55 */
    Z_MAX_PLACED = 26, /* CPUPLRnn.DAT +0x792, ZED.EXE 0x9d985. */
    Z_CLIFF_BLOCKS = 20, Z_CLIFF_BYTES = 138,
    Z_AI_TASKS = 240,
    Z_BRIDGES = 12,
    Z_NATIVE_HZ = 70,
    Z_BRIDGE_SCRIPTS = 8,
    Z_BRIDGE_REBUILD_FRAMES = 26,
};

typedef struct {
    irect_t bounds; /* Native pixel rectangle. */
    uint16_t initial_hp;
    uint16_t rebuild_ticks;
    bool vertical, cpu_rebuild;
} zbridge_t;

/* ZED.EXE turns a turret an eighth of a circle toward its target, then
 * waits three updates (0x58b7a); here it turns smoothly at that rate. */
#define Z_TURRET_TURN (ANG45 / 3u)

/* A group the computer starts with, CPUPLRnn.DAT +0x792. */
typedef struct {
    int group;
    ivec2_t at; /* Pixels, the first member's top-left. */
    uint8_t home; /* +7: initial destination territory + 1. */
} zplaced_t;

typedef struct {
    ivec2_t min, max; /* Cells, max exclusive. */
    int team;
    ivec2_t flag;     /* Cell; whoever reaches it takes the territory. */
    uint8_t options;  /* +0x18: bits 0 and 1 leave it without a flag. */
    uint8_t importance; /* CPUPLR territory +9. */
} zterritory_t;

/* Native computer records at 0x18d388, with native pointers represented
 * by territory indices and stable mobj IDs. These own orders, not units. */
typedef struct {
    uint32_t unit, boarding, service, enemy, execute_at;
    fixed2_t order;
    int32_t strength;
    int16_t current, target, destination;
    int16_t home;     /* Group +0xc: where a donated reserve returns, or -1. */
    int8_t task;      /* +0x11: -1 idle, 0 march, 1 flag, 2 staged, 3 assault,
                       * 4 garrison, 5 vehicle, 7 building support, 8 return, 9 crane work,
                       * 10 frontier, 11 vehicle repair. */
    uint8_t squad, flags, idle;
    uint8_t passes;   /* +0x12: heavy passes spent idle inside CPU land. */
    int8_t retry;
    bool pending;
} zaitask_t;

typedef struct {
    uint32_t attacker; /* Last unit to hit a building here. */
    uint16_t strength[4];
    uint16_t lockout; /* +0x16: frames left of the 510 set when its flag turns. */
    int32_t value;
    uint8_t owner, flags, known;
} zai_territory_t;

typedef struct {
    zaitask_t tasks[Z_AI_TASKS];
    zai_territory_t territories[Z_MAX_TERRITORIES];
    uint32_t clock, order_tail, stray_cursor;
    int last_tic, fraction, delay;
    int32_t total, mobile, imbalance;
    uint16_t seed;
    uint8_t intelligence, phase;
    bool initialized;
} zai_t;

/* What a level keeps of its native files after G_DoLoadLevel. */
typedef struct {
    char root[1024], dir[1024];
    char objects[16], buildings[16], robots[16]; /* LEVELS.DAT file names. */
    int number;                      /* LEVELS.DAT entry */
    int world;                       /* LEVELS.DAT +0x6e */
    int production_speed;            /* CPUPLR +0x8a2, ZED.EXE 0x103d81. */
    struct { uint8_t intelligence, allocation, territory_weight; } computer;
    zai_t ai;
    uint8_t groups[Z_MAX_GROUPS];    /* ROBOTS.DAT groups, 0xff ends */
    zplaced_t placed[Z_MAX_PLACED];
    int placed_count;
    zterritory_t territories[Z_MAX_TERRITORIES];
    int territory_count;
    ivec2_t forts[Z_MAX_FORTS];      /* Pixels; the first is drawn with its gate. */
    int fort_count;
    zbridge_t bridges[Z_BRIDGES];
    blob_t bridge_scripts[Z_BRIDGE_SCRIPTS];
    uint8_t terrain[Z_MAP_STRIDE * Z_MAP_STRIDE]; /* Attribute plane. */
    uint8_t ground[Z_MAP_STRIDE * Z_MAP_STRIDE], ground_attr[Z_MAP_STRIDE * Z_MAP_STRIDE];
    uint8_t cliffs[Z_CLIFF_BLOCKS * Z_CLIFF_BYTES + 1];
    blob_t rsc;                      /* SPRITES.RSC */
    blob_t roster;                   /* ROBOTS.DAT, including production requirements. */
    blob_t weights, square_roots;    /* MULT.DAT and WARDATA.PAC/SQROOT.BIN. */
} zmap_t;

bool Z_LoadLevel(const char *map_path, level_t *out);
int Z_LoadThings(void);
int Z_TeamAt(const zmap_t *map, ivec2_t pixel);
/* Give a thing to team 0..3, or Z_NEUTRAL for nobody (owner 7). */
void Z_SetTeam(mobj_t *mo, int team);
/* Centre of a unit of weapon type `t` whose first picture's top-left is
 * at `pixel`; false for a bad type. */
bool Z_UnitCentre(const zmap_t *map, int t, ivec2_t pixel, fixed2_t *centre);
/* Weapon type of a unit's mobj type, or -1. */
int Z_UnitType(const mobj_t *mo);
int Z_UnitAccuracy(int type);
ivec2_t Z_BlastAnchor(const mobj_t *mo);
fixed_t Z_LevelSpeed(int level);
void Z_BodyDamaged(mobj_t *mo, int old_hp);
bool Z_EjectCrew(mobj_t *vehicle);
int Z_GroupType(const zmap_t *map, int group, int member);
int Z_GroupRobot(const zmap_t *map, int group, int member);
bool Z_IsRobot(int t);
/* Size of a SPRITES.RSC picture; false for a bad index or file. */
bool Z_SpriteSize(const zmap_t *map, int index, isize2_t *size);
ivec2_t Z_BuildingOrigin(const mobj_t *building);
bool Z_LoadAssets(const char *root, const level_t *map, tileset_t *tileset,
                  spritesheet_t *unit_sprite);
bool Z_InitSprites(const level_t *map, spritecache_t *cache);
/* UI-owned SPRITES.RSC images in native index order; -1 leaves a blank cell. */
bool Z_LoadUISprites(const char *root, const int *indices, int count, spritesheet_t *out);

/* p_capture.c: flags, vehicles and grenades, run once a tic. */
void Z_Ticker(void);
bool Z_BoardOrder(mobj_t *const *units, int count, mobj_t *target);
bool Z_CanTarget(const mobj_t *attacker, const mobj_t *victim);
void Z_MobjTicker(mobj_t *mo);
void Z_ServiceTick(mobj_t *unit);
bool Z_ServiceOrder(mobj_t *unit, const mobj_t *target, fixed2_t goal);
bool Z_ServiceApproach(const mobj_t *unit, const mobj_t *target, fixed2_t *goal);
mobj_t *Z_ServiceTarget(uint32_t id);
bool Z_RepairVehicle(const mobj_t *unit);
void Z_CancelService(mobj_t *unit);
void Z_BridgeDamage(mobj_t *bridge);
void Z_RebuildBridge(mobj_t *bridge);
void Z_BridgeTick(mobj_t *bridge);
void Z_BridgeEffectTick(mobj_t *effect);
int Z_BridgeTerritory(const zmap_t *map, const mobj_t *bridge);
bool Z_BridgeOrder(const ticcmd_t *cmd, mobj_t *const *units, int count);

/* Crossings of a native clock during this simulation tic. */
static inline int Z_NativeSteps(int64_t tick, int hz) {
    return (int)((tick + 1) * hz / RTS_TICRATE - tick * hz / RTS_TICRATE);
}
bool Z_Attack(mobj_t *attacker, const weapondef_t *weapon, mobj_t *target);
void A_ZShellBlast(mobj_t *missile);
int Z_HitDamage(const mobj_t *attacker, const weapondef_t *weapon, mobj_t *target, int damage, int divisor);
/* p_fort.c: a fort takes no harm; it falls to a unit that gets inside. */
void Z_FortUnharmed(mobj_t *fort);
/* Cell at the middle of a fort's entrance, where a unit takes it. */
bool Z_FortEntrance(const mobj_t *fort, fixed2_t *at);
/* p_alert.c: an idle unit takes its squad's enemy or one close by. */
void Z_Alert(mobj_t *unit);
uint16_t Z_NextRandom(uint16_t *seed);
int Z_AIEstimate(int value, int intelligence);
int Z_AIUnitValue(const mobj_t *unit);
int Z_AIEmptyValue(const mobj_t *unit);
ivec2_t Z_AITopLeft(const mobj_t *unit);
int Z_AITerritory(const zmap_t *map, fixed2_t at);
uint32_t Z_AIRank(int value, ivec2_t from, ivec2_t to, const uint8_t *roots);
int Z_AIMobileBudget(int total, int imbalance, int home_human, int home_computer, int allocation);

/* p_prod.c: what forts and factories make (products.inc). */
int Z_ProductId(int type); /* Catalog ui_id of a unit's mobj type. */
bool Z_Producer(const mobj_t *mo);
int Z_ProductsFor(const mobj_t *producer, const StaticProductDefinition **out, int max);
const StaticProductDefinition *Z_ProductInProduction(const mobj_t *producer);
const StaticProductDefinition *Z_AIProduct(const mobj_t *producer);
bool Z_GunReady(const mobj_t *producer);
int Z_ProductTimeMs(const mobj_t *producer, const StaticProductDefinition *product);
bool Z_PlaceGun(mobj_t *producer, int type, ivec2_t cell);
bool Z_GunPosition(const mobj_t *producer, int type, ivec2_t cell, fixed3_t *out);
/* Flags a team holds (Z_NEUTRAL: nobody's), and all of them. */
int Z_TerritoriesHeld(int team, int *total);
int Z_ProductionRate(const mobj_t *producer, int held, int total);

/* w_panel.c: the 640x480 panel art from SHEADFX.PAC. `pieces` is
 * NSNEWPAN.DAT (the panel, labels, buttons, digits and bars), `heads` the
 * robot faces SHEADBI0..4.DAT and `backs` SHEADBAK.DAT, the scenery behind
 * a face. Pictures are one byte a pixel in the world palette. */
enum { Z_FACES = 5 };
typedef struct {
    spritesheet_t pieces, backs, heads[Z_FACES], window;
    spritesheet_t small_backs, small_heads[Z_FACES]; /* HEADFX production portraits. */
} zpanel_t;
/* SPRITES.RSC: product labels 787..808, preview background 1889, then
 * the building window and its small font, 4694..4816. */
enum { Z_UI_LABEL = 0, Z_UI_BACKGROUND = 22, Z_UI_WINDOW = 23, Z_UI_FIRST = 4694 };
bool Z_LoadPanelSprites(const char *root, spritesheet_t *out);
bool Z_LoadPanel(const char *root, zpanel_t *out);
void Z_FreePanel(zpanel_t *panel);
/* Give every sheet the world's palette. */
void Z_PanelPalette(zpanel_t *panel, const uint32_t palette[256]);

/* Build the destructible cliffs into the planes, as a level start does. */
void Z_StampCliffs(uint8_t *tiles, uint8_t *attrs, const uint8_t *blocks, int world);
void Z_RebuildCliffs(level_t *level);
int Z_LoadCliffs(void);
void Z_CliffDamage(mobj_t *cliff);
bool Z_CliffOrder(const ticcmd_t *cmd, mobj_t *const *units, int count);
bool Z_CliffWeapon(const mobj_t *unit);
bool Z_Enterable(const mobj_t *mo);
/* World sprite of a building type, or -1. */
int Z_BuildingSprite(int native, int world);

#endif
