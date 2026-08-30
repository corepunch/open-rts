#include "game.h"
#include "kknd.h"

static const actortype_t KKND_ACTOR_TYPES[] = {
    {
        .id = 1,
        .name = "Survivor Infantry",
        .sprite_name = "LEVELS/640/SPRITES.LVL|Infantry.mobd",
        .traits = MF_SELECTABLE | MF_MOBILE |
                  MF_RENDERABLE | MF_ATTACK,
        .speed = 4.0f,
        .max_hp = 100,
        .attack_range = 4.0f,
        .attack_damage = 10,
        .attack_cooldown_ms = 650,
        .attack_anim_ms = 450,
    },
};

static const gameinfo_t KKND_GAME_INFO = {
    .direction_mode   = RTS_DIRECTION_DARK_REIGN_8,
    .selection_marker = { .style = SELECTION_STYLE_CIRCLE, .sprite = -1 },
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
const gameinfo_t *const gameinfo = &KKND_GAME_INFO;
const actortype_t *const mobjinfo = KKND_ACTOR_TYPES;
const int num_mobjinfo =
    (int)(sizeof(KKND_ACTOR_TYPES) / sizeof(KKND_ACTOR_TYPES[0]));
const uidefinition_t *const gameui = NULL;

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

bool G_DoLoadLevel(const char *path, level_t *out) {
    return load_kknd_map(path, out);
}

bool W_LoadAssets(SDL_Renderer *renderer, const char *root, const level_t *map,
                  const char *sprite, tileset_t *tileset, spritesheet_t *unit_sprite) {
    return kknd_load_assets(renderer, root, map, sprite, tileset, unit_sprite);
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
