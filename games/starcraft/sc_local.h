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
} sc_unit_t;
extern const sc_unit_t sc_units[SC_TYPES];
extern char sc_names[SC_TYPES][16];
extern uint32_t sc_palette[256];
bool sc_read(const char *root, const char *name, blob_t *out);
void sc_asset_path(char *out, size_t size, const char *root, const char *name);
bool sc_briefing(const char *path, char *text, size_t text_size, char *objectives, size_t objectives_size);
bool sc_load_graphics(const char *root, const level_t *map, spritecache_t *cache);
bool sc_load_tiles(const char *root, const level_t *map, tileset_t *out);
bool sc_load_chk(const char *path, level_t *out);
/* Lobby races for the match about to load. NULL clears them. Index 0 is
 * Terran, 1 Zerg, 2 Protoss. Single-player loads ignore the list. */
void sc_set_net_races(const int *races);
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
} sc_mission_t;
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
