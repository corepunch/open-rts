#include "engine.h"
#include "w_spr.h"

#include <SDL.h>
#include <stdio.h>

int main(void) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32,
                                                          SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *renderer = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
    if (!renderer) {
        fprintf(stderr, "FAIL: create software renderer: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return 1;
    }

    spritesheet_t sprite;
    if (!load_dark_colony_sprite(renderer, "data/DCOLONY/SPRITES/TRSC.SPR",
                                 &sprite, NULL)) {
        fprintf(stderr, "FAIL: load Trooper sprite definition\n");
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(surface);
        return 1;
    }

    static const int expected_lumps[8] = { 16, 23, 22, 21, 20, 19, 18, 17 };
    static const uint8_t expected_flips[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    const spriteframe_t *run = &sprite.spritedef.spriteframes[16];
    bool valid = sprite.spritedef.rotations == 8 && run->rotate;
    for (int rotation = 0; rotation < 8; ++rotation) {
        if (run->lump[rotation] != expected_lumps[rotation] ||
            run->flip[rotation] != expected_flips[rotation]) valid = false;
    }
    if (!valid) {
        fprintf(stderr, "FAIL: Trooper run state frame resolves FIN rotations:");
        for (int rotation = 0; rotation < 8; ++rotation)
            fprintf(stderr, " %d/%u", run->lump[rotation], run->flip[rotation]);
        fputc('\n', stderr);
    }

    R_FreeSprite(&sprite);
    if (!load_dark_colony_sprite(renderer, "data/DCOLONY/SPRITES/EXPL.SPR",
                                 &sprite, NULL)) {
        fprintf(stderr, "FAIL: load Exploiter sprite definition\n");
        valid = false;
    } else {
        const spriteframe_t *stand = &sprite.spritedef.spriteframes[0];
        if (sprite.spritedef.rotations != 16 || !stand->rotate ||
            stand->lump[1] != 1 || !stand->flip[1]) {
            fprintf(stderr, "FAIL: Exploiter facing 15 preserves native frame and flip: %d/%u\n",
                stand->lump[1], stand->flip[1]);
            valid = false;
        }
        R_FreeSprite(&sprite);
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    return valid ? 0 : 1;
}