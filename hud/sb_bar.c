#include "d_net.h"
#include "sb_bar.h"
#include "game.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(RTS_UI_MAX_RESOURCES == RTS_MAX_RESOURCES,
               "UI and simulation resource slot counts must match");

static irect_t ui_scaled_rect(const app_t *app, const uidefinition_t *def, irect_t rect) {
    float sx = (float)app->win.w / (float)def->logical_width;
    float sy = (float)app->win.h / (float)def->logical_height;
    return (irect_t){
        (int)((float)rect.x * sx), (int)((float)rect.y * sy),
        (int)((float)rect.w * sx), (int)((float)rect.h * sy),
    };
}

bool SB_Init(sb_state_t *st, const char *data_root,
             const uidefinition_t *definition) {
    if (!st || !data_root || !definition ||
        definition->image_count < 0 || definition->image_count > RTS_UI_MAX_LAYERS ||
        definition->product_count < 0) return false;
    memset(st, 0, sizeof(*st));
    st->definition = definition;
    for (int i = 0; i < definition->image_count; ++i) {
        char path[1024];
        M_PathJoin(path, sizeof(path), definition->asset_root ? definition->asset_root : data_root,
                   definition->images[i].asset_path);
        if (!W_LoadIndexedSheet(path, &st->images[i])) {
            fprintf(stderr, "warning: failed to load UI asset %s\n", path);
            SB_Shutdown(st);
            return false;
        }
    }
    if (definition->product_count > 0) {
        st->product_icons = calloc(definition->product_count, sizeof(*st->product_icons));
        if (!st->product_icons) { SB_Shutdown(st); return false; }
        for (int i = 0; i < definition->product_count; ++i) {
            if (!G_LoadMenuSprite(data_root, definition->products[i].image,
                                  &st->product_icons[i])) {
                fprintf(stderr, "failed to load menu image %s\n", definition->products[i].image);
                SB_Shutdown(st);
                return false;
            }
        }
    }
    st->ready = true;
    SB_Start(st);
    return true;
}

void SB_Start(sb_state_t *st) {
    if (!st) return;
    st->first_draw = true;
    st->pressed_button = -1;
    st->clock = 0;
    st->production_category = -1;
    st->path.mode = WP_ONCE;
    st->saved_path_selection = -1;
    st->radar_visible = st->definition->minimap.w > 0;
}

bool SB_Responder(sb_state_t *st, const app_t *app, const SDL_Event *event) {
    if (!st || !st->ready || !st->definition || !app || !event ||
        st->definition->sidebar_cell_size <= 0) return false;
    if (event->type != SDL_MOUSEBUTTONDOWN && event->type != SDL_MOUSEBUTTONUP)
        return false;

    int x = 0, y = 0;
    R_WindowToRenderPt(app, event->button.x, event->button.y, &x, &y);
    irect_t rail = ui_scaled_rect(app, st->definition,
                                 st->definition->sidebar_panel.rect);
    bool inside = irect_contains(rail, (ivec2_t){ x, y });
    if (event->type == SDL_MOUSEBUTTONDOWN && inside) {
        int cell_h = st->definition->sidebar_cell_size * app->win.h /
                     st->definition->logical_height;
        if (cell_h < 1) cell_h = 1;
        st->pressed_button = (y - rail.y) / cell_h;
        return true;
    }
    if (event->type == SDL_MOUSEBUTTONUP && st->pressed_button >= 0) {
        st->pressed_button = -1;
        return true;
    }
    return inside;
}

void SB_Ticker(sb_state_t *st) {
    if (st && st->ready) st->clock++;
}

irect_t SB_MinimapRect(const level_t *map) {
    if (gameui->minimap_scale && map)
        return (irect_t){0, gameui->logical_height - map->height * gameui->minimap_scale,
            map->width * gameui->minimap_scale, map->height * gameui->minimap_scale};
    return gameui->minimap;
}

static void SB_drawMinimap(const sb_state_t *st, app_t *app, const level_t *map,
                                 mobj_t *const *units, int unit_count) {
    if (st->definition->product_count && !st->radar_visible) return;
    int radar_level = G_ModelRadarLevel(consoleplayer);
    if (!radar_level) return;
    irect_t rect = ui_scaled_rect(app, st->definition, SB_MinimapRect(map));
    if (rect.w <= 0 || rect.h <= 0 || !map || map->width <= 0 || map->height <= 0) return;
    V_FillRect(rect, V_NearestIndex(0xff050707u));
    irect_t previous_clip = V_GetClip();
    V_SetClip(rect);
    uint8_t solid = V_NearestIndex(0xff5d5b46u);
    uint8_t open = V_NearestIndex(0xff3f4f34u);
    for (int i = 0; i < map->decoration_count; ++i) {
        const mapdecoration_t *dec = &map->decorations[i];
        if (!P_SightBrightness(map, dec->cell)) continue;
        int x = rect.x + dec->cell.x * rect.w / map->width;
        int y = rect.y + L_ScreenY(map, dec->cell.y) * rect.h / map->height;
        V_DrawPoint((ivec2_t){x, y}, dec->solid ? solid : open);
    }
    uint8_t friendly = V_NearestIndex(0xff30dc41u);
    uint8_t enemy = V_NearestIndex(0xffd22d41u);
    for (int i = 0; i < unit_count; ++i) {
        if (radar_level < 2 && units[i]->owner != consoleplayer) continue;
        if (!P_VisibleToPlayer(units[i]) || units[i]->remove || units[i]->hp <= 0) continue;
        fvec2_t position = fixed3_xy_to_fvec2(units[i]->core.position);
        int x = rect.x + (int)(position.x * (float)rect.w / (float)map->width);
        int y = rect.y + (int)(L_ScreenYF(map, position.y) * (float)rect.h /
                              (float)map->height);
        irect_t dot = { x - 1, y - 1, 3, 3 };
        V_FillRect(dot, units[i]->owner == consoleplayer ? friendly : enemy);
    }
    int cell_w = app->cell.w > 0 ? app->cell.w : 24;
    int cell_h = app->cell.h > 0 ? app->cell.h : 24;
    float left = -app->cam.x / (float)cell_w;
    float top_screen = -app->cam.y / (float)cell_h;
    irect_t view = {
        rect.x + (int)(left * (float)rect.w / (float)map->width),
        rect.y + (int)(top_screen * (float)rect.h / (float)map->height),
        G_WorldViewportWidth(app) * rect.w / (cell_w * map->width),
        app->win.h * rect.h / (cell_h * map->height),
    };
    V_DrawRectOutline(view, V_NearestIndex(0xffd7d7cdu));
    V_SetClip(previous_clip);
    if (st->definition->draw_minimap_overlay)
        st->definition->draw_minimap_overlay(app, map, rect);
}

static void draw_digit(int x, int y, int digit, uint32_t argb) {
    static const unsigned char segments[10] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
    };
    const irect_t bars[7] = {
        {2,0,8,2},{10,2,2,8},{10,12,2,8},{2,20,8,2},{0,12,2,8},{0,2,2,8},{2,10,8,2},
    };
    uint8_t color = V_NearestIndex(argb);
    unsigned char mask = segments[digit];
    for (int i = 0; i < 7; ++i) if (mask & (1u << i)) {
        irect_t bar = { x + bars[i].x, y + bars[i].y, bars[i].w, bars[i].h };
        V_FillRect(bar, color);
    }
}

static void SB_drawPanel(const sb_state_t *st, app_t *app, uipanel_t panel) {
    irect_t rect = ui_scaled_rect(app, st->definition, panel.rect);
    if (rect.w <= 0 || rect.h <= 0) return;
    V_FillRect(rect, V_NearestIndex(panel.fill));
    V_DrawRectOutline(rect, V_NearestIndex(panel.border));
}

static void SB_drawSidebarIcon(irect_t cell, int slot) {
    if (slot < 5 || slot > 9) return;
    int cx = cell.x + cell.w / 2;
    int cy = cell.y + cell.h / 2;
    int r = cell.w / 5;
    uint8_t color = V_NearestIndex(0xff28c4dau);
    if (slot == 5) { /* bomber */
        for (int y = -r; y <= r; ++y) {
            int half = r - abs(y);
            V_DrawLine((ivec2_t){cx - half, cy + y}, (ivec2_t){cx + half, cy + y}, color);
        }
        V_DrawLine((ivec2_t){cx, cy - r - 6}, (ivec2_t){cx + 5, cy - r - 1}, color);
    } else if (slot == 6) { /* sell */
        V_DrawLine((ivec2_t){cx + 5, cy - r}, (ivec2_t){cx - 5, cy - r}, color);
        V_DrawLine((ivec2_t){cx - 5, cy - r}, (ivec2_t){cx - 7, cy - 1}, color);
        V_DrawLine((ivec2_t){cx - 7, cy - 1}, (ivec2_t){cx + 7, cy + 1}, color);
        V_DrawLine((ivec2_t){cx + 7, cy + 1}, (ivec2_t){cx + 5, cy + r}, color);
        V_DrawLine((ivec2_t){cx + 5, cy + r}, (ivec2_t){cx - 5, cy + r}, color);
        V_DrawLine((ivec2_t){cx, cy - r - 4}, (ivec2_t){cx, cy + r + 4}, color);
    } else if (slot == 7) { /* research */
        V_DrawLine((ivec2_t){cx - 4, cy - r}, (ivec2_t){cx + 4, cy - r}, color);
        V_DrawLine((ivec2_t){cx - 2, cy - r}, (ivec2_t){cx - 2, cy - 2}, color);
        V_DrawLine((ivec2_t){cx + 2, cy - r}, (ivec2_t){cx + 2, cy - 2}, color);
        V_DrawLine((ivec2_t){cx - 2, cy - 2}, (ivec2_t){cx - r, cy + r}, color);
        V_DrawLine((ivec2_t){cx + 2, cy - 2}, (ivec2_t){cx + r, cy + r}, color);
        V_DrawLine((ivec2_t){cx - r, cy + r}, (ivec2_t){cx + r, cy + r}, color);
        V_DrawLine((ivec2_t){cx - r + 3, cy + 4}, (ivec2_t){cx + r - 3, cy + 4}, color);
    } else if (slot == 8) { /* repair */
        V_DrawLine((ivec2_t){cx - r, cy + r}, (ivec2_t){cx + r, cy - r}, color);
        V_DrawLine((ivec2_t){cx - r + 1, cy + r}, (ivec2_t){cx - r - 4, cy + r - 5}, color);
        V_DrawLine((ivec2_t){cx + r, cy - r}, (ivec2_t){cx + r + 5, cy - r + 3}, color);
        V_DrawLine((ivec2_t){cx + r, cy - r}, (ivec2_t){cx + r - 3, cy - r - 5}, color);
    } else { /* radar */
        for (int y = -r; y <= r; ++y) {
            int x = (int)sqrtf((float)(r * r - y * y));
            V_DrawPoint((ivec2_t){cx - x, cy + y}, color);
            V_DrawPoint((ivec2_t){cx + x, cy + y}, color);
        }
        V_DrawLine((ivec2_t){cx - r, cy}, (ivec2_t){cx + r, cy}, color);
        V_DrawLine((ivec2_t){cx, cy - r}, (ivec2_t){cx, cy + r}, color);
    }
}

/* Black at alpha 96 over the bevel: keep each pixel, scaled by (255-96)/255. */
static void darken_pressed(irect_t cell) {
    if (!screens[0].pixels || cell.w <= 0 || cell.h <= 0) return;
    int x0 = cell.x > 0 ? cell.x : 0;
    int y0 = cell.y > 0 ? cell.y : 0;
    int x1 = cell.x + cell.w;
    int y1 = cell.y + cell.h;
    if (x1 > screens[0].w) x1 = screens[0].w;
    if (y1 > screens[0].h) y1 = screens[0].h;
    int kept = 255 - 96;
    for (int y = y0; y < y1; ++y) {
        uint8_t *row = screens[0].pixels + (size_t)y * (size_t)screens[0].w;
        for (int x = x0; x < x1; ++x) {
            uint32_t src = vpalette[row[x]];
            int r = (int)((src >> 16) & 255) * kept / 255;
            int g = (int)((src >> 8) & 255) * kept / 255;
            int b = (int)(src & 255) * kept / 255;
            row[x] = V_NearestIndex(0xff000000u | ((uint32_t)r << 16) |
                                    ((uint32_t)g << 8) | (uint32_t)b);
        }
    }
}

static void SB_drawSidebarCells(const sb_state_t *st, app_t *app) {
    const uidefinition_t *def = st->definition;
    if (def->sidebar_cell_size <= 0 || def->sidebar_panel.rect.h <= 0) return;
    int count = (def->sidebar_panel.rect.h + def->sidebar_cell_size - 1) /
                def->sidebar_cell_size;
    uint8_t fill = V_NearestIndex(0xff322218u);
    uint8_t highlight = V_NearestIndex(0xff775b43u);
    uint8_t shadow = V_NearestIndex(0xff18100bu);
    for (int i = 0; i < count; ++i) {
        irect_t cell = ui_scaled_rect(app, def, (irect_t){
            def->sidebar_panel.rect.x,
            def->sidebar_panel.rect.y + i * def->sidebar_cell_size,
            def->sidebar_panel.rect.w,
            def->sidebar_cell_size,
        });
        V_FillRect(cell, fill);
        V_DrawLine((ivec2_t){cell.x, cell.y},
                   (ivec2_t){cell.x + cell.w - 1, cell.y}, highlight);
        V_DrawLine((ivec2_t){cell.x, cell.y},
                   (ivec2_t){cell.x, cell.y + cell.h - 1}, highlight);
        V_DrawLine((ivec2_t){cell.x, cell.y + cell.h - 1},
                   (ivec2_t){cell.x + cell.w - 1, cell.y + cell.h - 1}, shadow);
        V_DrawLine((ivec2_t){cell.x + cell.w - 1, cell.y},
                   (ivec2_t){cell.x + cell.w - 1, cell.y + cell.h - 1}, shadow);
        if (st->pressed_button == i) darken_pressed(cell);
        SB_drawSidebarIcon(cell, i);
    }
}

static void SB_drawResource(const sb_state_t *st, app_t *app,
                                  const uiresource_t *display, int amount) {
    char value[24];
    if (amount < 0) amount = 0;
    snprintf(value, sizeof(value), "%d", amount);
    int count = (int)strlen(value);
    float sx = (float)app->win.w / (float)st->definition->logical_width;
    float sy = (float)app->win.h / (float)st->definition->logical_height;
    int anchor_x = (int)((float)display->text.x * sx);
    int x = display->right_aligned ? anchor_x - count * 14 + 2 : anchor_x - count * 7;
    int y = (int)((float)display->text.y * sy);
    for (int i = 0; i < count; ++i)
        draw_digit(x + i * 14, y, value[i] - '0', display->color);
}

static void SB_drawElapsedTime(const sb_state_t *st, app_t *app) {
    if (!st->definition->status_elapsed_time) return;
    irect_t panel = ui_scaled_rect(app, st->definition,
                                  st->definition->status_panel.rect);
    int seconds = (int)(st->clock / 30u);
    int minutes = (seconds / 60) % 100;
    seconds %= 60;
    int x = panel.x + 9;
    int y = panel.y + 3;
    draw_digit(x, y, minutes / 10, 0xffffffffu);
    draw_digit(x + 14, y, minutes % 10, 0xffffffffu);
    uint8_t white = V_NearestIndex(0xffffffffu);
    V_DrawPoint((ivec2_t){x + 29, y + 7}, white);
    V_DrawPoint((ivec2_t){x + 29, y + 14}, white);
    draw_digit(x + 34, y, seconds / 10, 0xffffffffu);
    draw_digit(x + 48, y, seconds % 10, 0xffffffffu);
}

static void SB_drawWidgets(const sb_state_t *st, app_t *app, const spritecache_t *sprites) {
    const uidefinition_t *def = st->definition;
    if (!sprites || def->product_count || def->command_columns <= 0 || def->command_rows <= 0) return;
    irect_t grid = ui_scaled_rect(app, def, def->command_grid);
    int cell_w = grid.w / def->command_columns;
    int cell_h = grid.h / def->command_rows;
    int slot = 0;
    for (int i = 0; i < sprites->count && slot < def->command_columns * def->command_rows; ++i) {
        const cachedsprite_t *cached = &sprites->entries[i];
        if (cached->name[0] == '\0' || tolower((unsigned char)cached->name[0]) == 'a' ||
            strstr(cached->name, "sh") ||
            !cached->sprite.lumps ||
            cached->sprite.numlumps <= 0) continue;
        irect_t src = cached->sprite.cells[0].rect;
        irect_t bounds = cached->sprite.lumps ? cached->sprite.cells[0].bounds :
            (irect_t){ 0, 0, src.w, src.h };
        src.x += bounds.x; src.y += bounds.y; src.w = bounds.w; src.h = bounds.h;
        if (src.w <= 0 || src.h <= 0) continue;
        int col = slot % def->command_columns;
        int row = slot / def->command_columns;
        irect_t cell = { grid.x + col * cell_w + 3, grid.y + row * cell_h + 3,
                          cell_w - 6, cell_h - 6 };
        float scale = fminf((float)cell.w / (float)src.w, (float)cell.h / (float)src.h);
        irect_t dst = { cell.x + (cell.w - (int)((float)src.w * scale)) / 2,
                         cell.y + (cell.h - (int)((float)src.h * scale)) / 2,
                         (int)((float)src.w * scale), (int)((float)src.h * scale) };
        irect_t cell_rect = cached->sprite.cells[0].rect;
        if (!cached->sprite.lumps[0].indices || cell_rect.w <= 0 || cell_rect.h <= 0 ||
            src.x < cell_rect.x || src.y < cell_rect.y ||
            src.x + src.w > cell_rect.x + cell_rect.w ||
            src.y + src.h > cell_rect.y + cell_rect.h)
            continue;
        uint8_t tint[256];
        V_ModulateRemap(tint, cached->sprite.source_palette, 0xffd23034u);
        V_DrawSpriteCellScaled(dst, &cached->sprite, 0, &src, tint, 0);
        slot++;
    }
}

void SB_Drawer(sb_state_t *st, app_t *app, const level_t *map,
               mobj_t *const *units, int unit_count, const spritecache_t *sprites,
               bool fullscreen, bool refresh) {
    st->sprites = sprites;
    (void)fullscreen;
    if (!st || !st->ready || !st->definition || !app) return;
    const uidefinition_t *def = st->definition;
    refresh = refresh || st->first_draw;
    st->first_draw = false;
    (void)refresh;
    SB_drawPanel(st, app, def->sidebar_panel);
    SB_drawSidebarCells(st, app);
    SB_drawPanel(st, app, def->status_panel);
    if (def->draw_status)
        def->draw_status(app, map, ui_scaled_rect(app, def, def->status_panel.rect));
    for (int i = 0; i < def->image_count; ++i) {
        if (def->images[i].destination.w <= 0 || def->images[i].destination.h <= 0) continue;
        irect_t dst = ui_scaled_rect(app, def, def->images[i].destination);
        const irect_t *src = def->images[i].source.w > 0 && def->images[i].source.h > 0 ?
            &def->images[i].source : NULL;
        R_DrawSprite(&st->images[i], 0, -1, src, &dst, 0, 16);
    }
    SB_drawWidgets(st, app, sprites);
    SB_drawMinimap(st, app, map, units, unit_count);
    SB_drawElapsedTime(st, app);
    int resource_count = def->resource_count;
    if (resource_count < 0) resource_count = 0;
    if (resource_count > RTS_UI_MAX_RESOURCES) resource_count = RTS_UI_MAX_RESOURCES;
    for (int i = 0; i < resource_count; ++i)
        SB_drawResource(st, app, &def->resources[i],
                        map ? map->player_resources[consoleplayer][i] : 0);
}

void SB_Shutdown(sb_state_t *st) {
    if (!st) return;
    if (st->product_icons) {
        for (int i = 0; i < st->definition->product_count; ++i)
            R_FreeSprite(&st->product_icons[i]);
        free(st->product_icons);
    }
    for (int i = 0; i < RTS_UI_MAX_LAYERS; ++i)
        R_FreeSprite(&st->images[i]);
    memset(st, 0, sizeof(*st));
}
