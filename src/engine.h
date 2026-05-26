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
#define MAX_DECORATIONS 8192
#define MAX_DECORATION_SPRITES 512
#define MAX_TILE_OVERLAYS 3
#define MAX_TILE_ANIMATION_FRAMES 8
#define MAX_SPRITE_SEQUENCES 8
#define MAX_SEQUENCE_FACINGS 16
#define MAX_PATH_CELLS 4096
#define FIXED_DT (1.0f / 30.0f)

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
} RtsTrait;

typedef struct {
    uint16_t id;
    const char *name;
    const char *sprite_name;
    const char *shadow_name;
    uint32_t traits;
    float speed;
    int max_hp;
} RtsActorType;

typedef struct {
    int gx;
    int gy;
    int footprint_w;
    int footprint_h;
    bool solid;
    char sprite_name[32];
    char shadow_name[32];
} MapDecoration;

typedef struct {
    char name[32];
    SpriteSheet sprite;
} CachedSprite;

typedef struct {
    CachedSprite entries[MAX_DECORATION_SPRITES];
    int count;
} SpriteCache;

typedef struct App App;

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
    char tileset_name[32];
    void (*render_transitions)(App *app, const struct GameMap *map, const Tileset *tileset,
                               int x, int y, int dx, int dy);
} GameMap;

typedef struct {
    float gx;
    float gy;
    float speed;
    int facing_code;
    uint16_t type_id;
    uint8_t owner;
    uint32_t traits;
    int hp;
    int max_hp;
    bool selected;
    char sprite_name[32];
    char shadow_name[32];
    Cell path[MAX_PATH_CELLS];
    int path_len;
    int path_index;
} Unit;

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

int map_index(const GameMap *map, int x, int y);
bool map_contains(const GameMap *map, int x, int y);
bool map_walkable(const GameMap *map, int x, int y);
int astar_find(const GameMap *map, Cell start, Cell goal, Cell *out_path, int max_path);

void grid_to_screen(const App *app, float gx, float gy, float *sx, float *sy);
Cell screen_to_grid(const App *app, int sx, int sy);
void render_grid_cell(App *app, int gx, int gy, SDL_Color color);
void render_tile_at(App *app, const Tileset *tileset, int tile, SDL_Rect src_part, SDL_Rect dst_part);
void render_map(App *app, const GameMap *map, const Tileset *tileset);
void render_decorations(App *app, const GameMap *map, const SpriteCache *cache);
void render_units(App *app, const Unit *units, int unit_count, const SpriteSheet *fallback_sprite,
                  const SpriteCache *cache, uint32_t ticks);

CachedSprite *sprite_cache_find(SpriteCache *cache, const char *name);
const SpriteSheet *sprite_cache_lookup(const SpriteCache *cache, const char *name);

void issue_move_order(const GameMap *map, Unit *units, int unit_count, Cell goal);
void update_units(Unit *units, int unit_count, float dt);
void handle_event(App *app, const GameMap *map, Unit *units, int unit_count, const SDL_Event *e);
void update_camera_from_keyboard(App *app, float dt);

void destroy_tileset(Tileset *tileset);
void destroy_map(GameMap *map);
void destroy_sprite(SpriteSheet *sprite);
void destroy_sprite_cache(SpriteCache *cache);

#endif
