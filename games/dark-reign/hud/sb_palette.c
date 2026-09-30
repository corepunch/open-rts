#include "d_net.h"
#include "dr_hud.h"
#include "game.h"
#include "dr_types.h"
#include "info.h"

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
}

static int product_list(sb_state_t *st, int *items) {
    update_selection(st);
    const dr_mission_t *mission = level.mission;
    mobj_t *u = selection();
    bool buildings = u && (u->type_id == MT_FG_CONSTRUCTION_CREW ||
                           u->type_id == MT_IMP_CONSTRUCTION_CREW);
    int count = 0;
    int total = mission ? mission->product_count : gameui->product_count;
    for (int j = 0; j < total; ++j) {
        int id = mission ? mission->products[j].type : gameui->products[j].id;
        const StaticProductDefinition *product = G_ModelProductByUIId(NULL, id);
        if (!product || !DR_ProductInTech(product->ui_id) ||
            (product->product_class == RTS_PRODUCT_BUILDING) != buildings) continue;
        for (int i = 0; i < gameui->product_count; ++i)
            if (gameui->products[i].id == id) { items[count++] = i; break; }
    }
    return count;
}

static void draw_image(sb_state_t *st, const app_t *app, int image, irect_t src, irect_t dst) {
    dst = scaled(app, dst);
    /* BUISOBOX is color-keyed at index 0. The other chrome sheets are opaque. */
    uint32_t flags = image == 8 ? 0 : V_OPAQUE;
    R_DrawSprite(&st->images[image], 0, -1, &src, &dst, flags, 16);
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
    V_FillRect(rect, V_NearestIndex(0xff000000u));
    V_DrawRectOutline(rect, V_NearestIndex(0xffdcdccdu));
    DR_DrawText(app, (ivec2_t){box.x + 6, box.y + 7}, title, box.w - 12);
    if (p) DR_DrawText(app, (ivec2_t){box.x + 6, box.y + 24}, text, box.w - 12);
}

static irect_t menu_rect(void) {
    return (irect_t){gameui->logical_width/2-120, gameui->logical_height/2-50, 240, 100};
}

static bool path_action_visible(const sb_state_t *st, const uiaction_t *action) {
    return st->path_advanced || (action->action != UI_PATH_SAVE &&
        action->action != UI_PATH_DESELECT && action->action != UI_PATH_MODE);
}

bool DR_PaletteResponder(sb_state_t *st, app_t *app, const SDL_Event *event) {
    update_selection(st);
    if (event->type == SDL_KEYDOWN && !event->key.repeat &&
        !(event->key.keysym.mod & (KMOD_CTRL | KMOD_ALT))) {
        switch (event->key.keysym.sym) {
        case SDLK_b: st->page = DR_PAGE_BUILD; st->order = UI_UNAVAILABLE; return true;
        case SDLK_p: st->page = DR_PAGE_PATHS; st->order = UI_UNAVAILABLE; return true;
        case SDLK_m: st->order = UI_MOVE; return true;
        case SDLK_a: st->order = UI_ATTACK; return true;
        case SDLK_s: SB_SelectedOrder(TC_STOP, (fvec2_t){0}, 0); return true;
        default: break;
        }
    }
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
            else if (a->action == UI_PRODUCT) {
                if (!G_ModelProductAvailable(NULL,consoleplayer,G_ModelProductByUIId(NULL,a->product))) return true;
                st->order = a->action;
                st->order_product = a->product;
            } else SB_ActivateAction(st, a);
        }
        return true;
    }
    irect_t radar = DR_MinimapRect(&level);
    if (st->radar_visible && G_ModelRadarLevel(consoleplayer) && irect_contains(radar, mouse)) {
        if (click) {
            fvec2_t position = {(float)(mouse.x-radar.x), (float)(mouse.y-radar.y)};
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
            st->page = DR_PAGE_BUILD;
            st->order = UI_UNAVAILABLE;
        }
        return true;
    }
    if (st->page == DR_PAGE_PATHS) {
        for (int i = 0; i < gameui->path_action_count; ++i) {
            const uiaction_t *action = &gameui->path_actions[i];
            if (!path_action_visible(st,action)) continue;
            if (!irect_contains(action->rect, mouse)) continue;
            if (click) SB_ActivateAction(st, action);
            return true;
        }
        if (st->path_advanced && SB_PathListResponder(st,event,mouse)) return true;
        if (irect_contains((irect_t){448,64,192,278}, mouse)) return true;
    }
    int items[gameui->product_count];
    int count = product_list(st, items);
    if (st->page == DR_PAGE_BUILD && irect_contains((irect_t){448,316,44,22}, mouse)) {
        int capacity = gameui->command_columns * gameui->command_rows;
        int pages = count ? (count + capacity - 1) / capacity : 1;
        if (click) st->production_page = (st->production_page +
            (mouse.x < 470 ? pages - 1 : 1)) % pages;
        return true;
    }
    irect_t grid = gameui->command_grid;
    if (st->page == DR_PAGE_BUILD && st->production_category >= 0 && irect_contains(grid, mouse)) {
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
    if (SB_PathResponder(st, app, event)) return true;
    if (st->order && event->type == SDL_MOUSEBUTTONDOWN) {
        if (click) {
            ivec2_t render = {mouse.x*app->win.w/gameui->logical_width,
                              mouse.y*app->win.h/gameui->logical_height};
            cell_t cell = R_ScreenToMapGrid(app, &level, render.x, render.y);
            fvec2_t goal = fvec2_cell_center(cell);
            if (st->order == UI_MOVE) SB_SelectedOrder(TC_MOVE, goal, 0);
            else {
                mobjlist_t units = P_ListMobjs();
                int picked = R_PickUnit(app, &level, units.items, units.count, NULL,
                    st->sprites, gameinfo, render.x, render.y, st->order == UI_PRODUCT ? consoleplayer : -1);
                if (picked >= 0) {
                    mobj_t *u = units.items[picked];
                    if (st->order == UI_PRODUCT) G_BuildOrder(u, st->order_product);
                    else SB_SelectedOrder(TC_ATTACK, goal, u->id);
                }
                P_FreeMobjList(&units);

            }
        }
        st->order = UI_UNAVAILABLE;
        return true;
    }
    return false;
}

static void draw_paths(sb_state_t *st, const app_t *app, ivec2_t mouse) {
    draw_image(st, app, 4, (irect_t){0,0,192,278}, (irect_t){448,64,192,278});
    draw_image(st, app, 13, (irect_t){st->path_advanced ? 192 : 0,0,192,32}, (irect_t){448,64,192,32});
    DR_DrawHeader(app,(ivec2_t){st->path_advanced ? 462 : 461,st->path_advanced ? 78 : 75},"Basic",71);
    DR_DrawHeader(app,(ivec2_t){st->path_advanced ? 561 : 560,st->path_advanced ? 78 : 77},"Advanced",78);
    for (int i = 0; i < gameui->path_action_count; ++i) {
        const uiaction_t *a = &gameui->path_actions[i];
        if (!path_action_visible(st,a) || a->action == UI_PATH_ADVANCED) continue;
        bool active = a->action == UI_WAYPOINT ? st->order == UI_WAYPOINT :
                      a->action == UI_PATH_MODE && (int)st->path.mode == a->product;
        bool hover = irect_contains(a->rect, mouse);
        irect_t src = a->source;
        src.x += (active ? 2 : hover ? 1 : 0) * (a->image == 14 ? 71 : src.w);
        draw_image(st, app, a->image, src, a->rect);
        if (a->image != 14) DR_DrawText(app, ivec2_add((ivec2_t){a->rect.x,a->rect.y},
            (ivec2_t){a->rect.w == 71 ? 7 : 9,5}), a->label, a->rect.w - 14);
    }
    if (!st->path_advanced) return;
    DR_DrawCaption(app,(ivec2_t){544,205},"Path Direction",true);
    DR_DrawCaption(app,(ivec2_t){468,258},"Current Path",false);
    DR_DrawCaption(app,(ivec2_t){555,257},"Saved Paths",false);
    for (int i = 0; i < gameui->path_list.h / gameui->path_row_height &&
         i + st->path_scroll < st->saved_path_count; ++i) {
        char text[24];
        snprintf(text, sizeof(text), "Trail %d", i + st->path_scroll + 1);
        DR_DrawText(app, (ivec2_t){gameui->path_list.x + 2,
            gameui->path_list.y + i*gameui->path_row_height}, text, gameui->path_list.w - 2);
    }
    char current[24];
    if (st->saved_path_selection >= 0)
        snprintf(current, sizeof(current), "Trail %d", st->saved_path_selection + 1);
    else snprintf(current, sizeof(current), "None Selected");
    DR_DrawText(app, (ivec2_t){470,260}, current, 76);
}

void DR_PaletteDrawer(sb_state_t *st, const app_t *app) {
    int items[gameui->product_count];
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
        irect_t src = a->source;
        bool active = a->action == UI_PAGE && st->page == a->product;
        if (active) src.x += 384;
        if (irect_contains(a->rect, mouse)) {
            hovered_action = i;
            if (!active) src.x += 192;
        }
        draw_image(st, app, a->image, src, a->rect);
    }
    for (int i = 0; i < gameui->category_count; ++i) {
        const uicategory_t *c = &gameui->categories[i];
        irect_t src = c->source;
        src.x += st->page == DR_PAGE_BUILD ? 384 : irect_contains(c->rect, mouse) ? 192 : 0;
        draw_image(st, app, c->image, src, c->rect);
        if (irect_contains(c->rect, mouse)) hovered_category = i;
    }
    if (st->page == DR_PAGE_PATHS) {
        draw_paths(st, app, mouse);
        goto overlays;
    }
    int capacity = gameui->command_columns * gameui->command_rows;
    int pages = count > 0 ? (count + capacity - 1) / capacity : 1;
    if (st->production_page >= pages) st->production_page = 0;
    irect_t grid = scaled(app, gameui->command_grid);
    V_FillRect(grid, V_NearestIndex(0xff000000u));
    for (int slot = 0; slot < capacity; ++slot) {
        int index = st->production_page * capacity + slot;
        irect_t cell = {448 + slot % 3 * 64, 64 + slot / 3 * 50, 64, 50};
        int item = index < count ? items[index] : -1;
        const StaticProductDefinition *p = item >= 0 ?
            G_ModelProductByUIId(NULL, gameui->products[item].id) : NULL;
        if (p) {
            mobj_t *producer = producer_for(p);
            const spritesheet_t *sprite = &st->product_icons[item];
            irect_t src = sprite->cells[0].rect;
            irect_t dst = scaled(app, (irect_t){cell.x+9,cell.y+2,src.w,src.h});
            R_DrawSprite(sprite, 0, enabled(p, producer) ? -1 : 1, &src, &dst, 0, 16);
            const production_t *q = producer ? producer->production : NULL;
            if (q && q->product_type == p->product_type && q->product_class == p->product_class) {
                char amount[16];
                snprintf(amount, sizeof(amount), "%d", q->queue_count);
                DR_DrawText(app, (ivec2_t){cell.x+4,cell.y+4}, amount, cell.w-8);
            }
        }
        bool hover = p && irect_contains(cell, mouse);
        draw_image(st, app, 8, (irect_t){hover ? 64 : 0,0,64,50}, cell);
        if (hover) hovered_product = item;
    }
    draw_image(st, app, 9, (irect_t){0,0,22,22}, (irect_t){448,316,22,22});
    draw_image(st, app, 10, (irect_t){0,0,22,22}, (irect_t){470,316,22,22});
    draw_image(st, app, 12, (irect_t){213,0,71,22}, (irect_t){496,316,71,22});
    draw_image(st, app, 12, (irect_t){0,0,71,22}, (irect_t){568,316,71,22});
    DR_DrawText(app, (ivec2_t){507,323}, "Upgrade", 60);
    DR_DrawText(app, (ivec2_t){586,323}, "Decoy", 48);
overlays:
    if (hovered_product >= 0) {
        const StaticProductDefinition *p = G_ModelProductByUIId(NULL, gameui->products[hovered_product].id);
        tooltip(app, mouse, p->label, p, producer_for(p));
    } else if (hovered_category >= 0) tooltip(app, mouse, gameui->categories[hovered_category].label, NULL, NULL);
    else if (hovered_action >= 0) tooltip(app, mouse, gameui->actions[hovered_action].label, NULL, NULL);
    if (st->options_visible) {
        irect_t menu = menu_rect(), rect = scaled(app, menu);
        V_FillRect(rect, V_NearestIndex(0xff0c1216u));
        V_DrawRectOutline(rect, V_NearestIndex(0xffe6e6e6u));
        DR_DrawText(app, (ivec2_t){menu.x+20,menu.y+25}, "RESUME GAME", menu.w-40);
        DR_DrawText(app, (ivec2_t){menu.x+20,menu.y+70}, "QUIT GAME", menu.w-40);
    }
}
