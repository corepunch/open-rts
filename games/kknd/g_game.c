#include "game.h"
#include "kknd.h"

static const actortype_t ACTOR_TYPES[] = {
    {
        .id = 1,
        .name = "Survivor Infantry",
        .sprite_name = "LEVELS/640/SPRITES.LVL|Infantry.mobd",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 4.0f,
        .max_hp = 100,
        .attack = { .range = 4.0f, .damage = 10, .cooldown_ms = 650, .anim_ms = 450 },
    },
};

static const gameinfo_t GAME_INFO = {
    .selection_marker = { .style = SELECTION_STYLE_CIRCLE, .sprite = -1 },
};

/* OpenKKnD uses a 48-pixel button rail on the right and a centered 180x28
   status box.  Keep those native proportions while command icons are loaded
   independently from the original game's MOBD assets. */
static const uidefinition_t UI = {
    .logical_width = 640,
    .logical_height = 480,
    .world_viewport = { 0, 0, 592, 480 },
    .resources = {
        [0] = { .text = { 400, 3 }, .color = { 255, 255, 255, 255 } },
    },
    .resource_count = 1,
    .status_panel = {
        .rect = { 230, 0, 180, 28 },
        .fill = { 0, 0, 0, 255 },
        .border = { 255, 255, 255, 255 },
    },
    .status_elapsed_time = true,
    .sidebar_panel = {
        .rect = { 592, 0, 48, 480 },
        .fill = { 0, 0, 0, 255 },
        .border = { 104, 104, 96, 255 },
    },
    .sidebar_cell_size = 48,
};

/* ── game identity (Doom-style externs) ─────────────────────────────────── */

const char *const g_game_id            = "kknd";
const char *const g_game_name          = "KKnD";
const char *const g_game_default_root  = "data/KKND";
const char *const g_game_default_map   = "LEVELS/640/SURV_01.LVL";
const char *const g_game_default_sprite = "LEVELS/640/SPRITES.LVL|Infantry.mobd";
const int g_cell_w = 32;
const int g_cell_h = 32;
const uint16_t g_debug_enemy_type = 1;
const gameinfo_t *const gameinfo = &GAME_INFO;
const actortype_t *const mobjinfo = ACTOR_TYPES;
const int num_mobjinfo =
    (int)(sizeof(ACTOR_TYPES) / sizeof(ACTOR_TYPES[0]));
const uidefinition_t *const gameui = &UI;

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

void G_InitGame(void) {
}

bool G_DoLoadLevel(const char *path, level_t *out) {
    return load_kknd_map(path, out);
}

bool W_LoadAssets(SDL_Renderer *renderer, const char *root, const level_t *map,
                  const char *sprite, tileset_t *tileset, spritesheet_t *unit_sprite) {
    return load_assets(renderer, root, map, sprite, tileset, unit_sprite);
}

int P_LoadThings(const char *path, mobj_t *mobjs, int max) {
    (void)path; (void)mobjs; (void)max;
    return 0;
}

bool R_InitSprites(SDL_Renderer *renderer, const char *root, const level_t *map,
                          const mobj_t *mobjs, int count, spritecache_t *cache) {
    (void)renderer; (void)root; (void)map; (void)mobjs; (void)count; (void)cache;
    return true;
}

bool HU_LoadFont(SDL_Renderer *renderer, const char *root, bitmapfont_t *font) {
    (void)renderer; (void)root; (void)font;
    return false;
}

void *G_LoadMission(const char *path) { (void)path; return NULL; }
void  G_MissionTicker(void *m, level_t *map, mobj_t *mobjs, int *count,
                      effect_t *effects, int max_effects, hudtext_t *hud, float dt) {
    (void)m; (void)map; (void)mobjs; (void)count;
    (void)effects; (void)max_effects; (void)hud; (void)dt;
}
void  G_FreeMission(void *m) { (void)m; }

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
