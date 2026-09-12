#include "engine.h"
#include "info.h"
#include "w_spr.h"
#include "dc_facing.h"

#include <assert.h>
#include <stdio.h>

enum { SIZE = 160, PIXELS = SIZE * SIZE };

int main(void) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, SIZE, SIZE, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface *atlas = SDL_CreateRGBSurfaceWithFormat(0, SIZE * 4, SIZE * 4, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface && atlas);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(r_renderer);
    app_t app = { .renderer = r_renderer, .win = { SIZE, SIZE },
                  .cell = { 32, 32 }, .cam = { SIZE / 2, SIZE / 2 } };
    spritesheet_t sheet;
    dc_fin_t fin;
    assert(load_dark_colony_sprite("data/DCOLONY/ANIMATE/BARR.FIN", &sheet, NULL));
    assert(DC_LoadFIN("data/DCOLONY/ANIMATE/BARR.FIN", &fin));
    gameinfo = &game_info;
    level = (level_t){0};
    mobj_t unit = { .type_id = MT_THUNDERBOLT, .traits = MF_RENDERABLE, .team = 7 };
    mobj_t *units[] = { &unit };
    assert(P_SetMobjState(&unit, mobjinfo[MT_THUNDERBOLT].spawnstate));
    assert(sheet.spritedef.spriteframes[unit.core.frame].rotations == 16);
    uint32_t actual[PIXELS], expected[PIXELS];
    bool matched = true;
    for (int suffix = 0; suffix < 16; ++suffix) {
        unit.core.angle = dc_fin_direction_to_angle(suffix);
        /* Native frames 0..15 interleave even STAND and singleton odd MOVE.
         * The later SHUF labels reuse the odd cells at different offsets. */
        const dc_fin_label_t *label = DC_FINLabel(&fin,
            M_va("BARR%s%d", suffix & 1 ? "MOVE" : "STAND", suffix));
        assert(label && label->start == label->end);
        spritedirection_t native = {0};
        assert(DC_FINFrame(&fin, SDL_SwapLE16(label->start), &native));
        const spritelayer_t *part = native.layers;
        assert(part[0].sprite_name[0] && !part[1].sprite_name[0]);
        const spritecell_t *cell = &sheet.cells[part->lump];
        bool flip = part->flags & RTS_FRAME_FLIP_X;
        ivec2_t origin = { SIZE / 2 + part->offset.x + (flip ? 0 : cell->displacement.x),
                           SIZE / 2 + part->offset.y - cell->rect.h };
        for (int i = 0; i < PIXELS; ++i) expected[i] = 0xff46505a;
        for (int y = 0; y < cell->rect.h; ++y)
            for (int x = 0; x < cell->rect.w; ++x) {
                int index = sheet.lumps[part->lump].indices[y * cell->rect.w +
                    (flip ? cell->rect.w - 1 - x : x)];
                ivec2_t dst = ivec2_add(origin, (ivec2_t){x, y});
                assert(dst.x >= 0 && dst.y >= 0 && dst.x < SIZE && dst.y < SIZE);
                if (index) expected[dst.y * SIZE + dst.x] = sheet.source_palette[index];
            }
        SDL_SetRenderDrawColor(r_renderer, 70, 80, 90, 255);
        SDL_RenderClear(r_renderer);
        R_RenderPlayerView(&app, &level, NULL, units, 1, &sheet, NULL, &game_info, 0);
        assert(!SDL_RenderReadPixels(r_renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                    actual, SIZE * sizeof(*actual)));
        if (memcmp(actual, expected, sizeof(actual))) {
            fprintf(stderr, "Barrager facing %d differs from %s at (%d,%d)\n",
                    suffix, M_va("BARR%s%d", suffix & 1 ? "MOVE" : "STAND", suffix),
                    origin.x, origin.y);
            matched = false;
        }
        SDL_SetRenderDrawColor(r_renderer, 100, 120, 130, 255);
        SDL_RenderDrawLine(r_renderer, SIZE / 2 - 4, SIZE / 2, SIZE / 2 + 4, SIZE / 2);
        SDL_RenderPresent(r_renderer);
        irect_t tile = { (suffix % 4) * SIZE, (suffix / 4) * SIZE, SIZE, SIZE };
        assert(!SDL_BlitSurface(surface, NULL, atlas, &tile));
        free(native.layers);
    }
    assert(!SDL_SaveBMP(atlas, "/private/tmp/barrager-turning.bmp"));
    DC_FreeFIN(&fin);
    R_FreeSprite(&sheet);
    R_FreeSpriteBuffer();
    SDL_DestroyRenderer(r_renderer);
    r_renderer = NULL;
    SDL_FreeSurface(atlas);
    SDL_FreeSurface(surface);
    assert(matched);
    puts("PASS: all sixteen Barrager turning poses retain native placement and pixels");
    return 0;
}
