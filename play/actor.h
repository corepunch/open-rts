#ifndef __ACTOR__
#define __ACTOR__

#include "engine_config.h"
#include "facing.h"
#include "map.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct mobj_s mobj_t;
typedef struct statecontext_s statecontext_t;
typedef void (*actionf_p1)(statecontext_t *ctx, mobj_t *mo);

typedef enum {
    MF_SELECTABLE = 1u << 0,
    MF_MOBILE = 1u << 1,
    MF_RENDERABLE = 1u << 2,
    MF_ATTACK = 1u << 3,
    MF_HARVESTER = 1u << 4,
    MF_RESOURCE_BASE = 1u << 5,
} mobjflag_t;

typedef struct actortype_s {
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
        int anim_ms;
    } attack;
    struct {
        int anim_ms;
    } death;
    struct {
        int state_id;
        int capacity;
    } harvest;
    int muzzle_flash_sprite;
    int muzzle_flash_ms;
    const char *muzzle_flash_name;
    int hit_effect_sprite;
    const char *hit_effect_name;
    actionf_p1 death_effect_action;
} actortype_t;

typedef struct state_s {
    int sprite;
    int frame;
    int tics;
    actionf_p1 action;
    int nextstate;
    uint32_t flags;
    int misc1;
    int misc2;
    int facings;
    angle_t rotation_angles[RTS_MAX_STATE_FACINGS];
    int facing_frames[RTS_MAX_STATE_FACINGS];
    uint32_t facing_flags[RTS_MAX_STATE_FACINGS];
    int offset_x[RTS_MAX_STATE_FACINGS];
    int offset_y[RTS_MAX_STATE_FACINGS];
    int remap[RTS_MAX_STATE_FACINGS];
    int intensity[RTS_MAX_STATE_FACINGS];
    int overlay_sprite;
    int overlay_frame;
    uint32_t overlay_flags;
    int overlay_facings;
    angle_t overlay_rotation_angles[RTS_MAX_STATE_FACINGS];
    int overlay_facing_frames[RTS_MAX_STATE_FACINGS];
    uint32_t overlay_facing_flags[RTS_MAX_STATE_FACINGS];
    int overlay_offset_x[RTS_MAX_STATE_FACINGS];
    int overlay_offset_y[RTS_MAX_STATE_FACINGS];
    int overlay_remap[RTS_MAX_STATE_FACINGS];
    int overlay_intensity[RTS_MAX_STATE_FACINGS];
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
    int muzzleflash;
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
    int sprite;
    int healthy_frame;
    int wounded_frame;
    int critical_frame;
    int top_offset_y;
} selectionmarker_t;

typedef struct gameinfo_s {
    const char *const *sprnames;
    int sprite_count;
    const state_t *states;
    int state_count;
    const mobjinfo_t *mobjinfo;
    int mobj_type_count;
    int null_state;
    StateCoordMode state_coord_mode;
    selectionmarker_t selection_marker;
} gameinfo_t;

/*
 * State-machine and presentation data shared by gameplay mobjs and transient
 * effects.  Keep this as the single definition of the fields advanced by a
 * state_t; effect_t adds only lifetime policy around the same lightweight
 * object core.
 */
typedef struct mobjcore_s {
    fixedvec3_t position;
    fixedvec3_t momentum;
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

struct mobj_s {
    mobjcore_t core;
    float speed;
    uint32_t id;
    uint16_t type_id;
    uint16_t native_type_id;
    float render_sort_y;
    uint8_t owner;
    uint32_t traits;
    int hp;
    int max_hp;
    struct {
        float range;
        int damage;
        int cooldown_ms;
        int anim_ms;
        int cooldown_left_ms;
        int anim_left_ms;
        int target;
    } attack;
    struct {
        int anim_ms;
        int anim_left_ms;
    } death;
    struct {
        int target;
        int timer_ms;
        int state_id;
        int cargo;
        int capacity;
        int phase;
        fvec2_t return_position;
    } harvest;
    int muzzle_flash_ms;
    bool selected;
    bool death_started;
    bool remove;
    struct {
        uint16_t actor_id;
        uint8_t product_class;
        int product_type;
        int queue_count;
        int time_ms;
        int time_left_ms;
        bool blocked;
        bool release_active;
        int release_time_left_ms;
    } production;
    float radius;
    struct {
        fvec2_t goal;
        uint32_t order_id;
        bool order_arrived;
        cell_t path[MAX_PATH_CELLS];
        int path_len;
        int path_index;
        int turn_timer_ms;
    } movement;
    int muzzle_flash_sprite;
    int hit_effect_sprite;
    char muzzle_flash_name[32];
    char hit_effect_name[32];
    char shadow_name[32];
    actionf_p1 death_effect_action;
};

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
    char sequence_name[16];
} effect_t;

struct statecontext_s {
    level_t *map;
    mobj_t *mobjs;
    int *mobj_count;
    effect_t *effects;
    int max_effects;
    const gameinfo_t *game_info;
};

#endif
