#include "d_net.h"
#define _DEFAULT_SOURCE
#include "sb_bar.h"
#include "info.h"
#include "dc_types.h"
#include "gamestat.h"
#include "m_menu.h"

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
    int frame;
    int pushed;
    char label[40];
    irect_t rect;
    ivec2_t counter;
} SidebarCommand;

typedef struct {
    SidebarCommand controls[207]; /* Native MAINE control IDs. */
    int tab;
    int targeting;
    dc_waypoints_t waypoints;
    bool assault;
    int bright_pushed, bright_highlight;
} Sidebar;

typedef struct {
    bool active;
    bool font_ready;
    bitmapfont_t font;
    spritesheet_t background;
    Sidebar sidebar;
} sb_state_t;

typedef StaticProductDefinition ProductButton;

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
    sidebar->assault = true;
    for (int i = 0; i < 6; ++i) {
        sidebar->controls[ids[i]].frame = frames[i];
        sidebar->controls[ids[i]].rect = (irect_t){518,112 + i * 41,59,41};
        snprintf(sidebar->controls[ids[i]].label, sizeof(sidebar->controls[ids[i]].label), "%s", labels[i]);
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
    return sidebar && id >= 0 && id < 207 ? &sidebar->controls[id] : NULL;
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
            sscanf(line, "bright_pushed %d", &sidebar->bright_pushed);
            sscanf(line, "bright_highlight %d", &sidebar->bright_highlight);
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
                int control_id, description, normal, pressed;
                irect_t rect;
                if (sscanf(line, "count %d %d %d %d %d %d %d %d",
                           &control_id, &description, &rect.x, &rect.y, &rect.w, &rect.h,
                           &normal, &pressed) == 8 && control_id >= 0 && control_id < 207)
        {
                    sidebar->controls[control_id].rect = rect;
                    sidebar->controls[control_id].pushed = pressed;
                    const char *offset = strstr(line, "offset");
                    if (offset) sscanf(offset, "offset %d %d", &sidebar->controls[control_id].counter.x,
                                       &sidebar->controls[control_id].counter.y);
                }
                char kind[16] = { 0 };
                int desc = 0, x = 0, y = 0, w = 0, h = 0, frame = 0, pushed = 0;
                if (sscanf(line, "%15s %d %d %d %d %d %d %d %d",
                           kind, &id, &desc, &x, &y, &w, &h, &frame, &pushed) == 9 &&
                    (strcmp(kind, "pushb") == 0 || strcmp(kind, "checkb") == 0)) {
                    SidebarCommand *cmd = sidebar_command(sidebar, id);
                    if (cmd) {
                        cmd->frame = frame;
                        cmd->pushed = pushed;
                        cmd->rect = (irect_t){x,y,w,h};
                    }
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

static irect_t product_button_rect(const app_t *app, const Sidebar *sidebar,
                                    const ProductButton *product) {
    irect_t r = sidebar->controls[product->ui_id].rect;
    return ui_rect(app, r.x, r.y, r.w, r.h);
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

static void dc_ui_draw_sprite(SDL_Renderer *renderer, const spritesheet_t *sprite, int frame,
                              irect_t box, int palette) {
    if (!renderer || !sprite || !sprite->lumps || frame < 0 || frame >= sprite->numlumps) return;
    const spritecell_t *cell = &sprite->cells[frame];
    ivec2_t origin = ivec2_add((ivec2_t){box.x,box.y}, cell->displacement);
    irect_t dst = {origin.x,origin.y,cell->rect.w,cell->rect.h};
    R_DrawSprite(renderer, sprite, frame, palette, &cell->rect, &dst, SDL_FLIP_NONE,
                 (SDL_Color){255, 255, 255, 255}, SDL_BLENDMODE_BLEND);
}

static void dc_ui_draw_image_part(SDL_Renderer *renderer, const spritesheet_t *image,
                                  irect_t src, irect_t dst) {
    if (!renderer || !image || !image->lumps || image->numlumps <= 0 ||
        src.w <= 0 || src.h <= 0 ||
        dst.w <= 0 || dst.h <= 0) {
        return;
    }
    R_DrawSprite(renderer, image, 0, -1, &src, &dst, SDL_FLIP_NONE,
                 (SDL_Color){255, 255, 255, 255}, SDL_BLENDMODE_BLEND);
}

static const mobj_t *dc_first_selected_unit(mobj_t *const *units, int unit_count) {
    if (!units) return NULL;
    for (int i = 0; i < unit_count; ++i) {
        if (P_MobjIsSelected(units[i]) && !units[i]->remove) return units[i];
    }
    return NULL;
}

static bool dc_selected_unit_is_player_building(const mobj_t *selected) {
    return selected && selected->owner == consoleplayer && !selected->remove && selected->hp > 0 &&
        !(selected->traits & MF_MOBILE) &&
        selected->type_id >= MT_EXCOPOD;
}

static int dc_available_products(mobj_t *const *units, int unit_count, int tab,
                                  const ProductButton *out[16]) {
    static ProductButton products[64];
    int count = 0;
    bool research = tab == 1;
    int source_count = G_ModelGetProducts(NULL, consoleplayer, products, 64);
    for (int i = 0; i < source_count && count < 16; ++i) {
        const ProductButton *product = &products[i];
        if ((product->product_class == RTS_PRODUCT_UPGRADE) != research) continue;
        if (!level.purchases[consoleplayer][product->row_id].selected &&
            !G_ModelProductAvailableForUnits(units, unit_count, product)) continue;
        if (product->product_class == RTS_PRODUCT_BUILDING) {
            bool exists = false;
            for (int j = 0; j < unit_count; ++j)
                if (units[j]->owner == consoleplayer && !units[j]->remove && units[j]->hp > 0 &&
                    DC_ProductActorMatches(units[j]->type_id, G_ModelActorIdForProduct(product))) exists = true;
            if (exists) continue;
        }
        out[count++] = product;
    }
    return count;
}

static const char *dc_selected_building_label(const mobj_t *selected) {
    if (!selected) return "";
    switch (selected->type_id) {
    case MT_EXCOPOD: return "Exo-Ctr";
    case MT_BRRKPOD: return "Barracks";
    case MT_ROBOPOD: return "Robo-Ftr";
    case MT_ROBOPOD2: return "Robo-Ftr+";
    case MT_SCNCPOD: return "Sci-Pod";
    case MT_SCNCPOD2: return "Sci-Pod+";
    case MT_RSCHPOD: return "Rsch-Bay";
    case MT_ALIEN_MINDHIVE: return "Mind-Hive";
    case MT_ALIEN_WARHIVE: return "War. Fold";
    case MT_ALIEN_BRDRHIVE: return "Gene-Sac";
    case MT_ALIEN_BRDRHIVE2: return "Gene-Upgrd";
    case MT_ALIEN_MINDHIVE2: return "Breed-Pod";
    case MT_ALIEN_MINDHIVE3: return "Pod-Upgrd";
    case MT_ALIEN_RSCHIVE: return "Neur-Hive";
    default: return "";
    }
}

static int dc_commands(mobj_t *const *units, int count, int ids[6]) {
    const int primary[] = {-1,138,139,140,141,142,142,37};
    const int secondary[] = {-1,144,145,146,197,143,198};
    int first = 0, second = 0;
    ids[0] = 150; ids[1] = 33; ids[2] = 35; ids[3] = 36;
    for (int i = 0; i < count; ++i) {
        const mobj_t *actor = units[i];
        if (!P_MobjIsSelected(actor) || actor->owner != consoleplayer || actor->remove ||
            actor->hp <= 0 || actor->native_type_id >= GAMESTAT_UNIT_COUNT) continue;
        const DcGamestatUnit *type = &dc_gamestat_units[actor->native_type_id];
        unsigned flags = type->values[27]; /* DC type +0x104. */
        if (!(flags & 0x80) || ((flags & 0x40) && actor->ability_charge > 32) || actor->ability_charge == 255) {
            int ability = flags & 0x3f;
            if (ability) first = !first || first == ability ? ability : 7;
        }
        flags = type->values[28]; /* DC type +0x108. */
        if (!(flags & 0x80) || actor->ability_charge == 255) {
            int ability = flags & 0x3f;
            if (ability) second = !second || second == ability ? ability : 5;
        }
    }
    int n = 4;
    if (first > 0 && first < 8) ids[n++] = primary[first];
    if (second > 0 && second < 7) ids[n++] = secondary[second];
    return n;
}

static void finish_waypoints(Sidebar *sidebar, mobj_t *const *units, int count) {
    for (int i = 0; i < sidebar->waypoints.count; ++i)
        G_SelectedTiccmd(TC_WAYPOINT, units, count,
                        fvec2_cell_center(sidebar->waypoints.points[i]), i != 0);
    sidebar->waypoints = (dc_waypoints_t){0};
    sidebar->targeting = 0;
}

static bool dc_SB_responder(Sidebar *sidebar, app_t *app, level_t *map,
                            mobj_t *const *units, int unit_count, const SDL_Event *e) {
    if (!app || !map || !e) return false;
    if (e->type == SDL_KEYDOWN && !e->key.repeat && !(e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT))) {
        if (e->key.keysym.sym == SDLK_RETURN || e->key.keysym.sym == SDLK_KP_ENTER) {
            if (sidebar->targeting == 36) finish_waypoints(sidebar, units, unit_count);
            else G_SelectedTiccmd(TC_DEPLOY, units, unit_count, (fvec2_t){0}, 0);
            return true;
        }
        int id = e->key.keysym.sym == SDLK_s ? 150 : e->key.keysym.sym == SDLK_m ? 33 :
                 e->key.keysym.sym == SDLK_a ? 35 : e->key.keysym.sym == SDLK_w ? 36 :
                 e->key.keysym.sym == SDLK_SPACE ? 19 : -1;
        if (e->key.keysym.sym == SDLK_t) { G_QueueTiccmd(&(ticcmd_t){.order = TC_PAUSE}); return true; }
        if (id == 19) { G_QueueTiccmd(&(ticcmd_t){.order = TC_SUBMIT}); return true; }
        if (id == 150) { G_SelectedTiccmd(TC_STOP, units, unit_count, (fvec2_t){0}, 0); return true; }
        if (id == 33 || id == 35) {
            sidebar->assault = id == 35;
            G_SelectedTiccmd(TC_MODE, units, unit_count, (fvec2_t){0}, id);
            return true;
        }
        if (id == 36) { sidebar->targeting = 36; sidebar->waypoints = (dc_waypoints_t){0}; return true; }
    }
    if (e->type != SDL_MOUSEBUTTONDOWN && e->type != SDL_MOUSEBUTTONUP) return false;
    int rx = 0, ry = 0;
    R_WindowToRenderPt(app, e->button.x, e->button.y, &rx, &ry);
    UiLayout layout = ui_layout(app);
    ivec2_t mouse = {rx,ry};
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_RIGHT && sidebar->targeting) {
        if (sidebar->targeting == 36) finish_waypoints(sidebar, units, unit_count);
        else sidebar->targeting = 0;
        return true;
    }
    if (!irect_contains(layout.outer, mouse)) {
        if (!sidebar->targeting || e->button.button != SDL_BUTTON_LEFT) return false;
        if (e->type == SDL_MOUSEBUTTONUP) return true;
        if (sidebar->targeting == 36) {
            cell_t cell = R_ScreenToMapGrid(app, map, rx, ry);
            int count = sidebar->waypoints.count;
            if (count == 7 || (count && (ivec2_equal(cell, sidebar->waypoints.points[0]) ||
                                         ivec2_equal(cell, sidebar->waypoints.points[count - 1]))))
                finish_waypoints(sidebar, units, unit_count);
            else sidebar->waypoints.points[sidebar->waypoints.count++] = cell;
        }
        return true;
    }
    if (e->type == SDL_MOUSEBUTTONUP) return true;
    if (e->button.button == SDL_BUTTON_LEFT && irect_contains(layout.minimap, mouse)) {
        /* 0x4097f8: native minimap uses (519,90), 96x84, bottom-up Y. */
        fvec2_t position = {
            (float)map->width * (2 * (rx - (app->win.w - 121)) + 1) / 192.0f,
            (float)map->height * (2 * (90 - ry) + 1) / 168.0f
        };
        fvec2_t screen;
        R_MapToScreen(app, map, position.x, position.y, &screen.x, &screen.y);
        app->cam = fvec2_add(app->cam, fvec2_sub(
            (fvec2_t){DC_SB_WorldViewportWidth(app) / 2.0f, 455 / 2.0f}, screen));
        return true;
    }
    if (e->button.button == SDL_BUTTON_LEFT) {
        for (int i = 0; i < 3; ++i)
            if (irect_contains(layout.tabs[i], mouse)) { sidebar->tab = i; sidebar->targeting = 0; return true; }
        if (irect_contains(layout.build, mouse)) {
            G_QueueTiccmd(&(ticcmd_t){.order = TC_SUBMIT});
            return true;
        }
    }
    int selected_index = -1;
    for (int i = 0; i < unit_count; ++i) {
        if (P_MobjIsSelected(units[i]) && !units[i]->remove) {
            selected_index = i;
            break;
        }
    }
    mobj_t *selected = selected_index >= 0 ? units[selected_index] : NULL;
    if (sidebar->tab == 2) {
        const int ids[] = {62,63,64,151,196,202};
        for (int i = 0; i < 6; ++i) {
            const SidebarCommand *control = &sidebar->controls[ids[i]];
            irect_t r = ui_rect(app, control->rect.x, control->rect.y, control->rect.w, control->rect.h);
            if (e->button.button != SDL_BUTTON_LEFT || !irect_contains(r, mouse)) continue;
            if (ids[i] == 62) DC_OpenQuitDialog(app);
            if (ids[i] == 64) M_StartControlPanel(app);
            if (ids[i] == 196) G_QueueTiccmd(&(ticcmd_t){.order = TC_PAUSE});
            return true;
        }
        return true;
    }
    if (sidebar->tab == 1 || !selected || dc_selected_unit_is_player_building(selected)) {
        const ProductButton *products[16] = { 0 };
        int product_count = dc_available_products(units, unit_count, sidebar->tab, products);
        if (e->button.button == SDL_BUTTON_LEFT || e->button.button == SDL_BUTTON_RIGHT) {
            for (int i = 0; i < product_count; ++i) {
                if (!irect_contains(product_button_rect(app, sidebar, products[i]),
                                    (ivec2_t){ rx, ry })) continue;
                const ProductButton *product = products[i];
                G_QueueTiccmd(&(ticcmd_t){.order = TC_PURCHASE, .product = product->ui_id,
                    .target = e->button.button == SDL_BUTTON_RIGHT});
                return true;
            }
        }
        return true;
    }
    int ids[6];
    int count = dc_commands(units, unit_count, ids);
    for (int i = 0; i < count && e->button.button == SDL_BUTTON_LEFT; ++i) {
        const SidebarCommand *control = &sidebar->controls[ids[i]];
        irect_t r = ui_rect(app, control->rect.x, control->rect.y, control->rect.w, control->rect.h);
        if (!irect_contains(r, mouse)) continue;
        if (ids[i] == 150 || ids[i] == 138)
            G_SelectedTiccmd(TC_STOP, units, unit_count, (fvec2_t){0}, 0);
        else if (ids[i] == 33 || ids[i] == 35) {
            sidebar->assault = ids[i] == 35;
            G_SelectedTiccmd(TC_MODE, units, unit_count, (fvec2_t){0}, ids[i]);
        } else if (ids[i] == 139 || ids[i] == 140 || ids[i] == 37)
            G_SelectedTiccmd(TC_DEPLOY, units, unit_count, (fvec2_t){0}, 0);
        else { sidebar->targeting = ids[i]; sidebar->waypoints = (dc_waypoints_t){0}; }
        break;
    }
    return true;
}

static void dc_ui_draw_minimap(app_t *app, const level_t *map, mobj_t *const *units, int unit_count,
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
            int light = P_SightBrightness(map, (ivec2_t){gx, gy});
            SDL_SetRenderDrawColor(app->renderer, r * light / 16, g * light / 16,
                                  b * light / 16, 255);
            SDL_RenderDrawPoint(app->renderer, clip.x + px, clip.y + py);
        }
    }
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!P_SightBrightness(map, vent->cell)) continue;
        int x = clip.x + vent->cell.x * clip.w / map->width;
        int y = clip.y + (int)(L_ScreenY(map, vent->cell.y) * clip.h / map->height);
        irect_t dot = { x - 1, y - 1, 3, 3 };
        dc_ui_fill(app->renderer, dot, vent->active ?
                   (SDL_Color){ 89, 226, 184, 255 } : (SDL_Color){ 68, 86, 84, 255 });
    }
    for (int i = 0; i < unit_count; ++i) {
        fvec2_t position = fixed3_xy_to_fvec2(units[i]->core.position);
        if (!P_VisibleToPlayer(units[i]) || units[i]->remove ||
            position.x < 0.0f || position.y < 0.0f) continue;
        int x = clip.x + (int)(position.x * (float)clip.w / (float)map->width);
        int y = clip.y + (int)(L_ScreenYF(map, position.y) *
                                (float)clip.h / (float)map->height);
        irect_t dot = { x - 1, y - 1, 2, 2 };
        dc_ui_fill(app->renderer, dot, units[i]->owner == consoleplayer ?
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
                              const spritecache_t *cache) {
    if (!app || !map || !font || !layout) return;
    char text[32];
    const spritesheet_t *buttons = R_CacheLookup(cache, "INTRFACE/MAINBUT.SPR");
    if (buttons && buttons->lumps && buttons->numlumps > 0)
        dc_ui_draw_sprite(app->renderer, buttons, 104, layout->money, 16 * 8 + 7);
    int resources = map->player_resources[consoleplayer][0];
    if (resources < 0) resources = 0;
    snprintf(text, sizeof(text), "%d", resources);
    dc_ui_draw_text_right(app->renderer, font, layout->money,
                          layout->money.y + 2, text,
                          (SDL_Color){ 41, 217, 230, 255 });

    const spritesheet_t *dial = R_CacheLookup(cache, "SPRITES/CLOC.SPR");
    if (dial && dial->numlumps >= 2 && map->daylight.duration > 0) {
        int half = dial->numlumps / 2;
        int frame = (int)((int64_t)map->daylight.tics * half / map->daylight.duration);
        if (frame >= half) frame = half - 1;
        frame += map->daylight.phase * half;
        irect_t src = dial->cells[frame].rect;
        /* 0x4377e3–0x437806 cancels the SPR displacement at (608,450). */
        irect_t dst = ui_rect(app, 608, 450, src.w, src.h);
        dst.y += app->win.h - 480;
        R_DrawSprite(app->renderer, dial, frame, -1, &src, &dst, SDL_FLIP_NONE,
                     (SDL_Color){255,255,255,255}, SDL_BLENDMODE_BLEND);
    }
    uint64_t clock = (uint64_t)leveltime * 1000 / (WORLD_CLOCK_MS * RTS_TICRATE);
    int days = map->daylight.duration > 0 ?
        (int)(clock / (uint64_t)map->daylight.duration / 2u) : 0;
    if (days > 999) days = 999;
    snprintf(text, sizeof(text), "%03d", days);
    int x = layout->days.x - HU_TextWidth(font, text, 1) / 2;
    HU_DrawTextRemapped(app->renderer, font, x, layout->days.y, text,
                        (SDL_Color){ 255, 255, 255, 255 }, 1, 0);
}

static void dc_SB_drawer(app_t *app, const level_t *map,
                         mobj_t *const *units, int unit_count,
                         const spritecache_t *cache, const bitmapfont_t *font,
                         const Sidebar *sidebar,
                         const spritesheet_t *background) {
    if (!app || !font || !font->sprite.lumps || font->sprite.numlumps <= 0) return;
    SDL_BlendMode old_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(app->renderer, &old_blend);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    UiLayout layout = ui_layout(app);
    if (background && background->lumps && background->numlumps > 0) {
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
    const ProductButton *products[16] = { 0 };
    int product_count = dc_available_products(units, unit_count, sidebar->tab, products);
    bool product_mode = sidebar->tab == 1 || !selected || dc_selected_unit_is_player_building(selected);
    int ids[6];
    int command_count = dc_commands(units, unit_count, ids);
    if (sidebar->tab == 2) {
        const int options[] = {62,63,64,151,196,202};
        memcpy(ids, options, sizeof(ids));
        command_count = 6;
        product_mode = false;
    }
    /* MAINE's title pictures 3/4/5 sit over the shared tab strip. */
    if (buttons && buttons->numlumps > 79) {
        irect_t title = ui_rect(app, 521,96,110,12);
        dc_ui_draw_sprite(app->renderer, buttons, 77 + sidebar->tab, title, 16 * 8 + 7);
    }
    int visible_button_count = product_mode ? product_count : command_count;
    for (int i = 0; i < visible_button_count; ++i) {
        irect_t button_rect = product_mode ? product_button_rect(app, sidebar, products[i]) :
            ui_rect(app, sidebar->controls[ids[i]].rect.x, sidebar->controls[ids[i]].rect.y,
                    sidebar->controls[ids[i]].rect.w, sidebar->controls[ids[i]].rect.h);
        if (irect_contains(button_rect, app->mouse)) {
            hover_button = i;
            break;
        }
    }
    if (!background || !background->lumps || background->numlumps <= 0) {
        dc_ui_fill(app->renderer, layout.build, (SDL_Color){ 160, 160, 160, 255 });
        dc_ui_stroke(app->renderer, layout.build, (SDL_Color){ 39, 39, 39, 255 });
        HU_DrawText(app->renderer, font,
                           layout.build.x + layout.build.w / 2 - HU_TextWidth(font, "BUILD", 5) / 2,
                           layout.build.y + layout.build.h / 2 - font->line_h / 2,
                           "BUILD", (SDL_Color){ 24, 24, 24, 255 }, 1);
    }
    if (hover_button >= 0) {
        if (product_mode && products[hover_button]) {
            snprintf(line, sizeof(line), "%s",
                     sidebar->controls[products[hover_button]->ui_id].label);
            HU_DrawText(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                           line,
                           map->player_resources[consoleplayer][0] >= products[hover_button]->cost ?
                           amber : (SDL_Color){ 208, 103, 88, 255 },
                           1);
        } else {
            HU_DrawText(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                           sidebar->controls[ids[hover_button]].label,
                           amber, 1);
        }
    } else if (product_mode) {
        if (selected && selected->production && selected->production->queue_count > 0 &&
            selected->production->time_ms > 0) {
            int done = selected->production->time_ms - selected->production->time_left_ms;
            int pct = done * 100 / selected->production->time_ms;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            snprintf(line, sizeof(line), "Training x%d %d%%",
                     selected->production->queue_count, pct);
        } else {
            snprintf(line, sizeof(line), "%s", dc_selected_building_label(selected));
        }
        if (line[0] != '\0') {
            HU_DrawText(app->renderer, font, layout.message.x + 4, layout.message.y + 2,
                           line, dim, 1);
        }
    }
    int button_slots = visible_button_count;
    for (int i = 0; i < button_slots; ++i) {
        irect_t button_rect = product_mode ? product_button_rect(app, sidebar, products[i]) :
            ui_rect(app, sidebar->controls[ids[i]].rect.x, sidebar->controls[ids[i]].rect.y,
                    sidebar->controls[ids[i]].rect.w, sidebar->controls[ids[i]].rect.h);
        int frame = product_mode && products[i] ? products[i]->icon_frame :
            sidebar->controls[ids[i]].frame;
        if (buttons && buttons->lumps && buttons->numlumps > 0) {
            bool checked = !product_mode && ((ids[i] == 33 && !sidebar->assault) ||
                                             (ids[i] == 35 && sidebar->assault));
            const SidebarCommand *control = &sidebar->controls[product_mode ? products[i]->ui_id : ids[i]];
            int intensity = checked && control->pushed < 0 ? -control->pushed : 16;
            if (checked) intensity += sidebar->bright_pushed;
            else if (i == hover_button) intensity += sidebar->bright_highlight;
            if (intensity > 31) intensity = 31;
            dc_ui_draw_sprite(app->renderer, buttons, frame, button_rect, intensity * 8 + 7);
            if (product_mode && products[i] && map->player_resources[consoleplayer][0] < products[i]->cost) {
                dc_ui_fill(app->renderer, button_rect, (SDL_Color){ 0, 0, 0, 105 });
            }
            if (product_mode && products[i]) {
                int quantity = map->purchases[consoleplayer][products[i]->row_id].selected;
                if (quantity) {
                    snprintf(line, sizeof(line), "%d", quantity);
                    ivec2_t origin = ivec2_add((ivec2_t){button_rect.x,button_rect.y},
                                               sidebar->controls[products[i]->ui_id].counter);
                    HU_DrawText(app->renderer, font, origin.x, origin.y,
                                line, amber, 1);
                }
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
    if (!app || !hud || !font || !font->sprite.lumps ||
        font->sprite.numlumps <= 0 || hud->count <= 0) return;
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

bool DC_SB_Responder(void *sb_ptr, app_t *app, level_t *map,
                  mobj_t *const *units, int unit_count, const SDL_Event *event) {
    sb_state_t *sb = sb_ptr;
    return sb && sb->active &&
           dc_SB_responder(&sb->sidebar, app, map, units, unit_count, event);
}

void DC_SB_Drawer(void *sb_ptr, app_t *app, const level_t *map,
               mobj_t *const *units, int unit_count,
               const spritecache_t *sprites, const hudtext_t *hud) {
    sb_state_t *sb = sb_ptr;
    if (!sb || !sb->active || !sb->font_ready) return;
    const spritecache_t *images = sprites ? sprites->ui : NULL;
    dc_SB_drawer(app, map, units, unit_count, images, &sb->font,
                 &sb->sidebar, &sb->background);
    UiLayout layout = ui_layout(app);
    dc_ui_draw_status(app, map, &sb->font, &layout, images);
    render_hud_messages(app, hud, &sb->font);
}

void DC_SB_Shutdown(void *sb_ptr) {
    sb_state_t *sb = sb_ptr;
    if (!sb) return;
    R_FreeSprite(&sb->background);
    HU_FreeFont(&sb->font);
    free(sb);
}
