#include "game_ui.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static SDL_Rect ui_scaled_rect(const App *app, const GameUiDefinition *def, SDL_Rect rect) {
    float sx = (float)app->win_w / (float)def->logical_width;
    float sy = (float)app->win_h / (float)def->logical_height;
    return (SDL_Rect){
        (int)((float)rect.x * sx), (int)((float)rect.y * sy),
        (int)((float)rect.w * sx), (int)((float)rect.h * sy),
    };
}

bool game_ui_load(GameUi *ui, SDL_Renderer *renderer, const char *data_root,
                  const GameUiDefinition *definition) {
    if (!ui || !renderer || !data_root || !definition ||
        definition->image_count < 0 || definition->image_count > RTS_UI_MAX_LAYERS) return false;
    memset(ui, 0, sizeof(*ui));
    ui->definition = definition;
    for (int i = 0; i < definition->image_count; ++i) {
        char path[1024];
        path_join(path, sizeof(path), data_root, definition->images[i].asset_path);
        SDL_Surface *surface = SDL_LoadBMP(path);
        if (!surface) {
            fprintf(stderr, "warning: failed to load UI asset %s: %s\n", path, SDL_GetError());
            game_ui_destroy(ui);
            return false;
        }
        ui->textures[i] = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);
        if (!ui->textures[i]) {
            game_ui_destroy(ui);
            return false;
        }
    }
    ui->ready = true;
    return true;
}

static void game_ui_draw_minimap(const GameUi *ui, App *app, const GameMap *map,
                                 const Unit *units, int unit_count) {
    SDL_Rect rect = ui_scaled_rect(app, ui->definition, ui->definition->minimap);
    if (rect.w <= 0 || rect.h <= 0 || !map || map->width <= 0 || map->height <= 0) return;
    SDL_SetRenderDrawColor(app->renderer, 5, 7, 7, 255);
    SDL_RenderFillRect(app->renderer, &rect);
    for (int i = 0; i < map->decoration_count; ++i) {
        const MapDecoration *dec = &map->decorations[i];
        int x = rect.x + dec->gx * rect.w / map->width;
        int y = rect.y + map_screen_y_for_cell(map, dec->gy) * rect.h / map->height;
        SDL_SetRenderDrawColor(app->renderer, dec->solid ? 93 : 63,
                              dec->solid ? 91 : 79, dec->solid ? 70 : 52, 255);
        SDL_RenderDrawPoint(app->renderer, x, y);
    }
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].remove || units[i].hp <= 0) continue;
        int x = rect.x + (int)(units[i].gx * (float)rect.w / (float)map->width);
        int y = rect.y + (int)(map_screen_y_for_point(map, units[i].gy) * (float)rect.h /
                              (float)map->height);
        SDL_SetRenderDrawColor(app->renderer, units[i].owner == 0 ? 48 : 210,
                              units[i].owner == 0 ? 220 : 45, 65, 255);
        SDL_Rect dot = { x - 1, y - 1, 3, 3 };
        SDL_RenderFillRect(app->renderer, &dot);
    }
    int cell_w = app->cell_w > 0 ? app->cell_w : 24;
    int cell_h = app->cell_h > 0 ? app->cell_h : 24;
    float left = -app->cam_x / (float)cell_w;
    float top_screen = -app->cam_y / (float)cell_h;
    SDL_Rect view = {
        rect.x + (int)(left * (float)rect.w / (float)map->width),
        rect.y + (int)(top_screen * (float)rect.h / (float)map->height),
        app->win_w * rect.w / (cell_w * map->width),
        app->win_h * rect.h / (cell_h * map->height),
    };
    SDL_SetRenderDrawColor(app->renderer, 215, 215, 205, 255);
    SDL_RenderDrawRect(app->renderer, &view);
}

static void draw_digit(SDL_Renderer *renderer, int x, int y, int digit, SDL_Color color) {
    static const unsigned char segments[10] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
    };
    const SDL_Rect bars[7] = {
        {2,0,8,2},{10,2,2,8},{10,12,2,8},{2,20,8,2},{0,12,2,8},{0,2,2,8},{2,10,8,2},
    };
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    unsigned char mask = segments[digit];
    for (int i = 0; i < 7; ++i) if (mask & (1u << i)) {
        SDL_Rect bar = { x + bars[i].x, y + bars[i].y, bars[i].w, bars[i].h };
        SDL_RenderFillRect(renderer, &bar);
    }
}

static void game_ui_draw_resources(const GameUi *ui, App *app, int amount) {
    char value[24];
    snprintf(value, sizeof(value), "%d", amount);
    int count = (int)strlen(value);
    float sx = (float)app->win_w / (float)ui->definition->logical_width;
    float sy = (float)app->win_h / (float)ui->definition->logical_height;
    int x = (int)((float)ui->definition->resource_text.x * sx) - count * 7;
    int y = (int)((float)ui->definition->resource_text.y * sy);
    for (int i = 0; i < count; ++i) {
        draw_digit(app->renderer, x + i * 14, y, value[i] - '0', ui->definition->resource_color);
    }
}

static void game_ui_draw_commands(const GameUi *ui, App *app, const SpriteCache *sprites) {
    const GameUiDefinition *def = ui->definition;
    if (!sprites || def->command_columns <= 0 || def->command_rows <= 0) return;
    SDL_Rect grid = ui_scaled_rect(app, def, def->command_grid);
    int cell_w = grid.w / def->command_columns;
    int cell_h = grid.h / def->command_rows;
    int slot = 0;
    for (int i = 0; i < sprites->count && slot < def->command_columns * def->command_rows; ++i) {
        const CachedSprite *cached = &sprites->entries[i];
        if (cached->name[0] == '\0' || tolower((unsigned char)cached->name[0]) == 'a' ||
            strstr(cached->name, "sh") ||
            !cached->sprite.texture || cached->sprite.frame_count <= 0) continue;
        SDL_Rect src = cached->sprite.frames[0];
        SDL_Rect bounds = cached->sprite.frame_bounds ? cached->sprite.frame_bounds[0] :
            (SDL_Rect){ 0, 0, src.w, src.h };
        src.x += bounds.x; src.y += bounds.y; src.w = bounds.w; src.h = bounds.h;
        if (src.w <= 0 || src.h <= 0) continue;
        int col = slot % def->command_columns;
        int row = slot / def->command_columns;
        SDL_Rect cell = { grid.x + col * cell_w + 3, grid.y + row * cell_h + 3,
                          cell_w - 6, cell_h - 6 };
        float scale = fminf((float)cell.w / (float)src.w, (float)cell.h / (float)src.h);
        SDL_Rect dst = { cell.x + (cell.w - (int)((float)src.w * scale)) / 2,
                         cell.y + (cell.h - (int)((float)src.h * scale)) / 2,
                         (int)((float)src.w * scale), (int)((float)src.h * scale) };
        SDL_SetTextureColorMod(cached->sprite.texture, 210, 48, 52);
        SDL_RenderCopy(app->renderer, cached->sprite.texture, &src, &dst);
        SDL_SetTextureColorMod(cached->sprite.texture, 255, 255, 255);
        slot++;
    }
}

void game_ui_render(const GameUi *ui, App *app, const GameMap *map,
                    const Unit *units, int unit_count, const SpriteCache *sprites) {
    if (!ui || !ui->ready || !ui->definition || !app) return;
    const GameUiDefinition *def = ui->definition;
    for (int i = 0; i < def->image_count; ++i) {
        SDL_Rect dst = ui_scaled_rect(app, def, def->images[i].destination);
        const SDL_Rect *src = def->images[i].source.w > 0 && def->images[i].source.h > 0 ?
            &def->images[i].source : NULL;
        SDL_RenderCopy(app->renderer, ui->textures[i], src, &dst);
    }
    game_ui_draw_commands(ui, app, sprites);
    game_ui_draw_minimap(ui, app, map, units, unit_count);
    game_ui_draw_resources(ui, app, map ? map->player_resources[0] : 0);
}

void game_ui_destroy(GameUi *ui) {
    if (!ui) return;
    for (int i = 0; i < RTS_UI_MAX_LAYERS; ++i) {
        if (ui->textures[i]) SDL_DestroyTexture(ui->textures[i]);
    }
    memset(ui, 0, sizeof(*ui));
}
