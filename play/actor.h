#ifndef __ACTOR__
#define __ACTOR__

#include "engine_config.h"
#include "facing.h"
#include "map.h"
#include "p_waypoint.h"
#include "mobj_data.h"

#include <stdbool.h>
#include <stdint.h>

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
    struct {
        int state_id;
        int unload_state_id;
        angle_t dock_angle;
        harvestresource_t resources[RTS_MAX_RESOURCES];
    } harvest;
    uint16_t native_type_id;
    actionf_p1 damage_action;
} mobjtype_t;

typedef struct state_s {
    int sprite;
    int frame;
    int tics;
    actionf_p1 action;
    int nextstate;
    int group; /* Gameplay animation group, independent of sprite presentation. */
} state_t;

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
    int game_speed; /* Default simulation speed multiplier, 1..9; 0 means 1. */
};

/* State-machine and presentation fields of an ordinary mobj. */
typedef struct mobjcore_s {
    fixed3_t position;
    fixed3_t momentum;
    angle_t angle;
    int state_id;
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
bool P_TickMobjState(mobj_t *unit);
production_t *P_EnsureMobjProduction(mobj_t *unit);
void P_FreeMobjProduction(mobj_t *unit);

/* State-entry actions, matching Hexen's state_t action model. */
void A_Look(mobj_t *unit);
void A_Chase(mobj_t *unit);
void A_Attack(mobj_t *unit);

/* Move toward movement.goal at unit->speed.  Physical displacement belongs
 * to the movement system, not to a state-entry action. */

#endif
