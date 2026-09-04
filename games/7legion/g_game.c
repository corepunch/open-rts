#include "game.h"
#include "sl_types.h"

bool sl_load_map(const char *map_path, level_t *out);
bool sl_load_assets(SDL_Renderer *renderer, const char *data_root, const level_t *map,
                    const char *sprite_name, tileset_t *tileset, spritesheet_t *unit_sprite);
int  sl_load_initial_units(const char *map_path, mobj_t *units, int max_units);
bool sl_load_runtime_sprites(SDL_Renderer *renderer, const char *data_root,
                             const level_t *map, const mobj_t *units, int unit_count,
                             spritecache_t *cache);

/* mobj_t types defined in 7th Legion based on sprites present in data/7LEGION/GFX/ */
static const actortype_t ACTOR_TYPES[] = {
    {
        .id          = 1,
        .name        = "Trooper",
        .sprite_name = "GFX/LTROOP.BIM",
        .traits      = MF_SELECTABLE | MF_MOBILE |
                       MF_RENDERABLE | MF_ATTACK,
        .speed       = 4.0f,
        .max_hp      = 100,
        .attack = { .range = 5.0f, .damage = 15, .cooldown_ms = 800, .anim_ms = 400 },
        .death = { .anim_ms = 600 },
    },
    {
        .id          = 2,
        .name        = "Slave",
        .sprite_name = "GFX/SLAVEN1.BIM",
        .traits      = MF_SELECTABLE | MF_MOBILE |
                       MF_RENDERABLE | MF_HARVESTER,
        .speed       = 3.5f,
        .max_hp      = 60,
    },
    {
        .id          = 3,
        .name        = "Spider Mech",
        .sprite_name = "GFX/SPIDER.BIM",
        .traits      = MF_SELECTABLE | MF_MOBILE |
                       MF_RENDERABLE | MF_ATTACK,
        .speed       = 3.0f,
        .max_hp      = 300,
        .attack = { .range = 7.0f, .damage = 35, .cooldown_ms = 1200, .anim_ms = 500 },
        .death = { .anim_ms = 800 },
    },
    {
        .id          = 4,
        .name        = "Tank",
        .sprite_name = "GFX/TANKBASE.BIM",
        .traits      = MF_SELECTABLE | MF_MOBILE |
                       MF_RENDERABLE | MF_ATTACK,
        .speed       = 4.5f,
        .max_hp      = 500,
        .attack = { .range = 8.0f, .damage = 50, .cooldown_ms = 1500, .anim_ms = 600 },
        .death = { .anim_ms = 1000 },
    },
    {
        .id          = 5,
        .name        = "Rock Mech",
        .sprite_name = "GFX/ROCKMECH.BIM",
        .traits      = MF_SELECTABLE | MF_MOBILE |
                       MF_RENDERABLE | MF_ATTACK,
        .speed       = 2.5f,
        .max_hp      = 800,
        .attack = { .range = 6.0f, .damage = 70, .cooldown_ms = 2000, .anim_ms = 700 },
        .death = { .anim_ms = 1200 },
    },
    {
        .id          = 6,
        .name        = "Truck",
        .sprite_name = "GFX/TRUCK.BIM",
        .traits      = MF_SELECTABLE | MF_MOBILE |
                       MF_RENDERABLE | MF_HARVESTER,
        .speed       = 5.0f,
        .max_hp      = 200,
    },
    {
        .id          = 7,
        .name        = "Mobile Base",
        .sprite_name = "GFX/MOBBASE.BIM",
        .traits      = MF_SELECTABLE | MF_MOBILE |
                       MF_RENDERABLE,
        .speed       = 2.5f,
        .max_hp      = 1000,
    },
};

static const gameinfo_t GAME_INFO = {
    .selection_marker = { .style = SELECTION_STYLE_CIRCLE, .sprite = -1 },
};

static const uidefinition_t UI = {
    .logical_width = 640,
    .logical_height = 480,
    .world_viewport = { 0, 28, 640, 452 },
    .resources = {
        [0] = { .text = { 630, 3 }, .color = { 230, 215, 80, 255 },
                .right_aligned = true },
    },
    .resource_count = 1,
    .status_panel = {
        .rect = { 480, 0, 160, 28 },
        .fill = { 8, 11, 15, 255 },
        .border = { 126, 132, 126, 255 },
    },
};

/* ── game identity (Doom-style externs) ─────────────────────────────────── */

const char *const g_game_id            = "7legion";
const char *const g_game_name          = "7th Legion";
const char *const g_game_default_root  = "data/7LEGION";
const char *const g_game_default_map   = "DATA/MAPT.000";
const char *const g_game_default_sprite = "GFX/LTROOP.BIM";
const int g_cell_w = TILE_W;
const int g_cell_h = TILE_H;
const uint16_t g_debug_enemy_type = 1;
const gameinfo_t *const gameinfo = &GAME_INFO;
const actortype_t *const mobjinfo =
    (const actortype_t *)ACTOR_TYPES;
const int num_mobjinfo =
    (int)(sizeof(ACTOR_TYPES) / sizeof(ACTOR_TYPES[0]));
const uidefinition_t *const gameui = &UI;

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

void G_InitGame(void) {
}

bool G_DoLoadLevel(const char *path, level_t *out) {
    return sl_load_map(path, out);
}

bool W_LoadAssets(SDL_Renderer *renderer, const char *root, const level_t *map,
                  const char *sprite, tileset_t *tileset, spritesheet_t *unit_sprite) {
    return sl_load_assets(renderer, root, map, sprite, tileset, unit_sprite);
}

int P_LoadThings(const char *path, mobj_t *mobjs, int max) {
    return sl_load_initial_units(path, (mobj_t *)mobjs, max);
}

bool R_InitSprites(SDL_Renderer *renderer, const char *root, const level_t *map,
                          const mobj_t *mobjs, int count, spritecache_t *cache) {
    return sl_load_runtime_sprites(renderer, root, map, (const mobj_t *)mobjs, count, cache);
}

bool HU_LoadFont(SDL_Renderer *renderer, const char *root, bitmapfont_t *font) {
    (void)renderer; (void)root; (void)font;
    return false;
}

void  G_MissionTicker(level_t *map, mobj_t *mobjs, int *count,
                      effect_t *effects, int max_effects, hudtext_t *hud, float dt) {
    (void)map; (void)mobjs; (void)count;
    (void)effects; (void)max_effects; (void)hud; (void)dt;
}

void *G_InitCustomUI(app_t *app, const char *data_root) {
    (void)app; (void)data_root;
    return NULL;
}

bool G_CustomUIResponder(void *ui, const app_t *app, level_t *map,
                         mobj_t *units, int unit_count, const SDL_Event *event) {
    (void)ui; (void)app; (void)map; (void)units; (void)unit_count; (void)event;
    return false;
}

void G_CustomUITicker(void *ui) {
    (void)ui;
}

void G_CustomUIDrawer(void *ui, app_t *app, const level_t *map,
                      const mobj_t *units, int unit_count,
                      const spritecache_t *sprites, const hudtext_t *hud) {
    (void)ui; (void)app; (void)map; (void)units; (void)unit_count;
    (void)sprites; (void)hud;
}

bool G_UpdateProduction(void *ui, level_t *map, mobj_t *units, int *unit_count,
                        effect_t *effects, int max_effects, float dt) {
    (void)ui; (void)map; (void)units; (void)unit_count;
    (void)effects; (void)max_effects; (void)dt;
    return false;
}

void G_ShutdownCustomUI(void *ui) {
    (void)ui;
}

int G_WorldViewportWidth(const app_t *app) {
    if (!app) return 0;
    if (gameui && gameui->world_viewport.w > 0 && gameui->logical_width > 0)
        return gameui->world_viewport.w * app->win.w / gameui->logical_width;
    return app->win.w > 0 ? app->win.w : 1;
}
