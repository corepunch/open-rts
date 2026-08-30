#ifndef OPEN_RTS_GAME_H
#define OPEN_RTS_GAME_H

/*
 * Doom-style game interface.  Every game binary defines these externs in its
 * own games/{GameDir}/plugin.c (or game.c).  The engine calls them by name —
 * no vtable, no dlopen, no registry.
 *
 * Include this header from both the engine side (engine calls) and the game
 * side (game implements).
 */

#include "engine.h"
#include "ui_definition.h"

/* ── game identity ─────────────────────────────────────────────────────── */
extern const char *const g_game_id;
extern const char *const g_game_name;
extern const char *const g_game_default_root;
extern const char *const g_game_default_map;
extern const char *const g_game_default_sprite;
extern const int g_cell_w;
extern const int g_cell_h;
extern const uint16_t g_debug_enemy_type;
extern const GameInfo *const gameinfo;
extern const MobjType *const mobjTypes;
extern const int mobjTypeCount;
extern const GameUiDefinition *const gameui;   /* NULL if unused */

/* ── game functions ────────────────────────────────────────────────────── */

/* Load the map at path into *out.  Returns true on success. */
bool     G_LoadMap(const char *path, GameMap *out);

/* Load tile/sprite assets into tileset and unit_sprite. */
bool     R_LoadAssets(SDL_Renderer *renderer, const char *root, const GameMap *map,
                      const char *sprite, Tileset *tileset, SpriteSheet *unit_sprite);

/* Populate mobjs[] with initial units from the map file.
   Returns the number of mobjs spawned, or 0 on none/error. */
int      G_SpawnThings(const char *path, Mobj *mobjs, int max);

/* Load per-unit sprites into cache after units are known.
   Returns true on success (partial loads are allowed). */
bool     R_LoadRuntimeSprites(SDL_Renderer *renderer, const char *root, const GameMap *map,
                               const Mobj *mobjs, int count, SpriteCache *cache);

/* Load the game UI font into *font.  Returns false if the game has no font. */
bool     R_LoadFont(SDL_Renderer *renderer, const char *root, BitmapFont *font);

/* Load a mission script for the map.  Returns NULL if none exists. */
void    *G_LoadMission(const char *path);

/* Advance mission state by dt seconds. */
void     G_UpdateMission(void *mission, GameMap *map, Mobj *mobjs, int *count,
                          VisualEffect *effects, int max_effects, HudText *hud, float dt);

/* Release a loaded mission. */
void     G_FreeMission(void *mission);

#endif
