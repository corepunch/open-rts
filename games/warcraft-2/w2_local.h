#ifndef __W2_LOCAL__
#define __W2_LOCAL__

#include "engine.h"
#include "warcraft-2.h"
#include "info.h"

#include <stddef.h>
#include <stdint.h>

/* Stratagus advances a time-cost unit every six 30 Hz cycles. */
static inline int w2_cost_ms(int cost) { return cost * 6 * 1000 / RTS_TICRATE; }
void w2_advance_build(mobj_t *site, int ticks);
bool w2_site_reachable(const mobj_t *walker, ivec2_t cell, isize2_t foot);
/* Whether a footprint stands on the way between one of the owner's halls and its gold mine. */
bool w2_blocks_mining(int owner, ivec2_t cell, isize2_t foot);

#define W2_TILE_LOOKUP 0x9E0
/* Native special megatiles have no PUD slots. Append runtime slots. */
enum {
    W2_TILE_REMOVED_TREE = W2_TILE_LOOKUP,
    W2_TILE_TREE_TOP, W2_TILE_TREE_MIDDLE, W2_TILE_TREE_BOTTOM,
    W2_TILE_COUNT,
};
/* Stratagus Speed is not cells per second. This divisor is an engine
 * presentation choice so a footman (Speed 10) walks at 1.25 cells/s. */
#define W2_SPEED_DIVISOR 8.0f
#define W2_WALK_TICS 4
#define W2_ENTRY_LIMIT (16u * 1024u * 1024u)

typedef struct {
    uint8_t *data;
    size_t size;
} w2_blob_t;

typedef struct {
    uint8_t *file;
    size_t file_size;
    int count;
    uint32_t *offsets;
} w2_archive_t;

typedef struct {
    uint16_t x, y;
    uint8_t type, player;
    uint16_t data;
} w2_pud_unit_t;

typedef struct {
    int era;
    int ver;
    uint8_t owners[16];
    uint8_t sides[16];
    uint8_t ai[16];     /* AIPL: 0 land attack, 1 passive, 25 sea attack, 26 air attack, others campaign scripts */
    w2_pud_unit_t *units;
    int unit_count;
    int view_player;
} w2_pud_t;
void w2_build_info(void);
extern const soundinfo_t w2_soundinfo;
void A_W2_Chop(mobj_t *actor);
void A_W2_Platform(mobj_t *actor);
void w2_init_products(void);
/* The computer player's battle orders (p_ai.c): waves split by what can
 * reach the goal, ships, transports and spells. */
bool w2_ai_dispatch(level_t *map, int owner, mobj_t *const *wave, int count, mobj_t *goal);
void w2_ai_tactics(level_t *map, int owner, mobj_t *const *units, int count);
typedef struct {
    const char *name;
    int frames, sleep, speed, range, splash, impact, bounces;
    bool directional;
} w2_effect_t;
extern const w2_effect_t w2_effects[W2_FX_COUNT];
void A_W2_Effect(mobj_t *effect);
mobj_t *w2_spawn_effect(mobj_t *source, int kind, fixed3_t position);
bool w2_fire_projectile(mobj_t *source, mobj_t *target);
extern const int w2_spell_sounds[W2_SPELL_COUNT];
void w2_build_states(int pud, int phases);
int w2_pud_named(const char *name);

bool w2_archive_open(w2_archive_t *arc, const char *path);
void w2_archive_close(w2_archive_t *arc);
bool w2_archive_extract(const w2_archive_t *arc, int index, w2_blob_t *out);
void w2_blob_free(w2_blob_t *blob);

bool w2_decode_palette(const w2_blob_t *entry, uint32_t palette[256]);
bool w2_decode_tileset(const w2_archive_t *arc, int era, tileset_t *out);
bool w2_decode_grp(const w2_blob_t *entry, const uint32_t palette[256],
                   spritesheet_t *out, bool directional, int *phases);
int w2_grp_entry(const mobjinfo_t *unit, int era, int archive_count);
int w2_install_team_colors(spritesheet_t *sprite, const uint32_t palette[256]);

bool w2_load_pud(const char *path, level_t *out);
/* Eight lobby races, -1 for an empty seat. NULL clears them. */
void w2_set_net_races(const int *races);
/* In a net game, move person slots onto seats 0..numplayers-1 and apply races. */
void w2_apply_net_seats(level_t *map);

/* What a scenario's header says, read without loading the map. */
typedef struct {
    char description[33];
    int width, height, era;
    uint8_t owners[16], sides[16];
} w2_pud_info_t;
bool w2_pud_info_bytes(const uint8_t *data, size_t size, w2_pud_info_t *out);
bool w2_pud_info(const char *path, w2_pud_info_t *out);
/* Campaign levels are MAINDAT entries 192.. (human even, orc odd). */
enum { W2_CAMPAIGN_ENTRY = 192, W2_CAMPAIGN_LEVELS = 14 };
/* The built-in (skirmish) scenarios, MAINDAT 220..247. */
enum { W2_FIRST_SCENARIO = 220, W2_SCENARIOS = 28 };
typedef struct { int number; bool orc; } w2_campaign_t;
typedef struct {
    w2_campaign_t campaign;
    bool done, victory;
    int countdown;
} w2_mission_t;
void W2_SetCampaign(int number, bool orc);
bool w2_init_mission(level_t *map);
/* One archive read for the whole campaign; a level that is absent has width 0. */
bool w2_campaign_infos(const char *root, bool orc, w2_pud_info_t infos[W2_CAMPAIGN_LEVELS]);
bool w2_extract_campaign_level(const char *root, int level, bool orc, char *path, size_t size);
bool w2_extract_map(const char *root, int entry, const char *name, char *path, size_t size);
/* The next level load starts every playing side with this much (0 keeps the map's). */
void W2_SetStartResources(int mode);
/* The end-of-scenario screen (m_menu.c). Continue then follows WAR2.EXE
 * state 0x8fe18: the next briefing, the same briefing, the title, or
 * the network screen. There is no replay prompt. */
void W2_ShowResult(bool victory);
int w2_spawn_units(void);
bool w2_init_resources(level_t *map);
void w2_remove_tree(ivec2_t cell);
bool w2_load_shared_sprites(const w2_archive_t *arc, const uint32_t palette[256], int era,
                            spritecache_t *cache);
/* A free cell beside a footprint that the unit can reach (p_harvest.c). */
bool w2_load_assets(const char *data_root, const level_t *map, const char *sprite_name,
                    tileset_t *tileset, spritesheet_t *unit_sprite);
bool w2_load_runtime_sprites(const char *data_root, const level_t *map,
                             mobj_t *const *units, int unit_count, spritecache_t *cache);
int w2_era_palette(int era);
void w2_mark_footprint(int x, int y, isize2_t foot);
void w2_clear_footprint(int x, int y, isize2_t foot);
bool w2_cache_unit_sprite(const char *root, spritecache_t *cache, int pud);

enum { W2_PANEL_GAME, W2_PANEL_OPTIONS, W2_PANEL_FILE, W2_PANEL_DIALOG, W2_PANEL_SCENARIO, W2_PANELS };

/* Dialog text (w_str.c): STRDAT entries that the menus read. */
enum {
    STR_TITLE = 3, STR_MAIN_MENU = 4, STR_MULTIPLAYER = 5, STR_CAMPAIGN = 6, STR_GAME_MENU = 7,
    STR_HELP_MENU = 8, STR_CUSTOM_MENU = 9, STR_OPTIONS = 10, STR_SOUND = 11, STR_SPEED = 13,
    STR_QUIT = 14, STR_RESTART = 15, STR_WIN = 20, STR_LOSE = 21, STR_SAVE = 26, STR_LOAD = 27,
    STR_MESSAGES = 40, STR_SETUP = 44, STR_SETUP_VALUES = 45, STR_HELP_SCREEN = 47,
    STR_OBJECTIVES = 52, STR_LEVELS = 53, STR_DISPATCH = 54, STR_CREDITS_BUTTON = 56,
    STR_CREDITS = 57, STR_KEYS = 59, STR_CONNECTION = 60, STR_PICK = 62, STR_SCENARIOS = 63
};
typedef struct { char text[160]; int mark_at, mark_len; } w2_text_t;
bool w2_strings_load(const char *root);
void w2_strings_free(void);
bool w2_label(int entry, int index, w2_text_t *out);
bool w2_resource_label(int resource, int index, w2_text_t *out);
/* One string, without w2_text_t's 160-byte limit. Briefings and credits need it. */
size_t w2_label_copy(int entry, int index, char *out, size_t size);
size_t w2_label_lines(int entry, int first, int last, char *out, size_t size);

/* REZDAT dialogs are linked 72-byte records. Decode directly into the engine
 * table; item zero is the window, children retain signed native control IDs.
 * Rendering art, radio groups, ranges, contents and callbacks are bound by
 * the caller: those runtime values are absent from the serialized records. */
bool w2_decode_scene(const w2_blob_t *blob, menuitem_t *items, int capacity, int *count);
bool w2_load_scene(const char *root, int resource, menuitem_t *items, int capacity, int *count);

/* Menu chrome is REZDAT. The in-game panel is MAINDAT. Each screen owns a font. */
typedef struct {
    spritesheet_t widgets[2]; /* 0 human, 1 orc */
    /* REZDAT 3..12 pairs: game menu 256x288, options 288x256, save/load
     * 384x256, message 288x128, scenario 352x352. */
    spritesheet_t panel[2][W2_PANELS];
    spritesheet_t title, dimmed; /* REZDAT 13 and the darker 15 behind popups */
    /* Five mission introscreens per race. Human palette 367, images
     * 369/375/376/377/378. Orc palette 368, images 370–374. Orc image 370
     * is the Zul'Dare scroll. */
    spritesheet_t briefing[2][5];
    spritesheet_t results[2][2]; /* race; victory/defeat images and their native palettes */
    bitmapfont_t font, small_font, tiny_font; /* MAINDAT 281, 282, 283 */
    bool ready;
} w2_menu_art_t;

typedef struct {
    spritesheet_t menu_button, minimap, info, buttons, resource, status, filler, icons, resource_icons;
    spritesheet_t menu_widgets, spell_icons;
    bitmapfont_t font, small_font;
    bool orc;
    bool ready;
} w2_hud_art_t;

bool w2_load_menu_art(const char *root, w2_menu_art_t *art);

/* A dialog item's flags: the native record's u16 flags, its native kind above. */
#define W2_ITEM_FLAGS(kind, flags) ((unsigned)(kind) << 16 | ((flags) & 0xffffu))
#define W2_ITEM_KIND(flags) ((int)((flags) >> 16 & 0xffu))
/* A scroll bar that draws its own arrows (no arrow buttons beside it). */
#define W2_ITEM_ARROWS 0x01000000u
/* Draws a menu item as WAR2.EXE does; the menus' menu_t.drawitem. */
void w2_dialog_art(const w2_menu_art_t *art);
bool w2_draw_item(const menu_t *menu, const menuitem_t *item, menustate_t state, irect_t rect);
void w2_free_menu_art(w2_menu_art_t *art);
bool w2_load_hud_art(const char *root, int era, bool orc, w2_hud_art_t *art);
void w2_free_hud_art(w2_hud_art_t *art);
void w2_draw_selection(const unitoverlaycontext_t *ctx);
void w2_draw_buffs(const unitoverlaycontext_t *ctx);

#endif
