#include "engine.h"
#include "info.h"
#include "dc_facing.h"
#include "w_spr.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

int main(void) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(r_renderer);
    dc_fin_t fin;
    spritesheet_t sheet;
    CHECK(DC_LoadFIN("data/DCOLONY/ANIMATE/REAP.FIN", &fin));
    CHECK(load_dark_colony_sprite("data/DCOLONY/ANIMATE/REAP.FIN", &sheet, NULL));
    /* Freeze the preexisting direction choice, including ties. This is an
     * engine compatibility check, not a claim about retail selection. */
    static const int suffixes[16] = { 2,14,14,14,14,10,10,10,10,6,6,6,6,2,2,2 };
    for (int direction = 0; direction < 16; ++direction) {
        char name[32];
        snprintf(name, sizeof(name), "REAPDIEA%d", suffixes[direction]);
        const dc_fin_label_t *label = DC_FINLabel(&fin, name);
        CHECK(label);
        int start = SDL_SwapLE16(label->start), end = SDL_SwapLE16(label->end);
        level_t map = {0};
        effect_t effects[2] = {0};
        mobj_t unit = { .type_id = MT_REAPER };
        P_ApplyActorTypeDefaults(&unit, actor_type_by_id(MT_REAPER));
        unit.core.angle = dc_direction_to_angle(direction);
        unit.core.position = fixed3_from_fvec2((fvec2_t){ 4, 5 }, 0);
        unit.hp = 0;
        statecontext_t ctx = { .map = &map, .game_info = &game_info,
            .effects = effects, .max_effects = 2 };
        CHECK(P_SetMobjState(&ctx, &unit, dc_mobjinfo[MT_REAPER].deathstate));
        CHECK(!(unit.traits & (MF_SELECTABLE | MF_MOBILE | MF_ATTACK)));
        int explosion_frames = 0, elapsed = 0;
        for (int f = start; f <= end; ++f) {
            CHECK(!unit.remove && unit.core.sprite_id == SPR_REAP);
            CHECK(unit.core.frame == sheet.numlumps + f);
            CHECK(unit.core.render_flags == 0);
            const spriteframe_t *frame = &sheet.spritedef.spriteframes[unit.core.frame];
            CHECK(frame->rotations == 1);
            const spritelayer_t *actual = frame->directions[0].layers;
            spritedirection_t expected = {0};
            CHECK(DC_FINFrame(&fin, f, &expected));
            CHECK(frame->directions[0].ticks == expected.ticks);
            int p = 0;
            for (; expected.layers[p].sprite_name[0]; ++p) {
                const spritelayer_t *part = &expected.layers[p];
                CHECK(!strcasecmp(actual[p].sprite_name,
                      !strcasecmp(part->sprite_name, "reap") ? "." : part->sprite_name));
                CHECK(actual[p].lump == part->lump);
                CHECK(ivec2_equal(actual[p].offset, part->offset));
                CHECK(actual[p].flags == part->flags && actual[p].layer == part->layer);
                CHECK(actual[p].remap == part->remap && actual[p].intensity == part->intensity);
                if (!strcasecmp(part->sprite_name, "blam")) ++explosion_frames;
            }
            CHECK(!actual[p].sprite_name[0]);
            free(expected.layers);
            int state = unit.core.state_id, tics = unit.core.tics;
            CHECK(tics > 0);
            if (f == start && (suffixes[direction] == 14 || suffixes[direction] == 6))
                CHECK(tics == 30 && explosion_frames == 0);
            for (int t = 0; t < tics; ++t) {
                CHECK(unit.core.state_id == state);
                P_TickMobjState(&ctx, &unit);
                ++elapsed;
                CHECK(!effects[0].active && !effects[1].active);
            }
        }
        CHECK(unit.remove && map.decoration_count == 1);
        CHECK(map.decorations[0].frame_index == sheet.numlumps + end);
        CHECK(explosion_frames == (suffixes[direction] == 14 ? 18 : suffixes[direction] == 6 ? 17 : 0));
        CHECK(elapsed == (suffixes[direction] == 14 ? 674 : suffixes[direction] == 10 ? 322 :
                          suffixes[direction] == 6 ? 717 : 398));
        free(map.decorations);
    }
    R_FreeSprite(&sheet);
    DC_FreeFIN(&fin);
    SDL_DestroyRenderer(r_renderer);
    r_renderer = NULL;
    SDL_FreeSurface(surface);
    puts("PASS: Reaper death uses complete FIN timelines and leaves native corpse frames");
    return 0;
}
