#define _DEFAULT_SOURCE
#include "engine.h"
#include <inttypes.h>

/* The catalog exercises the private format decoders without adding runtime
 * APIs just for tests. The Makefile omits the included loader object. */
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

/* Hash what the loader decoded: every index expanded through its palette. */
static void hash_indexed(const uint8_t *indices, size_t count, const uint32_t palette[256]) {
    for (size_t i = 0; i < count; ++i) {
        uint32_t color = palette[indices[i]];
        HASH(color);
    }
}

static void hash_sprite(const spritesheet_t *sprite) {
    HASH(sprite->numlumps); HASH(sprite->frame_size);
    for (int i = 0; i < sprite->numlumps; ++i) {
        HASH(sprite->cells[i]);
        if (!sprite->lumps[i].indices) continue;
        int width = sprite->cells[i].rect.w, height = sprite->cells[i].rect.h;
        HASH(width); HASH(height);
        hash_indexed(sprite->lumps[i].indices, (size_t)width * (size_t)height,
                     sprite->source_palette);
    }
    HASH(sprite->spritedef.numframes);
    for (int i = 0; i < sprite->spritedef.numframes; ++i) {
        const spriteframe_t *frame = &sprite->spritedef.spriteframes[i];
        HASH(frame->rotations); HASH(frame->frame_name);
        for (int j = 0; j < MAX_SPRITE_ROTATIONS; ++j) {
            const spritedirection_t empty = {0};
            const spritedirection_t *direction = j < frame->rotations ?
                &frame->directions[j] : &empty;
            HASH(direction->ticks);
            const spritelayer_t *layer = direction->layers;
            for (; layer && layer->sprite_name[0]; ++layer) HASH(*layer);
            int end = -1;
            HASH(end);
        }
    }
}

static bool catalog_sprite(char *path) {
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
        ok = load_dark_sprite(file.bytes + offset, size, palette, &sprite);
        W_FreeFile(&file);
    }
    *bar = '|';
#elif defined(SL)
    ok = sl_load_bim_sprite(path, palette, &sprite);
#elif defined(KK)
    ok = load_sprite("data/KKND", path, palette, &sprite, NULL, NULL);
#else
    (void)path;
#endif
    if (ok) hash_sprite(&sprite);
    R_FreeSprite(&sprite);
    return ok;
}

static bool catalog_map(const char *path) {
    P_InitThinkers();
    bool ok = G_DoLoadLevel(path, &level);
    if (ok) {
        HASH(level.width); HASH(level.height); HASH(level.tileset_name);
        HASH(level.render_capabilities); HASH(level.camera); HASH(level.has_camera);
        HASH(level.daylight.duration); HASH(level.player_resources);
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
        CHECK(build_map_tileset(level.native_data, &tileset));
        HASH(tileset.count); HASH(tileset.tile_w); HASH(tileset.tile_h);
        hash_indexed(tileset.indices, (size_t)tileset.count * (size_t)tileset.tile_w *
                     (size_t)tileset.tile_h, tileset.palette);
        R_FreeTileset(&tileset);
#endif
    }
    P_FreeLevel(&level);
    return ok;
}

#include "loader_fixtures.h"

int main(int argc, char **argv) {
    G_InitGame();
    if (argc == 2 && strcmp(argv[1], "--fixtures") == 0) {
        test_loader_fixtures();
    } else {
        CHECK(argc == 3);
        FILE *files = fopen(argv[2], "r");
        CHECK(files);
        char path[1024];
        while (fgets(path, sizeof(path), files)) {
            path[strcspn(path, "\n")] = 0;
            hash = UINT64_C(14695981039346656037);
            bool ok = strcmp(argv[1], "maps") == 0 ?
                catalog_map(path) : catalog_sprite(path);
            printf("%s %016" PRIx64 " %s\n", ok ? "OK" : "FAIL", hash, path);
            fflush(stdout);
        }
        fclose(files);
    }
    return 0;
}
