#ifndef OPEN_RTS_ENGINE_H
#define OPEN_RTS_ENGINE_H

#include "app.h"
#include "actor.h"
#include "assets.h"
#include "engine_config.h"
#include "facing.h"
#include "map.h"
#include "render_plan.h"
#include "sprites.h"

#include <stddef.h>
#include <stdint.h>

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
void font_draw_text(SDL_Renderer *renderer, const BitmapFont *font, int x, int y,
                        const char *text, SDL_Color color, int scale);
void font_draw_text_wrapped(SDL_Renderer *renderer, const BitmapFont *font, int x, int y,
                                int max_w, const char *text, SDL_Color color, int scale);
int font_text_width(const BitmapFont *font, const char *text, int scale);
void hud_text_push(HudText *hud, const char *text, int ttl_ms);
void hud_text_update(HudText *hud, float dt);

int map_index(const GameMap *map, int x, int y);
bool map_contains(const GameMap *map, int x, int y);
bool map_walkable(const GameMap *map, int x, int y);
int astar_find(const GameMap *map, Cell start, Cell goal, Cell *out_path, int max_path);

void grid_to_screen(const App *app, float gx, float gy, float *sx, float *sy);
Cell screen_to_grid(const App *app, int sx, int sy);
void map_grid_to_screen(const App *app, const GameMap *map, float gx, float gy,
                        float *sx, float *sy);
Cell screen_to_map_grid(const App *app, const GameMap *map, int sx, int sy);
void refresh_app_viewport(App *app);
void window_to_render_point(const App *app, int wx, int wy, int *rx, int *ry);
void window_to_render_delta(const App *app, int wx, int wy, float *rx, float *ry);
void render_grid_cell(App *app, int gx, int gy, SDL_Color color);
void render_tile_at(App *app, const Tileset *tileset, int tile, SDL_Rect src_part, SDL_Rect dst_part);
void render_map(App *app, const GameMap *map, const Tileset *tileset);
void render_decorations(App *app, const GameMap *map, const SpriteCache *cache);
void render_units(App *app, const Unit *units, int unit_count, const SpriteSheet *fallback_sprite,
                  const SpriteCache *cache, const GameInfo *game_info, uint32_t ticks);
void render_world_objects(App *app, const GameMap *map, const Tileset *tileset,
                          const Unit *units, int unit_count, const SpriteSheet *fallback_sprite,
                          const SpriteCache *cache, const GameInfo *game_info, uint32_t ticks);
void render_visual_effects(App *app, const GameMap *map,
                           const VisualEffect *effects, int max_effects,
                           const SpriteCache *cache, const GameInfo *game_info);

CachedSprite *sprite_cache_find(SpriteCache *cache, const char *name);
const SpriteSheet *sprite_cache_lookup(const SpriteCache *cache, const char *name);

void issue_move_order(const GameMap *map, Unit *units, int unit_count, Cell goal);
void issue_move_order_at(const GameMap *map, Unit *units, int unit_count,
                         float goal_gx, float goal_gy);
bool issue_harvest_order_at(const GameMap *map, Unit *units, int unit_count,
                            float gx, float gy);
void apply_mobjinfo_defaults(const GameInfo *game_info, Unit *unit);
bool set_unit_state(StateContext *ctx, Unit *unit, int state_id);
bool spawn_state_effect(StateContext *ctx, int state_id, float gx, float gy, int facing_code);
bool unit_fire_attack(StateContext *ctx, Unit *attacker);
bool unit_add_corpse_decoration(StateContext *ctx, const Unit *unit);
int direction_code_from_vector(const GameInfo *game_info, float dx, float dy);
void direction_vector_from_code(const GameInfo *game_info, int code, float *dx, float *dy);
void update_units(GameMap *map, Unit *units, int *unit_count, VisualEffect *effects,
                  int max_effects, const GameInfo *game_info, float dt);
void update_visual_effects(GameMap *map, VisualEffect *effects, int max_effects,
                           const GameInfo *game_info, float dt);
void handle_event(App *app, const GameMap *map, Unit *units, int unit_count,
                  const SpriteSheet *fallback_sprite, const SpriteCache *cache,
                  const GameInfo *game_info, const SDL_Event *e);
void update_camera_from_keyboard(App *app, float dt);
void clamp_camera_to_map(App *app, const GameMap *map, int viewport_w, int viewport_h);

void destroy_tileset(Tileset *tileset);
void destroy_map(GameMap *map);
void destroy_sprite(SpriteSheet *sprite);
void destroy_font(BitmapFont *font);
void destroy_sprite_cache(SpriteCache *cache);

#endif
