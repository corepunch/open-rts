#define _DEFAULT_SOURCE
#include "game.h"
#include <inttypes.h>

/* The catalog exercises the private format decoders without adding runtime
 * APIs just for tests. tools/test_loaders.py omits the included loader object. */
#if defined(DR)
#include "games/dark-reign/w_spr.c"
#elif defined(SL)
#include "games/7legion/w_bim.c"
#elif defined(KK)
#include "games/kknd/w_spr.c"
#endif

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
static uint64_t hash;

static void hash_bytes(const void *data, size_t size) {
    const uint8_t *bytes = data;
    while (size--) {
        hash ^= *bytes++;
        hash *= UINT64_C(1099511628211);
    }
}
#define HASH(value) hash_bytes(&(value), sizeof(value))

static void hash_texture(SDL_Renderer *renderer, SDL_Texture *texture) {
    int width, height;
    CHECK(SDL_QueryTexture(texture, NULL, NULL, &width, &height) == 0);
    HASH(width); HASH(height);
    CHECK(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) == 0);
    /* Terrain atlases can exceed the software render surface. Read every
     * pixel in bounded chunks, preserving the same order across revisions. */
    uint32_t *pixels = malloc(2048 * 2048 * sizeof(*pixels));
    CHECK(pixels);
    for (int y = 0; y < height; y += 2048) {
        for (int x = 0; x < width; x += 2048) {
            SDL_Rect source = { x, y, width - x, height - y };
            if (source.w > 2048) source.w = 2048;
            if (source.h > 2048) source.h = 2048;
            SDL_Rect dest = { 0, 0, source.w, source.h };
            CHECK(SDL_RenderCopy(renderer, texture, &source, &dest) == 0);
            CHECK(SDL_RenderReadPixels(renderer, &dest, SDL_PIXELFORMAT_ARGB8888,
                                       pixels, dest.w * 4) == 0);
            hash_bytes(pixels, (size_t)dest.w * dest.h * sizeof(*pixels));
        }
    }
    free(pixels);
}

static void hash_sprite(SDL_Renderer *renderer, const spritesheet_t *sprite) {
    HASH(sprite->numlumps); HASH(sprite->frame_size);
    for (int i = 0; i < sprite->numlumps; ++i) {
        HASH(sprite->cells[i]);
        hash_texture(renderer, sprite->lumps[i].texture);
    }
    HASH(sprite->spritedef.numframes);
    for (int i = 0; i < sprite->spritedef.numframes; ++i) {
        const spriteframe_t *frame = &sprite->spritedef.spriteframes[i];
        HASH(frame->rotations); HASH(frame->frame_name);
        for (int j = 0; j < MAX_SPRITE_ROTATIONS; ++j) {
            HASH(frame->directions[j].ticks);
            const spritelayer_t *layer = frame->directions[j].layers;
            for (; layer && layer->sprite_name[0]; ++layer) HASH(*layer);
            int end = -1;
            HASH(end);
        }
    }
}

static bool catalog_sprite(SDL_Renderer *renderer, char *path) {
    spritesheet_t sprite = {0};
    uint32_t palette[256];
    for (int i = 0; i < 256; ++i) palette[i] = i ? 0xff000000u | (i * 0x010101u) : 0;
    bool ok = false;
#if defined(DR)
    char *bar = strchr(path, '|');
    CHECK(bar);
    *bar = 0;
    blob_t file;
    if (W_ReadFile(path, &file)) {
        size_t offset = strtoul(bar + 1, NULL, 10);
        size_t size = strtoul(strchr(bar + 1, ',') + 1, NULL, 10);
        CHECK(offset <= file.size && size <= file.size - offset);
        ok = load_dark_sprite(renderer, file.bytes + offset, size, palette, &sprite);
        W_FreeFile(&file);
    }
    *bar = '|';
#elif defined(SL)
    ok = sl_load_bim_sprite(renderer, path, palette, &sprite);
#elif defined(KK)
    ok = load_sprite(renderer, "data/KKND", path, palette, &sprite);
#else
    (void)path;
#endif
    if (ok) hash_sprite(renderer, &sprite);
    R_FreeSprite(&sprite);
    return ok;
}

static bool catalog_map(SDL_Renderer *renderer, const char *path) {
    (void)renderer;
    P_InitThinkers();
    bool ok = G_DoLoadLevel(path, &level);
    if (ok) {
        HASH(level.width); HASH(level.height); HASH(level.tileset_name);
        HASH(level.render_capabilities); HASH(level.camera); HASH(level.has_camera);
        HASH(level.day_rate); HASH(level.player_resources);
        size_t cells = (size_t)level.width * level.height;
        hash_bytes(level.tile_ids, cells * sizeof(*level.tile_ids));
        hash_bytes(level.blocked, cells * sizeof(*level.blocked));
        HASH(level.tile_overlay_count);
        for (int i = 0; i < MAX_TILE_OVERLAYS; ++i)
            if (level.tile_overlays[i]) hash_bytes(level.tile_overlays[i], cells * 2);
        for (int i = 0; i <= MAX_TILE_OVERLAYS; ++i)
            if (level.tile_transforms[i]) hash_bytes(level.tile_transforms[i], cells);
        if (level.cell_colors) hash_bytes(level.cell_colors, cells * 4);
        HASH(level.resource_vent_count);
        for (int i = 0; i < level.resource_vent_count; ++i) {
            const resourcevent_t *vent = &level.resource_vents[i];
            /* Hash defined fields, not padding in realloc-owned vent records. */
            HASH(vent->cell); HASH(vent->attachment); HASH(vent->amount);
            HASH(vent->rate); HASH(vent->active); HASH(vent->resource_type);
        }
        HASH(level.decoration_count);
        hash_bytes(level.decorations, level.decoration_count * sizeof(*level.decorations));
        int count = P_LoadThings(path);
        HASH(count);
        for (thinker_t *thinker = thinkercap.next; thinker != &thinkercap; thinker = thinker->next) {
            if (thinker->function != P_MobjThinker) continue;
            const mobj_t *mobj = (const mobj_t *)thinker;
            HASH(mobj->core); HASH(mobj->speed); HASH(mobj->type_id); HASH(mobj->native_type_id);
            HASH(mobj->owner); HASH(mobj->team); HASH(mobj->allegiance);
            HASH(mobj->traits); HASH(mobj->hp); HASH(mobj->max_hp);
        }
#ifdef KK
        tileset_t tileset = {0};
        CHECK(build_map_tileset(renderer, level.native_data, &tileset));
        HASH(tileset.count); HASH(tileset.atlas_cols); HASH(tileset.tile_w); HASH(tileset.tile_h);
        hash_texture(renderer, tileset.texture);
        R_FreeTileset(&tileset);
#endif
    }
    P_FreeLevel(&level);
    return ok;
}

#include "loader_fixtures.h"

int main(int argc, char **argv) {
    G_InitGame();
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 2048, 2048, 32, SDL_PIXELFORMAT_ARGB8888);
    CHECK(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(renderer);
    r_renderer = renderer;
    if (argc == 2 && strcmp(argv[1], "--fixtures") == 0) {
        test_loader_fixtures(renderer);
    } else {
        CHECK(argc == 3);
        FILE *files = fopen(argv[2], "r");
        CHECK(files);
        char path[1024];
        while (fgets(path, sizeof(path), files)) {
            path[strcspn(path, "\n")] = 0;
            hash = UINT64_C(14695981039346656037);
            bool ok = strcmp(argv[1], "maps") == 0 ?
                catalog_map(renderer, path) : catalog_sprite(renderer, path);
            printf("%s %016" PRIx64 " %s\n", ok ? "OK" : "FAIL", hash, path);
            fflush(stdout);
        }
        fclose(files);
    }
    r_renderer = NULL;
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    return 0;
}
