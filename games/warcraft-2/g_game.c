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
        const mobjinfo_t *src = &mobjinfo[pud + 1];
        mobjtype_t *dst = &actor_storage[pud];
        memset(dst, 0, sizeof(*dst));
        int hp = src->spawnhealth > 0 ? src->spawnhealth : 1;
        int sight = src->w2.sight > 0 ? src->w2.sight : 1;
        if (sight > 10) sight = 10;
        uint32_t traits = MF_RENDERABLE;
        if (!(src->w2.flags & W2_CRITTER)) traits |= MF_SELECTABLE;
        if (src->w2.flags & W2_MOBILE) traits |= MF_MOBILE;
        if (src->w2.flags & W2_AIR) traits |= MF_FLY;
        if (src->w2.flags & W2_HARVEST) traits |= MF_HARVESTER;
        if (src->w2.store_mask) traits |= MF_RESOURCE_BASE;
        /* Fighters look for enemies; cowards (workers) hit only what they
         * are sent at. Towers stand and shoot like turrets. */
        if ((src->w2.attributes & W2_CAN_ATTACK) && src->damage > 0) {
            traits |= MF_ATTACK;
            if (src->w2.attributes & W2_COWARD) traits |= MF_NOAUTOTARGET;
        }
        uint8_t move = 0;
        if (!(src->w2.flags & W2_STRUCTURE)) {
            if (src->w2.flags & W2_AIR) move = 3;
            else if (src->w2.flags & W2_SEA) move = 2;
            else if (src->w2.flags & W2_MOBILE) move = 1;
        }
        *dst = (mobjtype_t){
            .id = (uint16_t)(pud + 1),
            .name = src->name ? src->name : "empty",
            .sprite_name = src->name,
            .traits = traits,
            .speed = (src->w2.flags & W2_MOBILE) ? src->w2.speed / W2_SPEED_DIVISOR : 0.0f,
            .max_hp = hp,
            .sight = { .day = sight, .night = sight, .airborne = (src->w2.flags & W2_AIR) != 0 },
            .sight_from_footprint = (src->w2.flags & W2_STRUCTURE) != 0,
            .attack = { .range = src->w2.attack_range, .damage = src->damage },
            .move_class = move,
            .footprint = src->w2.footprint,
        };
        for (int resource = 0; resource < 3; ++resource)
            dst->harvest.resources[resource].capacity = src->w2.gather[resource].capacity;
    }
}

void G_InitGame(void) {
    w2_build_info();
    fill_actors();
    w2_init_products();
    W2_SeedCombat(0x9E3779B9u);
}

bool G_DoLoadLevel(const char *path, level_t *out) {
    if (!w2_load_pud(path, out)) return false;
    if (!w2_init_mission(out)) { P_FreeLevel(out); return false; }
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
    (void)map;
    if (units && unit_count) W2_CheckVictory(units, *unit_count);
    return G_ProductionTicker(dt);
}
