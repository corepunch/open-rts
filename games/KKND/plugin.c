#include "game.h"
#include "kknd.h"

static const MobjType KKND_ACTOR_TYPES[] = {
    {
        .id = 1,
        .name = "Survivor Infantry",
        .sprite_name = "LEVELS/640/SPRITES.LVL|Infantry.mobd",
        .traits = T_SELECTABLE | T_MOBILE |
                  T_RENDERABLE | T_ATTACK,
        .speed = 4.0f,
        .max_hp = 100,
        .attack_range = 4.0f,
        .attack_damage = 10,
        .attack_cooldown_ms = 650,
        .attack_anim_ms = 450,
    },
};

static const GameInfo KKND_GAME_INFO = {
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
const GameInfo *const gameinfo = &KKND_GAME_INFO;
const MobjType *const mobjTypes = KKND_ACTOR_TYPES;
const int mobjTypeCount =
    (int)(sizeof(KKND_ACTOR_TYPES) / sizeof(KKND_ACTOR_TYPES[0]));
const GameUiDefinition *const gameui = NULL;

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

bool G_LoadMap(const char *path, GameMap *out) {
    return load_kknd_map(path, out);
}

bool R_LoadAssets(SDL_Renderer *renderer, const char *root, const GameMap *map,
                  const char *sprite, Tileset *tileset, SpriteSheet *unit_sprite) {
    return kknd_load_assets(renderer, root, map, sprite, tileset, unit_sprite);
}

int G_SpawnThings(const char *path, Mobj *mobjs, int max) {
    (void)path; (void)mobjs; (void)max;
    return 0;
}

bool R_LoadRuntimeSprites(SDL_Renderer *renderer, const char *root, const GameMap *map,
                          const Mobj *mobjs, int count, SpriteCache *cache) {
    (void)renderer; (void)root; (void)map; (void)mobjs; (void)count; (void)cache;
    return true;
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
