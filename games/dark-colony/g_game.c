#define _DEFAULT_SOURCE
#include "game.h"
#include "engine.h"
#include "info.h"
#include "gamestat.h"
#include "dc_types.h"

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

void *dark_colony_load_mission(const char *map_path);
void dark_colony_update_mission(void *ptr, level_t *map, mobj_t *units, int *unit_count,
                                effect_t *effects, int max_effects,
                                const gameinfo_t *game_info, hudtext_t *hud, float dt);
void dark_colony_destroy_mission(void *mission);

const actortype_t DARK_COLONY_ACTOR_TYPES[] = {
    {
        .id = MT_DC_TROOPER,
        .name = "Trooper",
        .sprite_name = "SPRITES/TRSC.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 5.0f,
        .max_hp = 800,
        .attack_range = 4.0f,
        .attack_damage = 100,
        .attack_cooldown_ms = 500,
        .attack_anim_ms = 210,
        .muzzle_flash_name = "SPRITES/MUZA.SPR",
        .muzzle_flash_ms = 120,
        .hit_effect_name = "SPRITES/BLOO.SPR",
    },
    {
        .id = MT_DC_GREY,
        .name = "Grey",
        .sprite_name = "SPRITES/GRAY.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 5.0f,
        .max_hp = 800,
        .attack_range = 4.0f,
        .attack_damage = 100,
        .attack_cooldown_ms = 500,
        .attack_anim_ms = 210,
        .muzzle_flash_name = "SPRITES/MUZA.SPR",
        .muzzle_flash_ms = 120,
        .hit_effect_name = "SPRITES/BLOO.SPR",
    },
    {
        .id = MT_DC_EXPLOITER,
        .name = "Exploiter",
        .sprite_name = "SPRITES/EXPL.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_HARVESTER,
        .speed = 3.5f,
        .max_hp = 800,
        .harvest_state_id = S_DC_EXPL_DEPLOY1,
    },
    {
        .id = MT_DC_REAPER,
        .name = "Mech",
        .sprite_name = "SPRITES/REAP.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 6.0f,
        .max_hp = 800,
        .attack_range = 4.0f,
        .attack_damage = 100,
        .attack_cooldown_ms = 500,
        .attack_anim_ms = 210,
        .muzzle_flash_name = "SPRITES/BLAZ.SPR",
        .muzzle_flash_ms = 120,
        .hit_effect_name = "SPRITES/BLOO.SPR",
    },
    {
        .id = MT_DC_THUNDERBOLT,
        .name = "Thunderbolt",
        .sprite_name = "SPRITES/BARR.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 3.0f,
        .max_hp = 1200,
        .attack_range = 6.0f,
        .attack_damage = 180,
        .attack_cooldown_ms = 1200,
        .attack_anim_ms = 400,
        .muzzle_flash_name = "SPRITES/BLAZ.SPR",
        .muzzle_flash_ms = 150,
        .hit_effect_name = "SPRITES/BLOO.SPR",
    },
    {
        .id = MT_DC_CYBORG,
        .name = "Cyborg",
        .sprite_name = "SPRITES/SARG.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 7.0f,
        .max_hp = 1200,
        .attack_range = 3.0f,
        .attack_damage = 150,
        .attack_cooldown_ms = 700,
        .attack_anim_ms = 250,
        .muzzle_flash_name = "SPRITES/MUZA.SPR",
        .muzzle_flash_ms = 120,
        .hit_effect_name = "SPRITES/BLOO.SPR",
    },
    {
        .id = MT_DC_SCOUT,
        .name = "Scout",
        .sprite_name = "SPRITES/SCGM.SPR",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 9.4f,
        .max_hp = 600,
        .attack_range = 5.0f,
        .attack_damage = 80,
        .attack_cooldown_ms = 600,
        .attack_anim_ms = 200,
        .muzzle_flash_name = "SPRITES/MUZA.SPR",
        .muzzle_flash_ms = 100,
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
        .sprite_name = "SPRITES/ALIEN1.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 4800,
    },
    {
        .id = MT_DC_ALIEN_WARHIVE,
        .name = "Warrior Hive",
        .sprite_name = "SPRITES/ALIEN1.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_ALIEN_BRDRHIVE,
        .name = "Breeder Hive",
        .sprite_name = "SPRITES/ALIEN1.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_ALIEN_BRDRHIVE2,
        .name = "Breeder Hive II",
        .sprite_name = "SPRITES/ALIEN1.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_DC_ALIEN_MINDHIVE2,
        .name = "Mind Hive II",
        .sprite_name = "SPRITES/ALIEN1.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 2400,
    },
    {
        .id = MT_DC_ALIEN_MINDHIVE3,
        .name = "Mind Hive III",
        .sprite_name = "SPRITES/ALIEN1.SPR",
        .traits = MF_SELECTABLE | MF_RENDERABLE,
        .max_hp = 3600,
    },
    {
        .id = MT_DC_ALIEN_RSCHIVE,
        .name = "Research Hive",
        .sprite_name = "SPRITES/ALIEN1.SPR",
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
};

const actortype_t *dark_colony_actor_type_by_id(uint16_t type_id) {
    for (int i = 0; i < (int)(sizeof(DARK_COLONY_ACTOR_TYPES) / sizeof(DARK_COLONY_ACTOR_TYPES[0])); ++i) {
        if (DARK_COLONY_ACTOR_TYPES[i].id == type_id) return &DARK_COLONY_ACTOR_TYPES[i];
    }
    return NULL;
}

void dark_colony_apply_actor_type_defaults(mobj_t *unit, const actortype_t *type) {
    if (!unit || !type) return;
    unit->type_id = type->id;
    unit->traits = type->traits;
    if (unit->speed <= 0.0f) unit->speed = type->speed;
    if (unit->max_hp <= 0) unit->max_hp = type->max_hp;
    if (unit->hp <= 0) unit->hp = unit->max_hp;
    if (unit->attack_range <= 0.0f) unit->attack_range = type->attack_range;
    if (unit->attack_damage <= 0) unit->attack_damage = type->attack_damage;
    if (unit->attack_cooldown_ms <= 0) unit->attack_cooldown_ms = type->attack_cooldown_ms;
    if (unit->attack_anim_ms <= 0) unit->attack_anim_ms = type->attack_anim_ms;
    if (unit->death_anim_ms <= 0) unit->death_anim_ms = type->death_anim_ms;
    if (unit->harvest_state_id <= 0) unit->harvest_state_id = type->harvest_state_id;
    if (unit->muzzle_flash_ms <= 0) unit->muzzle_flash_ms = type->muzzle_flash_ms;
    if (unit->render_intensity == 0) unit->render_intensity = 16;
    if (unit->attack_target <= 0) unit->attack_target = -1;
    if (unit->harvest_target == 0) unit->harvest_target = -1;
    if (unit->sprite_name[0] == '\0' && type->sprite_name)
        snprintf(unit->sprite_name, sizeof(unit->sprite_name), "%s", type->sprite_name);
    if (unit->shadow_name[0] == '\0' && type->shadow_name)
        snprintf(unit->shadow_name, sizeof(unit->shadow_name), "%s", type->shadow_name);
    if (unit->muzzle_flash_name[0] == '\0' && type->muzzle_flash_name)
        snprintf(unit->muzzle_flash_name, sizeof(unit->muzzle_flash_name), "%s", type->muzzle_flash_name);
    if (unit->hit_effect_name[0] == '\0' && type->hit_effect_name)
        snprintf(unit->hit_effect_name, sizeof(unit->hit_effect_name), "%s", type->hit_effect_name);
}

static bool dark_colony_load_font(SDL_Renderer *renderer, const char *data_root, bitmapfont_t *font) {
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
const gameinfo_t *const gameinfo = &dark_colony_game_info;
const actortype_t *const mobjinfo =
    (const actortype_t *)DARK_COLONY_ACTOR_TYPES;
const int num_mobjinfo =
    (int)(sizeof(DARK_COLONY_ACTOR_TYPES) / sizeof(DARK_COLONY_ACTOR_TYPES[0]));
const uidefinition_t *const gameui = NULL;

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

bool G_DoLoadLevel(const char *path, level_t *out) {
    return load_dark_colony_map(path, out);
}

bool W_LoadAssets(SDL_Renderer *renderer, const char *root, const level_t *map,
                  const char *sprite, tileset_t *tileset, spritesheet_t *unit_sprite) {
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
    return dark_colony_load_font(renderer, root, font);
}

void *G_LoadMission(const char *path) {
    return dark_colony_load_mission(path);
}

void G_MissionTicker(void *mission, level_t *map, mobj_t *mobjs, int *count,
                     effect_t *effects, int max_effects, hudtext_t *hud, float dt) {
    dark_colony_update_mission(mission, map, (mobj_t *)mobjs, count,
                               effects, max_effects, gameinfo, hud, dt);
}

void G_FreeMission(void *mission) {
    dark_colony_destroy_mission(mission);
}
