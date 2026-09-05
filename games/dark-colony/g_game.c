#define _DEFAULT_SOURCE
#include "game.h"
#include "dc_facing.h"
#include "engine.h"
#include "info.h"
#include "gamestat.h"
#include "dc_types.h"
#include "sb_bar.h"
#include "w_spr.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

bool load_dark_colony_map(const char *map_path, level_t *out);
int load_dark_colony_initial_units(const char *map_path, mobj_t *units, int max_units);
bool load_dark_colony_unit_sprites(SDL_Renderer *renderer, const char *data_root,
                                   const level_t *map, const mobj_t *units, int unit_count,
                                   spritecache_t *cache);
bool load_dark_colony_sprite(SDL_Renderer *renderer, const char *path, spritesheet_t *out,
                             uint32_t palette_out[256]);
extern bool load_dark_colony_tileset(SDL_Renderer *renderer, const char *path, tileset_t *out);

void *load_mission(const char *map_path);
void update_mission(void *ptr, level_t *map, mobj_t *units, int *unit_count,
                                effect_t *effects, int max_effects,
                                const gameinfo_t *game_info, hudtext_t *hud, float dt);
void destroy_mission(void *mission);

const actortype_t DARK_COLONY_ACTOR_TYPES[] = {
    {
        .id = MT_DC_TROOPER,
        .name = "Trooper",
        .sprite_name = "SPRITES/TRSC.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        /* GAMESTAT.TXT stores movement in pixels per 32 Hz tick.  The
         * simulation stores map cells per second: 25 / 32 is the authored
         * Trooper rate, not the old placeholder 5.0. */
        .speed = 25.0f / 32.0f,
        .max_hp = 800,
        .attack = { .range = 4.0f, .damage = 100, .cooldown_ms = 500, .anim_ms = 210 },
        .muzzle_flash_sprite = SPR_DC_MUZA,
        .muzzle_flash_ms = 120,
        .muzzle_flash_name = "SPRITES/BLAZ.SPR",
        .hit_effect_sprite = SPR_DC_BLOO,
        .hit_effect_name = "SPRITES/BLOO.SPR",
    },
    {
        .id = MT_DC_GREY,
        .name = "Grey",
        .sprite_name = "SPRITES/GRAY.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 25.0f / 32.0f,
        .max_hp = 800,
        .attack = { .range = 4.0f, .damage = 100, .cooldown_ms = 500, .anim_ms = 210 },
        .muzzle_flash_sprite = SPR_DC_MUZA,
        .muzzle_flash_ms = 120,
        .muzzle_flash_name = "SPRITES/BLAZ.SPR",
        .hit_effect_sprite = SPR_DC_BLOO,
        .hit_effect_name = "SPRITES/BLOO.SPR",
    },
    {
        .id = MT_DC_EXPLOITER,
        .name = "Exploiter",
        .sprite_name = "SPRITES/EXPL.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_HARVESTER,
        /* The gameplay tuning uses the documented heavy-harvester rate. */
        .speed = 3.5f,
        .max_hp = 800,
        .harvest = { .capacity = 0, .state_id = S_DC_EXPL_DEPLOY1 },
    },
    {
        .id = MT_DC_REAPER,
        .name = "Mech",
        .sprite_name = "SPRITES/REAP.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 30.0f / 32.0f,
        .max_hp = 800,
        .attack = { .range = 4.0f, .damage = 100, .cooldown_ms = 500, .anim_ms = 210 },
        .muzzle_flash_sprite = SPR_DC_BLAZ,
        .muzzle_flash_ms = 120,
        .muzzle_flash_name = "SPRITES/BLAZ.SPR",
        .hit_effect_sprite = SPR_DC_BLOO,
        .hit_effect_name = "SPRITES/BLOO.SPR",
        .death_effect_action = A_DC_ReaperDeath,
    },
    {
        .id = MT_DC_THUNDERBOLT,
        .name = "Thunderbolt",
        .sprite_name = "SPRITES/BARR.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 15.0f / 32.0f,
        .max_hp = 1200,
        .attack = { .range = 6.0f, .damage = 180, .cooldown_ms = 1200, .anim_ms = 400 },
        .muzzle_flash_sprite = SPR_DC_BLAZ,
        .muzzle_flash_ms = 150,
        .muzzle_flash_name = "SPRITES/BLAZ.SPR",
        .hit_effect_sprite = SPR_DC_BLOO,
        .hit_effect_name = "SPRITES/BLOO.SPR",
    },
    {
        .id = MT_DC_CYBORG,
        .name = "Cyborg",
        .sprite_name = "SPRITES/SARG.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 45.0f / 32.0f,
        .max_hp = 1200,
        .attack = { .range = 3.0f, .damage = 150, .cooldown_ms = 700, .anim_ms = 250 },
        .muzzle_flash_sprite = SPR_DC_MUZA,
        .muzzle_flash_ms = 120,
        .muzzle_flash_name = "SPRITES/BLAZ.SPR",
        .hit_effect_sprite = SPR_DC_BLOO,
        .hit_effect_name = "SPRITES/BLOO.SPR",
    },
    {
        .id = MT_DC_SCOUT,
        .name = "Scout",
        .sprite_name = "SPRITES/SCGM.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 47.0f / 32.0f,
        .max_hp = 600,
        .attack = { .range = 5.0f, .damage = 80, .cooldown_ms = 600, .anim_ms = 200 },
        .muzzle_flash_sprite = SPR_DC_MUZA,
        .muzzle_flash_ms = 100,
        .muzzle_flash_name = "SPRITES/BLAZ.SPR",
        .hit_effect_sprite = SPR_DC_BLOO,
        .hit_effect_name = "SPRITES/BLOO.SPR",
    },
    {
        .id = MT_DC_EXCOPOD,
        .name = "Exco Center",
        .sprite_name = "SPRITES/HUBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 4800,
    },
    {
        .id = MT_DC_BRRKPOD,
        .name = "Barracks",
        .sprite_name = "SPRITES/HUBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_ROBOPOD,
        .name = "Robot Factory",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_ROBOPOD2,
        .name = "Robot Factory II",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_DC_SCNCPOD,
        .name = "Science Pod",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_SCNCPOD2,
        .name = "Science Pod II",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_DC_RSCHPOD,
        .name = "Research Pod",
        .sprite_name = "SPRITES/SHORTCIT.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_DC_ALIEN_MINDHIVE,
        .name = "Mind Hive",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 4800,
    },
    {
        .id = MT_DC_ALIEN_WARHIVE,
        .name = "Warrior Hive",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_ALIEN_BRDRHIVE,
        .name = "Breeder Hive",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_ALIEN_BRDRHIVE2,
        .name = "Breeder Hive II",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_DC_ALIEN_MINDHIVE2,
        .name = "Mind Hive II",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_ALIEN_MINDHIVE3,
        .name = "Mind Hive III",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_DC_ALIEN_RSCHIVE,
        .name = "Research Hive",
        .sprite_name = "SPRITES/ALBU.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_DC_COMMS_DISH,
        .name = "Communication Dish",
        .sprite_name = "SPRITES/DISH.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 1200,
    },
    {
        .id = MT_DC_CITY_TOWER,
        .name = "City Tower",
        .sprite_name = "SPRITES/TOWR.SPR",
        .traits = MF_RENDERABLE,
        .max_hp = 1600,
    },
    {
        .id = MT_DC_ORTU,
        .name = "Saucer Scout",
        .sprite_name = "SPRITES/ORTU.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 47.0f / 32.0f,
        .max_hp = 800,
        .attack = { .range = 2.0f, .damage = 100, .cooldown_ms = 500, .anim_ms = 210 },
    },
    {
        .id = MT_DC_SLUG,
        .name = "Alien Worker",
        .sprite_name = "SPRITES/SLUG.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_HARVESTER,
        .speed = 40.0f / 32.0f,
        .max_hp = 800,
    },
    {
        .id = MT_DC_MOBILE_TOWER,
        .name = "Mobile Tower",
        .sprite_name = "SPRITES/TURR.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
        .max_hp = 800,
        .attack = { .range = 4.0f, .damage = 100, .cooldown_ms = 500, .anim_ms = 210 },
    },
    {
        .id = MT_DC_DROP_LINK,
        .name = "Dropship Link",
        .sprite_name = "SPRITES/CENT.SPR",
        .traits = MF_RENDERABLE,
        .max_hp = 800,
    },
    {
        .id = MT_DC_ALIEN_COM,
        .name = "Alien Com Tower",
        .sprite_name = "SPRITES/TONG.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 800,
    },
    {
        .id = MT_DC_VISION_SIGHT,
        .name = "Vision Sight",
        .sprite_name = "SPRITES/DOTT.SPR",
        .traits = MF_RENDERABLE,
        .max_hp = 300,
    },
};

const actortype_t *actor_type_by_id(uint16_t type_id) {
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
    if (!load_dark_colony_sprite(renderer, path, &font->sprite, palette)) return false;
    const int font_offset = 31;
    int max_w = 0, max_h = 0;
    for (int ch = font_offset; ch < 128; ++ch) {
        int frame = ch - font_offset;
        if (frame >= font->sprite.frame_count) break;
        font->glyph_index[ch] = frame;
        irect_t bounds = font->sprite.frame_bounds ? font->sprite.frame_bounds[frame] : font->sprite.frames[frame];
        if (bounds.w > max_w) max_w = bounds.w;
        if (bounds.h > max_h) max_h = bounds.h;
    }
    font->draw_divisor = 1;
    font->glyph_w = max_w > 0 ? max_w : 6;
    font->glyph_h = max_h > 0 ? max_h : font->sprite.frame_h;
    font->line_h = font->glyph_h + 1;
    for (int ch = 0; ch < 128; ++ch) {
        int frame = font->glyph_index[ch];
        int advance = font->glyph_w;
        if (frame >= 0 && frame < font->sprite.frame_count && font->sprite.frame_bounds) {
            irect_t bounds = font->sprite.frame_bounds[frame];
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
const uint16_t g_debug_enemy_type = MT_DC_GREY;
static state_t runtime_states[NUMSTATES];
static gameinfo_t runtime_info;

static bool draw_selection(const selectiondrawcontext_t *ctx) {
    if (!ctx || !ctx->unit) return false;
    bool drawn = R_DrawSelectionMarkerSprite(ctx);
    uint16_t type = ctx->unit->native_type_id;
    if ((type < 69 || type > 76) || !ctx->game_info || !ctx->cache ||
        ctx->game_info->selection_marker.sprite < 0) {
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

const gameinfo_t *const gameinfo = &runtime_info;
const actortype_t *const mobjinfo =
    (const actortype_t *)DARK_COLONY_ACTOR_TYPES;
const int num_mobjinfo =
    (int)(sizeof(DARK_COLONY_ACTOR_TYPES) / sizeof(DARK_COLONY_ACTOR_TYPES[0]));
const uidefinition_t *const gameui = NULL;

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

void G_InitGame(void) {
    static bool initialized;
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
    if (!load_dark_colony_sprite(renderer, sprite_path, unit_sprite, sprite_palette)) {
        fprintf(stderr, "failed to load %s\n", sprite_path);
        R_FreeTileset(tileset);
        return false;
    }
    return true;
}

int P_LoadThings(const char *path, mobj_t *mobjs, int max) {
    return load_dark_colony_initial_units(path, (mobj_t *)mobjs, max);
}

bool R_InitSprites(SDL_Renderer *renderer, const char *root, const level_t *map,
                          const mobj_t *mobjs, int count, spritecache_t *cache) {
    return load_dark_colony_unit_sprites(renderer, root, map, (const mobj_t *)mobjs, count, cache);
}

bool HU_LoadFont(SDL_Renderer *renderer, const char *root, bitmapfont_t *font) {
    return load_font(renderer, root, font);
}

void G_MissionTicker(level_t *map, mobj_t *mobjs, int *count,
                     effect_t *effects, int max_effects, hudtext_t *hud, float dt) {
    if (!map || !map->mission) return;
    update_mission(map->mission, map, (mobj_t *)mobjs, count,
                               effects, max_effects, gameinfo, hud, dt);
}

void *G_InitCustomUI(app_t *app, const char *data_root) {
    return SB_Init(app, data_root);
}

bool G_CustomUIResponder(void *ui, const app_t *app, level_t *map,
                         mobj_t *units, int unit_count, const SDL_Event *event) {
    return SB_Responder(ui, app, map, units, unit_count, event);
}

void G_CustomUITicker(void *ui) {
    SB_Ticker(ui);
}

void G_CustomUIDrawer(void *ui, app_t *app, const level_t *map,
                      const mobj_t *units, int unit_count,
                      const spritecache_t *sprites, const hudtext_t *hud) {
    SB_Drawer(ui, app, map, units, unit_count, sprites, hud);
}

bool G_UpdateProduction(void *ui, level_t *map, mobj_t *units, int *unit_count,
                        effect_t *effects, int max_effects, float dt) {
    return SB_UpdateProduction(ui, map, units, unit_count, effects, max_effects, dt);
}

void G_ShutdownCustomUI(void *ui) {
    SB_Shutdown(ui);
}

int G_WorldViewportWidth(const app_t *app) {
    return SB_WorldViewportWidth(app);
}
