#ifndef OPEN_RTS_GAME_MODEL_H
#define OPEN_RTS_GAME_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#define RTS_MODEL_MAX_SNAPSHOT_UNITS 128
#define RTS_MODEL_MAX_SNAPSHOT_EFFECTS 256
#define RTS_MODEL_MAX_PLAYERS 8

typedef struct RtsGameModel RtsGameModel;

typedef struct {
    const char *game_id;
    const char *data_root;
    const char *map_path;
} RtsGameModelConfig;

typedef enum {
    RTS_GAME_COMMAND_NONE = 0,
    RTS_GAME_COMMAND_SELECT_ALL_PLAYER_UNITS,
    RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
    RTS_GAME_COMMAND_MOVE_SELECTED,
} RtsGameCommandKind;

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
    uint32_t render_flags;
    bool selected;
    bool has_move_order;
    char sprite_name[32];
    char shadow_name[32];
} RtsRenderUnit;

typedef struct {
    bool active;
    float gx;
    float gy;
    int frame;
    uint32_t render_flags;
    char sprite_name[32];
    char sequence_name[16];
} RtsRenderEffect;

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
} RtsRenderSnapshot;

RtsGameModel *rts_game_model_create(void);
void rts_game_model_destroy(RtsGameModel *model);

bool rts_game_model_load(RtsGameModel *model, const RtsGameModelConfig *config);
bool rts_game_model_tick(RtsGameModel *model, float dt);
bool rts_game_model_command(RtsGameModel *model, const RtsGameCommand *command);
bool rts_game_model_snapshot(const RtsGameModel *model, RtsRenderSnapshot *out);

const char *rts_game_model_last_error(const RtsGameModel *model);

#endif
