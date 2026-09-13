#include "engine.h"
#include "rts_test.h"
#define CHECK(c) RTS_CHECK(c, "engine images", #c)

int main(void) {
    uint8_t pcx[128 + 6 + 769] = {0};
    pcx[0] = 10; pcx[1] = 5; pcx[2] = 1; pcx[3] = 8;
    pcx[8] = 2; pcx[10] = 1; pcx[65] = 1; pcx[66] = 4;
    /* Two 3-pixel rows with one padding byte each. */
    memcpy(pcx + 128, (uint8_t[]){0xc3,7,99,0xc3,8,99}, 6);
    pcx[134] = 12;
    pcx[135+7*3] = 250;
    FILE *f = fopen("/private/tmp/open-rts-image-test.pcx", "wb");
    CHECK(f && fwrite(pcx, 1, sizeof(pcx), f) == sizeof(pcx));
    CHECK(fclose(f) == 0);
    SDL_Surface *s = W_LoadImage("/private/tmp/open-rts-image-test.pcx");
    CHECK(s && s->w == 3 && s->h == 2);
    CHECK(((uint8_t *)s->pixels)[0] == 7 && ((uint8_t *)s->pixels)[s->pitch+2] == 8);
    CHECK(s->format->palette->colors[7].r == 250);
    CHECK(SDL_SaveBMP(s, "/private/tmp/open-rts-image-test.bmp") == 0);
    SDL_FreeSurface(s);
    s = W_LoadImage("/private/tmp/open-rts-image-test.bmp");
    CHECK(s && s->w == 3 && s->h == 2);
    SDL_FreeSurface(s);
    pcx[128] = 0xc5; /* A run must not overrun the four-byte scanline. */
    f = fopen("/private/tmp/open-rts-image-test.pcx", "wb");
    CHECK(f && fwrite(pcx, 1, sizeof(pcx), f) == sizeof(pcx));
    CHECK(fclose(f) == 0);
    CHECK(!W_LoadImage("/private/tmp/open-rts-image-test.pcx"));
    puts("PASS: engine BMP/PCX pixels, palettes, padded rows and invalid runs");
    return 0;
}
