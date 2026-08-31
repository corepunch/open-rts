#ifndef __ENGINE__
#define __ENGINE__

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

bool W_ReadFile(const char *path, blob_t *out);
void W_FreeFile(blob_t *blob);
void M_PathJoin(char *dst, size_t dst_size, const char *a, const char *b);
int clamp255(int value);
void V_IndexedToRGBA(uint32_t *dst, const uint8_t *src, size_t count, const uint32_t palette[256]);
void V_BlitIndexed(uint32_t *dst, int dst_w, int dst_h, int dst_x, int dst_y,
                   const uint8_t *src, int src_w, int src_h, const uint32_t palette[256]);
SDL_Texture *I_CreateTexture(SDL_Renderer *renderer, const uint32_t *pixels, int w, int h, bool blend);
bool R_AddTileAnim(tileset_t *tileset, int value, const int *frames,
                   int frame_count, uint16_t frame_ms);
void HU_DrawText(SDL_Renderer *renderer, const bitmapfont_t *font, int x, int y,
                 const char *text, SDL_Color color, int scale);
void HU_DrawTextWrapped(SDL_Renderer *renderer, const bitmapfont_t *font, int x, int y,
                        int max_w, const char *text, SDL_Color color, int scale);
int HU_TextWidth(const bitmapfont_t *font, const char *text, int scale);
void HU_PushMessage(hudtext_t *hud, const char *text, int ttl_ms);
void HU_Ticker(hudtext_t *hud, float dt);

int L_Index(const level_t *map, int x, int y);
bool L_Contains(const level_t *map, int x, int y);
bool L_IsWalkable(const level_t *map, int x, int y);
int P_FindPath(const level_t *map, cell_t start, cell_t goal, cell_t *out_path, int max_path);

void R_GridToScreen(const app_t *app, float gx, float gy, float *sx, float *sy);
cell_t R_ScreenToGrid(const app_t *app, int sx, int sy);
void R_MapToScreen(const app_t *app, const level_t *map, float gx, float gy,
                   float *sx, float *sy);
cell_t R_ScreenToMapGrid(const app_t *app, const level_t *map, int sx, int sy);
void R_RefreshViewport(app_t *app);
void R_WindowToRenderPt(const app_t *app, int wx, int wy, int *rx, int *ry);
void R_WindowToRenderDelta(const app_t *app, int wx, int wy, float *rx, float *ry);
void R_DrawCell(app_t *app, int gx, int gy, SDL_Color color);
void R_DrawTile(app_t *app, const tileset_t *tileset, int tile, irect_t src_part, irect_t dst_part);
void R_DrawLevel(app_t *app, const level_t *map, const tileset_t *tileset);
void R_DrawDecorations(app_t *app, const level_t *map, const spritecache_t *cache);
void R_DrawThings(app_t *app, const mobj_t *units, int unit_count, const spritesheet_t *fallback_sprite,
                  const spritecache_t *cache, const gameinfo_t *game_info, uint32_t ticks);
void R_RenderPlayerView(app_t *app, const level_t *map, const tileset_t *tileset,
                        const mobj_t *units, int unit_count, const spritesheet_t *fallback_sprite,
                        const spritecache_t *cache, const gameinfo_t *game_info, uint32_t ticks);
void R_DrawEffects(app_t *app, const level_t *map,
                   const effect_t *effects, int max_effects,
                   const spritecache_t *cache, const gameinfo_t *game_info);

cachedsprite_t *R_CacheFind(spritecache_t *cache, const char *name);
const spritesheet_t *R_CacheLookup(const spritecache_t *cache, const char *name);

void P_MoveOrder(const level_t *map, mobj_t *units, int unit_count, cell_t goal);
void P_MoveOrderAt(const level_t *map, mobj_t *units, int unit_count,
                   float goal_gx, float goal_gy);
bool P_HarvestOrderAt(const level_t *map, mobj_t *units, int unit_count,
                      float gx, float gy);
void P_SpawnMobj(const gameinfo_t *game_info, mobj_t *unit);
bool P_SetMobjState(statecontext_t *ctx, mobj_t *unit, int state_id);
bool P_SpawnEffect(statecontext_t *ctx, int state_id, float gx, float gy, angle_t angle);
bool P_Attack(statecontext_t *ctx, mobj_t *attacker);
bool P_AddCorpse(statecontext_t *ctx, const mobj_t *unit);
angle_t P_PointToAngle(float dx, float dy);
void P_AngleToVec(angle_t angle, float *dx, float *dy);
void P_Ticker(level_t *map, mobj_t *units, int *unit_count, effect_t *effects,
              int max_effects, const gameinfo_t *game_info, float dt);
void P_UpdateEffects(level_t *map, effect_t *effects, int max_effects,
                     const gameinfo_t *game_info, float dt);
void G_Responder(app_t *app, const level_t *map, mobj_t *units, int unit_count,
                 const spritesheet_t *fallback_sprite, const spritecache_t *cache,
                 const gameinfo_t *game_info, const SDL_Event *e);
void G_CameraMove(app_t *app, float dt);
void R_ClampCamera(app_t *app, const level_t *map, int viewport_w, int viewport_h);

void R_FreeTileset(tileset_t *tileset);
void P_FreeLevel(level_t *map);
void R_FreeSprite(spritesheet_t *sprite);
void HU_FreeFont(bitmapfont_t *font);
void R_FreeSpriteCache(spritecache_t *cache);

#endif
