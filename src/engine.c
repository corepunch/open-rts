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

static int app_cell_w(const App *app) {
    return app->cell_w > 0 ? app->cell_w : CELL_W;
}

static int app_cell_h(const App *app) {
    return app->cell_h > 0 ? app->cell_h : CELL_H;
}

static int tileset_resolve_tile(const Tileset *tileset, int value) {
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

void render_grid_cell(App *app, int gx, int gy, SDL_Color color) {
    float sx, sy;
    grid_to_screen(app, (float)gx, (float)gy, &sx, &sy);
    SDL_SetRenderDrawColor(app->renderer, color.r, color.g, color.b, color.a);
    SDL_Rect r = { (int)sx, (int)sy, app_cell_w(app), app_cell_h(app) };
    SDL_RenderDrawRect(app->renderer, &r);
}

void render_tile_at(App *app, const Tileset *tileset, int tile, SDL_Rect src_part, SDL_Rect dst_part) {
    tile = tileset_resolve_tile(tileset, tile);
    if (!tileset->texture || tile < 0 || tile >= tileset->count) return;
    SDL_Rect src = {
        (tile % tileset->atlas_cols) * tileset->tile_w + src_part.x,
        (tile / tileset->atlas_cols) * tileset->tile_h + src_part.y,
        src_part.w,
        src_part.h,
    };
    SDL_RenderCopy(app->renderer, tileset->texture, &src, &dst_part);
}

void render_map(App *app, const GameMap *map, const Tileset *tileset) {
    int cell_w = app_cell_w(app);
    int cell_h = app_cell_h(app);
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
            if (sx < -tileset->tile_w || sy < -tileset->tile_h ||
                sx > app->win_w + tileset->tile_w || sy > app->win_h + tileset->tile_h) {
                continue;
            }
            int tile = map->tile_ids[map_index(map, x, y)];
            if ((map->render_features & MAP_RENDER_SKIP_ZERO_TILES) && tile == 0) continue;
            SDL_Rect src = { 0, 0, tileset->tile_w, tileset->tile_h };
            SDL_Rect dst = {
                (int)sx,
                (int)(sy + tileset->draw_y_offset),
                tileset->tile_w,
                tileset->tile_h,
            };
            render_tile_at(app, tileset, tile, src, dst);
        }
    }

    for (int layer = 0; layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS; ++layer) {
        if (!map->tile_overlays[layer]) continue;
        for (int y = 0; y < map->height; ++y) {
            for (int x = 0; x < map->width; ++x) {
                float sx, sy;
                grid_to_screen(app, (float)x, (float)y, &sx, &sy);
                if (sx < -tileset->tile_w || sy < -tileset->tile_h ||
                    sx > app->win_w + tileset->tile_w || sy > app->win_h + tileset->tile_h) {
                    continue;
                }
                int overlay = map->tile_overlays[layer][map_index(map, x, y)];
                if (overlay <= 0) continue;
                SDL_Rect src = { 0, 0, tileset->tile_w, tileset->tile_h };
                SDL_Rect dst = {
                    (int)sx,
                    (int)(sy + tileset->draw_y_offset),
                    tileset->tile_w,
                    tileset->tile_h,
                };
                render_tile_at(app, tileset, overlay, src, dst);
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
                if (sx < -tileset->tile_w || sy < -tileset->tile_h ||
                    sx > app->win_w + tileset->tile_w || sy > app->win_h + tileset->tile_h) {
                    continue;
                }
                int dx = (int)sx;
                int dy = (int)(sy + tileset->draw_y_offset);
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
                if (sx < -tileset->tile_w || sy < -tileset->tile_h ||
                    sx > app->win_w + tileset->tile_w || sy > app->win_h + tileset->tile_h) {
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
    SDL_Rect dst = {
        (int)(sx + (float)(footprint_w * app_cell_w(app) - sprite->frame_w) * 0.5f),
        (int)(sy + (float)(footprint_h * app_cell_h(app) - sprite->frame_h)),
        sprite->frame_w,
        sprite->frame_h,
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

void issue_move_order(const GameMap *map, Unit *units, int unit_count, Cell goal) {
    int selected_index = 0;
    for (int i = 0; i < unit_count; ++i) {
        if (!units[i].selected) continue;
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
    for (int i = 0; i < unit_count; ++i) {
        Unit *u = &units[i];
        if (u->path_index <= 0 || u->path_index >= u->path_len) continue;
        Cell c = u->path[u->path_index];
        float tx = (float)c.x + 0.5f;
        float ty = (float)c.y + 0.5f;
        float dx = tx - u->gx;
        float dy = ty - u->gy;
        float dist = sqrtf(dx * dx + dy * dy);
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
}

void render_units(App *app, const Unit *units, int unit_count, const SpriteSheet *fallback_sprite,
                  const SpriteCache *cache, uint32_t ticks) {
    for (int i = 0; i < unit_count; ++i) {
        const Unit *u = &units[i];
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
        int frame = 0;
        int anim_frames = sprite->primary_frames_per_rotation > 0 ?
            sprite->primary_frames_per_rotation : sprite->frame_count;
        if (u->path_len != 0 && anim_frames > 0) {
            frame = (int)((ticks / 120) % (uint32_t)anim_frames);
        }
        if (frame >= sprite->frame_count) frame = 0;
        SDL_Rect dst = {
            (int)(sx - sprite->frame_w / 2),
            (int)(sy - sprite->frame_h / 2),
            sprite->frame_w,
            sprite->frame_h,
        };
        const SpriteSheet *shadow = sprite_cache_lookup(cache, u->shadow_name);
        if (shadow && shadow->texture && shadow->frame_count > 0) {
            SDL_Rect shadow_dst = {
                (int)(sx - shadow->frame_w / 2),
                (int)(sy - shadow->frame_h / 2),
                shadow->frame_w,
                shadow->frame_h,
            };
            SDL_RenderCopy(app->renderer, shadow->texture, &shadow->frames[0], &shadow_dst);
        }
        SDL_RenderCopy(app->renderer, sprite->texture, &sprite->frames[frame], &dst);
        if (u->selected) {
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
                app->win_w = e->window.data1;
                app->win_h = e->window.data2;
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
            app->mouse_x = e->motion.x;
            app->mouse_y = e->motion.y;
            if (app->panning) {
                app->cam_x += (float)e->motion.xrel;
                app->cam_y += (float)e->motion.yrel;
            }
            if (app->dragging_select) {
                app->selection_rect = normalized_rect(app->mouse_down_x, app->mouse_down_y, e->motion.x, e->motion.y);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            app->mouse_down_x = e->button.x;
            app->mouse_down_y = e->button.y;
            if (e->button.button == SDL_BUTTON_LEFT) {
                app->dragging_select = true;
                app->selection_rect = (SDL_Rect){ e->button.x, e->button.y, 0, 0 };
            } else if (e->button.button == SDL_BUTTON_RIGHT) {
                issue_move_order(map, units, unit_count, screen_to_grid(app, e->button.x, e->button.y));
            } else if (e->button.button == SDL_BUTTON_MIDDLE) {
                app->panning = true;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e->button.button == SDL_BUTTON_LEFT && app->dragging_select) {
                SDL_Rect rect = normalized_rect(app->mouse_down_x, app->mouse_down_y, e->button.x, e->button.y);
                bool box = rect.w > 5 || rect.h > 5;
                bool additive = (SDL_GetModState() & KMOD_SHIFT) != 0;
                if (!additive) {
                    for (int i = 0; i < unit_count; ++i) units[i].selected = false;
                }
                for (int i = 0; i < unit_count; ++i) {
                    float sx, sy;
                    grid_to_screen(app, units[i].gx, units[i].gy, &sx, &sy);
                    if ((box && point_in_rect((int)sx, (int)sy, rect)) ||
                        (!box && hypotf((float)e->button.x - sx, (float)e->button.y - sy) < 30.0f)) {
                        units[i].selected = true;
                    }
                }
                app->dragging_select = false;
            } else if (e->button.button == SDL_BUTTON_MIDDLE) {
                app->panning = false;
            }
            break;
        case SDL_MOUSEWHEEL:
            app->cam_y += (float)e->wheel.y * 48.0f;
            app->cam_x += (float)e->wheel.x * 48.0f;
            break;
        default:
            break;
    }
}

void update_camera_from_keyboard(App *app, float dt) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    float speed = 600.0f * dt;
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) app->cam_x += speed;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) app->cam_x -= speed;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) app->cam_y += speed;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) app->cam_y -= speed;
}

void destroy_tileset(Tileset *tileset) {
    if (tileset->texture) SDL_DestroyTexture(tileset->texture);
    free(tileset->tile_lookup);
    memset(tileset, 0, sizeof(*tileset));
}

void destroy_map(GameMap *map) {
    free(map->tile_ids);
    for (int i = 0; i < MAX_TILE_OVERLAYS; ++i) free(map->tile_overlays[i]);
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
