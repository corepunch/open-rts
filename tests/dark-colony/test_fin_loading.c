#define _POSIX_C_SOURCE 200809L
#include "w_spr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

enum { LABEL = 16, FRAMES = 36, COMMANDS = FRAMES + 2 * 164, SIZE = COMMANDS + 3 * 22 };
static void u16(uint8_t *p, unsigned value) { p[0] = value; p[1] = value >> 8; }

static bool load(const uint8_t *bytes, size_t size, dc_fin_t *fin) {
    char path[] = "/private/tmp/dc-fin-test-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    FILE *file = fdopen(fd, "wb");
    CHECK(file && fwrite(bytes, 1, size, file) == size);
    CHECK(fclose(file) == 0);
    bool ok = DC_LoadFIN(path, fin);
    CHECK(unlink(path) == 0);
    return ok;
}

int main(void) {
    uint8_t bytes[SIZE] = {0};
    u16(bytes, 29); u16(bytes + 2, 2); u16(bytes + 4, 1); u16(bytes + 6, 1);
    memcpy(bytes + 8, "EXTERNAL", 8);
    memcpy(bytes + LABEL, "ABCDEFGHIJKLMNOP", 16);
    u16(bytes + LABEL + 18, 1);
    u16(bytes + FRAMES, 2); u16(bytes + FRAMES + 2, 7);
    u16(bytes + FRAMES + 164, 1); u16(bytes + FRAMES + 166, 11);
    for (int i = 0; i < 3; ++i) {
        uint8_t *command = bytes + COMMANDS + i * 22;
        memcpy(command, "EXTERNAL", 8);
        u16(command + 8, 20 + i);
        u16(command + 10, (uint16_t)-159); u16(command + 12, 28);
        u16(command + 14, 7); u16(command + 16, 16);
        u16(command + 18, 5); u16(command + 20, 1);
    }
    dc_fin_t fin;
    CHECK(load(bytes, sizeof(bytes), &fin));
    CHECK((const void *)fin.header == fin.file.bytes);
    CHECK((const void *)fin.labels == fin.file.bytes + LABEL);
    CHECK((const void *)fin.frames == fin.file.bytes + FRAMES);
    CHECK((const void *)fin.commands == fin.file.bytes + COMMANDS);
    CHECK(fin.frame_commands[1] == fin.commands + 2);
    CHECK(SDL_SwapLE16(fin.header->magic) == 29 && SDL_SwapLE16(fin.header->frame_count) == 2);
    CHECK(DC_FINLabel(&fin, "ABCDEFGHIJKLMNOP") == &fin.labels[0]);
    CHECK(!DC_FINLabel(&fin, "ABCDEFGHIJKLMNO"));
    CHECK(!DC_FINLabel(&fin, "ABCDEFGHIJKLMNOPQ"));
    spritedirection_t direction = {0};
    CHECK(DC_FINFrame(&fin, 0, &direction));
    CHECK(direction.ticks == 7 && direction.layers[1].lump == 21);
    CHECK(direction.layers[2].sprite_name[0] == '\0');
    CHECK(DC_FINFrame(&fin, 1, &direction));
    CHECK(direction.ticks == 11 && direction.layers[0].lump == 22);
    CHECK(direction.layers[1].sprite_name[0] == '\0');
    CHECK(!DC_FINFrame(&fin, 2, &direction));
    CHECK(!DC_FINFrame(&fin, -1, &direction));
    DC_FreeFIN(&fin);
    CHECK(strcmp(direction.layers[0].sprite_name, "EXTERNAL") == 0);
    CHECK(ivec2_equal(direction.layers[0].offset, (ivec2_t){ -159, 28 }));
    CHECK(direction.layers[0].remap == 7 && direction.layers[0].intensity == 16);
    CHECK(direction.layers[0].layer == 5 && direction.layers[0].flags == 1);
    free(direction.layers);
    direction = (spritedirection_t){0};

    CHECK(!load(bytes, SIZE - 1, &fin));
    CHECK(!fin.file.bytes && fin.file.size == 0);
    CHECK(!load(bytes, FRAMES - 1, &fin));
    u16(bytes + FRAMES, 4);
    CHECK(!load(bytes, SIZE, &fin));
    u16(bytes + FRAMES, 2);
    u16(bytes + COMMANDS + 14, 256);
    CHECK(load(bytes, SIZE, &fin));
    CHECK(!DC_FINFrame(&fin, 0, &direction));
    CHECK(!direction.layers);
    DC_FreeFIN(&fin);
    puts("PASS: FIN direct layers, lifetime, fixed names, and malformed spans");
    return 0;
}
