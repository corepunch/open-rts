#define _DEFAULT_SOURCE
#include "sb_bar.h"
#include "info.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    irect_t outer;
    irect_t header;
    irect_t status;
    irect_t commands;
    irect_t money;
    irect_t days;
    irect_t minimap;
    irect_t message;
    irect_t build;
    irect_t tabs[3];
    irect_t buttons[8];
} UiLayout;

typedef struct {
    int id;
    int frame;
    char label[40];
} SidebarCommand;

typedef struct {
    SidebarCommand commands[6];
    int command_count;
} Sidebar;

typedef struct {
    bool active;
    bool font_ready;
    bitmapfont_t font;
    spritesheet_t background;
    Sidebar sidebar;
    uint64_t clock;
} sb_state_t;

typedef StaticProductDefinition ProductButton;

enum {
    CLIENT_PRODUCT_UNIT = 2,
    CLIENT_MT_TROOPER = 1,
    CLIENT_MT_REAPER = 4,
    CLIENT_MT_THUNDERBOLT = 5,
    CLIENT_MT_CYBORG = 6,
    CLIENT_MT_SCOUT = 7,
    CLIENT_MT_EXPLOITER = 3,
    CLIENT_MT_EXCOPOD = 1000,
    CLIENT_MT_BRRKPOD = 1001,
    CLIENT_MT_ROBOPOD = 1002,
    CLIENT_MT_ROBOPOD2 = 1003,
    CLIENT_MT_SCNCPOD = 1004,
    CLIENT_MT_SCNCPOD2 = 1005,
    CLIENT_MT_RSCHPOD = 1006,
    CLIENT_PRODUCTION_BUILD_GROUP = 6,
    CLIENT_TRSCBUILD_FIRST_FRAME = 12,
};

static void sidebar_defaults(Sidebar *sidebar) {
    if (!sidebar) return;
    const int ids[6] = { 150, 33, 35, 36, 37, 143 };
    const int frames[6] = { 62, 63, 65, 66, 74, 2 };
    const char *labels[6] = {
        "Stop",
        "Move Only",
        "Move & Attack",
        "Set waypoints",
        "Deploy",
        "Second Attack",
    };
    memset(sidebar, 0, sizeof(*sidebar));
    sidebar->command_count = 6;
    for (int i = 0; i < sidebar->command_count; ++i) {
        sidebar->commands[i].id = ids[i];
        sidebar->commands[i].frame = frames[i];
        snprintf(sidebar->commands[i].label, sizeof(sidebar->commands[i].label), "%s", labels[i]);
    }
}

static irect_t ui_rect(const app_t *app, int x, int y, int w, int h) {
    int win_w = app && app->win.w > 0 ? app->win.w : 640;
    int win_h = app && app->win.h > 0 ? app->win.h : 480;
    irect_t r = {
        x >= 516 ? win_w - (640 - x) : x,
        y >= 455 ? win_h - (480 - y) : y,
        w,
        h,
    };
    if (r.w < 1 && w > 0) r.w = 1;
    if (r.h < 1 && h > 0) r.h = 1;
    return r;
}

static SidebarCommand *sidebar_command(Sidebar *sidebar, int id) {
    if (!sidebar) return NULL;
    for (int i = 0; i < sidebar->command_count; ++i)
        if (sidebar->commands[i].id == id) return &sidebar->commands[i];
    return NULL;
}

static void sidebar_load(Sidebar *sidebar, const char *data_root) {
    if (!sidebar || !data_root) return;
    char path[1024];
    M_PathJoin(path, sizeof(path), data_root, "INTRFACE/MAINE");
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return;
    char *text = malloc(blob.size + 1);
    if (!text) {
        W_FreeFile(&blob);
        return;
    }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';
    W_FreeFile(&blob);

    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char nl = *next;
            *next++ = '\0';
            if (nl == '\r' && *next == '\n') next++;
        }
        while (isspace((unsigned char)*line)) line++;
        if (*line != '%' && *line != '\0') {
            int id = 0;
            char label[40] = { 0 };
            if (sscanf(line, "textmsg %d %39[^\r\n]", &id, label) == 2) {
                SidebarCommand *cmd = sidebar_command(sidebar, id);
                if (cmd) {
                    size_t len = strlen(label);
                    while (len > 0 && isspace((unsigned char)label[len - 1])) label[--len] = '\0';
                    snprintf(cmd->label, sizeof(cmd->label), "%s", label);
                }
            } else {
                char kind[16] = { 0 };
                int desc = 0, x = 0, y = 0, w = 0, h = 0, frame = 0, pushed = 0;
                if (sscanf(line, "%15s %d %d %d %d %d %d %d %d",
                           kind, &id, &desc, &x, &y, &w, &h, &frame, &pushed) == 9 &&
                    (strcmp(kind, "pushb") == 0 || strcmp(kind, "checkb") == 0)) {
                    SidebarCommand *cmd = sidebar_command(sidebar, id);
                    if (cmd) cmd->frame = frame;
                }
            }
        }
        line = next;
    }
    free(text);
}

int DC_SB_WorldViewportWidth(const app_t *app) {
    if (!app) return 0;
    int w = app->win.w - 124;
    return w > 0 ? w : 1;
}

static UiLayout ui_layout(const app_t *app) {
    UiLayout layout;
    memset(&layout, 0, sizeof(layout));
    layout.outer = ui_rect(app, 516, 0, 124, 480);
    layout.minimap = ui_rect(app, 520, 5, 96, 84);
    layout.commands = ui_rect(app, 516, 92, 124, 330);
    layout.status = ui_rect(app, 518, 368, 59, 41);
    layout.money = ui_rect(app, 524, 456, 72, 17);
    layout.days = ui_rect(app, 613, 433, 3, 1);
    layout.message = ui_rect(app, 50, 462, 427, 11);
    layout.build = ui_rect(app, 516, 422, 86, 27);
    layout.header = ui_rect(app, 516, 0, 124, 92);
    layout.tabs[0] = ui_rect(app, 518, 92, 40, 20);
    layout.tabs[1] = ui_rect(app, 557, 92, 41, 20);
    layout.tabs[2] = ui_rect(app, 598, 92, 40, 20);

    const int button_y[6] = { 112, 153, 194, 235, 276, 317 };
    for (int i = 0; i < 6; ++i) {
        layout.buttons[i] = ui_rect(app, 518, button_y[i], 59, 41);
    }
    return layout;
}

static irect_t product_button_rect(const app_t *app, int index) {
    int col = index / 4;
    int row = index % 4;
    return ui_rect(app, 518 + col * 59, 112 + row * 41, 59, 41);
}

static void dc_ui_set_draw(SDL_Renderer *renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

static void dc_ui_fill(SDL_Renderer *renderer, irect_t rect, SDL_Color color) {
    dc_ui_set_draw(renderer, color);
    SDL_RenderFillRect(renderer, &rect);
}

static void dc_ui_stroke(SDL_Renderer *renderer, irect_t rect, SDL_Color color) {
    dc_ui_set_draw(renderer, color);
    SDL_RenderDrawRect(renderer, &rect);
}

static void dc_ui_draw_sprite_fit(SDL_Renderer *renderer, const spritesheet_t *sprite, int frame,
                                  irect_t box, uint32_t render_flags) {
    if (!renderer || !sprite || !sprite->texture || sprite->frame_count <= 0) return;
    if (frame < 0 || frame >= sprite->frame_count) frame = 0;
    irect_t src = sprite->frames[frame];
    if (sprite->frame_bounds && sprite->frame_bounds[frame].w > 0 && sprite->frame_bounds[frame].h > 0) {
        irect_t bounds = sprite->frame_bounds[frame];
        src.x += bounds.x;
        src.y += bounds.y;
        src.w = bounds.w;
        src.h = bounds.h;
    }
    if (src.w <= 0 || src.h <= 0 || box.w <= 0 || box.h <= 0) return;
    int draw_w = box.w;
    int draw_h = src.h * draw_w / src.w;
    if (draw_h > box.h) {
        draw_h = box.h;
        draw_w = src.w * draw_h / src.h;
    }
    if (draw_w <= 0) draw_w = 1;
    if (draw_h <= 0) draw_h = 1;
    irect_t dst = {
        box.x + (box.w - draw_w) / 2,
        box.y + (box.h - draw_h) / 2,
        draw_w,
        draw_h,
    };
    SDL_RendererFlip flip = (render_flags & RTS_FRAME_FLIP_X) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    SDL_RenderCopyEx(renderer, sprite->texture, &src, &dst, 0.0, NULL, flip);
}

static void dc_ui_draw_image_part(SDL_Renderer *renderer, const spritesheet_t *image,
                                  irect_t src, irect_t dst) {
    if (!renderer || !image || !image->texture || src.w <= 0 || src.h <= 0 ||
        dst.w <= 0 || dst.h <= 0) {
        return;
    }
    SDL_RenderCopy(renderer, image->texture, &src, &dst);
}

static const mobj_t *dc_first_selected_unit(const mobj_t *units, int unit_count) {
    if (!units) return NULL;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].selected && !units[i].remove) return &units[i];
    }
    return NULL;
}

static bool dc_product_prerequisites_met(const mobj_t *units, int unit_count,
                                         const ProductButton *product) {
    return G_ModelProductAvailableForUnits(units, unit_count, product);
}

static bool dc_selected_unit_is_player_building(const mobj_t *selected) {
    return selected && selected->owner == 0 && !selected->remove && selected->hp > 0 &&
        selected->type_id >= CLIENT_MT_EXCOPOD;
}

static int dc_products_for_selected_building(const mobj_t *selected, const mobj_t *units,
                                             int unit_count,
                                             const ProductButton *out[8]) {
    if (!dc_selected_unit_is_player_building(selected) || !out) return 0;
    int count = 0;
    static ProductButton products[RTS_MODEL_MAX_SNAPSHOT_UNITS];
    int source_count = G_ModelGetProducts(NULL, 0, products,
                                          (int)(sizeof(products) / sizeof(products[0])));
    for (int i = 0; i < source_count && count < 8; ++i) {
        const ProductButton *product = &products[i];
        bool this_maker = false;
        for (int maker = 0; maker < product->maker_count; ++maker) {
            if (product->makers[maker] == (int)selected->type_id) {
                this_maker = true;
                break;
            }
        }
        if (!this_maker) continue;
        if (!dc_product_prerequisites_met(units, unit_count, product)) continue;
        out[count++] = product;
    }
    return count;
}

static const char *dc_selected_building_label(const mobj_t *selected) {
    if (!selected) return "";
    switch (selected->type_id) {
    case CLIENT_MT_EXCOPOD: return "Exo-Ctr";
    case CLIENT_MT_BRRKPOD: return "Barracks";
    case CLIENT_MT_ROBOPOD: return "Robo-Ftr";
    case CLIENT_MT_ROBOPOD2: return "Robo-Ftr+";
    case CLIENT_MT_SCNCPOD: return "Sci-Pod";
    case CLIENT_MT_SCNCPOD2: return "Sci-Pod+";
    case CLIENT_MT_RSCHPOD: return "Rsch-Bay";
    default: return "";
    }
}

static int dc_sidebar_command_frame(const SidebarCommand *cmd, const mobj_t *selected) {
    if (!cmd) return 0;
    (void)selected;
    return cmd->frame;
}

static const char *dc_sidebar_command_label(const SidebarCommand *cmd,
                                            const mobj_t *selected) {
    if (!cmd) return "";
    (void)selected;
    if (cmd->id == 37) {
        return "Dig";
    }
    return cmd->label;
}

static void dc_stop_selected_units(mobj_t *units, int unit_count) {
    for (int i = 0; i < unit_count; ++i) {
        if (!units[i].selected) continue;
        units[i].movement.path_len = 0;
        units[i].movement.path_index = 0;
        units[i].attack.target = -1;
        units[i].harvest.target = -1;
        units[i].harvest.timer_ms = 0;
        units[i].movement.goal = fixedvec3_xy_to_fvec2(units[i].core.position);
        units[i].movement.order_id = 0;
        units[i].movement.order_arrived = false;
        units[i].core.momentum = fixedvec3_zero();
    }
}

static bool dc_SB_responder(const app_t *app, level_t *map,
                            mobj_t *units, int unit_count, const SDL_Event *e) {
    if (!app || !map || !e) return false;
    if (e->type != SDL_MOUSEBUTTONDOWN) return false;
    int rx = 0, ry = 0;
    R_WindowToRenderPt(app, e->button.x, e->button.y, &rx, &ry);
    UiLayout layout = ui_layout(app);
    if (!irect_contains(layout.outer, (ivec2_t){ rx, ry })) return false;
    int selected_index = -1;
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].selected && !units[i].remove) {
            selected_index = i;
            break;
        }
    }
    mobj_t *selected = selected_index >= 0 ? &units[selected_index] : NULL;
    if (dc_selected_unit_is_player_building(selected)) {
        const ProductButton *products[8] = { 0 };
        int product_count = dc_products_for_selected_building(selected, units, unit_count, products);
        if (e->button.button == SDL_BUTTON_LEFT) {
            for (int i = 0; i < product_count; ++i) {
                if (!irect_contains(product_button_rect(app, i),
                                    (ivec2_t){ rx, ry })) continue;
                const ProductButton *product = products[i];
                uint16_t actor_id = G_ModelActorIdForProduct(product);
                if (actor_id == 0 || map->player_resources[0][0] < product->cost) return true;
                if (G_ModelEnqueueProduction(selected, product, actor_id)) {
                    map->player_resources[0][0] -= product->cost;
                }
                return true;
            }
        }
        return true;
    }
    if (e->button.button == SDL_BUTTON_LEFT &&
        irect_contains(layout.buttons[0], (ivec2_t){ rx, ry })) {
        dc_stop_selected_units(units, unit_count);
    }
    return true;
}

static void dc_ui_draw_minimap(app_t *app, const level_t *map, const mobj_t *units, int unit_count,
                               irect_t rect) {
    if (!app || !map || map->width <= 0 || map->height <= 0) return;
    dc_ui_fill(app->renderer, rect, (SDL_Color){ 4, 8, 9, 255 });
    irect_t clip = { rect.x + 3, rect.y + 3, rect.w - 6, rect.h - 6 };
    if (clip.w <= 0 || clip.h <= 0) return;
    for (int py = 0; py < clip.h; ++py) {
        int screen_y = py * map->height / clip.h;
        int gy = L_ScreenY(map, screen_y);
        for (int px = 0; px < clip.w; ++px) {
            int gx = px * map->width / clip.w;
            uint32_t color = map->cell_colors ? map->cell_colors[L_Index(map, gx, gy)] : 0xff202820u;
            uint8_t r = (uint8_t)(color >> 16);
            uint8_t g = (uint8_t)(color >> 8);
            uint8_t b = (uint8_t)color;
            SDL_SetRenderDrawColor(app->renderer, r / 2, g / 2, b / 2, 255);
            SDL_RenderDrawPoint(app->renderer, clip.x + px, clip.y + py);
        }
    }
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        int x = clip.x + vent->cell.x * clip.w / map->width;
        int y = clip.y + (int)(L_ScreenY(map, vent->cell.y) * clip.h / map->height);
        irect_t dot = { x - 1, y - 1, 3, 3 };
        dc_ui_fill(app->renderer, dot, vent->active ?
                   (SDL_Color){ 89, 226, 184, 255 } : (SDL_Color){ 68, 86, 84, 255 });
    }
    for (int i = 0; i < unit_count; ++i) {
        fvec2_t position = fixedvec3_xy_to_fvec2(units[i].core.position);
        if (units[i].hidden || units[i].remove || position.x < 0.0f || position.y < 0.0f) continue;
        int x = clip.x + (int)(position.x * (float)clip.w / (float)map->width);
        int y = clip.y + (int)(L_ScreenYF(map, position.y) *
                                (float)clip.h / (float)map->height);
        irect_t dot = { x - 1, y - 1, 2, 2 };
        dc_ui_fill(app->renderer, dot, units[i].owner == 0 ?
                   (SDL_Color){ 218, 214, 135, 255 } : (SDL_Color){ 204, 68, 72, 255 });
    }
    int world_right = app->win.w - 124;
    cell_t tl = R_ScreenToGrid(app, 0, 0);
    cell_t br = R_ScreenToGrid(app, world_right, app->win.h);
    int vx = clip.x + tl.x * clip.w / map->width;
    int vy = clip.y + tl.y * clip.h / map->height;
    int vw = (br.x - tl.x) * clip.w / map->width;
    int vh = (br.y - tl.y) * clip.h / map->height;
    if (vw < 3) vw = 3;
    if (vh < 3) vh = 3;
    irect_t view = { vx, vy, vw, vh };
    dc_ui_stroke(app->renderer, view, (SDL_Color){ 164, 236, 203, 220 });
    dc_ui_stroke(app->renderer, rect, (SDL_Color){ 72, 91, 88, 255 });
}

static void dc_ui_draw_text_right(SDL_Renderer *renderer, const bitmapfont_t *font,
                                  irect_t rect, int y, const char *text,
                                  SDL_Color color) {
    if (!renderer || !font || !text) return;
    int x = rect.x + rect.w - 3 - HU_TextWidth(font, text, 1);
    if (x < rect.x + 2) x = rect.x + 2;
    HU_DrawText(renderer, font, x, y, text, color, 1);
}

static void dc_ui_draw_status(app_t *app, const level_t *map,
                              const bitmapfont_t *font,
                              const UiLayout *layout,
                              const spritecache_t *cache,
                              uint64_t clock) {
    if (!app || !map || !font || !layout) return;
    char text[32];
    const spritesheet_t *buttons = R_CacheLookup(cache, "INTRFACE/MAINBUT.SPR");
    if (buttons && buttons->texture)
        dc_ui_draw_sprite_fit(app->renderer, buttons, 104, layout->money, 0);
    int resources = map->player_resources[0][0];
    if (resources < 0) resources = 0;
    snprintf(text, sizeof(text), "%d", resources);
    dc_ui_draw_text_right(app->renderer, font, layout->money,
                          layout->money.y + 2, text,
                          (SDL_Color){ 41, 217, 230, 255 });

    int days = map->day_rate > 0 ?
        (int)(clock / (uint64_t)map->day_rate / 2u) : 0;
    if (days > 999) days = 999;
    snprintf(text, sizeof(text), "%03d", days);
    int x = layout->days.x - HU_TextWidth(font, text, 1) / 2;
    HU_DrawTextRemapped(app->renderer, font, x, layout->days.y, text,
                        (SDL_Color){ 255, 255, 255, 255 }, 1, 0);
}

static void dc_SB_drawer(app_t *app, const level_t *map,
                         const mobj_t *units, int unit_count,
                         const spritecache_t *cache, const bitmapfont_t *font,
                         const Sidebar *sidebar,
                         const spritesheet_t *background) {
    if (!app || !font || !font->sprite.texture) return;
    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(app->renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    UiLayout layout = ui_layout(app);
    if (background && background->texture) {
        dc_ui_draw_image_part(app->renderer, background,
                              (irect_t){ 516, 0, 124, 480 }, layout.outer);
        dc_ui_draw_image_part(app->renderer, background,
                              (irect_t){ 0, 455, 516, 25 },
                              ui_rect(app, 0, 455, 516, 25));
    } else {
        dc_ui_fill(app->renderer, layout.outer, (SDL_Color){ 2, 2, 2, 255 });
        dc_ui_fill(app->renderer, ui_rect(app, 0, 455, 640, 25),
                   (SDL_Color){ 3, 3, 3, 255 });
        dc_ui_stroke(app->renderer, layout.outer, (SDL_Color){ 178, 178, 178, 255 });
        dc_ui_stroke(app->renderer, ui_rect(app, 0, 455, 640, 18),
                     (SDL_Color){ 164, 164, 164, 255 });
        dc_ui_stroke(app->renderer, layout.minimap, (SDL_Color){ 154, 154, 154, 255 });
        dc_ui_stroke(app->renderer, ui_rect(app, 516, 0, 107, 92),
                     (SDL_Color){ 86, 86, 86, 255 });
        dc_ui_stroke(app->renderer, ui_rect(app, 516, 92, 124, 363),
                     (SDL_Color){ 154, 154, 154, 255 });

        for (int i = 0; i < 3; ++i) {
            dc_ui_fill(app->renderer, layout.tabs[i], (SDL_Color){ 126, 126, 126, 255 });
            dc_ui_stroke(app->renderer, layout.tabs[i], (SDL_Color){ 38, 38, 38, 255 });
            char tab[2] = { (char)('1' + i), '\0' };
            HU_DrawText(app->renderer, font,
                               layout.tabs[i].x + layout.tabs[i].w / 2 - HU_TextWidth(font, tab, 1) / 2,
                               layout.tabs[i].y + layout.tabs[i].h / 2 - font->line_h / 2,
                               tab, (SDL_Color){ 24, 24, 24, 255 }, 1);
        }
    }

    SDL_Color dim = { 112, 130, 125, 255 };
    SDL_Color amber = { 231, 194, 94, 255 };
    char line[96];

    const spritesheet_t *buttons = R_CacheLookup(cache, "INTRFACE/MAINBUT.SPR");
    irect_t mini = {
        layout.minimap.x + 2,
        layout.minimap.y + 2,
        layout.minimap.w - 4,
        layout.minimap.h - 4,
    };
    dc_ui_draw_minimap(app, map, units, unit_count, mini);

    Sidebar fallback_sidebar;
    if (!sidebar) {
        sidebar_defaults(&fallback_sidebar);
        sidebar = &fallback_sidebar;
    }
    int hover_button = -1;
    const mobj_t *selected = dc_first_selected_unit(units, unit_count);
    const ProductButton *products[8] = { 0 };
    int product_count = dc_products_for_selected_building(selected, units, unit_count, products);
    bool product_mode = dc_selected_unit_is_player_building(selected);
    int visible_button_count = product_mode ? product_count : sidebar->command_count;
    for (int i = 0; i < visible_button_count; ++i) {
        irect_t button_rect = product_mode ? product_button_rect(app, i) :
            layout.buttons[i];
        if (irect_contains(button_rect, app->mouse)) {
            hover_button = i;
            break;
        }
    }
    if (!background || !background->texture) {
        dc_ui_fill(app->renderer, layout.build, (SDL_Color){ 160, 160, 160, 255 });
        dc_ui_stroke(app->renderer, layout.build, (SDL_Color){ 39, 39, 39, 255 });
        HU_DrawText(app->renderer, font,
                           layout.build.x + layout.build.w / 2 - HU_TextWidth(font, "BUILD", 5) / 2,
                           layout.build.y + layout.build.h / 2 - font->line_h / 2,
                           "BUILD", (SDL_Color){ 24, 24, 24, 255 }, 1);
    }
    if (hover_button >= 0) {
        if (product_mode && products[hover_button]) {
            snprintf(line, sizeof(line), "%s %d",
                     products[hover_button]->label, products[hover_button]->cost);
            HU_DrawText(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                           line,
                           map->player_resources[0][0] >= products[hover_button]->cost ?
                           amber : (SDL_Color){ 208, 103, 88, 255 },
                           1);
        } else {
            HU_DrawText(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                           dc_sidebar_command_label(&sidebar->commands[hover_button], selected),
                           amber, 1);
        }
    } else if (product_mode) {
        if (selected && selected->production.queue_count > 0 &&
            selected->production.time_ms > 0) {
            int done = selected->production.time_ms - selected->production.time_left_ms;
            int pct = done * 100 / selected->production.time_ms;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            snprintf(line, sizeof(line), "Training x%d %d%%",
                     selected->production.queue_count, pct);
        } else {
            snprintf(line, sizeof(line), "%s", dc_selected_building_label(selected));
        }
        if (line[0] != '\0') {
            HU_DrawText(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                           line, dim, 1);
        }
    }
    int button_slots = product_mode ? 8 : 6;
    for (int i = 0; i < button_slots; ++i) {
        irect_t button_rect = product_mode ? product_button_rect(app, i) :
            layout.buttons[i];
        if (product_mode && i >= product_count) {
            dc_ui_fill(app->renderer, button_rect, (SDL_Color){ 10, 12, 12, 185 });
            dc_ui_stroke(app->renderer, button_rect, (SDL_Color){ 66, 72, 70, 255 });
            continue;
        }
        int frame = product_mode && products[i] ? products[i]->icon_frame :
            dc_sidebar_command_frame(&sidebar->commands[i], selected);
        if (buttons && buttons->texture) {
            dc_ui_draw_sprite_fit(app->renderer, buttons, frame, button_rect, 0);
            if (product_mode && products[i] && map->player_resources[0][0] < products[i]->cost) {
                dc_ui_fill(app->renderer, button_rect, (SDL_Color){ 0, 0, 0, 105 });
            }
        } else {
            SDL_Color fill = (i == 0 && !product_mode) ? (SDL_Color){ 150, 150, 145, 255 } :
                             (SDL_Color){ 175, 175, 168, 255 };
            dc_ui_fill(app->renderer, button_rect, fill);
            dc_ui_stroke(app->renderer, button_rect, i == 0 && !product_mode ?
                         (SDL_Color){ 136, 58, 53, 255 } : (SDL_Color){ 72, 95, 88, 255 });
        }
    }
    SDL_SetRenderDrawBlendMode(app->renderer, old_blend);
}

static void render_hud_messages(app_t *app, const hudtext_t *hud, const bitmapfont_t *font) {
    if (!app || !hud || !font || !font->sprite.texture || hud->count <= 0) return;
    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(app->renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    UiLayout layout = ui_layout(app);
    char message[62];
    snprintf(message, sizeof(message), "%.61s", hud->messages[hud->count - 1].text);
    HU_DrawTextRemapped(app->renderer, font, layout.message.x, layout.message.y,
                        message, (SDL_Color){ 255, 255, 255, 255 }, 1, 2);
    SDL_SetRenderDrawBlendMode(app->renderer, old_blend);
}

void *DC_SB_Init(app_t *app, const char *data_root) {
    if (!app || !data_root) return NULL;
    sb_state_t *sb = calloc(1, sizeof(sb_state_t));
    if (!sb) return NULL;
    sb->active = true;
    sidebar_defaults(&sb->sidebar);

    sb->font_ready = HU_LoadFont(app->renderer, data_root, &sb->font);
    if (!sb->font_ready)
        fprintf(stderr, "warning: failed to create Dark Colony UI font\n");
    sidebar_load(&sb->sidebar, data_root);

    char path[1024];
    M_PathJoin(path, sizeof(path), data_root, "INTRFACE/INTRFACE.GIF");
    if (!W_LoadGIFTexture(app->renderer, path, &sb->background))
        fprintf(stderr, "warning: failed to load Dark Colony UI background %s\n", path);
    return sb;
}

bool DC_SB_Responder(void *sb_ptr, const app_t *app, level_t *map,
                  mobj_t *units, int unit_count, const SDL_Event *event) {
    sb_state_t *sb = sb_ptr;
    return sb && sb->active &&
           dc_SB_responder(app, map, units, unit_count, event);
}

void DC_SB_Ticker(void *sb_ptr) {
    sb_state_t *sb = sb_ptr;
    if (sb && sb->active) sb->clock++;
}

void DC_SB_Drawer(void *sb_ptr, app_t *app, const level_t *map,
               const mobj_t *units, int unit_count,
               const spritecache_t *sprites, const hudtext_t *hud) {
    sb_state_t *sb = sb_ptr;
    if (!sb || !sb->active || !sb->font_ready) return;
    dc_SB_drawer(app, map, units, unit_count, sprites, &sb->font,
                 &sb->sidebar, &sb->background);
    UiLayout layout = ui_layout(app);
    dc_ui_draw_status(app, map, &sb->font, &layout, sprites, sb->clock);
    render_hud_messages(app, hud, &sb->font);
}

void DC_SB_Shutdown(void *sb_ptr) {
    sb_state_t *sb = sb_ptr;
    if (!sb) return;
    R_FreeSprite(&sb->background);
    HU_FreeFont(&sb->font);
    free(sb);
}
