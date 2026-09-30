#include "game.h"
#include "dr_hud.h"
#include "d_net.h"

static const uiimage_t DARK_REIGN_UI_IMAGES[] = {
    { "graphics/INTFACE/IGI/TOPBTNS.BMP", {   0, 0, 147, 32 }, {   6,   0, 147,  32 } }, /* 0 */
    { "graphics/INTFACE/IGI/TOPBITS.BMP", {   6, 0, 141, 32 }, { 153,   0, 141,  32 } }, /* 1 */
    { "graphics/INTFACE/IGI/TOPBTNS.BMP", { 147, 0, 147, 32 }, { 294,   0, 147,  32 } }, /* 2 */
    { "graphics/INTFACE/IGI/MFDBTNS.BMP", {   0, 0, 192, 64 }, { 448,   0, 192,  64 } }, /* 3 */
    { "graphics/INTFACE/IGI/MFDBAC1.BMP", {   0, 0, 192,278 }, { 448,  64, 192, 278 } }, /* 4 */
    { "graphics/INTFACE/IGI/BUBLDBIT.BMP",{   0, 0, 192, 28 }, { 448, 314, 192,  28 } }, /* 5 */
    { "graphics/INTFACE/IGI/MINIMAP.BMP", {   0, 0, 140,138 }, { 448, 342, 140, 138 } }, /* 6 */
    { "graphics/INTFACE/IGI/RESOBARS.BMP",{   0, 0,  52,104 }, { 588, 376,  52, 104 } }, /* 7 */
    { "graphics/INTFACE/IGI/BUISOBOX.BMP", {0}, {0} }, /* 8 */
    { "graphics/INTFACE/IGI/BUSCRLUP.BMP", {0}, {0} }, /* 9 */
    { "graphics/INTFACE/IGI/BUSCRLDN.BMP", {0}, {0} }, /* 10 */
    { "graphics/INTFACE/IGI/TEAMPIC.BMP", {0,0,52,34}, {588,342,52,34} }, /* 11 */
    { "graphics/INTFACE/IGI/SBTNS.BMP", {0}, {0} }, /* 12 */
    { "graphics/INTFACE/IGI/BASADV.BMP", {0}, {0} }, /* 13 */
    { "graphics/INTFACE/IGI/TRAILMDE.BMP", {0}, {0} }, /* 14 */
};

/* Retail MFDBTNS are pages, not infantry/vehicle categories. */
static const uicategory_t DARK_REIGN_UI_CATEGORIES[] = {
    { "BUILD", {448, 0, 64, 32}, 3, {0, 0, 64, 32} },
};
static const uiaction_t DARK_REIGN_UI_ACTIONS[] = {
    { "COMMS", UI_UNAVAILABLE, {512, 0, 64, 32}, 3, {64, 0, 64, 32}, 0 },
    { "MENU", UI_OPTIONS, {576, 0, 64, 32}, 3, {128, 0, 64, 32}, 0 },
    { "ORDERS", UI_UNAVAILABLE, {448, 32, 64, 32}, 3, {0, 32, 64, 32}, 0 },
    { "PATHS", UI_PAGE, {512, 32, 64, 32}, 3, {64, 32, 64, 32}, DR_PAGE_PATHS },
    { "SPECIAL", UI_UNAVAILABLE, {576, 32, 64, 32}, 3, {128, 32, 64, 32}, 0 },
};

/* 00467d60 constructs these controls; 00490900 selects TRAILMDE states. */
static const uiaction_t DARK_REIGN_PATH_ACTIONS[] = {
    {"Add Waypoints", UI_WAYPOINT, {502,100,103,22}, 12, {284,0,103,22}, 0},
    {"Clear All", UI_PATH_CLEAR, {478,125,71,22}, 12, {0,0,71,22}, 0},
    {"Delete", UI_PATH_DELETE, {559,125,71,22}, 12, {0,0,71,22}, 0},
    {"Go", UI_PATH_GO, {502,150,103,22}, 12, {284,0,103,22}, 0},
    {"De-Select", UI_PATH_DESELECT, {468,280,71,22}, 12, {0,0,71,22}, 0},
    {"Save Path", UI_PATH_SAVE, {468,305,71,22}, 12, {0,0,71,22}, 0},
    {"One Pass", UI_PATH_MODE, {508,205,24,22}, 14, {0,0,24,22}, WP_ONCE},
    {"Backtrack", UI_PATH_MODE, {532,205,23,22}, 14, {24,0,23,22}, WP_BACKTRACK},
    {"Loop", UI_PATH_MODE, {555,205,24,22}, 14, {47,0,24,22}, WP_LOOP},
    {"Basic", UI_PATH_ADVANCED, {448,64,96,32}, 13, {0}, 0},
    {"Advanced", UI_PATH_ADVANCED, {544,64,96,32}, 13, {0}, 1},
};

/* Native menu sprites, separate from world sprite IDs. */
static const uiproduct_t DARK_REIGN_UI_PRODUCTS[] = {
    /* BUILD: all buildings and Construction Rig */
    { 10001, 0, "bfhqtmn0.spr" }, { 10002, 0, "bfhqtmn1.spr" }, { 10003, 0, "bfhqtmn2.spr" },
    { 10004, 0, "bfutfmn0.spr" }, { 10005, 0, "bfutfmn1.spr" },
    { 10006, 0, "bfvcymn0.spr" }, { 10007, 0, "bfvcymn1.spr" },
    { 10008, 0, "bfhspmn0.spr" }, { 10009, 0, "bfrepmn0.spr" },
    { 10010, 0, "bccammn0.spr" }, { 10011, 0, "bfrrmmn0.spr" },
    { 10012, 0, "bfaarmn0.spr" }, { 10013, 0, "bfgdtmn0.spr" }, { 10014, 0, "bfagtmn0.spr" },
    { 10015, 0, "bfphfmn0.spr" }, { 10016, 0, "bfphfmn1.spr" },
    { 10019, 0, "bclncmn0.spr" }, { 10020, 0, "bcpowmn0.spr" },
    { 10040, 0, "bcsbhmn0.spr" }, { 10041, 0, "bcsbvmn0.spr" }, { 10042, 0, "bcsbcmn0.spr" },
    {    11, 0, "ucfcnmn0.spr" },
    /* COMMS: infantry */
    {  9, 1, "ufradmn0.spr" }, { 10, 1, "ufmrcmn0.spr" }, {  8, 1, "ufsnpmn0.spr" },
    {  6, 1, "ufsctmn0.spr" }, {  7, 1, "ufmedmn0.spr" }, {  3, 1, "ufsabmn0.spr" },
    {  2, 1, "ufmecmn0.spr" }, {  5, 1, "ufmtrmn0.spr" }, {  4, 1, "ucinfmn0.spr" },
    /* Vehicles */
    {  1, 2, "ufspbmn0.spr" }, { 15, 2, "ufratmn0.spr" }, { 20, 2, "ufsktmn0.spr" },
    { 17, 2, "ufthnmn0.spr" }, { 21, 2, "ufphtmn0.spr" }, { 12, 2, "ufflkmn0.spr" },
    { 16, 2, "uftrtmn0.spr" }, { 19, 2, "uffarmn0.spr" }, { 23, 2, "ufskbmn0.spr" },
    { 24, 2, "ufoutmn0.spr" }, { 18, 2, "ufswvmn0.spr" }, { 30, 2, "ucwcomn0.spr" },
    { 13, 2, "ucfrgmn0.spr" }, { 14, 2, "uchfrmn0.spr" },
    /* Imperium uses its own native menu images and production IDs. */
    {11001, 0, "bihqtmn0.spr"}, {11002, 0, "bihqtmn1.spr"}, {11003, 0, "bihqtmn2.spr"},
    {11004, 0, "biutfmn0.spr"}, {11005, 0, "biutfmn1.spr"},
    {11006, 0, "bivcymn0.spr"}, {11007, 0, "bivcymn1.spr"},
    {11008, 0, "bitgtmn0.spr"}, {11009, 0, "bihspmn0.spr"},
    {11010, 0, "birepmn0.spr"}, {11011, 0, "bccammn0.spr"},
    {11012, 0, "birrmmn0.spr"}, {11013, 0, "biaarmn0.spr"},
    {11014, 0, "bigdtmn0.spr"}, {11015, 0, "biagtmn0.spr"},
    {11019, 0, "bclncmn0.spr"}, {11020, 0, "bcpowmn0.spr"}, {11021, 0, "bitrcmn0.spr"},
    {1005, 0, "ucfcnmn0.spr"}, {1002, 1, "uigrdmn0.spr"},
    {1003, 1, "uibonmn0.spr"}, {1004, 1, "uiextmn0.spr"}, {1001, 1, "ucinfmn0.spr"},
    {1010, 2, "uisttmn0.spr"}, {1009, 2, "uiittmn0.spr"}, {1011, 2, "uipltmn0.spr"},
    {1008, 2, "uiampmn0.spr"}, {1013, 2, "uimadmn0.spr"}, {1019, 2, "uirdrmn0.spr"},
    {1015, 2, "uishrmn0.spr"}, {1014, 2, "uihosmn0.spr"}, {1012, 2, "uitctmn0.spr"},
    {1017, 2, "uiiarmn0.spr"}, {1020, 2, "uicycmn0.spr"}, {1018, 2, "uiskymn0.spr"},
    {1016, 2, "ucwcomn0.spr"}, {1006, 2, "ucfrgmn0.spr"}, {1007, 2, "uchfrmn0.spr"},
};

static const uidefinition_t DARK_REIGN_UI = {
    .logical_width = 640,
    .logical_height = 480,
    .world_viewport = { 0, 32, 448, 448 },
    .minimap = { 454, 348, 128, 126 },
    .command_grid = { 448, 64, 192, 250 },
    .command_columns = 3,
    .command_rows = 5,
    .icon_size = { 64, 50 },
    .images = DARK_REIGN_UI_IMAGES,
    .image_count = (int)(sizeof(DARK_REIGN_UI_IMAGES) / sizeof(DARK_REIGN_UI_IMAGES[0])),
    .products = DARK_REIGN_UI_PRODUCTS,
    .product_count = (int)(sizeof(DARK_REIGN_UI_PRODUCTS) / sizeof(DARK_REIGN_UI_PRODUCTS[0])),
    .categories = DARK_REIGN_UI_CATEGORIES,
    .category_count = (int)(sizeof(DARK_REIGN_UI_CATEGORIES) / sizeof(DARK_REIGN_UI_CATEGORIES[0])),
    .actions = DARK_REIGN_UI_ACTIONS,
    .action_count = (int)(sizeof(DARK_REIGN_UI_ACTIONS) / sizeof(DARK_REIGN_UI_ACTIONS[0])),
    .path_actions = DARK_REIGN_PATH_ACTIONS,
    .path_action_count = sizeof(DARK_REIGN_PATH_ACTIONS) / sizeof(*DARK_REIGN_PATH_ACTIONS),
    .path_list = {555,257,80,78},
    .path_row_height = 12,
};

const uidefinition_t *const gameui = &DARK_REIGN_UI;

/* One active status bar, following Doom's ST_Init/Start/Stop ownership. */
static sb_state_t bar;
typedef struct { SDL_Texture *texture; irect_t glyphs[256]; } dr_font_t;
static dr_font_t fonts[4];

static irect_t scaled(const app_t *app, irect_t r) {
    return (irect_t){r.x * app->win.w / 640, r.y * app->win.h / 480,
                     r.w * app->win.w / 640, r.h * app->win.h / 480};
}

/* 0048f990: cell coordinates, connecting lines and centered 3x3 markers. */
static void draw_path(app_t *app, irect_t radar, const waypoints_t *path, int line) {
    if (!bar.product_icons) return;
    const uint32_t *palette = bar.product_icons[0].palette;
    for (int i = 0; i < path->count; ++i) {
        ivec2_t point = ivec2_add((ivec2_t){radar.x,radar.y}, path->points[i]);
        if (i) {
            ivec2_t previous = ivec2_add((ivec2_t){radar.x,radar.y}, path->points[i-1]);
            uint32_t color = palette[line];
            SDL_SetRenderDrawColor(app->renderer, color >> 16, color >> 8, color, 255);
            SDL_RenderDrawLine(app->renderer, previous.x*app->win.w/640, previous.y*app->win.h/480,
                              point.x*app->win.w/640, point.y*app->win.h/480);
        }
        uint32_t color = palette[0x8a];
        SDL_SetRenderDrawColor(app->renderer, color >> 16, color >> 8, color, 255);
        irect_t marker = scaled(app, (irect_t){point.x-1,point.y-1,3,3});
        SDL_RenderFillRect(app->renderer, &marker);
    }
}

static bool load_font(app_t *app, const char *root, const char *name, int translation, dr_font_t *font) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/graphics/INTFACE/IGI/%s", root, name);
    SDL_Surface *surface = W_LoadImage(path);
    if (!surface) return false;
    const uint8_t *pixels = surface->pixels;
    int ch = 0, start = 1;
    for (int x = 1; x < surface->w && ch < 256; ++x) {
        if (pixels[x] != pixels[0]) continue;
        font->glyphs[ch++] = (irect_t){start, 1, x - start, surface->h - 1};
        start = x + 1;
    }
    M_PathJoin(path, sizeof(path), root, "graphics/INTFACE/IGI/TOPBITS.BMP");
    SDL_Surface *chrome = W_LoadImage(path);
    if (!chrome || !chrome->format->palette) {
        SDL_FreeSurface(chrome); SDL_FreeSurface(surface); return false;
    }
    SDL_SetPaletteColors(surface->format->palette, chrome->format->palette->colors,
                         0, chrome->format->palette->ncolors);
    /* 00477e00: native normal/header tables add 2*8/7*8 to indices 32..41. */
    SDL_SetPaletteColors(surface->format->palette, chrome->format->palette->colors + 32 + translation*8,
                         32, 10);
    SDL_FreeSurface(chrome);
    SDL_SetColorKey(surface, SDL_TRUE, 0);
    font->texture = SDL_CreateTextureFromSurface(app->renderer, surface);
    SDL_FreeSurface(surface);
    return font->texture && font->glyphs['0'].w > 0;
}

irect_t DR_MinimapRect(const level_t *map) {
    int w = map->width < 130 ? map->width : 130;
    int h = map->height < 127 ? map->height : 127;
    return (irect_t){453 + (130-w)/2, 348 + (127-h)/2, w, h};
}

static void draw_minimap(app_t *app, const level_t *map, mobj_t *const *units, int count) {
    irect_t area = DR_MinimapRect(map), rect = scaled(app, area);
    SDL_SetRenderDrawColor(app->renderer, 0,0,0,255);
    SDL_RenderFillRect(app->renderer, &rect);
    SDL_RenderSetClipRect(app->renderer, &rect);
    for (int i = 0; i < count; ++i) {
        const mobj_t *u = units[i];
        if (u->remove || u->hp <= 0 || !P_VisibleToPlayer(u)) continue;
        ivec2_t cell = {u->core.position.x >> FIXED_FRAC_BITS, u->core.position.y >> FIXED_FRAC_BITS};
        SDL_SetRenderDrawColor(app->renderer, u->owner == consoleplayer ? 230 : 200,
                              u->owner == consoleplayer ? 160 : 40, 40,255);
        irect_t dot = scaled(app, (irect_t){area.x + cell.x, area.y + cell.y, 1,1});
        SDL_RenderFillRect(app->renderer, &dot);
        if (P_MobjIsSelected(u) && u->owner == consoleplayer)
            draw_path(app, area, &u->waypoints, 0x16);
    }
    if (bar.page == DR_PAGE_PATHS) draw_path(app, area, &bar.path, 0x18);
    irect_t view = scaled(app, (irect_t){area.x - (int)(app->cam.x / app->cell.w),
        area.y + (int)((32*app->win.h/480 - app->cam.y) / app->cell.h),
        G_WorldViewportWidth(app) / app->cell.w, (app->win.h - 32*app->win.h/480) / app->cell.h});
    SDL_SetRenderDrawColor(app->renderer, 215,215,205,255);
    SDL_RenderDrawRect(app->renderer, &view);
    SDL_RenderSetClipRect(app->renderer, NULL);
}

void *G_InitCustomUI(app_t *app, const char *root) {
    if (!SB_Init(&bar, app->renderer, root, gameui) || !load_font(app, root, "FONT16.PCX", 2, &fonts[0]) ||
        !load_font(app, root, "FONT12T.PCX", 2, &fonts[1]) ||
        !load_font(app, root, "FONT12W.PCX", 2, &fonts[2]) ||
        !load_font(app, root, "FONT12T.PCX", 7, &fonts[3])) {
        G_ShutdownCustomUI(&bar);
        return NULL;
    }
    bar.production_category = 0;
    char path[1024];
    M_PathJoin(path, sizeof(path), root, "graphics/INTFACE/IGI/BUISOBOX.BMP");
    SDL_Surface *surface = W_LoadImage(path);
    if (!surface) { G_ShutdownCustomUI(&bar); return NULL; }
    SDL_SetColorKey(surface, SDL_TRUE, 0);
    SDL_DestroyTexture(bar.textures[8]);
    bar.textures[8] = SDL_CreateTextureFromSurface(app->renderer, surface);
    SDL_FreeSurface(surface);
    if (!bar.textures[8]) { G_ShutdownCustomUI(&bar); return NULL; }
    return &bar;
}

bool G_CustomUIResponder(void *ui, app_t *app, level_t *map,
                         mobj_t *const *units, int count, const SDL_Event *event) {
    (void)map; (void)units; (void)count;
    return ui && DR_PaletteResponder(ui, (app_t *)app, event);
}

void G_CustomUITicker(void *ui) {
    if (ui) SB_Ticker(ui);
}

void G_CustomUIDrawer(void *ui, app_t *app, const level_t *map,
                      mobj_t *const *units, int count,
                      const spritecache_t *sprites, const hudtext_t *hud) {
    (void)hud;
    if (!ui) return;
    bar.radar_visible = false;
    SB_Drawer(ui, app, map, units, count, sprites, false, true);
    bar.radar_visible = true;
    draw_minimap(app, map, units, count);
    irect_t left = scaled(app, (irect_t){0,0,6,32});
    irect_t right = scaled(app, (irect_t){441,0,7,32});
    SDL_RenderCopy(app->renderer, bar.textures[1], &(irect_t){0,0,6,32}, &left);
    SDL_RenderCopy(app->renderer, bar.textures[1], &(irect_t){147,0,7,32}, &right);
    char value[16];
    snprintf(value, sizeof(value), "%09d", map->player_resources[consoleplayer][0]);
    for (int i = 0; i < 8 && value[i] == '0'; ++i) value[i] = ':';
    int x = 178;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        irect_t src = fonts[0].glyphs[*p];
        irect_t dst = scaled(app, (irect_t){x,6,src.w,src.h});
        SDL_RenderCopy(app->renderer, fonts[0].texture, &src, &dst);
        x += src.w;
    }
    DR_PaletteDrawer(ui, app);
}

void G_ShutdownCustomUI(void *ui) {
    if (!ui) return;
    for (unsigned i = 0; i < sizeof(fonts)/sizeof(*fonts); ++i) {
        SDL_DestroyTexture(fonts[i].texture);
        memset(&fonts[i], 0, sizeof(fonts[i]));
    }
    SB_Shutdown(ui);
}

static void draw_text(const app_t *app, const dr_font_t *font,
                      ivec2_t point, const char *text, int width) {
    int x = point.x;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        irect_t src = font->glyphs[*p];
        if (x + src.w > point.x + width) break;
        irect_t dst = scaled(app, (irect_t){x,point.y,src.w,src.h});
        SDL_RenderCopy(app->renderer, font->texture, &src, &dst);
        x += src.w;
    }
}

void DR_DrawText(const app_t *app, ivec2_t point, const char *text, int width) {
    draw_text(app, &fonts[1], point, text, width);
}

void DR_DrawHeader(const app_t *app, ivec2_t point, const char *text, int width) {
    draw_text(app, &fonts[3], point, text, width);
}

void DR_DrawCaption(const app_t *app, ivec2_t anchor, const char *text, bool centered) {
    const dr_font_t *font = &fonts[2];
    int width = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) width += font->glyphs[*p].w;
    ivec2_t offset = {centered ? -width/2 : 0, -font->glyphs['A'].h - 2};
    draw_text(app,font,ivec2_add(anchor,offset),text,width);
}
