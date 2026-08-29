#ifndef OPEN_RTS_ACTOR_H
#define OPEN_RTS_ACTOR_H

#include "engine_config.h"
#include "map.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct Unit Unit;
typedef struct StateContext StateContext;
typedef void (*StateAction)(StateContext *ctx, Unit *unit);

typedef enum {
    RTS_TRAIT_SELECTABLE = 1u << 0,
    RTS_TRAIT_MOBILE = 1u << 1,
    RTS_TRAIT_RENDERABLE = 1u << 2,
    RTS_TRAIT_ATTACK = 1u << 3,
    RTS_TRAIT_HARVESTER = 1u << 4,
} Trait;

typedef struct {
    uint16_t id;
    const char *name;
    const char *sprite_name;
    const char *shadow_name;
    uint32_t traits;
    float speed;
    int max_hp;
    float attack_range;
    int attack_damage;
    int attack_cooldown_ms;
    int attack_anim_ms;
    int death_anim_ms;
    int harvest_state_id;
    const char *muzzle_flash_name;
    int muzzle_flash_ms;
    const char *hit_effect_name;
} ActorType;

typedef struct {
    int sprite;
    int frame;
    int tics;
    StateAction action;
    int nextstate;
    uint32_t flags;
    int misc1;
    int misc2;
    int facings;
    int direction_codes[RTS_MAX_STATE_FACINGS];
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
    int overlay_direction_codes[RTS_MAX_STATE_FACINGS];
    int overlay_facing_frames[RTS_MAX_STATE_FACINGS];
    uint32_t overlay_facing_flags[RTS_MAX_STATE_FACINGS];
    int overlay_offset_x[RTS_MAX_STATE_FACINGS];
    int overlay_offset_y[RTS_MAX_STATE_FACINGS];
    int overlay_remap[RTS_MAX_STATE_FACINGS];
    int overlay_intensity[RTS_MAX_STATE_FACINGS];
} State;

typedef struct {
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
} MobjInfo;

typedef enum {
    RTS_STATE_COORDS_GROUND_OFFSET = 0,
    RTS_STATE_COORDS_FIN_TOP_LEFT = 1,
} StateCoordMode;

typedef enum {
    SELECTION_STYLE_SPRITE = 0,
    SELECTION_STYLE_CIRCLE,
} SelectionStyle;

typedef struct {
    SelectionStyle style;
    int sprite;
    int healthy_frame;
    int wounded_frame;
    int critical_frame;
    int top_offset_y;
} SelectionMarkerInfo;

typedef struct {
    const char *const *sprnames;
    int sprite_count;
    const State *states;
    int state_count;
    const MobjInfo *mobjinfo;
    int mobj_type_count;
    int null_state;
    DirectionMode direction_mode;
    StateCoordMode state_coord_mode;
    SelectionMarkerInfo selection_marker;
} GameInfo;

typedef struct Unit {
    float gx;
    float gy;
    float speed;
    int facing_code;
    uint16_t type_id;
    int state_id;
    int tics;
    int sprite_id;
    int frame;
    uint32_t render_flags;
    int render_remap;
    int render_intensity;
    int render_offset_x;
    int render_offset_y;
    float render_sort_y;
    uint8_t owner;
    uint32_t traits;
    int hp;
    int max_hp;
    float attack_range;
    int attack_damage;
    int attack_cooldown_ms;
    int attack_anim_ms;
    int attack_cooldown_left_ms;
    int attack_anim_left_ms;
    int death_anim_ms;
    int death_anim_left_ms;
    int muzzle_flash_ms;
    int attack_target;
    int harvest_target;
    int harvest_timer_ms;
    int harvest_state_id;
    bool selected;
    bool death_started;
    bool remove;
    bool move_order_arrived;
    uint16_t production_actor_id;
    uint8_t production_product_class;
    int production_product_type;
    int production_queue_count;
    int production_time_ms;
    int production_time_left_ms;
    bool production_release_active;
    int production_release_time_left_ms;
    float radius;
    float move_goal_gx;
    float move_goal_gy;
    uint32_t move_order_id;
    char sprite_name[32];
    char shadow_name[32];
    char muzzle_flash_name[32];
    char hit_effect_name[32];
    Cell path[MAX_PATH_CELLS];
    int path_len;
    int path_index;
} Unit;

typedef struct {
    bool active;
    bool use_state;
    float gx;
    float gy;
    int facing_code;
    int state_id;
    int tics;
    int sprite_id;
    int frame;
    uint32_t render_flags;
    int render_remap;
    int render_intensity;
    int screen_offset_x;
    int screen_offset_y;
    int age_ms;
    int duration_ms;
    int frame_ms;
    int decoration_frame_index;
    bool add_decoration_on_finish;
    char sprite_name[32];
    char sequence_name[16];
} VisualEffect;

struct StateContext {
    GameMap *map;
    Unit *units;
    int *unit_count;
    VisualEffect *effects;
    int max_effects;
    const GameInfo *game_info;
};

#endif
