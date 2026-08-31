#ifndef __GAME__
#define __GAME__

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
extern const gameinfo_t *const gameinfo;
extern const actortype_t *const mobjinfo;
extern const int num_mobjinfo;
extern const uidefinition_t *const gameui;   /* NULL if unused */

/* ── game functions ────────────────────────────────────────────────────── */

/* Convert native game metadata into the engine's canonical runtime format. */
void     G_InitGame(void);

/* Load the map at path into *out.  Returns true on success. */
bool     G_DoLoadLevel(const char *path, level_t *out);

/* Load tile/sprite assets into tileset and unit_sprite. */
bool     W_LoadAssets(SDL_Renderer *renderer, const char *root, const level_t *map,
                      const char *sprite, tileset_t *tileset, spritesheet_t *unit_sprite);

/* Populate mobjs[] with initial units from the map file.
   Returns the number of mobjs spawned, or 0 on none/error. */
int      P_LoadThings(const char *path, mobj_t *mobjs, int max);

/* Load per-unit sprites into cache after units are known.
   Returns true on success (partial loads are allowed). */
bool     R_InitSprites(SDL_Renderer *renderer, const char *root, const level_t *map,
                       const mobj_t *mobjs, int count, spritecache_t *cache);

/* Load the game UI font into *font.  Returns false if the game has no font. */
bool     HU_LoadFont(SDL_Renderer *renderer, const char *root, bitmapfont_t *font);

/* Load a mission script for the map.  Returns NULL if none exists. */
void    *G_LoadMission(const char *path);

/* Advance mission state by dt seconds. */
void     G_MissionTicker(void *mission, level_t *map, mobj_t *mobjs, int *count,
                         effect_t *effects, int max_effects, hudtext_t *hud, float dt);

/* Release a loaded mission. */
void     G_FreeMission(void *mission);

#endif
