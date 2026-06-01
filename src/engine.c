#define _DEFAULT_SOURCE
#include "engine.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
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

typedef struct {
    int cost;
    uint8_t closed;
    uint8_t queued;
} FlowCell;

enum {
    RTS_HARVEST_INTERVAL_MS = 1000,
    RTS_HARVEST_DEFAULT_RATE = 20,
};

static bool debug_effects_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("OPEN_RTS_DEBUG_EFFECTS");
        enabled = value && value[0] != '\0' && strcmp(value, "0") != 0;
    }
    return enabled != 0;
}

static void debug_effects_log(const char *fmt, ...) {
    if (!debug_effects_enabled()) return;
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[effects] ");
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
}

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

int rts_font_text_width(const RtsBitmapFont *font, const char *text, int scale) {
    if (!font || !text || scale <= 0) return 0;
    int width = 0, line_width = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '\r') continue;
        if (*p == '\n') {
            if (line_width > width) width = line_width;
            line_width = 0;
            continue;
        }
        unsigned char ch = *p;
        if (ch >= 128 || font->glyph_index[ch] < 0) ch = '?';
        int advance = font->glyph_width[ch] > 0 ? font->glyph_width[ch] : font->glyph_w;
        line_width += advance * scale;
    }
    return line_width > width ? line_width : width;
}

void rts_font_draw_text(SDL_Renderer *renderer, const RtsBitmapFont *font, int x, int y,
                        const char *text, SDL_Color color, int scale) {
    if (!renderer || !font || !font->sprite.texture || !text || scale <= 0) return;
    SDL_SetTextureColorMod(font->sprite.texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(font->sprite.texture, color.a);
    int cx = x, cy = y;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '\r') continue;
        if (*p == '\n') {
            cx = x;
            cy += (font->line_h > 0 ? font->line_h : font->glyph_h) * scale;
            continue;
        }
        unsigned char ch = *p;
        if (ch >= 128 || font->glyph_index[ch] < 0) ch = '?';
        int frame = font->glyph_index[ch];
        int advance = font->glyph_width[ch] > 0 ? font->glyph_width[ch] : font->glyph_w;
        if (frame >= 0 && frame < font->sprite.frame_count) {
            SDL_Rect src = font->sprite.frames[frame];
            if (font->sprite.frame_bounds && font->sprite.frame_bounds[frame].w > 0 &&
                font->sprite.frame_bounds[frame].h > 0) {
                SDL_Rect bounds = font->sprite.frame_bounds[frame];
                src.x += bounds.x;
                src.y += bounds.y;
                src.w = bounds.w;
                src.h = bounds.h;
            }
            if (src.w > 0 && src.h > 0) {
                int divisor = font->draw_divisor > 0 ? font->draw_divisor : 1;
                SDL_Rect dst = {
                    cx,
                    cy,
                    (src.w * scale + divisor - 1) / divisor,
                    (src.h * scale + divisor - 1) / divisor,
                };
                SDL_RenderCopy(renderer, font->sprite.texture, &src, &dst);
            }
        }
        cx += advance * scale;
    }
    SDL_SetTextureColorMod(font->sprite.texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(font->sprite.texture, 255);
}

void rts_font_draw_text_wrapped(SDL_Renderer *renderer, const RtsBitmapFont *font, int x, int y,
                                int max_w, const char *text, SDL_Color color, int scale) {
    if (!renderer || !font || !text || max_w <= 0 || scale <= 0) return;
    char line[256] = { 0 };
    int line_len = 0;
    int cy = y;
    const char *word = text;
    while (*word) {
        while (*word == ' ' || *word == '\r' || *word == '\n') {
            if (*word == '\n' && line_len > 0) {
                rts_font_draw_text(renderer, font, x, cy, line, color, scale);
                cy += (font->line_h > 0 ? font->line_h : font->glyph_h) * scale;
                line[0] = '\0';
                line_len = 0;
            }
            word++;
        }
        if (!*word) break;
        const char *end = word;
        while (*end && *end != ' ' && *end != '\r' && *end != '\n') end++;
        size_t word_len = (size_t)(end - word);
        if (word_len >= sizeof(line)) word_len = sizeof(line) - 1;
        char candidate[256];
        if (line_len > 0)
            snprintf(candidate, sizeof(candidate), "%s %.*s", line, (int)word_len, word);
        else
            snprintf(candidate, sizeof(candidate), "%.*s", (int)word_len, word);
        if (line_len > 0 && rts_font_text_width(font, candidate, scale) > max_w) {
            rts_font_draw_text(renderer, font, x, cy, line, color, scale);
            cy += (font->line_h > 0 ? font->line_h : font->glyph_h) * scale;
            snprintf(line, sizeof(line), "%.*s", (int)word_len, word);
        } else {
            snprintf(line, sizeof(line), "%s", candidate);
        }
        line_len = (int)strlen(line);
        word = end;
    }
    if (line_len > 0) rts_font_draw_text(renderer, font, x, cy, line, color, scale);
}

void rts_hud_text_push(RtsHudText *hud, const char *text, int ttl_ms) {
    if (!hud || !text || text[0] == '\0') return;
    if (ttl_ms <= 0) ttl_ms = 5000;
    int slot = hud->count;
    if (slot >= RTS_MAX_HUD_MESSAGES) {
        memmove(&hud->messages[0], &hud->messages[1],
                sizeof(hud->messages[0]) * (RTS_MAX_HUD_MESSAGES - 1));
        slot = RTS_MAX_HUD_MESSAGES - 1;
        hud->count = RTS_MAX_HUD_MESSAGES;
    } else {
        hud->count++;
    }
    snprintf(hud->messages[slot].text, sizeof(hud->messages[slot].text), "%s", text);
    hud->messages[slot].ttl_ms = ttl_ms;
}

void rts_hud_text_update(RtsHudText *hud, float dt) {
    if (!hud || hud->count <= 0) return;
    int dt_ms = (int)lroundf(dt * 1000.0f);
    for (int i = 0; i < hud->count;) {
        hud->messages[i].ttl_ms -= dt_ms;
        if (hud->messages[i].ttl_ms <= 0) {
            memmove(&hud->messages[i], &hud->messages[i + 1],
                    sizeof(hud->messages[0]) * (size_t)(hud->count - i - 1));
            hud->count--;
        } else {
            i++;
        }
    }
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

static float unit_radius_cells(const Unit *unit) {
    if (unit && unit->radius > 0.05f) return unit->radius;
    return 0.42f;
}

static bool map_circle_walkable(const GameMap *map, float gx, float gy, float radius) {
    if (!map) return true;
    if (radius < 0.01f) radius = 0.01f;
    if (gx - radius < 0.0f || gy - radius < 0.0f ||
        gx + radius > (float)map->width || gy + radius > (float)map->height) {
        return false;
    }

    int min_x = (int)floorf(gx - radius);
    int max_x = (int)floorf(gx + radius);
    int min_y = (int)floorf(gy - radius);
    int max_y = (int)floorf(gy + radius);
    float radius2 = radius * radius - 0.0001f;
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            if (map_walkable(map, x, y)) continue;
            float closest_x = gx;
            float closest_y = gy;
            if (closest_x < (float)x) closest_x = (float)x;
            if (closest_x > (float)x + 1.0f) closest_x = (float)x + 1.0f;
            if (closest_y < (float)y) closest_y = (float)y;
            if (closest_y > (float)y + 1.0f) closest_y = (float)y + 1.0f;
            float dx = gx - closest_x;
            float dy = gy - closest_y;
            if (dx * dx + dy * dy < radius2) return false;
        }
    }
    return true;
}

static bool unit_position_walkable(const GameMap *map, const Unit *unit, float gx, float gy) {
    return map_circle_walkable(map, gx, gy, unit_radius_cells(unit));
}

static void clamp_unit_position_to_map(const GameMap *map, Unit *unit) {
    if (!map || !unit || map->width <= 0 || map->height <= 0) return;
    float r = unit_radius_cells(unit);
    float min_x = r;
    float min_y = r;
    float max_x = (float)map->width - r;
    float max_y = (float)map->height - r;
    if (max_x < min_x) max_x = min_x = (float)map->width * 0.5f;
    if (max_y < min_y) max_y = min_y = (float)map->height * 0.5f;
    if (unit->gx < min_x) unit->gx = min_x;
    if (unit->gy < min_y) unit->gy = min_y;
    if (unit->gx > max_x) unit->gx = max_x;
    if (unit->gy > max_y) unit->gy = max_y;
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

static bool find_nearest_walkable_cell(const GameMap *map, Cell wanted, int radius, Cell *out) {
    if (!map || !out) return false;
    if (map_walkable(map, wanted.x, wanted.y)) {
        *out = wanted;
        return true;
    }
    int best_h = 1000000;
    bool found = false;
    Cell best = wanted;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            Cell c = { wanted.x + dx, wanted.y + dy };
            if (!map_walkable(map, c.x, c.y)) continue;
            int h = dx * dx + dy * dy;
            if (h < best_h) {
                best_h = h;
                best = c;
                found = true;
            }
        }
    }
    if (!found) return false;
    *out = best;
    return true;
}

static bool find_nearest_walkable_position(const GameMap *map, float wanted_gx, float wanted_gy,
                                           float unit_radius, int search_radius,
                                           float *gx_out, float *gy_out) {
    if (!gx_out || !gy_out) return false;
    if (map_circle_walkable(map, wanted_gx, wanted_gy, unit_radius)) {
        *gx_out = wanted_gx;
        *gy_out = wanted_gy;
        return true;
    }
    Cell wanted = { (int)floorf(wanted_gx), (int)floorf(wanted_gy) };
    float best_d2 = 1000000000.0f;
    bool found = false;
    float best_x = wanted_gx;
    float best_y = wanted_gy;
    for (int dy = -search_radius; dy <= search_radius; ++dy) {
        for (int dx = -search_radius; dx <= search_radius; ++dx) {
            float gx = (float)(wanted.x + dx) + 0.5f;
            float gy = (float)(wanted.y + dy) + 0.5f;
            if (!map_circle_walkable(map, gx, gy, unit_radius)) continue;
            float ddx = gx - wanted_gx;
            float ddy = gy - wanted_gy;
            float d2 = ddx * ddx + ddy * ddy;
            if (d2 < best_d2) {
                best_d2 = d2;
                best_x = gx;
                best_y = gy;
                found = true;
            }
        }
    }
    if (!found) return false;
    *gx_out = best_x;
    *gy_out = best_y;
    return true;
}

static FlowCell *build_flow_field(const GameMap *map, Cell goal) {
    if (!map || !map_walkable(map, goal.x, goal.y)) return NULL;
    int total = map->width * map->height;
    FlowCell *field = malloc((size_t)total * sizeof(*field));
    int *open = malloc((size_t)total * sizeof(*open));
    if (!field || !open) {
        free(field);
        free(open);
        return NULL;
    }
    for (int i = 0; i < total; ++i) {
        field[i].cost = 1000000000;
        field[i].closed = 0;
        field[i].queued = 0;
    }

    int open_count = 0;
    int goal_idx = map_index(map, goal.x, goal.y);
    field[goal_idx].cost = 0;
    field[goal_idx].queued = 1;
    open[open_count++] = goal_idx;

    static const int dirs[8][3] = {
        { 1, 0, 10 }, { -1, 0, 10 }, { 0, 1, 10 }, { 0, -1, 10 },
        { 1, 1, 14 }, { -1, 1, 14 }, { 1, -1, 14 }, { -1, -1, 14 },
    };
    while (open_count > 0) {
        int best_open = 0;
        for (int i = 1; i < open_count; ++i) {
            if (field[open[i]].cost < field[open[best_open]].cost) best_open = i;
        }
        int current = open[best_open];
        open[best_open] = open[--open_count];
        field[current].queued = 0;
        if (field[current].closed) continue;
        field[current].closed = 1;

        int cx = current % map->width;
        int cy = current / map->width;
        for (int d = 0; d < 8; ++d) {
            int nx = cx + dirs[d][0];
            int ny = cy + dirs[d][1];
            if (!map_walkable(map, nx, ny)) continue;
            if (dirs[d][0] != 0 && dirs[d][1] != 0 &&
                (!map_walkable(map, cx + dirs[d][0], cy) ||
                 !map_walkable(map, cx, cy + dirs[d][1]))) {
                continue;
            }
            int ni = map_index(map, nx, ny);
            int next_cost = field[current].cost + dirs[d][2];
            if (next_cost >= field[ni].cost) continue;
            field[ni].cost = next_cost;
            if (!field[ni].queued && !field[ni].closed && open_count < total) {
                field[ni].queued = 1;
                open[open_count++] = ni;
            }
        }
    }

    free(open);
    return field;
}

static bool line_walkable(const GameMap *map, Cell a, Cell b, float radius) {
    if (!map) return true;
    int dx = abs(b.x - a.x);
    int dy = abs(b.y - a.y);
    int steps = (dx > dy ? dx : dy) * 4;
    if (steps <= 0) return map_circle_walkable(map, (float)a.x + 0.5f, (float)a.y + 0.5f, radius);
    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        float x = (float)a.x + 0.5f + ((float)b.x - (float)a.x) * t;
        float y = (float)a.y + 0.5f + ((float)b.y - (float)a.y) * t;
        if (!map_circle_walkable(map, x, y, radius)) return false;
    }
    return true;
}

static int smooth_cell_path(const GameMap *map, Cell *path, int length, float radius) {
    if (!map || !path || length <= 2) return length;
    int write = 0;
    int anchor = 0;
    path[write++] = path[0];
    while (anchor < length - 1) {
        int next = length - 1;
        while (next > anchor + 1 && !line_walkable(map, path[anchor], path[next], radius)) next--;
        path[write++] = path[next];
        anchor = next;
    }
    return write;
}

static int flow_path_find(const GameMap *map, const FlowCell *field, Cell start,
                          Cell *out_path, int max_path, float radius) {
    if (!map || !field || !out_path || max_path <= 0 ||
        !map_walkable(map, start.x, start.y)) {
        return 0;
    }
    int current = map_index(map, start.x, start.y);
    if (field[current].cost >= 1000000000) return 0;
    int length = 0;
    out_path[length++] = start;
    static const int dirs[8][2] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
        { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 },
    };
    int guard = map->width * map->height;
    while (field[current].cost > 0 && length < max_path && guard-- > 0) {
        int cx = current % map->width;
        int cy = current / map->width;
        int best = current;
        int best_cost = field[current].cost;
        for (int d = 0; d < 8; ++d) {
            int nx = cx + dirs[d][0];
            int ny = cy + dirs[d][1];
            if (!map_walkable(map, nx, ny)) continue;
            if (dirs[d][0] != 0 && dirs[d][1] != 0 &&
                (!map_walkable(map, cx + dirs[d][0], cy) ||
                 !map_walkable(map, cx, cy + dirs[d][1]))) {
                continue;
            }
            int ni = map_index(map, nx, ny);
            if (field[ni].cost < best_cost) {
                best = ni;
                best_cost = field[ni].cost;
            }
        }
        if (best == current) break;
        current = best;
        out_path[length++] = (Cell){ current % map->width, current / map->width };
    }
    return smooth_cell_path(map, out_path, length, radius);
}

void grid_to_screen(const App *app, float gx, float gy, float *sx, float *sy) {
    *sx = gx * (float)app_cell_w(app) + app->cam_x;
    *sy = gy * (float)app_cell_h(app) + app->cam_y;
}

static void screen_to_grid_point(const App *app, int sx, int sy, float *gx, float *gy) {
    if (gx) *gx = ((float)sx - app->cam_x) / (float)app_cell_w(app);
    if (gy) *gy = ((float)sy - app->cam_y) / (float)app_cell_h(app);
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

static void render_blocked_overlay(App *app, const GameMap *map) {
    if (!app || !map || !map->blocked) return;
    int cell_w = app_cell_w(app);
    int cell_h = app_cell_h(app);
    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(app->renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            if (!map->blocked[map_index(map, x, y)]) continue;
            float sx, sy;
            grid_to_screen(app, (float)x, (float)y, &sx, &sy);
            if (sx < -cell_w || sy < -cell_h ||
                sx > app->win_w + cell_w || sy > app->win_h + cell_h) {
                continue;
            }
            SDL_Rect r = { (int)sx, (int)sy, cell_w, cell_h };
            SDL_SetRenderDrawColor(app->renderer, 230, 45, 40, 92);
            SDL_RenderFillRect(app->renderer, &r);
            SDL_SetRenderDrawColor(app->renderer, 255, 205, 64, 180);
            SDL_RenderDrawRect(app->renderer, &r);
        }
    }
    SDL_SetRenderDrawBlendMode(app->renderer, old_blend);
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
            if ((map->render_features & MAP_RENDER_INTERLEAVED_OVERLAYS) == 0) {
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

    if (app->show_blocked) {
        render_blocked_overlay(app, map);
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

static int sprite_sequence_frame(const SpriteSheet *sprite, const char *sequence_name,
                                 int facing_code, int sequence_frame) {
    const SpriteSequence *seq = sprite_sequence_find(sprite, sequence_name);
    if (!seq || seq->facings <= 0 || seq->length <= 0) {
        debug_effects_log("sequence miss sprite_frames=%d sequence=%s facing=%d anim=%d",
                          sprite ? sprite->frame_count : 0,
                          sequence_name ? sequence_name : "(null)",
                          facing_code, sequence_frame);
        return -1;
    }
    int facing = sequence_facing_index(seq, facing_code);
    int anim = sequence_frame < 0 ? seq->length - 1 : sequence_frame;
    if (anim >= seq->length) anim = seq->length - 1;
    int frame_stride = seq->frame_stride > 0 ? seq->frame_stride : 1;
    int start = seq->frame_starts[facing];
    if (start < 0 || start >= sprite->frame_count) {
        debug_effects_log("sequence bad start sequence=%s facing=%d/%d code=%d start=%d frame_count=%d",
                          sequence_name, facing, seq->facings, facing_code, start,
                          sprite ? sprite->frame_count : 0);
        return -1;
    }
    int frame = start + anim * frame_stride;
    if (frame < 0) return start;
    if (frame >= sprite->frame_count) return sprite->frame_count - 1;
    return frame;
}

static const RtsState *rts_state_at(const RtsGameInfo *game_info, int state_id) {
    if (!game_info || !game_info->states || state_id < 0 || state_id >= game_info->state_count)
        return NULL;
    return &game_info->states[state_id];
}

static int rts_state_facing_slot(const RtsState *state, int facing_code) {
    if (!state || state->facings <= 0) return -1;
    int wrap = 16;
    for (int i = 0; i < state->facings && i < RTS_MAX_STATE_FACINGS; ++i) {
        if (state->direction_codes[i] > 7 || facing_code > 7) {
            wrap = 16;
            break;
        }
        wrap = 8;
    }
    int best = 0;
    int best_delta = 1000;
    for (int i = 0; i < state->facings && i < RTS_MAX_STATE_FACINGS; ++i) {
        int code = state->direction_codes[i];
        int delta = abs(code - facing_code);
        if (delta > wrap / 2) delta = wrap - delta;
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    return best;
}

static void rts_resolve_state_frame(const RtsState *state, int facing_code,
                                    int *frame_out, uint32_t *flags_out,
                                    int *offset_x_out, int *offset_y_out) {
    int frame = state ? state->frame : 0;
    uint32_t flags = state ? state->flags : 0;
    int offset_x = 0;
    int offset_y = 0;
    if (state && state->facings > 0) {
        int best = rts_state_facing_slot(state, facing_code);
        if (best < 0) best = 0;
        frame = state->facing_frames[best];
        flags = state->facing_flags[best];
        offset_x = state->offset_x[best];
        offset_y = state->offset_y[best];
    }
    if (frame_out) *frame_out = frame;
    if (flags_out) *flags_out = flags;
    if (offset_x_out) *offset_x_out = offset_x;
    if (offset_y_out) *offset_y_out = offset_y;
}

static void rts_apply_state_visuals(const RtsGameInfo *game_info, Unit *unit,
                                    const RtsState *state) {
    if (!game_info || !unit || !state) return;
    unit->sprite_id = state->sprite;
    rts_resolve_state_frame(state, unit->facing_code, &unit->frame, &unit->render_flags,
                            NULL, NULL);
    if (unit->sprite_id >= 0 && unit->sprite_id < game_info->sprite_count &&
        game_info->sprnames && game_info->sprnames[unit->sprite_id]) {
        snprintf(unit->sprite_name, sizeof(unit->sprite_name), "%s",
                 game_info->sprnames[unit->sprite_id]);
    }
}

static void rts_apply_effect_state_visuals(const RtsGameInfo *game_info, RtsVisualEffect *effect,
                                           const RtsState *state) {
    if (!game_info || !effect || !state) return;
    effect->sprite_id = state->sprite;
    rts_resolve_state_frame(state, effect->facing_code, &effect->frame, &effect->render_flags,
                            &effect->screen_offset_x, &effect->screen_offset_y);
    if (effect->sprite_id >= 0 && effect->sprite_id < game_info->sprite_count &&
        game_info->sprnames && game_info->sprnames[effect->sprite_id]) {
        snprintf(effect->sprite_name, sizeof(effect->sprite_name), "%s",
                 game_info->sprnames[effect->sprite_id]);
    }
}

bool rts_set_unit_state(RtsStateContext *ctx, Unit *unit, int state_id) {
    const RtsGameInfo *game_info = ctx ? ctx->game_info : NULL;
    if (!game_info || !unit) return false;
    int guard = 0;
    while (guard++ < game_info->state_count + 1) {
        if (state_id == game_info->null_state || state_id < 0 ||
            state_id >= game_info->state_count) {
            unit->state_id = game_info->null_state;
            unit->tics = 0;
            unit->remove = true;
            return false;
        }
        const RtsState *state = &game_info->states[state_id];
        unit->state_id = state_id;
        unit->tics = state->tics;
        rts_apply_state_visuals(game_info, unit, state);
        debug_effects_log("state unit type=%u state=%d sprite=%d frame=%d tics=%d",
                          unit->type_id, unit->state_id, unit->sprite_id, unit->frame, unit->tics);
        if (state->misc1 == 3) {
            int dir_slot = rts_state_facing_slot(state, unit->facing_code);
            const char *sprite_name = "(unknown)";
            if (unit->sprite_id >= 0 && unit->sprite_id < game_info->sprite_count &&
                game_info->sprnames && game_info->sprnames[unit->sprite_id]) {
                sprite_name = game_info->sprnames[unit->sprite_id];
            }
            debug_effects_log("shoot state unit_type=%u state=%d facing_code=%d dir_slot=%d sprite=%s frame=%d",
                              unit->type_id, unit->state_id, unit->facing_code,
                              dir_slot, sprite_name, unit->frame);
        }
        if (state->action) state->action(ctx, unit);
        if (unit->remove || unit->state_id != state_id) return !unit->remove;
        if (unit->tics != 0) return true;
        state_id = state->nextstate;
    }
    unit->remove = true;
    return false;
}

static bool rts_set_effect_state(const RtsGameInfo *game_info, RtsVisualEffect *effect,
                                 int state_id) {
    if (!game_info || !effect) return false;
    int guard = 0;
    while (guard++ < game_info->state_count + 1) {
        if (state_id == game_info->null_state || state_id < 0 ||
            state_id >= game_info->state_count) {
            memset(effect, 0, sizeof(*effect));
            return false;
        }
        const RtsState *state = &game_info->states[state_id];
        effect->state_id = state_id;
        effect->tics = state->tics;
        rts_apply_effect_state_visuals(game_info, effect, state);
        if (effect->tics != 0) return true;
        state_id = state->nextstate;
    }
    memset(effect, 0, sizeof(*effect));
    return false;
}

void rts_apply_mobjinfo_defaults(const RtsGameInfo *game_info, Unit *unit) {
    if (!game_info || !unit || !game_info->mobjinfo ||
        unit->type_id <= 0 || unit->type_id >= game_info->mobj_type_count) {
        return;
    }
    const RtsMobjInfo *info = &game_info->mobjinfo[unit->type_id];
    if (unit->max_hp <= 0) unit->max_hp = info->spawnhealth;
    if (unit->hp <= 0) unit->hp = unit->max_hp;
    if (unit->speed <= 0.0f) unit->speed = (float)info->speed;
    if (unit->radius <= 0.05f) {
        unit->radius = (float)info->radius / 32.0f;
        if (unit->radius < 0.32f) unit->radius = 0.32f;
        if (unit->radius > 0.90f) unit->radius = 0.90f;
    }
    if (unit->state_id <= 0) {
        RtsStateContext ctx = { .game_info = game_info };
        rts_set_unit_state(&ctx, unit, info->spawnstate);
    }
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
    SDL_Rect dst;
    if (dec->center_anchor) {
        grid_to_screen(app, (float)dec->gx + 0.5f, (float)dec->gy + 0.5f, &sx, &sy);
        dst = (SDL_Rect){ (int)(sx - sprite_w / 2), (int)(sy - sprite_h / 2), sprite_w, sprite_h };
    } else {
        dst = (SDL_Rect){
            (int)(sx + (float)(footprint_w * app_cell_w(app) - sprite_w) * 0.5f),
            (int)(sy + (float)(footprint_h * app_cell_h(app) - sprite_h)),
            sprite_w,
            sprite_h,
        };
    }
    if (dst.x > app->win_w || dst.y > app->win_h ||
        dst.x + dst.w < 0 || dst.y + dst.h < 0) {
        return;
    }
    int frame = -1;
    if (dec->sequence_name[0] != '\0') {
        frame = sprite_sequence_frame(sprite, dec->sequence_name, dec->facing_code,
                                      dec->frame_index);
    }
    if (frame < 0) frame = dec->frame_index >= 0 && dec->frame_index < sprite->frame_count ?
        dec->frame_index : 0;
    SDL_RendererFlip flip = (dec->render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_RenderCopyEx(app->renderer, sprite->texture, &sprite->frames[frame], &dst, 0.0, NULL, flip);
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

static float unit_pick_radius_px(const App *app, const Unit *unit) {
    float cell = ((float)app_cell_w(app) + (float)app_cell_h(app)) * 0.5f;
    float radius = unit_radius_cells(unit) * cell;
    float min_radius = 12.0f * (float)app_scale(app);
    return radius < min_radius ? min_radius : radius;
}

static bool circle_intersects_rect(float cx, float cy, float radius, SDL_Rect r) {
    float nearest_x = cx;
    float nearest_y = cy;
    if (nearest_x < (float)r.x) nearest_x = (float)r.x;
    if (nearest_x > (float)(r.x + r.w)) nearest_x = (float)(r.x + r.w);
    if (nearest_y < (float)r.y) nearest_y = (float)r.y;
    if (nearest_y > (float)(r.y + r.h)) nearest_y = (float)(r.y + r.h);
    float dx = cx - nearest_x;
    float dy = cy - nearest_y;
    return dx * dx + dy * dy <= radius * radius;
}

static int pick_unit_at(const App *app, const Unit *units, int unit_count, int x, int y,
                        int owner_filter) {
    int best = -1;
    float best_score = 1000000000.0f;
    for (int i = unit_count - 1; i >= 0; --i) {
        const Unit *unit = &units[i];
        if (unit->hp <= 0 || (unit->traits & RTS_TRAIT_SELECTABLE) == 0) continue;
        if (owner_filter >= 0 && unit->owner != owner_filter) continue;
        float sx = 0.0f, sy = 0.0f;
        grid_to_screen(app, unit->gx, unit->gy, &sx, &sy);
        float radius = unit_pick_radius_px(app, unit);
        float dx = (float)x - sx;
        float dy = (float)y - sy;
        float dist2 = dx * dx + dy * dy;
        if (dist2 > radius * radius || dist2 >= best_score) continue;
        best_score = dist2;
        best = i;
    }
    return best;
}

static int compass16_direction_code_from_vector(float dx, float dy) {
    if (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) return 0;
    float angle = atan2f(-dy, dx);
    const float quarter_turn = 0.7853981633974483f;
    int sector = (int)floorf(angle / quarter_turn + 0.5f);
    sector %= 8;
    if (sector < 0) sector += 8;
    return sector * 2;
}

static int dark_colony8_direction_code_from_vector(float dx, float dy) {
    if (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) return 0;
    const float quarter_turn = 0.7853981633974483f;
    int sector = (int)floorf(atan2f(dx, dy) / quarter_turn + 0.5f);
    sector %= 8;
    if (sector < 0) sector += 8;
    return sector;
}

static int dark_colony16_direction_code_from_vector(float dx, float dy) {
    if (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) return 0;
    const float sixteenth_turn = 0.39269908169872414f;
    int sector = (int)floorf(atan2f(dx, dy) / sixteenth_turn + 0.5f);
    sector %= 16;
    if (sector < 0) sector += 16;
    return sector;
}

int rts_direction_code_from_vector(const RtsGameInfo *game_info, float dx, float dy) {
    if (game_info && game_info->direction_mode == RTS_DIRECTION_DARK_COLONY_16)
        return dark_colony16_direction_code_from_vector(dx, dy);
    if (game_info && game_info->direction_mode == RTS_DIRECTION_DARK_COLONY_8)
        return dark_colony8_direction_code_from_vector(dx, dy);
    return compass16_direction_code_from_vector(dx, dy);
}

void rts_direction_vector_from_code(const RtsGameInfo *game_info, int code, float *dx, float *dy) {
    if (!dx || !dy) return;
    if (game_info && game_info->direction_mode == RTS_DIRECTION_DARK_COLONY_16) {
        float angle = (float)code * 0.39269908169872414f;
        *dx = sinf(angle);
        *dy = cosf(angle);
        return;
    }
    if (game_info && game_info->direction_mode == RTS_DIRECTION_DARK_COLONY_8) {
        static const float dirs[8][2] = {
            {  0.0f,  1.0f },
            {  0.70710678f,  0.70710678f },
            {  1.0f,  0.0f },
            {  0.70710678f, -0.70710678f },
            {  0.0f, -1.0f },
            { -0.70710678f, -0.70710678f },
            { -1.0f,  0.0f },
            { -0.70710678f,  0.70710678f },
        };
        int dir = code % 8;
        if (dir < 0) dir += 8;
        *dx = dirs[dir][0];
        *dy = dirs[dir][1];
        return;
    }
    float angle = -(float)code * 0.39269908169872414f;
    *dx = cosf(angle);
    *dy = -sinf(angle);
}

static void issue_move_order_at(const GameMap *map, Unit *units, int unit_count,
                                float goal_gx, float goal_gy) {
    int selected_count = 0;
    for (int i = 0; i < unit_count; ++i) {
        if (!units[i].selected) continue;
        if (units[i].hp <= 0) continue;
        if (units[i].owner != 0 || (units[i].traits & RTS_TRAIT_MOBILE) == 0) continue;
        selected_count++;
    }
    if (selected_count <= 0) return;

    Cell goal = { (int)floorf(goal_gx), (int)floorf(goal_gy) };
    if (!find_nearest_walkable_cell(map, goal, 8, &goal)) return;
    FlowCell *field = build_flow_field(map, goal);
    if (!field) return;

    int formation_columns = selected_count < 3 ? selected_count : 3;
    int formation_rows = (selected_count + formation_columns - 1) / formation_columns;
    int selected_index = 0;
    for (int i = 0; i < unit_count; ++i) {
        if (!units[i].selected) continue;
        if (units[i].hp <= 0) continue;
        if (units[i].owner != 0 || (units[i].traits & RTS_TRAIT_MOBILE) == 0) continue;
        units[i].harvest_target = -1;
        units[i].harvest_timer_ms = 0;
        Cell start = { (int)floorf(units[i].gx), (int)floorf(units[i].gy) };
        int len = flow_path_find(map, field, start, units[i].path, MAX_PATH_CELLS,
                                 unit_radius_cells(&units[i]));
        int row = selected_index / formation_columns;
        int row_start = row * formation_columns;
        int row_count = selected_count - row_start;
        if (row_count > formation_columns) row_count = formation_columns;
        int col = selected_index - row_start;
        float spacing = unit_radius_cells(&units[i]) * 1.8f;
        float offset_x = ((float)col - ((float)row_count - 1.0f) * 0.5f) * spacing;
        float offset_y = ((float)row - ((float)formation_rows - 1.0f) * 0.5f) * spacing;
        units[i].move_goal_gx = goal_gx + offset_x;
        units[i].move_goal_gy = goal_gy + offset_y;
        if (!unit_position_walkable(map, &units[i], units[i].move_goal_gx, units[i].move_goal_gy)) {
            float adjusted_gx = (float)goal.x + 0.5f;
            float adjusted_gy = (float)goal.y + 0.5f;
            find_nearest_walkable_position(map, adjusted_gx, adjusted_gy,
                                           unit_radius_cells(&units[i]), 8,
                                           &adjusted_gx, &adjusted_gy);
            units[i].move_goal_gx = adjusted_gx;
            units[i].move_goal_gy = adjusted_gy;
        }
        if (len == 1 && hypotf(units[i].move_goal_gx - units[i].gx,
                               units[i].move_goal_gy - units[i].gy) > 0.05f &&
            MAX_PATH_CELLS > 1) {
            units[i].path[1] = units[i].path[0];
            len = 2;
        }
        units[i].path_len = len;
        units[i].path_index = len > 1 ? 1 : 0;
        selected_index++;
    }
    free(field);
}

static int find_resource_vent_at(const GameMap *map, float gx, float gy) {
    if (!map || !map->resource_vents || map->resource_vent_count <= 0) return -1;
    int cell_x = (int)floorf(gx);
    int cell_y = (int)floorf(gy);
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const MapResourceVent *vent = &map->resource_vents[i];
        if (!vent->active || vent->rate <= 0 || vent->amount <= 0) continue;
        if (vent->gx == cell_x && vent->gy == cell_y) return i;
    }

    int best = -1;
    float best_dist2 = 1.45f * 1.45f;
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const MapResourceVent *vent = &map->resource_vents[i];
        if (!vent->active || vent->rate <= 0 || vent->amount <= 0) continue;
        float dx = ((float)vent->gx + 0.5f) - gx;
        float dy = ((float)vent->gy + 0.5f) - gy;
        float dist2 = dx * dx + dy * dy;
        if (dist2 < best_dist2) {
            best_dist2 = dist2;
            best = i;
        }
    }
    return best;
}

static bool issue_harvest_order_at(const GameMap *map, Unit *units, int unit_count,
                                   float gx, float gy) {
    int vent_index = find_resource_vent_at(map, gx, gy);
    if (vent_index < 0) return false;

    bool has_harvester = false;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].selected && units[i].owner == 0 && units[i].hp > 0 &&
            (units[i].traits & (RTS_TRAIT_MOBILE | RTS_TRAIT_HARVESTER)) ==
                (RTS_TRAIT_MOBILE | RTS_TRAIT_HARVESTER)) {
            has_harvester = true;
            break;
        }
    }
    if (!has_harvester) return false;

    const MapResourceVent *vent = &map->resource_vents[vent_index];
    bool issued = false;
    for (int i = 0; i < unit_count; ++i) {
        Unit *unit = &units[i];
        if (!unit->selected || unit->owner != 0 || unit->hp <= 0) continue;
        if ((unit->traits & (RTS_TRAIT_MOBILE | RTS_TRAIT_HARVESTER)) !=
            (RTS_TRAIT_MOBILE | RTS_TRAIT_HARVESTER)) {
            continue;
        }

        float goal_gx = (float)vent->gx + 0.5f;
        float goal_gy = (float)vent->gy + 0.5f;
        if (!find_nearest_walkable_position(map, goal_gx, goal_gy,
                                            unit_radius_cells(unit), 8,
                                            &goal_gx, &goal_gy)) {
            Cell fallback = { vent->gx, vent->gy };
            if (!find_nearest_walkable_cell(map, fallback, 8, &fallback)) continue;
            goal_gx = (float)fallback.x + 0.5f;
            goal_gy = (float)fallback.y + 0.5f;
        }

        Cell goal = { (int)floorf(goal_gx), (int)floorf(goal_gy) };
        if (!find_nearest_walkable_cell(map, goal, 8, &goal)) continue;
        FlowCell *field = build_flow_field(map, goal);
        if (!field) continue;

        Cell start = { (int)floorf(unit->gx), (int)floorf(unit->gy) };
        int len = flow_path_find(map, field, start, unit->path, MAX_PATH_CELLS,
                                 unit_radius_cells(unit));
        free(field);
        unit->move_goal_gx = goal_gx;
        unit->move_goal_gy = goal_gy;
        if (!unit_position_walkable(map, unit, unit->move_goal_gx, unit->move_goal_gy)) {
            float adjusted_gx = (float)goal.x + 0.5f;
            float adjusted_gy = (float)goal.y + 0.5f;
            find_nearest_walkable_position(map, adjusted_gx, adjusted_gy,
                                           unit_radius_cells(unit), 8,
                                           &adjusted_gx, &adjusted_gy);
            unit->move_goal_gx = adjusted_gx;
            unit->move_goal_gy = adjusted_gy;
        }
        if (len == 1 && hypotf(unit->move_goal_gx - unit->gx,
                               unit->move_goal_gy - unit->gy) > 0.05f &&
            MAX_PATH_CELLS > 1) {
            unit->path[1] = unit->path[0];
            len = 2;
        }
        unit->path_len = len;
        unit->path_index = len > 1 ? 1 : 0;
        unit->attack_target = -1;
        unit->harvest_target = vent_index;
        unit->harvest_timer_ms = 0;
        issued = true;
    }
    return issued;
}

void issue_move_order(const GameMap *map, Unit *units, int unit_count, Cell goal) {
    issue_move_order_at(map, units, unit_count, (float)goal.x + 0.5f, (float)goal.y + 0.5f);
}

static bool unit_has_attack_target_in_range(const Unit *attacker, const Unit *units, int unit_count,
                                            int *target_index_out) {
    if (target_index_out) *target_index_out = -1;
    if (!attacker || !units || unit_count <= 0 ||
        (attacker->traits & RTS_TRAIT_ATTACK) == 0 ||
        attacker->attack_damage <= 0 || attacker->attack_range <= 0.0f) {
        return false;
    }

    int preferred = attacker->attack_target;
    if (preferred >= 0 && preferred < unit_count) {
        const Unit *target = &units[preferred];
        if (!target->remove && target->hp > 0 && target->owner != attacker->owner) {
            float dx = target->gx - attacker->gx;
            float dy = target->gy - attacker->gy;
            if (dx * dx + dy * dy <= attacker->attack_range * attacker->attack_range) {
                if (target_index_out) *target_index_out = preferred;
                return true;
            }
        }
    }

    int best = -1;
    float best_dist2 = attacker->attack_range * attacker->attack_range;
    for (int i = 0; i < unit_count; ++i) {
        const Unit *candidate = &units[i];
        if (candidate == attacker || candidate->remove || candidate->hp <= 0 ||
            candidate->owner == attacker->owner) {
            continue;
        }
        float dx = candidate->gx - attacker->gx;
        float dy = candidate->gy - attacker->gy;
        float dist2 = dx * dx + dy * dy;
        if (dist2 <= best_dist2) {
            best_dist2 = dist2;
            best = i;
        }
    }
    if (best < 0) return false;
    if (target_index_out) *target_index_out = best;
    return true;
}

static bool spawn_visual_effect(RtsVisualEffect *effects, int max_effects,
                                const char *sprite_name, const char *sequence_name,
                                float gx, float gy, int facing_code, int duration_ms,
                                int frame_ms, bool add_decoration_on_finish,
                                int decoration_frame_index) {
    if (!effects || max_effects <= 0 || !sprite_name || sprite_name[0] == '\0') {
        debug_effects_log("spawn skipped sprite=%s max=%d", sprite_name ? sprite_name : "(null)", max_effects);
        return false;
    }
    for (int i = 0; i < max_effects; ++i) {
        RtsVisualEffect *effect = &effects[i];
        if (effect->active) continue;
        memset(effect, 0, sizeof(*effect));
        effect->active = true;
        effect->gx = gx;
        effect->gy = gy;
        effect->facing_code = facing_code;
        effect->duration_ms = duration_ms > 0 ? duration_ms : 120;
        effect->frame_ms = frame_ms > 0 ? frame_ms : 90;
        effect->decoration_frame_index = decoration_frame_index;
        effect->add_decoration_on_finish = add_decoration_on_finish;
        snprintf(effect->sprite_name, sizeof(effect->sprite_name), "%s", sprite_name);
        if (sequence_name && sequence_name[0] != '\0') {
            snprintf(effect->sequence_name, sizeof(effect->sequence_name), "%s", sequence_name);
        }
        debug_effects_log("spawn slot=%d sprite=%s sequence=%s pos=%.2f,%.2f facing=%d duration=%d frame_ms=%d corpse=%d",
                          i, effect->sprite_name,
                          effect->sequence_name[0] ? effect->sequence_name : "(none)",
                          effect->gx, effect->gy, effect->facing_code,
                          effect->duration_ms, effect->frame_ms,
                          effect->add_decoration_on_finish ? 1 : 0);
        return true;
    }
    debug_effects_log("spawn failed no free slot sprite=%s sequence=%s",
                      sprite_name, sequence_name ? sequence_name : "(none)");
    return false;
}

bool rts_spawn_state_effect(RtsStateContext *ctx, int state_id, float gx, float gy, int facing_code) {
    if (!ctx || !ctx->effects || ctx->max_effects <= 0 || !ctx->game_info) return false;
    for (int i = 0; i < ctx->max_effects; ++i) {
        RtsVisualEffect *effect = &ctx->effects[i];
        if (effect->active) continue;
        memset(effect, 0, sizeof(*effect));
        effect->active = true;
        effect->use_state = true;
        effect->gx = gx;
        effect->gy = gy;
        effect->facing_code = facing_code;
        bool ok = rts_set_effect_state(ctx->game_info, effect, state_id);
        debug_effects_log("spawn state effect slot=%d state=%d ok=%d sprite=%s frame=%d",
                          i, state_id, ok ? 1 : 0, effect->sprite_name, effect->frame);
        return ok;
    }
    debug_effects_log("spawn state effect failed no free slot state=%d", state_id);
    return false;
}

static const char *death_sequence_name_for_unit(const Unit *unit) {
    (void)unit;
    return "die";
}

static void add_effect_finish_decoration(GameMap *map, const RtsVisualEffect *effect) {
    if (!map || !effect || !effect->add_decoration_on_finish ||
        effect->sprite_name[0] == '\0' || map->decoration_count >= MAX_DECORATIONS) {
        return;
    }
    MapDecoration *decorations = realloc(map->decorations,
                                         (size_t)(map->decoration_count + 1) * sizeof(MapDecoration));
    if (!decorations) return;
    map->decorations = decorations;
    MapDecoration *dec = &map->decorations[map->decoration_count++];
    memset(dec, 0, sizeof(*dec));
    dec->gx = (int)floorf(effect->gx);
    dec->gy = (int)floorf(effect->gy);
    dec->footprint_w = 1;
    dec->footprint_h = 1;
    dec->center_anchor = true;
    dec->frame_index = effect->decoration_frame_index;
    dec->facing_code = effect->facing_code;
    snprintf(dec->sprite_name, sizeof(dec->sprite_name), "%s", effect->sprite_name);
    snprintf(dec->sequence_name, sizeof(dec->sequence_name), "%s", effect->sequence_name);
    debug_effects_log("corpse decoration sprite=%s sequence=%s grid=%d,%d facing=%d frame_index=%d count=%d",
                      dec->sprite_name, dec->sequence_name, dec->gx, dec->gy,
                      dec->facing_code, dec->frame_index, map->decoration_count);
}

bool rts_unit_add_corpse_decoration(RtsStateContext *ctx, const Unit *unit) {
    if (!ctx || !ctx->map || !unit || unit->sprite_name[0] == '\0' ||
        ctx->map->decoration_count >= MAX_DECORATIONS) {
        return false;
    }
    MapDecoration *decorations = realloc(ctx->map->decorations,
                                         (size_t)(ctx->map->decoration_count + 1) * sizeof(MapDecoration));
    if (!decorations) return false;
    ctx->map->decorations = decorations;
    MapDecoration *dec = &ctx->map->decorations[ctx->map->decoration_count++];
    memset(dec, 0, sizeof(*dec));
    dec->gx = (int)floorf(unit->gx);
    dec->gy = (int)floorf(unit->gy);
    dec->footprint_w = 1;
    dec->footprint_h = 1;
    dec->center_anchor = true;
    dec->frame_index = unit->frame;
    dec->facing_code = unit->facing_code;
    dec->render_flags = unit->render_flags;
    snprintf(dec->sprite_name, sizeof(dec->sprite_name), "%s", unit->sprite_name);
    debug_effects_log("corpse unit sprite=%s frame=%d flags=%u grid=%d,%d count=%d",
                      dec->sprite_name, dec->frame_index, dec->render_flags,
                      dec->gx, dec->gy, ctx->map->decoration_count);
    return true;
}

bool rts_unit_fire_attack(RtsStateContext *ctx, Unit *attacker) {
    if (!ctx || !attacker || !ctx->units || !ctx->unit_count) return false;
    int count = *ctx->unit_count;
    int target_index = attacker->attack_target;
    if (target_index < 0 || target_index >= count || ctx->units[target_index].hp <= 0 ||
        ctx->units[target_index].owner == attacker->owner) {
        target_index = -1;
        float best_dist2 = attacker->attack_range * attacker->attack_range;
        for (int i = 0; i < count; ++i) {
            Unit *candidate = &ctx->units[i];
            if (candidate == attacker || candidate->hp <= 0 || candidate->owner == attacker->owner)
                continue;
            float dx = candidate->gx - attacker->gx;
            float dy = candidate->gy - attacker->gy;
            float dist2 = dx * dx + dy * dy;
            if (dist2 <= best_dist2) {
                best_dist2 = dist2;
                target_index = i;
            }
        }
    }
    if (target_index < 0) return false;

    Unit *target = &ctx->units[target_index];
    const RtsState *attack_state = rts_state_at(ctx->game_info, attacker->state_id);
    int dir_slot = rts_state_facing_slot(attack_state, attacker->facing_code);
    const char *sprite_name = "(unknown)";
    if (ctx->game_info && attacker->sprite_id >= 0 &&
        attacker->sprite_id < ctx->game_info->sprite_count &&
        ctx->game_info->sprnames && ctx->game_info->sprnames[attacker->sprite_id]) {
        sprite_name = ctx->game_info->sprnames[attacker->sprite_id];
    }
    debug_effects_log("shoot fire unit_type=%u state=%d facing_code=%d dir_slot=%d sprite=%s frame=%d target=%d",
                      attacker->type_id, attacker->state_id, attacker->facing_code,
                      dir_slot, sprite_name, attacker->frame, target_index);
    target->hp -= attacker->attack_damage;
    if (attacker->attack_cooldown_ms > 0)
        attacker->attack_cooldown_left_ms = attacker->attack_cooldown_ms;
    debug_effects_log("state attack attacker_type=%u target=%d damage=%d hp=%d/%d",
                      attacker->type_id, target_index, attacker->attack_damage,
                      target->hp, target->max_hp);
    if (target->hp <= 0) {
        target->hp = 0;
        target->selected = false;
        target->traits &= ~(RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                            RTS_TRAIT_ATTACK | RTS_TRAIT_HARVESTER);
        target->path_len = 0;
        target->path_index = 0;
        target->harvest_target = -1;
        target->harvest_timer_ms = 0;
        if (ctx->game_info && target->type_id > 0 &&
            target->type_id < ctx->game_info->mobj_type_count) {
            int deathstate = ctx->game_info->mobjinfo[target->type_id].deathstate;
            rts_set_unit_state(ctx, target, deathstate);
        } else {
            target->remove = true;
        }
    }
    return true;
}

void update_visual_effects(GameMap *map, RtsVisualEffect *effects, int max_effects,
                           const RtsGameInfo *game_info, float dt) {
    if (!effects || max_effects <= 0) return;
    int dt_ms = (int)lroundf(dt * 1000.0f);
    for (int i = 0; i < max_effects; ++i) {
        RtsVisualEffect *effect = &effects[i];
        if (!effect->active) continue;
        if (effect->use_state && game_info) {
            if (effect->tics > 0) effect->tics--;
            if (effect->tics == 0) {
                const RtsState *state = rts_state_at(game_info, effect->state_id);
                int next = state ? state->nextstate : game_info->null_state;
                rts_set_effect_state(game_info, effect, next);
            }
            continue;
        }
        effect->age_ms += dt_ms;
        if (effect->age_ms < effect->duration_ms) continue;
        debug_effects_log("finish sprite=%s sequence=%s age=%d duration=%d corpse=%d",
                          effect->sprite_name,
                          effect->sequence_name[0] ? effect->sequence_name : "(none)",
                          effect->age_ms, effect->duration_ms,
                          effect->add_decoration_on_finish ? 1 : 0);
        add_effect_finish_decoration(map, effect);
        memset(effect, 0, sizeof(*effect));
    }
}

static void separate_units(const GameMap *map, Unit *units, int count) {
    if (!units || count <= 1) return;
    for (int iter = 0; iter < 3; ++iter) {
        for (int i = 0; i < count; ++i) {
            Unit *a = &units[i];
            if (a->remove || a->hp <= 0 || (a->traits & RTS_TRAIT_MOBILE) == 0) continue;
            for (int j = i + 1; j < count; ++j) {
                Unit *b = &units[j];
                if (b->remove || b->hp <= 0 || (b->traits & RTS_TRAIT_MOBILE) == 0) continue;
                float min_dist = unit_radius_cells(a) + unit_radius_cells(b);
                float dx = b->gx - a->gx;
                float dy = b->gy - a->gy;
                float dist2 = dx * dx + dy * dy;
                if (dist2 >= min_dist * min_dist) continue;
                float dist = sqrtf(dist2);
                if (dist < 0.0001f) {
                    float angle = (float)((i * 37 + j * 17) % 360) * 0.01745329252f;
                    dx = cosf(angle);
                    dy = sinf(angle);
                    dist = 1.0f;
                }
                float push = (min_dist - dist) * 0.5f;
                float nx = dx / dist;
                float ny = dy / dist;
                float ax = a->gx - nx * push;
                float ay = a->gy - ny * push;
                float bx = b->gx + nx * push;
                float by = b->gy + ny * push;
                if (unit_position_walkable(map, a, ax, ay)) {
                    a->gx = ax;
                    a->gy = ay;
                    clamp_unit_position_to_map(map, a);
                }
                if (unit_position_walkable(map, b, bx, by)) {
                    b->gx = bx;
                    b->gy = by;
                    clamp_unit_position_to_map(map, b);
                }
            }
        }
    }
}

static bool move_unit_if_walkable(const GameMap *map, Unit *unit, float gx, float gy) {
    if (!unit) return false;
    if (unit_position_walkable(map, unit, gx, gy)) {
        unit->gx = gx;
        unit->gy = gy;
        return true;
    }
    if (unit_position_walkable(map, unit, gx, unit->gy)) {
        unit->gx = gx;
        return true;
    }
    if (unit_position_walkable(map, unit, unit->gx, gy)) {
        unit->gy = gy;
        return true;
    }
    return false;
}

static float unit_harvest_interaction_radius_cells(const Unit *unit) {
    float radius = unit_radius_cells(unit) + 1.25f;
    return radius < 1.25f ? 1.25f : radius;
}

static bool update_unit_harvest(GameMap *map, Unit *unit, int dt_ms,
                                const RtsGameInfo *game_info) {
    if (!map || !unit || (unit->traits & RTS_TRAIT_HARVESTER) == 0 ||
        unit->harvest_target < 0) {
        return false;
    }
    if (unit->harvest_target >= map->resource_vent_count || !map->resource_vents) {
        unit->harvest_target = -1;
        unit->harvest_timer_ms = 0;
        return false;
    }

    MapResourceVent *vent = &map->resource_vents[unit->harvest_target];
    if (!vent->active || vent->rate <= 0 || vent->amount <= 0) {
        unit->harvest_target = -1;
        unit->harvest_timer_ms = 0;
        return false;
    }

    float dx = ((float)vent->gx + 0.5f) - unit->gx;
    float dy = ((float)vent->gy + 0.5f) - unit->gy;
    float interaction_radius = unit_harvest_interaction_radius_cells(unit);
    if (dx * dx + dy * dy > interaction_radius * interaction_radius) return false;

    unit->path_len = 0;
    unit->path_index = 0;
    unit->attack_target = -1;
    if (game_info && unit->harvest_state_id > 0 &&
        unit->harvest_state_id < game_info->state_count) {
        const RtsState *state = rts_state_at(game_info, unit->state_id);
        if (!state || state->misc1 != 5) {
            unit->facing_code = (((float)vent->gx + 0.5f) < unit->gx) ? 14 : 2;
            RtsStateContext ctx = { .map = map, .game_info = game_info };
            rts_set_unit_state(&ctx, unit, unit->harvest_state_id);
        }
    }
    unit->harvest_timer_ms += dt_ms;
    while (unit->harvest_timer_ms >= RTS_HARVEST_INTERVAL_MS && vent->amount > 0) {
        unit->harvest_timer_ms -= RTS_HARVEST_INTERVAL_MS;
        int take = vent->rate;
        if (take > vent->amount) take = vent->amount;
        vent->amount -= take;
        int owner = unit->owner < 8 ? unit->owner : 0;
        map->player_resources[owner] += take;
        if (vent->amount <= 0) {
            vent->active = false;
            unit->harvest_target = -1;
            unit->harvest_timer_ms = 0;
            break;
        }
    }
    return true;
}

void update_units(GameMap *map, Unit *units, int *unit_count, RtsVisualEffect *effects,
                  int max_effects, const RtsGameInfo *game_info, float dt) {
    if (game_info) {
        if (!units || !unit_count || *unit_count <= 0) return;
        int count = *unit_count;
        int dt_ms = (int)lroundf(dt * 1000.0f);
        RtsStateContext ctx = {
            .map = map,
            .units = units,
            .unit_count = unit_count,
            .effects = effects,
            .max_effects = max_effects,
            .game_info = game_info,
        };

        for (int i = 0; i < count; ++i) {
            Unit *u = &units[i];
            if (u->remove) continue;
            if (u->state_id <= 0) rts_apply_mobjinfo_defaults(game_info, u);
            if (u->tics > 0) {
                u->tics--;
                if (u->tics == 0) {
                    const RtsState *state = rts_state_at(game_info, u->state_id);
                    int next = state ? state->nextstate : game_info->null_state;
                    rts_set_unit_state(&ctx, u, next);
                }
            }
            if (u->remove || u->hp <= 0) continue;

            if (u->attack_cooldown_left_ms > 0) {
                u->attack_cooldown_left_ms -= dt_ms;
                if (u->attack_cooldown_left_ms < 0) u->attack_cooldown_left_ms = 0;
            }
            if (u->attack_anim_left_ms > 0) {
                u->attack_anim_left_ms -= dt_ms;
                if (u->attack_anim_left_ms < 0) u->attack_anim_left_ms = 0;
            }

            bool moving = u->path_index > 0 && u->path_index < u->path_len;
            if (moving && u->attack_anim_left_ms <= 0) {
                int stop_target = -1;
                if (unit_has_attack_target_in_range(u, units, count, &stop_target)) {
                    u->attack_target = stop_target;
                    Unit *target = &units[stop_target];
                    u->facing_code = rts_direction_code_from_vector(game_info,
                                                                    target->gx - u->gx,
                                                                    target->gy - u->gy);
                    u->path_len = 0;
                    u->path_index = 0;
                    moving = false;
                }
            }
            if (moving) {
                Cell c = u->path[u->path_index];
                bool final = u->path_index == u->path_len - 1;
                float tx = final ? u->move_goal_gx : (float)c.x + 0.5f;
                float ty = final ? u->move_goal_gy : (float)c.y + 0.5f;
                float dx = tx - u->gx;
                float dy = ty - u->gy;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist >= 0.001f)
                    u->facing_code = rts_direction_code_from_vector(game_info, dx, dy);
                float step = u->speed * dt;
                if (dist <= step || dist < 0.001f) {
                    if (move_unit_if_walkable(map, u, tx, ty)) {
                        u->path_index++;
                        if (u->path_index >= u->path_len) {
                            u->path_len = 0;
                            u->path_index = 0;
                            moving = false;
                        }
                    } else {
                        u->path_len = 0;
                        u->path_index = 0;
                        moving = false;
                    }
                } else {
                    float nx = u->gx + dx / dist * step;
                    float ny = u->gy + dy / dist * step;
                    if (!move_unit_if_walkable(map, u, nx, ny)) {
                        u->path_len = 0;
                        u->path_index = 0;
                        moving = false;
                    }
                }
            }

            if (update_unit_harvest(map, u, dt_ms, game_info)) {
                moving = false;
            }

            if (u->type_id > 0 && u->type_id < game_info->mobj_type_count &&
                u->attack_anim_left_ms <= 0) {
                const RtsMobjInfo *mi = &game_info->mobjinfo[u->type_id];
                const RtsState *state = rts_state_at(game_info, u->state_id);
                int group = state ? state->misc1 : 0;
                if (moving && group != 2) {
                    rts_set_unit_state(&ctx, u, mi->seestate);
                } else if (!moving && group == 2) {
                    rts_set_unit_state(&ctx, u, mi->spawnstate);
                } else {
                    rts_apply_state_visuals(game_info, u, rts_state_at(game_info, u->state_id));
                }
            }
        }

        separate_units(map, units, count);

        for (int i = 0; i < count; ++i) {
            Unit *attacker = &units[i];
            if (attacker->remove || attacker->hp <= 0 ||
                (attacker->traits & RTS_TRAIT_ATTACK) == 0 ||
                attacker->attack_damage <= 0 || attacker->attack_range <= 0.0f ||
                attacker->type_id <= 0 || attacker->type_id >= game_info->mobj_type_count) {
                continue;
            }
            const RtsMobjInfo *mi = &game_info->mobjinfo[attacker->type_id];
            const RtsState *state = rts_state_at(game_info, attacker->state_id);
            if (state && state->misc1 == 3) continue;

            int target_index = -1;
            float best_dist2 = attacker->attack_range * attacker->attack_range;
            for (int j = 0; j < count; ++j) {
                if (i == j || units[j].remove || units[j].hp <= 0 ||
                    units[j].owner == attacker->owner) {
                    continue;
                }
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
            attacker->facing_code = rts_direction_code_from_vector(game_info,
                                                                   target->gx - attacker->gx,
                                                                   target->gy - attacker->gy);
            if (attacker->attack_cooldown_left_ms > 0 ||
                attacker->attack_anim_left_ms > 0 ||
                mi->missilestate == game_info->null_state) {
                continue;
            }
            attacker->attack_anim_left_ms = attacker->attack_anim_ms > 0 ? attacker->attack_anim_ms : 240;
            rts_set_unit_state(&ctx, attacker, mi->missilestate);
        }

        int write = 0;
        for (int read = 0; read < count; ++read) {
            if (units[read].remove) continue;
            if (write != read) units[write] = units[read];
            write++;
        }
        if (write != count) debug_effects_log("state compacted units before=%d after=%d", count, write);
        *unit_count = write;
        return;
    }

    if (!units || !unit_count || *unit_count <= 0) return;
    int count = *unit_count;
    int dt_ms = (int)lroundf(dt * 1000.0f);
    for (int i = 0; i < count; ++i) {
        Unit *u = &units[i];
        if (u->hp <= 0) {
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
        if (u->path_index > 0 && u->path_index < u->path_len &&
            u->attack_anim_left_ms <= 0) {
            int stop_target = -1;
            if (unit_has_attack_target_in_range(u, units, count, &stop_target)) {
                u->attack_target = stop_target;
                Unit *target = &units[stop_target];
                u->facing_code = rts_direction_code_from_vector(NULL,
                                                                target->gx - u->gx,
                                                                target->gy - u->gy);
                u->path_len = 0;
                u->path_index = 0;
            }
        }
        if (u->path_index <= 0 || u->path_index >= u->path_len) {
            update_unit_harvest(map, u, dt_ms, NULL);
            continue;
        }
        Cell c = u->path[u->path_index];
        bool final = u->path_index == u->path_len - 1;
        float tx = final ? u->move_goal_gx : (float)c.x + 0.5f;
        float ty = final ? u->move_goal_gy : (float)c.y + 0.5f;
        float dx = tx - u->gx;
        float dy = ty - u->gy;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist >= 0.001f) u->facing_code = rts_direction_code_from_vector(NULL, dx, dy);
        float step = u->speed * dt;
        if (dist <= step || dist < 0.001f) {
            if (move_unit_if_walkable(map, u, tx, ty)) {
                u->path_index++;
                if (u->path_index >= u->path_len) {
                    u->path_len = 0;
                    u->path_index = 0;
                }
            } else {
                u->path_len = 0;
                u->path_index = 0;
            }
        } else {
            float nx = u->gx + dx / dist * step;
            float ny = u->gy + dy / dist * step;
            if (!move_unit_if_walkable(map, u, nx, ny)) {
                u->path_len = 0;
                u->path_index = 0;
            }
        }
        (void)update_unit_harvest(map, u, dt_ms, NULL);
    }

    separate_units(map, units, count);

    for (int i = 0; i < count; ++i) {
        Unit *attacker = &units[i];
        if (attacker->hp <= 0 || (attacker->traits & RTS_TRAIT_ATTACK) == 0 ||
            attacker->attack_damage <= 0 || attacker->attack_range <= 0.0f) {
            continue;
        }

        int target_index = -1;
        float best_dist2 = attacker->attack_range * attacker->attack_range;
        for (int j = 0; j < count; ++j) {
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
        attacker->facing_code = rts_direction_code_from_vector(NULL,
                                                               target->gx - attacker->gx,
                                                               target->gy - attacker->gy);
        if (attacker->attack_cooldown_left_ms > 0) continue;

        if (attacker->muzzle_flash_name[0] != '\0') {
            float vx = 0.0f, vy = 0.0f;
            rts_direction_vector_from_code(NULL, attacker->facing_code, &vx, &vy);
            bool spawned = spawn_visual_effect(effects, max_effects, attacker->muzzle_flash_name, "flash",
                                               attacker->gx + vx * 0.42f, attacker->gy + vy * 0.42f,
                                               attacker->facing_code,
                                               attacker->muzzle_flash_ms > 0 ? attacker->muzzle_flash_ms : 120,
                                               40, false, 0);
            debug_effects_log("attack muzzle attacker=%d target=%d spawned=%d sprite=%s",
                              i, target_index, spawned ? 1 : 0, attacker->muzzle_flash_name);
        }
        target->hp -= attacker->attack_damage;
        debug_effects_log("attack damage attacker=%d target=%d damage=%d hp=%d/%d target_sprite=%s",
                          i, target_index, attacker->attack_damage, target->hp,
                          target->max_hp, target->sprite_name);
        if (target->hit_effect_name[0] != '\0') {
            spawn_visual_effect(effects, max_effects, target->hit_effect_name, NULL,
                                target->gx, target->gy, target->facing_code,
                                400, 50, false, 0);
        }
        if (target->hp <= 0) {
            target->hp = 0;
            target->selected = false;
            target->traits &= ~(RTS_TRAIT_SELECTABLE | RTS_TRAIT_MOBILE |
                                RTS_TRAIT_ATTACK | RTS_TRAIT_HARVESTER);
            target->path_len = 0;
            target->path_index = 0;
            target->attack_target = -1;
            target->harvest_target = -1;
            target->harvest_timer_ms = 0;
            target->attack_cooldown_left_ms = 0;
            target->attack_anim_left_ms = 0;
            target->death_started = true;
            if (target->death_anim_ms <= 0) {
                target->death_anim_ms = 900;
            }
            target->death_anim_left_ms = target->death_anim_ms;
            bool spawned = spawn_visual_effect(effects, max_effects, target->sprite_name,
                                               death_sequence_name_for_unit(target),
                                               target->gx, target->gy, target->facing_code,
                                               target->death_anim_ms, 90, true, -1);
            debug_effects_log("death target=%d spawned=%d sprite=%s sequence=%s facing=%d duration=%d",
                              target_index, spawned ? 1 : 0, target->sprite_name,
                              death_sequence_name_for_unit(target), target->facing_code,
                              target->death_anim_ms);
        }
        attacker->attack_cooldown_left_ms = attacker->attack_cooldown_ms > 0 ?
            attacker->attack_cooldown_ms : 500;
        attacker->attack_anim_left_ms = attacker->attack_anim_ms > 0 ?
            attacker->attack_anim_ms : attacker->attack_cooldown_left_ms;
    }

    int write = 0;
    for (int read = 0; read < count; ++read) {
        if (units[read].hp <= 0) continue;
        if (write != read) units[write] = units[read];
        write++;
    }
    if (write != count) {
        debug_effects_log("compacted units before=%d after=%d", count, write);
    }
    *unit_count = write;
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
            bool final = unit->path_index == unit->path_len - 1;
            float tx = final ? unit->move_goal_gx : (float)c.x + 0.5f;
            float ty = final ? unit->move_goal_gy : (float)c.y + 0.5f;
            float dx = tx - unit->gx;
            float dy = ty - unit->gy;
            direction_code = rts_direction_code_from_vector(NULL, dx, dy);
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

static SDL_Rect sprite_visible_bounds(const SpriteSheet *sprite, int frame) {
    if (sprite && sprite->frame_bounds && frame >= 0 && frame < sprite->frame_count) {
        SDL_Rect r = sprite->frame_bounds[frame];
        if (r.w > 0 && r.h > 0) return r;
    }
    return (SDL_Rect){ 0, 0, sprite ? sprite->frame_w : 1, sprite ? sprite->frame_h : 1 };
}

static void draw_selection_ellipse(App *app, float cx, float cy, float rx, float ry,
                                   SDL_Color color) {
    if (!app || !app->renderer || rx <= 0.0f || ry <= 0.0f) return;
    SDL_SetRenderDrawColor(app->renderer, color.r, color.g, color.b, color.a);
    const int segments = 40;
    float prev_x = cx + rx;
    float prev_y = cy;
    for (int i = 1; i <= segments; ++i) {
        float a = ((float)i / (float)segments) * 6.283185307179586f;
        float x = cx + cosf(a) * rx;
        float y = cy + sinf(a) * ry;
        SDL_RenderDrawLine(app->renderer, (int)lroundf(prev_x), (int)lroundf(prev_y),
                           (int)lroundf(x), (int)lroundf(y));
        prev_x = x;
        prev_y = y;
    }
}

static void render_unit_sprite(App *app, const Unit *u, const SpriteSheet *fallback_sprite,
                               const SpriteCache *cache, const RtsGameInfo *game_info,
                               uint32_t ticks) {
    if (!u || (u->traits & RTS_TRAIT_RENDERABLE) == 0) return;
    const char *sprite_name = u->sprite_name;
    if (game_info && u->sprite_id >= 0 && u->sprite_id < game_info->sprite_count &&
        game_info->sprnames && game_info->sprnames[u->sprite_id]) {
        sprite_name = game_info->sprnames[u->sprite_id];
    }
    const SpriteSheet *sprite = sprite_cache_lookup(cache, sprite_name);
    if (!sprite) sprite = fallback_sprite;
    if (!sprite || !sprite->texture || sprite->frame_count <= 0) return;

    float sx, sy;
    grid_to_screen(app, u->gx, u->gy, &sx, &sy);
    int frame = game_info ? u->frame : sprite_frame_for_unit(sprite, u, ticks);
    uint32_t render_flags = game_info ? u->render_flags : 0;
    if (frame >= sprite->frame_count) frame = 0;
    if (frame < 0) frame = 0;
    SDL_Rect bounds = sprite_visible_bounds(sprite, frame);
    int scale = app_scale(app);
    int sprite_w = sprite->frame_w * scale;
    int sprite_h = sprite->frame_h * scale;
    float content_w = (float)bounds.w * (float)scale;
    float rx = unit_radius_cells(u) * (float)app_cell_w(app);
    float min_rx = content_w * 0.34f;
    if (rx < min_rx) rx = min_rx;
    float ry = rx * 0.38f;
    float ground_offset_y = ((float)bounds.y + (float)bounds.h) * (float)scale - ry * 0.35f;
    SDL_Rect dst = {
        (int)(sx - sprite_w / 2),
        (int)(sy - ground_offset_y),
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
    float content_y = (float)dst.y + (float)bounds.y * (float)scale;
    if (u->selected && (u->traits & RTS_TRAIT_SELECTABLE) != 0) {
        draw_selection_ellipse(app, sx, sy, rx + 2.0f, ry + 1.0f, (SDL_Color){ 15, 35, 30, 180 });
        draw_selection_ellipse(app, sx, sy, rx, ry, (SDL_Color){ 98, 224, 161, 255 });
    }
    SDL_RendererFlip flip = (render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_RenderCopyEx(app->renderer, sprite->texture, &sprite->frames[frame], &dst, 0.0, NULL, flip);
    if (u->max_hp > 0 && u->hp > 0 && u->hp < u->max_hp) {
        int bar_w = sprite_w / 2;
        int bar_h = app_scale(app) < 2 ? 2 : 3;
        int bx = (int)(sx - bar_w / 2);
        int by = (int)content_y - bar_h - 4;
        SDL_Rect back = { bx, by, bar_w, bar_h };
        SDL_Rect fill = { bx, by, (bar_w * u->hp) / u->max_hp, bar_h };
        SDL_SetRenderDrawColor(app->renderer, 40, 20, 20, 220);
        SDL_RenderFillRect(app->renderer, &back);
        SDL_SetRenderDrawColor(app->renderer, 98, 224, 161, 230);
        SDL_RenderFillRect(app->renderer, &fill);
    }
}

void render_units(App *app, const Unit *units, int unit_count, const SpriteSheet *fallback_sprite,
                  const SpriteCache *cache, const RtsGameInfo *game_info, uint32_t ticks) {
    for (int i = 0; i < unit_count; ++i) {
        render_unit_sprite(app, &units[i], fallback_sprite, cache, game_info, ticks);
    }
}

typedef enum {
    RENDER_ITEM_OVERLAY,
    RENDER_ITEM_DECORATION,
    RENDER_ITEM_UNIT,
} RenderItemKind;

typedef struct {
    RenderItemKind kind;
    float sort_y;
    int index;
    int x;
    int y;
    int layer;
} RenderItem;

static int compare_render_items(const void *a, const void *b) {
    const RenderItem *ia = a;
    const RenderItem *ib = b;
    if (ia->sort_y < ib->sort_y) return -1;
    if (ia->sort_y > ib->sort_y) return 1;
    if (ia->kind != ib->kind) return (int)ia->kind - (int)ib->kind;
    return ia->index - ib->index;
}

static void render_overlay_tile_item(App *app, const GameMap *map, const Tileset *tileset,
                                     int x, int y, int layer) {
    if (!app || !map || !tileset || layer < 0 || layer >= map->tile_overlay_count ||
        layer >= MAX_TILE_OVERLAYS || !map->tile_overlays[layer]) {
        return;
    }
    int idx = map_index(map, x, y);
    int overlay = map->tile_overlays[layer][idx];
    if (overlay <= 0) return;

    int tile_w = app_tile_w(app, tileset);
    int tile_h = app_tile_h(app, tileset);
    int draw_y_offset = tileset->draw_y_offset * app_scale(app);
    float sx, sy;
    grid_to_screen(app, (float)x, (float)y, &sx, &sy);
    if (sx < -tile_w || sy < -tile_h ||
        sx > app->win_w + tile_w || sy > app->win_h + tile_h) {
        return;
    }
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

void render_world_objects(App *app, const GameMap *map, const Tileset *tileset,
                          const Unit *units, int unit_count, const SpriteSheet *fallback_sprite,
                          const SpriteCache *cache, const RtsGameInfo *game_info, uint32_t ticks) {
    if (!app || !map) return;
    int overlay_count = 0;
    if (map->render_features & MAP_RENDER_INTERLEAVED_OVERLAYS) {
        for (int layer = 0; layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS; ++layer) {
            if (!map->tile_overlays[layer]) continue;
            for (int y = 0; y < map->height; ++y) {
                for (int x = 0; x < map->width; ++x) {
                    if (map->tile_overlays[layer][map_index(map, x, y)] > 0) overlay_count++;
                }
            }
        }
    }

    int decoration_count = map->decoration_count > 0 ? map->decoration_count : 0;
    int total = overlay_count + decoration_count + (unit_count > 0 ? unit_count : 0);
    if (total <= 0) return;
    RenderItem *items = malloc((size_t)total * sizeof(*items));
    if (!items) {
        render_decorations(app, map, cache);
        render_units(app, units, unit_count, fallback_sprite, cache, game_info, ticks);
        return;
    }

    int count = 0;
    if (map->render_features & MAP_RENDER_INTERLEAVED_OVERLAYS) {
        for (int layer = 0; layer < map->tile_overlay_count && layer < MAX_TILE_OVERLAYS; ++layer) {
            if (!map->tile_overlays[layer]) continue;
            for (int y = 0; y < map->height; ++y) {
                for (int x = 0; x < map->width; ++x) {
                    if (map->tile_overlays[layer][map_index(map, x, y)] <= 0) continue;
                    items[count++] = (RenderItem){
                        .kind = RENDER_ITEM_OVERLAY,
                        .sort_y = (float)y + 1.0f + (float)layer * 0.001f,
                        .index = map_index(map, x, y),
                        .x = x,
                        .y = y,
                        .layer = layer,
                    };
                }
            }
        }
    }
    for (int i = 0; i < map->decoration_count; ++i) {
        const MapDecoration *dec = &map->decorations[i];
        float sort_y = dec->center_anchor ?
            (float)dec->gy + 0.5f :
            (float)dec->gy + (float)(dec->footprint_h > 0 ? dec->footprint_h : 1);
        items[count++] = (RenderItem){
            .kind = RENDER_ITEM_DECORATION,
            .sort_y = sort_y,
            .index = i,
        };
    }
    for (int i = 0; i < unit_count; ++i) {
        items[count++] = (RenderItem){
            .kind = RENDER_ITEM_UNIT,
            .sort_y = units[i].gy,
            .index = i,
        };
    }

    qsort(items, (size_t)count, sizeof(*items), compare_render_items);
    for (int i = 0; i < count; ++i) {
        RenderItem *item = &items[i];
        if (item->kind == RENDER_ITEM_OVERLAY) {
            render_overlay_tile_item(app, map, tileset, item->x, item->y, item->layer);
        } else if (item->kind == RENDER_ITEM_DECORATION) {
            const MapDecoration *dec = &map->decorations[item->index];
            render_decoration_sprite(app, dec, sprite_cache_lookup(cache, dec->shadow_name));
            render_decoration_sprite(app, dec, sprite_cache_lookup(cache, dec->sprite_name));
        } else {
            render_unit_sprite(app, &units[item->index], fallback_sprite, cache, game_info, ticks);
        }
    }
    free(items);
}

static int sprite_frame_for_effect(const SpriteSheet *sprite, const RtsVisualEffect *effect) {
    if (!sprite || !effect) return 0;
    int frame_ms = effect->frame_ms > 0 ? effect->frame_ms : 90;
    int anim = effect->age_ms / frame_ms;
    if (effect->sequence_name[0] != '\0') {
        int frame = sprite_sequence_frame(sprite, effect->sequence_name, effect->facing_code, anim);
        if (frame >= 0) return frame;
        if (strcmp(effect->sequence_name, "die") == 0) {
            frame = sprite_sequence_frame(sprite, "death", effect->facing_code, anim);
            if (frame >= 0) return frame;
        }
    }
    if (sprite->frame_count <= 0) return 0;
    if (anim >= sprite->frame_count) anim = sprite->frame_count - 1;
    return anim < 0 ? 0 : anim;
}

void render_visual_effects(App *app, const RtsVisualEffect *effects, int max_effects,
                           const SpriteCache *cache, const RtsGameInfo *game_info) {
    if (!effects || max_effects <= 0) return;
    for (int i = 0; i < max_effects; ++i) {
        const RtsVisualEffect *effect = &effects[i];
        if (!effect->active) continue;
        const char *sprite_name = effect->sprite_name;
        if (effect->use_state && game_info && effect->sprite_id >= 0 &&
            effect->sprite_id < game_info->sprite_count &&
            game_info->sprnames && game_info->sprnames[effect->sprite_id]) {
            sprite_name = game_info->sprnames[effect->sprite_id];
        }
        const SpriteSheet *sprite = sprite_cache_lookup(cache, sprite_name);
        if (!sprite || !sprite->texture || sprite->frame_count <= 0) {
            debug_effects_log("render skip slot=%d sprite=%s sequence=%s reason=missing-cache",
                              i, effect->sprite_name,
                              effect->sequence_name[0] ? effect->sequence_name : "(none)");
            continue;
        }

        float sx, sy;
        grid_to_screen(app, effect->gx, effect->gy, &sx, &sy);
        int scale = app_scale(app);
        int sprite_w = sprite->frame_w * scale;
        int sprite_h = sprite->frame_h * scale;
        SDL_Rect dst = {
            (int)(sx - sprite_w / 2),
            (int)(sy - sprite_h / 2),
            sprite_w,
            sprite_h,
        };
        if (dst.x > app->win_w || dst.y > app->win_h ||
            dst.x + dst.w < 0 || dst.y + dst.h < 0) {
            debug_effects_log("render skip slot=%d sprite=%s sequence=%s frame_count=%d pos=%.2f,%.2f dst=%d,%d,%d,%d reason=offscreen",
                              i, effect->sprite_name,
                              effect->sequence_name[0] ? effect->sequence_name : "(none)",
                              sprite->frame_count, effect->gx, effect->gy,
                              dst.x, dst.y, dst.w, dst.h);
            continue;
        }
        int frame = effect->use_state ? effect->frame : sprite_frame_for_effect(sprite, effect);
        if (frame < 0 || frame >= sprite->frame_count) frame = 0;
        if (effect->screen_offset_x != 0 || effect->screen_offset_y != 0) {
            dst.x += effect->screen_offset_x * scale;
            dst.y += effect->screen_offset_y * scale;
        }
        debug_effects_log("render slot=%d sprite=%s sequence=%s age=%d/%d facing=%d anim=%d frame=%d frame_count=%d offset=%d,%d dst=%d,%d,%d,%d",
                          i, effect->sprite_name,
                          effect->sequence_name[0] ? effect->sequence_name : "(none)",
                          effect->age_ms, effect->duration_ms, effect->facing_code,
                          effect->frame_ms > 0 ? effect->age_ms / effect->frame_ms : 0,
                          frame, sprite->frame_count,
                          effect->screen_offset_x, effect->screen_offset_y,
                          dst.x, dst.y, dst.w, dst.h);
        SDL_RendererFlip flip = (effect->render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        if ((effect->render_flags & RTS_FRAME_ADDITIVE) != 0)
            SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_ADD);
        if ((effect->render_flags & RTS_FRAME_TINT_YELLOW) != 0) {
            SDL_SetTextureColorMod(sprite->texture, 255, 236, 72);
            SDL_SetTextureAlphaMod(sprite->texture, 230);
        }
        SDL_RenderCopyEx(app->renderer, sprite->texture, &sprite->frames[frame], &dst, 0.0, NULL, flip);
        if ((effect->render_flags & RTS_FRAME_TINT_YELLOW) != 0) {
            SDL_SetTextureColorMod(sprite->texture, 255, 255, 255);
            SDL_SetTextureAlphaMod(sprite->texture, 255);
        }
        if ((effect->render_flags & RTS_FRAME_ADDITIVE) != 0)
            SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_BLEND);
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
            if (e->key.keysym.sym == SDLK_b) app->show_blocked = !app->show_blocked;
            if (e->key.keysym.sym == SDLK_a && (e->key.keysym.mod & KMOD_CTRL)) {
                for (int i = 0; i < unit_count; ++i) {
                    units[i].selected = units[i].owner == 0 &&
                        (units[i].traits & RTS_TRAIT_SELECTABLE) != 0 &&
                        units[i].hp > 0;
                }
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
                float gx = 0.0f, gy = 0.0f;
                screen_to_grid_point(app, rx, ry, &gx, &gy);
                int target = pick_unit_at(app, units, unit_count, rx, ry, -1);
                if (target >= 0 && units[target].owner != 0 && units[target].hp > 0) {
                    for (int i = 0; i < unit_count; ++i) {
                        if (!units[i].selected || units[i].owner != 0 || units[i].hp <= 0) continue;
                        if ((units[i].traits & RTS_TRAIT_ATTACK) == 0) continue;
                        units[i].attack_target = target;
                        units[i].harvest_target = -1;
                        units[i].harvest_timer_ms = 0;
                    }
                    gx = units[target].gx;
                    gy = units[target].gy;
                } else {
                    if (issue_harvest_order_at(map, units, unit_count, gx, gy)) {
                        break;
                    }
                    for (int i = 0; i < unit_count; ++i) {
                        if (units[i].selected && units[i].owner == 0) {
                            units[i].attack_target = -1;
                        }
                    }
                }
                issue_move_order_at(map, units, unit_count, gx, gy);
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
                if (box) {
                    for (int i = 0; i < unit_count; ++i) {
                        if (units[i].hp <= 0) continue;
                        if ((units[i].traits & RTS_TRAIT_SELECTABLE) == 0) continue;
                        if (units[i].owner != 0) continue;
                        float sx, sy;
                        grid_to_screen(app, units[i].gx, units[i].gy, &sx, &sy);
                        float radius = unit_pick_radius_px(app, &units[i]);
                        if (point_in_rect((int)sx, (int)sy, rect) ||
                            circle_intersects_rect(sx, sy, radius, rect)) {
                            units[i].selected = true;
                        }
                    }
                } else {
                    int picked = pick_unit_at(app, units, unit_count, bx, by, 0);
                    if (picked >= 0) {
                        units[picked].selected = true;
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

void clamp_camera_to_map(App *app, const GameMap *map, int viewport_w, int viewport_h) {
    if (!app || !map || map->width <= 0 || map->height <= 0) return;
    if (viewport_w <= 0) viewport_w = app->win_w;
    if (viewport_h <= 0) viewport_h = app->win_h;

    float map_w = (float)map->width * (float)app_cell_w(app);
    float map_h = (float)map->height * (float)app_cell_h(app);
    if (map_w <= (float)viewport_w) {
        app->cam_x = ((float)viewport_w - map_w) * 0.5f;
    } else {
        float min_x = (float)viewport_w - map_w;
        if (app->cam_x < min_x) app->cam_x = min_x;
        if (app->cam_x > 0.0f) app->cam_x = 0.0f;
    }
    if (map_h <= (float)viewport_h) {
        app->cam_y = ((float)viewport_h - map_h) * 0.5f;
    } else {
        float min_y = (float)viewport_h - map_h;
        if (app->cam_y < min_y) app->cam_y = min_y;
        if (app->cam_y > 0.0f) app->cam_y = 0.0f;
    }
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
    free(map->resource_vents);
    memset(map, 0, sizeof(*map));
}

void destroy_sprite(SpriteSheet *sprite) {
    if (sprite->texture) SDL_DestroyTexture(sprite->texture);
    free(sprite->frames);
    free(sprite->frame_bounds);
    memset(sprite, 0, sizeof(*sprite));
}

void destroy_font(RtsBitmapFont *font) {
    if (!font) return;
    destroy_sprite(&font->sprite);
    memset(font, 0, sizeof(*font));
}

void destroy_sprite_cache(SpriteCache *cache) {
    for (int i = 0; i < cache->count; ++i) {
        destroy_sprite(&cache->entries[i].sprite);
    }
    memset(cache, 0, sizeof(*cache));
}
