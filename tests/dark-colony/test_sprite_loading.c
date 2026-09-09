#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include "w_spr.h"
#include <unistd.h>
#include <inttypes.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
enum { HEADER = 8 + 256 * 3, DATA = HEADER + 3 * 8 };

static void u16(uint8_t *p, unsigned n) { p[0] = n; p[1] = n >> 8; }
static void u32(uint8_t *p, unsigned n) { u16(p, n); u16(p + 2, n >> 16); }

static size_t fixture(uint8_t *file, bool compressed) {
    memset(file, 0, 1024);
    u16(file, compressed ? 0x81 : 1);
    u16(file + 2, 3);
    for (int i = 0; i < 256; ++i)
        memset(file + 8 + i * 3, i % 64, 3);
    u16(file + HEADER, 3); u16(file + HEADER + 2, 2);
    u16(file + HEADER + 4, 7); u16(file + HEADER + 6, 9);
    u16(file + HEADER + 8, 2); u16(file + HEADER + 10, 1);
    static const uint8_t raw[] = { 0, 138, 5, 143, 0, 2, 8, 9 };
    static const uint8_t packed[] = { 255, 2, 138, 5, 143, 255, 0, 2 };
    if (!compressed) {
        u32(file + 4, sizeof(raw));
        memcpy(file + DATA, raw, sizeof(raw));
        return DATA + sizeof(raw);
    }
    u32(file + 4, sizeof(packed) + 3);
    u32(file + DATA, sizeof(packed));
    memcpy(file + DATA + 4, packed, sizeof(packed));
    size_t pos = DATA + 4 + sizeof(packed);
    u32(file + pos, 3); pos += 4;
    file[pos++] = 1; file[pos++] = 8; file[pos++] = 9;
    u32(file + pos, 0); /* Empty cell still has a chunk length. */
    return pos + 4;
}

static bool load(SDL_Renderer *renderer, const uint8_t *bytes, size_t size,
                  spritesheet_t *sheet) {
    char path[] = "/private/tmp/dc-sprite-test-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    FILE *file = fdopen(fd, "wb");
    CHECK(file && fwrite(bytes, 1, size, file) == size);
    CHECK(fclose(file) == 0);
    bool loaded = load_dark_colony_sprite(renderer, path, sheet, NULL);
    CHECK(unlink(path) == 0);
    return loaded;
}

static void rejected(SDL_Renderer *renderer, const uint8_t *file, size_t size) {
    spritesheet_t sheet;
    CHECK(!load(renderer, file, size, &sheet));
    CHECK(!sheet.lumps && !sheet.spritedef.spriteframes);
    CHECK(sheet.numlumps == 0 && sheet.spritedef.numframes == 0);
    R_FreeSprite(&sheet); /* Failure leaves the public result safe to free. */
}

static void check_pixels(SDL_Renderer *renderer, SDL_Texture *texture,
                         const uint8_t *indices, int remap) {
    uint32_t actual[6];
    SDL_Rect rect = { 0, 0, 3, 2 };
    CHECK(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) == 0);
    CHECK(SDL_RenderCopy(renderer, texture, NULL, &rect) == 0);
    CHECK(SDL_RenderReadPixels(renderer, &rect, SDL_PIXELFORMAT_ARGB8888, actual, 12) == 0);
    for (int i = 0; i < 6; ++i) {
        int index = indices[i];
        if (remap >= 0 && index >= 138 && index <= 143) index += (remap - 7) * 6;
        uint32_t channel = (index % 64) * 4 + 3;
        uint32_t expected = index ? 0xff000000u | channel * 0x010101u : 0;
        CHECK(actual[i] == expected);
    }
}


/* Optional catalog fingerprint compares actual SDL pixels (including all team
 * remaps), indices, placement, and FIN definitions across loader revisions. */
static uint64_t catalog_hash;
static void hash_bytes(const void *data, size_t size) {
    const uint8_t *bytes = data;
    while (size--) {
        catalog_hash ^= *bytes++;
        catalog_hash *= UINT64_C(1099511628211);
    }
}
static void hash_int(int value) { hash_bytes(&value, sizeof(value)); }

static void hash_texture(SDL_Renderer *renderer, SDL_Texture *texture, irect_t rect) {
    size_t size = (size_t)rect.w * (size_t)rect.h * sizeof(uint32_t);
    uint32_t *pixels = malloc(size);
    SDL_Rect area = { 0, 0, rect.w, rect.h };
    CHECK(pixels);
    CHECK(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) == 0);
    CHECK(SDL_RenderCopy(renderer, texture, NULL, &area) == 0);
    CHECK(SDL_RenderReadPixels(renderer, &area, SDL_PIXELFORMAT_ARGB8888, pixels, rect.w * 4) == 0);
    hash_bytes(pixels, size);
    free(pixels);
}

static int catalog(const char *manifest) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 512, 512, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *renderer = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
    FILE *files = fopen(manifest, "r");
    CHECK(renderer && files);
    char path[1024];
    while (fgets(path, sizeof(path), files)) {
        path[strcspn(path, "\n")] = '\0';
        spritesheet_t sheet;
        if (!load_dark_colony_sprite(renderer, path, &sheet, NULL)) {
            printf("FAIL %s\n", path);
            continue;
        }
        catalog_hash = UINT64_C(14695981039346656037);
        hash_int(sheet.numlumps);
        hash_int(sheet.frame_size.w); hash_int(sheet.frame_size.h);
        hash_bytes(sheet.palette, sizeof(sheet.palette));
        for (int i = 0; i < sheet.numlumps; ++i) {
            const spritelump_t *lump = &sheet.lumps[i];
            hash_bytes(&lump->rect, sizeof(lump->rect));
            hash_bytes(&lump->bounds, sizeof(lump->bounds));
            hash_bytes(&lump->ground_point, sizeof(lump->ground_point));
            hash_bytes(&lump->displacement, sizeof(lump->displacement));
            hash_bytes(lump->indices, (size_t)lump->rect.w * (size_t)lump->rect.h);
            hash_texture(renderer, lump->texture, lump->rect);
            for (int remap = 0; remap < 8; ++remap) {
                SDL_Texture *texture = lump->texture;
                for (int j = 0; j < lump->translation_count; ++j)
                    if (lump->translations[j].id == remap) texture = lump->translations[j].texture;
                hash_texture(renderer, texture, lump->rect);
            }
        }
        hash_int(sheet.spritedef.numframes); hash_int(sheet.spritedef.rotations);
        hash_int(sheet.spritedef.first_angle); hash_int(sheet.spritedef.clockwise);
        for (int i = 0; i < sheet.spritedef.numframes; ++i) {
            const spriteframe_t *frame = &sheet.spritedef.spriteframes[i];
            hash_bytes(frame->frame_name, sizeof(frame->frame_name));
            for (int j = 0; j < MAX_SPRITE_ROTATIONS; ++j) {
                const spritedirection_t *direction = &frame->directions[j];
                hash_int(direction->ticks);
                if (direction->layers)
                    for (const spritelayer_t *part = direction->layers; part->sprite_name[0]; ++part)
                        hash_bytes(part, sizeof(*part));
                hash_int(-1);
            }
        }
        printf("%016" PRIx64 " %s\n", catalog_hash, path);
        R_FreeSprite(&sheet);
    }
    fclose(files);
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2) return catalog(argv[1]);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *renderer = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
    CHECK(renderer);
    uint8_t file[1024];
    static const uint8_t pixels[] = { 0, 138, 5, 143, 0, 2 };
    for (int compressed = 0; compressed <= 1; ++compressed) {
        size_t size = fixture(file, compressed);
        spritesheet_t sheet;
        CHECK(load(renderer, file, size, &sheet));
        CHECK(sheet.numlumps == 3 && sheet.spritedef.numframes == 3);
        CHECK(sheet.frame_size.w == 10 && sheet.frame_size.h == 11);
        CHECK(memcmp(sheet.lumps[0].indices, pixels, sizeof(pixels)) == 0);
        CHECK(sheet.lumps[1].indices[0] == 8 && sheet.lumps[1].indices[1] == 9);
        CHECK(sheet.lumps[2].rect.w == 1 && sheet.lumps[2].rect.h == 1);
        CHECK(sheet.lumps[2].indices[0] == 0);
        CHECK(ivec2_equal(sheet.lumps[0].displacement, (ivec2_t){ 7, 9 }));
        CHECK(ivec2_equal(sheet.lumps[0].ground_point, (ivec2_t){ 1, 2 }));
        CHECK(sheet.lumps[0].translation_count == 8);
        CHECK(sheet.lumps[1].translation_count == 0 && sheet.lumps[2].translation_count == 0);
        check_pixels(renderer, sheet.lumps[0].texture, pixels, -1);
        for (int i = 0; i < 8; ++i) {
            CHECK(sheet.lumps[0].translations[i].id == i);
            check_pixels(renderer, sheet.lumps[0].translations[i].texture, pixels, i);
        }
        R_FreeSprite(&sheet);
        rejected(renderer, file, size - 1); /* Truncated last cell, after allocations. */
        rejected(renderer, file, HEADER - 1);
        rejected(renderer, file, DATA - 1);
    }
    size_t size = fixture(file, true);
    file[DATA + 4] = 127; /* Literal run exceeds the destination. */
    rejected(renderer, file, size);
    size = fixture(file, true);
    file[DATA + 4] = 128; /* Transparent run exceeds the destination. */
    rejected(renderer, file, size);
    size = fixture(file, true);
    u32(file + DATA, 1); file[DATA + 4] = 0; /* Literal has no source byte. */
    rejected(renderer, file, size);
    size = fixture(file, true);
    u32(file + DATA + 12, UINT32_MAX); /* Oversized second chunk. */
    rejected(renderer, file, size);
    size = fixture(file, false);
    u16(file + HEADER, 513);
    rejected(renderer, file, size);
    size = fixture(file, false);
    u16(file + 2, 0);
    rejected(renderer, file, size);
    size = fixture(file, false);
    rejected(NULL, file, size); /* Texture failure cleans up decoded storage. */
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    puts("PASS: direct SPR loading, translations, empty cells, and malformed spans");
    return 0;
}
