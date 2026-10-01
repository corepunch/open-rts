#include "engine.h"
#include "info.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BACKGROUND 0xff46505au

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static void draw_pixels(app_t *app, const spritesheet_t *sheet, const gameinfo_t *game,
                         mobj_t *unit, uint32_t pixels[2]) {
    V_BeginFrame(BACKGROUND);
    const level_t map = {0};
    R_RenderPlayerView(app, &map, NULL, &unit, 1, sheet, NULL, game, 0);
    V_ReadPixels(pixels, 2 * sizeof(*pixels));
}

int main(void) {
    V_AllocScreen(2, 1);
    CHECK(screens[0].pixels);
    app_t app = { .win = { 2, 1 } };
    spritesheet_t sheet = {0};
    CHECK(R_AllocSpriteCells(&sheet, 1));
    sheet.cells[0].rect = (irect_t){ 0, 0, 2, 1 };
    const uint32_t colors[2] = { 0xff123456, 0xffabcdef };
    const uint32_t remapped[2] = { 0xff804020, 0xff204080 };
    sheet.lumps[0].indices = malloc(2);
    sheet.palette_maps = calloc(1, sizeof(*sheet.palette_maps));
    CHECK(sheet.lumps[0].indices && sheet.palette_maps);
    sheet.lumps[0].indices[0] = 1; sheet.lumps[0].indices[1] = 2;
    sheet.source_palette[1] = colors[0]; sheet.source_palette[2] = colors[1];
    sheet.source_palette[3] = remapped[0]; sheet.source_palette[4] = remapped[1];
    sheet.palette_map_count = 1;
    sheet.palette_maps[0].id = 1;
    sheet.palette_maps[0].indices[1] = 3; sheet.palette_maps[0].indices[2] = 4;
    /* The screen shows only palette entries, so give it every colour a case
     * below must land on. Blends pick the entry nearest their truecolour result. */
    const uint32_t dimmed[2] = { 0xff102040, 0xff402010 };
    const uint32_t additive[2] = { 0xff62857a, 0xffb98562 };
    const uint32_t dim_additive[2] = { 0xff546a6a, 0xff7f6a5e };
    sheet.source_palette[5] = BACKGROUND;
    sheet.source_palette[6] = dimmed[0]; sheet.source_palette[7] = dimmed[1];
    sheet.source_palette[8] = additive[0]; sheet.source_palette[9] = additive[1];
    sheet.source_palette[10] = dim_additive[0]; sheet.source_palette[11] = dim_additive[1];
    I_SetPalette(sheet.source_palette);
    CHECK(R_InitSpriteDef(&sheet, 1, 1) && R_InstallSpriteLump(&sheet, 0, 0, 0, false));
    spritelayer_t *part = sheet.spritedef.spriteframes[0].directions[0].layers;
    part->offset = (ivec2_t){ 0, 1 };
    part->layer = 1;
    state_t states[2] = { {0}, { .tics = -1 } };
    gameinfo_t game = { .states = states, .state_count = 2,
                       .state_coord_mode = RTS_STATE_COORDS_FIN_TOP_LEFT };
    gameinfo = &game;
    mobj_t unit = { .traits = MF_RENDERABLE };
    unit.core.render_flags = UINT32_MAX;
    CHECK(P_SetMobjState(&unit, 1));
    CHECK(unit.core.render_flags == 0);

    uint32_t pixels[2], baseline[2];
    draw_pixels(&app, &sheet, &game, &unit, baseline);
    CHECK(!memcmp(baseline, colors, sizeof(colors)));
    /* Native layer rendering must ignore every actor-level flag, palette
     * override and intensity; those are not this command's metadata. */
    unit.core.render_flags = UINT32_MAX;
    unit.core.render_remap = 1;
    unit.core.render_intensity = 1;
    draw_pixels(&app, &sheet, &game, &unit, pixels);
    CHECK(!memcmp(pixels, baseline, sizeof(pixels)));

    part->flags = RTS_FRAME_FLIP_X;
    part->remap = 4;
    unit.team = 1;
    draw_pixels(&app, &sheet, &game, &unit, pixels);
    CHECK(pixels[0] == remapped[1] && pixels[1] == remapped[0]);
    part->intensity = 8;
    draw_pixels(&app, &sheet, &game, &unit, pixels);
    CHECK(pixels[0] == dimmed[0] && pixels[1] == dimmed[1]);

    /* FIN rendering modes must not override the object's palette/team. */
    for (int mode = 0; mode < 8; ++mode) {
        part->remap = mode;
        draw_pixels(&app, &sheet, &game, &unit, pixels);
        CHECK(pixels[0] == dimmed[0] && pixels[1] == dimmed[1]);
    }
    part->layer = 3;
    part->intensity = 16;
    draw_pixels(&app, &sheet, &game, &unit, pixels);
    /* Background + flipped/remapped source * old yellow tint * 230/255,
     * nearest-matched into the screen palette. */
    CHECK(pixels[0] == additive[0] && pixels[1] == additive[1]);
    part->intensity = 8;
    draw_pixels(&app, &sheet, &game, &unit, pixels);
    CHECK(pixels[0] == dim_additive[0] && pixels[1] == dim_additive[1]);
    part->layer = 1;
    draw_pixels(&app, &sheet, &game, &unit, pixels);
    CHECK(pixels[0] == dimmed[0] && pixels[1] == dimmed[1]);

    R_FreeSprite(&sheet);
    V_FreeScreen();
    puts("PASS: sprite layers own flags/intensity; objects own team color; layer 3 is yellow/additive without leaking texture state");
    return 0;
}
