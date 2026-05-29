#define _DEFAULT_SOURCE
#include "engine.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    int g;
    int f;
    int parent;
    uint8_t state;
} AStarNode;

uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

int32_t read_i32_le(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool load_blob(const char *path, Blob *out) {
    memset(out, 0, sizeof(*out));
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return false;
    }
    rewind(fp);
    out->bytes = malloc((size_t)len);
    out->size = (size_t)len;
    if (!out->bytes) {
        fclose(fp);
        return false;
    }
    if (fread(out->bytes, 1, out->size, fp) != out->size) {
        free(out->bytes);
        memset(out, 0, sizeof(*out));
        fclose(fp);
        return false;
    }
    fclose(fp);
    return true;
}

void free_blob(Blob *blob) {
    free(blob->bytes);
    memset(blob, 0, sizeof(*blob));
}

void path_join(char *dst, size_t dst_size, const char *a, const char *b) {
    snprintf(dst, dst_size, "%s/%s", a, b);
}

int clamp255(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

void indexed_to_rgba(uint32_t *dst, const uint8_t *src, size_t count, const uint32_t palette[256]) {
    for (size_t i = 0; i < count; ++i) dst[i] = palette[src[i]];
}

void blit_indexed_to_rgba(uint32_t *dst, int dst_w, int dst_h, int dst_x, int dst_y,
                          const uint8_t *src, int src_w, int src_h, const uint32_t palette[256]) {
    for (int y = 0; y < src_h; ++y) {
        int out_y = dst_y + y;
        if (out_y < 0 || out_y >= dst_h) continue;
        for (int x = 0; x < src_w; ++x) {
            int out_x = dst_x + x;
            if (out_x < 0 || out_x >= dst_w) continue;
            dst[out_y * dst_w + out_x] = palette[src[y * src_w + x]];
        }
    }
}

SDL_Texture *rgba_texture(SDL_Renderer *renderer, const uint32_t *pixels, int w, int h, bool blend) {
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!texture) return NULL;
    SDL_UpdateTexture(texture, NULL, pixels, w * (int)sizeof(uint32_t));
    SDL_SetTextureBlendMode(texture, blend ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    return texture;
}

bool tileset_add_animation(Tileset *tileset, int value, const int *frames,
                           int frame_count, uint16_t frame_ms) {
    if (!tileset || !frames || frame_count < 2 || frame_count > MAX_TILE_ANIMATION_FRAMES || frame_ms == 0) {
        return false;
    }
    TileAnimation *animations = realloc(tileset->animations,
                                        (size_t)(tileset->animation_count + 1) * sizeof(TileAnimation));
    if (!animations) return false;
    tileset->animations = animations;

    TileAnimation *anim = &tileset->animations[tileset->animation_count++];
    memset(anim, 0, sizeof(*anim));
    anim->value = value;
    anim->frame_count = frame_count;
    anim->frame_ms = frame_ms;
    for (int i = 0; i < frame_count; ++i) anim->frames[i] = frames[i];
    return true;
}

static int app_cell_w(const App *app) {
    int scale = app->render_scale > 0 ? app->render_scale : 1;
    return (app->cell_w > 0 ? app->cell_w : CELL_W) * scale;
}

static int app_cell_h(const App *app) {
    int scale = app->render_scale > 0 ? app->render_scale : 1;
    return (app->cell_h > 0 ? app->cell_h : CELL_H) * scale;
}

static int app_scale(const App *app) {
    return app->render_scale > 0 ? app->render_scale : 1;
}

static float viewport_scale_x(const App *app) {
    int window_w = 0, window_h = 0;
    int render_w = 0, render_h = 0;
    if (!app || !app->window || !app->renderer) return 1.0f;
    SDL_GetWindowSize(app->window, &window_w, &window_h);
    if (SDL_GetRendererOutputSize(app->renderer, &render_w, &render_h) != 0 ||
        window_w <= 0 || window_h <= 0 || render_w <= 0 || render_h <= 0) {
        return 1.0f;
    }
    return (float)render_w / (float)window_w;
}

static float viewport_scale_y(const App *app) {
    int window_w = 0, window_h = 0;
    int render_w = 0, render_h = 0;
    if (!app || !app->window || !app->renderer) return 1.0f;
    SDL_GetWindowSize(app->window, &window_w, &window_h);
    if (SDL_GetRendererOutputSize(app->renderer, &render_w, &render_h) != 0 ||
        window_w <= 0 || window_h <= 0 || render_w <= 0 || render_h <= 0) {
        return 1.0f;
    }
    return (float)render_h / (float)window_h;
}

static int app_tile_w(const App *app, const Tileset *tileset) {
    return tileset->tile_w * app_scale(app);
}

static int app_tile_h(const App *app, const Tileset *tileset) {
    return tileset->tile_h * app_scale(app);
}

static int tileset_animate_value(const Tileset *tileset, int value, uint32_t ticks_ms) {
    if (!tileset->animations || tileset->animation_count <= 0) return value;
    for (int i = 0; i < tileset->animation_count; ++i) {
        const TileAnimation *anim = &tileset->animations[i];
        if (anim->value != value) continue;
        int frame = (int)((ticks_ms / anim->frame_ms) % (uint32_t)anim->frame_count);
        return anim->frames[frame];
    }
    return value;
}

static int tileset_resolve_tile(const Tileset *tileset, int value, uint32_t ticks_ms) {
    value = tileset_animate_value(tileset, value, ticks_ms);
    if (value < 0 || tileset->count <= 0) return -1;
    if (tileset->tile_lookup) {
        if (value < tileset->tile_lookup_count) {
            int tile = tileset->tile_lookup[value];
            if (tile >= 0 && tile < tileset->count) return tile;
        }
        return -1;
    }
    if (value < tileset->count) return value;
    return value % tileset->count;
}

int map_index(const GameMap *map, int x, int y) {
    return y * map->width + x;
}

bool map_contains(const GameMap *map, int x, int y) {
    return x >= 0 && y >= 0 && x < map->width && y < map->height;
}

bool map_walkable(const GameMap *map, int x, int y) {
    return map_contains(map, x, y) && (!map->blocked || map->blocked[map_index(map, x, y)] == 0);
}

static int heuristic(Cell a, Cell b) {
    int dx = abs(a.x - b.x);
    int dy = abs(a.y - b.y);
    return 10 * (dx + dy);
}

int astar_find(const GameMap *map, Cell start, Cell goal, Cell *out_path, int max_path) {
    if (!map_walkable(map, start.x, start.y) || !map_contains(map, goal.x, goal.y) || max_path <= 0) return 0;
    if (!map_walkable(map, goal.x, goal.y)) {
        const int radius = 8;
        bool found = false;
        Cell best = goal;
        int best_h = 1000000;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                int x = goal.x + dx;
                int y = goal.y + dy;
                if (!map_walkable(map, x, y)) continue;
                int h = abs(dx) + abs(dy);
                if (h < best_h) {
                    best_h = h;
                    best = (Cell){ x, y };
                    found = true;
                }
            }
        }
        if (!found) return 0;
        goal = best;
    }

    int total = map->width * map->height;
    AStarNode *nodes = calloc((size_t)total, sizeof(AStarNode));
    int *open = malloc((size_t)total * sizeof(int));
    if (!nodes || !open) {
        free(nodes);
        free(open);
        return 0;
    }
    for (int i = 0; i < total; ++i) nodes[i].parent = -1;
    int open_count = 0;
    int start_idx = map_index(map, start.x, start.y);
    int goal_idx = map_index(map, goal.x, goal.y);
    nodes[start_idx].g = 0;
    nodes[start_idx].f = heuristic(start, goal);
    nodes[start_idx].state = 1;
    open[open_count++] = start_idx;

    const int dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    while (open_count > 0) {
        int best_open = 0;
        for (int i = 1; i < open_count; ++i) {
            if (nodes[open[i]].f < nodes[open[best_open]].f) best_open = i;
        }
        int current = open[best_open];
        open[best_open] = open[--open_count];
        nodes[current].state = 2;
        if (current == goal_idx) break;

        int cx = current % map->width;
        int cy = current / map->width;
        for (int d = 0; d < 4; ++d) {
            int nx = cx + dirs[d][0];
            int ny = cy + dirs[d][1];
            if (!map_walkable(map, nx, ny)) continue;
            int ni = map_index(map, nx, ny);
            if (nodes[ni].state == 2) continue;
            int ng = nodes[current].g + 10;
            if (nodes[ni].state != 1 || ng < nodes[ni].g) {
                nodes[ni].parent = current;
                nodes[ni].g = ng;
                nodes[ni].f = ng + heuristic((Cell){ nx, ny }, goal);
                if (nodes[ni].state != 1) {
                    nodes[ni].state = 1;
                    open[open_count++] = ni;
                }
            }
        }
    }

    int length = 0;
    if (nodes[goal_idx].parent != -1 || goal_idx == start_idx) {
        int cursor = goal_idx;
        while (cursor != -1 && length < max_path) {
            out_path[length++] = (Cell){ cursor % map->width, cursor / map->width };
            cursor = nodes[cursor].parent;
        }
        for (int i = 0; i < length / 2; ++i) {
            Cell tmp = out_path[i];
            out_path[i] = out_path[length - 1 - i];
            out_path[length - 1 - i] = tmp;
        }
    }
    free(nodes);
    free(open);
    return length;
}

void grid_to_screen(const App *app, float gx, float gy, float *sx, float *sy) {
    *sx = gx * (float)app_cell_w(app) + app->cam_x;
    *sy = gy * (float)app_cell_h(app) + app->cam_y;
}

Cell screen_to_grid(const App *app, int sx, int sy) {
    return (Cell){ (int)floorf(((float)sx - app->cam_x) / (float)app_cell_w(app)),
                   (int)floorf(((float)sy - app->cam_y) / (float)app_cell_h(app)) };
}

void refresh_app_viewport(App *app) {
    if (!app || !app->window || !app->renderer) return;
    int render_w = 0, render_h = 0;
    if (SDL_GetRendererOutputSize(app->renderer, &render_w, &render_h) != 0 ||
        render_w <= 0 || render_h <= 0) {
        SDL_GetWindowSize(app->window, &render_w, &render_h);
    }
    if (render_w > 0) app->win_w = render_w;
    if (render_h > 0) app->win_h = render_h;
}

void window_to_render_point(const App *app, int wx, int wy, int *rx, int *ry) {
    float sx = viewport_scale_x(app);
    float sy = viewport_scale_y(app);
    if (rx) *rx = (int)lroundf((float)wx * sx);
    if (ry) *ry = (int)lroundf((float)wy * sy);
}

void window_to_render_delta(const App *app, int wx, int wy, float *rx, float *ry) {
    float sx = viewport_scale_x(app);
    float sy = viewport_scale_y(app);
    if (rx) *rx = (float)wx * sx;
    if (ry) *ry = (float)wy * sy;
}

void render_grid_cell(App *app, int gx, int gy, SDL_Color color) {
    float sx, sy;
    grid_to_screen(app, (float)gx, (float)gy, &sx, &sy);
    SDL_SetRenderDrawColor(app->renderer, color.r, color.g, color.b, color.a);
    SDL_Rect r = { (int)sx, (int)sy, app_cell_w(app), app_cell_h(app) };
    SDL_RenderDrawRect(app->renderer, &r);
}

void render_tile_at(App *app, const Tileset *tileset, int tile, SDL_Rect src_part, SDL_Rect dst_part) {
    tile = tileset_resolve_tile(tileset, tile, app->ticks_ms);
    if (!tileset->texture || tile < 0 || tile >= tileset->count) return;
    SDL_Rect src = {
        (tile % tileset->atlas_cols) * tileset->tile_w + src_part.x,
        (tile / tileset->atlas_cols) * tileset->tile_h + src_part.y,
        src_part.w,
        src_part.h,
    };
    SDL_RenderCopy(app->renderer, tileset->texture, &src, &dst_part);
}

static void render_tile_at_flipped(App *app, const Tileset *tileset, int tile,
                                   SDL_Rect src_part, SDL_Rect dst_part, uint8_t flip_flags) {
    tile = tileset_resolve_tile(tileset, tile, app->ticks_ms);
    if (!tileset->texture || tile < 0 || tile >= tileset->count) return;
    SDL_Rect src = {
        (tile % tileset->atlas_cols) * tileset->tile_w + src_part.x,
        (tile / tileset->atlas_cols) * tileset->tile_h + src_part.y,
        src_part.w,
        src_part.h,
    };
    SDL_RendererFlip flip = (flip_flags & 1) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_RenderCopyEx(app->renderer, tileset->texture, &src, &dst_part, 0.0, NULL, flip);
}

void render_map(App *app, const GameMap *map, const Tileset *tileset) {
    int cell_w = app_cell_w(app);
    int cell_h = app_cell_h(app);
    int tile_w = app_tile_w(app, tileset);
    int tile_h = app_tile_h(app, tileset);
    int draw_y_offset = tileset->draw_y_offset * app_scale(app);
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            float sx, sy;
            grid_to_screen(app, (float)x, (float)y, &sx, &sy);
            if ((map->render_features & MAP_RENDER_USE_CELL_COLORS) && map->cell_colors) {
                if (sx < -cell_w || sy < -cell_h ||
                    sx > app->win_w + cell_w || sy > app->win_h + cell_h) {
                    continue;
                }
                uint32_t color = map->cell_colors[map_index(map, x, y)];
                SDL_SetRenderDrawColor(app->renderer,
                                       (uint8_t)(color >> 16),
                                       (uint8_t)(color >> 8),
                                       (uint8_t)color,
                                       255);
                SDL_Rect dst = { (int)sx, (int)sy, cell_w, cell_h };
                SDL_RenderFillRect(app->renderer, &dst);
                continue;
            }
            if (sx < -tile_w || sy < -tile_h ||
                sx > app->win_w + tile_w || sy > app->win_h + tile_h) {
                continue;
            }
            int idx = map_index(map, x, y);
            int tile = map->tile_ids[idx];
            if ((map->render_features & MAP_RENDER_SKIP_ZERO_TILES) && tile == 0) continue;
            SDL_Rect src = { 0, 0, tileset->tile_w, tileset->tile_h };
            SDL_Rect dst = {
                (int)sx,
                (int)(sy + draw_y_offset),
                tile_w,
                tile_h,
            };
            uint8_t base_flip = map->tile_flip_flags[0] ? map->tile_flip_flags[0][idx] : 0;
            render_tile_at_flipped(app, tileset, tile, src, dst, base_flip);
            if (map->render_features & MAP_RENDER_INTERLEAVED_OVERLAYS) {
                for (int layer = 0; layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS; ++layer) {
                    if (!map->tile_overlays[layer]) continue;
                    int overlay = map->tile_overlays[layer][idx];
                    if (overlay <= 0) continue;
                    uint8_t overlay_flip = map->tile_flip_flags[layer + 1] ?
                        map->tile_flip_flags[layer + 1][idx] : 0;
                    render_tile_at_flipped(app, tileset, overlay, src, dst, overlay_flip);
                }
            }
        }
    }

    for (int layer = 0;
         !(map->render_features & MAP_RENDER_INTERLEAVED_OVERLAYS) &&
         layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS;
         ++layer) {
        if (!map->tile_overlays[layer]) continue;
        for (int y = 0; y < map->height; ++y) {
            for (int x = 0; x < map->width; ++x) {
                float sx, sy;
                grid_to_screen(app, (float)x, (float)y, &sx, &sy);
                if (sx < -tile_w || sy < -tile_h ||
                    sx > app->win_w + tile_w || sy > app->win_h + tile_h) {
                    continue;
                }
                int idx = map_index(map, x, y);
                int overlay = map->tile_overlays[layer][idx];
                if (overlay <= 0) continue;
                SDL_Rect src = { 0, 0, tileset->tile_w, tileset->tile_h };
                SDL_Rect dst = {
                    (int)sx,
                    (int)(sy + draw_y_offset),
                    tile_w,
                    tile_h,
                };
                uint8_t overlay_flip = map->tile_flip_flags[layer + 1] ?
                    map->tile_flip_flags[layer + 1][idx] : 0;
                render_tile_at_flipped(app, tileset, overlay, src, dst, overlay_flip);
            }
        }
    }

    if ((map->render_features & MAP_RENDER_SMOOTH_TRANSITIONS) &&
        !(map->render_features & MAP_RENDER_USE_CELL_COLORS) &&
        map->render_transitions) {
        SDL_SetTextureBlendMode(tileset->texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tileset->texture, 255);
        for (int y = 0; y < map->height; ++y) {
            for (int x = 0; x < map->width; ++x) {
                float sx, sy;
                grid_to_screen(app, (float)x, (float)y, &sx, &sy);
                if (sx < -tile_w || sy < -tile_h ||
                    sx > app->win_w + tile_w || sy > app->win_h + tile_h) {
                    continue;
                }
                int dx = (int)sx;
                int dy = (int)(sy + draw_y_offset);
                map->render_transitions(app, map, tileset, x, y, dx, dy);
            }
        }
        SDL_SetTextureAlphaMod(tileset->texture, 255);
        SDL_SetTextureBlendMode(tileset->texture, SDL_BLENDMODE_NONE);
    }

    if (app->show_grid) {
        for (int y = 0; y < map->height; ++y) {
            for (int x = 0; x < map->width; ++x) {
                float sx, sy;
                grid_to_screen(app, (float)x, (float)y, &sx, &sy);
                if (sx < -tile_w || sy < -tile_h ||
                    sx > app->win_w + tile_w || sy > app->win_h + tile_h) {
                    continue;
                }
                render_grid_cell(app, x, y, (SDL_Color){ 50, 78, 72, 80 });
            }
        }
    }
}

const SpriteSheet *sprite_cache_lookup(const SpriteCache *cache, const char *name) {
    if (!name || name[0] == '\0') return NULL;
    for (int i = 0; i < cache->count; ++i) {
        if (strcasecmp(cache->entries[i].name, name) == 0) return &cache->entries[i].sprite;
    }
    return NULL;
}

CachedSprite *sprite_cache_find(SpriteCache *cache, const char *name) {
    if (!name || name[0] == '\0') return NULL;
    for (int i = 0; i < cache->count; ++i) {
        if (strcasecmp(cache->entries[i].name, name) == 0) return &cache->entries[i];
    }
    return NULL;
}

static void render_decoration_sprite(App *app, const MapDecoration *dec, const SpriteSheet *sprite) {
    if (!sprite || !sprite->texture || sprite->frame_count <= 0) return;

    float sx, sy;
    grid_to_screen(app, (float)dec->gx, (float)dec->gy, &sx, &sy);
    int footprint_w = dec->footprint_w > 0 ? dec->footprint_w : 1;
    int footprint_h = dec->footprint_h > 0 ? dec->footprint_h : 1;
    int scale = app_scale(app);
    int sprite_w = sprite->frame_w * scale;
    int sprite_h = sprite->frame_h * scale;
    SDL_Rect dst = {
        (int)(sx + (float)(footprint_w * app_cell_w(app) - sprite_w) * 0.5f),
        (int)(sy + (float)(footprint_h * app_cell_h(app) - sprite_h)),
        sprite_w,
        sprite_h,
    };
    if (dst.x > app->win_w || dst.y > app->win_h ||
        dst.x + dst.w < 0 || dst.y + dst.h < 0) {
        return;
    }
    SDL_RenderCopy(app->renderer, sprite->texture, &sprite->frames[0], &dst);
}

void render_decorations(App *app, const GameMap *map, const SpriteCache *cache) {
    for (int i = 0; i < map->decoration_count; ++i) {
        const MapDecoration *dec = &map->decorations[i];
        render_decoration_sprite(app, dec, sprite_cache_lookup(cache, dec->shadow_name));
        render_decoration_sprite(app, dec, sprite_cache_lookup(cache, dec->sprite_name));
    }
}

static SDL_Rect normalized_rect(int x0, int y0, int x1, int y1) {
    SDL_Rect r;
    r.x = x0 < x1 ? x0 : x1;
    r.y = y0 < y1 ? y0 : y1;
    r.w = abs(x1 - x0);
    r.h = abs(y1 - y0);
    return r;
}

static bool point_in_rect(int x, int y, SDL_Rect r) {
    return x >= r.x && y >= r.y && x <= r.x + r.w && y <= r.y + r.h;
}

static int direction_code_from_vector(float dx, float dy) {
    if (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) return 0;
    float angle = atan2f(-dy, dx);
    const float quarter_turn = 0.7853981633974483f;
    int sector = (int)floorf(angle / quarter_turn + 0.5f);
    sector %= 8;
    if (sector < 0) sector += 8;
    return sector * 2;
}

void issue_move_order(const GameMap *map, Unit *units, int unit_count, Cell goal) {
    int selected_index = 0;
    for (int i = 0; i < unit_count; ++i) {
        if (!units[i].selected) continue;
        if (units[i].hp <= 0) continue;
        if (units[i].owner != 0 || (units[i].traits & RTS_TRAIT_MOBILE) == 0) continue;
        Cell target = { goal.x + selected_index % 3 - 1, goal.y + selected_index / 3 };
        if (!map_contains(map, target.x, target.y)) target = goal;
        Cell start = { (int)floorf(units[i].gx), (int)floorf(units[i].gy) };
        int len = astar_find(map, start, target, units[i].path, MAX_PATH_CELLS);
        units[i].path_len = len;
        units[i].path_index = len > 1 ? 1 : 0;
        selected_index++;
    }
}

void update_units(Unit *units, int unit_count, float dt) {
    int dt_ms = (int)lroundf(dt * 1000.0f);
    for (int i = 0; i < unit_count; ++i) {
        Unit *u = &units[i];
        if (u->hp <= 0) {
            if (u->death_started && u->death_anim_left_ms > 0) {
                u->death_anim_left_ms -= dt_ms;
                if (u->death_anim_left_ms < 0) u->death_anim_left_ms = 0;
            }
            continue;
        }
        if (u->attack_cooldown_left_ms > 0) {
            u->attack_cooldown_left_ms -= dt_ms;
            if (u->attack_cooldown_left_ms < 0) u->attack_cooldown_left_ms = 0;
        }
        if (u->attack_anim_left_ms > 0) {
            u->attack_anim_left_ms -= dt_ms;
            if (u->attack_anim_left_ms < 0) u->attack_anim_left_ms = 0;
        }
        if (u->path_index <= 0 || u->path_index >= u->path_len) continue;
        Cell c = u->path[u->path_index];
        float tx = (float)c.x + 0.5f;
        float ty = (float)c.y + 0.5f;
        float dx = tx - u->gx;
        float dy = ty - u->gy;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist >= 0.001f) u->facing_code = direction_code_from_vector(dx, dy);
        float step = u->speed * dt;
        if (dist <= step || dist < 0.001f) {
            u->gx = tx;
            u->gy = ty;
            u->path_index++;
            if (u->path_index >= u->path_len) {
                u->path_len = 0;
                u->path_index = 0;
            }
        } else {
            u->gx += dx / dist * step;
            u->gy += dy / dist * step;
        }
    }

    for (int i = 0; i < unit_count; ++i) {
        Unit *attacker = &units[i];
        if (attacker->hp <= 0 || (attacker->traits & RTS_TRAIT_ATTACK) == 0 ||
            attacker->attack_damage <= 0 || attacker->attack_range <= 0.0f) {
            continue;
        }

        int target_index = -1;
        float best_dist2 = attacker->attack_range * attacker->attack_range;
        for (int j = 0; j < unit_count; ++j) {
            if (i == j || units[j].hp <= 0 || units[j].owner == attacker->owner) continue;
            float dx = units[j].gx - attacker->gx;
            float dy = units[j].gy - attacker->gy;
            float dist2 = dx * dx + dy * dy;
            if (dist2 <= best_dist2) {
                best_dist2 = dist2;
                target_index = j;
            }
        }
        attacker->attack_target = target_index;
        if (target_index < 0) continue;

        Unit *target = &units[target_index];
        attacker->facing_code = direction_code_from_vector(target->gx - attacker->gx,
                                                           target->gy - attacker->gy);
        if (attacker->attack_cooldown_left_ms > 0) continue;

        target->hp -= attacker->attack_damage;
        if (target->hp <= 0) {
            target->hp = 0;
            target->selected = false;
            target->traits &= ~(RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE | RTS_TRAIT_ATTACK);
            target->path_len = 0;
            target->path_index = 0;
            target->attack_target = -1;
            target->attack_cooldown_left_ms = 0;
            target->attack_anim_left_ms = 0;
            target->death_started = true;
            if (target->death_anim_ms <= 0) {
                target->death_anim_ms = 900;
            }
            target->death_anim_left_ms = target->death_anim_ms;
        }
        attacker->attack_cooldown_left_ms = attacker->attack_cooldown_ms > 0 ?
            attacker->attack_cooldown_ms : 500;
        attacker->attack_anim_left_ms = attacker->attack_anim_ms > 0 ?
            attacker->attack_anim_ms : attacker->attack_cooldown_left_ms;
    }
}

static const SpriteSequence *sprite_sequence_find(const SpriteSheet *sprite, const char *name) {
    if (!sprite || !name) return NULL;
    for (int i = 0; i < sprite->sequence_count; ++i) {
        if (strcmp(sprite->sequences[i].name, name) == 0) return &sprite->sequences[i];
    }
    return NULL;
}

static int sequence_facing_index(const SpriteSequence *seq, int direction_code) {
    if (!seq || seq->facings <= 0) return 0;
    int best = 0;
    int best_delta = 1000;
    for (int i = 0; i < seq->facings && i < MAX_SEQUENCE_FACINGS; ++i) {
        int code = seq->direction_codes[i];
        int delta = abs(code - direction_code);
        if (delta > 8) delta = 16 - delta;
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    return best;
}

static int sprite_frame_for_unit(const SpriteSheet *sprite, const Unit *unit, uint32_t ticks) {
    bool moving = unit->path_index > 0 && unit->path_index < unit->path_len;
    bool attacking = unit->attack_anim_left_ms > 0;
    bool dead = unit->hp <= 0;
    const SpriteSequence *seq = NULL;
    bool using_death_sequence = false;
    if (dead) {
        seq = sprite_sequence_find(sprite, "die");
        if (seq) using_death_sequence = true;
        if (!seq) {
            seq = sprite_sequence_find(sprite, "death");
            if (seq) using_death_sequence = true;
        }
        if (!seq) seq = sprite_sequence_find(sprite, "shoot");
    } else {
        seq = sprite_sequence_find(sprite, attacking ? "shoot" : (moving ? "run" : "stand"));
        if (!seq && attacking) seq = sprite_sequence_find(sprite, "attack");
        if (!seq && moving) seq = sprite_sequence_find(sprite, "walk");
    }
    if (!seq) seq = sprite_sequence_find(sprite, "idle");
    if (!seq && dead) seq = sprite_sequence_find(sprite, "stand");
    if (seq && seq->facings > 0 && seq->length > 0) {
        int direction_code = unit->facing_code;
        if (moving && !dead) {
            Cell c = unit->path[unit->path_index];
            float dx = ((float)c.x + 0.5f) - unit->gx;
            float dy = ((float)c.y + 0.5f) - unit->gy;
            direction_code = direction_code_from_vector(dx, dy);
        }
        int facing = sequence_facing_index(seq, direction_code);
        int tick_ms = seq->tick_ms > 0 ? seq->tick_ms : 120;
        int anim = 0;
        if (dead && using_death_sequence && unit->death_started && seq->length > 1) {
            int elapsed_ms = unit->death_anim_ms - unit->death_anim_left_ms;
            if (elapsed_ms < 0) elapsed_ms = 0;
            anim = elapsed_ms / tick_ms;
            if (anim >= seq->length) anim = seq->length - 1;
        } else if (dead && seq->length > 1) {
            anim = seq->length - 1;
        } else if (attacking && seq->length > 1) {
            int elapsed_ms = unit->attack_anim_ms - unit->attack_anim_left_ms;
            if (elapsed_ms < 0) elapsed_ms = 0;
            anim = elapsed_ms / tick_ms;
            if (anim >= seq->length) anim = seq->length - 1;
        } else if (moving && seq->length > 1) {
            anim = (int)((ticks / (uint32_t)tick_ms) % (uint32_t)seq->length);
        }
        int frame_stride = seq->frame_stride > 0 ? seq->frame_stride : 1;
        int frame = seq->frame_starts[facing] + anim * frame_stride;
        if (frame >= 0 && frame < sprite->frame_count) return frame;
    }

    int anim_frames = sprite->primary_frames_per_rotation > 0 ?
        sprite->primary_frames_per_rotation : sprite->frame_count;
    if (moving && anim_frames > 0 && sprite->sequence_count == 0) {
        return (int)((ticks / 120) % (uint32_t)anim_frames);
    }
    return 0;
}

void render_units(App *app, const Unit *units, int unit_count, const SpriteSheet *fallback_sprite,
                  const SpriteCache *cache, uint32_t ticks) {
    for (int i = 0; i < unit_count; ++i) {
        const Unit *u = &units[i];
        if ((u->traits & RTS_TRAIT_RENDERABLE) == 0) continue;
        const SpriteSheet *sprite = sprite_cache_lookup(cache, u->sprite_name);
        if (!sprite) sprite = fallback_sprite;
        if (!sprite || !sprite->texture || sprite->frame_count <= 0) continue;

        if (u->path_len > 1) {
            SDL_SetRenderDrawColor(app->renderer, 98, 224, 161, 170);
            for (int p = u->path_index; p < u->path_len - 1; ++p) {
                float ax, ay, bx, by;
                grid_to_screen(app, u->path[p].x + 0.5f, u->path[p].y + 0.5f, &ax, &ay);
                grid_to_screen(app, u->path[p + 1].x + 0.5f, u->path[p + 1].y + 0.5f, &bx, &by);
                SDL_RenderDrawLine(app->renderer, (int)ax, (int)ay, (int)bx, (int)by);
            }
        }

        float sx, sy;
        grid_to_screen(app, u->gx, u->gy, &sx, &sy);
        int frame = sprite_frame_for_unit(sprite, u, ticks);
        if (frame >= sprite->frame_count) frame = 0;
        int scale = app_scale(app);
        int sprite_w = sprite->frame_w * scale;
        int sprite_h = sprite->frame_h * scale;
        SDL_Rect dst = {
            (int)(sx - sprite_w / 2),
            (int)(sy - sprite_h / 2),
            sprite_w,
            sprite_h,
        };
        const SpriteSheet *shadow = sprite_cache_lookup(cache, u->shadow_name);
        if (shadow && shadow->texture && shadow->frame_count > 0) {
            int shadow_frame = frame < shadow->frame_count ? frame : 0;
            int shadow_w = shadow->frame_w * scale;
            int shadow_h = shadow->frame_h * scale;
            SDL_Rect shadow_dst = {
                (int)(sx - shadow_w / 2),
                (int)(sy - shadow_h / 2),
                shadow_w,
                shadow_h,
            };
            SDL_RenderCopy(app->renderer, shadow->texture, &shadow->frames[shadow_frame], &shadow_dst);
        }
        SDL_RenderCopy(app->renderer, sprite->texture, &sprite->frames[frame], &dst);
        if (u->max_hp > 0 && u->hp > 0 && u->hp < u->max_hp) {
            int bar_w = sprite_w / 2;
            int bar_h = app_scale(app) < 2 ? 2 : 3;
            int bx = (int)(sx - bar_w / 2);
            int by = dst.y - bar_h - 2;
            SDL_Rect back = { bx, by, bar_w, bar_h };
            SDL_Rect fill = { bx, by, (bar_w * u->hp) / u->max_hp, bar_h };
            SDL_SetRenderDrawColor(app->renderer, 40, 20, 20, 220);
            SDL_RenderFillRect(app->renderer, &back);
            SDL_SetRenderDrawColor(app->renderer, 98, 224, 161, 230);
            SDL_RenderFillRect(app->renderer, &fill);
        }
        if (u->selected && (u->traits & RTS_TRAIT_SELECTABLE) != 0) {
            SDL_SetRenderDrawColor(app->renderer, 98, 224, 161, 255);
            SDL_Rect box = { dst.x - 3, dst.y - 3, dst.w + 6, dst.h + 6 };
            SDL_RenderDrawRect(app->renderer, &box);
            render_grid_cell(app, (int)floorf(u->gx), (int)floorf(u->gy), (SDL_Color){ 98, 224, 161, 255 });
        }
    }
}

void handle_event(App *app, const GameMap *map, Unit *units, int unit_count, const SDL_Event *e) {
    switch (e->type) {
        case SDL_QUIT:
            app->running = false;
            break;
        case SDL_WINDOWEVENT:
            if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                refresh_app_viewport(app);
            }
            break;
        case SDL_KEYDOWN:
            if (e->key.keysym.sym == SDLK_ESCAPE) app->running = false;
            if (e->key.keysym.sym == SDLK_g) app->show_grid = !app->show_grid;
            if (e->key.keysym.sym == SDLK_a && (e->key.keysym.mod & KMOD_CTRL)) {
                for (int i = 0; i < unit_count; ++i) units[i].selected = true;
            }
            break;
        case SDL_MOUSEMOTION:
            window_to_render_point(app, e->motion.x, e->motion.y, &app->mouse_x, &app->mouse_y);
            if (app->panning) {
                float dx = 0.0f, dy = 0.0f;
                window_to_render_delta(app, e->motion.xrel, e->motion.yrel, &dx, &dy);
                app->cam_x += dx;
                app->cam_y += dy;
            }
            if (app->dragging_select) {
                int mx = 0, my = 0;
                window_to_render_point(app, e->motion.x, e->motion.y, &mx, &my);
                app->selection_rect = normalized_rect(app->mouse_down_x, app->mouse_down_y, mx, my);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            window_to_render_point(app, e->button.x, e->button.y, &app->mouse_down_x, &app->mouse_down_y);
            if (e->button.button == SDL_BUTTON_LEFT) {
                app->dragging_select = true;
                app->selection_rect = (SDL_Rect){ app->mouse_down_x, app->mouse_down_y, 0, 0 };
            } else if (e->button.button == SDL_BUTTON_RIGHT) {
                int rx = 0, ry = 0;
                window_to_render_point(app, e->button.x, e->button.y, &rx, &ry);
                issue_move_order(map, units, unit_count, screen_to_grid(app, rx, ry));
            } else if (e->button.button == SDL_BUTTON_MIDDLE) {
                app->panning = true;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e->button.button == SDL_BUTTON_LEFT && app->dragging_select) {
                int bx = 0, by = 0;
                window_to_render_point(app, e->button.x, e->button.y, &bx, &by);
                SDL_Rect rect = normalized_rect(app->mouse_down_x, app->mouse_down_y, bx, by);
                bool box = rect.w > 5 || rect.h > 5;
                bool additive = (SDL_GetModState() & KMOD_SHIFT) != 0;
                if (!additive) {
                    for (int i = 0; i < unit_count; ++i) units[i].selected = false;
                }
                for (int i = 0; i < unit_count; ++i) {
                    if (units[i].hp <= 0) continue;
                    if ((units[i].traits & RTS_TRAIT_SELECTABLE) == 0) continue;
                    float sx, sy;
                    grid_to_screen(app, units[i].gx, units[i].gy, &sx, &sy);
                    if ((box && point_in_rect((int)sx, (int)sy, rect)) ||
                        (!box && hypotf((float)bx - sx, (float)by - sy) < 30.0f)) {
                        units[i].selected = true;
                    }
                }
                app->dragging_select = false;
            } else if (e->button.button == SDL_BUTTON_MIDDLE) {
                app->panning = false;
            }
            break;
        case SDL_MOUSEWHEEL:
            app->cam_y += (float)e->wheel.y * 48.0f * (float)app_scale(app);
            app->cam_x += (float)e->wheel.x * 48.0f * (float)app_scale(app);
            break;
        default:
            break;
    }
}

void update_camera_from_keyboard(App *app, float dt) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    float speed = 600.0f * dt * (float)app_scale(app);
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) app->cam_x += speed;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) app->cam_x -= speed;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) app->cam_y += speed;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) app->cam_y -= speed;
}

void destroy_tileset(Tileset *tileset) {
    if (tileset->texture) SDL_DestroyTexture(tileset->texture);
    free(tileset->tile_lookup);
    free(tileset->animations);
    memset(tileset, 0, sizeof(*tileset));
}

void destroy_map(GameMap *map) {
    free(map->tile_ids);
    for (int i = 0; i < MAX_TILE_OVERLAYS; ++i) free(map->tile_overlays[i]);
    for (int i = 0; i < MAX_TILE_OVERLAYS + 1; ++i) free(map->tile_flip_flags[i]);
    free(map->blocked);
    free(map->cell_colors);
    free(map->decorations);
    memset(map, 0, sizeof(*map));
}

void destroy_sprite(SpriteSheet *sprite) {
    if (sprite->texture) SDL_DestroyTexture(sprite->texture);
    free(sprite->frames);
    memset(sprite, 0, sizeof(*sprite));
}

void destroy_sprite_cache(SpriteCache *cache) {
    for (int i = 0; i < cache->count; ++i) {
        destroy_sprite(&cache->entries[i].sprite);
    }
    memset(cache, 0, sizeof(*cache));
}
