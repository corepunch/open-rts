#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include "w_spr.h"
#include "info.h"

#include <SDL.h>
#include <stdio.h>

#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static void check_all_fin_frames(void) {
    dc_fin_t fin;
    CHECK(DC_LoadFIN("data/DCOLONY/ANIMATE/TRSC.FIN", &fin));
    /* An unfamiliar name and a prefix unrelated to the SPR must still work. */
    for (int i = 0; i < SDL_SwapLE16(fin.header->label_count); ++i) {
        dc_fin_label_t *label = (dc_fin_label_t *)&fin.labels[i];
        if (!strncmp(label->name, "TRSCMOVE", 8)) {
            int direction = atoi(label->name + 8);
            memset(label->name, 0, sizeof(label->name));
            snprintf(label->name, sizeof(label->name), "WAVE%d", direction);
        }
    }
    char directory[] = "/private/tmp/dc-fin-names-XXXXXX", cwd[1024];
    CHECK(mkdtemp(directory) && getcwd(cwd, sizeof(cwd)));
    CHECK(mkdir(M_va("%s/ANIMATE", directory), 0700) == 0);
    CHECK(mkdir(M_va("%s/SPRITES", directory), 0700) == 0);
    CHECK(symlink(M_va("%s/data/DCOLONY/SPRITES/TRSC.SPR", cwd),
                  M_va("%s/SPRITES/TRSC.SPR", directory)) == 0);
    FILE *file = fopen(M_va("%s/ANIMATE/TRSC.FIN", directory), "wb");
    CHECK(file && fwrite(fin.file.bytes, 1, fin.file.size, file) == fin.file.size);
    CHECK(fclose(file) == 0);
    spritesheet_t sheet;
    CHECK(load_dark_colony_sprite(M_va("%s/ANIMATE/TRSC.FIN", directory), &sheet, NULL));
    CHECK(sheet.spritedef.spriteframes[sheet.numlumps + 16].rotations == 8);
    CHECK(!strcmp(sheet.spritedef.spriteframes[sheet.numlumps + 16].frame_name, "WAVE0"));
    CHECK(sheet.spritedef.spriteframes[sheet.numlumps + 16].directions[0].layers[0].lump == 20);
    int count = SDL_SwapLE16(fin.header->frame_count);
    for (int f = 0; f < count; ++f) {
        const spriteframe_t *frame = &sheet.spritedef.spriteframes[sheet.numlumps + f];
        if (frame->rotations != 1) continue;
        spritedirection_t expected = {0};
        CHECK(DC_FINFrame(&fin, f, &expected));
        CHECK(frame->directions[0].ticks == expected.ticks);
        const spritelayer_t *actual = frame->directions[0].layers;
        int p = 0;
        for (; expected.layers[p].sprite_name[0]; ++p) {
            spritelayer_t *part = &expected.layers[p];
            const char *name = !strcasecmp(part->sprite_name, "TRSC") ? "." : M_Upper(part->sprite_name);
            CHECK(!strcmp(actual[p].sprite_name, name));
            CHECK(actual[p].lump == part->lump && ivec2_equal(actual[p].offset, part->offset));
            CHECK(actual[p].flags == part->flags && actual[p].layer == part->layer);
            CHECK(actual[p].remap == part->remap && actual[p].intensity == part->intensity);
        }
        CHECK(!actual[p].sprite_name[0]);
        free(expected.layers);
    }
    DC_FreeFIN(&fin);
    CHECK(sheet.spritedef.spriteframes[sheet.numlumps + 16].directions[0].layers[0].lump == 20);
    R_FreeSprite(&sheet);
    CHECK(unlink(M_va("%s/ANIMATE/TRSC.FIN", directory)) == 0);
    CHECK(unlink(M_va("%s/SPRITES/TRSC.SPR", directory)) == 0);
    CHECK(rmdir(M_va("%s/ANIMATE", directory)) == 0);
    CHECK(rmdir(M_va("%s/SPRITES", directory)) == 0);
    CHECK(rmdir(directory) == 0);
}

static void check_sprite_registry(void) {
    spritecache_t *cache = calloc(1, sizeof(*cache));
    CHECK(cache);
    cache->count = 2;
    snprintf(cache->entries[0].name, sizeof(cache->entries[0].name), "FIRST");
    snprintf(cache->entries[1].name, sizeof(cache->entries[1].name), "SECOND");
    const char *const names[] = { "SECOND", "FIRST" };
    gameinfo_t info = { .sprite_count = 2, .sprnames = names };
    CHECK(R_BindSprites(cache, &info));
    CHECK(R_StateSprite(cache, NULL, 0, NULL) == &cache->entries[1].sprite);
    CHECK(R_StateSprite(cache, NULL, 1, "SECOND") == &cache->entries[0].sprite);
    CHECK(R_StateSprite(cache, &info, -1, "FIRST") == &cache->entries[0].sprite);
    CHECK(R_BindSprites(cache, &info));
    R_FreeSpriteCache(cache);
    CHECK(!cache->sprites && !cache->numsprites);
    free(cache);
}

static void check_ui_storage(SDL_Renderer *renderer) {
    spritecache_t *cache = calloc(1, sizeof(*cache));
    CHECK(cache && load_dark_colony_unit_sprites("data/DCOLONY", NULL, NULL, 0, cache));
    CHECK(cache->numsprites == NUMSPRITES && cache->ui);
    CHECK(!cache->ui->sprites && cache->ui->numsprites == 0);
    CHECK(R_StateSprite(cache, NULL, SPR_BARR, NULL));
    CHECK(!R_CacheLookup(cache, "ENCYCLO/BARR.SPR"));
    CHECK(!R_CacheLookup(cache->ui, "BARR"));
    CHECK(R_CacheLookup(cache->ui, "ENCYCLO/BARR.SPR"));
    CHECK(R_CacheLookup(cache->ui, "CURSOR/CURS.SPR"));
    CHECK(R_CacheLookup(cache->ui, "INTRFACE/MAINBUT.SPR"));
    const spritesheet_t *marker = R_CacheLookup(cache->ui, game_info.selection_marker.image);
    CHECK(marker);
    app_t app = { .renderer = renderer };
    mobj_t unit = {0};
    selectiondrawcontext_t ctx = { .app = &app, .unit = &unit, .cache = cache, .game_info = &game_info };
    CHECK(R_DrawSelectionMarkerFrame(&ctx, 0, marker->cells[0].rect));
    R_FreeSpriteCache(cache);
    CHECK(!cache->ui && !cache->sprites && !cache->count);
    free(cache);
}

static void check_turn_and_travel_definitions(void) {
    static const char *const stems[] = { "TRSC", "EXPL", "REAP", "BARR", "SLUG", "ORTU" };
    for (size_t i = 0; i < sizeof(stems) / sizeof(*stems); ++i) {
        dc_fin_t fin;
        spritesheet_t sheet;
        const char *stem = stems[i];
        const char *path = M_va("data/DCOLONY/ANIMATE/%s.FIN", stem);
        CHECK(DC_LoadFIN(path, &fin));
        CHECK(load_dark_colony_sprite(path, &sheet, NULL));
        for (int moving = 0; moving < 2; ++moving) {
            const char *action = moving ? "MOVE" : "STAND";
            const dc_fin_label_t *base = DC_FINLabel(&fin, M_va("%s%s0", stem, action));
            CHECK(base);
            int start = SDL_SwapLE16(base->start), end = SDL_SwapLE16(base->end);
            int rotations = moving && strcmp(stem, "ORTU") ? 8 : 16;
            for (int f = start; f <= end; ++f) {
                const spriteframe_t *frame = &sheet.spritedef.spriteframes[sheet.numlumps + f];
                CHECK(frame->rotations == rotations);
                for (int r = 0; r < rotations; ++r) {
                    int suffix = ((rotations / 2 - r + rotations) % rotations) * (16 / rotations);
                    const char *label_action = !moving && (suffix & 1) ? "SHUF" : action;
                    const dc_fin_label_t *label = DC_FINLabel(&fin, M_va("%s%s%d", stem, label_action, suffix));
                    CHECK(label);
                    int source = SDL_SwapLE16(label->start) + f - start;
                    if (source > SDL_SwapLE16(label->end)) source = SDL_SwapLE16(label->end);
                    spritedirection_t expected = {0};
                    CHECK(DC_FINFrame(&fin, source, &expected));
                    CHECK(frame->directions[r].ticks == expected.ticks);
                    const spritelayer_t *actual = frame->directions[r].layers;
                    int p = 0;
                    for (; expected.layers[p].sprite_name[0]; ++p) {
                        spritelayer_t *part = &expected.layers[p];
                        const char *name = !strcasecmp(part->sprite_name, stem) ? "." : M_Upper(part->sprite_name);
                        CHECK(!strcmp(actual[p].sprite_name, name));
                        CHECK(actual[p].lump == part->lump && ivec2_equal(actual[p].offset, part->offset));
                        CHECK(actual[p].flags == part->flags && actual[p].layer == part->layer);
                        CHECK(actual[p].remap == part->remap && actual[p].intensity == part->intensity);
                    }
                    CHECK(!actual[p].sprite_name[0]);
                    free(expected.layers);
                }
            }
        }
        R_FreeSprite(&sheet);
        DC_FreeFIN(&fin);
    }
}

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

    check_all_fin_frames();
    check_sprite_registry();
    check_ui_storage(renderer);
    check_turn_and_travel_definitions();

    spritesheet_t sprite;
    if (!load_dark_colony_sprite("data/DCOLONY/ANIMATE/TRSC.FIN",
                                 &sprite, NULL)) {
        fprintf(stderr, "FAIL: load Trooper sprite definition\n");
        r_renderer = NULL;
        R_FreeSpriteBuffer();
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(surface);
        return 1;
    }

    static const int expected_lumps[8] = { 20, 21, 22, 23, 16, 17, 18, 19 };
    static const uint8_t expected_flips[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    const spriteframe_t *run = &sprite.spritedef.spriteframes[sprite.numlumps + 16];
    bool valid = run->rotations == 8 && sprite.spritedef.numframes == 472 + sprite.numlumps;
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
        sprite.spritedef.spriteframes[sprite.numlumps + 91].directions[2].layers;
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
        const spriteframe_t *stand = &sprite.spritedef.spriteframes[sprite.numlumps];
        /* Eight STAND and eight SHUF poses turn; eight MOVE pairs travel. */
        static const int stand_lumps[16] = { 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6, 7 };
        CHECK(sprite.spritedef.numframes == 232 + sprite.numlumps);
        CHECK(stand->rotations == 16);
        for (int r = 0; r < 16; ++r)
            CHECK(stand->directions[r].layers[0].lump == stand_lumps[r]);
        static const int move_lumps[2][8] = {
            { 8, 6, 4, 2, 0, 2, 4, 6 },
            { 13, 12, 11, 10, 9, 10, 11, 12 },
        };
        for (int f = 0; f < 2; ++f) {
            const spriteframe_t *move = &sprite.spritedef.spriteframes[sprite.numlumps + 16 + f];
            CHECK(move->rotations == 8);
            for (int r = 0; r < 8; ++r)
                CHECK(move->directions[r].layers[0].lump == move_lumps[f][r]);
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
    R_FreeSpriteBuffer();
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    return valid ? 0 : 1;
}
