#include "d_net.h"
#include "sb_bar.h"
#include "game.h"
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

bool SB_ProductionResponder(sb_state_t *st, const app_t *app, const SDL_Event *event) {
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

/* Five-column glyphs for labels and prices; no game font is required. */
static void production_text(const app_t *app, ivec2_t point, const char *text, int width) {
    static const uint8_t glyphs[][5] = {
        {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},
        {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},
        {6,0x49,0x49,0x29,0x1e},
        {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
        {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},
        {0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
        {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
        {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
        {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
        {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},
    };
    for (int n = 0; text[n] && n * 6 + 5 <= width; ++n) {
        int ch = toupper((unsigned char)text[n]);
        int glyph = ch >= '0' && ch <= '9' ? ch - '0' :
                    ch >= 'A' && ch <= 'Z' ? ch - 'A' + 10 : -1;
        if (glyph < 0) continue;
        for (int x = 0; x < 5; ++x)
            for (int y = 0; y < 7; ++y)
                if (glyphs[glyph][x] & (1u << y)) {
                    irect_t pixel = scaled_rect(app, (irect_t){point.x + n * 6 + x, point.y + y, 1, 1});
                    SDL_RenderFillRect(app->renderer, &pixel);
                }
    }
}

void SB_ProductionDrawer(sb_state_t *st, const app_t *app) {
    if (!st || !st->ready || !app || !gameui ||
        gameui->command_grid.h < PRODUCT_ROWSIZE * 2) return;
    StaticProductDefinition products[PRODUCT_LIST_MAX];
    mobj_t *producer = selected_producer();
    int count = production_list(st, producer, products);
    irect_t grid = gameui->command_grid;
    irect_t panel = scaled_rect(app, grid);
    SDL_SetRenderDrawColor(app->renderer, 12, 18, 22, 255);
    SDL_RenderFillRect(app->renderer, &panel);
    int rows = grid.h / PRODUCT_ROWSIZE - 1;
    for (int row = 0; row < rows; ++row) {
        int index = st->production_page * rows + row;
        if (index >= count) break;
        const StaticProductDefinition *product = &products[index];
        bool enabled = product_enabled(producer, product);
        irect_t cell = {grid.x, grid.y + row * PRODUCT_ROWSIZE, grid.w, PRODUCT_ROWSIZE - 1};
        irect_t rect = scaled_rect(app, cell);
        SDL_SetRenderDrawColor(app->renderer, 48, 62, 70, 255);
        SDL_RenderDrawRect(app->renderer, &rect);
        SDL_SetRenderDrawColor(app->renderer, enabled ? 220 : 100, enabled ? 230 : 100,
                               enabled ? 220 : 100, 255);
        production_text(app, (ivec2_t){cell.x + 5, cell.y + 5}, product->label, cell.w - 10);
        char text[48];
        const production_t *queue = producer->production;
        int queued = queue && queue->product_type == product->product_type ? queue->queue_count : 0;
        snprintf(text, sizeof(text), "%d   QUEUED %d", product->cost, queued);
        production_text(app, (ivec2_t){cell.x + 5, cell.y + 18}, text, cell.w - 10);
    }
    SDL_SetRenderDrawColor(app->renderer, 200, 210, 180, 255);
    if (!count) {
        production_text(app, (ivec2_t){grid.x + 5, grid.y + 8}, "SELECT A PRODUCER", grid.w - 10);
    } else if (count > rows) {
        char text[40];
        snprintf(text, sizeof(text), "NEXT PAGE %d OF %d", st->production_page + 1,
                 (count + rows - 1) / rows);
        production_text(app, (ivec2_t){grid.x + 5, grid.y + rows * PRODUCT_ROWSIZE + 8},
                         text, grid.w - 10);
    }
}
