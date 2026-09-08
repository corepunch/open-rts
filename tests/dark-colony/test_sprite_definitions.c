#include "engine.h"
#include "w_spr.h"

#include <SDL.h>
#include <stdio.h>

int main(void) {
    dc_fin_t fin = { 0 };
    if (!W_LoadFin("data/DCOLONY/ANIMATE/TRSC.FIN", &fin) ||
        fin.header->default_ticks != 29 || fin.header->frame_count != 472 ||
        fin.header->label_count != 77 || fin.header->dependency_count != 5 ||
        fin.layer_count != 596 || fin.frames[91].part_count != 3) {
        fprintf(stderr, "FAIL: FIN header %u/%u/%u/%u layers=%d frame91=%u\n",
            fin.header ? fin.header->default_ticks : 0,
            fin.header ? fin.header->frame_count : 0,
            fin.header ? fin.header->label_count : 0,
            fin.header ? fin.header->dependency_count : 0,
            fin.layer_count, fin.frames ? fin.frames[91].part_count : 0);
        W_FreeFin(&fin);
        return 1;
    }
    W_FreeFin(&fin);

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32,
                                                          SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *renderer = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
    if (!renderer) {
        fprintf(stderr, "FAIL: create software renderer: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return 1;
    }

    spritesheet_t sprite;
    if (!load_dark_colony_sprite(renderer, "data/DCOLONY/ANIMATE/TRSC.FIN",
                                 &sprite, NULL)) {
        fprintf(stderr, "FAIL: load Trooper sprite definition\n");
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(surface);
        return 1;
    }

    static const int expected_lumps[8] = { 16, 23, 22, 21, 20, 19, 18, 17 };
    static const uint8_t expected_flips[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    const spriteframe_t *run = &sprite.spritedef.spriteframes[16];
    bool valid = sprite.spritedef.rotations == 8;
    for (int rotation = 0; rotation < 8; ++rotation) {
        const spritelayer_t *part = run->directions[rotation].layers;
        if (!part || part->lump != expected_lumps[rotation] ||
            ((part->flags & RTS_FRAME_FLIP_X) != 0) != expected_flips[rotation]) valid = false;
    }
    if (!valid) {
        fprintf(stderr, "FAIL: Trooper run state frame resolves FIN rotations:");
        for (int rotation = 0; rotation < 8; ++rotation) {
            const spritelayer_t *part = run->directions[rotation].layers;
            fprintf(stderr, " %d/%u", part ? part->lump : -1,
                    part ? (unsigned)((part->flags & RTS_FRAME_FLIP_X) != 0) : 0);
        }
        fputc('\n', stderr);
    }

    const spritelayer_t *fire_parts =
        sprite.spritedef.spriteframes[91].directions[2].layers;
    if (!fire_parts ||
        fire_parts[0].sprite[0] != '.' || fire_parts[0].sprite[1] != '\0' ||
        fire_parts[0].lump != 94 || fire_parts[0].x != -159 ||
        fire_parts[0].y != 0 || fire_parts[0].layer != 1 ||
        fire_parts[1].sprite[0] != '.' || fire_parts[1].sprite[1] != '\0' ||
        fire_parts[1].lump != 171 || fire_parts[1].x != -64 ||
        fire_parts[1].y != -28 || fire_parts[1].layer != 5 ||
        (fire_parts[1].flags & RTS_FRAME_FLIP_X) == 0 ||
        fire_parts[2].sprite[0] != '.' || fire_parts[2].sprite[1] != '\0' ||
        fire_parts[2].lump != 176 || fire_parts[2].x != -43 ||
        fire_parts[2].y != -24 || fire_parts[2].layer != 5 ||
        (fire_parts[2].flags & RTS_FRAME_FLIP_X) == 0 ||
        strncasecmp(fire_parts[3].sprite, "BLAZ", 4) != 0 ||
        fire_parts[3].lump != 0 || fire_parts[3].x != -56 ||
        fire_parts[3].y != 4 || fire_parts[3].layer != 3 ||
        fire_parts[4].sprite[0] != '\0') {
        fprintf(stderr, "FAIL: Trooper FIN frame preserves ordered multipart commands\n");
        valid = false;
    }

    R_FreeSprite(&sprite);
    if (!load_dark_colony_sprite(renderer, "data/DCOLONY/SPRITES/EXPL.SPR",
                                 &sprite, NULL)) {
        fprintf(stderr, "FAIL: load Exploiter sprite definition\n");
        valid = false;
    } else {
        const spriteframe_t *stand = &sprite.spritedef.spriteframes[0];
        const spritelayer_t *part = stand->directions[1].layers;
        if (sprite.spritedef.rotations != 16 ||
            !part || part->lump != 1 || (part->flags & RTS_FRAME_FLIP_X) == 0) {
            fprintf(stderr, "FAIL: Exploiter facing 15 preserves native frame and flip: %d/%u\n",
                part ? part->lump : -1,
                part ? (unsigned)((part->flags & RTS_FRAME_FLIP_X) != 0) : 0);
            valid = false;
        }
        R_FreeSprite(&sprite);
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    return valid ? 0 : 1;
}