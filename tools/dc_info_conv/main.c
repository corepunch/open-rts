/* Inspect native FIN/SPR records without a renderer or a running game. */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int export_states(int argc, char **argv);

static void die(const char *path, const char *reason) {
    fprintf(stderr, "dc_info_conv: %s: %s\n", path, reason);
    exit(1);
}

static unsigned word(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t dword(const unsigned char *p) { return word(p) | (uint32_t)word(p + 2) << 16; }

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) die(path, strerror(errno));
    if (fseek(f, 0, SEEK_END)) die(path, "seek failed");
    long n = ftell(f);
    if (n < 0) die(path, "length unavailable");
    rewind(f);
    unsigned char *data = malloc(n ? (size_t)n : 1);
    if (!data) die(path, "out of memory");
    if (fread(data, 1, n, f) != (size_t)n || fclose(f)) die(path, "read failed");
    *size = n;
    return data;
}

static void string(const unsigned char *p, size_t n) {
    putchar('"');
    for (size_t i = 0; i < n && p[i]; ++i) {
        if (p[i] == '"' || p[i] == '\\') printf("\\%c", p[i]);
        else if (p[i] < 32 || p[i] >= 127) printf("\\u%04x", p[i]);
        else putchar(p[i]);
    }
    putchar('"');
}

static int number(const char *s) {
    char *end;
    errno = 0;
    long n = strtol(s, &end, 10);
    if (errno || !*s || *end || n < 0 || n > INT32_MAX) die(s, "expected a nonnegative index");
    return (int)n;
}

typedef struct {
    unsigned char *data;
    const unsigned char *labels, *frames, *commands;
    unsigned frame_count, label_count, dependency_count;
    size_t *command_offsets;
} fin_t;

static int load_fin(const char *path, fin_t *fin) {
    size_t size;
    *fin = (fin_t){0};
    fin->data = read_file(path, &size);
    if (size < 8) return 0;
    fin->frame_count = word(fin->data + 2);
    fin->label_count = word(fin->data + 4);
    fin->dependency_count = word(fin->data + 6);
    size_t lo = 8 + fin->dependency_count * 8;
    size_t fo = lo + fin->label_count * 20;
    size_t co = fo + fin->frame_count * 164;
    if (co > size || (size - co) % 22) return 0;
    fin->labels = fin->data + lo;
    fin->frames = fin->data + fo;
    fin->commands = fin->data + co;
    fin->command_offsets = calloc(fin->frame_count + 1, sizeof(size_t));
    if (!fin->command_offsets) die(path, "out of memory");
    size_t used = 0;
    for (unsigned i = 0; i < fin->frame_count; ++i) {
        fin->command_offsets[i] = used;
        used += word(fin->frames + i * 164) * 22;
        if (used > size - co) return 0;
    }
    for (unsigned i = 0; i < fin->label_count; ++i) {
        const unsigned char *label = fin->labels + i * 20;
        if (word(label + 16) > word(label + 18) || word(label + 18) >= fin->frame_count)
            return 0;
    }
    return 1;
}

static void free_fin(fin_t *fin) {
    free(fin->command_offsets);
    free(fin->data);
}

static void frame_json(const fin_t *fin, unsigned index) {
    const unsigned char *frame = fin->frames + index * 164;
    unsigned ticks = word(frame + 2), parts = word(frame);
    printf("{\"index\":%u,\"ticks\":%u,\"native_ticks\":%u,\"unknown_04\":\"",
           index, ticks, ((ticks ? ticks : 15) + 3) * 15 / 100);
    for (int i = 4; i < 164; ++i) printf("%02x", frame[i]);
    printf("\",\"commands\":[");
    for (unsigned i = 0; i < parts; ++i) {
        const unsigned char *p = fin->commands + fin->command_offsets[index] + i * 22;
        if (i) putchar(',');
        printf("{\"sprite\":"); string(p, 8);
        printf(",\"cell\":%d,\"offset\":[%d,%d],\"remap\":%d,\"intensity\":%d,\"layer\":%d,\"flags\":%d}",
            (int16_t)word(p + 8), (int16_t)word(p + 10), (int16_t)word(p + 12),
            (int16_t)word(p + 14), (int16_t)word(p + 16), (int16_t)word(p + 18), (int16_t)word(p + 20));
    }
    printf("]}");
}

static void inspect_fin(const char *path, const char *label_name, int index, int labels_only) {
    fin_t fin;
    if (!load_fin(path, &fin)) die(path, "invalid, truncated, or unsupported FIN (expected 22-byte commands)");
    unsigned first = 0, last = fin.frame_count;
    if (index >= 0) {
        if ((unsigned)index >= last) die(path, "frame out of range");
        first = index; last = first + 1;
    }
    if (label_name) {
        unsigned i;
        for (i = 0; i < fin.label_count; ++i) {
            const unsigned char *p = fin.labels + i * 20;
            if (strlen(label_name) <= 16 && !strncmp((const char *)p, label_name, 16)) {
                first = word(p + 16); last = word(p + 18) + 1; break;
            }
        }
        if (i == fin.label_count) die(path, "label not found");
    }
    printf("{\"format\":\"FIN\",\"header_word\":%u,\"frame_count\":%u,\"label_count\":%u,\"dependencies\":[",
           word(fin.data), fin.frame_count, fin.label_count);
    for (unsigned i = 0; i < fin.dependency_count; ++i) {
        if (i) putchar(',');
        string(fin.data + 8 + i * 8, 8);
    }
    printf("],\"labels\":[");
    for (unsigned i = 0; i < fin.label_count; ++i) {
        const unsigned char *p = fin.labels + i * 20;
        if (i) putchar(',');
        printf("{\"name\":"); string(p, 16);
        printf(",\"start\":%u,\"end\":%u}", word(p + 16), word(p + 18));
    }
    printf("],\"frames\":[");
    if (!labels_only) for (unsigned i = first; i < last; ++i) {
        if (i != first) putchar(',');
        frame_json(&fin, i);
    }
    puts("]}");
    free_fin(&fin);
}

static void inspect_spr(const char *path, int index) {
    size_t size;
    unsigned char *data = read_file(path, &size);
    if (size < 776) die(path, "missing SPR header");
    unsigned count = word(data + 2);
    if (776 + count * 8 > size) die(path, "truncated SPR cells");
    if (index >= 0 && (unsigned)index >= count) die(path, "cell out of range");
    printf("{\"format\":\"SPR\",\"flags\":%u,\"cell_count\":%u,\"payload_size\":%u,\"palette\":[",
           word(data), count, dword(data + 4));
    for (unsigned i = 0; i < 256; ++i) {
        const unsigned char *p = data + 8 + i * 3;
        printf("%s[%u,%u,%u]", i ? "," : "", p[0], p[1], p[2]);
    }
    printf("],\"cells\":[");
    unsigned first = index < 0 ? 0 : (unsigned)index;
    unsigned last = index < 0 ? count : first + 1;
    for (unsigned i = first; i < last; ++i) {
        const unsigned char *p = data + 776 + i * 8;
        printf("%s{\"index\":%u,\"size\":[%u,%u],\"displacement\":[%u,%u]}",
               i == first ? "" : ",", i, word(p), word(p + 2), word(p + 4), word(p + 6));
    }
    puts("]}");
    free(data);
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        puts("dc_info_conv [--labels | --label NAME | --frame N | --cell N] FILE\n"
             "  JSON: FIN header/dependencies/labels/commands, SPR header/palette/cell geometry.\n"
             "dc_info_conv --states FILE.FIN...   Export raw FIN state rows.");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "--states")) return export_states(argc - 1, argv + 1);
    const char *label = NULL, *path = NULL;
    int frame = -1, cell = -1, labels = 0;
    if (argc == 2) path = argv[1];
    else if (argc == 3 && !strcmp(argv[1], "--labels")) { labels = 1; path = argv[2]; }
    else if (argc == 4) {
        if (!strcmp(argv[1], "--label")) label = argv[2];
        else if (!strcmp(argv[1], "--frame")) frame = number(argv[2]);
        else if (!strcmp(argv[1], "--cell")) cell = number(argv[2]);
        else die(argv[1], "unknown option; use --help");
        path = argv[3];
    }
    if (!path) die("usage", "dc_info_conv --help");
    const char *ext = strrchr(path, '.');
    if (ext && !strcasecmp(ext, ".FIN") && cell < 0) inspect_fin(path, label, frame, labels);
    else if (ext && !strcasecmp(ext, ".SPR") && !label && frame < 0 && !labels) inspect_spr(path, cell);
    else die(path, "expected FIN or SPR with a matching inspection option");
    if (fflush(stdout) || ferror(stdout)) die("stdout", "write failed");
    return 0;
}
