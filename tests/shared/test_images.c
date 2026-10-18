#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "engine.h"
#include "t_local.h"
#define CHECK(c) RTS_CHECK(c, "engine images", #c)

static int lbm(void) {
    for (int planar = 0; planar < 2; ++planar) for (int compressed = 0; compressed < 2; ++compressed) {
        uint8_t data[840] = {0};
        memcpy(data, "FORM", 4); memcpy(data + 8, planar ? "ILBM" : "PBM ", 4);
        memcpy(data + 12, "BMHD", 4); data[19] = 20;
        data[21] = planar ? 1 : 3; data[23] = planar ? 1 : 2;
        data[28] = 8; data[30] = compressed;
        memcpy(data + 40, "CMAP", 4); data[46] = 3;
        data[48 + 133 * 3] = 217;
        memcpy(data + 816, "BODY", 4);
        int length = planar ? 16 : compressed ? 4 : 8;
        data[823] = length;
        if (planar) {
            for (int p = 0; p < 8; ++p) {
                if (compressed) data[824 + 2 * p] = 255;
                data[824 + 2 * p + (compressed ? 1 : 0)] = (133 & (1 << p)) ? 128 : 0;
            }
        } else if (compressed) memcpy(data + 824, (uint8_t[]){253,7,253,8}, 4);
        else memcpy(data + 824, (uint8_t[]){7,7,7,99,8,8,8,99}, 8);
        size_t size = 824 + length;
        data[6] = (size - 8) >> 8; data[7] = size - 8;
        blob_t file = {.bytes = data, .size = size}; spritesheet_t sheet = {0};
        CHECK(W_DecodeLBM(&file, &sheet));
        CHECK(sheet.frame_size.w == (planar ? 1 : 3) && sheet.frame_size.h == (planar ? 1 : 2));
        CHECK(sheet.lumps[0].indices[0] == (planar ? 133 : 7));
        CHECK(sheet.palette[133] == 0xffd90000u);
        if (!planar) CHECK(sheet.lumps[0].indices[5] == 8);
        R_FreeSprite(&sheet);
        --file.size; CHECK(!W_DecodeLBM(&file, &sheet)); ++file.size;
        if (compressed) {
            data[824] = 129; /* A repeat may not cross a plane/scanline. */
            CHECK(!W_DecodeLBM(&file, &sheet));
        }
        data[822] = 127; CHECK(!W_DecodeLBM(&file, &sheet));
    }
    return 0;
}

int main(void) {
    RTS_RUN(lbm());
    char directory[] = "/private/tmp/open-rts-image-test-XXXXXX";
    CHECK(mkdtemp(directory));
    char pcx_path[256], bmp_path[256];
    snprintf(pcx_path, sizeof(pcx_path), "%s/image.pcx", directory);
    snprintf(bmp_path, sizeof(bmp_path), "%s/image.bmp", directory);
    uint8_t pcx[128 + 6 + 769] = {0};
    pcx[0] = 10; pcx[1] = 5; pcx[2] = 1; pcx[3] = 8;
    pcx[8] = 2; pcx[10] = 1; pcx[65] = 1; pcx[66] = 4;
    /* Two 3-pixel rows with one padding byte each. */
    memcpy(pcx + 128, (uint8_t[]){0xc3,7,99,0xc3,8,99}, 6);
    pcx[134] = 12;
    pcx[135+7*3] = 250;
    FILE *f = fopen(pcx_path, "wb");
    CHECK(f && fwrite(pcx, 1, sizeof(pcx), f) == sizeof(pcx));
    CHECK(fclose(f) == 0);
    SDL_Surface *s = W_LoadImage(pcx_path);
    CHECK(s && s->w == 3 && s->h == 2);
    CHECK(((uint8_t *)s->pixels)[0] == 7 && ((uint8_t *)s->pixels)[s->pitch+2] == 8);
    CHECK(s->format->palette->colors[7].r == 250);
    CHECK(SDL_SaveBMP(s, bmp_path) == 0);
    SDL_FreeSurface(s);
    s = W_LoadImage(bmp_path);
    CHECK(s && s->w == 3 && s->h == 2);
    SDL_FreeSurface(s);
    pcx[128] = 0xc5; /* A run must not overrun the four-byte scanline. */
    f = fopen(pcx_path, "wb");
    CHECK(f && fwrite(pcx, 1, sizeof(pcx), f) == sizeof(pcx));
    CHECK(fclose(f) == 0);
    CHECK(!W_LoadImage(pcx_path));
    unlink(pcx_path);
    unlink(bmp_path);
    rmdir(directory);
    puts("PASS: engine BMP/PCX/ILBM/PBM pixels, palettes, padded rows, chunks and invalid runs");
    return 0;
}
