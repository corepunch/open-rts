#include "engine.h"

#include <math.h>
#include <string.h>

/* DC.EXE 0x44ecd0/0x44ee68: quantize each vertical interpolation first,
 * then interpolate horizontally. A single bilinear float is not equivalent. */
int R_FogSample(const int corners[4], ivec2_t pixel) {
    int left = ((31 - pixel.y) * corners[0] + pixel.y * corners[2]) / 31;
    int right = ((31 - pixel.y) * corners[1] + pixel.y * corners[3]) / 31;
    return ((31 - pixel.x) * left + pixel.x * right) / 31;
}

static int corner_brightness(const level_t *map, ivec2_t corner) {
    int sum = 0;
    for (int y = corner.y - 1; y <= corner.y; ++y) {
        for (int x = corner.x - 1; x <= corner.x; ++x) {
            ivec2_t cell = { x, y };
            cell.x = cell.x < 0 ? 0 : cell.x >= map->width ? map->width - 1 : cell.x;
            cell.y = cell.y < 0 ? 0 : cell.y >= map->height ? map->height - 1 : cell.y;
            cell.y = L_ScreenY(map, cell.y);
            sum += P_SightBrightness(map, cell);
        }
    }
    return sum >> 2;
}

/* Destination darkening equivalent to SDL_BLENDMODE_MOD with gray light/255.
 * R_FogSample is 0..16; light is sample * 255 / 16, so 16 keeps the pixel,
 * 8 is 127/255, and 0 is black. */
static uint8_t fogmap[17][256];
static uint32_t fogmap_palette[256];
static bool fogmap_ready;
static uint8_t foglerp[17][17][32];

static void ensure_fogmap(void) {
    if (fogmap_ready && memcmp(fogmap_palette, vpalette, sizeof(vpalette)) == 0) return;
    memcpy(fogmap_palette, vpalette, sizeof(vpalette));
    if (!fogmap_ready)
        for (int a = 0; a <= 16; ++a)
            for (int b = 0; b <= 16; ++b)
                for (int p = 0; p < 32; ++p)
                    foglerp[a][b][p] = (uint8_t)(((31 - p) * a + p * b) / 31);
    for (int sample = 0; sample <= 16; ++sample) {
        int light = sample * 255 / 16;
        for (int i = 0; i < 256; ++i) {
            if (sample == 16) { fogmap[sample][i] = (uint8_t)i; continue; }
            int r = ((int)((vpalette[i] >> 16) & 255) * light) / 255;
            int g = ((int)((vpalette[i] >> 8) & 255) * light) / 255;
            int b = ((int)(vpalette[i] & 255) * light) / 255;
            fogmap[sample][i] = V_NearestIndex(0xff000000u | ((uint32_t)r << 16) |
                                               ((uint32_t)g << 8) | (uint32_t)b);
        }
    }
    fogmap_ready = true;
}

void R_DrawFog(app_t *app, const level_t *map) {
    if (!map->sight.cells || !screens[0].pixels || app->cell.w <= 0 || app->cell.h <= 0) return;
    ensure_fogmap();
    irect_t view = G_WorldViewport(app);
    int origin = view.x < 0 ? 0 : view.x;
    if (origin > screens[0].w) origin = screens[0].w;
    int width = view.w;
    if (width > screens[0].w - origin) width = screens[0].w - origin;
    if (width < 0) width = 0;
    int height = app->win.h < screens[0].h ? app->win.h : screens[0].h;
    ivec2_t first;
    isize2_t tiles;
    int x_limit;
    if (origin == 0) {
        first = (ivec2_t){(int)floorf(-app->cam.x / app->cell.w),
                          (int)floorf(-app->cam.y / app->cell.h)};
        tiles = (isize2_t){(width + app->cell.w - 1) / app->cell.w + 1,
                           (height + app->cell.h - 1) / app->cell.h + 1};
        x_limit = width;
    } else {
        int last_x = (int)floorf(((float)origin + (float)width - 1.0f - app->cam.x) /
                                 (float)app->cell.w);
        first.x = (int)floorf(((float)origin - app->cam.x) / (float)app->cell.w);
        first.y = (int)floorf(-app->cam.y / app->cell.h);
        tiles.w = last_x - first.x + 1;
        tiles.h = (height + app->cell.h - 1) / app->cell.h + 1;
        if (tiles.w < 1) tiles.w = 1;
        x_limit = origin + width;
    }
    irect_t dst = {(int)(first.x * app->cell.w + app->cam.x),
                   (int)(first.y * app->cell.h + app->cam.y),
                   tiles.w * app->cell.w, tiles.h * app->cell.h};
    int x0 = dst.x > origin ? dst.x : origin;
    int y0 = dst.y > 0 ? dst.y : 0;
    int x1 = dst.x + dst.w < x_limit ? dst.x + dst.w : x_limit;
    int y1 = dst.y + dst.h < height ? dst.y + dst.h : height;
    if (dst.w <= 0 || dst.h <= 0 || x0 >= x1 || y0 >= y1) return;
    for (int ty = 0; ty < tiles.h; ++ty) {
        int tile_y = dst.y + ty * app->cell.h;
        int top = tile_y > y0 ? tile_y : y0;
        int bottom = tile_y + app->cell.h < y1 ? tile_y + app->cell.h : y1;
        if (top >= bottom) continue;
        for (int tx = 0; tx < tiles.w; ++tx) {
            int tile_x = dst.x + tx * app->cell.w;
            int left = tile_x > x0 ? tile_x : x0;
            int right = tile_x + app->cell.w < x1 ? tile_x + app->cell.w : x1;
            if (left >= right) continue;
            ivec2_t cell = ivec2_add(first, (ivec2_t){tx, ty});
            int corners[4] = {0};
            if (L_Contains(map, cell.x, cell.y)) {
                corners[0] = corner_brightness(map, cell);
                corners[1] = corner_brightness(map, ivec2_add(cell, (ivec2_t){1, 0}));
                corners[2] = corner_brightness(map, ivec2_add(cell, (ivec2_t){0, 1}));
                corners[3] = corner_brightness(map, ivec2_add(cell, (ivec2_t){1, 1}));
            }
            bool uniform = corners[0] == corners[1] && corners[0] == corners[2] && corners[0] == corners[3];
            if (uniform && corners[0] == 16) continue;
            for (int y = top; y < bottom; ++y) {
                uint8_t *row = screens[0].pixels + (size_t)y * screens[0].w;
                if (uniform) {
                    if (!corners[0]) memset(row + left, fogmap[0][0], (size_t)(right - left));
                    else {
                        const uint8_t *shade = fogmap[corners[0]];
                        for (int x = left; x < right; ++x) row[x] = shade[row[x]];
                    }
                    continue;
                }
                int ly = (y - tile_y) * 32 / app->cell.h;
                int a = foglerp[corners[0]][corners[2]][ly];
                int b = foglerp[corners[1]][corners[3]][ly];
                const uint8_t *samples = foglerp[a][b];
                if (app->cell.w == 32) {
                    for (int x = left; x < right; ++x)
                        row[x] = fogmap[samples[x - tile_x]][row[x]];
                } else {
                    for (int x = left; x < right; ++x)
                        row[x] = fogmap[samples[(x - tile_x) * 32 / app->cell.w]][row[x]];
                }
            }
        }
    }
}

/* ── Tile-based fog of war ────────────────────────────────────────────── */

enum { FOG_VISIBLE, FOG_EXPLORED, FOG_UNEXPLORED };

static int cell_fog_state(const level_t *map, int x, int y) {
    if (!map->sight.cells) return FOG_VISIBLE;
    if (!L_Contains(map, x, y)) return FOG_UNEXPLORED;
    int idx = L_Index(map, x, y);
    uint32_t bits = map->sight.cells[idx];
    if (!(bits & SIGHT_EXPLORED)) return FOG_UNEXPLORED;
    return (bits & map->sight.allies[consoleplayer]) ? FOG_VISIBLE : FOG_EXPLORED;
}

/* 16 procedural 32x32 fog masks — one per combination of fogged corners.
 * Bit 0 = TL, 1 = TR, 2 = BL, 3 = BR.  1 = fogged pixel, 0 = clear. */
#define FOG_TILE_SIZE 32
static uint8_t fog_masks[16][FOG_TILE_SIZE * FOG_TILE_SIZE];
static bool fog_masks_ready;

static void generate_fog_masks(void) {
    if (fog_masks_ready) return;
    int r2 = FOG_TILE_SIZE * FOG_TILE_SIZE / 2;
    for (int mask = 0; mask < 16; ++mask) {
        uint8_t *tile = fog_masks[mask];
        for (int py = 0; py < FOG_TILE_SIZE; ++py) {
            for (int px = 0; px < FOG_TILE_SIZE; ++px) {
                bool fogged = false;
                if (mask & 1) fogged |= px * px + py * py < r2;
                if (mask & 2) fogged |= (FOG_TILE_SIZE - 1 - px) * (FOG_TILE_SIZE - 1 - px) + py * py < r2;
                if (mask & 4) fogged |= px * px + (FOG_TILE_SIZE - 1 - py) * (FOG_TILE_SIZE - 1 - py) < r2;
                if (mask & 8) fogged |= (FOG_TILE_SIZE - 1 - px) * (FOG_TILE_SIZE - 1 - px) +
                                        (FOG_TILE_SIZE - 1 - py) * (FOG_TILE_SIZE - 1 - py) < r2;
                tile[py * FOG_TILE_SIZE + px] = fogged ? 1 : 0;
            }
        }
    }
    fog_masks_ready = true;
}

/* Build a 4-bit corner mask: which corners of cell (cx,cy) border a cell
 * in state >= threshold (FOG_EXPLORED or FOG_UNEXPLORED). */
static int fog_corner_mask(const level_t *map, int cx, int cy, int threshold) {
    int mask = 0;
    bool n = cell_fog_state(map, cx, cy - 1) >= threshold;
    bool s = cell_fog_state(map, cx, cy + 1) >= threshold;
    bool w = cell_fog_state(map, cx - 1, cy) >= threshold;
    bool e = cell_fog_state(map, cx + 1, cy) >= threshold;
    bool nw = cell_fog_state(map, cx - 1, cy - 1) >= threshold;
    bool ne = cell_fog_state(map, cx + 1, cy - 1) >= threshold;
    bool sw = cell_fog_state(map, cx - 1, cy + 1) >= threshold;
    bool se = cell_fog_state(map, cx + 1, cy + 1) >= threshold;
    if (n || w || nw) mask |= 1;
    if (n || e || ne) mask |= 2;
    if (s || w || sw) mask |= 4;
    if (s || e || se) mask |= 8;
    return mask;
}

static void apply_fog_mask(const uint8_t *mask_tile, int tile_size,
                           int sx, int sy, int cell_w, int cell_h,
                           uint8_t color, bool use_color) {
    for (int py = 0; py < cell_h; ++py) {
        int my = py * tile_size / cell_h;
        int screen_y = sy + py;
        if (screen_y < 0 || screen_y >= screens[0].h) continue;
        uint8_t *row = screens[0].pixels + (size_t)screen_y * screens[0].w;
        for (int px = 0; px < cell_w; ++px) {
            int mx = px * tile_size / cell_w;
            if (!mask_tile[my * tile_size + mx]) continue;
            int screen_x = sx + px;
            if (screen_x < 0 || screen_x >= screens[0].w) continue;
            if (use_color)
                row[screen_x] = color;
            else
                row[screen_x] = fogmap[10][row[screen_x]];
        }
    }
}

void R_DrawFogTiles(app_t *app, const level_t *map, const tileset_t *tileset) {
    if (!map->sight.cells || !screens[0].pixels) return;
    generate_fog_masks();
    ensure_fogmap();
    (void)tileset;
    int cell_w = app->cell.w > 0 ? app->cell.w : CELL_W;
    int cell_h = app->cell.h > 0 ? app->cell.h : CELL_H;
    irect_t view = G_WorldViewport(app);
    int origin = view.x < 0 ? 0 : view.x;
    if (origin > screens[0].w) origin = screens[0].w;
    int width = view.w;
    if (width > screens[0].w - origin) width = screens[0].w - origin;
    if (width < 0) width = 0;
    int height = app->win.h < screens[0].h ? app->win.h : screens[0].h;
    uint8_t black = V_NearestIndex(0xff000000u);

    /* Pass 1: fill unexplored cells black, darken explored cells. */
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            int state = cell_fog_state(map, x, L_ScreenY(map, y));
            if (state == FOG_VISIBLE) continue;
            float sx, sy;
            R_GridToScreen(app, (float)x, (float)y, &sx, &sy);
            int dx = (int)sx, dy = (int)sy;
            if (dx + cell_w <= origin || dx >= origin + width ||
                dy + cell_h <= 0 || dy >= height) continue;
            if (state == FOG_UNEXPLORED) {
                irect_t r = { dx, dy, cell_w, cell_h };
                V_FillRect(r, black);
            } else {
                for (int py = 0; py < cell_h; ++py) {
                    int screen_y = dy + py;
                    if (screen_y < 0 || screen_y >= height) continue;
                    uint8_t *row = screens[0].pixels + (size_t)screen_y * screens[0].w;
                    int x0 = dx < origin ? origin : dx;
                    int x1 = dx + cell_w > origin + width ? origin + width : dx + cell_w;
                    for (int screen_x = x0; screen_x < x1; ++screen_x)
                        row[screen_x] = fogmap[10][row[screen_x]];
                }
            }
        }
    }

    /* Pass 2: smooth shroud edges on explored/visible cells. */
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            int wy = L_ScreenY(map, y);
            int state = cell_fog_state(map, x, wy);
            if (state == FOG_UNEXPLORED) continue;
            int mask = fog_corner_mask(map, x, wy, FOG_UNEXPLORED);
            if (!mask) continue;
            float sx, sy;
            R_GridToScreen(app, (float)x, (float)y, &sx, &sy);
            int dx = (int)sx, dy = (int)sy;
            if (dx + cell_w <= origin || dx >= origin + width ||
                dy + cell_h <= 0 || dy >= height) continue;
            apply_fog_mask(fog_masks[mask], FOG_TILE_SIZE,
                           dx, dy, cell_w, cell_h, black, true);
        }
    }

    /* Pass 3: smooth fog edges on visible cells. */
    for (int y = 0; y < map->height; ++y) {
        for (int x = 0; x < map->width; ++x) {
            int wy = L_ScreenY(map, y);
            if (cell_fog_state(map, x, wy) != FOG_VISIBLE) continue;
            int mask = fog_corner_mask(map, x, wy, FOG_EXPLORED);
            if (!mask) continue;
            float sx, sy;
            R_GridToScreen(app, (float)x, (float)y, &sx, &sy);
            int dx = (int)sx, dy = (int)sy;
            if (dx + cell_w <= origin || dx >= origin + width ||
                dy + cell_h <= 0 || dy >= height) continue;
            apply_fog_mask(fog_masks[mask], FOG_TILE_SIZE,
                           dx, dy, cell_w, cell_h, 0, false);
        }
    }
}
