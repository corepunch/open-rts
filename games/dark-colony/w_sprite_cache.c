#include "w_spr.h"
#include "w_sprite_private.h"
#include "info.h"
#include <stdio.h>
#include <string.h>

static bool sprite_cache_load_dark_colony(spritecache_t *cache, SDL_Renderer *renderer,
                                          const char *data_root, const char *name) {
    if (!name || name[0] == '\0') return true;
    if (R_CacheFind(cache, name)) return true;
    if (cache->count >= MAX_DECORATION_SPRITES) {
        fprintf(stderr, "too many Dark Colony sprites; skipped %s\n", name);
        return false;
    }
    char sprite_path[1024];
    if (name[0] == '/') {
        snprintf(sprite_path, sizeof(sprite_path), "%s", name);
    } else if (strchr(name, '/') != NULL) {
        M_PathJoin(sprite_path, sizeof(sprite_path), data_root, name);
    } else {
        static const char *const sprite_directories[] = {
            "SPRITES", "CURSOR", "ENCYCLO", "INTRFACE",
        };
        bool found = false;
        char candidate[1024];
        char filename[64];
        snprintf(filename, sizeof(filename), "%s.FIN", name);
        M_PathJoin(candidate, sizeof(candidate), data_root, "ANIMATE");
        M_PathJoin(sprite_path, sizeof(sprite_path), candidate, filename);
        if (DC_AssetExists(sprite_path)) found = true;
        for (size_t i = 0; i < sizeof(sprite_directories) / sizeof(sprite_directories[0]); ++i) {
            if (found) break;
            snprintf(filename, sizeof(filename), "%s.SPR", name);
            M_PathJoin(candidate, sizeof(candidate), data_root, sprite_directories[i]);
            M_PathJoin(sprite_path, sizeof(sprite_path), candidate, filename);
            if (DC_AssetExists(sprite_path)) {
                found = true;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "failed to resolve Dark Colony sprite %s\n", name);
            return false;
        }
    }
    cachedsprite_t *entry = &cache->entries[cache->count];
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    uint32_t palette[256] = { 0 };
    AnimationFile animation = {0};
    if (!DC_LoadSpriteWithAnimation(renderer, sprite_path, &entry->sprite, palette,
                                          &animation)) {
        fprintf(stderr, "failed to load %s\n", sprite_path);
        memset(entry, 0, sizeof(*entry));
        return false;
    }
    cache->count++;
    if (animation.command_count > 0) {
        for (int i = 0; i < animation.dependency_count; ++i) {
            char dependency_name[64];
            if (!DC_DependencySpriteName(dependency_name, sizeof(dependency_name),
                                                    animation.dependencies[i].name)) {
                continue;
            }
            if (R_CacheFind(cache, dependency_name)) continue;
            char dependency_path[1024];
            M_PathJoin(dependency_path, sizeof(dependency_path), data_root, dependency_name);
            if (!DC_AssetExists(dependency_path)) continue;
            if (!sprite_cache_load_dark_colony(cache, renderer, data_root, dependency_name)) {
                DC_FreeAnimation(&animation);
                return false;
            }
        }
    }
    DC_FreeAnimation(&animation);
    return true;
}

bool load_dark_colony_unit_sprites(SDL_Renderer *renderer, const char *data_root,
                                   const level_t *map, const mobj_t *units, int unit_count,
                                   spritecache_t *cache) {
    bool ok = true;
    static const char *const ui_sprites[] = {
        "INTRFACE/DCSS.SPR",
        "INTRFACE/DCUT.SPR",
        "INTRFACE/MAINBUT.SPR",
        "INTRFACE/SHUMANE.SPR",
        "SPRITES/DROP.SPR",
        "SPRITES/BEAC.SPR",
        "SPRITES/MUZA.SPR",
        "SPRITES/BLOO.SPR",
    };
    for (int i = 0; i < NUMSTATES; ++i) {
        int sprite = states[i].sprite;
        if (sprite >= 0 && sprite < NUMSPRITES &&
            !sprite_cache_load_dark_colony(cache, renderer, data_root, sprnames[sprite])) {
            ok = false;
        }
    }
    for (size_t i = 0; i < sizeof(ui_sprites) / sizeof(ui_sprites[0]); ++i) {
        if (!sprite_cache_load_dark_colony(cache, renderer, data_root, ui_sprites[i]))
            ok = false;
    }
    int selection_sprite = game_info.selection_marker.sprite;
    if (selection_sprite >= 0 && selection_sprite < NUMSPRITES &&
        !sprite_cache_load_dark_colony(cache, renderer, data_root, sprnames[selection_sprite])) {
        ok = false;
    }
    if (map) {
        for (int i = 0; i < map->decoration_count; ++i) {
            if (!sprite_cache_load_dark_colony(cache, renderer, data_root, map->decorations[i].sprite_name))
                ok = false;
            if (!sprite_cache_load_dark_colony(cache, renderer, data_root, map->decorations[i].sprite2_name))
                ok = false;
            if (!sprite_cache_load_dark_colony(cache, renderer, data_root, map->decorations[i].shadow_name))
                ok = false;
        }
    }
    for (int i = 0; i < unit_count; ++i) {
        if (!sprite_cache_load_dark_colony(cache, renderer, data_root,
                           units[i].core.sprite_name))
            ok = false;
        const char *shadow_name = units[i].info ? units[i].info->shadow_name : NULL;
        if (!sprite_cache_load_dark_colony(cache, renderer, data_root, shadow_name))
            ok = false;
        const mobjtype_t *info = units[i].info;
        if (info && !sprite_cache_load_dark_colony(
                        cache, renderer, data_root, info->hit_effect_name))
            ok = false;
    }
    return ok;
}

