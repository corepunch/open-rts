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
};

/* Retail MFDBTNS are pages, not infantry/vehicle categories. */
static const uicategory_t DARK_REIGN_UI_CATEGORIES[] = {
    { "BUILD", {448, 0, 64, 32}, 3, {0, 0, 64, 32} },
};
static const uiaction_t DARK_REIGN_UI_ACTIONS[] = {
    { "COMMS", UI_UNAVAILABLE, {512, 0, 64, 32}, 3, {64, 0, 64, 32}, 0 },
    { "MENU", UI_OPTIONS, {576, 0, 64, 32}, 3, {128, 0, 64, 32}, 0 },
    { "ORDERS", UI_UNAVAILABLE, {448, 32, 64, 32}, 3, {0, 32, 64, 32}, 0 },
    { "PATHS", UI_UNAVAILABLE, {512, 32, 64, 32}, 3, {64, 32, 64, 32}, 0 },
    { "SPECIAL", UI_UNAVAILABLE, {576, 32, 64, 32}, 3, {128, 32, 64, 32}, 0 },
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
};

const uidefinition_t *const gameui = &DARK_REIGN_UI;

/* One active status bar, following Doom's ST_Init/Start/Stop ownership. */
static sb_state_t bar;
typedef struct { SDL_Texture *texture; irect_t glyphs[256]; } dr_font_t;
static dr_font_t fonts[2];

static irect_t scaled(const app_t *app, irect_t r) {
    return (irect_t){r.x * app->win.w / 640, r.y * app->win.h / 480,
                     r.w * app->win.w / 640, r.h * app->win.h / 480};
}

static bool load_font(app_t *app, const char *root, const char *name, dr_font_t *font) {
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
    /* 00477e00 translates indices 32..41; table 0 at 005cc9c0 adds 2*8. */
    SDL_SetPaletteColors(surface->format->palette, chrome->format->palette->colors + 48,
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
    }
    irect_t view = scaled(app, (irect_t){area.x - (int)(app->cam.x / app->cell.w),
        area.y + (int)((32*app->win.h/480 - app->cam.y) / app->cell.h),
        G_WorldViewportWidth(app) / app->cell.w, (app->win.h - 32*app->win.h/480) / app->cell.h});
    SDL_SetRenderDrawColor(app->renderer, 215,215,205,255);
    SDL_RenderDrawRect(app->renderer, &view);
    SDL_RenderSetClipRect(app->renderer, NULL);
}

void *G_InitCustomUI(app_t *app, const char *root) {
    if (!SB_Init(&bar, app->renderer, root, gameui) || !load_font(app, root, "FONT16.PCX", &fonts[0]) ||
        !load_font(app, root, "FONT12T.PCX", &fonts[1])) {
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

bool G_CustomUIResponder(void *ui, const app_t *app, level_t *map,
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
    for (int i = 0; i < 2; ++i) {
        SDL_DestroyTexture(fonts[i].texture);
        memset(&fonts[i], 0, sizeof(fonts[i]));
    }
    SB_Shutdown(ui);
}

void DR_DrawText(const app_t *app, ivec2_t point, const char *text, int width) {
    const dr_font_t *font = &fonts[1];
    int x = point.x;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        irect_t src = font->glyphs[*p];
        if (x + src.w > point.x + width) break;
        irect_t dst = scaled(app, (irect_t){x,point.y,src.w,src.h});
        SDL_RenderCopy(app->renderer, font->texture, &src, &dst);
        x += src.w;
    }
}
