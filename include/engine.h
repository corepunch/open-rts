#ifndef __ENGINE__
#define __ENGINE__

#include <SDL.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>


typedef struct { int x, y; }   ivec2_t;
typedef struct { float x, y; } fvec2_t;
typedef struct { int w, h; }   isize2_t;

typedef int32_t fixed_t;
typedef struct { fixed_t x, y, z; } fixed3_t;

enum {
    FIXED_FRAC_BITS = 16,
    FIXED_ONE = 1 << FIXED_FRAC_BITS,
};

/* Engine-facing rectangle names with exact SDL ABI compatibility. */
typedef SDL_Rect  irect_t;
typedef SDL_FRect frect_t;

static inline isize2_t isize2_max(isize2_t a, isize2_t b) {
    return (isize2_t){ a.w > b.w ? a.w : b.w, a.h > b.h ? a.h : b.h };
}

static inline fvec2_t fvec2_add(fvec2_t a, fvec2_t b) { return (fvec2_t){ a.x + b.x, a.y + b.y }; }
static inline fvec2_t fvec2_sub(fvec2_t a, fvec2_t b) { return (fvec2_t){ a.x - b.x, a.y - b.y }; }
static inline fvec2_t fvec2_scale(fvec2_t value, float scale) {
    return (fvec2_t){ value.x * scale, value.y * scale };
}
static inline float fvec2_length_squared(fvec2_t value) {
    return value.x * value.x + value.y * value.y;
}
static inline float fvec2_distance_squared(fvec2_t a, fvec2_t b) {
    return fvec2_length_squared(fvec2_sub(a, b));
}
static inline bool fvec2_near(fvec2_t a, fvec2_t b, float epsilon) {
    fvec2_t delta = fvec2_sub(a, b);
    return delta.x > -epsilon && delta.x < epsilon &&
           delta.y > -epsilon && delta.y < epsilon;
}
static inline fvec2_t fvec2_cell_center(ivec2_t cell) {
    return (fvec2_t){ (float)cell.x + 0.5f, (float)cell.y + 0.5f };
}
static inline ivec2_t fvec2_cell(fvec2_t value) {
    return (ivec2_t){ (int)floorf(value.x), (int)floorf(value.y) };
}
static inline ivec2_t ivec2_add(ivec2_t a, ivec2_t b) {
    return (ivec2_t){ a.x + b.x, a.y + b.y };
}

static inline ivec2_t ivec2_sub(ivec2_t a, ivec2_t b) {
    return (ivec2_t){ a.x - b.x, a.y - b.y };
}

static inline ivec2_t ivec2_scale(ivec2_t v, int scale) {
    return (ivec2_t){ v.x * scale, v.y * scale };
}

static inline bool ivec2_equal(ivec2_t a, ivec2_t b) {
    return a.x == b.x && a.y == b.y;
}

static inline fixed_t fixed_from_float(float value) {
    double scaled = (double)value * (double)FIXED_ONE;
    if (isnan(scaled)) return 0;
    if (scaled >= (double)INT32_MAX) return INT32_MAX;
    if (scaled <= (double)INT32_MIN) return INT32_MIN;
    return (fixed_t)llround(scaled);
}

static inline float fixed_to_float(fixed_t value) {
    return (float)value / (float)FIXED_ONE;
}

static inline fixed_t fixed_add_saturated(fixed_t a, fixed_t b) {
    int64_t sum = (int64_t)a + (int64_t)b;
    if (sum > INT32_MAX) return INT32_MAX;
    if (sum < INT32_MIN) return INT32_MIN;
    return (fixed_t)sum;
}

static inline fixed_t fixed_sub_saturated(fixed_t a, fixed_t b) {
    int64_t difference = (int64_t)a - (int64_t)b;
    if (difference > INT32_MAX) return INT32_MAX;
    if (difference < INT32_MIN) return INT32_MIN;
    return (fixed_t)difference;
}

static inline fixed3_t fixed3_from_fvec2(fvec2_t value, fixed_t z) {
    return (fixed3_t){ fixed_from_float(value.x), fixed_from_float(value.y), z };
}

static inline fvec2_t fixed3_xy_to_fvec2(fixed3_t value) {
    return (fvec2_t){ fixed_to_float(value.x), fixed_to_float(value.y) };
}

static inline fixed3_t fixed3_add(fixed3_t a, fixed3_t b) {
    return (fixed3_t){
        fixed_add_saturated(a.x, b.x),
        fixed_add_saturated(a.y, b.y),
        fixed_add_saturated(a.z, b.z),
    };
}

static inline fixed3_t fixed3_zero(void) {
    return (fixed3_t){ 0, 0, 0 };
}

static inline fixed3_t fixed3_sub(fixed3_t a, fixed3_t b) {
    return (fixed3_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline fixed3_t fixed3_planar_delta(fvec2_t delta) {
    return fixed3_from_fvec2(delta, 0);
}

static inline fixed3_t fixed3_add_planar(fixed3_t position,
                                                fixed3_t displacement) {
    return (fixed3_t){
        fixed_add_saturated(position.x, displacement.x),
        fixed_add_saturated(position.y, displacement.y),
        position.z,
    };
}

static inline fixed3_t fixed3_with_xy(fixed3_t value, fvec2_t xy) {
    return (fixed3_t){ fixed_from_float(xy.x), fixed_from_float(xy.y), value.z };
}

static inline fixed3_t fixed3_planar_displacement(fixed3_t from,
                                                         fixed3_t to) {
    return (fixed3_t){
        fixed_sub_saturated(to.x, from.x),
        fixed_sub_saturated(to.y, from.y),
        0,
    };
}

static inline irect_t irect_from_points(ivec2_t a, ivec2_t b) {
    return (irect_t){
        a.x < b.x ? a.x : b.x,
        a.y < b.y ? a.y : b.y,
        a.x < b.x ? b.x - a.x : a.x - b.x,
        a.y < b.y ? b.y - a.y : a.y - b.y,
    };
}

static inline bool irect_contains(irect_t rect, ivec2_t point) {
    return point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.w && point.y < rect.y + rect.h;
}

static inline bool irect_intersects(irect_t a, irect_t b) {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

static inline frect_t frect_from_points(fvec2_t a, fvec2_t b) {
    return (frect_t){
        a.x < b.x ? a.x : b.x,
        a.y < b.y ? a.y : b.y,
        a.x < b.x ? b.x - a.x : a.x - b.x,
        a.y < b.y ? b.y - a.y : a.y - b.y,
    };
}

static inline bool frect_contains(frect_t rect, fvec2_t point) {
    return point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.w && point.y < rect.y + rect.h;
}

static inline bool frect_intersects(frect_t a, frect_t b) {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}


typedef struct app_s {
    SDL_Window *window;
    SDL_Renderer *renderer; /* Present only. Drawing goes through screens[0]. */
    isize2_t win; /* Logical draw size. The OS window may be a different size. */
    isize2_t cell;
    fvec2_t  cam;
    bool show_grid;
    bool show_blocked;
    bool running;
    bool dragging_select;
    bool panning;
    ivec2_t  mouse_down;
    ivec2_t  mouse;
    uint32_t ticks_ms;
    irect_t selection_rect;
} app_t;


typedef struct blob_s {
    uint8_t *bytes; /* W_ReadFile appends a NUL byte outside size. */
    size_t size;
} blob_t;


#define CELL_W 24
#define CELL_H 24
#define TILE_PIX_W 24
#define TILE_PIX_H 24
#define TILE_ATLAS_COLS 24
#define MAX_DECORATIONS 8192
#define MAX_DECORATION_SPRITES 512
#define MAX_TILE_OVERLAYS 3
#define MAX_TILE_ANIMATION_FRAMES 8
#define MAX_SPRITE_ROTATIONS 32
#ifdef RTS_GAME_DARK_COLONY
#define RTS_MAX_PRODUCTION_QUEUE 50
#else
#define RTS_MAX_PRODUCTION_QUEUE 9
#endif
#define MAX_PATH_CELLS 4096
#define RTS_TICRATE 30
#define WORLD_CLOCK_MS 66 /* DC.EXE's default environment clock. */
#define FIXED_DT (1.0f / RTS_TICRATE)

#ifndef RTS_WORLD_Y_UP
#define RTS_WORLD_Y_UP 0
#endif

#if RTS_WORLD_Y_UP != 0 && RTS_WORLD_Y_UP != 1
#error "RTS_WORLD_Y_UP must be 0 or 1"
#endif

#define RTS_SPRITEFRAME_FLIP_X (1u << 0) /* Doom-style spriteframe_t.flip[rotation]. */
#define RTS_FRAME_FLIP_X RTS_SPRITEFRAME_FLIP_X
#define RTS_FRAME_BLINK (1u << 3)


/* Hexen/Doom binary angle measurement: east=0, north=ANG90, increasing CCW. */
typedef uint32_t angle_t;

#define ANG45  UINT32_C(0x20000000)
#define ANG90  UINT32_C(0x40000000)
#define ANG180 UINT32_C(0x80000000)
#define ANG270 UINT32_C(0xc0000000)
#define ANGLE_MAX UINT32_MAX

/* Movement vectors use screen coordinates: +x=east, +y=south. */
angle_t angle_from_screen_vector(float dx, float dy);
void angle_to_screen_vector(angle_t angle, float *dx, float *dy);
uint32_t angle_distance(angle_t a, angle_t b);

/* Quantize a BAM angle into an authored rotation table.  first_angle is the
 * direction represented by slot zero; count may be up to 32. */
int angle_to_direction(angle_t angle, int count, angle_t first_angle, bool clockwise);
angle_t direction_to_angle(int direction, int count, angle_t first_angle, bool clockwise);


typedef struct app_s app_t;
typedef struct tileset_s tileset_t;
typedef struct flowfield_s flowfield_t;

typedef ivec2_t cell_t;

/* Native DC terrain flag positions, shared by the engine sight traversal. */
enum {
    MAP_SIGHT_PASS = 1u << 7,
    MAP_SIGHT_NEAR = 1u << 8,
};

#define SIGHT_EXPLORED UINT32_C(0x80000000)
typedef struct {
    uint32_t *cells; /* Current team bits and persistent local exploration. */
    uint32_t allies[8];
} sightmap_t;

typedef struct {
    /* weight: 0 = day, 256 = night (DC.EXE level +0x540). */
    int phase, tics, duration, transition, weight;
} daylight_t;

enum {
    MAP_RENDER_CAP_CELL_COLORS = 1 << 0,
    MAP_RENDER_CAP_TERRAIN_TRANSITIONS = 1 << 1,
    MAP_RENDER_CAP_ZERO_TILE_EMPTY = 1 << 2,
    MAP_RENDER_CAP_DEPTH_SORTED_TILE_LAYERS = 1 << 3,
    MAP_RENDER_CAP_TILE_TRANSFORMS = 1 << 4,
};

enum {
    MAP_TILE_TRANSFORM_FLIP_X = 1 << 0,
    MAP_TILE_TRANSFORM_FLIP_Y = 1 << 1,
};

typedef enum {
    MAP_EXTRA_DECORATION,
    MAP_EXTRA_RESOURCE_VENT,
    MAP_EXTRA_OVERLAY,
} MapExtraKind;

typedef struct mapextra_s {
    MapExtraKind kind;
    ivec2_t cell;
    int layer;
    int value;
    uint32_t flags;
} mapextra_t;

#define MAP_DECORATION_MAX_ANIMATION_FRAMES 32

typedef struct mapdecorationframe_s {
    ivec2_t sprite_pivot;
    int sprite_frame;
    int duration_ms;
} mapdecorationframe_t;

typedef struct mapdecoration_s {
    ivec2_t cell;
    isize2_t footprint;
    bool solid;
    bool hidden;
    bool center_anchor;
    /* Optional authored sprite pivot.  When set, (pivot_x, pivot_y) in the
       sprite canvas is attached directly to the decoration's (gx, gy) world
       point.  This lets a game plugin preserve its native placement convention
       without deriving an origin from opaque bounds or a guessed footprint. */
    bool has_sprite_pivot;
    ivec2_t sprite_pivot;
    int animation_frame_count;
    mapdecorationframe_t animation_frames[MAP_DECORATION_MAX_ANIMATION_FRAMES];
    int frame_interval_ms;
    int frame_index;
    int frame2_index;
    int frame3_index;
    angle_t angle;
    int render_remap;
    uint32_t render_flags;
    int render_selector;
    uint32_t render2_flags;
    int render2_selector;
    uint32_t render3_flags;
    int render3_selector;
    char sprite_name[32];
    char sprite2_name[32];
    char sprite3_name[32];
    char shadow_name[32];
} mapdecoration_t;

#define RTS_MAX_RESOURCES 8

typedef struct resourcevent_s {
    ivec2_t cell;
    /* Visual/interaction attachment point inside the authored vent stamp.
       cell remains the integer scenario coordinate used by scripts. */
    fvec2_t attachment;
    /* Occupied cells for click and harvest range.  0x0 means 1x1. */
    isize2_t footprint;
    int amount;
    int rate;
    bool active;
    int resource_type; /* 0-based index into player_resources[][resource_type] */
    /* Structure that yields this deposit (mobj id), or 0 for a terrain vent.
       Only the structure's allies may harvest it; it closes when the
       structure dies. Kept by P_SyncDepositStructures. */
    uint32_t source_id;
} resourcevent_t;

typedef struct level_s {
    int width;
    int height;
    uint16_t *tile_ids;
    uint16_t *tile_overlays[MAX_TILE_OVERLAYS];
    uint8_t *tile_transforms[MAX_TILE_OVERLAYS + 1];
    int tile_overlay_count;
    uint8_t *blocked;
    uint16_t *tile_flags;
    sightmap_t sight;
    daylight_t daylight;
    uint32_t *cell_colors;
    uint32_t render_capabilities;
    mapdecoration_t *decorations;
    int decoration_count;
    resourcevent_t *resource_vents;
    int resource_vent_count;
    mapextra_t *extras;
    int extra_count;
    bool has_camera;
    fvec2_t camera;
    int player_resources[8][RTS_MAX_RESOURCES];
    uint16_t income_scale[8]; /* 8.8 credit multiplier per owner; 0 means 1.0. */
    bool player_teams; /* Independent players use the alliance masks. */
    uint8_t player_colors[8];
    char tileset_name[32];
    void *native_data;
    void (*destroy_native_data)(void *);
    void *mission;
    void (*destroy_mission)(void *);
    flowfield_t *flow_fields;
#ifdef RTS_GAME_DARK_COLONY
    struct dc_pathmap_s *paths;
    struct dc_weapons_s *weapons;
    struct { uint8_t weapon, armor; } upgrades[106][8];
    struct { uint8_t selected, queued; } purchases[8][110]; /* Native DEPEND rows. */
#endif
    void (*render_transitions)(app_t *app, const struct level_s *map, const tileset_t *tileset,
                               int x, int y, int dx, int dy);
    uint32_t next_mobj_id;
    uint32_t next_move_order_id;
    uint8_t random_index;
} level_t;

static inline int L_ScreenY(const level_t *map, int y) {
#if RTS_WORLD_Y_UP
    return map->height - 1 - y;
#else
    (void)map;
    return y;
#endif
}

static inline float L_ScreenYF(const level_t *map, float y) {
#if RTS_WORLD_Y_UP
    return (float)map->height - y;
#else
    (void)map;
    return y;
#endif
}

static inline float L_WorldYF(const level_t *map, float y) {
    return L_ScreenYF(map, y);
}

extern level_t level;


/* DC has eight route destinations. DR's native traversal modes are 0/1/2. */
enum { MAXWAYPOINTS = 8 };
typedef enum { WP_BACKTRACK, WP_LOOP, WP_ONCE } waypointmode_t;
typedef struct {
    ivec2_t points[MAXWAYPOINTS];
    int count, current;
    waypointmode_t mode;
    bool backwards;
} waypoints_t;


typedef struct mobj_s mobj_t;
typedef struct app_s app_t;
typedef struct spritecache_s spritecache_t;
typedef struct gameinfo_s gameinfo_t;
typedef bool (*harvestdropoffmatchf_t)(const mobj_t *unit, int resource_type,
                                     const mobj_t *base, fvec2_t *position);
typedef struct production_s production_t;
typedef void (*actionf_p1)(mobj_t *mo);
typedef struct thinker_s {
    struct thinker_s *prev, *next;
    actionf_p1 function;
} thinker_t;

typedef enum {
    MF_SELECTABLE = 1u << 0,
    MF_MOBILE = 1u << 1,
    MF_RENDERABLE = 1u << 2,
    MF_ATTACK = 1u << 3,
    MF_HARVESTER = 1u << 4,
    MF_RESOURCE_BASE = 1u << 5,
    MF_FLY = 1u << 6,
    MF_SELECTED = 1u << 7,
    MF_DONTDRAW = 1u << 8,
    MF_NOBLOCKMAP = 1u << 9, /* Non-interacting puff, blood, or light mobj. */
    MF_MISSILE = 1u << 10,
    MF_TURRET = 1u << 11,
    MF_LANDMINE = 1u << 12,
    MF_DETECTOR = 1u << 13,
    MF_HUMAN = 1u << 14,
    MF_HEAL = 1u << 15,
    MF_REPAIR = 1u << 16,
    MF_NOAUTOTARGET = 1u << 17,
    /* Structure that yields a resource deposit (KKnD drill rig): the engine
     * keeps a resource vent on it that only its allies' harvesters use. */
    MF_RESOURCE_SOURCE = 1u << 18,
} mobjflag_t;

enum {
    ALLEGIANCE_PLAYER = 0,
    ALLEGIANCE_ENEMY  = 1,
    ALLEGIANCE_ALLIED = 2,
    ALLEGIANCE_NEUTRAL = 3,
};

typedef struct { int capacity, load_amount, unload_amount; } harvestresource_t;

/* Authored weapon data. Distances are map cells; fixed values are 16.16. */
typedef struct {
    const uint8_t *weights;
    int size;
    const uint16_t *damage_factors; /* 8.8 multipliers by armor class. */
    int armor_classes;
} blastdef_t;

typedef struct {
    fixed_t step;
    int period_ms, lifetime;
    bool timed;
    const fixed_t *arc; /* Height per flight tick, indexed by remaining fraction. */
    int arc_count;
    const int8_t *weave;
    int weave_count, weave_step;
    uint16_t trail_type;
} missiledef_t;

typedef struct mobjtype_s {
    uint16_t id;
    const char *name;
    const char *sprite_name;
    const char *shadow_name;
    uint32_t traits;
    float speed;
    angle_t turn_step; /* Per simulation tic; zero uses the legacy turn cadence. */
    int max_hp;
    unsigned armor_class;
    uint16_t defense[3]; /* Native damage multipliers; zero uses 256. */
    struct {
        int day, night;
        bool airborne;
    } sight;
    struct {
        float range;
        int damage;
        int upgrade_damage[2];
        int cooldown_ms;
        uint16_t projectile_type;
        int health_cost;
        int shots, reload_ms;
    } attack;
    missiledef_t missile;
    blastdef_t blast;
    struct { int state; uint16_t type; } deploy;
    /* MF_RESOURCE_SOURCE structures: the deposit the engine opens on them. */
    struct { int amount; int rate; int resource_type; } deposit;
    struct {
        int state_id;
        int unload_state_id;
        angle_t dock_angle;
        harvestresource_t resources[RTS_MAX_RESOURCES];
    } harvest;
    uint16_t native_type_id;
    actionf_p1 damage_action;
} mobjtype_t;

/* A state is a run of `count` consecutive sprite frames sharing one action,
 * like Quake II's mmove_t. The action runs on entering every frame; nextstate
 * follows the last. Doom's one-frame state is the count == 1 case. */
typedef struct state_s {
    int sprite;
    int frame; /* First logical frame of the run. */
    int count; /* Frames in the run; zero means one. */
    int tics;  /* Duration of each frame, unless frame_tics lists them. */
    actionf_p1 action;
    int nextstate;
    int group; /* Gameplay animation group, independent of sprite presentation. */
    const int16_t *frame_tics; /* Optional per-frame durations, `count` entries. */
} state_t;

/* Per-frame durations of a state-table row with uneven native timing. */
#define TICS(...) (const int16_t[]){ __VA_ARGS__ }

static inline int P_StateFrames(const state_t *state) {
    return state->count > 1 ? state->count : 1;
}

static inline int P_StateTics(const state_t *state, int frame) {
    return state->frame_tics ? state->frame_tics[frame] : state->tics;
}

typedef struct mobjinfo_s mobjinfo_t;

typedef enum {
    RTS_STATE_COORDS_GROUND_OFFSET = 0,
    RTS_STATE_COORDS_FIN_TOP_LEFT = 1,
} StateCoordMode;

typedef enum {
    SELECTION_STYLE_DEFAULT = 0, /* Green sprite rectangle and health bar. */
    SELECTION_STYLE_SPRITE,
    SELECTION_STYLE_BRACKETS,
} SelectionStyle;

typedef struct selectionmarker_s {
    SelectionStyle style;
    const char *image;
} selectionmarker_t;

typedef struct unitoverlaycontext_s {
    app_t *app;
    const mobj_t *unit;
    const spritecache_t *cache;
    const gameinfo_t *game_info;
    fvec2_t anchor;
    irect_t bounds; /* Current sprite bounds in screen pixels. */
} unitoverlaycontext_t;

typedef void (*unitoverlaydrawf_t)(const unitoverlaycontext_t *ctx);

struct gameinfo_s {
    const char *const *sprnames;
    int sprite_count;
    const state_t *states;
    int state_count;
    const mobjinfo_t *mobjinfo;
    int mobj_type_count;
    int null_state;
    StateCoordMode state_coord_mode;
    selectionmarker_t selection_marker;
    unitoverlaydrawf_t draw_overlays; /* Replaces the engine selection and health overlay. */
    bool right_click_orders; /* Default: left selects/orders, right deselects. */
    harvestdropoffmatchf_t harvest_dropoff_matches;
    const uint32_t *random_table; /* Optional native 256-entry gameplay RNG. */
    int game_speed; /* Default simulation speed in percent, 10..200; 0 means 100. */
};

/* State-machine and presentation fields of an ordinary mobj. */
typedef struct mobjcore_s {
    fixed3_t position;
    fixed3_t momentum;
    angle_t angle;
    int state_id;
    int state_frame; /* Index into the current state's run of frames. */
    int tics;
    int sprite_id;
    int frame;
    uint32_t render_flags;
    int render_remap;
    int render_intensity;
    ivec2_t render_offset;
    char sprite_name[32];
} mobjcore_t;

struct production_s {
    uint16_t actor_id;
    uint8_t product_class;
    int product_type;
    int queue_count;
    int time_ms;
    int time_left_ms;
    bool blocked;
    bool release_active;
    bool release_ready;
};

#include "mobj_data.h" // per-game object fields (needs m_vec types above)

struct mobj_s {
    thinker_t thinker;
    mobjcore_t core;
    const mobjtype_t *info;
    float speed;
    uint32_t id;
    uint16_t type_id;
    uint16_t native_type_id;
    uint8_t owner;
    uint8_t team;
    uint8_t allegiance;
    uint32_t traits;
    int hp;
    int max_hp;
    mobj_t *target; /* Doom: missile originator, or another mobj target. */
    struct { int clock, age, duration, phase, damage, wait; } missile;
    struct {
        int cooldown_left_ms;
        int shots;
        mobj_t *target;
    } attack;
    struct {
        int target;
        int timer_ms;
        int cargo;
        int resource_type;
        int phase;
        mobj_t *base;
        fvec2_t return_position;
    } harvest;
    bool remove;
    production_t *production;
    float radius;
    waypoints_t waypoints;
    bool move_only;
    struct {
        fvec2_t goal;
        const flowfield_t *flow_field;
        uint32_t order_id;
        bool order_arrived;
        int turn_timer_ms;
    } movement;
    MOBJ_GAME_FIELDS
};

static inline void P_ClearMove(mobj_t *unit) {
    unit->movement.flow_field = NULL;
#ifdef RTS_GAME_DARK_COLONY
    unit->route = (dc_route_t){0};
#endif
}

static inline bool P_MobjIsSelected(const mobj_t *mobj) {
    return mobj && (mobj->traits & MF_SELECTED) != 0;
}

static inline void P_MobjSetSelected(mobj_t *mobj, bool selected) {
    if (!mobj) return;
    if (selected) mobj->traits |= MF_SELECTED;
    else mobj->traits &= ~MF_SELECTED;
}

static inline bool P_MobjIsHidden(const mobj_t *mobj) {
    return mobj && (mobj->traits & MF_DONTDRAW) != 0;
}

static inline void P_MobjSetHidden(mobj_t *mobj, bool hidden) {
    if (!mobj) return;
    if (hidden) mobj->traits |= MF_DONTDRAW;
    else mobj->traits &= ~MF_DONTDRAW;
}

static inline bool P_AreAllegiancesAllied(uint8_t a, uint8_t b) {
    if (a == ALLEGIANCE_NEUTRAL || b == ALLEGIANCE_NEUTRAL)
        return false;
    if (a == b) return true;
    if ((a == ALLEGIANCE_PLAYER || a == ALLEGIANCE_ALLIED) &&
        (b == ALLEGIANCE_PLAYER || b == ALLEGIANCE_ALLIED))
        return true;
    return false;
}

bool P_IsAlly(const mobj_t *a, const mobj_t *b);
/* Live mobj with this id, or NULL. */
mobj_t *P_MobjById(uint32_t id);
/* Whether `unit` may harvest `vent`: it is active and either a terrain vent
 * or one whose source structure is alive and allied with the unit. */
bool P_VentOpenTo(const struct level_s *map, const struct resourcevent_s *vent,
                  const mobj_t *unit);
/* Opens a vent on every living MF_RESOURCE_SOURCE structure (reusing closed
 * slots) and closes the vents of dead ones. Runs every tick. */
void P_SyncDepositStructures(struct level_s *map);

extern thinker_t thinkercap;
extern int leveltime;
void P_InitThinkers(void);
void P_AddThinker(thinker_t *thinker);
void P_RemoveThinker(thinker_t *thinker);
void P_RunThinkers(void);
void P_FreeThinkers(void);
void P_MobjThinker(mobj_t *mobj);
void P_TickWaypoints(mobj_t *actor);
bool P_CanTarget(const mobj_t *attacker, const mobj_t *victim);
mobj_t *P_SpawnMobj(fixed3_t position, uint16_t type);
mobj_t *P_SpawnMissile(mobj_t *source, mobj_t *target, uint16_t type);
void P_ExplodeMissile(mobj_t *missile);
void A_Explode(mobj_t *actor);
bool P_Deploy(mobj_t *actor);
void A_Deploy(mobj_t *actor);
void P_RemoveMobj(mobj_t *mobj);
void P_InitMobj(const gameinfo_t *game_info, mobj_t *unit);
/* Non-owning snapshots for rendering/UI and batch RTS orders. The thinker list
 * alone owns objects; these pointers never move or compact their storage. */
typedef struct { mobj_t **items; int count; } mobjlist_t;
mobjlist_t P_ListMobjs(void);
void P_FreeMobjList(mobjlist_t *list);

bool P_SetMobjState(mobj_t *unit, int state_id);
/* Enters one frame of a state's run; a frame past its end enters nextstate. */
bool P_SetMobjStateFrame(mobj_t *unit, int state_id, int frame);
bool P_TickMobjState(mobj_t *unit);
production_t *P_EnsureMobjProduction(mobj_t *unit);
void P_FreeMobjProduction(mobj_t *unit);

/* State-entry actions, matching Hexen's state_t action model. */
void A_Look(mobj_t *unit);
void A_Chase(mobj_t *unit);
void A_Attack(mobj_t *unit);

/* Move toward movement.goal at unit->speed.  Physical displacement belongs
 * to the movement system, not to a state-entry action. */


/* RTS replacement for Doom's movement/buttons. A group order is atomic. */
#define MAXCOMMANDUNITS 1024
typedef enum {
    TC_NONE, TC_ORDER, TC_MOVE, TC_HARVEST, TC_ATTACK, TC_STOP, TC_BUILD, TC_DEPLOY,
    TC_PURCHASE, TC_SUBMIT, TC_MODE, TC_WAYPOINT, TC_PAUSE,
    TC_PATH,
    TC_MAX = TC_PATH
} ticorder_t;

typedef struct {
    uint32_t consistancy;
    ticorder_t order;
    fixed3_t position;
    uint32_t target;
    int product;
    unsigned count;
    uint32_t units[MAXCOMMANDUNITS];
    waypoints_t path; /* TC_PATH installs the complete route atomically. */
} ticcmd_t;

extern bool paused;

void G_BuildTiccmd(ticcmd_t *cmd);
bool G_QueueTiccmd(const ticcmd_t *cmd);
void G_ClearTiccmds(void);
void G_RunTiccmd(int player, const ticcmd_t *cmd);
uint32_t G_Consistency(void);
bool G_NetSignature(const char *map_path, uint32_t *signature);
bool G_SelectedTiccmd(ticorder_t order, mobj_t *const *units, int count,
                     fvec2_t position, uint32_t target);
bool G_BuildOrder(mobj_t *producer, int product);
bool G_PathOrder(mobj_t *const *units, int count, const waypoints_t *path);


#define MAXPLAYERS 4
#define MAXNETNODES 8
#define BACKUPTICS 12
#define DOOMCOM_ID UINT32_C(0x12345678)
#define NCMD_EXIT UINT32_C(0x80000000)
#define NCMD_RETRANSMIT UINT32_C(0x40000000)
#define NCMD_SETUP UINT32_C(0x20000000)
#define NCMD_KILL UINT32_C(0x10000000)
#define NCMD_CHECKSUM UINT32_C(0x0fffffff)

typedef struct {
    uint32_t checksum;
    uint8_t retransmitfrom, starttic, player, numtics;
    ticcmd_t cmds[BACKUPTICS];
} doomdata_t;

typedef struct {
    uint32_t id;
    int command, remotenode, datalength;
    int numnodes, numplayers, consoleplayer, ticdup, extratics;
    doomdata_t data;
} doomcom_t;

enum { CMD_SEND = 1, CMD_GET = 2 };
extern doomcom_t *doomcom;
extern doomdata_t *netbuffer;
extern bool netgame, netready, nodeingame[MAXNETNODES], playeringame[MAXPLAYERS];
extern bool netactive;
extern int consoleplayer, gametic, maketic, ticdup;
extern int game_speed;
extern int nettics[MAXNETNODES];
extern ticcmd_t netcmds[MAXPLAYERS][BACKUPTICS];
extern char neterror[256];

/* Remove network switches from argv before the game parses its arguments. */
bool I_InitNetwork(int *argc, char **argv);
/* Host distributes a data-root-relative map before any level is loaded. */
bool I_StartNetGame(const char *game, char *map, size_t capacity);
bool I_NetJoining(void);
typedef struct {
    char address[64], name[32], map[512];
    int players, capacity;
    uint64_t id, seen;
} netgame_t;
/* Menu sessions use the same transport as --host/--join, without blocking. */
bool I_HostNetGame(const char *game, const char *name, const char *map, int players);
/* Host-only: up to 64 opaque bytes delivered to every joiner (I_NetSetup). */
bool I_SetNetSetup(const void *data, size_t size);
size_t I_NetSetup(void *data, size_t capacity);
bool I_JoinNetGame(const char *game, const char *address);
bool I_OpenNetBrowser(const char *game);
void I_QueryNetGames(const char *address);
const netgame_t *I_NetGames(int *count);
int I_PollNetGame(char *map, size_t capacity); /* -1 error, 0 waiting, 1 ready */
int I_NetPlayerCount(void);
bool I_NetMenuSession(void);
void I_CancelNetGame(void);
void I_NetCmd(void);
void I_ShutdownNetwork(void);
void D_CheckNetGame(uint32_t signature);
void D_SetGameSpeed(int speed);
void D_QuitNetGame(void);
void NetUpdate(void);
/* Returns available simulation tics; the driver runs each and advances gametic. */
int TryRunTics(void);
bool D_RunTiccmds(void);
bool D_PlayerIsHuman(int owner);
int ExpandTics(int low);


typedef enum {
    RENDER_LAYER_TERRAIN = 0,
    RENDER_LAYER_TERRAIN_OVERLAY = 10,
    RENDER_LAYER_DECORATION = 20,
    RENDER_LAYER_UNIT = 30,
    RENDER_LAYER_EFFECT = 40,
    RENDER_LAYER_UI = 100,
} RenderLayer;

typedef enum {
    DRAW_COMMAND_TILE_OVERLAY,
    DRAW_COMMAND_DECORATION,
    DRAW_COMMAND_UNIT,
} DrawCommandKind;

typedef struct drawcommand_s {
    DrawCommandKind kind;
    RenderLayer layer;
    fixed_t sort_z;
    float sort_y;
    int stable_index;
    union {
        struct {
            int x;
            int y;
            int layer;
        } tile_overlay;
        const mapdecoration_t *decoration;
        const mobj_t *unit;
    } ref;
} drawcommand_t;


typedef struct app_s app_t;

typedef struct {
    int id;
    uint8_t indices[256];
} spritepalettemap_t;

typedef struct {
    int value;
    int frames[MAX_TILE_ANIMATION_FRAMES];
    int frame_count;
    uint16_t frame_ms;
} TileAnimation;

typedef struct spritelayer_s {
    ivec2_t offset;
    uint16_t lump;
    uint8_t remap; /* Native FIN drawing mode, not a team/palette ID. */
    uint8_t intensity;
    uint8_t layer;
    uint8_t flags;
    char sprite_name[9];
} spritelayer_t;

typedef struct spritedirection_s {
    int ticks;
    spritelayer_t *layers;
} spritedirection_t;

typedef struct spriteframe_s {
    int rotations; /* 1 is nondirectional; slots start north, counterclockwise. */
    char frame_name[17];
    spritedirection_t *directions;
} spriteframe_t;

typedef struct spritedef_s {
    int numframes;
    spriteframe_t *spriteframes;
} spritedef_t;

typedef struct spritelump_s {
    uint8_t *indices;
} spritelump_t;

/* Sprite geometry, indexed by the layer cell number; no renderer resources. */
typedef struct spritecell_s {
    irect_t rect;
    irect_t bounds;
    ivec2_t ground_point;
    ivec2_t displacement;
} spritecell_t;

typedef struct spritesheet_s {
    spritecell_t *cells;
    spritelump_t *lumps;
    int numlumps;
    isize2_t frame_size;
    spritedef_t spritedef;
    bool indexed;
    uint32_t palette[256];
    uint32_t source_palette[256]; /* Source colors, independent of world colormaps. */
    spritepalettemap_t *palette_maps;
    int palette_map_count;
    int indexed_blend_selector;
    const uint8_t *indexed_blend_table;
    const uint8_t *shadowmap; /* Destination colormap for native projected shadows. */
} spritesheet_t;

typedef struct tilepalettecycle_s {
    uint8_t *tiles;
    uint8_t indices[256];
    int count;
    uint16_t frame_ms;
} tilepalettecycle_t;

typedef struct tileset_s {
    uint8_t *indices; /* Contiguous native tiles, one byte per pixel. */
    uint32_t palette[256];
    tilepalettecycle_t palette_cycle;
    int *tile_lookup;
    int tile_lookup_count;
    TileAnimation *animations;
    int animation_count;
    int count;
    int atlas_cols;
    int tile_w;
    int tile_h;
    int draw_y_offset;
} tileset_t;

typedef struct cachedsprite_s {
    char name[32];
    spritesheet_t sprite;
    const spritesheet_t *alias; /* Borrowed image for another native resource name. */
} cachedsprite_t;

typedef struct spritecache_s {
    cachedsprite_t entries[MAX_DECORATION_SPRITES];
    int count;
    const spritesheet_t **sprites; /* Borrowed sheets, indexed by state sprite ID. */
    int numsprites;
    struct spritecache_s *ui; /* Owned images keyed by path, outside the state registry. */
} spritecache_t;

typedef struct bitmapfont_s {
    spritesheet_t sprite;
    int glyph_index[128];
    uint8_t glyph_width[128];
    isize2_t glyph_size;
    int line_h;
    int draw_divisor;
    bool native_origin; /* Draw cells at their authored displacement. */
} bitmapfont_t;

#define RTS_MAX_HUD_MESSAGES 8

typedef struct {
    char text[256];
    int ttl_ms;
} HudMessage;

typedef struct hudtext_s {
    HudMessage messages[RTS_MAX_HUD_MESSAGES];
    int count;
} hudtext_t;


#define SCREENWIDTH  640
#define SCREENHEIGHT 480

enum {
    V_FLIP_X = 1u << 0,
    V_FLIP_Y = 1u << 1,
    /* Write source index 0. Sprites skip it; opaque backgrounds do not, unless
     * the palette marks entry 0 transparent (alpha 0). Transparency is decided
     * on the source index, before any remap. */
    V_OPAQUE = 1u << 2
};

typedef struct {
    uint8_t *pixels; /* w*h palette indices, row-major, pitch == w */
    int w, h;
} vscreen_t;

extern vscreen_t screens[1];
extern uint32_t vpalette[256];

void V_AllocScreen(int w, int h);
void V_FreeScreen(void);
void V_BeginFrame(uint32_t clear_argb);
void I_SetPalette(const uint32_t argb[256]);
bool I_ReadScreen(uint8_t *dst);
uint8_t V_NearestIndex(uint32_t argb);
/* Source index -> screen index. Identity when the palette matches the screen.
 * The result is only valid until the next palette-resolving call. */
const uint8_t *V_RemapPalette(const uint32_t palette[256]);
/* Multiply source RGB by an 0xAARRGGBB color, then nearest-match the screen. */
void V_ModulateRemap(uint8_t out[256], const uint32_t palette[256], uint32_t color);
void V_ReadPixels(uint32_t *dst, int dst_pitch_bytes);

void V_SetClip(irect_t clip);
irect_t V_GetClip(void);

void V_FillRect(irect_t r, uint8_t color);
void V_DrawLine(ivec2_t a, ivec2_t b, uint8_t color);
void V_DrawPoint(ivec2_t p, uint8_t color);
void V_DrawRectOutline(irect_t r, uint8_t color);
void V_DrawBlock(ivec2_t at, const uint8_t *src, isize2_t size, int src_pitch,
                 const uint8_t *remap, uint32_t flags);
void V_DrawBlockScaled(irect_t dst, const uint8_t *src, isize2_t size, int src_pitch,
                       const uint8_t *remap, uint32_t flags);
void V_DrawBlockTranslucent(ivec2_t at, const uint8_t *src, isize2_t size, int src_pitch,
                            const uint8_t *table, uint32_t flags);
void V_DrawSilhouetteColormap(ivec2_t at, const uint8_t *src, isize2_t size, int src_pitch,
                              const uint8_t *colormap, uint32_t flags);

void V_DrawSpriteCell(ivec2_t at, const spritesheet_t *sheet, int cell,
                      const uint8_t *remap, uint32_t flags);
void V_DrawSpriteCellScaled(irect_t dst, const spritesheet_t *sheet, int cell,
                            const irect_t *src, const uint8_t *remap, uint32_t flags);

int V_TextWidth(const bitmapfont_t *font, const char *text);
/* Existing engine fallback glyphs, positioned in the supplied logical space. */
void V_DrawSmallText(irect_t box, const char *text, uint32_t argb, isize2_t space);
void V_DrawText(ivec2_t at, const bitmapfont_t *font, const char *text, const uint8_t *remap);
void V_DrawTextScaled(ivec2_t at, const bitmapfont_t *font, const char *text,
                      const uint8_t *remap, int scale);
void V_DrawTextWrapped(irect_t box, const bitmapfont_t *font, const char *text,
                       const uint8_t *remap, int scroll_px);

bool R_DrawIndexed(const uint8_t *indices, isize2_t size, const uint32_t palette[256],
                   const irect_t *src, const irect_t *dst, uint32_t flags);
bool R_DrawSprite(const spritesheet_t *sprite, int frame, int palette,
                  const irect_t *src, const irect_t *dst, uint32_t flags, int intensity);


/* Present renderer. Drawing does not use it. */
extern SDL_Renderer *r_renderer;


uint16_t read_u16_le(const uint8_t *p);
int32_t read_i32_le(const uint8_t *p);
uint32_t read_u32_le(const uint8_t *p);

bool W_ReadFile(const char *path, blob_t *out);
void W_FreeFile(blob_t *blob);
SDL_Surface *W_LoadImage(const char *path);
/* Decode a BMP or PCX into one indexed spritesheet cell. Owns the lump pixels. */
bool W_LoadIndexedSheet(const char *path, spritesheet_t *out);
bool W_LoadGIFTexture(const char *path, spritesheet_t *out);
/* Temporary strings survive seven further M_va calls on this thread.
 * Returns NULL on formatting failure or overflow; copy results kept longer. */
char *M_va(const char *format, ...) __attribute__((format(printf, 1, 2)));
const char *M_FileName(const char *path);
char *M_Upper(char *text);
void M_PathJoin(char *dst, size_t dst_size, const char *a, const char *b);
int clamp255(int value);
void V_IndexedToRGBA(uint32_t *dst, const uint8_t *src, size_t count, const uint32_t palette[256]);
void V_BlitIndexed(uint32_t *dst, int dst_w, int dst_h, int dst_x, int dst_y,
                   const uint8_t *src, int src_w, int src_h, const uint32_t palette[256]);
bool R_AllocSpriteCells(spritesheet_t *sprite, int count);
void R_FreeSpriteBuffer(void);
void R_DropIndexed(const uint8_t *base, size_t bytes);
bool R_RenderIndexedBlend(app_t *app, const spritesheet_t *sprite, int frame,
                          irect_t dst, uint32_t flags, int selector);
bool R_RenderSpriteShadow(app_t *app, const spritesheet_t *sprite, int frame,
                          irect_t ground_dst, uint32_t flags);
bool R_AddTileAnim(tileset_t *tileset, int value, const int *frames,
                   int frame_count, uint16_t frame_ms);
const uint8_t *R_PaletteMap(const spritesheet_t *sprite, int id);
void HU_DrawText(ivec2_t at, const bitmapfont_t *font, const char *text,
                 const uint8_t *remap, int scale);
void HU_DrawTextWrapped(irect_t box, const bitmapfont_t *font, const char *text,
                        const uint8_t *remap, int scale);
int HU_TextWidth(const bitmapfont_t *font, const char *text, int scale);
void HU_PushMessage(hudtext_t *hud, const char *text, int ttl_ms);
void HU_Ticker(hudtext_t *hud, float dt);

int L_Index(const level_t *map, int x, int y);
bool L_Contains(const level_t *map, int x, int y);
bool L_IsWalkable(const level_t *map, int x, int y);
int P_FindPath(const level_t *map, cell_t start, cell_t goal, cell_t *out_path, int max_path);
void P_FreeFlowFields(level_t *map);

void R_GridToScreen(const app_t *app, float gx, float gy, float *sx, float *sy);
cell_t R_ScreenToGrid(const app_t *app, int sx, int sy);
void R_MapToScreen(const app_t *app, const level_t *map, float gx, float gy,
                   float *sx, float *sy);
void R_MapPositionToScreen(const app_t *app, const level_t *map,
                           fixed3_t position, float *sx, float *sy);
cell_t R_ScreenToMapGrid(const app_t *app, const level_t *map, int sx, int sy);
void R_RefreshViewport(app_t *app);
void R_WindowToRenderPt(const app_t *app, int wx, int wy, int *rx, int *ry);
void R_WindowToRenderDelta(const app_t *app, int wx, int wy, float *rx, float *ry);
void R_DrawCell(app_t *app, int gx, int gy, uint32_t argb);
void R_DrawTile(app_t *app, const tileset_t *tileset, int tile, irect_t src_part, irect_t dst_part);
/* The level's tileset palette is the screen palette. R_DrawLevel installs it
 * every frame; loading installs it so the first clear already uses it. */
void R_SetLevelPalette(const tileset_t *tileset);
void R_DrawLevel(app_t *app, const level_t *map, const tileset_t *tileset);
void R_DrawGridOverlay(app_t *app, const level_t *map);
int R_PickUnit(const app_t *app, const level_t *map, mobj_t *const *units, int unit_count,
               const spritesheet_t *fallback_sprite, const spritecache_t *cache,
               const gameinfo_t *game_info, int x, int y, int owner_filter);
void R_DrawDecorations(app_t *app, const level_t *map, const spritecache_t *cache);
void R_DrawThings(app_t *app, mobj_t *const *units, int unit_count, const spritesheet_t *fallback_sprite,
                  const spritecache_t *cache, const gameinfo_t *game_info, uint32_t ticks);
void R_RenderPlayerView(app_t *app, const level_t *map, const tileset_t *tileset,
                        mobj_t *const *units, int unit_count, const spritesheet_t *fallback_sprite,
                        const spritecache_t *cache, const gameinfo_t *game_info, uint32_t ticks);
cachedsprite_t *R_CacheFind(spritecache_t *cache, const char *name);
bool R_BindSprites(spritecache_t *cache, const gameinfo_t *game_info);
const spritesheet_t *R_StateSprite(const spritecache_t *cache, const gameinfo_t *game_info,
                                   int sprite, const char *name);
const spritesheet_t *R_CacheLookup(const spritecache_t *cache, const char *name);
bool R_InitSpriteDef(spritesheet_t *sprite, int numframes, int rotations);
bool R_AllocSpriteDirections(spriteframe_t *frame, int rotations);
bool R_InstallSpriteLump(spritesheet_t *sprite, int frame, int rotation,
                         int lump, bool flip);

void P_MoveOrder(const level_t *map, mobj_t *const *units, int unit_count, cell_t goal);
bool P_HasMoveOrder(const mobj_t *unit);
void P_MoveOrderAt(const level_t *map, mobj_t *const *units, int unit_count,
                         fvec2_t goal_position);
bool P_MoveUnitTo(const level_t *map, mobj_t *unit, fvec2_t goal_position);
bool P_HarvestOrderAt(const level_t *map, mobj_t *const *units, int unit_count,
                             fvec2_t position);
bool P_HarvestUnitTo(const level_t *map, mobj_t *unit, fvec2_t position);
void P_InitMobj(const gameinfo_t *game_info, mobj_t *unit);
void P_ApplyActorTypeDefaults(mobj_t *unit, const mobjtype_t *type);
bool P_SetMobjState(mobj_t *unit, int state_id);
bool P_TickMobjState(mobj_t *unit);
bool P_Attack(mobj_t *attacker);
void P_DamageMobj(mobj_t *target, mobj_t *source, int damage);
angle_t P_PointToAngle(float dx, float dy);
void P_AngleToVec(angle_t angle, float *dx, float *dy);
void P_Ticker(void);
void G_Responder(app_t *app, const level_t *map, mobj_t *const *units, int unit_count,
                 const spritesheet_t *fallback_sprite, const spritecache_t *cache,
                 const gameinfo_t *game_info, const SDL_Event *e);
void G_CameraMove(app_t *app, float dt);
void R_ClampCamera(app_t *app, const level_t *map, int viewport_w, int viewport_h);

void R_FreeTileset(tileset_t *tileset);
void P_FreeLevel(level_t *map);
bool P_InitSight(void);
void P_UpdateSight(void);
void P_RevealSight(ivec2_t origin, int radius, uint32_t mask, bool airborne);
bool P_VisibleToPlayer(const mobj_t *mobj);
bool P_VisibleTo(const mobj_t *observer, const mobj_t *target);
int P_SightBrightness(const level_t *map, ivec2_t cell);
int R_FogSample(const int corners[4], ivec2_t pixel);
void R_DrawFog(app_t *app, const level_t *map);
void R_FreeSprite(spritesheet_t *sprite);
void HU_FreeFont(bitmapfont_t *font);
void R_FreeSpriteCache(spritecache_t *cache);


extern bool menuactive;
extern bool menuerror;
extern const char *menumap;

bool M_Init(app_t *app, const char *root);
void M_StartControlPanel(app_t *app);
bool M_Responder(app_t *app, const SDL_Event *event, bool inlevel);
bool D_MenuResponder(app_t *app, const SDL_Event *event, void *ui,
                      mobj_t *const *units, int unit_count);
void M_Drawer(const app_t *app);
void M_Ticker(void);
void M_Shutdown(void);


#define RTS_MODEL_MAX_SNAPSHOT_UNITS 128
#define RTS_MODEL_MAX_SNAPSHOT_DECORATIONS MAX_DECORATIONS
#define RTS_MODEL_MAX_PLAYERS 8
#define RTS_MODEL_MAX_RESOURCES 8
#define RTS_MODEL_MAX_PRODUCT_PREREQUISITES 4
#define RTS_MODEL_UI_SCRIPT_BYTES 4096

typedef struct RtsGameModel RtsGameModel;

typedef struct {
    /* Root data directory for the game. Defaults to g_game_default_root when NULL. */
    const char *data_root;
    /* Relative-to-data-root or absolute mission/map path. Defaults to g_game_default_map. */
    const char *map_path;
} RtsGameModelConfig;

typedef enum {
    RTS_GAME_COMMAND_NONE = 0,
    RTS_GAME_COMMAND_SELECT_ALL_PLAYER_UNITS,
    RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
    RTS_GAME_COMMAND_MOVE_SELECTED,
    RTS_GAME_COMMAND_HARVEST_SELECTED,
    RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON,
    RTS_GAME_COMMAND_ATTACK_UNIT,
    RTS_GAME_COMMAND_BUILD_PRODUCT,
    RTS_GAME_COMMAND_DEPLOY_SELECTED,
    RTS_GAME_COMMAND_PATH_SELECTED,
    RTS_GAME_COMMAND_STOP_SELECTED,
} RtsGameCommandKind;

typedef enum {
    RTS_GAME_EVENT_NONE = 0,
    RTS_GAME_EVENT_UNIT_ARRIVED,
    /* A player order was accepted and placed in a producer queue. */
    RTS_GAME_EVENT_BUILD_QUEUED,
    RTS_GAME_EVENT_BUILD_STARTED,
    /* Compatibility/general completion event; use the typed events below. */
    RTS_GAME_EVENT_BUILD_FINISHED,
    RTS_GAME_EVENT_UNIT_BUILT,
    RTS_GAME_EVENT_BUILDING_BUILT,
    /* Production could not proceed (for example, a blocked release point). */
    RTS_GAME_EVENT_BUILD_BLOCKED,
    RTS_GAME_EVENT_UNIT_DIED,
    RTS_GAME_EVENT_ATTACK_STARTED,
} RtsGameEventType;

typedef enum {
    RTS_RENDER_TRAIT_SELECTABLE = 1u << 0,
    RTS_RENDER_TRAIT_MOBILE = 1u << 1,
    RTS_RENDER_TRAIT_RENDERABLE = 1u << 2,
    RTS_RENDER_TRAIT_ATTACK = 1u << 3,
    RTS_RENDER_TRAIT_HARVESTER = 1u << 4,
} RtsRenderTrait;

typedef enum {
    RTS_PRODUCT_BUILDING = 1,
    RTS_PRODUCT_UNIT = 2,
    RTS_PRODUCT_UPGRADE = 3,
} RtsProductClass;

typedef struct {
    int row_id;
    int ui_id;
    const char *label;
    int cost;
    int icon_frame;
    RtsProductClass product_class;
    int product_type;
    int faction;
    int prerequisites[RTS_MODEL_MAX_PRODUCT_PREREQUISITES];
    int prerequisite_count;
    int makers[RTS_MODEL_MAX_PRODUCT_PREREQUISITES];
    int maker_count;
} StaticProductDefinition;

typedef struct {
    RtsGameCommandKind kind;
    union {
        struct {
            int unit_index;
            bool additive;
        } select_unit_index;
        struct {
            fvec2_t target;
        } move_selected;
        waypoints_t path_selected;
        struct {
            fvec2_t target;
        } harvest_selected;
        struct {
            int ui_id;
        } activate_ui_button;
        struct {
            uint32_t target_id;
            int target_index; /* Compatibility fallback; prefer target_id. */
        } attack_unit;
        struct {
            uint32_t producer_id;
            int producer_index; /* Compatibility fallback; prefer producer_id. */
            int ui_id;
        } build_product;
    } data;
} RtsGameCommand;

typedef struct {
    RtsGameEventType type;
    uint64_t tick;
    uint32_t subject_id;
    uint32_t target_id;
    uint16_t subject_type_id;
    uint16_t target_type_id;
    uint8_t subject_owner;
    uint8_t target_owner;
    int product_class;
    int product_type;
    fvec2_t position;
} RtsGameEvent;

typedef struct {
    fvec2_t position;
    fvec2_t move_goal;
    uint16_t type_id;
    uint8_t owner;
    uint32_t traits;
    int hp;
    int max_hp;
    int frame;
    int facing_code;
    int state_id;
    uint32_t id;
    uint32_t render_flags;
    int render_remap;
    int render_intensity;
    ivec2_t render_offset;
    bool selected;
    bool has_move_order;
    int harvest_target;
    bool hidden;
    char sprite_name[32];
    char shadow_name[32];
} RtsRenderUnit;



typedef struct {
    ivec2_t cell;
    isize2_t footprint;
    bool hidden;
    bool center_anchor;
    bool has_sprite_pivot;
    ivec2_t sprite_pivot;
    int frame_interval_ms;
    int frame_index;
    int frame2_index;
    int frame3_index;
    int facing_code;
    int render_remap;
    uint32_t render_flags;
    int render_selector;
    uint32_t render2_flags;
    int render2_selector;
    uint32_t render3_flags;
    int render3_selector;
    char sprite_name[32];
    char sprite2_name[32];
    char sprite3_name[32];
    char shadow_name[32];
} RtsRenderDecoration;

typedef struct {
    int map_width;
    int map_height;
    int unit_count;
    int decoration_count;
    int resource_vent_count;
    int player_resources[RTS_MODEL_MAX_PLAYERS][RTS_MODEL_MAX_RESOURCES];
    RtsRenderUnit units[RTS_MODEL_MAX_SNAPSHOT_UNITS];
    RtsRenderDecoration decorations[RTS_MODEL_MAX_SNAPSHOT_DECORATIONS];
    /*
     * Quake-style declarative UI emitted by the model/server.
     * Example:
     *   x 516 y 92 btn 206 enabled 1 pic 129
     *   x 524 y 126 text "Exo-Ctr 2000"
     */
    char ui_script[RTS_MODEL_UI_SCRIPT_BYTES];
} RtsRenderSnapshot;

typedef struct {
    /* Original game UI/button id. For Dark Colony this comes from INTRFACE/MAINE. */
    int ui_id;
    char label[40];
    int cost;
    int icon_frame;
    RtsProductClass product_class;
    /* Original game product row/type, not an internal actor id. */
    int product_type;
    int faction;
    int prerequisite_count;
    /* Original product row/types required before this product is enabled. */
    int prerequisites[RTS_MODEL_MAX_PRODUCT_PREREQUISITES];
    bool available;
} RtsProductDefinition;

RtsGameModel *rts_game_model_create(void);
void rts_game_model_destroy(RtsGameModel *model);

/* Loads or reloads a game/model instance. This does not create a window or renderer. */
bool rts_game_model_load(RtsGameModel *model, const RtsGameModelConfig *config);
/* Advances one model tic. With D_CheckNetGame active, pumps networking and
 * advances only when all commands are present, using FIXED_DT. While waiting,
 * returns true without advancing gametic; false reports a network/load error. */
bool rts_game_model_tick(RtsGameModel *model, float dt);
/* Applies local selection; world orders are queued when D_CheckNetGame is active.
 * Otherwise retains immediate commands for manually ticked headless clients. */
bool rts_game_model_command(RtsGameModel *model, const RtsGameCommand *command);
/* Pops the oldest simulation transition event. Returns false when empty. */
bool rts_game_model_poll_event(RtsGameModel *model, RtsGameEvent *out);
/* Produces presentation-neutral state for a renderer or test to inspect. */
bool rts_game_model_snapshot(const RtsGameModel *model, RtsRenderSnapshot *out);
/* Returns product definitions with availability computed from current model state. */
int rts_game_model_products(const RtsGameModel *model, RtsProductDefinition *out, int max_products);

const char *rts_game_model_last_error(const RtsGameModel *model);
/* Computer-player state (stats and event log) for inspection by tests and tools. */
struct AiContext;
struct AiContext *rts_game_model_ai(RtsGameModel *model);
int rts_game_model_player_resources(const RtsGameModel *model, int player, int resource_type);


#define RTS_UI_MAX_LAYERS 16
#define RTS_UI_MAX_RESOURCES 8

typedef struct uiimage_s {
    const char *asset_path;
    irect_t source;
    irect_t destination;
} uiimage_t;

typedef struct uiresource_s {
    ivec2_t text; /* amount anchor in logical UI coordinates */
    uint32_t color; /* 0xAARRGGBB, nearest palette index at draw time */
    /* The native UI may center a counter (false) or pin its right edge (true). */
    bool right_aligned;
} uiresource_t;

typedef struct uipanel_s {
    irect_t rect;
    uint32_t fill;
    uint32_t border;
} uipanel_t;

typedef struct uiproduct_s {
    int id;
    int category;
    const char *image;
} uiproduct_t;

typedef struct uicategory_s {
    const char *label;
    irect_t rect;
    int image;
    irect_t source;
} uicategory_t;

typedef enum {
    UI_UNAVAILABLE, UI_MOVE, UI_ATTACK, UI_STOP, UI_RADAR, UI_OPTIONS, UI_PRODUCT,
    UI_PAGE, UI_WAYPOINT, UI_PATH_CLEAR, UI_PATH_DELETE, UI_PATH_GO,
    UI_PATH_SAVE, UI_PATH_DESELECT, UI_PATH_MODE, UI_PATH_ADVANCED
} uiactionkind_t;
typedef struct uiaction_s {
    const char *label;
    uiactionkind_t action;
    irect_t rect;
    int image;
    irect_t source;
    int product;
} uiaction_t;

/* Games describe native assets and layout; the client owns loading and rendering. */
typedef struct uidefinition_s {
    int logical_width;
    int logical_height;
    irect_t world_viewport;
    irect_t minimap;
    irect_t command_grid;
    int command_columns;
    int command_rows;
    uiresource_t resources[RTS_UI_MAX_RESOURCES];
    int resource_count;
    uipanel_t status_panel;
    bool status_elapsed_time;
    uipanel_t sidebar_panel;
    int sidebar_cell_size;
    const uiimage_t *images;
    int image_count;
    const char *asset_root;
    const uiproduct_t *products;
    int product_count;
    const uicategory_t *categories;
    int category_count;
    isize2_t icon_size;
    const uiaction_t *actions;
    int action_count;
    int minimap_scale;
    const uiaction_t *path_actions;
    int path_action_count;
    irect_t path_list;
    int path_row_height;
    /* Native chrome the engine does not draw. Arguments are logical rects. */
    void (*draw_status)(const struct app_s *app, const struct level_s *map, irect_t rect);
    void (*draw_minimap_overlay)(const struct app_s *app, const struct level_s *map, irect_t rect);
    void (*draw_product_slot)(const struct app_s *app, int product, irect_t rect);
} uidefinition_t;


/*
 * Doom-style game interface.  Every game binary defines these externs in its
 * own games/{GameDir}/plugin.c (or game.c).  The engine calls them by name —
 * no vtable, no dlopen, no registry.
 *
 * Include this header from both the engine side (engine calls) and the game
 * side (game implements).
 */


/* ── game identity ─────────────────────────────────────────────────────── */
extern const char *const g_game_id;
extern const char *const g_game_name;
extern const char *const g_game_default_root;
extern const char *const g_game_default_map;
extern const char *const g_game_default_sprite;
extern const int g_cell_w;
extern const int g_cell_h;
extern const uint16_t g_debug_enemy_type;
extern const gameinfo_t *gameinfo;
extern const mobjtype_t *const actor_types;
extern const int num_actor_types;
extern const uidefinition_t *const gameui;   /* NULL if unused */

/* ── game functions ────────────────────────────────────────────────────── */

/* Convert native game metadata into the engine's canonical runtime format. */
void     G_InitGame(void);

/* Load the map at path into *out.  Returns true on success. */
bool     G_DoLoadLevel(const char *path, level_t *out);

/* Load tile/sprite assets into tileset and unit_sprite. */
bool     W_LoadAssets(const char *root, const level_t *map,
                      const char *sprite, tileset_t *tileset, spritesheet_t *unit_sprite);

/* Spawn initial map objects into the thinker list.
   Returns the number of mobjs spawned, or 0 on none/error. */
int      P_LoadThings(const char *path);

/* Load per-unit sprites into cache after units are known.
   Returns true on success (partial loads are allowed). */
bool     R_InitSprites(const char *root, const level_t *map,
                       mobj_t *const *mobjs, int count, spritecache_t *cache);

/* Load the game UI font into *font.  Returns false if the game has no font. */
bool     HU_LoadFont(const char *root, bitmapfont_t *font);

/* Advance mission state by dt seconds. */
void     G_MissionTicker(level_t *map, mobj_t *const *mobjs, int *count,
                         hudtext_t *hud, float dt);

/* Return mission state: 0=active, 1=won, 2=lost, 3=ally_lost. */
int      G_MissionState(const level_t *map);

/* ── custom interactive UI / sidebar hooks ─────────────────────────────── */

/* Initialize game-specific interactive UI/sidebar. Returns an opaque pointer, or NULL if none. */
void    *G_InitCustomUI(app_t *app, const char *data_root);

/* Handle input events for custom UI. Returns true if handled. */
bool     G_CustomUIResponder(void *ui, app_t *app, level_t *map,
                             mobj_t *const *units, int unit_count, const SDL_Event *event);

/* Advance custom UI state by one tick. */
void     G_CustomUITicker(void *ui);

/* Draw custom UI overlay/sidebar. */
void     G_CustomUIDrawer(void *ui, app_t *app, const level_t *map,
                          mobj_t *const *units, int unit_count,
                          const spritecache_t *sprites, const hudtext_t *hud);

/* Advance game-specific production queues in interactive mode. Returns true if a unit was spawned. */
bool     G_UpdateProduction(void *ui, level_t *map, mobj_t *const *units, int *unit_count,
                            float dt);

/* Shutdown and free custom UI. */
void     G_ShutdownCustomUI(void *ui);

/* Return the effective world viewport width in screen pixels. */
int      G_WorldViewportWidth(const app_t *app);

/* ── headless model hooks ───────────────────────────────────────────────── */

/* Query static products supported by the game. Returns count. */
int      G_ModelGetProducts(const RtsGameModel *model, int owner,
                            StaticProductDefinition *out, int max_products);

/* Query alien production products. Returns count. */
int      G_ModelAlienProducts(StaticProductDefinition *out, int max_products);

/* Lookup a static product definition by its UI ID. */
const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *model, int ui_id);

/* Lookup a static product definition by class and product type. */
const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *model,
                                                         int product_class,
                                                         int product_type);

/* Check if prerequisites are satisfied for an owner to build a product. */
bool     G_ModelProductAvailable(const RtsGameModel *model, int owner,
                                 const StaticProductDefinition *product);
bool     G_ModelProductAvailableForUnits(mobj_t *const *units, int unit_count,
                                          const StaticProductDefinition *product);

/* Find the producer mobj index for a product. Returns -1 if no eligible producer. */
int      G_ModelFindProducerIndex(const RtsGameModel *model, int owner,
                                  const StaticProductDefinition *product);

/* Lookup actor type ID corresponding to a product definition. */
uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *product);

/* Initial frame and state for spawned building products. */
int      G_ModelBuildingFrameForProduct(const StaticProductDefinition *product);
int      G_ModelBuildingStateForProduct(const gameinfo_t *game_info,
                                        const StaticProductDefinition *product);

/* Product training / build time in milliseconds. */
int      G_ModelProductTrainingTimeMs(const StaticProductDefinition *product);

/* Game-specific release animation and placement hooks (e.g. Dark Colony barracks release). */
bool     G_ModelStartProductionRelease(RtsGameModel *model, mobj_t *producer,
                                       const StaticProductDefinition *product,
                                       uint16_t actor_id);
bool     G_ModelSpecialReleaseSpawnPoint(const RtsGameModel *model, const mobj_t *producer,
                                         const StaticProductDefinition *product,
                                         const mobj_t *new_unit,
                                         float *out_gx, float *out_gy);

/* Check if a player/owner currently possesses an alive unit of actor_id. */
bool     G_ModelHasActorType(const RtsGameModel *model, int owner, uint16_t actor_id);

/* Emit declarative UI script from model and snapshot state. */
void     G_ModelBuildUIScript(const RtsGameModel *model,
                              const RtsRenderSnapshot *snapshot,
                              char *dst, size_t dst_size);

/* Universal AI hooks (see play/p_ai.h). Returns the game's interface, or NULL
 * when the game keeps only its own production AI. */
struct AiGameInterface;
const struct AiGameInterface *G_AiInterface(void);

/* Catalog-backed owned/can_purchase/purchase hooks (game/g_ai.c) for games
 * that buy through G_QueueProduct; goal product ids are catalog ui_ids. */
int      G_AiCatalogOwned(int owner, int ui_id);
int      G_AiCatalogCanPurchase(const level_t *map, int owner, int ui_id);
bool     G_AiCatalogPurchase(level_t *map, int owner, int ui_id);
bool     G_AiIsStructure(const mobj_t *unit); /* AiGameInterface.is_anchor */

/* Shared level production: UI and AI enqueue on the actual producer. */
mobj_t  *G_FindProducer(int owner, const StaticProductDefinition *product);
bool     G_QueueProduct(mobj_t *producer, const StaticProductDefinition *product);
int G_ModelRadarLevel(int owner);
bool     G_ModelProducerHasTech(const mobj_t *producer, const StaticProductDefinition *product);
bool     G_PlayerBuildProduct(mobj_t *producer, const StaticProductDefinition *product);
bool     G_ProductionTicker(float dt);
int      G_CountPlannedActors(int owner, uint16_t actor_id);
typedef struct {
    int ui_id;
    int count;
} productiongoal_t;
void     G_ProductionGoals(const productiongoal_t *goals, int count);

/* ── interactive production simulation (borrowed mobj pointers) ── */

/* Enqueue a unit production order on producer. Returns false if rejected. */
bool     G_ModelEnqueueProduction(mobj_t *producer, const StaticProductDefinition *product,
                                  uint16_t actor_id);

/* Advance production queues by dt seconds, spawning finished units. Returns true if a unit was spawned. */
bool     G_ModelUpdateProduction(level_t *map, mobj_t *const *units, int *unit_count,
                                 float dt);


/*
 * Universal computer-player layer.
 *
 * The engine owns scheduling, economy (harvester assignment), base defense,
 * attack waves and a goal-ladder production planner. A game attaches an
 * AiGameInterface that answers the game-specific questions (who is an AI and
 * of which strength, what to build in which order, how to buy it) and selects
 * which engine features run. With no interface attached the original generic
 * MF_RESOURCE_BASE behavior is retained for legacy callers.
 */

#define AI_MAX_TEAMS 8
#define AI_MAX_HARVEST_ASSIGNMENTS 32
#define AI_MAX_VENT_TRIES 64 /* Vents considered per harvester order. */
#define AI_DEFENSE_RADIUS 15.0f
#define AI_ATTACK_WAVE_INTERVAL_MS 30000
#define AI_ATTACK_WAVE_MIN_SIZE 3
#define AI_ATTACK_WAVE_MAX_SIZE 8
#define AI_EVENT_LOG_SIZE 256
/* DC.EXE 0x419a54 runs the AI chooser only when (tick & 3) == 0. */
#define AI_THINK_INTERVAL_TICKS 4

typedef enum {
    AI_FEATURE_ECONOMY    = 1u << 0, /* assign idle harvesters to free vents */
    AI_FEATURE_PRODUCTION = 1u << 1, /* buy along the game's goal ladder */
    AI_FEATURE_DEFENSE    = 1u << 2, /* rally idle fighters on base intruders */
    AI_FEATURE_ATTACK     = 1u << 3, /* periodic attack waves */
    AI_FEATURE_RESEARCH   = 1u << 4, /* start tech-ups a goal is waiting on */
    AI_FEATURE_ALL        = 0x1Fu,
} AiFeature;

typedef enum {
    AI_LEVEL_NONE = 0, /* Human, empty slot or scripted: the AI never acts. */
    AI_LEVEL_NORMAL = 1,
    AI_LEVEL_PLUS = 2,
} AiLevel;

typedef enum {
    AI_BUY_OK = 0,
    AI_BUY_BLOCKED,       /* missing prerequisite/producer or already owned */
    AI_BUY_NEED_CREDITS,  /* possible with more credits: the AI saves up */
    AI_BUY_NEED_TECH,     /* a producer exists but lacks the tech: see develop() */
} AiBuyStatus;

/* Keep at least `count` of `product` (alive plus queued) once `after_ms` of
 * game time has passed. Product ids are opaque to the engine. */
typedef struct {
    int product;
    int count;
    int after_ms;
} AiGoal;

#define AI_MAX_GOALS 96

typedef struct {
    AiGoal goals[AI_MAX_GOALS]; /* Priority order; earlier goals are served first. */
    int goal_count;
    int wave_interval_ms;       /* 0 selects AI_ATTACK_WAVE_INTERVAL_MS. */
    int wave_min_size;          /* 0 selects AI_ATTACK_WAVE_MIN_SIZE. */
    int wave_max_size;          /* 0 selects AI_ATTACK_WAVE_MAX_SIZE. */
} AiPlan;

typedef struct AiGameInterface {
    const char *name;
    uint32_t features; /* AiFeature mask the game enables. */
    /* AiLevel of an owner; AI_LEVEL_NONE owners are skipped entirely. */
    int  (*player_level)(const level_t *map, int owner);
    /* Fills the owner's goal ladder. Optional when PRODUCTION is disabled. */
    bool (*plan)(const level_t *map, int owner, int level, AiPlan *out);
    int  (*owned)(int owner, int product);  /* alive plus queued */
    int  (*can_purchase)(const level_t *map, int owner, int product);
    bool (*purchase)(level_t *map, int owner, int product);
    /* Optional. Called for a NEED_TECH goal when RESEARCH is enabled; starts
     * whatever tech-up unlocks `product`. Returning true makes the AI wait
     * for it (like saving credits) instead of spending on lower goals. */
    bool (*develop)(level_t *map, int owner, int product);
    /* Resource drop-off ("base") units that harvesters return to. Optional;
     * the default is MF_RESOURCE_BASE. */
    bool (*is_base)(const mobj_t *unit);
    /* Structures that anchor defense and are the targets of attack waves.
     * Optional; the default is is_base. Games whose drop-offs are a small
     * subset of their buildings (Dark Reign, KKnD) widen it to all structures. */
    bool (*is_anchor)(const mobj_t *unit);
} AiGameInterface;

typedef enum {
    AI_EVENT_NONE = 0,
    AI_EVENT_HARVEST_ASSIGNED, /* value = vent index */
    AI_EVENT_PURCHASE,         /* value = product id */
    AI_EVENT_DEFENSE_RALLY,    /* value = defenders sent */
    AI_EVENT_WAVE_LAUNCHED,    /* value = units sent */
    AI_EVENT_RESEARCH,         /* value = product id that needed the tech */
} AiEventType;

typedef struct {
    AiEventType type;
    int owner;
    int time_ms; /* AI clock: accumulated tick time */
    int value;
} AiEvent;

typedef struct {
    int harvest_orders;
    int purchases;
    int defense_rallies;
    int research_orders;
    int waves;
    int wave_units; /* total units sent in waves */
    int thinks;
} AiStats;

typedef struct {
    int vent_index;
    int slug_unit_index;
} AiHarvestAssignment;

typedef struct {
    fvec2_t base_position;
    bool has_base;
    int combat_unit_count;
    int harvester_count;
    AiHarvestAssignment harvest_assignments[AI_MAX_HARVEST_ASSIGNMENTS];
    int harvest_assignment_count;
    int attack_wave_timer_ms;
    int attack_wave_size;
    bool attack_wave_active;
    uint8_t allegiance;
    int level;          /* AiLevel; legacy mode uses AI_LEVEL_NORMAL */
    AiPlan plan;
    bool plan_loaded;
    AiStats stats;
} AiTeamState;

typedef struct AiContext {
    AiTeamState teams[AI_MAX_TEAMS];
    bool initialized;
    const AiGameInterface *game; /* NULL keeps the legacy generic behavior. */
    uint32_t features;           /* Active mask; defaults to the game's. */
    int clock_ms;
    int think_counter;
    AiEvent events[AI_EVENT_LOG_SIZE]; /* Ring; oldest entries are overwritten. */
    int event_head;
    int event_count;
    int events_dropped;
} AiContext;

/* Appends a goal to a plan (ignored when full). */
void P_AiPlanAdd(AiPlan *plan, int product, int count);
/* Default AiGameInterface.player_level: every non-human owner is a NORMAL
 * computer player. */
int  P_AiLevelNonHuman(const level_t *map, int owner);

void P_AiInit(AiContext *ctx);
/* Attaches a game's interface and adopts its feature mask (NULL detaches). */
void P_AiAttachGame(AiContext *ctx, const AiGameInterface *game);
/* Runtime toggles, e.g. for a difficulty menu or tests. */
void P_AiSetFeatures(AiContext *ctx, uint32_t features);
void P_AiTick(AiContext *ctx, level_t *map, mobj_t *const *units, int unit_count,
              const gameinfo_t *game_info, int dt_ms);

/* Pops the oldest retained AI event. Returns false when the log is empty. */
bool P_AiPollEvent(AiContext *ctx, AiEvent *out);
const AiStats *P_AiStats(const AiContext *ctx, int owner);


enum {
    HARVEST_PHASE_NONE = 0,
    HARVEST_PHASE_TO_MINE = 1,
    HARVEST_PHASE_MINING = 2,
    HARVEST_PHASE_TO_BASE = 3,
    HARVEST_PHASE_TURNING = 4,
    HARVEST_PHASE_UNLOAD_TURNING = 5,
    HARVEST_PHASE_UNLOADING = 6,
};

bool P_HarvesterDocked(const mobj_t *unit);
/* Scales a harvested credit amount by the owner's 8.8 income_scale. */
int P_ScaleIncome(const level_t *map, int owner, int amount);

void debug_effects_log(const char *fmt, ...);

float P_MobjRadius(const mobj_t *unit);

static inline isize2_t P_ResourceVentFootprint(const resourcevent_t *vent) {
    int w = vent && vent->footprint.w > 0 ? vent->footprint.w : 1;
    int h = vent && vent->footprint.h > 0 ? vent->footprint.h : 1;
    return (isize2_t){ w, h };
}

static inline bool P_ResourceVentContainsCell(const resourcevent_t *vent, ivec2_t cell) {
    if (!vent) return false;
    isize2_t fp = P_ResourceVentFootprint(vent);
    return cell.x >= vent->cell.x && cell.x < vent->cell.x + fp.w &&
           cell.y >= vent->cell.y && cell.y < vent->cell.y + fp.h;
}

static inline float P_ResourceVentRadius(const resourcevent_t *vent) {
    isize2_t fp = P_ResourceVentFootprint(vent);
    float hx = (float)fp.w * 0.5f, hy = (float)fp.h * 0.5f;
    float r = sqrtf(hx * hx + hy * hy);
    return r > 1.45f ? r : 1.45f;
}
bool P_CheckPosition(const level_t *map, const mobj_t *unit, float gx, float gy);
bool P_TryMove(mobj_t *unit, fixed3_t position);
void P_ClampToLevel(const level_t *map, mobj_t *unit);
bool P_FlowFieldTarget(const level_t *map, const flowfield_t *field,
                       fvec2_t position, fvec2_t goal, float radius,
                       fvec2_t *target, bool *final);
void P_FreeFlowFields(level_t *map);
void P_MoveUnitsAt(const level_t *map, mobj_t *const *units, int count, fvec2_t goal);
bool P_HarvestUnitsAt(const level_t *map, mobj_t *const *units, int count, fvec2_t goal);

void P_MoveOrderAt(const level_t *map, mobj_t *const *units, int unit_count,
                         fvec2_t goal_position);
bool P_MoveUnitTo(const level_t *map, mobj_t *unit, fvec2_t goal_position);
bool P_HarvestOrderAt(const level_t *map, mobj_t *const *units, int unit_count,
                             fvec2_t position);
bool P_HarvestUnitTo(const level_t *map, mobj_t *unit, fvec2_t position);


/* A screen is a table of items. The game fills the table and supplies the
 * routines; the engine hit-tests, keeps focus, edits text and draws. */
typedef enum {
    MI_STATIC, /* a picture, text or ownerdraw that takes no input */
    MI_BUTTON, MI_CHECK, MI_TEXTFIELD, MI_LIST, MI_SCROLLBAR
} menuitemkind_t;

/* How an item looks in one state. */
typedef enum { MS_NORMAL, MS_FOCUS, MS_PUSHED, MS_STATES } menustate_t;
typedef struct {
    int cell;     /* of the item's sheet; negative draws no picture */
    irect_t part; /* a rectangle of that cell; empty means all of it */
    int palette;  /* palette map of the sheet and of the font, or -1 */
} menulook_t;

typedef struct menu_s menu_t;
typedef struct menuitem_s menuitem_t;

/* MA_ACTIVATE: clicked, its hotkey pressed, or Enter while focused; a check
 * box has already changed. MA_SECONDARY: clicked with the right button.
 * MA_CHANGE: the text of a field or the selected row of a list changed.
 * MA_WHEEL: the wheel turned by menu->wheel over a HUD item. */
typedef enum { MA_ACTIVATE, MA_SECONDARY, MA_CHANGE, MA_WHEEL } menuaction_t;

/* An item can step through frames first..last. A loop wraps; a one-off stops
 * on its last frame. The game draws the frame, or uses it as it likes. */
typedef enum { MANIM_STOPPED, MANIM_LOOP, MANIM_ONCE } menuanimmode_t;
typedef struct {
    menuanimmode_t mode;
    int first, last, frame;
    int delay; /* ticks left on this frame */
} menuanim_t;

typedef void (*menuroutine_t)(menu_t *menu, menuitem_t *item, menuaction_t action);
typedef void (*menudraw_t)(const menu_t *menu, const menuitem_t *item);

struct menuitem_s {
    menuitemkind_t kind;
    irect_t rect;
    bool visible, enabled;
    SDL_Keycode hotkey; /* activates the item while it is enabled */
    /* The picture is drawn at the rect origin plus the cell's displacement. */
    const spritesheet_t *sheet;
    menulook_t look[MS_STATES];
    bool opaque; /* the sheet has no colour key: write its index 0 */
    bool stretch; /* scale the source picture to the item's rectangle */
    int light;   /* 1..15 darkens the picture, in sixteenths; 0 is full light */
    const bitmapfont_t *font;
    uint32_t ink; /* 0xAARRGGBB text colour; 0 draws through the palette map */
    char text[128];
    /* Long text wrapped in the rect, in the font's own colours. A list shows
     * it while it has no rows. */
    const char *prose;
    bool centered;
    ivec2_t inset; /* text origin inside the rect */
    int maxchars;
    int value; /* check: set; list: selected row, or -1 */
    int group; /* check: nonzero makes it one of a set where exactly one is set */
    /* A list asks for its rows; first_row is the scroll position, in rows for
     * a list and in lines for prose. */
    int rows, first_row, row_height;
    const char *(*row)(const menuitem_t *item, int row);
    /* A scroll bar shows and drags the list at index link. A button with a
     * step scrolls the list or prose at index link by that much. */
    int link, step;
    uint32_t fill;  /* 0xAARRGGBB behind the item; 0 draws none */
    uint32_t color; /* list selection, scroll bar or plain button focus outline */
    menuanim_t anim;
    menuroutine_t routine;
    menudraw_t ownerdraw; /* native content drawn after the standard picture */
    const void *userdata;
};

struct menu_s {
    menuitem_t *items;
    int numitems;
    /* A modal screen takes every event and has keyboard focus. Any other is a
     * HUD: it takes hotkeys and the mouse events that land on a visible item,
     * and its focus is the live item under the pointer. */
    bool modal;
    int itemOn;       /* focused item, or -1 */
    menuitem_t *held; /* pressed by the mouse */
    ivec2_t cursor;
    int wheel;        /* for MA_WHEEL */
    const spritesheet_t *background;
    const uint32_t *palette; /* screen palette while the menu draws; NULL keeps the level's */
    void (*escape)(menu_t *menu);
    /* Ticks an animated item spends on its current frame; NULL means one. */
    int (*frametics)(const menuitem_t *item);
    void *owner;
};

/* Returns whether the screen took the event. */
bool M_MenuResponder(menu_t *menu, const app_t *app, const SDL_Event *event);
/* Advance every visible, running animation by one tick. */
void M_MenuTicker(menu_t *menu);
/* Restart an item's animation from its first frame. */
void M_MenuAnimate(menuitem_t *item, menuanimmode_t mode);
/* Set a list's row count and keep its scroll position inside it. */
void M_MenuSetRows(menuitem_t *list, int rows);
void M_MenuDrawer(const menu_t *menu);
/* Shared fallback lifecycle; games without a native front end supply this table. */
extern menu_t gamemenu;
void M_MenuBeginLevel(menu_t *menu, menuitem_t *item, menuaction_t action);
void M_MenuQuitGame(menu_t *menu, menuitem_t *item, menuaction_t action);


enum { MAXSAVEDPATHS = 30 };

typedef struct {
    const uidefinition_t *definition;
    spritesheet_t images[RTS_UI_MAX_LAYERS];
    bool ready;
    bool first_draw;
    int pressed_button;
    uint64_t clock;
    uint32_t production_selection;
    int production_page;
    int production_category;
    spritesheet_t *product_icons;
    const spritecache_t *sprites; /* Borrowed from the world renderer for picking. */
    bool radar_visible;
    bool options_visible;
    uiactionkind_t order;
    int order_product;
    int page;
    waypoints_t path;
    waypoints_t saved_paths[MAXSAVEDPATHS];
    int saved_path_count;
    int saved_path_selection;
    bool path_advanced;
} sb_state_t;

/* Doom-style status-bar lifecycle.  The explicit state argument replaces the
   original globals while keeping call sites directly comparable to sb_bar.c. */
bool SB_Init(sb_state_t *st, const char *data_root,
             const uidefinition_t *definition);
void SB_Start(sb_state_t *st);
bool SB_Responder(sb_state_t *st, const app_t *app, const SDL_Event *event);
void SB_Ticker(sb_state_t *st);
void SB_Drawer(sb_state_t *st, app_t *app, const level_t *map,
               mobj_t *const *units, int unit_count, const spritecache_t *sprites,
               bool fullscreen, bool refresh);
void SB_Shutdown(sb_state_t *st);
bool SB_ProductionResponder(sb_state_t *st, app_t *app, const SDL_Event *event);
bool SB_SelectedOrder(ticorder_t order, fvec2_t goal, uint32_t target);
bool SB_ActivateAction(sb_state_t *st, const uiaction_t *action);
bool SB_PathResponder(sb_state_t *st, const app_t *app, const SDL_Event *event);
void SB_ProductionDrawer(sb_state_t *st, const app_t *app);
irect_t SB_MinimapRect(const level_t *map);
void SB_DrawText(ivec2_t point, const char *text, int width, uint32_t argb);
bool G_LoadMenuSprite(const char *root, const char *name, spritesheet_t *out);


typedef struct app_s app_t;

bool I_InitGraphics(app_t *app, int window_w, int window_h, bool hidden, bool software);
void I_ShutdownGraphics(void);
void I_FinishUpdate(void);
bool I_SaveScreenshot(const char *path);
void I_SetScaleMode(bool linear);


typedef struct renderer_s renderer_t;

typedef struct rendererbackend_s {
    const char *id;
    const char *name;
    bool (*create)(renderer_t *renderer, const char *title, int width, int height,
                   bool hidden, bool software);
    void (*destroy)(renderer_t *renderer);
    void (*begin_frame)(renderer_t *renderer, SDL_Color clear);
    void (*end_frame)(renderer_t *renderer);
    bool (*save_screenshot)(renderer_t *renderer, const char *path);
} rendererbackend_t;

struct renderer_s {
    const rendererbackend_t *backend;
    SDL_Window *window;
    SDL_Renderer *sdl;
    int width;
    int height;
};

const rendererbackend_t *sdl_renderer_backend(void);
const rendererbackend_t *renderer_backend_by_id(const char *id);

bool renderer_create(renderer_t *renderer, const rendererbackend_t *backend,
                         const char *title, int width, int height,
                         bool hidden, bool software);
void renderer_destroy(renderer_t *renderer);
void renderer_begin_frame(renderer_t *renderer, SDL_Color clear);
void renderer_end_frame(renderer_t *renderer);
bool renderer_save_screenshot(renderer_t *renderer, const char *path);


#endif
