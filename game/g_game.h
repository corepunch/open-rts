#ifndef __G_GAME__
#define __G_GAME__

#include <stdbool.h>
#include <stdint.h>

#include "engine_config.h"
#include "m_vec.h"

#define RTS_MODEL_MAX_SNAPSHOT_UNITS 128
#define RTS_MODEL_MAX_SNAPSHOT_EFFECTS 256
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
    bool active;
    fvec2_t position;
    int frame;
    uint32_t render_flags;
    int render_remap;
    int render_intensity;
    int render_selector;
    bool ground_light;
    int light_radius;
    char sprite_name[32];
} RtsRenderEffect;

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
    int effect_count;
    int decoration_count;
    int resource_vent_count;
    int player_resources[RTS_MODEL_MAX_PLAYERS][RTS_MODEL_MAX_RESOURCES];
    RtsRenderUnit units[RTS_MODEL_MAX_SNAPSHOT_UNITS];
    RtsRenderEffect effects[RTS_MODEL_MAX_SNAPSHOT_EFFECTS];
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
/* Advances deterministic model time by dt seconds. */
bool rts_game_model_tick(RtsGameModel *model, float dt);
/* Applies a player/game command. Renderers should translate input into these commands. */
bool rts_game_model_command(RtsGameModel *model, const RtsGameCommand *command);
/* Pops the oldest simulation transition event. Returns false when empty. */
bool rts_game_model_poll_event(RtsGameModel *model, RtsGameEvent *out);
/* Produces presentation-neutral state for a renderer or test to inspect. */
bool rts_game_model_snapshot(const RtsGameModel *model, RtsRenderSnapshot *out);
/* Returns product definitions with availability computed from current model state. */
int rts_game_model_products(const RtsGameModel *model, RtsProductDefinition *out, int max_products);

const char *rts_game_model_last_error(const RtsGameModel *model);
int rts_game_model_player_resources(const RtsGameModel *model, int player, int resource_type);

#endif
