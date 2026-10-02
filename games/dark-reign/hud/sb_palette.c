#include "engine.h"
#include "dark-reign.h"
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

static bool path_action_visible(const sb_state_t *st, const uiaction_t *action) {
    return st->path_advanced || (action->action != UI_PATH_SAVE &&
        action->action != UI_PATH_DESELECT && action->action != UI_PATH_MODE);
}

/* One table owns the palette's controls. Native fonts and product counters
 * extend engine picture drawing; world orders stay in the game's responder. */
static menu_t palette;
static app_t *palette_app;
static struct {
    int categories, paths, list, grid, icons, slots, previous, next;
    int upgrade, decoy, radar, chrome, keys, count;
} indices;

static menuitem_t *item(int index) { return &palette.items[index]; }

static void set_picture(menuitem_t *widget, const app_t *app, const spritesheet_t *sheet,
                         irect_t source, irect_t rect, int stride, bool opaque) {
    widget->rect = scaled(app, rect);
    widget->sheet = sheet;
    widget->stretch = true;
    widget->opaque = opaque;
    for (int state = 0; state < MS_STATES; ++state) {
        widget->look[state] = (menulook_t){.part = source, .palette = -1};
        widget->look[state].part.x += state * stride;
    }
}

static void action(menu_t *menu, menuitem_t *widget, menuaction_t event) {
    sb_state_t *st = menu->owner;
    const uiaction_t *a = widget->userdata;
    if (event != MA_ACTIVATE) return;
    if (a->action == UI_RADAR && G_ModelRadarLevel(consoleplayer)) st->radar_visible = !st->radar_visible;
    else if (a->action == UI_PRODUCT) {
        if (!G_ModelProductAvailable(NULL, consoleplayer, G_ModelProductByUIId(NULL, a->product))) return;
        st->order = a->action;
        st->order_product = a->product;
    } else SB_ActivateAction(st, a);
}

static void category(menu_t *menu, menuitem_t *widget, menuaction_t event) {
    sb_state_t *st = menu->owner;
    if (event != MA_ACTIVATE) return;
    st->production_category = (int)(widget - menu->items) - indices.categories;
    st->production_page = 0;
    st->page = DR_PAGE_BUILD;
    st->order = UI_UNAVAILABLE;
}

static int page_count(sb_state_t *st) {
    int products[gameui->product_count];
    int count = product_list(st, products);
    int capacity = gameui->command_columns * gameui->command_rows;
    return count ? (count + capacity - 1) / capacity : 1;
}

static void turn_page(menu_t *menu, menuitem_t *widget, menuaction_t event) {
    sb_state_t *st = menu->owner;
    if (event != MA_ACTIVATE && event != MA_WHEEL) return;
    int pages = page_count(st);
    int step = event == MA_WHEEL ? (menu->wheel < 0 ? 1 : pages - 1) :
        widget == item(indices.previous) ? pages - 1 : 1;
    st->production_page = (st->production_page + step) % pages;
}

static void product(menu_t *menu, menuitem_t *widget, menuaction_t event) {
    if (event == MA_WHEEL) { turn_page(menu, widget, event); return; }
    if (event != MA_ACTIVATE) return;
    const StaticProductDefinition *p = widget->userdata;
    if (!p) return;
    mobj_t *producer = producer_for(p);
    if (enabled(p, producer)) G_BuildOrder(producer, p->ui_id);
}

static void select_path(menu_t *menu, menuitem_t *widget, menuaction_t event) {
    sb_state_t *st = menu->owner;
    if (event != MA_CHANGE) return;
    st->saved_path_selection = widget->value;
    st->path = st->saved_paths[widget->value];
    st->path.current = 0;
    st->order = UI_UNAVAILABLE;
}

static const char *path_row(const menuitem_t *widget, int row) {
    (void)widget;
    return M_va("Trail %d", row + 1);
}

static void camera(menu_t *menu, menuitem_t *widget, menuaction_t event) {
    (void)menu;
    if (event != MA_ACTIVATE) return;
    app_t *app = palette_app;
    fvec2_t position = fvec2_sub((fvec2_t){palette.cursor.x, palette.cursor.y},
                                (fvec2_t){widget->rect.x, widget->rect.y});
    position = (fvec2_t){position.x * gameui->logical_width / app->win.w,
                         position.y * gameui->logical_height / app->win.h};
    app->cam = fvec2_sub((fvec2_t){G_WorldViewportWidth(app)/2.0f, app->win.h/2.0f},
                         (fvec2_t){position.x * app->cell.w, position.y * app->cell.h});
    R_ClampCamera(app, &level, G_WorldViewportWidth(app), app->win.h);
}

static void draw_label(const menu_t *menu, const menuitem_t *widget) {
    (void)menu;
    ivec2_t at = {widget->rect.x * gameui->logical_width / palette_app->win.w,
                  widget->rect.y * gameui->logical_height / palette_app->win.h};
    at = ivec2_add(at, widget->inset);
    DR_DrawText(palette_app, at, widget->text,
                widget->rect.w * gameui->logical_width / palette_app->win.w - widget->inset.x);
}

static void draw_quantity(const menu_t *menu, const menuitem_t *widget) {
    (void)menu;
    const StaticProductDefinition *p = widget->userdata;
    if (!p) return;
    mobj_t *producer = producer_for(p);
    const production_t *q = producer ? producer->production : NULL;
    if (!q || q->product_type != p->product_type || q->product_class != p->product_class) return;
    int slot = (int)(widget - menu->items) - indices.icons;
    irect_t rect = item(indices.slots + slot)->rect;
    ivec2_t at = {rect.x * gameui->logical_width / palette_app->win.w + 4,
                  rect.y * gameui->logical_height / palette_app->win.h + 4};
    DR_DrawText(palette_app, at, M_va("%d", q->queue_count), gameui->icon_size.w - 8);
}

/* Labels use the native variable-width PCX font. A list's selection and
 * scrolling are still owned by the engine. */
static void draw_saved_paths(const menu_t *menu, const menuitem_t *widget) {
    (void)menu;
    for (int row = 0; row < widget->rect.h / widget->row_height && row + widget->first_row < widget->rows; ++row)
        DR_DrawText(palette_app, (ivec2_t){gameui->path_list.x + 2,
            gameui->path_list.y + row * gameui->path_row_height},
            path_row(widget, row + widget->first_row), gameui->path_list.w - 2);
}

bool DR_PaletteInit(sb_state_t *st) {
    int capacity = gameui->command_columns * gameui->command_rows;
    indices.categories = gameui->action_count;
    indices.paths = indices.categories + gameui->category_count;
    indices.list = indices.paths + gameui->path_action_count;
    indices.grid = indices.list + 1;
    indices.icons = indices.grid + 1;
    indices.slots = indices.icons + capacity;
    indices.previous = indices.slots + capacity;
    indices.next = indices.previous + 1;
    indices.upgrade = indices.next + 1;
    indices.decoy = indices.upgrade + 1;
    indices.radar = indices.decoy + 1;
    indices.chrome = indices.radar + 1;
    indices.keys = indices.chrome + gameui->image_count;
    indices.count = indices.keys + 4;
    palette = (menu_t){.numitems = indices.count, .itemOn = -1, .owner = st};
    palette.items = calloc(palette.numitems, sizeof(*palette.items));
    if (!palette.items) return false;
    for (int i = 0; i < palette.numitems; ++i)
        *item(i) = (menuitem_t){.link = -1, .look = {{.cell = -1}, {.cell = -1}, {.cell = -1}}};
    for (int i = 0; i < gameui->action_count; ++i)
        *item(i) = (menuitem_t){.kind = MI_CHECK, .routine = action, .userdata = &gameui->actions[i]};
    for (int i = 0; i < gameui->category_count; ++i)
        *item(indices.categories + i) = (menuitem_t){.kind = MI_CHECK, .routine = category};
    for (int i = 0; i < gameui->path_action_count; ++i) {
        menuitem_t *widget = item(indices.paths + i);
        *widget = (menuitem_t){.kind = MI_CHECK, .routine = action, .userdata = &gameui->path_actions[i]};
        if (gameui->path_actions[i].image != 14 && gameui->path_actions[i].action != UI_PATH_ADVANCED) {
            widget->ownerdraw = draw_label;
            widget->inset = (ivec2_t){gameui->path_actions[i].rect.w == 71 ? 7 : 9, 5};
            snprintf(widget->text, sizeof(widget->text), "%s", gameui->path_actions[i].label);
        }
    }
    *item(indices.list) = (menuitem_t){.kind = MI_LIST, .value = -1, .row = path_row,
        .routine = select_path, .ownerdraw = draw_saved_paths};
    *item(indices.grid) = (menuitem_t){.kind = MI_BUTTON, .routine = turn_page};
    for (int i = 0; i < capacity; ++i) {
        item(indices.icons + i)->ownerdraw = draw_quantity;
        *item(indices.slots + i) = (menuitem_t){.kind = MI_BUTTON, .routine = product,
                                               .link = -1};
    }
    *item(indices.previous) = (menuitem_t){.kind = MI_BUTTON, .routine = turn_page};
    *item(indices.next) = (menuitem_t){.kind = MI_BUTTON, .routine = turn_page};
    *item(indices.radar) = (menuitem_t){.kind = MI_BUTTON, .routine = camera};
    static const uiaction_t keys[] = {
        {.action = UI_PAGE, .product = DR_PAGE_PATHS}, {.action = UI_MOVE},
        {.action = UI_ATTACK}, {.action = UI_STOP},
    };
    static const SDL_Keycode hotkeys[] = {SDLK_p, SDLK_m, SDLK_a, SDLK_s};
    item(indices.categories)->hotkey = SDLK_b;
    for (int i = 0; i < 4; ++i)
        *item(indices.keys + i) = (menuitem_t){.kind = MI_BUTTON, .enabled = true,
            .hotkey = hotkeys[i], .routine = action, .userdata = &keys[i]};
    return true;
}

void DR_PaletteShutdown(void) {
    free(palette.items);
    palette = (menu_t){0};
    palette_app = NULL;
}

static void refresh(sb_state_t *st, const app_t *app) {
    int products[gameui->product_count];
    int count = product_list(st, products);
    int capacity = gameui->command_columns * gameui->command_rows;
    int pages = count ? (count + capacity - 1) / capacity : 1;
    if (st->production_page >= pages) st->production_page = 0;
    bool build = st->page == DR_PAGE_BUILD;
    bool paths = st->page == DR_PAGE_PATHS;
    for (int i = 0; i < gameui->action_count; ++i) {
        const uiaction_t *a = &gameui->actions[i];
        menuitem_t *widget = item(i);
        set_picture(widget, app, &st->images[a->image], a->source, a->rect, 192, true);
        widget->visible = widget->enabled = true;
        widget->value = a->action == UI_PAGE && st->page == a->product;
    }
    for (int i = 0; i < gameui->category_count; ++i) {
        const uicategory_t *c = &gameui->categories[i];
        menuitem_t *widget = item(indices.categories + i);
        set_picture(widget, app, &st->images[c->image], c->source, c->rect, 192, true);
        widget->visible = widget->enabled = true;
        widget->value = build;
    }
    for (int i = 0; i < gameui->path_action_count; ++i) {
        const uiaction_t *a = &gameui->path_actions[i];
        menuitem_t *widget = item(indices.paths + i);
        bool tab = a->action == UI_PATH_ADVANCED;
        if (!tab) set_picture(widget, app, &st->images[a->image], a->source, a->rect,
                              a->image == 14 ? 71 : a->source.w, true);
        else { widget->sheet = NULL; widget->rect = scaled(app, a->rect); }
        widget->visible = widget->enabled = paths && path_action_visible(st, a);
        widget->value = a->action == UI_WAYPOINT ? st->order == UI_WAYPOINT :
            a->action == UI_PATH_MODE && (int)st->path.mode == a->product;
    }
    menuitem_t *list = item(indices.list);
    list->rect = scaled(app, gameui->path_list);
    list->row_height = gameui->path_row_height * app->win.h / gameui->logical_height;
    if (list->row_height < 1) list->row_height = 1;
    list->visible = list->enabled = paths && st->path_advanced;
    M_MenuSetRows(list, st->saved_path_count);
    list->value = st->saved_path_selection;
    menuitem_t *grid = item(indices.grid);
    grid->rect = scaled(app, gameui->command_grid);
    grid->visible = build;
    grid->fill = 0xff000000u;
    for (int slot = 0; slot < capacity; ++slot) {
        int index = st->production_page * capacity + slot;
        int pindex = index < count ? products[index] : -1;
        const StaticProductDefinition *p = pindex >= 0 ?
            G_ModelProductByUIId(NULL, gameui->products[pindex].id) : NULL;
        irect_t rect = {gameui->command_grid.x + slot % gameui->command_columns * gameui->icon_size.w,
            gameui->command_grid.y + slot / gameui->command_columns * gameui->icon_size.h,
            gameui->icon_size.w, gameui->icon_size.h};
        menuitem_t *icon = item(indices.icons + slot), *widget = item(indices.slots + slot);
        icon->visible = build && p;
        icon->userdata = p;
        if (p) {
            irect_t source = st->product_icons[pindex].cells[0].rect;
            set_picture(icon, app, &st->product_icons[pindex], source,
                (irect_t){rect.x + 9, rect.y + 2, source.w, source.h}, 0, false);
            for (int state = 0; state < MS_STATES; ++state)
                icon->look[state].palette = enabled(p, producer_for(p)) ? -1 : 1;
        }
        set_picture(widget, app, &st->images[8], (irect_t){0,0,64,50}, rect, 64, false);
        widget->look[MS_PUSHED] = widget->look[MS_FOCUS];
        if (!p) widget->look[MS_FOCUS] = widget->look[MS_NORMAL];
        widget->userdata = p;
        widget->visible = widget->enabled = build;
    }
    grid->enabled = false; /* The slots handle clicks and wheel events. */
    set_picture(item(indices.previous), app, &st->images[9], (irect_t){0,0,22,22}, (irect_t){448,316,22,22}, 0, true);
    set_picture(item(indices.next), app, &st->images[10], (irect_t){0,0,22,22}, (irect_t){470,316,22,22}, 0, true);
    set_picture(item(indices.upgrade), app, &st->images[12], (irect_t){213,0,71,22}, (irect_t){496,316,71,22}, 0, true);
    set_picture(item(indices.decoy), app, &st->images[12], (irect_t){0,0,71,22}, (irect_t){568,316,71,22}, 0, true);
    for (int i = indices.previous; i <= indices.decoy; ++i) {
        item(i)->visible = build;
        item(i)->enabled = build && i <= indices.next;
    }
    item(indices.radar)->rect = scaled(app, DR_MinimapRect(&level));
    item(indices.radar)->visible = item(indices.radar)->enabled = st->radar_visible && G_ModelRadarLevel(consoleplayer);
    for (int i = 0; i < gameui->image_count; ++i) {
        item(indices.chrome + i)->rect = scaled(app, gameui->images[i].destination);
        item(indices.chrome + i)->visible = true;
    }
}

bool DR_PaletteResponder(sb_state_t *st, app_t *app, const SDL_Event *event) {
    palette_app = app;
    refresh(st, app);
    if ((event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_RIGHT && st->order) ||
        (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE && st->order)) {
        st->order = UI_UNAVAILABLE;
        return true;
    }
    bool taken = M_MenuResponder(&palette, app, event);
    /* MENU opens the native options screen instead of a HUD popup. */
    if (st->options_visible) {
        st->options_visible = false;
        DR_OpenOptions(app);
        return true;
    }
    if (taken) return true;
    if (SB_PathResponder(st, app, event)) return true;
    if (!st->order || event->type != SDL_MOUSEBUTTONDOWN) return false;
    if (event->button.button == SDL_BUTTON_LEFT) {
        cell_t cell = R_ScreenToMapGrid(app, &level, palette.cursor.x, palette.cursor.y);
        fvec2_t goal = fvec2_cell_center(cell);
        if (st->order == UI_MOVE) SB_SelectedOrder(TC_MOVE, goal, 0);
        else {
            mobjlist_t units = P_ListMobjs();
            int picked = R_PickUnit(app, &level, units.items, units.count, NULL,
                st->sprites, gameinfo, palette.cursor.x, palette.cursor.y,
                st->order == UI_PRODUCT ? consoleplayer : -1);
            if (picked >= 0) {
                if (st->order == UI_PRODUCT) G_BuildOrder(units.items[picked], st->order_product);
                else SB_SelectedOrder(TC_ATTACK, goal, units.items[picked]->id);
            }
            P_FreeMobjList(&units);
        }
    }
    st->order = UI_UNAVAILABLE;
    return true;
}

void DR_PaletteDrawer(sb_state_t *st, const app_t *app) {
    palette_app = (app_t *)app;
    refresh(st, app);
    if (st->page == DR_PAGE_PATHS) {
        draw_image(st, app, 4, (irect_t){0,0,192,278}, (irect_t){448,64,192,278});
        draw_image(st, app, 13, (irect_t){st->path_advanced ? 192 : 0,0,192,32}, (irect_t){448,64,192,32});
        DR_DrawHeader(app, (ivec2_t){st->path_advanced ? 462 : 461,st->path_advanced ? 78 : 75}, "Basic",71);
        DR_DrawHeader(app, (ivec2_t){st->path_advanced ? 561 : 560,st->path_advanced ? 78 : 77}, "Advanced",78);
        if (st->path_advanced) {
            DR_DrawCaption(app,(ivec2_t){544,205},"Path Direction",true);
            DR_DrawCaption(app,(ivec2_t){468,258},"Current Path",false);
            DR_DrawCaption(app,(ivec2_t){555,257},"Saved Paths",false);
            DR_DrawText(app,(ivec2_t){470,260}, st->saved_path_selection >= 0 ?
                M_va("Trail %d", st->saved_path_selection + 1) : "None Selected",76);
        }
    }
    M_MenuDrawer(&palette);
    if (st->page == DR_PAGE_BUILD) {
        DR_DrawText(app,(ivec2_t){507,323},"Upgrade",60);
        DR_DrawText(app,(ivec2_t){586,323},"Decoy",48);
    }
    const menuitem_t *hover = palette.itemOn >= 0 ? item(palette.itemOn) : NULL;
    ivec2_t mouse = {palette.cursor.x * gameui->logical_width / app->win.w,
                     palette.cursor.y * gameui->logical_height / app->win.h};
    if (hover && hover->visible && hover->enabled) {
        if (hover->routine == product && hover->userdata) {
            const StaticProductDefinition *p = hover->userdata;
            tooltip(app,mouse,p->label,p,producer_for(p));
        } else if (hover->routine == category)
            tooltip(app,mouse,gameui->categories[palette.itemOn - indices.categories].label,NULL,NULL);
        else if (hover->routine == action && palette.itemOn < indices.categories)
            tooltip(app,mouse,((const uiaction_t *)hover->userdata)->label,NULL,NULL);
    }
}
