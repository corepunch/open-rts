#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

static mobjtype_t actor_storage[W2_TYPE_COUNT];

const char *const g_game_id = "warcraft-2";
const char *const g_game_name = "Warcraft II";
const char *const g_game_default_root = "data/WAR2";
const char *const g_game_default_map = "ALAMO.PUD";
const char *const g_game_default_sprite = "footman";
const int g_cell_w = TILE_W;
const int g_cell_h = TILE_H;
const uint16_t g_debug_enemy_type = 2;
const gameinfo_t *gameinfo = &game_info;
const mobjtype_t *const actor_types = actor_storage;
const int num_actor_types = W2_TYPE_COUNT;

static void fill_actors(void) {
    for (int pud = 0; pud < W2_TYPE_COUNT; ++pud) {
        const w2_unit_t *src = &w2_units[pud];
        mobjtype_t *dst = &actor_storage[pud];
        memset(dst, 0, sizeof(*dst));
        int hp = src->hp > 0 ? src->hp : 1;
        int sight = src->sight > 0 ? src->sight : 1;
        if (sight > 10) sight = 10;
        uint32_t traits = MF_RENDERABLE;
        if (!(src->flags & W2_CRITTER)) traits |= MF_SELECTABLE;
        if (src->flags & W2_MOBILE) traits |= MF_MOBILE;
        if (src->flags & W2_AIR) traits |= MF_FLY;
        if (src->flags & W2_HARVEST) traits |= MF_HARVESTER;
        if ((src->flags & W2_HALL) || pud == 76 || pud == 77) traits |= MF_RESOURCE_BASE;
        uint8_t move = 0;
        if (!(src->flags & W2_STRUCTURE)) {
            if (src->flags & W2_AIR) move = 3;
            else if (src->flags & W2_SEA) move = 2;
            else if (src->flags & W2_MOBILE) move = 1;
        }
        *dst = (mobjtype_t){
            .id = (uint16_t)(pud + 1),
            .name = src->name ? src->name : "empty",
            .sprite_name = src->name,
            .traits = traits,
            .speed = (src->flags & W2_MOBILE) ? src->speed / W2_SPEED_DIVISOR : 0.0f,
            .max_hp = hp,
            .sight = { .day = sight, .night = sight, .airborne = (src->flags & W2_AIR) != 0 },
            .attack = { .range = src->range, .damage = src->damage },
            .move_class = move,
        };
        if (src->flags & W2_HARVEST) {
            dst->harvest.resources[0].capacity = W2_HARVEST_GOLD;
            dst->harvest.resources[1].capacity = 100;
        }
    }
}

void G_InitGame(void) {
    w2_build_info();
    fill_actors();
}

bool G_DoLoadLevel(const char *path, level_t *out) {
    if (!w2_load_pud(path, out)) return false;
    const w2_pud_t *pud = out->native_data;
    /* Single player watches the OWNR 5 slot. Sight and allegiance both read
     * consoleplayer after this returns. */
    if (!netgame && pud && pud->view_player >= 0 && pud->view_player < 8)
        consoleplayer = pud->view_player;
    return true;
}

bool W_LoadAssets(const char *root, const level_t *map, const char *sprite,
                  tileset_t *tileset, spritesheet_t *unit_sprite) {
    return w2_load_assets(root, map, sprite, tileset, unit_sprite);
}

int P_LoadThings(const char *path) {
    (void)path;
    return w2_spawn_units();
}

bool R_InitSprites(const char *root, const level_t *map,
                   mobj_t *const *mobjs, int count, spritecache_t *cache) {
    return w2_load_runtime_sprites(root, map, mobjs, count, cache);
}

void G_MissionTicker(level_t *map, mobj_t *const *mobjs, int *count,
                     hudtext_t *hud_text, float dt) {
    (void)map; (void)mobjs; (void)count; (void)hud_text; (void)dt;
}

bool G_UpdateProduction(level_t *map, mobj_t *const *units, int *unit_count, float dt) {
    (void)map; (void)units; (void)unit_count;
    return G_ProductionTicker(dt);
}
