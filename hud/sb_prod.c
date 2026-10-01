#include "engine.h"
#include <ctype.h>

/* Minimal engine production controls. These are not retail sidebar scripts. */
enum { PRODUCT_ROWSIZE = 32, PRODUCT_LIST_MAX = 64 };

static mobj_t *selected_producer(void) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (unit->owner == consoleplayer && unit->hp > 0 && !unit->remove && P_MobjIsSelected(unit))
            return unit;
    }
    return NULL;
}

static int production_list(sb_state_t *st, mobj_t *producer,
                            StaticProductDefinition products[PRODUCT_LIST_MAX]) {
    uint32_t id = producer ? producer->id : 0;
    if (st->production_selection != id) {
        st->production_selection = id;
        st->production_page = 0;
    }
    if (!producer) return 0;
    int count = G_ModelGetProducts(NULL, consoleplayer, products, PRODUCT_LIST_MAX);
    int output = 0;
    for (int i = 0; i < count; ++i) {
        for (int j = 0; j < products[i].maker_count; ++j) {
            if (products[i].makers[j] != producer->type_id) continue;
            products[output++] = products[i];
            break;
        }
    }
    return output;
}

static irect_t scaled_rect(const app_t *app, irect_t rect) {
    return (irect_t){ rect.x * app->win.w / gameui->logical_width,
                       rect.y * app->win.h / gameui->logical_height,
                       rect.w * app->win.w / gameui->logical_width,
                       rect.h * app->win.h / gameui->logical_height };
}

static bool product_enabled(const mobj_t *producer, const StaticProductDefinition *product) {
    const production_t *queue = producer->production;
    return G_ModelProductAvailable(NULL, consoleplayer, product) &&
        level.player_resources[consoleplayer][0] >= product->cost &&
        (!queue || queue->queue_count == 0 ||
         (queue->product_type == product->product_type &&
          queue->product_class == product->product_class &&
          queue->queue_count < RTS_MAX_PRODUCTION_QUEUE));
}

bool SB_ProductionResponder(sb_state_t *st, app_t *app, const SDL_Event *event) {
    if (!st || !st->ready || !app || !gameui ||
        gameui->command_grid.h < PRODUCT_ROWSIZE * 2) return false;
    if (event->type != SDL_MOUSEBUTTONDOWN && event->type != SDL_MOUSEBUTTONUP)
        return false;
    ivec2_t mouse;
    R_WindowToRenderPt(app, event->button.x, event->button.y, &mouse.x, &mouse.y);
    if (!irect_contains(scaled_rect(app, gameui->command_grid), mouse)) return false;
    if (event->type != SDL_MOUSEBUTTONDOWN || event->button.button != SDL_BUTTON_LEFT)
        return true;
    StaticProductDefinition products[PRODUCT_LIST_MAX];
    mobj_t *producer = selected_producer();
    int count = production_list(st, producer, products);
    int rows = gameui->command_grid.h / PRODUCT_ROWSIZE - 1;
    int row = (mouse.y * gameui->logical_height / app->win.h -
               gameui->command_grid.y) / PRODUCT_ROWSIZE;
    int pages = count > 0 ? (count + rows - 1) / rows : 1;
    if (row >= rows) {
        st->production_page = (st->production_page + 1) % pages;
    } else {
        int index = st->production_page * rows + row;
        if (index < count && product_enabled(producer, &products[index]))
            G_BuildOrder(producer, products[index].ui_id);
    }
    return true;
}

void SB_DrawText(ivec2_t point, const char *text, int width, uint32_t argb) {
    V_DrawSmallText((irect_t){point.x, point.y, width, 7}, text, argb,
                    (isize2_t){gameui->logical_width, gameui->logical_height});
}

void SB_ProductionDrawer(sb_state_t *st, const app_t *app) {
    if (!st || !st->ready || !app || !gameui ||
        gameui->command_grid.h < PRODUCT_ROWSIZE * 2) return;
    StaticProductDefinition products[PRODUCT_LIST_MAX];
    mobj_t *producer = selected_producer();
    int count = production_list(st, producer, products);
    irect_t grid = gameui->command_grid;
    irect_t panel = scaled_rect(app, grid);
    V_FillRect(panel, V_NearestIndex(0xff0c1216u));
    int rows = grid.h / PRODUCT_ROWSIZE - 1;
    uint8_t border = V_NearestIndex(0xff303e46u);
    for (int row = 0; row < rows; ++row) {
        int index = st->production_page * rows + row;
        if (index >= count) break;
        const StaticProductDefinition *product = &products[index];
        bool enabled = product_enabled(producer, product);
        irect_t cell = {grid.x, grid.y + row * PRODUCT_ROWSIZE, grid.w, PRODUCT_ROWSIZE - 1};
        irect_t rect = scaled_rect(app, cell);
        V_DrawRectOutline(rect, border);
        uint32_t ink = enabled ? 0xffdce6dcu : 0xff646464u;
        SB_DrawText((ivec2_t){cell.x + 5, cell.y + 5}, product->label, cell.w - 10, ink);
        char text[48];
        const production_t *queue = producer->production;
        int queued = queue && queue->product_type == product->product_type ? queue->queue_count : 0;
        snprintf(text, sizeof(text), "%d   QUEUED %d", product->cost, queued);
        SB_DrawText((ivec2_t){cell.x + 5, cell.y + 18}, text, cell.w - 10, ink);
        if (gameui->draw_product_slot)
            gameui->draw_product_slot(app, product->ui_id, rect);
    }
    if (!count) {
        SB_DrawText((ivec2_t){grid.x + 5, grid.y + 8}, "SELECT A PRODUCER", grid.w - 10,
                    0xffc8d2b4u);
    } else if (count > rows) {
        char text[40];
        snprintf(text, sizeof(text), "NEXT PAGE %d OF %d", st->production_page + 1,
                 (count + rows - 1) / rows);
        SB_DrawText((ivec2_t){grid.x + 5, grid.y + rows * PRODUCT_ROWSIZE + 8},
                         text, grid.w - 10, 0xffc8d2b4u);
    }
}
