#include "t_local.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II selection", #c)

enum { W = 256, BACK = 1, BODY = 2 };

static int footprint(mobj_t *unit, app_t *app, irect_t expected) {
    memset(screens[0].pixels, BACK, W * W);
    R_DrawThings(app, &unit, 1, NULL, NULL, gameinfo, 0);
    uint8_t green = V_NearestIndex(0xff00fc00u);
    for (int y = 0; y < W; ++y)
        for (int x = 0; x < W; ++x) {
            bool edge = irect_contains(expected, (ivec2_t){x, y}) &&
                (x == expected.x || x == expected.x + expected.w - 1 ||
                 y == expected.y || y == expected.y + expected.h - 1);
            CHECK(screens[0].pixels[y * W + x] == (edge ? green : BACK));
        }
    return 0;
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    level.width = level.height = 16;
    app_t app = {.win = {W, W}, .cell = {32, 32}};
    V_AllocScreen(W, W);
    uint32_t palette[256] = {0};
    palette[BACK] = 0xff404040u;
    palette[BODY] = 0xfffc0000u;
    palette[3] = 0xff00fc00u;
    I_SetPalette(palette);
    /* Independently authored expected extents: infantry, ship, barracks,
     * town hall. Sprite box sizes (31/70/95/126) do not determine these. */
    static const struct { int type; irect_t rect; } cases[] = {
        {1, {112, 112, 32, 32}}, {39, {96, 96, 64, 64}},
        {61, {80, 80, 96, 96}}, {75, {64, 64, 128, 128}},
    };
    mobj_t *units[4];
    for (int i = 0; i < 4; ++i) {
        units[i] = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){4, 4}, 0), cases[i].type);
        CHECK(units[i]);
        P_MobjSetSelected(units[i], true);
        RTS_RUN(footprint(units[i], &app, cases[i].rect));
    }
    app.cam = (fvec2_t){3.25f, -5.75f};
    app.cell = (isize2_t){40, 24};
    RTS_RUN(footprint(units[0], &app, (irect_t){143, 78, 40, 24}));
    app.cam = (fvec2_t){0};
    app.cell = (isize2_t){32, 32};
    /* A synthetic wide sprite covers the entire footprint, so every
     * selection pixel must be occluded. Exercise both world draw paths
     * and put the selected unit last to detect per-unit mark ordering. */
    spritesheet_t sprite = {0};
    CHECK(R_AllocSpriteCells(&sprite, 1));
    sprite.frame_size = (isize2_t){160, 160};
    sprite.cells[0].rect = sprite.cells[0].bounds = (irect_t){0, 0, 160, 160};
    sprite.cells[0].ground_point = (ivec2_t){80, 80};
    sprite.lumps[0].indices = malloc(160 * 160);
    CHECK(sprite.lumps[0].indices);
    memset(sprite.lumps[0].indices, BODY, 160 * 160);
    memcpy(sprite.source_palette, palette, sizeof(palette));
    CHECK(R_InitSpriteDef(&sprite, 1, 1) && R_InstallSpriteLump(&sprite, 0, 0, 0, false));
    mobj_t *drawn[] = {units[1], units[0]};
    P_MobjSetSelected(units[1], false);
    spritesheet_t transparent = sprite;
    spritelump_t blank = {.indices = calloc(160 * 160, 1)};
    CHECK(blank.indices);
    transparent.lumps = &blank;
    const spritesheet_t *bound[W2_SPRITE_COUNT] = {0};
    bound[units[1]->core.sprite_id] = &sprite;
    bound[units[0]->core.sprite_id] = &transparent;
    spritecache_t cache = {.sprites = bound, .numsprites = W2_SPRITE_COUNT};
    for (int path = 0; path < 4; ++path) {
        memset(screens[0].pixels, BACK, W * W);
        const spritecache_t *images = path >= 2 ? &cache : NULL;
        if (path & 1) R_RenderPlayerView(&app, &level, NULL, drawn, 2, &sprite, images, gameinfo, 0);
        else R_DrawThings(&app, drawn, 2, &sprite, images, gameinfo, 0);
        for (int y = 0; y < W; ++y)
            for (int x = 0; x < W; ++x)
                CHECK(screens[0].pixels[y * W + x] ==
                      (irect_contains((irect_t){48, 48, 160, 160}, (ivec2_t){x, y}) ? BODY : BACK));
    }
    /* Hidden units cannot leave a selection mark behind. */
    P_MobjSetHidden(units[0], true);
    RTS_RUN(footprint(units[0], &app, (irect_t){0}));
    free(blank.indices);
    R_FreeSprite(&sprite);
    V_FreeScreen();
    P_FreeLevel(&level);
    puts("PASS: tile footprints, camera/cell scaling, hidden units and selection beneath all sprites");
    return 0;
}
