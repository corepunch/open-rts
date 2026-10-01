#ifndef __DARK_COLONY__
#define __DARK_COLONY__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "engine.h"


struct level_s;
struct mobjtype_s;
struct app_s;
void DC_OpenQuitDialog(struct app_s *app);
void DC_OpenOptionsDialog(struct app_s *app);
/* Native screen-script frames and brightness to engine item looks. */
void DC_ControlLooks(menuitem_t *item, int normal, int pushed, int remap,
                     int bright_pushed, int bright_highlight);

/* Gameplay lookup is hand-authored, independent of generated state tables. */
const struct mobjtype_s *actor_type_by_id(uint16_t type_id);

/* SOUND2.DAT, SLIST.DAT and tileset ambience (sounds.c). */
extern const soundinfo_t dc_soundinfo;

/* Scenario TEAM records are native map data, but AI consumption belongs to
 * the simulation.  owner 0 is the human side; non-zero owners are DC's
 * computer-controlled side in the current runtime mapping. */
bool map_has_ai(const struct level_s *map, int owner);
int DC_PlayerRace(int owner);
void DC_SelectPurchase(int owner, int ui_id, bool refund);
void DC_SubmitPurchases(int owner);
void DC_RunPurchases(void);


enum {
    MAX_OBJECTS = 800,
    BUILDINGS_PER_SIDE = 15,
    BUILDING_OBJECT_COUNT = 8 * BUILDINGS_PER_SIDE,
    DYNAMIC_OBJECT_FIRST = 0x98,
    OBJECT_SIZE = 0xdc,
    OBJECT_TYPE_PETRA7_VENT = 40,
    FIXED_TILE_CENTER = 0x80,
};

typedef struct {
    int16_t x_pos;              /* +0x00, 8.8 fixed map x */
    int16_t unk_02;             /* +0x02 */
    int16_t z_pos;              /* +0x04, 8.8 fixed map z */
    uint8_t type;               /* +0x06, GAMESTAT object type */
    uint8_t team;               /* +0x07 */
    uint8_t unk_08;             /* +0x08 */
    uint8_t stat_byte;          /* +0x09, copied from the object type table */
    uint8_t regen_timer;        /* +0x0a, initialized to 0x40 */
    uint8_t unk_0b;             /* +0x0b */
    int32_t health_or_amount;   /* +0x0c, health for units/buildings, P-7 amount for vents */
    uint8_t anim_flags;         /* +0x10 */
    uint8_t unk_11;             /* +0x11 */
    uint8_t unk_12;             /* +0x12 */
    uint8_t unk_13;             /* +0x13 */
    uint8_t anim_state0[8];     /* +0x14 */
    uint8_t anim_state1[8];     /* +0x1c */
    uint8_t anim_state2[8];     /* +0x24 */
    uint8_t active;             /* +0x2c, 0 = free/inactive, 10 observed as destroyed/burned */
    uint8_t pad_2d[0x32 - 0x2d];
    int16_t vent_rate;          /* +0x32, resource rate for type-40 Petra-7 vents */
    uint8_t unk_34;             /* +0x34 */
    uint8_t facing_or_anim;     /* +0x35, initialized to 0xff */
    uint8_t unk_36;             /* +0x36 */
    uint8_t pad_37[0xc6 - 0x37];
    uint8_t unk_c6;             /* +0xc6 */
    uint8_t unk_c7;             /* +0xc7 */
    uint8_t unk_c8;             /* +0xc8 */
    uint8_t unk_c9;             /* +0xc9 */
    uint8_t unk_ca;             /* +0xca */
    uint8_t subtype;            /* +0xcb */
    uint8_t unk_cc;             /* +0xcc */
    uint8_t cell_x;             /* +0xcd, integer map x */
    uint8_t cell_z;             /* +0xce, integer map z */
    uint8_t unk_cf;             /* +0xcf */
    uint8_t cooldown_d0;        /* +0xd0 */
    uint8_t marker_d1;          /* +0xd1, initialized to 0xff */
    int16_t target_a;           /* +0xd2, initialized to -2 */
    int16_t target_b;           /* +0xd4, initialized to -2 */
    uint8_t timer_d6;           /* +0xd6 */
    uint8_t pad_d7[OBJECT_SIZE - 0xd7];
} DcObject;

_Static_assert(sizeof(DcObject) == OBJECT_SIZE, "DcObject must match DC.EXE stride");
_Static_assert(offsetof(DcObject, x_pos) == 0x00, "DcObject.x_pos offset");
_Static_assert(offsetof(DcObject, z_pos) == 0x04, "DcObject.z_pos offset");
_Static_assert(offsetof(DcObject, type) == 0x06, "DcObject.type offset");
_Static_assert(offsetof(DcObject, team) == 0x07, "DcObject.team offset");
_Static_assert(offsetof(DcObject, health_or_amount) == 0x0c, "DcObject.health offset");
_Static_assert(offsetof(DcObject, active) == 0x2c, "DcObject.active offset");
_Static_assert(offsetof(DcObject, vent_rate) == 0x32, "DcObject.vent_rate offset");
_Static_assert(offsetof(DcObject, cell_x) == 0xcd, "DcObject.cell_x offset");
_Static_assert(offsetof(DcObject, cell_z) == 0xce, "DcObject.cell_z offset");
_Static_assert(offsetof(DcObject, target_a) == 0xd2, "DcObject.target_a offset");
_Static_assert(offsetof(DcObject, target_b) == 0xd4, "DcObject.target_b offset");

ivec2_t DC_CitySlotOffset(int slot);
bool DC_ProductActorMatches(int actor, int required);


/* DC.EXE setup.c: player record +0x1c/+0x20/+0x24/+0x28. */
enum { DC_PLAYER_AI, DC_PLAYER_AI_PLUS, DC_PLAYER_HUMAN, DC_PLAYER_NONE };
typedef struct {
    int race, type, color, team;
    char name[17];
} dc_skirmish_player_t;

typedef struct {
    dc_skirmish_player_t players[8];
    int storage, artifacts, erupting, renewable;
    int flow, quantity, rank; /* Multipliers are native 25% steps, 1..20. */
    uint8_t seed;
} dc_skirmish_t;

/* A start request is consumed once, before the level's SCN is instantiated. */
void DC_RequestSkirmish(const char *map, const dc_skirmish_t *setup);
bool DC_TakeSkirmish(const char *map, dc_skirmish_t *setup);
struct level_s;
const dc_skirmish_t *DC_LevelSkirmish(const struct level_s *map);
/* Applies the AI+ credit multiplier to the level's income_scale. */
void DC_ApplyAiIncome(struct level_s *map, const dc_skirmish_t *setup);


static inline angle_t dc_direction_to_angle(int direction) {
    return direction_to_angle(direction, 16, ANG270, false);
}

static inline angle_t dc_fin_direction_to_angle(int direction) {
    return direction_to_angle(direction, 16, ANG270, true);
}

static inline int dc_angle_to_direction(angle_t angle) {
    return angle_to_direction(angle, 16, ANG270, false);
}


extern const uint32_t dc_random_table[256];

uint32_t M_DC_Random(uint8_t *index);
uint32_t P_DC_Random(void);


void A_DC_Damage(mobj_t *target);
int P_DC_BloodStates(uint16_t native_type, int out[7]);


bool DC_StartDropship(int team, ivec2_t origin,
                      const DropshipPayload *payload, int payload_count);


void DC_UpdateAI(const level_t *map, mobj_t *const *units, int unit_count);


typedef enum {
    MISSION_ACTIVE, MISSION_WON, MISSION_LOST, MISSION_ALLY_LOST,
} MissionState;
typedef struct ScriptState ScriptState;

ScriptState *DC_LoadScript(const char *map_path);
void DC_FreeScript(ScriptState *script);
MissionState DC_ScriptState(const ScriptState *script);
void DC_UpdateScript(ScriptState *mission, level_t *map, mobj_t *const *units,
                     int *unit_count, hudtext_t *hud, float dt);


/* The level owns this aggregate; each subsystem owns its private state. */
typedef struct {
    ScriptState *script;
} Mission;

Mission *load_mission(const char *map_path);
void destroy_mission(void *mission);
void update_mission(level_t *map, mobj_t *const *units, int *unit_count,
                    hudtext_t *hud, float dt);
MissionState mission_get_state(const void *mission);


/* Cell occupancy queries for mines, healers and splash damage. Movement and
 * pathfinding are shared by every game (play/p_nav.c, play/p_steer.c). */
ivec2_t DC_OccupiedPosition(const mobj_t *unit);
mobj_t *DC_Occupant(ivec2_t cell, bool airborne, bool buried);


void DC_TickSupport(int64_t clock);
/* 0x418c52: every sixteen native ticks, base owners earn their exo income. */
void DC_TickIncome(void);


void DC_AimMissile(mobj_t *missile, fixed3_t destination);
void DC_ScatterMissile(mobj_t *missile, fixed3_t destination);
int DC_WeaponLevel(const mobj_t *actor);
int DC_DefendedDamage(const mobj_t *victim, int damage);
bool DC_LoadWeapons(level_t *map, const char *root);
void DC_FreeWeapons(level_t *map);
mobj_t *DC_FireMissiles(mobj_t *source, mobj_t *target, uint16_t type);


bool load_render_tables(const char *data_root, const char *tileset_name);
void DC_RuntimePalette(uint32_t colors[256]);
bool DC_LoadFont(const char *root, const char *name,
                 bitmapfont_t *font);
bool load_dark_colony_sprite(const char *path,
                             spritesheet_t *out, uint32_t palette_out[256]);
bool DC_LoadSpriteImage(const char *path, spritesheet_t *out);
bool load_dark_colony_unit_sprites(const char *data_root,
                                   const level_t *map, mobj_t *const *units, int unit_count,
                                   spritecache_t *cache);

/* Native little-endian file records. These are views, not decoded copies. */
typedef struct {
    uint16_t magic, frame_count, label_count, dependency_count;
} dc_fin_header_t;

typedef struct {
    char name[8];
} dc_fin_dependency_t;

typedef struct {
    char name[16];
    uint16_t start, end;
} dc_fin_label_t;

typedef struct {
    char name[16];
    int16_t x, y;
} dc_fin_point_t;

typedef struct {
    uint16_t part_count, ticks;
    dc_fin_point_t points[8];
} dc_fin_frame_t;

typedef struct {
    int16_t x, y;
} dc_file_point_t;

typedef struct {
    char sprite[8];
    int16_t cell;
    dc_file_point_t offset;
    int16_t remap; /* Native draw mode; 2 selects the alternate clipping path. */
    int16_t intensity, layer, flags;
} dc_fin_command_t;

typedef struct {
    uint16_t w, h;
} dc_file_size_t;

typedef struct {
    uint16_t x, y;
} dc_file_displacement_t;

typedef struct {
    uint16_t flags, cell_count;
    uint32_t payload_size;
    uint8_t palette[256][3];
} dc_spr_header_t;

typedef struct {
    dc_file_size_t size;
    dc_file_displacement_t displacement;
} dc_spr_cell_t;

/* Owns only the file buffer and a relocation index for variable-length frames.
 * All record pointers borrow the file. Free with DC_FreeFIN. */
typedef struct {
    blob_t file;
    const dc_fin_header_t *header;
    const dc_fin_dependency_t *dependencies;
    const dc_fin_label_t *labels;
    const dc_fin_frame_t *frames;
    const dc_fin_command_t *commands;
    const dc_fin_command_t **frame_commands;
    int command_count;
} dc_fin_t;

/* Consume a checked span from a borrowed file cursor; do not free the cursor. */
const void *DC_TakeRecords(blob_t *cursor, size_t count, size_t record_size);
bool DC_LoadFIN(const char *path, dc_fin_t *out);
void DC_FreeFIN(dc_fin_t *fin);
const dc_fin_label_t *DC_FINLabel(const dc_fin_t *fin, const char *name);
bool DC_FINLayer(const dc_fin_t *fin, int index, spritelayer_t *out);
/* Replaces an initialized direction on success. Layers belong to the caller. */
bool DC_FINFrame(const dc_fin_t *fin, int index, spritedirection_t *out);


void DC_LoadSelectionOrigin(const char *stem, const dc_fin_t *fin, const spritecache_t *cache);
void DC_DrawUnitOverlays(const unitoverlaycontext_t *ctx);


void *DC_SB_Init(app_t *app, const char *data_root);
bool  DC_SB_Responder(void *sb, app_t *app, level_t *map,
                   mobj_t *const *units, int unit_count, const SDL_Event *event);
void  DC_SB_Drawer(void *sb, app_t *app, const level_t *map,
                mobj_t *const *units, int unit_count,
                const spritecache_t *sprites, const hudtext_t *hud);
void  DC_SB_Shutdown(void *sb);
int   DC_SB_WorldViewportWidth(const app_t *app);


#endif
