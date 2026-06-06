#ifndef OPEN_RTS_GAME_MODEL_H
#define OPEN_RTS_GAME_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#include "engine_config.h"

#define RTS_MODEL_MAX_SNAPSHOT_UNITS 128
#define RTS_MODEL_MAX_SNAPSHOT_EFFECTS 256
#define RTS_MODEL_MAX_SNAPSHOT_DECORATIONS MAX_DECORATIONS
#define RTS_MODEL_MAX_PLAYERS 8
#define RTS_MODEL_MAX_PRODUCT_PREREQUISITES 4
#define RTS_MODEL_UI_SCRIPT_BYTES 4096

typedef struct RtsGameModel RtsGameModel;

typedef struct {
    /* Plugin id, for example "dark-colony". Defaults to Dark Colony when NULL. */
    const char *game_id;
    /* Root data directory for the selected game. Defaults to the plugin root. */
    const char *data_root;
    /* Relative-to-data-root or absolute mission/map path. Defaults to the plugin map. */
    const char *map_path;
} RtsGameModelConfig;

typedef enum {
    RTS_GAME_COMMAND_NONE = 0,
    RTS_GAME_COMMAND_SELECT_ALL_PLAYER_UNITS,
    RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
    RTS_GAME_COMMAND_MOVE_SELECTED,
    RTS_GAME_COMMAND_HARVEST_SELECTED,
    RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON,
} RtsGameCommandKind;

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
    RtsGameCommandKind kind;
    union {
        struct {
            int unit_index;
            bool additive;
        } select_unit_index;
        struct {
            float gx;
            float gy;
        } move_selected;
        struct {
            float gx;
            float gy;
        } harvest_selected;
        struct {
            int ui_id;
        } activate_ui_button;
    } data;
} RtsGameCommand;

typedef struct {
    float gx;
    float gy;
    float move_goal_gx;
    float move_goal_gy;
    uint16_t type_id;
    uint8_t owner;
    uint32_t traits;
    int hp;
    int max_hp;
    int frame;
    int state_id;
    uint32_t render_flags;
    int render_remap;
    int render_intensity;
    bool selected;
    bool has_move_order;
    int harvest_target;
    char sprite_name[32];
    char shadow_name[32];
} RtsRenderUnit;

typedef struct {
    bool active;
    float gx;
    float gy;
    int frame;
    uint32_t render_flags;
    int render_remap;
    int render_intensity;
    char sprite_name[32];
    char sequence_name[16];
} RtsRenderEffect;

typedef struct {
    int gx;
    int gy;
    int footprint_w;
    int footprint_h;
    bool center_anchor;
    int frame_index;
    int frame2_index;
    int facing_code;
    uint32_t render_flags;
    uint32_t render2_flags;
    char sprite_name[32];
    char sprite2_name[32];
    char shadow_name[32];
    char sequence_name[16];
} RtsRenderDecoration;

typedef struct {
    int map_width;
    int map_height;
    int unit_count;
    int effect_count;
    int decoration_count;
    int resource_vent_count;
    int player_resources[RTS_MODEL_MAX_PLAYERS];
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
/* Produces presentation-neutral state for a renderer or test to inspect. */
bool rts_game_model_snapshot(const RtsGameModel *model, RtsRenderSnapshot *out);
/* Returns product definitions with availability computed from current model state. */
int rts_game_model_products(const RtsGameModel *model, RtsProductDefinition *out, int max_products);

const char *rts_game_model_last_error(const RtsGameModel *model);

#endif
