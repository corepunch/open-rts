#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include "dark-colony.h"
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

static bool load(const uint8_t *bytes, size_t size,
                  spritesheet_t *sheet) {
    char path[] = "/private/tmp/dc-sprite-test-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    FILE *file = fdopen(fd, "wb");
    CHECK(file && fwrite(bytes, 1, size, file) == size);
    CHECK(fclose(file) == 0);
    bool loaded = load_dark_colony_sprite(path, sheet, NULL);
    CHECK(unlink(path) == 0);
    return loaded;
}

static void rejected(const uint8_t *file, size_t size) {
    spritesheet_t sheet;
    CHECK(!load(file, size, &sheet));
    CHECK(!sheet.lumps && !sheet.cells && !sheet.spritedef.spriteframes);
    CHECK(sheet.numlumps == 0 && sheet.spritedef.numframes == 0);
    R_FreeSprite(&sheet); /* Failure leaves the public result safe to free. */
}

/* The screen is 8x8 indices. Reading it back expands through the screen
 * palette with alpha forced to 0xff. SPR entry 0 is transparent, so even an
 * opaque draw leaves the cleared black behind it. */
static void read_screen(uint32_t actual[64]) {
    V_ReadPixels(actual, 8 * 4);
}

static void check_pixels(const spritesheet_t *sprite, const uint8_t *indices, int remap) {
    uint32_t actual[64];
    irect_t rect = { 0, 0, 3, 2 };
    V_BeginFrame(0xff000000u);
    CHECK(R_DrawSprite(sprite, 0, remap, NULL, &rect, V_OPAQUE, 16));
    read_screen(actual);
    for (int i = 0; i < 6; ++i) {
        int index = indices[i];
        if (remap >= 0 && index >= 138 && index <= 143) index += (remap - 7) * 6;
        uint32_t channel = (index % 64) * 4 + 3;
        uint32_t expected = index ? 0xff000000u | channel * 0x010101u : 0xff000000u;
        CHECK(actual[(i / 3) * 8 + i % 3] == expected);
    }
}

static void check_blits(const spritesheet_t *sprite) {
    irect_t left = {0, 0, 3, 2}, right = {4, 0, 3, 2};
    CHECK(R_DrawSprite(sprite, 0, 0, NULL, &left, V_OPAQUE, 16));
    CHECK(R_DrawSprite(sprite, 0, 7, NULL, &right, V_OPAQUE, 16));
    uint32_t actual[64];
    read_screen(actual);
    CHECK(actual[1] == sprite->source_palette[96]);
    CHECK(actual[5] == sprite->source_palette[138]);

    V_BeginFrame(0xff000000u);
    irect_t src = {1, 0, 2, 2}, dst = {1, 1, 4, 4}, clip = {2, 2, 3, 3};
    V_SetClip(clip);
    CHECK(R_DrawSprite(sprite, 0, -1, &src, &dst, V_FLIP_X, 16));
    V_SetClip((irect_t){0});
    read_screen(actual);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            uint32_t expected = 0xff000000;
            if (y == 2 && x >= 2 && x <= 4)
                expected = sprite->source_palette[x == 2 ? 5 : 138];
            if ((y == 3 || y == 4) && x == 2)
                expected = sprite->source_palette[2];
            CHECK(actual[y * 8 + x] == expected);
        }
    }
    src.x = 2; /* Crop crosses the right edge. */
    CHECK(!R_DrawSprite(sprite, 0, -1, &src, &dst, 0, 16));
}


/* Optional catalog fingerprint compares drawn pixels (including all team
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

static void hash_sprite(const spritesheet_t *sprite, int frame, int palette) {
    irect_t rect = sprite->cells[frame].rect;
    irect_t area = { 0, 0, rect.w, rect.h };
    CHECK(rect.w <= screens[0].w && rect.h <= screens[0].h);
    CHECK(R_DrawSprite(sprite, frame, palette, NULL, &area, V_OPAQUE, 16));
    for (int y = 0; y < rect.h; ++y) {
        const uint8_t *row = screens[0].pixels + (size_t)y * (size_t)screens[0].w;
        for (int x = 0; x < rect.w; ++x) {
            uint32_t color = vpalette[row[x]] | 0xff000000u;
            hash_bytes(&color, sizeof(color));
        }
    }
}

static int catalog(const char *manifest) {
    V_AllocScreen(1024, 1024);
    FILE *files = fopen(manifest, "r");
    CHECK(screens[0].pixels && files);
    char path[1024];
    while (fgets(path, sizeof(path), files)) {
        path[strcspn(path, "\n")] = '\0';
        spritesheet_t sheet;
        if (!load_dark_colony_sprite(path, &sheet, NULL)) {
            printf("FAIL %s\n", path);
            continue;
        }
        I_SetPalette(sheet.source_palette);
        catalog_hash = UINT64_C(14695981039346656037);
        hash_int(sheet.numlumps);
        hash_int(sheet.frame_size.w); hash_int(sheet.frame_size.h);
        hash_bytes(sheet.palette, sizeof(sheet.palette));
        for (int i = 0; i < sheet.numlumps; ++i) {
            const spritelump_t *lump = &sheet.lumps[i];
            const spritecell_t *cell = &sheet.cells[i];
            hash_bytes(&cell->rect, sizeof(cell->rect));
            hash_bytes(&cell->bounds, sizeof(cell->bounds));
            hash_bytes(&cell->ground_point, sizeof(cell->ground_point));
            hash_bytes(&cell->displacement, sizeof(cell->displacement));
            hash_bytes(lump->indices, (size_t)cell->rect.w * (size_t)cell->rect.h);
            for (int remap = -1; remap < 8; ++remap)
                hash_sprite(&sheet, i, remap);
        }
        hash_int(sheet.spritedef.numframes);
        for (int i = 0; i < sheet.spritedef.numframes; ++i) {
            const spriteframe_t *frame = &sheet.spritedef.spriteframes[i];
            hash_bytes(frame->frame_name, sizeof(frame->frame_name));
            hash_int(sheet.spritedef.spriteframes[i].rotations);
            for (int j = 0; j < MAX_SPRITE_ROTATIONS; ++j) {
                const spritedirection_t empty = {0};
                const spritedirection_t *direction = j < frame->rotations ?
                    &frame->directions[j] : &empty;
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
    V_FreeScreen();
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2) return catalog(argv[1]);
    V_AllocScreen(8, 8);
    CHECK(screens[0].pixels);
    uint8_t file[1024];
    static const uint8_t pixels[] = { 0, 138, 5, 143, 0, 2 };
    /* Transparent margins remain part of the authored SPR geometry. */
    size_t padded_size = fixture(file, false);
    memset(file + DATA, 0, 6);
    file[DATA + 1] = 5;
    spritesheet_t padded;
    CHECK(load(file, padded_size, &padded));
    CHECK(memcmp(&padded.cells[0].bounds, &padded.cells[0].rect,
                 sizeof(irect_t)) == 0);
    CHECK(ivec2_equal(padded.cells[0].ground_point, (ivec2_t){ 1, 2 }));
    R_FreeSprite(&padded);
    for (int compressed = 0; compressed <= 1; ++compressed) {
        size_t size = fixture(file, compressed);
        spritesheet_t sheet;
        CHECK(load(file, size, &sheet));
        CHECK(sheet.numlumps == 3 && sheet.spritedef.numframes == 3);
        CHECK(sheet.frame_size.w == 10 && sheet.frame_size.h == 11);
        CHECK(memcmp(sheet.lumps[0].indices, pixels, sizeof(pixels)) == 0);
        CHECK(sheet.lumps[1].indices[0] == 8 && sheet.lumps[1].indices[1] == 9);
        CHECK(sheet.cells[2].rect.w == 1 && sheet.cells[2].rect.h == 1);
        CHECK(sheet.lumps[2].indices[0] == 0);
        CHECK(ivec2_equal(sheet.cells[0].displacement, (ivec2_t){ 7, 9 }));
        CHECK(ivec2_equal(sheet.cells[0].ground_point, (ivec2_t){ 1, 2 }));
        /* World colormaps must not replace the SPR's source palette. */
        memset(sheet.palette, 0, sizeof(sheet.palette));
        I_SetPalette(sheet.source_palette);
        for (int pass = 0; pass < 2; ++pass)
            for (int i = -1; i < 8; ++i)
                check_pixels(&sheet, pixels, i);
        check_pixels(&sheet, pixels, -1);
        check_blits(&sheet);
        CHECK(!R_DrawSprite(NULL, 0, 2, NULL, NULL, V_OPAQUE, 16));
        CHECK(!R_DrawSprite(&sheet, -1, 0, NULL, NULL, V_OPAQUE, 16));
        CHECK(!R_DrawSprite(&sheet, sheet.numlumps, 0, NULL, NULL, V_OPAQUE, 16));
        /* Unknown palette IDs select source colors, and source palettes remain live. */
        irect_t dst = {0, 0, 3, 2};
        CHECK(R_DrawSprite(&sheet, 0, 99, NULL, &dst, V_OPAQUE, 16));
        uint32_t actual[64];
        read_screen(actual);
        CHECK(actual[1] == sheet.source_palette[138]);
        /* A colour the screen palette lacks is nearest-matched, not invented. */
        sheet.source_palette[138] = 0xff123456;
        CHECK(R_DrawSprite(&sheet, 0, -1, NULL, &dst, V_OPAQUE, 16));
        read_screen(actual);
        CHECK(actual[1] != 0xff123456);
        I_SetPalette(sheet.source_palette);
        CHECK(R_DrawSprite(&sheet, 0, -1, NULL, &dst, V_OPAQUE, 16));
        read_screen(actual);
        CHECK(actual[1] == 0xff123456);
        R_FreeSprite(&sheet);
        rejected(file, size - 1); /* Truncated last cell, after allocations. */
        rejected(file, HEADER - 1);
        rejected(file, DATA - 1);
    }
    /* Native cells wider than 512 pixels must not hit an invented format cap. */
    uint8_t wide[HEADER + 8 + 640] = {0};
    u16(wide, 1); u16(wide + 2, 1); u32(wide + 4, 640);
    u16(wide + HEADER, 640); u16(wide + HEADER + 2, 1);
    memset(wide + HEADER + 8, 5, 640);
    spritesheet_t wide_sprite;
    CHECK(load(wide, sizeof(wide), &wide_sprite));
    CHECK(wide_sprite.cells[0].rect.w == 640 && wide_sprite.cells[0].rect.h == 1);
    CHECK(wide_sprite.lumps[0].indices[639] == 5);
    R_FreeSprite(&wide_sprite);
    size_t size = fixture(file, true);
    file[DATA + 4] = 127; /* Literal run exceeds the destination. */
    rejected(file, size);
    size = fixture(file, true);
    file[DATA + 4] = 128; /* Transparent run exceeds the destination. */
    rejected(file, size);
    size = fixture(file, true);
    u32(file + DATA, 1); file[DATA + 4] = 0; /* Literal has no source byte. */
    rejected(file, size);
    size = fixture(file, true);
    u32(file + DATA + 12, UINT32_MAX); /* Oversized second chunk. */
    rejected(file, size);
    size = fixture(file, false);
    u16(file + HEADER, 513);
    rejected(file, size);
    size = fixture(file, false);
    u16(file + 2, 0);
    rejected(file, size);
    size = fixture(file, false);
    V_FreeScreen();
    spritesheet_t decoded;
    CHECK(load(file, size, &decoded)); /* Decoding needs no screen. */
    R_FreeSprite(&decoded);
    puts("PASS: direct SPR loading, translations, empty cells, and malformed spans");
    return 0;
}
