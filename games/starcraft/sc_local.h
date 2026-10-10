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
    int build_time;
    int supply_provided, supply_required; /* units.dat halves; 2 is one supply */
    int armor, armor_upgrade; /* upgrades.dat id; 255 none */
    int build_score, destroy_score;
    int shields;   /* 0 without units.dat shield enable */
    int subunit;   /* Turret row whose weapons the unit fires; SC_TYPES for none */
    int ground_weapon, air_weapon; /* weapons.dat rows; no row (100 here) for none */
    int size;      /* SC_SIZE_* */
    int speed;     /* Top speed, 1/256 pixel per 24 Hz frame (flingy.dat or walking script) */
    int space, space_provided; /* Transport slots taken and offered; 255 cannot board */
    ivec2_t addon; /* An add-on's top-left from its parent's, in pixels */
} sc_unit_t;
extern sc_unit_t sc_units[SC_TYPES];
enum { SC_SIZE_INDEPENDENT, SC_SIZE_SMALL, SC_SIZE_MEDIUM, SC_SIZE_LARGE };
/* units.dat special ability flags. */
enum {
    SC_UNIT_BUILDING = 0x1, SC_UNIT_WORKER = 0x8, SC_UNIT_HERO = 0x40, SC_UNIT_CLOAKABLE = 0x200,
    SC_UNIT_ROBOTIC = 0x4000, SC_UNIT_ORGANIC = 0x10000, SC_UNIT_SPELLCASTER = 0x200000,
    SC_UNIT_PERMANENT_CLOAK = 0x400000, SC_UNIT_MECHANICAL = 0x40000000,
};
/* Map pixels (32 to a cell) as 16.16 cells. */
#define SC_PIXELS(px) ((fixed_t)((px) * (FIXED_ONE / 32)))
/* Whether a mobj is a units.dat type, and its row. */
static inline const sc_unit_t *sc_unit(const mobj_t *mo) {
    return mo && mo->type_id >= 1 && mo->type_id <= SC_TYPES ? &sc_units[mo->type_id - 1] : NULL;
}
/* weapons.dat (PyMS names). Ranges and radii are pixels, cooldown 24 Hz
 * frames; factor is the hits of one attack. Rows past the DAT have no name. */
enum { SC_WEAPONS = 130 };
enum { SC_DAMAGE_INDEPENDENT, SC_DAMAGE_EXPLOSIVE, SC_DAMAGE_CONCUSSIVE, SC_DAMAGE_NORMAL, SC_DAMAGE_IGNORE_ARMOR };
enum { SC_EXPLOSION_RADIAL = 2, SC_EXPLOSION_ENEMY = 3, SC_EXPLOSION_AIR = 24 };
enum { SC_BEHAVIOR_BOUNCE = 7 };
typedef struct {
    const char *name;
    int damage, bonus, cooldown, factor, upgrade, type, explosion, behavior, min_range, max_range;
    int splash[3];
    unsigned targets;
} sc_weapon_t;
extern sc_weapon_t sc_weapons[SC_WEAPONS];
/* techdata.dat: research cost and time, and the energy a use costs. The
 * Nuclear Strike is an order past the DAT that shares the spell command. */
enum {
    SC_TECHS = 44, SC_TECH_STIM_PACKS = 0, SC_TECH_LOCKDOWN = 1, SC_TECH_EMP = 2, SC_TECH_SCANNER_SWEEP = 4,
    SC_TECH_SIEGE_MODE = 5, SC_TECH_DEFENSIVE_MATRIX = 6, SC_TECH_IRRADIATE = 7, SC_TECH_YAMATO_GUN = 8,
    SC_TECH_CLOAKING_FIELD = 9, SC_TECH_PERSONNEL_CLOAKING = 10, SC_TECH_SPAWN_BROODLING = 13,
    SC_TECH_DARK_SWARM = 14, SC_TECH_PLAGUE = 15, SC_TECH_CONSUME = 16, SC_TECH_ENSNARE = 17,
    SC_TECH_PARASITE = 18, SC_TECH_PSIONIC_STORM = 19, SC_TECH_HALLUCINATION = 20, SC_TECH_ARCHON_WARP = 23,
    SC_TECH_NUCLEAR_STRIKE = SC_TECHS,
};
typedef struct { const char *name; int minerals, gas, time, energy, race, icon; } sc_tech_t;
extern sc_tech_t sc_techs[SC_TECHS];
/* NULL for a row past the DAT. */
const sc_weapon_t *sc_weapon(int id);
/* Fills a type's attack and air_attack from its weapons.dat rows. */
void sc_unit_weapons(int type, mobjtype_t *out);
/* Sets the combat hooks: damage types, shields, energy and cloaking. */
void sc_init_combat(void);
/* upgrades.dat: cost and time are base plus factor per level already held. */
enum { SC_UPGRADES = 46 };
typedef struct {
    const char *name;
    int minerals, mineral_factor, gas, gas_factor, time, time_factor, icon, race, max_level;
} sc_upgrade_t;
extern sc_upgrade_t sc_upgrades[SC_UPGRADES];
/* Catalog ui id of upgrade research: SC_UPGRADE_UI + upgrade * 4 + the level it starts from. */
enum { SC_UPGRADE_UI = 1000 };
int sc_upgrade_level(int owner, int upgrade);
int sc_upgrade_product(int upgrade, int level);
/* False for an upgrade level other than the owner's next one. */
/* Whether owner has a building of type, or one that counts as it. */
bool sc_owner_has(int owner, uint16_t type);
bool sc_upgrade_offered(int owner, const StaticProductDefinition *product);
extern char sc_names[SC_TYPES][16];
extern uint32_t sc_palette[256];
bool sc_read(const char *root, const char *name, blob_t *out);
/* An images.tbl string, or NULL past the table. */
const char *sc_tbl_string(const blob_t *tbl, unsigned index);
/* sprites.dat selection circles and status bars: loaded with the unit
 * graphics, drawn as the world underlay and overlay hooks. */
bool sc_load_selection(const char *root, const blob_t *units, const blob_t *flingy,
                       const blob_t *sprites, const blob_t *images, const blob_t *names);
void sc_free_selection(void);
void sc_draw_selection_circle(const unitoverlaycontext_t *ctx);
void sc_draw_status_bars(const unitoverlaycontext_t *ctx);
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

/* Map stat overrides (CHK UNIx/UNIS, UPGx/UPGS, TECx/TECS, PUNI) as ruleset
 * patch entries. */
enum { SC_PATCH_UNIT = 1, SC_PATCH_UPGRADE, SC_PATCH_TECH, SC_PATCH_UNAVAILABLE, SC_PATCH_WEAPON };
enum { SC_WEAPON_DAMAGE, SC_WEAPON_BONUS };
enum { SC_UNIT_HP, SC_UNIT_SHIELDS, SC_UNIT_ARMOR, SC_UNIT_BUILD_TIME, SC_UNIT_MINERALS, SC_UNIT_GAS };
enum { SC_UPGRADE_MINERALS, SC_UPGRADE_MINERAL_FACTOR, SC_UPGRADE_GAS, SC_UPGRADE_GAS_FACTOR,
       SC_UPGRADE_TIME, SC_UPGRADE_TIME_FACTOR };
enum { SC_TECH_MINERALS, SC_TECH_GAS, SC_TECH_TIME, SC_TECH_ENERGY };
/* Section payloads of one scenario; NULL when absent. Broodwar's x sections win. */
typedef struct {
    const uint8_t *unis, *unix_, *upgs, *upgx, *tecs, *tecx, *puni;
    size_t unis_size, unix_size, upgs_size, upgx_size, tecs_size, tecx_size, puni_size;
} sc_rule_sections_t;
/* Adds an entry for every value the scenario overrides: a unit, upgrade or
 * tech whose "use defaults" byte is clear, and each unit a player may not
 * build (row = player, field = unit). Returns the entries added. */
int sc_decode_rules(const sc_rule_sections_t *sections, rulepatchset_t *out);
/* Owner may not build this unit (PUNI). */
bool sc_unit_unavailable(int owner, int type);
void sc_refresh_actors(void);
void sc_rebuild_products(void);
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
void sc_mission_tick(level_t *map, hudtext_t *hud, int dt_ms);
void sc_note_damage(mobj_t *mo);
mobj_t *sc_spawn_actor(unsigned type, ivec2_t pixel, uint8_t owner);
uint8_t sc_allegiance_for(uint8_t owner);
int sc_owner_kind(int owner);
int sc_player_side(int owner);
bool sc_player_ai(int owner);
bool sc_supply_ok(int owner, const StaticProductDefinition *product, const mobj_t *maker);
void sc_supply_counts(int owner, int *used, int *provided);
/* Zerg: the egg (or cocoon) a larva (or mutalisk) turns into when it is
 * given an order, MT_NONE for other makers. */
uint16_t sc_egg_for(uint16_t maker);
mobj_t *sc_spawn_larva(mobj_t *hatchery);
/* Retail tech tree: a Lair or Hive counts as a Hatchery, a Hive as a
 * Lair, a Greater Spire as a Spire. */
bool sc_counts_as(uint16_t type, uint16_t as);
/* False for a Protoss building outside its owner's psi fields. */
bool sc_powered(const mobj_t *mo);
const char *sc_objectives_text(void);
void sc_show_result(int result);

/* Researched techs live in level.upgrades[SC_UPGRADES + tech][owner].weapon,
 * saved and hashed with the level. Scanner Sweep, Defensive Matrix, Dark
 * Swarm, Parasite and Archon Warp need no research, as in retail melee. */
bool sc_has_tech(int owner, int tech);
/* Whether tech is researched rather than given. */
bool sc_tech_researched(int tech);
/* A unit type's abilities (the techs it casts), for its command card. */
int sc_unit_techs(uint16_t type, int *out, int cap);
/* Whether a tech is aimed at a unit or a spot, rather than cast at once. */
bool sc_tech_aimed(int tech);
/* Pending orders a unit walks to carry out. */
enum { SC_ORDER_NONE, SC_ORDER_CAST, SC_ORDER_BOARD, SC_ORDER_MERGE, SC_ORDER_NUKE };
/* Spell upkeep each engine tic: timers, damage over time, effect areas,
 * disabled units and pending casts. frames is the 24 Hz frames passed. */
void sc_spell_ticker(mobj_t *mo, int frames);
/* What spells make of one hit, in 1/256 points: hallucinations, the
 * Defensive Matrix and Dark Swarm. */
int sc_spell_hit(const mobj_t *attacker, const weapondef_t *weapon, mobj_t *target, int dealt);
uint32_t sc_sight_teams(const mobj_t *mo);
/* A weapons.dat hit of damage by owner's weapon on target, through shields,
 * armor and size, as hit_damage takes it; for spells with no attacker. */
int sc_hit(int owner, const sc_weapon_t *weapon, mobj_t *target, int damage, int divisor);
int sc_max_energy(const mobj_t *mo);
/* Shields and energy take their spawn values on first use. */
void sc_start(mobj_t *mo);
/* Interceptors, scarabs and a silo's nuke: what a maker keeps in its
 * hangar (MT_NONE for none) and how many it holds. */
uint16_t sc_hangar_type(uint16_t maker);
int sc_hangar_capacity(const mobj_t *maker);
/* Docked plus launched (interceptors, scarabs in flight). */
int sc_hangar_count(const mobj_t *maker);
/* Unit upkeep each engine tic: loaded units, interceptors and scarabs,
 * boarding and archon merges. */
void sc_unit_ticker(mobj_t *mo, int frames);
bool sc_launch(mobj_t *attacker, const weapondef_t *weapon, mobj_t *target);
fixed_t sc_range_bonus(const mobj_t *attacker, const weapondef_t *weapon);
/* Add-ons: the building that builds an add-on type (MT_NONE if it is not
 * one), the add-on attached to a building, and attaching a new one at
 * units.dat's add-on position. */
uint16_t sc_addon_parent(uint16_t type);
mobj_t *sc_addon_of(const mobj_t *building);
bool sc_addon_site(const mobj_t *building, uint16_t type, ivec2_t *cell);
/* The cells the first add-on of a building of type with its top-left at
 * cell would take, and that add-on; false for a type without add-ons. */
bool sc_addon_place(uint16_t type, ivec2_t cell, uint16_t *addon, irect_t *out);
mobj_t *sc_attach_addon(mobj_t *building, uint16_t type);
/* Bunkers: slots in use, boarding (walks there first) and unloading. */
int sc_cargo_space(const mobj_t *bunker);
bool sc_can_board(const mobj_t *unit, const mobj_t *bunker);
bool sc_board(mobj_t *unit, mobj_t *bunker);
void sc_unload(mobj_t *bunker);
/* Two High Templar walk together and become an Archon. */
bool sc_merge(mobj_t *a, mobj_t *b);
/* A silo of owner with a nuke ready, or NULL. */
mobj_t *sc_armed_silo(int owner);
/* Computer players: spells, sieging, Bunkers, hangars and research. */
void sc_ai_tactics(level_t *map, int owner, mobj_t *const *units, int count);
/* Product ids: research of techdata row t is SC_TECH_UI + t. */
enum { SC_TECH_UI = 2000 };
#endif
