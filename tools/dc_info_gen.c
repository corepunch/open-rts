/* Export native FIN animation ranges as multigen-style state rows. */
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char *path, const char *message) {
    fprintf(stderr, "dc_info_gen: %s: %s\n", path, message);
    exit(1);
}

static void skipped(const char *path, const char *message) {
    fprintf(stderr, "dc_info_gen: %s: %s (skipped)\n", path, message);
    printf("; SKIPPED %s: %s\n", path, message);
}

static unsigned word(const unsigned char *p) {
    return p[0] | (unsigned)p[1] << 8;
}

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) die(path, strerror(errno));
    if (fseek(file, 0, SEEK_END)) die(path, "seek failed");
    long length = ftell(file);
    if (length < 8) die(path, "missing FIN header");
    rewind(file);
    unsigned char *data = malloc((size_t)length);
    if (!data) die(path, "out of memory");
    if (fread(data, 1, (size_t)length, file) != (size_t)length)
        die(path, "read failed");
    if (fclose(file)) die(path, "close failed");
    *size = (size_t)length;
    return data;
}

static void symbol(char *out, const unsigned char *name, size_t length) {
    for (size_t i = 0; i < length; ++i)
        out[i] = isalnum(name[i]) ? (char)toupper(name[i]) : '_';
    out[length] = 0;
}

static void export_fin(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *extension = strrchr(base, '.');
    if (!extension || strlen(extension) != 4 ||
        toupper((unsigned char)extension[1]) != 'F' ||
        toupper((unsigned char)extension[2]) != 'I' ||
        toupper((unsigned char)extension[3]) != 'N' ||
        extension == base || extension - base > 8)
        die(path, "expected an eight-character-or-shorter FIN basename");
    char sprite[9];
    symbol(sprite, (const unsigned char *)base, (size_t)(extension - base));
    size_t size;
    unsigned char *data = read_file(path, &size);
    unsigned frames = word(data + 2), labels = word(data + 4);
    size_t label_offset = 8 + word(data + 6) * 8;
    size_t frame_offset = label_offset + labels * 20;
    if (frame_offset > size || frames > (size - frame_offset) / 164) {
        skipped(path, "unsupported or truncated FIN tables");
        free(data);
        return;
    }
    unsigned char *used = calloc(frames ? frames : 1, 1);
    if (!used) die(path, "out of memory");
    printf("\n; %s: %u animations, %u frames, default duration %u\n",
           base, labels, frames, word(data));
    for (unsigned i = 0; i < labels; ++i) {
        const unsigned char *label = data + label_offset + i * 20;
        unsigned first = word(label + 16), last = word(label + 18);
        if (first > last || last >= frames) {
            char message[96];
            snprintf(message, sizeof(message), "animation %u range %u..%u outside %u frames",
                     i, first, last, frames);
            skipped(path, message);
            continue;
        }
        size_t length = 0;
        while (length < 16 && label[length]) ++length;
        char name[17];
        symbol(name, label, length);
        printf("\n; animation %u: %.*s [%u..%u]\n", i, (int)length, label, first, last);
        for (unsigned frame = first; frame <= last; ++frame) {
            used[frame] = 1;
            printf("S_%s_%u_%s_%u %s %u %u NULL ", sprite, i, name,
                   frame - first, sprite, frame, word(data + frame_offset + frame * 164 + 2));
            if (frame < last)
                printf("S_%s_%u_%s_%u\n", sprite, i, name, frame - first + 1);
            else
                puts("S_NULL");
        }
    }
    for (unsigned frame = 0; frame < frames; ++frame) {
        if (!used[frame])
            printf("S_%s_FRAME_%u %s %u %u NULL S_NULL ; unlabeled\n",
                   sprite, frame, sprite, frame, word(data + frame_offset + frame * 164 + 2));
    }
    free(used);
    free(data);
}

int main(int argc, char **argv) {
    if (argc < 2) die("usage", "dc_info_gen ANIMATE/*.FIN > animations.txt");
    puts("; state sprite frame duration action nextstate\n"
         "; Numeric frame indices and durations are raw FIN values, not engine tics.\n"
         "; NULL actions and terminal S_NULL are export placeholders.\n"
         "; Labels are preserved verbatim; directions and layers are not interpreted.");
    for (int i = 1; i < argc; ++i) export_fin(argv[i]);
    if (fflush(stdout) || ferror(stdout)) die("stdout", "write failed");
    return 0;
}
