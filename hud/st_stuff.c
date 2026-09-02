#include "st_stuff.h"

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

bool ST_Init(st_state_t *st, SDL_Renderer *renderer, const char *data_root,
             const uidefinition_t *definition) {
    if (!st || !renderer || !data_root || !definition ||
        definition->image_count < 0 || definition->image_count > RTS_UI_MAX_LAYERS) return false;
    memset(st, 0, sizeof(*st));
    st->definition = definition;
    for (int i = 0; i < definition->image_count; ++i) {
        char path[1024];
        M_PathJoin(path, sizeof(path), data_root, definition->images[i].asset_path);
        SDL_Surface *surface = SDL_LoadBMP(path);
        if (!surface) {
            fprintf(stderr, "warning: failed to load UI asset %s: %s\n", path, SDL_GetError());
            ST_Shutdown(st);
            return false;
        }
        st->textures[i] = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
        if (!st->textures[i]) {
            ST_Shutdown(st);
            return false;
        }
    }
    st->ready = true;
    ST_Start(st);
    return true;
}

void ST_Start(st_state_t *st) {
    if (!st) return;
    st->first_draw = true;
    st->pressed_button = -1;
    st->clock = 0;
}

bool ST_Responder(st_state_t *st, const app_t *app, const SDL_Event *event) {
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

void ST_Ticker(st_state_t *st) {
    if (st && st->ready) st->clock++;
}

static void ST_drawMinimap(const st_state_t *st, app_t *app, const level_t *map,
                                 const mobj_t *units, int unit_count) {
    irect_t rect = ui_scaled_rect(app, st->definition, st->definition->minimap);
    if (rect.w <= 0 || rect.h <= 0 || !map || map->width <= 0 || map->height <= 0) return;
    SDL_SetRenderDrawColor(app->renderer, 5, 7, 7, 255);
    SDL_RenderFillRect(app->renderer, &rect);
    for (int i = 0; i < map->decoration_count; ++i) {
        const mapdecoration_t *dec = &map->decorations[i];
        int x = rect.x + dec->cell.x * rect.w / map->width;
        int y = rect.y + L_ScreenY(map, dec->cell.y) * rect.h / map->height;
        SDL_SetRenderDrawColor(app->renderer, dec->solid ? 93 : 63,
                              dec->solid ? 91 : 79, dec->solid ? 70 : 52, 255);
        SDL_RenderDrawPoint(app->renderer, x, y);
    }
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].hidden || units[i].remove || units[i].hp <= 0) continue;
        fvec2_t position = fixedvec3_xy_to_fvec2(units[i].core.position);
        int x = rect.x + (int)(position.x * (float)rect.w / (float)map->width);
        int y = rect.y + (int)(L_ScreenYF(map, position.y) * (float)rect.h /
                              (float)map->height);
        SDL_SetRenderDrawColor(app->renderer, units[i].owner == 0 ? 48 : 210,
                              units[i].owner == 0 ? 220 : 45, 65, 255);
        irect_t dot = { x - 1, y - 1, 3, 3 };
        SDL_RenderFillRect(app->renderer, &dot);
    }
    int cell_w = app->cell.w > 0 ? app->cell.w : 24;
    int cell_h = app->cell.h > 0 ? app->cell.h : 24;
    float left = -app->cam.x / (float)cell_w;
    float top_screen = -app->cam.y / (float)cell_h;
    irect_t view = {
        rect.x + (int)(left * (float)rect.w / (float)map->width),
        rect.y + (int)(top_screen * (float)rect.h / (float)map->height),
        app->win.w * rect.w / (cell_w * map->width),
        app->win.h * rect.h / (cell_h * map->height),
    };
    SDL_SetRenderDrawColor(app->renderer, 215, 215, 205, 255);
    SDL_RenderDrawRect(app->renderer, &view);
}

static void draw_digit(SDL_Renderer *renderer, int x, int y, int digit, SDL_Color color) {
    static const unsigned char segments[10] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
    };
    const irect_t bars[7] = {
        {2,0,8,2},{10,2,2,8},{10,12,2,8},{2,20,8,2},{0,12,2,8},{0,2,2,8},{2,10,8,2},
    };
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    unsigned char mask = segments[digit];
    for (int i = 0; i < 7; ++i) if (mask & (1u << i)) {
        irect_t bar = { x + bars[i].x, y + bars[i].y, bars[i].w, bars[i].h };
        SDL_RenderFillRect(renderer, &bar);
    }
}

static void ST_drawPanel(const st_state_t *st, app_t *app, uipanel_t panel) {
    irect_t rect = ui_scaled_rect(app, st->definition, panel.rect);
    if (rect.w <= 0 || rect.h <= 0) return;
    SDL_SetRenderDrawColor(app->renderer, panel.fill.r, panel.fill.g,
                           panel.fill.b, panel.fill.a);
    SDL_RenderFillRect(app->renderer, &rect);
    SDL_SetRenderDrawColor(app->renderer, panel.border.r, panel.border.g,
                           panel.border.b, panel.border.a);
    SDL_RenderDrawRect(app->renderer, &rect);
}

static void ST_drawSidebarIcon(SDL_Renderer *renderer, irect_t cell, int slot) {
    if (slot < 5 || slot > 9) return;
    int cx = cell.x + cell.w / 2;
    int cy = cell.y + cell.h / 2;
    int r = cell.w / 5;
    SDL_SetRenderDrawColor(renderer, 40, 196, 218, 255);
    if (slot == 5) { /* bomber */
        for (int y = -r; y <= r; ++y) {
            int half = r - abs(y);
            SDL_RenderDrawLine(renderer, cx - half, cy + y, cx + half, cy + y);
        }
        SDL_RenderDrawLine(renderer, cx, cy - r - 6, cx + 5, cy - r - 1);
    } else if (slot == 6) { /* sell */
        SDL_RenderDrawLine(renderer, cx + 5, cy - r, cx - 5, cy - r);
        SDL_RenderDrawLine(renderer, cx - 5, cy - r, cx - 7, cy - 1);
        SDL_RenderDrawLine(renderer, cx - 7, cy - 1, cx + 7, cy + 1);
        SDL_RenderDrawLine(renderer, cx + 7, cy + 1, cx + 5, cy + r);
        SDL_RenderDrawLine(renderer, cx + 5, cy + r, cx - 5, cy + r);
        SDL_RenderDrawLine(renderer, cx, cy - r - 4, cx, cy + r + 4);
    } else if (slot == 7) { /* research */
        SDL_RenderDrawLine(renderer, cx - 4, cy - r, cx + 4, cy - r);
        SDL_RenderDrawLine(renderer, cx - 2, cy - r, cx - 2, cy - 2);
        SDL_RenderDrawLine(renderer, cx + 2, cy - r, cx + 2, cy - 2);
        SDL_RenderDrawLine(renderer, cx - 2, cy - 2, cx - r, cy + r);
        SDL_RenderDrawLine(renderer, cx + 2, cy - 2, cx + r, cy + r);
        SDL_RenderDrawLine(renderer, cx - r, cy + r, cx + r, cy + r);
        SDL_RenderDrawLine(renderer, cx - r + 3, cy + 4, cx + r - 3, cy + 4);
    } else if (slot == 8) { /* repair */
        SDL_RenderDrawLine(renderer, cx - r, cy + r, cx + r, cy - r);
        SDL_RenderDrawLine(renderer, cx - r + 1, cy + r, cx - r - 4, cy + r - 5);
        SDL_RenderDrawLine(renderer, cx + r, cy - r, cx + r + 5, cy - r + 3);
        SDL_RenderDrawLine(renderer, cx + r, cy - r, cx + r - 3, cy - r - 5);
    } else { /* radar */
        for (int y = -r; y <= r; ++y) {
            int x = (int)sqrtf((float)(r * r - y * y));
            SDL_RenderDrawPoint(renderer, cx - x, cy + y);
            SDL_RenderDrawPoint(renderer, cx + x, cy + y);
        }
        SDL_RenderDrawLine(renderer, cx - r, cy, cx + r, cy);
        SDL_RenderDrawLine(renderer, cx, cy - r, cx, cy + r);
    }
}

static void ST_drawSidebarCells(const st_state_t *st, app_t *app) {
    const uidefinition_t *def = st->definition;
    if (def->sidebar_cell_size <= 0 || def->sidebar_panel.rect.h <= 0) return;
    int count = (def->sidebar_panel.rect.h + def->sidebar_cell_size - 1) /
                def->sidebar_cell_size;
    for (int i = 0; i < count; ++i) {
        irect_t cell = ui_scaled_rect(app, def, (irect_t){
            def->sidebar_panel.rect.x,
            def->sidebar_panel.rect.y + i * def->sidebar_cell_size,
            def->sidebar_panel.rect.w,
            def->sidebar_cell_size,
        });
        SDL_SetRenderDrawColor(app->renderer, 50, 34, 24, 255);
        SDL_RenderFillRect(app->renderer, &cell);
        SDL_SetRenderDrawColor(app->renderer, 119, 91, 67, 255);
        SDL_RenderDrawLine(app->renderer, cell.x, cell.y,
                          cell.x + cell.w - 1, cell.y);
        SDL_RenderDrawLine(app->renderer, cell.x, cell.y,
                          cell.x, cell.y + cell.h - 1);
        SDL_SetRenderDrawColor(app->renderer, 24, 16, 11, 255);
        SDL_RenderDrawLine(app->renderer, cell.x, cell.y + cell.h - 1,
                          cell.x + cell.w - 1, cell.y + cell.h - 1);
        SDL_RenderDrawLine(app->renderer, cell.x + cell.w - 1, cell.y,
                          cell.x + cell.w - 1, cell.y + cell.h - 1);
        if (st->pressed_button == i) {
            SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
            SDL_GetRenderDrawBlendMode(app->renderer, &old_blend);
            SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 96);
            SDL_RenderFillRect(app->renderer, &cell);
            SDL_SetRenderDrawBlendMode(app->renderer, old_blend);
        }
        ST_drawSidebarIcon(app->renderer, cell, i);
    }
}

static void ST_drawResource(const st_state_t *st, app_t *app,
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
    for (int i = 0; i < count; ++i) {
        draw_digit(app->renderer, x + i * 14, y, value[i] - '0', display->color);
    }
}

static void ST_drawElapsedTime(const st_state_t *st, app_t *app) {
    if (!st->definition->status_elapsed_time) return;
    irect_t panel = ui_scaled_rect(app, st->definition,
                                  st->definition->status_panel.rect);
    int seconds = (int)(st->clock / 30u);
    int minutes = (seconds / 60) % 100;
    seconds %= 60;
    int x = panel.x + 9;
    int y = panel.y + 3;
    SDL_Color white = { 255, 255, 255, 255 };
    draw_digit(app->renderer, x, y, minutes / 10, white);
    draw_digit(app->renderer, x + 14, y, minutes % 10, white);
    SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
    SDL_RenderDrawPoint(app->renderer, x + 29, y + 7);
    SDL_RenderDrawPoint(app->renderer, x + 29, y + 14);
    draw_digit(app->renderer, x + 34, y, seconds / 10, white);
    draw_digit(app->renderer, x + 48, y, seconds % 10, white);
}

static void ST_drawWidgets(const st_state_t *st, app_t *app, const spritecache_t *sprites) {
    const uidefinition_t *def = st->definition;
    if (!sprites || def->command_columns <= 0 || def->command_rows <= 0) return;
    irect_t grid = ui_scaled_rect(app, def, def->command_grid);
    int cell_w = grid.w / def->command_columns;
    int cell_h = grid.h / def->command_rows;
    int slot = 0;
    for (int i = 0; i < sprites->count && slot < def->command_columns * def->command_rows; ++i) {
        const cachedsprite_t *cached = &sprites->entries[i];
        if (cached->name[0] == '\0' || tolower((unsigned char)cached->name[0]) == 'a' ||
            strstr(cached->name, "sh") ||
            !cached->sprite.texture || cached->sprite.frame_count <= 0) continue;
        irect_t src = cached->sprite.frames[0];
        irect_t bounds = cached->sprite.frame_bounds ? cached->sprite.frame_bounds[0] :
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
        SDL_SetTextureColorMod(cached->sprite.texture, 210, 48, 52);
        SDL_RenderCopy(app->renderer, cached->sprite.texture, &src, &dst);
        SDL_SetTextureColorMod(cached->sprite.texture, 255, 255, 255);
        slot++;
    }
}

void ST_Drawer(st_state_t *st, app_t *app, const level_t *map,
               const mobj_t *units, int unit_count, const spritecache_t *sprites,
               bool fullscreen, bool refresh) {
    (void)fullscreen;
    if (!st || !st->ready || !st->definition || !app) return;
    const uidefinition_t *def = st->definition;
    refresh = refresh || st->first_draw;
    st->first_draw = false;
    (void)refresh;
    ST_drawPanel(st, app, def->sidebar_panel);
    ST_drawSidebarCells(st, app);
    ST_drawPanel(st, app, def->status_panel);
    for (int i = 0; i < def->image_count; ++i) {
        irect_t dst = ui_scaled_rect(app, def, def->images[i].destination);
        const irect_t *src = def->images[i].source.w > 0 && def->images[i].source.h > 0 ?
            &def->images[i].source : NULL;
        SDL_RenderCopy(app->renderer, st->textures[i], src, &dst);
    }
    ST_drawWidgets(st, app, sprites);
    ST_drawMinimap(st, app, map, units, unit_count);
    ST_drawElapsedTime(st, app);
    int resource_count = def->resource_count;
    if (resource_count < 0) resource_count = 0;
    if (resource_count > RTS_UI_MAX_RESOURCES) resource_count = RTS_UI_MAX_RESOURCES;
    for (int i = 0; i < resource_count; ++i)
        ST_drawResource(st, app, &def->resources[i],
                        map ? map->player_resources[0][i] : 0);
}

void ST_Shutdown(st_state_t *st) {
    if (!st) return;
    for (int i = 0; i < RTS_UI_MAX_LAYERS; ++i) {
        if (st->textures[i]) SDL_DestroyTexture(st->textures[i]);
    }
    memset(st, 0, sizeof(*st));
}
