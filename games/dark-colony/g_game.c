#define _DEFAULT_SOURCE
#include "game.h"
#include "dc_facing.h"
#include "engine.h"
#include "info.h"
#include "gamestat.h"
#include "dc_types.h"
#include "sb_bar.h"
#include "w_spr.h"
#include "p_mission.h"
#include "p_blood.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

bool load_dark_colony_map(const char *map_path, level_t *out);
int load_dark_colony_initial_units(const char *map_path);
extern bool load_dark_colony_tileset(SDL_Renderer *renderer, const char *path, tileset_t *out);

const mobjtype_t DARK_COLONY_ACTOR_TYPES[] = {
    {
        .id = MT_TROOPER,
        .native_type_id = 0,
        .damage_action = A_DC_Damage,
        .name = "Trooper",
        .sprite_name = "SPRITES/TRSC.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        /* GAMESTAT.TXT stores movement in pixels per 32 Hz tick.  The
         * simulation stores map cells per second: 25 / 32 is the authored
         * Trooper rate, not the old placeholder 5.0. */
        .speed = 25.0f / 32.0f,
        .max_hp = 800,
        .attack = { .range = 4.0f, .damage = 100, .cooldown_ms = 500 },
    },
    { .id = MT_BLOOD, .name = "Blood", .traits = MF_RENDERABLE | MF_NOBLOCKMAP },
    {
        .id = MT_GREY,
        .native_type_id = 8,
        .damage_action = A_DC_Damage,
        .name = "Grey",
        .sprite_name = "SPRITES/GRAY.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 25.0f / 32.0f,
        .max_hp = 800,
        .attack = { .range = 4.0f, .damage = 100, .cooldown_ms = 500 },
    },
    {
        .id = MT_EXPLOITER,
        .native_type_id = 6,
        .damage_action = A_DC_Damage,
        .name = "Exploiter",
        .sprite_name = "SPRITES/EXPL.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_HARVESTER,
        /* The gameplay tuning uses the documented heavy-harvester rate. */
        .speed = 3.5f,
        .max_hp = 800,
        .harvest = { .capacity = 0, .state_id = S_EXPL_DEPLOY1 },
    },
    {
        .id = MT_REAPER,
        .native_type_id = 2,
        .damage_action = A_DC_Damage,
        .name = "Mech",
        .sprite_name = "SPRITES/REAP.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 30.0f / 32.0f,
        .max_hp = 800,
        .attack = { .range = 4.0f, .damage = 100, .cooldown_ms = 500 },
    },
    {
        .id = MT_THUNDERBOLT,
        .native_type_id = 3,
        .damage_action = A_DC_Damage,
        .name = "Thunderbolt",
        .sprite_name = "SPRITES/BARR.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 15.0f / 32.0f,
        .max_hp = 1200,
        .attack = { .range = 6.0f, .damage = 180, .cooldown_ms = 1200 },
    },
    {
        .id = MT_CYBORG,
        .native_type_id = 4,
        .damage_action = A_DC_Damage,
        .name = "Cyborg",
        .sprite_name = "SPRITES/SARG.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 45.0f / 32.0f,
        .max_hp = 1200,
        .attack = { .range = 3.0f, .damage = 150, .cooldown_ms = 700 },
    },
    {
        .id = MT_SCOUT,
        .native_type_id = 5,
        .damage_action = A_DC_Damage,
        .name = "Scout",
        .sprite_name = "SPRITES/SCGM.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 47.0f / 32.0f,
        .max_hp = 600,
        .attack = { .range = 5.0f, .damage = 80, .cooldown_ms = 600 },
    },
    {
        .id = MT_EXCOPOD,
        .native_type_id = 16,
        .damage_action = A_DC_Damage,
        .name = "Exco Center",
        .sprite_name = "SPRITES/HUBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 4800,
    },
    {
        .id = MT_BRRKPOD,
        .native_type_id = 17,
        .damage_action = A_DC_Damage,
        .name = "Barracks",
        .sprite_name = "SPRITES/HUBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_ROBOPOD,
        .native_type_id = 18,
        .damage_action = A_DC_Damage,
        .name = "Robot Factory",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_ROBOPOD2,
        .native_type_id = 19,
        .damage_action = A_DC_Damage,
        .name = "Robot Factory II",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_SCNCPOD,
        .native_type_id = 20,
        .damage_action = A_DC_Damage,
        .name = "Science Pod",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_SCNCPOD2,
        .native_type_id = 21,
        .damage_action = A_DC_Damage,
        .name = "Science Pod II",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_RSCHPOD,
        .native_type_id = 22,
        .damage_action = A_DC_Damage,
        .name = "Research Pod",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_ALIEN_MINDHIVE,
        .native_type_id = 28,
        .damage_action = A_DC_Damage,
        .name = "Mind Hive",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 4800,
    },
    {
        .id = MT_ALIEN_WARHIVE,
        .native_type_id = 29,
        .damage_action = A_DC_Damage,
        .name = "Warrior Hive",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_ALIEN_BRDRHIVE,
        .native_type_id = 30,
        .damage_action = A_DC_Damage,
        .name = "Breeder Hive",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_ALIEN_BRDRHIVE2,
        .native_type_id = 31,
        .damage_action = A_DC_Damage,
        .name = "Breeder Hive II",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_ALIEN_MINDHIVE2,
        .native_type_id = 32,
        .damage_action = A_DC_Damage,
        .name = "Mind Hive II",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_ALIEN_MINDHIVE3,
        .native_type_id = 33,
        .damage_action = A_DC_Damage,
        .name = "Mind Hive III",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_ALIEN_RSCHIVE,
        .native_type_id = 34,
        .damage_action = A_DC_Damage,
        .name = "Research Hive",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_COMMS_DISH,
        .native_type_id = 86,
        .damage_action = A_DC_Damage,
        .name = "Communication Dish",
        .sprite_name = "SPRITES/DISH.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 1200,
    },
    {
        .id = MT_CITY_TOWER,
        .native_type_id = 81,
        .damage_action = A_DC_Damage,
        .name = "City Tower",
        .sprite_name = "SPRITES/TOWR.SPR",
        .traits = MF_RENDERABLE,
        .max_hp = 1600,
    },
    {
        .id = MT_ORTU,
        .native_type_id = 13,
        .damage_action = A_DC_Damage,
        .name = "Saucer Scout",
        .sprite_name = "SPRITES/ORTU.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 47.0f / 32.0f,
        .max_hp = 800,
        .attack = { .range = 2.0f, .damage = 100, .cooldown_ms = 500 },
    },
    {
        .id = MT_SLUG,
        .native_type_id = 14,
        .damage_action = A_DC_Damage,
        .name = "Alien Worker",
        .sprite_name = "SPRITES/SLUG.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_HARVESTER,
        .speed = 40.0f / 32.0f,
        .max_hp = 800,
    },
    {
        .id = MT_MOBILE_TOWER,
        .native_type_id = 41,
        .damage_action = A_DC_Damage,
        .name = "Mobile Tower",
        .sprite_name = "SPRITES/TURR.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
        .max_hp = 800,
        .attack = { .range = 4.0f, .damage = 100, .cooldown_ms = 500 },
    },
    {
        .id = MT_DROPSHIP,
        .native_type_id = 92,
        .damage_action = A_DC_Damage,
        .name = "Dropship",
        .sprite_name = "SPRITES/DROP.SPR",
        .traits = MF_RENDERABLE | MF_FLY,
        .speed = 1.0f,
        .max_hp = 800,
    },
    {
        .id = MT_DROP_LINK,
        .native_type_id = 89,
        .damage_action = A_DC_Damage,
        .name = "Dropship Link",
        .sprite_name = "SPRITES/CENT.SPR",
        .traits = MF_RENDERABLE,
        .max_hp = 800,
    },
    {
        .id = MT_ALIEN_COM,
        .native_type_id = 91,
        .damage_action = A_DC_Damage,
        .name = "Alien Com Tower",
        .sprite_name = "SPRITES/TONG.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 800,
    },
    {
        .id = MT_VISION_SIGHT,
        .native_type_id = 94,
        .damage_action = A_DC_Damage,
        .name = "Vision Sight",
        .sprite_name = "SPRITES/DOTT.SPR",
        .traits = MF_RENDERABLE,
        .max_hp = 300,
    },
};

const mobjtype_t *actor_type_by_id(uint16_t type_id) {
    for (int i = 0; i < (int)(sizeof(DARK_COLONY_ACTOR_TYPES) / sizeof(DARK_COLONY_ACTOR_TYPES[0])); ++i) {
        if (DARK_COLONY_ACTOR_TYPES[i].id == type_id) return &DARK_COLONY_ACTOR_TYPES[i];
    }
    return NULL;
}

static bool load_font(SDL_Renderer *renderer, const char *data_root, bitmapfont_t *font) {
    if (!renderer || !data_root || !font) return false;
    memset(font, 0, sizeof(*font));
    for (int i = 0; i < 128; ++i) font->glyph_index[i] = -1;
    char path[1024];
    M_PathJoin(path, sizeof(path), data_root, "INTRFACE/MFONTO7.SPR");
    uint32_t palette[256] = { 0 };
    if (!load_dark_colony_sprite(path, &font->sprite, palette)) return false;
    const int font_offset = 31;
    int max_w = 0, max_h = 0;
    for (int ch = font_offset; ch < 128; ++ch) {
        int frame = ch - font_offset;
        if (frame >= font->sprite.numlumps) break;
        font->glyph_index[ch] = frame;
        irect_t bounds = font->sprite.lumps ? font->sprite.cells[frame].bounds :
                             (irect_t){ 0, 0, 0, 0 };
        if (bounds.w > max_w) max_w = bounds.w;
        if (bounds.h > max_h) max_h = bounds.h;
    }
    font->draw_divisor = 1;
    font->glyph_size = (isize2_t){
        max_w > 0 ? max_w : 6,
        max_h > 0 ? max_h : font->sprite.frame_size.h,
    };
    font->line_h = font->glyph_size.h + 1;
    for (int ch = 0; ch < 128; ++ch) {
        int frame = font->glyph_index[ch];
        int advance = font->glyph_size.w;
        if (frame >= 0 && frame < font->sprite.numlumps && font->sprite.lumps) {
            irect_t bounds = font->sprite.cells[frame].bounds;
            if (bounds.w > 0) advance = bounds.w + 1;
        }
        font->glyph_width[ch] = (uint8_t)advance;
    }
    return true;
}

/* ── game identity (Doom-style externs) ─────────────────────────────────── */

const char *const g_game_id            = "dark-colony";
const char *const g_game_name          = "Dark Colony";
const char *const g_game_default_root  = "data/DCOLONY";
const char *const g_game_default_map   = "SCENARIO/HUMAN/HUMAN01.MAP";
const char *const g_game_default_sprite = "SPRITES/TROOPER1.SPR";
const int g_cell_w = 32;
const int g_cell_h = 32;
const uint16_t g_debug_enemy_type = MT_GREY;
static state_t runtime_states[NUMSTATES];
static gameinfo_t runtime_info;

static bool draw_selection(const selectiondrawcontext_t *ctx) {
    if (!ctx || !ctx->unit) return false;
    bool drawn = R_DrawSelectionMarkerSprite(ctx);
    uint16_t type = ctx->unit->native_type_id;
    if ((type < 69 || type > 76) || !ctx->game_info || !ctx->cache ||
        !ctx->game_info->selection_marker.image) {
        return drawn;
    }
    int frame = 30 + (type - 69) % 4;
    irect_t badge = {
        ctx->visible.x + (ctx->visible.w - 30) / 2,
        ctx->visible.y - 15,
        30,
        15,
    };
    return R_DrawSelectionMarkerFrame(ctx, frame, badge) || drawn;
}

const gameinfo_t *gameinfo = &runtime_info;
const mobjtype_t *const actor_types =
    (const mobjtype_t *)DARK_COLONY_ACTOR_TYPES;
const int num_actor_types =
    (int)(sizeof(DARK_COLONY_ACTOR_TYPES) / sizeof(DARK_COLONY_ACTOR_TYPES[0]));
const uidefinition_t *const gameui = NULL;

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

void G_InitGame(void) {
    static bool initialized;
    gameinfo = &runtime_info;
    if (initialized) return;

    memcpy(runtime_states, states, sizeof(runtime_states));
    runtime_info = game_info;
    runtime_info.states = runtime_states;
    runtime_info.draw_selection = draw_selection;
    initialized = true;
}

bool G_DoLoadLevel(const char *path, level_t *out) {
    if (!load_dark_colony_map(path, out)) return false;
    out->mission = load_mission(path);
    out->destroy_mission = destroy_mission;
    return true;
}

bool W_LoadAssets(SDL_Renderer *renderer, const char *root, const level_t *map,
                  const char *sprite, tileset_t *tileset, spritesheet_t *unit_sprite) {
    if (!load_render_tables(root, map->tileset_name)) {
        fprintf(stderr, "failed to load Dark Colony render tables for %s\n", map->tileset_name);
        return false;
    }
    char bts_path[1024];
    snprintf(bts_path, sizeof(bts_path), "%s/SCENARIO/%s.BTS", root, map->tileset_name);
    if (!load_dark_colony_tileset(renderer, bts_path, tileset)) return false;

    char sprite_path[1024];
    uint32_t sprite_palette[256] = { 0 };
    if (sprite[0] == '/') {
        snprintf(sprite_path, sizeof(sprite_path), "%s", sprite);
    } else {
        M_PathJoin(sprite_path, sizeof(sprite_path), root, sprite);
    }
    if (!load_dark_colony_sprite(sprite_path, unit_sprite, sprite_palette)) {
        fprintf(stderr, "failed to load %s\n", sprite_path);
        R_FreeTileset(tileset);
        return false;
    }
    return true;
}

int P_LoadThings(const char *path) {
    return load_dark_colony_initial_units(path);
}

bool R_InitSprites(SDL_Renderer *renderer, const char *root, const level_t *map,
                          mobj_t *const *mobjs, int count, spritecache_t *cache) {
    (void)renderer;
    return load_dark_colony_unit_sprites(root, map, mobjs, count, cache);
}

bool HU_LoadFont(SDL_Renderer *renderer, const char *root, bitmapfont_t *font) {
    return load_font(renderer, root, font);
}

void G_MissionTicker(level_t *map, mobj_t *const *mobjs, int *count,
                     hudtext_t *hud, float dt) {
    if (!map || !map->mission) return;
    update_mission(map, mobjs, count, hud, dt);
}

void *G_InitCustomUI(app_t *app, const char *data_root) {
    return DC_SB_Init(app, data_root);
}

bool G_CustomUIResponder(void *ui, const app_t *app, level_t *map,
                         mobj_t *const *units, int unit_count, const SDL_Event *event) {
    return DC_SB_Responder(ui, app, map, units, unit_count, event);
}

void G_CustomUITicker(void *ui) {
    DC_SB_Ticker(ui);
}

void G_CustomUIDrawer(void *ui, app_t *app, const level_t *map,
                      mobj_t *const *units, int unit_count,
                      const spritecache_t *sprites, const hudtext_t *hud) {
    DC_SB_Drawer(ui, app, map, units, unit_count, sprites, hud);
}

bool G_UpdateProduction(void *ui, level_t *map, mobj_t *const *units, int *unit_count,
                        float dt) {
    if (!ui) return false;
    return G_ModelUpdateProduction(map, units, unit_count, dt);
}

void G_ShutdownCustomUI(void *ui) {
    DC_SB_Shutdown(ui);
}

int G_WorldViewportWidth(const app_t *app) {
    return DC_SB_WorldViewportWidth(app);
}
