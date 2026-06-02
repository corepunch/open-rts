#ifndef OPEN_RTS_ENGINE_H
#define OPEN_RTS_ENGINE_H

#include <SDL.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CELL_W 24
#define CELL_H 24
#define TILE_PIX_W 24
#define TILE_PIX_H 24
#define TILE_ATLAS_COLS 24
#define MAX_UNITS 128
#define MAX_VISUAL_EFFECTS 256
#define MAX_DECORATIONS 8192
#define MAX_DECORATION_SPRITES 512
#define MAX_TILE_OVERLAYS 3
#define MAX_TILE_ANIMATION_FRAMES 8
#define MAX_SPRITE_SEQUENCES 8
#define MAX_SEQUENCE_FACINGS 16
#define RTS_MAX_STATE_FACINGS 16
#define MAX_PATH_CELLS 4096
#define FIXED_DT (1.0f / 30.0f)

#define RTS_SPRITEFRAME_FLIP_X (1u << 0) /* Doom-style spriteframe_t.flip[rotation]. */
#define RTS_FRAME_FLIP_X RTS_SPRITEFRAME_FLIP_X
#define RTS_FRAME_ADDITIVE (1u << 1)
#define RTS_FRAME_TINT_YELLOW (1u << 2)

typedef struct {
    int x;
    int y;
} Cell;

enum {
    MAP_RENDER_USE_CELL_COLORS = 1 << 0,
    MAP_RENDER_SMOOTH_TRANSITIONS = 1 << 1,
    MAP_RENDER_SKIP_ZERO_TILES = 1 << 2,
    MAP_RENDER_INTERLEAVED_OVERLAYS = 1 << 3,
};

typedef struct {
    uint8_t *bytes;
    size_t size;
} Blob;

typedef struct {
    int value;
    int frames[MAX_TILE_ANIMATION_FRAMES];
    int frame_count;
    uint16_t frame_ms;
} TileAnimation;

typedef struct {
    SDL_Texture *texture;
    int *tile_lookup;
    int tile_lookup_count;
    TileAnimation *animations;
    int animation_count;
    int count;
    int atlas_cols;
    int tile_w;
    int tile_h;
    int draw_y_offset;
} Tileset;

typedef struct {
    char name[16];
    int facings;
    int length;
    int frame_stride;
    int tick_ms;
    int frame_starts[MAX_SEQUENCE_FACINGS];
    int direction_codes[MAX_SEQUENCE_FACINGS];
} SpriteSequence;

typedef struct {
    SDL_Texture *texture;
    SDL_Rect *frames;
    SDL_Rect *frame_bounds;
    int frame_count;
    int frame_w;
    int frame_h;
    int rotations;
    int primary_frames_per_rotation;
    SpriteSequence sequences[MAX_SPRITE_SEQUENCES];
    int sequence_count;
} SpriteSheet;

typedef enum {
    RTS_TRAIT_SELECTABLE = 1u << 0,
    RTS_TRAIT_MOBILE = 1u << 1,
    RTS_TRAIT_RENDERABLE = 1u << 2,
    RTS_TRAIT_ATTACK = 1u << 3,
    RTS_TRAIT_HARVESTER = 1u << 4,
} RtsTrait;

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
} RtsActorType;

typedef struct {
    int gx;
    int gy;
    int footprint_w;
    int footprint_h;
    bool solid;
    bool center_anchor;
    int frame_index;
    int facing_code;
    uint32_t render_flags;
    char sprite_name[32];
    char shadow_name[32];
    char sequence_name[16];
} MapDecoration;

typedef struct {
    int gx;
    int gy;
    int amount;
    int rate;
    bool active;
} MapResourceVent;

typedef struct {
    char name[32];
    SpriteSheet sprite;
} CachedSprite;

typedef struct {
    CachedSprite entries[MAX_DECORATION_SPRITES];
    int count;
} SpriteCache;

typedef struct {
    SpriteSheet sprite;
    int glyph_index[128];
    uint8_t glyph_width[128];
    int glyph_w;
    int glyph_h;
    int line_h;
    int draw_divisor;
} RtsBitmapFont;

#define RTS_MAX_HUD_MESSAGES 8

typedef struct {
    char text[256];
    int ttl_ms;
} RtsHudMessage;

typedef struct {
    RtsHudMessage messages[RTS_MAX_HUD_MESSAGES];
    int count;
} RtsHudText;

typedef struct App App;
typedef struct Unit Unit;
typedef struct RtsStateContext RtsStateContext;
typedef void (*RtsStateAction)(RtsStateContext *ctx, Unit *unit);

typedef struct {
    int sprite;
    int frame;
    int tics;
    RtsStateAction action;
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
} RtsState;

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
} RtsMobjInfo;

typedef enum {
    RTS_DIRECTION_COMPASS_16 = 0,
    RTS_DIRECTION_DARK_COLONY_8 = 1,
    RTS_DIRECTION_DARK_COLONY_16 = 2,
} RtsDirectionMode;

typedef struct {
    const char *const *sprnames;
    int sprite_count;
    const RtsState *states;
    int state_count;
    const RtsMobjInfo *mobjinfo;
    int mobj_type_count;
    int null_state;
    RtsDirectionMode direction_mode;
} RtsGameInfo;

typedef struct GameMap {
    int width;
    int height;
    uint16_t *tile_ids;
    uint16_t *tile_overlays[MAX_TILE_OVERLAYS];
    uint8_t *tile_flip_flags[MAX_TILE_OVERLAYS + 1];
    int tile_overlay_count;
    uint8_t *blocked;
    uint32_t *cell_colors;
    uint32_t render_features;
    MapDecoration *decorations;
    int decoration_count;
    MapResourceVent *resource_vents;
    int resource_vent_count;
    bool has_camera;
    float camera_gx;
    float camera_gy;
    int player_resources[8];
    char tileset_name[32];
    void (*render_transitions)(App *app, const struct GameMap *map, const Tileset *tileset,
                               int x, int y, int dx, int dy);
} GameMap;

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
    int screen_offset_x;
    int screen_offset_y;
    int age_ms;
    int duration_ms;
    int frame_ms;
    int decoration_frame_index;
    bool add_decoration_on_finish;
    char sprite_name[32];
    char sequence_name[16];
} RtsVisualEffect;

struct RtsStateContext {
    GameMap *map;
    Unit *units;
    int *unit_count;
    RtsVisualEffect *effects;
    int max_effects;
    const RtsGameInfo *game_info;
};

struct App {
    SDL_Window *window;
    SDL_Renderer *renderer;
    int win_w;
    int win_h;
    int cell_w;
    int cell_h;
    int render_scale;
    float cam_x;
    float cam_y;
    bool show_grid;
    bool show_blocked;
    bool running;
    bool dragging_select;
    bool panning;
    int mouse_down_x;
    int mouse_down_y;
    int mouse_x;
    int mouse_y;
    uint32_t ticks_ms;
    SDL_Rect selection_rect;
};

uint16_t read_u16_le(const uint8_t *p);
int32_t read_i32_le(const uint8_t *p);
uint32_t read_u32_le(const uint8_t *p);

bool load_blob(const char *path, Blob *out);
void free_blob(Blob *blob);
void path_join(char *dst, size_t dst_size, const char *a, const char *b);
int clamp255(int value);
void indexed_to_rgba(uint32_t *dst, const uint8_t *src, size_t count, const uint32_t palette[256]);
void blit_indexed_to_rgba(uint32_t *dst, int dst_w, int dst_h, int dst_x, int dst_y,
                          const uint8_t *src, int src_w, int src_h, const uint32_t palette[256]);
SDL_Texture *rgba_texture(SDL_Renderer *renderer, const uint32_t *pixels, int w, int h, bool blend);
bool tileset_add_animation(Tileset *tileset, int value, const int *frames,
                           int frame_count, uint16_t frame_ms);
void rts_font_draw_text(SDL_Renderer *renderer, const RtsBitmapFont *font, int x, int y,
                        const char *text, SDL_Color color, int scale);
void rts_font_draw_text_wrapped(SDL_Renderer *renderer, const RtsBitmapFont *font, int x, int y,
                                int max_w, const char *text, SDL_Color color, int scale);
int rts_font_text_width(const RtsBitmapFont *font, const char *text, int scale);
void rts_hud_text_push(RtsHudText *hud, const char *text, int ttl_ms);
void rts_hud_text_update(RtsHudText *hud, float dt);

int map_index(const GameMap *map, int x, int y);
bool map_contains(const GameMap *map, int x, int y);
bool map_walkable(const GameMap *map, int x, int y);
int astar_find(const GameMap *map, Cell start, Cell goal, Cell *out_path, int max_path);

void grid_to_screen(const App *app, float gx, float gy, float *sx, float *sy);
Cell screen_to_grid(const App *app, int sx, int sy);
void refresh_app_viewport(App *app);
void window_to_render_point(const App *app, int wx, int wy, int *rx, int *ry);
void window_to_render_delta(const App *app, int wx, int wy, float *rx, float *ry);
void render_grid_cell(App *app, int gx, int gy, SDL_Color color);
void render_tile_at(App *app, const Tileset *tileset, int tile, SDL_Rect src_part, SDL_Rect dst_part);
void render_map(App *app, const GameMap *map, const Tileset *tileset);
void render_decorations(App *app, const GameMap *map, const SpriteCache *cache);
void render_units(App *app, const Unit *units, int unit_count, const SpriteSheet *fallback_sprite,
                  const SpriteCache *cache, const RtsGameInfo *game_info, uint32_t ticks);
void render_world_objects(App *app, const GameMap *map, const Tileset *tileset,
                          const Unit *units, int unit_count, const SpriteSheet *fallback_sprite,
                          const SpriteCache *cache, const RtsGameInfo *game_info, uint32_t ticks);
void render_visual_effects(App *app, const RtsVisualEffect *effects, int max_effects,
                           const SpriteCache *cache, const RtsGameInfo *game_info);

CachedSprite *sprite_cache_find(SpriteCache *cache, const char *name);
const SpriteSheet *sprite_cache_lookup(const SpriteCache *cache, const char *name);

void issue_move_order(const GameMap *map, Unit *units, int unit_count, Cell goal);
void rts_apply_mobjinfo_defaults(const RtsGameInfo *game_info, Unit *unit);
bool rts_set_unit_state(RtsStateContext *ctx, Unit *unit, int state_id);
bool rts_spawn_state_effect(RtsStateContext *ctx, int state_id, float gx, float gy, int facing_code);
bool rts_unit_fire_attack(RtsStateContext *ctx, Unit *attacker);
bool rts_unit_add_corpse_decoration(RtsStateContext *ctx, const Unit *unit);
int rts_direction_code_from_vector(const RtsGameInfo *game_info, float dx, float dy);
void rts_direction_vector_from_code(const RtsGameInfo *game_info, int code, float *dx, float *dy);
void update_units(GameMap *map, Unit *units, int *unit_count, RtsVisualEffect *effects,
                  int max_effects, const RtsGameInfo *game_info, float dt);
void update_visual_effects(GameMap *map, RtsVisualEffect *effects, int max_effects,
                           const RtsGameInfo *game_info, float dt);
void handle_event(App *app, const GameMap *map, Unit *units, int unit_count, const SDL_Event *e);
void update_camera_from_keyboard(App *app, float dt);
void clamp_camera_to_map(App *app, const GameMap *map, int viewport_w, int viewport_h);

void destroy_tileset(Tileset *tileset);
void destroy_map(GameMap *map);
void destroy_sprite(SpriteSheet *sprite);
void destroy_font(RtsBitmapFont *font);
void destroy_sprite_cache(SpriteCache *cache);

#endif
