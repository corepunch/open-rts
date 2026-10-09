#ifndef __SC_LOCAL__
#define __SC_LOCAL__
#include "engine.h"
#include "starcraft.h"
#include "info.h"
typedef struct {
    const char *name;
    int hp;
    uint32_t flags;
    isize2_t placement;
    int sight, orders, race, minerals, gas, portrait;
    int build_time, damage, range, cooldown;
    int supply_provided, supply_required; /* units.dat halves; 2 is one supply */
    int armor, armor_upgrade, weapon_upgrade, damage_bonus; /* upgrades.dat ids; 255 none */
    int build_score, destroy_score;
    int shields;   /* 0 without units.dat shield enable */
    int subunit;   /* Turret row whose weapons the unit fires; SC_TYPES for none */
    int air_damage, air_range, air_cooldown;
} sc_unit_t;
extern const sc_unit_t sc_units[SC_TYPES];
/* upgrades.dat: cost and time are base plus factor per level already held. */
enum { SC_UPGRADES = 46 };
typedef struct {
    const char *name;
    int minerals, mineral_factor, gas, gas_factor, time, time_factor, icon, race, max_level;
} sc_upgrade_t;
extern const sc_upgrade_t sc_upgrades[SC_UPGRADES];
int sc_upgrade_level(int owner, int upgrade);
void sc_reset_upgrades(void);
/* False for an upgrade level other than the owner's next one. */
bool sc_upgrade_offered(int owner, const StaticProductDefinition *product);
extern char sc_names[SC_TYPES][16];
extern uint32_t sc_palette[256];
bool sc_read(const char *root, const char *name, blob_t *out);
void sc_asset_path(char *out, size_t size, const char *root, const char *name);
bool sc_briefing(const char *path, char *text, size_t text_size, char *objectives, size_t objectives_size);
/* MBRF actions in order (PyMS TRG.py briefing table). Text and wav are
 * offsets into strings, or -1. Times are milliseconds. */
enum {
    SC_BRIEF_WAIT = 1, SC_BRIEF_WAV, SC_BRIEF_TEXT, SC_BRIEF_OBJECTIVES, SC_BRIEF_SHOW_PORTRAIT,
    SC_BRIEF_HIDE_PORTRAIT, SC_BRIEF_TALK, SC_BRIEF_TRANSMISSION, SC_BRIEF_SKIP_TUTORIAL,
};
typedef struct { uint8_t op, slot; uint16_t unit; int time, text, wav; } sc_brief_action_t;
typedef struct {
    sc_brief_action_t actions[64];
    int count;
    char strings[8192];
} sc_briefing_t;
bool sc_briefing_script(const char *path, sc_briefing_t *out);
/* The open briefing: advance its clock; whether a portrait frame shows a
 * speaker and whether that speaker is talking. */
void sc_briefing_advance(unsigned ms);
bool sc_briefing_slot(int slot, bool *talking);
/* units.dat portrait index to its idle (fid) or talking (tlk) movie. */
bool sc_portrait_movie(const char *root, int id, bool talking, char *path, size_t size);
bool sc_load_graphics(const char *root, const level_t *map, spritecache_t *cache);
/* The script a worker plays while it mines (iscript AlmostBuilt). */
void sc_set_harvest_state(int type, int state);
bool sc_load_tiles(const char *root, const level_t *map, tileset_t *out);
bool sc_load_chk(const char *path, level_t *out);
/* Lobby races for the match about to load. NULL clears them. Index 0 is
 * Terran, 1 Zerg, 2 Protoss. Single-player loads ignore the list. */
void sc_set_net_races(const int *races);
/* Single-player custom game, one entry per playable CHK slot in order.
 * NULL clears it; campaign and network loads leave the map's own slots. */
enum { SC_SLOT_HUMAN, SC_SLOT_COMPUTER, SC_SLOT_CLOSED };
void sc_set_custom_slots(const int *kinds, const int *races);
bool sc_start_tip(const level_t *map, char *text, size_t size);
int sc_spawn_things(void);
bool sc_portrait(const char *root,int id,char *path,size_t size);
bool sc_dialog(const char *root,const char *path,sc_dialog_t *out);
bool sc_font_colors(const char *root,const char *path,bitmapfont_t *font);
void sc_init_info(void);
bool sc_font(const char *root, const char *name, bitmapfont_t *font);
/* CHK mission: level.mission begins with the map blob so existing readers
 * keep working. The rest is the trigger runtime. */
typedef struct {
    int cursor, wait_ms, current;
    bool running, preserve, disabled;
} sc_trig_t;
typedef struct {
    blob_t file;
    const uint8_t *trig, *str, *uprp;
    size_t trig_size, str_size, uprp_size;
    int loc_left[64], loc_top[64], loc_right[64], loc_bottom[64];
    int loc_count;
    uint8_t owners[12], side[12], force[8], force_flags[4];
    sc_trig_t *rt;
    int trig_count;
    uint8_t switches[256];
    uint16_t deaths[8][SC_TYPES], kills[8][SC_TYPES];
    int elapsed_ms, countdown_ms;
    bool countdown_paused, has_victory, ai_on[8];
    int result;
    char objectives[512], next_scenario[256];
    uint32_t rng;
    bool view_pending;
    fvec2_t view;
    int stock[8][2], gathered[8][2], spent[8];
    bool stock_seen;
} sc_mission_t;
/* Score inputs: deaths and kills by type, income and spending since the start. */
typedef struct { int lost[SC_TYPES], killed[SC_TYPES], gathered[2], spent; } sc_stats_t;
void sc_player_stats(int owner, sc_stats_t *out);
int sc_elapsed_ms(void);
bool sc_mission_bind(level_t *map);
void sc_mission_tick(level_t *map, hudtext_t *hud, float dt);
void sc_note_damage(mobj_t *mo);
mobj_t *sc_spawn_actor(unsigned type, ivec2_t pixel, uint8_t owner);
uint8_t sc_allegiance_for(uint8_t owner);
int sc_owner_kind(int owner);
int sc_player_side(int owner);
bool sc_player_ai(int owner);
bool sc_supply_ok(int owner, const StaticProductDefinition *product);
void sc_supply_counts(int owner, int *used, int *provided);
const char *sc_objectives_text(void);
void sc_show_result(int result);
#endif
