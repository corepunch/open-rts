#ifndef __ACTOR__
#define __ACTOR__

#include "engine_config.h"
#include "facing.h"
#include "map.h"
#include "mobj_data.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct mobj_s mobj_t;
typedef struct statecontext_s statecontext_t;
typedef struct app_s app_t;
typedef struct spritecache_s spritecache_t;
typedef struct gameinfo_s gameinfo_t;
typedef struct production_s production_t;
typedef void (*actionf_p1)(mobj_t *mo);

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
} mobjflag_t;

enum {
    ALLEGIANCE_PLAYER = 0,
    ALLEGIANCE_ENEMY  = 1,
    ALLEGIANCE_ALLIED = 2,
    ALLEGIANCE_NEUTRAL = 3,
};

typedef struct mobjtype_s {
    uint16_t id;
    const char *name;
    const char *sprite_name;
    const char *shadow_name;
    uint32_t traits;
    float speed;
    int max_hp;
    struct {
        float range;
        int damage;
        int cooldown_ms;
    } attack;
    struct {
        int state_id;
        int capacity;
    } harvest;
    int muzzle_flash_ms;
    const char *muzzle_flash_name;
    int hit_effect_sprite;
    const char *hit_effect_name;
} mobjtype_t;

typedef struct state_s {
    int sprite;
    int frame;
    int tics;
    actionf_p1 action;
    int nextstate;
    int group; /* Gameplay animation group, independent of sprite presentation. */
} state_t;

typedef struct mobjinfo_s {
    int doomednum;
    int spawnstate;
    int spawnhealth;
    int seestate;
    int seesound;
    int reactiontime;
    int attacksound;
    int painstate;
    int painchance;
    int painsound;
    int meleestate;
    int missilestate;
    int deathstate;
    int xdeathstate;
    int deathsound;
    int speed;
    int radius;
    int height;
    int mass;
    int damage;
    int activesound;
    int flags;
    int raisestate;
    fixed_t spawnz;
} mobjinfo_t;

typedef enum {
    RTS_STATE_COORDS_GROUND_OFFSET = 0,
    RTS_STATE_COORDS_FIN_TOP_LEFT = 1,
} StateCoordMode;

typedef enum {
    SELECTION_STYLE_SPRITE = 0,
    SELECTION_STYLE_CIRCLE,
    SELECTION_STYLE_BRACKETS,
} SelectionStyle;

typedef struct selectionmarker_s {
    SelectionStyle style;
    const char *image;
    int healthy_frame;
    int wounded_frame;
    int critical_frame;
    int top_offset_y;
} selectionmarker_t;

typedef struct selectiondrawcontext_s {
    app_t *app;
    const mobj_t *unit;
    const spritecache_t *cache;
    const gameinfo_t *game_info;
    irect_t body_dst;
    irect_t visible;
    fvec2_t anchor;
    uint32_t ticks;
} selectiondrawcontext_t;

typedef bool (*selectiondrawf_t)(const selectiondrawcontext_t *ctx);

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
    selectiondrawf_t draw_selection;
};

/*
 * State-machine and presentation data shared by gameplay mobjs and transient
 * effects.  Keep this as the single definition of the fields advanced by a
 * state_t; effect_t adds only lifetime policy around the same lightweight
 * object core.
 */
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
    int release_time_left_ms;
};

struct mobj_s {
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
    struct {
        int cooldown_left_ms;
        int target;
    } attack;
    struct {
        int target;
        int timer_ms;
        int cargo;
        int phase;
        fvec2_t return_position;
    } harvest;
    bool remove;
    production_t *production;
    float radius;
    struct {
        fvec2_t goal;
        const flowfield_t *flow_field;
        uint32_t order_id;
        bool order_arrived;
        int turn_timer_ms;
    } movement;
    MOBJ_GAME_FIELDS
};

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

static inline bool P_IsAlly(const mobj_t *a, const mobj_t *b) {
    if (!a || !b) return false;
    return P_AreAllegiancesAllied(a->allegiance, b->allegiance);
}

typedef struct effect_s {
    mobjcore_t core;
    bool active;
    bool use_state;
    bool fin_placement;
    int render_selector;
    int age_ms;
    int duration_ms;
    int frame_ms;
    int decoration_frame_index;
    bool add_decoration_on_finish;
    bool ground_light;
    int light_radius;
} effect_t;

struct statecontext_s {
    level_t *map;
    mobj_t *mobjs;
    int *mobj_count;
    effect_t *effects;
    int max_effects;
    const gameinfo_t *game_info;
};

/* Removed objects become allocatable after P_Ticker compacts the live prefix. */
mobj_t *P_AllocMobj(mobj_t *mobjs, int *count);
/* Active only during a world-state action; nested dispatch restores its caller. */
statecontext_t *P_GetStateContext(void);
bool P_SetMobjState(statecontext_t *ctx, mobj_t *unit, int state_id);
bool P_TickMobjState(statecontext_t *ctx, mobj_t *unit);
production_t *P_EnsureMobjProduction(mobj_t *unit);
void P_FreeMobjProduction(mobj_t *unit);

/* State-entry actions, matching Hexen's state_t action model. */
void A_Look(mobj_t *unit);
void A_Chase(mobj_t *unit);
void A_Attack(mobj_t *unit);

/* Move toward movement.goal at unit->speed.  Physical displacement belongs
 * to the movement system, not to a state-entry action. */
bool P_MoveMobjToward(const level_t *map, mobj_t *unit, float dt);

#endif
