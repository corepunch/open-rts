#include "engine.h"

#include <string.h>

/* Pieces for HUD tables of games without a native sidebar. The game's table
 * places them; these draw and act. */
hudview_t hudview;

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

/* Seven-segment digits 14 pixels apart: centred on the rect, or ending at
 * its right edge. */
void HU_DrawCounter(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu;
    if (item->value < 0 || item->value >= RTS_MAX_RESOURCES) return;
    int amount = level.player_resources[consoleplayer][item->value];
    char value[24];
    snprintf(value, sizeof(value), "%d", amount < 0 ? 0 : amount);
    int count = (int)strlen(value);
    int x = item->align & MALIGN_RIGHT ? rect.x + rect.w - count * 14 + 2 :
                                         rect.x + rect.w / 2 - count * 7;
    for (int i = 0; i < count; ++i)
        draw_digit(x + i * 14, rect.y, value[i] - '0', item->ink);
}

void HU_DrawClock(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu; (void)item;
    int seconds = leveltime / RTS_TICRATE;
    int minutes = (seconds / 60) % 100;
    seconds %= 60;
    int x = rect.x + 9, y = rect.y + 3;
    draw_digit(x, y, minutes / 10, 0xffffffffu);
    draw_digit(x + 14, y, minutes % 10, 0xffffffffu);
    uint8_t white = V_NearestIndex(0xffffffffu);
    V_DrawPoint((ivec2_t){x + 29, y + 7}, white);
    V_DrawPoint((ivec2_t){x + 29, y + 14}, white);
    draw_digit(x + 34, y, seconds / 10, 0xffffffffu);
    draw_digit(x + 48, y, seconds % 10, 0xffffffffu);
}

/* ── the product list ───────────────────────────────────────────────────── */

enum { PRODUCT_LIST_MAX = 64 };

static uint32_t producer_id;
static char product_root[1024];

void HU_InitProducts(menu_t *menu, const char *root) {
    snprintf(product_root, sizeof(product_root), "%s", root ? root : g_game_default_root);
    producer_id = 0;
    M_MenuTarget(menu, NULL);
}

void HU_DrawPlacement(const menu_t *menu, uint16_t type, const mobj_t *builder) {
    const mobjtype_t *actor = P_ActorType(type);
    if (!actor || !builder) return;
    ivec2_t cell = R_ScreenToMapGrid(menu->app, &level, menu->cursor.x, menu->cursor.y);
    isize2_t foot = actor->footprint;
    bool clear = true;
    for (int y = 0; y < foot.h; ++y)
        for (int x = 0; x < foot.w; ++x) {
            if (actor->foundation && actor->foundation[y * foot.w + x] == ' ') continue;
            clear &= P_BuildingCellClear(type, ivec2_add(cell, (ivec2_t){x, y}), builder);
        }
    bool rule_blocked = clear && !P_CanPlaceBuilding(type, cell, builder);
    R_DrawBuildingPreview(menu->app, type, cell, builder->team, hudview.sprites);
    for (int y = 0; y < foot.h; ++y)
        for (int x = 0; x < foot.w; ++x) {
            if (actor->foundation && actor->foundation[y * foot.w + x] == ' ') continue;
            ivec2_t at = ivec2_add(cell, (ivec2_t){x, y});
            fvec2_t corner, opposite;
            R_MapToScreen(menu->app, &level, at.x, at.y, &corner.x, &corner.y);
            R_MapToScreen(menu->app, &level, at.x + 1, at.y + 1, &opposite.x, &opposite.y);
            irect_t rect = {(int)lroundf(fminf(corner.x, opposite.x)), (int)lroundf(fminf(corner.y, opposite.y)),
                            menu->app->cell.w, menu->app->cell.h};
            bool valid = !rule_blocked && P_BuildingCellClear(type, at, builder);
            /* Stratagus DrawBuildingCursor uses opacity 95/255. */
            V_FillRectTranslucent(rect, valid ? 0x5f00ff00u : 0x5fff0000u);
        }
}

void HU_DrawProductPlacement(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)item; (void)rect;
    const StaticProductDefinition *p = G_ModelProductByUIId(NULL, menu->placement.product);
    mobj_t *builder = P_MobjById(menu->placement.builder);
    if (p && builder && P_MobjIsSelected(builder))
        HU_DrawPlacement(menu, G_ModelActorIdForProduct(p), builder);
}

void HU_BuildProduct(menu_t *menu, menuitem_t *item, mobj_t *producer,
                     const StaticProductDefinition *product) {
    uint16_t type = G_ModelActorIdForProduct(product);
    if (product->product_class != RTS_PRODUCT_BUILDING ||
        (producer->info && producer->info->deploy.type == type)) {
        G_BuildOrder(producer, product->ui_id);
        return;
    }
    if (producer->production && producer->production->queue_count) return;
    const mobjtype_t *actor = P_ActorType(type);
    if (!actor) return;
    if (hudview.sprites && !R_CacheLookup(hudview.sprites, actor->sprite_name)) {
        mobj_t image = {.type_id = type, .info = actor};
        snprintf(image.core.sprite_name, sizeof(image.core.sprite_name), "%s", actor->sprite_name);
        mobj_t *images[] = {&image};
        if (!R_InitSprites(product_root, &level, images, 1, (spritecache_t *)hudview.sprites)) return;
        R_BindSprites((spritecache_t *)hudview.sprites, gameinfo);
    }
    M_MenuTarget(menu, item);
    menu->placement.builder = producer->id;
    menu->placement.product = product->ui_id;
}

void HU_PlaceProduct(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action != MA_TARGET) return;
    mobj_t *builder = P_MobjById(menu->placement.builder);
    const StaticProductDefinition *p = G_ModelProductByUIId(NULL, menu->placement.product);
    if (!builder || builder->hp <= 0 || builder->remove || !P_MobjIsSelected(builder) || !p) return;
    ivec2_t cell = R_ScreenToMapGrid(menu->app, &level, menu->cursor.x, menu->cursor.y);
    if ((builder->production && builder->production->queue_count) ||
        level.player_resources[builder->owner][0] < p->cost ||
        !G_ModelProductAvailable(NULL, builder->owner, p) ||
        !P_CanPlaceBuilding(G_ModelActorIdForProduct(p), cell, builder) ||
        !G_ConstructOrder(builder, p->ui_id, cell)) M_MenuTarget(menu, item);
}

static mobj_t *selected_producer(void) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (unit->owner == consoleplayer && unit->hp > 0 && !unit->remove && P_MobjIsSelected(unit))
            return unit;
    }
    return NULL;
}

static int production_list(const mobj_t *producer, StaticProductDefinition products[PRODUCT_LIST_MAX]) {
    if (!producer) return 0;
    int count = G_ModelGetProducts(NULL, consoleplayer, products, PRODUCT_LIST_MAX);
    int output = 0;
    for (int i = 0; i < count; ++i)
        for (int j = 0; j < products[i].maker_count; ++j) {
            if (products[i].makers[j] != producer->type_id) continue;
            products[output++] = products[i];
            break;
        }
    return output;
}

static bool product_enabled(const mobj_t *producer, const StaticProductDefinition *product) {
    const production_t *queue = producer->production;
    return G_ModelProductAvailable(NULL, consoleplayer, product) &&
        R_CanAfford(consoleplayer, product) &&
        (!queue || queue->queue_count == 0 ||
         (queue->product_type == product->product_type &&
          queue->product_class == product->product_class &&
          queue->queue_count < RTS_MAX_PRODUCTION_QUEUE));
}

static int page_rows(const menuitem_t *list) {
    return list->row_height > 0 ? list->rect.h / list->row_height : 1;
}

/* A new producer starts on the first page. */
void HU_RefreshProducts(menu_t *menu) {
    StaticProductDefinition products[PRODUCT_LIST_MAX];
    mobj_t *producer = selected_producer();
    uint32_t id = producer ? producer->id : 0;
    for (int i = 0; i < menu->numitems; ++i) {
        menuitem_t *list = &menu->items[i];
        if (list->routine != HU_ProductList) continue;
        if (id != producer_id) {
            list->first_row = 0;
            M_MenuTarget(menu, NULL);
        }
        list->rows = production_list(producer, products);
        if (list->first_row >= list->rows) list->first_row = 0;
    }
    producer_id = id;
}

void HU_ProductList(menu_t *menu, menuitem_t *item, menuaction_t action) {
    if (action == MA_TARGET) { HU_PlaceProduct(menu, item, action); return; }
    if (action != MA_CHANGE) return;
    StaticProductDefinition products[PRODUCT_LIST_MAX];
    mobj_t *producer = selected_producer();
    int count = production_list(producer, products);
    int index = item->value;
    item->value = -1;
    if (index >= 0 && index < count && product_enabled(producer, &products[index]))
        HU_BuildProduct(menu, item, producer, &products[index]);
}

static void small_text(ivec2_t at, const char *text, int width, uint32_t argb) {
    V_DrawSmallText((irect_t){at.x, at.y, width, 7}, text, argb,
                    V_DrawSize());
}

void HU_DrawProducts(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    (void)menu;
    StaticProductDefinition products[PRODUCT_LIST_MAX];
    mobj_t *producer = selected_producer();
    int count = production_list(producer, products);
    if (!count) {
        small_text((ivec2_t){rect.x + 5, rect.y + 8}, "SELECT A PRODUCER", rect.w - 10, 0xffc8d2b4u);
        return;
    }
    uint8_t border = V_NearestIndex(0xff303e46u);
    for (int row = 0; row < page_rows(item); ++row) {
        int index = item->first_row + row;
        if (index >= count) break;
        const StaticProductDefinition *product = &products[index];
        irect_t cell = {rect.x, rect.y + row * item->row_height, rect.w, item->row_height - 1};
        V_DrawRectOutline(cell, border);
        uint32_t ink = product_enabled(producer, product) ? 0xffdce6dcu : 0xff646464u;
        small_text((ivec2_t){cell.x + 5, cell.y + 5}, product->label, cell.w - 10, ink);
        const production_t *queue = producer->production;
        int queued = queue && queue->product_type == product->product_type ? queue->queue_count : 0;
        char text[48];
        snprintf(text, sizeof(text), "%d   QUEUED %d", product->cost, queued);
        small_text((ivec2_t){cell.x + 5, cell.y + 18}, text, cell.w - 10, ink);
    }
}

void HU_ProductPage(menu_t *menu, menuitem_t *item, menuaction_t action) {
    menuitem_t *list = item->link >= 0 && item->link < menu->numitems ? &menu->items[item->link] : NULL;
    if (action != MA_ACTIVATE || !list) return;
    int rows = page_rows(list);
    int pages = list->rows > 0 ? (list->rows + rows - 1) / rows : 1;
    list->first_row = (list->first_row / rows + 1) % pages * rows;
}

void HU_DrawProductPage(const menu_t *menu, const menuitem_t *item, irect_t rect) {
    const menuitem_t *list = item->link >= 0 && item->link < menu->numitems ? &menu->items[item->link] : NULL;
    if (!list) return;
    int rows = page_rows(list);
    if (list->rows <= rows) return;
    char text[40];
    snprintf(text, sizeof(text), "NEXT PAGE %d OF %d", list->first_row / rows + 1,
             (list->rows + rows - 1) / rows);
    small_text((ivec2_t){rect.x + 5, rect.y + 8}, text, rect.w - 10, 0xffc8d2b4u);
}

/* ── routes ─────────────────────────────────────────────────────────────── */

bool HU_SelectedOrder(ticorder_t order, fvec2_t goal, uint32_t target) {
    mobjlist_t units = P_ListMobjs();
    bool ok = G_SelectedTiccmd(order, units.items, units.count, fixed2_from_fvec2(goal), target);
    P_FreeMobjList(&units);
    return ok;
}

void HU_PathReset(pathbook_t *book) {
    memset(book, 0, sizeof(*book));
    book->path.mode = WP_ONCE;
    book->selection = -1;
}

void HU_PathClear(pathbook_t *book) {
    book->path.count = book->path.current = 0;
}

bool HU_PathDelete(pathbook_t *book) {
    waypoints_t *path = &book->path;
    if (!path->count) return false;
    memmove(path->points + path->current, path->points + path->current + 1,
            (--path->count - path->current) * sizeof(*path->points));
    if (path->current && path->current == path->count) --path->current;
    return true;
}

bool HU_PathSave(pathbook_t *book) {
    if (!book->path.count || book->saved_count == MAXSAVEDPATHS) return false;
    book->saved[book->saved_count++] = book->path;
    return true;
}

void HU_PathSelect(pathbook_t *book, int saved) {
    if (saved < 0 || saved >= book->saved_count) {
        book->selection = -1;
        book->path = (waypoints_t){.mode = WP_ONCE};
        return;
    }
    book->selection = saved;
    book->path = book->saved[saved];
    book->path.current = 0;
}

bool HU_PathPoint(pathbook_t *book, cell_t cell) {
    waypoints_t *path = &book->path;
    if (!L_Contains(&level, cell.x, cell.y)) return false;
    for (int i = 0; i < path->count; ++i)
        if (ivec2_equal(cell, path->points[i])) { path->current = i; return true; }
    if (path->count == MAXWAYPOINTS) return false;
    path->current = path->count;
    path->points[path->count++] = cell;
    return true;
}

bool HU_PathGo(pathbook_t *book) {
    mobjlist_t units = P_ListMobjs();
    bool ok = G_PathOrder(units.items, units.count, &book->path);
    P_FreeMobjList(&units);
    return ok;
}
