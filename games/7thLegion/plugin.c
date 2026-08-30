#include "game.h"
#include "sl_types.h"

bool sl_load_map(const char *map_path, GameMap *out);
bool sl_load_assets(SDL_Renderer *renderer, const char *data_root, const GameMap *map,
                    const char *sprite_name, Tileset *tileset, SpriteSheet *unit_sprite);
int  sl_load_initial_units(const char *map_path, Unit *units, int max_units);
bool sl_load_runtime_sprites(SDL_Renderer *renderer, const char *data_root,
                             const GameMap *map, const Unit *units, int unit_count,
                             SpriteCache *cache);

/* Unit types defined in 7th Legion based on sprites present in data/7LEGION/GFX/ */
static const ActorType SL_ACTOR_TYPES[] = {
    {
        .id          = 1,
        .name        = "Trooper",
        .sprite_name = "GFX/LTROOP.BIM",
        .traits      = T_SELECTABLE | T_MOBILE |
                       T_RENDERABLE | T_ATTACK,
        .speed       = 4.0f,
        .max_hp      = 100,
        .attack_range    = 5.0f,
        .attack_damage   = 15,
        .attack_cooldown_ms = 800,
        .attack_anim_ms  = 400,
        .death_anim_ms   = 600,
    },
    {
        .id          = 2,
        .name        = "Slave",
        .sprite_name = "GFX/SLAVEN1.BIM",
        .traits      = T_SELECTABLE | T_MOBILE |
                       T_RENDERABLE | T_HARVESTER,
        .speed       = 3.5f,
        .max_hp      = 60,
    },
    {
        .id          = 3,
        .name        = "Spider Mech",
        .sprite_name = "GFX/SPIDER.BIM",
        .traits      = T_SELECTABLE | T_MOBILE |
                       T_RENDERABLE | T_ATTACK,
        .speed       = 3.0f,
        .max_hp      = 300,
        .attack_range    = 7.0f,
        .attack_damage   = 35,
        .attack_cooldown_ms = 1200,
        .attack_anim_ms  = 500,
        .death_anim_ms   = 800,
    },
    {
        .id          = 4,
        .name        = "Tank",
        .sprite_name = "GFX/TANKBASE.BIM",
        .traits      = T_SELECTABLE | T_MOBILE |
                       T_RENDERABLE | T_ATTACK,
        .speed       = 4.5f,
        .max_hp      = 500,
        .attack_range    = 8.0f,
        .attack_damage   = 50,
        .attack_cooldown_ms = 1500,
        .attack_anim_ms  = 600,
        .death_anim_ms   = 1000,
    },
    {
        .id          = 5,
        .name        = "Rock Mech",
        .sprite_name = "GFX/ROCKMECH.BIM",
        .traits      = T_SELECTABLE | T_MOBILE |
                       T_RENDERABLE | T_ATTACK,
        .speed       = 2.5f,
        .max_hp      = 800,
        .attack_range    = 6.0f,
        .attack_damage   = 70,
        .attack_cooldown_ms = 2000,
        .attack_anim_ms  = 700,
        .death_anim_ms   = 1200,
    },
    {
        .id          = 6,
        .name        = "Truck",
        .sprite_name = "GFX/TRUCK.BIM",
        .traits      = T_SELECTABLE | T_MOBILE |
                       T_RENDERABLE | T_HARVESTER,
        .speed       = 5.0f,
        .max_hp      = 200,
    },
    {
        .id          = 7,
        .name        = "Mobile Base",
        .sprite_name = "GFX/MOBBASE.BIM",
        .traits      = T_SELECTABLE | T_MOBILE |
                       T_RENDERABLE,
        .speed       = 2.5f,
        .max_hp      = 1000,
    },
};

static const GameInfo SL_GAME_INFO = {
    .direction_mode   = RTS_DIRECTION_DARK_REIGN_8,
    .selection_marker = { .style = SELECTION_STYLE_CIRCLE, .sprite = -1 },
};

/* ── game identity (Doom-style externs) ─────────────────────────────────── */

const char *const g_game_id            = "7legion";
const char *const g_game_name          = "7th Legion";
const char *const g_game_default_root  = "data/7LEGION";
const char *const g_game_default_map   = "DATA/MAPT.000";
const char *const g_game_default_sprite = "GFX/LTROOP.BIM";
const int g_cell_w = SL_TILE_W;
const int g_cell_h = SL_TILE_H;
const uint16_t g_debug_enemy_type = 1;
const GameInfo *const gameinfo = &SL_GAME_INFO;
const MobjType *const mobjTypes =
    (const MobjType *)SL_ACTOR_TYPES;
const int mobjTypeCount =
    (int)(sizeof(SL_ACTOR_TYPES) / sizeof(SL_ACTOR_TYPES[0]));
const GameUiDefinition *const gameui = NULL;

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

bool G_LoadMap(const char *path, GameMap *out) {
    return sl_load_map(path, out);
}

bool R_LoadAssets(SDL_Renderer *renderer, const char *root, const GameMap *map,
                  const char *sprite, Tileset *tileset, SpriteSheet *unit_sprite) {
    return sl_load_assets(renderer, root, map, sprite, tileset, unit_sprite);
}

int G_SpawnThings(const char *path, Mobj *mobjs, int max) {
    return sl_load_initial_units(path, (Unit *)mobjs, max);
}

bool R_LoadRuntimeSprites(SDL_Renderer *renderer, const char *root, const GameMap *map,
                          const Mobj *mobjs, int count, SpriteCache *cache) {
    return sl_load_runtime_sprites(renderer, root, map, (const Unit *)mobjs, count, cache);
}

bool R_LoadFont(SDL_Renderer *renderer, const char *root, BitmapFont *font) {
    (void)renderer; (void)root; (void)font;
    return false;
}

void *G_LoadMission(const char *path) { (void)path; return NULL; }
void  G_UpdateMission(void *m, GameMap *map, Mobj *mobjs, int *count,
                      VisualEffect *effects, int max_effects, HudText *hud, float dt) {
    (void)m; (void)map; (void)mobjs; (void)count;
    (void)effects; (void)max_effects; (void)hud; (void)dt;
}
void  G_FreeMission(void *m) { (void)m; }
