#include "d_net.h"
#include "sb_bar.h"
#include "game.h"

static irect_t scaled(const app_t *app, irect_t r) {
    return (irect_t){r.x * app->win.w / gameui->logical_width,
        r.y * app->win.h / gameui->logical_height,
        r.w * app->win.w / gameui->logical_width,
        r.h * app->win.h / gameui->logical_height};
}

static mobj_t *selection(void) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *u = (mobj_t *)th;
        if (u->owner == consoleplayer && u->hp > 0 && !u->remove && P_MobjIsSelected(u)) return u;
    }
    return NULL;
}

static bool makes(const mobj_t *u, const StaticProductDefinition *p) {
    if (!u || !p) return false;
    for (int j = 0; j < p->maker_count; ++j)
        if (p->makers[j] == u->type_id) return true;
    return false;
}

static mobj_t *producer_for(const StaticProductDefinition *p) {
    mobj_t *u = selection();
    return makes(u, p) ? u : G_FindProducer(consoleplayer, p);
}

static bool enabled(const StaticProductDefinition *p, mobj_t *u) {
    if (!p || !u || !G_ModelProductAvailable(NULL, consoleplayer, p) ||
        level.player_resources[consoleplayer][0] < p->cost) return false;
    if (gameinfo->states[u->core.state_id].group == 6 || !G_ModelProducerHasTech(u, p)) return false;
    const production_t *q = u->production;
    return !q || (q->product_type == p->product_type && q->product_class == p->product_class &&
                  q->queue_count < RTS_MAX_PRODUCTION_QUEUE);
}

static void update_selection(sb_state_t *st) {
    mobj_t *u = selection();
    uint32_t id = u ? u->id : 0;
    if (st->production_selection == id) return;
    st->production_selection = id;
    st->production_page = 0;
    if (!u) return;
    for (int i = 0; i < gameui->product_count; ++i) {
        const uiproduct_t *item = &gameui->products[i];
        if (makes(u, G_ModelProductByUIId(NULL, item->id))) {
            st->production_category = item->category;
            return;
        }
    }
}

static int product_list(sb_state_t *st, int *items) {
    update_selection(st);
    StaticProductDefinition products[64];
    int total = G_ModelGetProducts(NULL, consoleplayer, products, 64);
    int count = 0;
    for (int i = 0; i < gameui->product_count; ++i) {
        if (gameui->products[i].category != st->production_category) continue;
        for (int j = 0; j < total; ++j)
            if (products[j].ui_id == gameui->products[i].id) { items[count++] = i; break; }
    }
    return count;
}

static void draw_image(sb_state_t *st, const app_t *app, int image, irect_t src, irect_t dst) {
    dst = scaled(app, dst);
    SDL_RenderCopy(app->renderer, st->textures[image], &src, &dst);
}

static void tooltip(const app_t *app, ivec2_t mouse, const char *title,
                     const StaticProductDefinition *p, mobj_t *producer) {
    char text[160];
    if (p) {
        const char *status = !G_ModelProductAvailable(NULL, consoleplayer, p) ? "REQUIRES TECH OR PRODUCER" :
            level.player_resources[consoleplayer][0] < p->cost ? "INSUFFICIENT FUNDS" :
            !enabled(p, producer) ? "PRODUCER BUSY" : "CLICK TO BUILD";
        snprintf(text, sizeof(text), "%d  %s", p->cost, status);
    }
    irect_t box = {mouse.x - 260, mouse.y, 250, p ? 40 : 24};
    if (box.x < 0) box.x = 0;
    if (box.y + box.h > gameui->logical_height) box.y = gameui->logical_height - box.h;
    irect_t rect = scaled(app, box);
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(app->renderer, &rect);
    SDL_SetRenderDrawColor(app->renderer, 220, 220, 205, 255);
    SDL_RenderDrawRect(app->renderer, &rect);
    SB_DrawText(app, (ivec2_t){box.x + 6, box.y + 7}, title, box.w - 12);
    if (p) SB_DrawText(app, (ivec2_t){box.x + 6, box.y + 24}, text, box.w - 12);
}

static irect_t menu_rect(void) {
    return (irect_t){gameui->logical_width/2-120, gameui->logical_height/2-50, 240, 100};
}

static bool selected_order(ticorder_t order, fvec2_t goal, uint32_t target) {
    mobj_t *units[MAXCOMMANDUNITS];
    int count = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *u = (mobj_t *)th;
        if (u->owner == consoleplayer && u->hp > 0 && !u->remove && P_MobjIsSelected(u)) {
            if (count == MAXCOMMANDUNITS) return false;
            units[count++] = u;
        }
    }
    return G_SelectedTiccmd(order, units, count, goal, target);
}

bool SB_PaletteResponder(sb_state_t *st, app_t *app, const SDL_Event *event) {
    update_selection(st);
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_RIGHT && st->order) {
        st->order = UI_UNAVAILABLE;
        return true;
    }
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE &&
        (st->options_visible || st->order)) {
        st->options_visible = false;
        st->order = UI_UNAVAILABLE;
        return true;
    }
    ivec2_t mouse;
    if (event->type != SDL_MOUSEBUTTONDOWN && event->type != SDL_MOUSEBUTTONUP &&
        event->type != SDL_MOUSEWHEEL) return false;
    if (event->type == SDL_MOUSEWHEEL) SDL_GetMouseState(&mouse.x, &mouse.y);
    else mouse = (ivec2_t){event->button.x, event->button.y};
    R_WindowToRenderPt(app, mouse.x, mouse.y, &mouse.x, &mouse.y);
    mouse = (ivec2_t){mouse.x * gameui->logical_width / app->win.w,
                      mouse.y * gameui->logical_height / app->win.h};
    bool click = event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT;
    if (st->options_visible) {
        irect_t menu = menu_rect();
        if (click && irect_contains(menu, mouse)) {
            if (mouse.y >= menu.y+60) {
                SDL_Event quit = {.type = SDL_QUIT};
                SDL_PushEvent(&quit);
            } else st->options_visible = false;
        }
        return true;
    }
    for (int i = 0; i < gameui->action_count; ++i) {
        const uiaction_t *a = &gameui->actions[i];
        if (!irect_contains(a->rect, mouse)) continue;
        if (click) {
            if (a->action == UI_RADAR && G_ModelRadarLevel(consoleplayer)) st->radar_visible = !st->radar_visible;
            else if (a->action == UI_OPTIONS) st->options_visible = true;
            else if (a->action == UI_STOP) selected_order(TC_STOP, (fvec2_t){0,0}, 0);
            else if (a->action == UI_MOVE || a->action == UI_ATTACK) st->order = a->action;
            else if (a->action == UI_PRODUCT) {
                if (!G_ModelProductAvailable(NULL,consoleplayer,G_ModelProductByUIId(NULL,a->product))) return true;
                st->order = a->action;
                st->order_product = a->product;
            }
        }
        return true;
    }
    irect_t radar = SB_MinimapRect(&level);
    if (st->radar_visible && G_ModelRadarLevel(consoleplayer) && irect_contains(radar, mouse)) {
        if (click) {
            fvec2_t position = {(float)(mouse.x-radar.x)*level.width/radar.w,
                                (float)(mouse.y-radar.y)*level.height/radar.h};
            app->cam = fvec2_sub((fvec2_t){G_WorldViewportWidth(app)/2.0f,app->win.h/2.0f},
                (fvec2_t){position.x*app->cell.w,position.y*app->cell.h});
            R_ClampCamera(app, &level, G_WorldViewportWidth(app), app->win.h);
        }
        return true;
    }
    for (int i = 0; i < gameui->category_count; ++i) {
        if (!irect_contains(gameui->categories[i].rect, mouse)) continue;
        if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
            st->production_category = i;
            st->production_page = 0;
        }
        return true;
    }
    int items[64];
    int count = product_list(st, items);
    irect_t grid = gameui->command_grid;
    if (st->production_category >= 0 && irect_contains(grid, mouse)) {
        int capacity = gameui->command_columns * gameui->command_rows;
        int pages = count > 0 ? (count + capacity - 1) / capacity : 1;
        if (event->type == SDL_MOUSEWHEEL) {
            st->production_page = (st->production_page + (event->wheel.y < 0 ? 1 : pages - 1)) % pages;
        } else if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
            int col = (mouse.x - grid.x) / gameui->icon_size.w;
            int row = (mouse.y - grid.y) / gameui->icon_size.h;
            int index = st->production_page * capacity + row * gameui->command_columns + col;
            if (index < count) {
                const StaticProductDefinition *p = G_ModelProductByUIId(NULL, gameui->products[items[index]].id);
                mobj_t *producer = producer_for(p);
                if (enabled(p, producer)) G_BuildOrder(producer, p->ui_id);
            }
        }
        return true;
    }
    if (irect_contains(gameui->sidebar_panel.rect, mouse)) return true;
    for (int i = 0; i < gameui->image_count; ++i)
        if (irect_contains(gameui->images[i].destination, mouse)) return true;
    if (st->order && event->type == SDL_MOUSEBUTTONDOWN) {
        if (click) {
            ivec2_t render = {mouse.x*app->win.w/gameui->logical_width,
                              mouse.y*app->win.h/gameui->logical_height};
            cell_t cell = R_ScreenToMapGrid(app, &level, render.x, render.y);
            fvec2_t goal = fvec2_cell_center(cell);
            if (st->order == UI_MOVE) selected_order(TC_MOVE, goal, 0);
            else {
                mobjlist_t units = P_ListMobjs();
                int picked = R_PickUnit(app, &level, units.items, units.count, NULL,
                    st->sprites, gameinfo, render.x, render.y, st->order == UI_PRODUCT ? consoleplayer : -1);
                if (picked >= 0) {
                    mobj_t *u = units.items[picked];
                    if (st->order == UI_PRODUCT) G_BuildOrder(u, st->order_product);
                    else if (u->owner != consoleplayer) selected_order(TC_ATTACK, goal, u->id);
                }
                P_FreeMobjList(&units);

            }
        }
        st->order = UI_UNAVAILABLE;
        return true;
    }
    return false;
}

void SB_PaletteDrawer(sb_state_t *st, const app_t *app) {
    int items[64];
    int count = product_list(st, items);
    ivec2_t mouse;
    SDL_GetMouseState(&mouse.x, &mouse.y);
    R_WindowToRenderPt(app, mouse.x, mouse.y, &mouse.x, &mouse.y);
    mouse = (ivec2_t){mouse.x * gameui->logical_width / app->win.w,
                      mouse.y * gameui->logical_height / app->win.h};
    int hovered_category = -1, hovered_product = -1;
    int hovered_action = -1;
    for (int i = 0; i < gameui->action_count; ++i) {
        const uiaction_t *a = &gameui->actions[i];
        irect_t dst = {a->rect.x+(a->rect.w-a->source.w)/2,
            a->rect.y+(a->rect.h-a->source.h)/2,a->source.w,a->source.h};
        draw_image(st, app, a->image, a->source, dst);
        if (st->order && st->order == a->action) {
            irect_t r = scaled(app,a->rect);
            SDL_SetRenderDrawColor(app->renderer,70,230,235,255);
            SDL_RenderDrawRect(app->renderer,&r);
        }
        if (a->action == UI_UNAVAILABLE || (a->action == UI_RADAR && !G_ModelRadarLevel(consoleplayer)) ||
            (a->action == UI_PRODUCT && !G_ModelProductAvailable(NULL,consoleplayer,G_ModelProductByUIId(NULL,a->product)))) {
            irect_t r = scaled(app, a->rect);
            SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(app->renderer, 0,0,0,145);
            SDL_RenderFillRect(app->renderer, &r);
        }
        if (irect_contains(a->rect, mouse)) hovered_action = i;
    }
    for (int i = 0; i < gameui->category_count; ++i) {
        const uicategory_t *c = &gameui->categories[i];
        irect_t dst = c->rect;
        draw_image(st, app, c->image, c->source, dst);
        if (i == st->production_category) {
            irect_t r = scaled(app, c->rect);
            SDL_SetRenderDrawColor(app->renderer, 70,230,235,255);
            SDL_RenderDrawRect(app->renderer, &r);
        }
        if (irect_contains(c->rect, mouse)) hovered_category = i;
    }
    int capacity = gameui->command_columns * gameui->command_rows;
    int pages = count > 0 ? (count + capacity - 1) / capacity : 1;
    if (st->production_page >= pages) st->production_page = 0;
    for (int slot = 0; slot < capacity; ++slot) {
        int index = st->production_page * capacity + slot;
        if (index >= count) break;
        int item = items[index];
        const StaticProductDefinition *p = G_ModelProductByUIId(NULL, gameui->products[item].id);
        if (!p) continue;
        mobj_t *producer = producer_for(p);
        irect_t cell = {gameui->command_grid.x + slot % gameui->command_columns * gameui->icon_size.w,
            gameui->command_grid.y + slot / gameui->command_columns * gameui->icon_size.h,
            gameui->icon_size.w, gameui->icon_size.h};
        {
            irect_t background = scaled(app,cell);
            SDL_SetRenderDrawColor(app->renderer,0,0,0,255);
            SDL_RenderFillRect(app->renderer,&background);
        }
        const spritesheet_t *sprite = &st->product_icons[item];
        irect_t src = sprite->cells[0].rect;
        float scale = fminf((float)cell.w / src.w, (float)cell.h / src.h);
        irect_t dst = {cell.x + (cell.w - (int)(src.w*scale))/2,
            cell.y + (cell.h - (int)(src.h*scale))/2, (int)(src.w*scale), (int)(src.h*scale)};
        dst = scaled(app, dst);
        R_DrawSprite(app->renderer, sprite, 0, -1, &src, &dst, SDL_FLIP_NONE,
                     (SDL_Color){255,255,255,255}, SDL_BLENDMODE_BLEND);
        irect_t rect = scaled(app, cell);
        if (!enabled(p, producer)) {
            SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(app->renderer, 0,0,0,160);
            SDL_RenderFillRect(app->renderer, &rect);
        }
        const production_t *q = producer ? producer->production : NULL;
        if (q && q->product_type == p->product_type && q->product_class == p->product_class) {
            char amount[16];
            snprintf(amount, sizeof(amount), "%d", q->queue_count);
            SDL_SetRenderDrawColor(app->renderer, 255,255,255,255);
            SB_DrawText(app, (ivec2_t){cell.x+4,cell.y+4}, amount, cell.w-8);
            if (q->time_ms > 0) {
                irect_t bar = scaled(app, (irect_t){cell.x,cell.y+cell.h-3,
                    cell.w*(q->time_ms-q->time_left_ms)/q->time_ms,3});
                SDL_SetRenderDrawColor(app->renderer, 40,220,70,255);
                SDL_RenderFillRect(app->renderer, &bar);
            }
        }
        if (irect_contains(cell, mouse)) hovered_product = item;
    }
    if (pages > 1) {
        char page[48];
        snprintf(page, sizeof(page), "SCROLL %d OF %d", st->production_page+1, pages);
        SDL_SetRenderDrawColor(app->renderer, 230,230,230,255);
        SB_DrawText(app, (ivec2_t){gameui->command_grid.x,gameui->command_grid.y-12}, page, gameui->command_grid.w);
    }
    if (hovered_product >= 0) {
        const StaticProductDefinition *p = G_ModelProductByUIId(NULL, gameui->products[hovered_product].id);
        tooltip(app, mouse, p->label, p, producer_for(p));
    } else if (hovered_category >= 0) tooltip(app, mouse, gameui->categories[hovered_category].label, NULL, NULL);
    else if (hovered_action >= 0) tooltip(app, mouse, gameui->actions[hovered_action].label, NULL, NULL);
    if (st->options_visible) {
        irect_t menu = menu_rect(), rect = scaled(app, menu);
        SDL_SetRenderDrawColor(app->renderer, 12,18,22,255);
        SDL_RenderFillRect(app->renderer, &rect);
        SDL_SetRenderDrawColor(app->renderer, 230,230,230,255);
        SDL_RenderDrawRect(app->renderer, &rect);
        SB_DrawText(app, (ivec2_t){menu.x+20,menu.y+25}, "RESUME GAME", menu.w-40);
        SB_DrawText(app, (ivec2_t){menu.x+20,menu.y+70}, "QUIT GAME", menu.w-40);
    }
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_NONE);
}
