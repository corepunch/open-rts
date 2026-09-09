#include "engine.h"
#include "w_spr.h"

#include <SDL.h>
#include <stdio.h>

int main(void) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32,
                                                          SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *renderer = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
    r_renderer = renderer;
    if (!renderer) {
        fprintf(stderr, "FAIL: create software renderer: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return 1;
    }

    spritesheet_t sprite;
    if (!load_dark_colony_sprite("data/DCOLONY/ANIMATE/TRSC.FIN",
                                 &sprite, NULL)) {
        fprintf(stderr, "FAIL: load Trooper sprite definition\n");
        r_renderer = NULL;
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(surface);
        return 1;
    }

    static const int expected_lumps[8] = { 20, 21, 22, 23, 16, 17, 18, 19 };
    static const uint8_t expected_flips[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    const spriteframe_t *run = &sprite.spritedef.spriteframes[16];
    bool valid = run->rotations == 8 && sprite.spritedef.numframes == 472;
    /* A raw cell on the same sheet remains nondirectional. */
    valid &= sprite.spritedef.spriteframes[15].rotations == 1;
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
        strcmp(fire_parts[0].sprite_name, ".") != 0 ||
        fire_parts[0].lump != 94 || fire_parts[0].offset.x != -159 ||
        fire_parts[0].offset.y != 0 || fire_parts[0].layer != 1 ||
        strcmp(fire_parts[1].sprite_name, ".") != 0 ||
        fire_parts[1].lump != 171 || fire_parts[1].offset.x != -64 ||
        fire_parts[1].offset.y != -28 || fire_parts[1].layer != 5 ||
        (fire_parts[1].flags & RTS_FRAME_FLIP_X) == 0 ||
        strcmp(fire_parts[2].sprite_name, ".") != 0 ||
        fire_parts[2].lump != 176 || fire_parts[2].offset.x != -43 ||
        fire_parts[2].offset.y != -24 || fire_parts[2].layer != 5 ||
        (fire_parts[2].flags & RTS_FRAME_FLIP_X) == 0 ||
        strcmp(fire_parts[3].sprite_name, "BLAZ") != 0 ||
        fire_parts[3].lump != 0 || fire_parts[3].offset.x != -56 ||
        fire_parts[3].offset.y != 4 || fire_parts[3].layer != 3 ||
        fire_parts[4].sprite_name[0] != '\0') {
        fprintf(stderr, "FAIL: Trooper FIN frame preserves ordered multipart commands\n");
        valid = false;
    }

    R_FreeSprite(&sprite);
    if (!load_dark_colony_sprite("data/DCOLONY/SPRITES/EXPL.SPR",
                                 &sprite, NULL)) {
        fprintf(stderr, "FAIL: load Exploiter sprite definition\n");
        valid = false;
    } else {
        const spriteframe_t *stand = &sprite.spritedef.spriteframes[0];
        /* All sixteen stationary poses remain available while turning. */
        static const int stand_lumps[16] = { 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6, 7 };
        if (sprite.spritedef.numframes != 232 || stand->rotations != 16) {
            fprintf(stderr, "FAIL: Exploiter standing uses sixteen turn poses\n");
            valid = false;
        }
        for (int r = 0; r < 16; ++r) {
            const spritelayer_t *part = stand->directions[r].layers;
            if (!part || part->lump != stand_lumps[r] ||
                ((part->flags & RTS_FRAME_FLIP_X) != 0) != (r > 0 && r < 8)) {
                fprintf(stderr, "FAIL: Exploiter STAND rotation %d: %d/%u\n", r,
                    part ? part->lump : -1, part ? part->flags : 0);
                valid = false;
            }
        }
        /* Single-frame intermediate MOVE poses are not travel cycles. */
        static const int move_lumps[2][8] = {
            { 8, 6, 4, 2, 0, 2, 4, 6 },
            { 13, 12, 11, 10, 9, 10, 11, 12 },
        };
        for (int f = 0; f < 2; ++f) {
            const spriteframe_t *move = &sprite.spritedef.spriteframes[16 + f];
            if (move->rotations != 8) valid = false;
            for (int r = 0; r < 8; ++r) {
                const spritelayer_t *part = move->directions[r].layers;
                if (!part || part->lump != move_lumps[f][r]) {
                    fprintf(stderr, "FAIL: Exploiter MOVE phase %d rotation %d: %d\n",
                        f, r, part ? part->lump : -1);
                    valid = false;
                }
            }
        }
        R_FreeSprite(&sprite);
    }
    spritesheet_t mixed = {0};
    if (!R_AllocSpriteCells(&mixed, 32) || !R_InitSpriteDef(&mixed, 2, 1)) {
        valid = false;
    } else {
        mixed.spritedef.spriteframes[1].rotations = 32;
        valid &= R_InstallSpriteLump(&mixed, 0, 0, 0, false);
        valid &= !R_InstallSpriteLump(&mixed, 0, 1, 0, false);
        for (int r = 0; r < 32; ++r)
            valid &= R_InstallSpriteLump(&mixed, 1, r, r, false);
        valid &= !R_InstallSpriteLump(&mixed, 1, 32, 0, false);
    }
    R_FreeSprite(&mixed);
    r_renderer = NULL;
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    return valid ? 0 : 1;
}
