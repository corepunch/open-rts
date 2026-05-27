#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t offset;
    uint16_t length;
    char *text;
} AnimString;

static void die(const char *msg, const char *path) {
    if (path) {
        fprintf(stderr, "anim_extract: %s: %s\n", path, msg);
    } else {
        fprintf(stderr, "anim_extract: %s\n", msg);
    }
    exit(1);
}

static unsigned char *read_file(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) die(strerror(errno), path);

    if (fseek(f, 0, SEEK_END) != 0) die("failed to seek", path);
    long size = ftell(f);
    if (size < 0) die("failed to measure file size", path);
    if (fseek(f, 0, SEEK_SET) != 0) die("failed to rewind", path);

    unsigned char *data = malloc((size_t)size);
    if (!data) die("out of memory", path);
    if (size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        die("failed to read file", path);
    }
    fclose(f);
    *size_out = (size_t)size;
    return data;
}

static void sanitize_symbol(const char *path, char *out, size_t out_size) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t len = 0;
    for (const char *p = base; *p && *p != '.' && len + 1 < out_size; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (isalnum(ch)) {
            out[len++] = (char)tolower(ch);
        } else {
            out[len++] = '_';
        }
    }
    if (len == 0) {
        snprintf(out, out_size, "anim");
        return;
    }
    out[len] = '\0';
}

static void append_string(AnimString **items, size_t *count, size_t *cap,
                          uint32_t offset, const unsigned char *data, size_t len) {
    if (*count >= *cap) {
        size_t next_cap = *cap ? *cap * 2 : 64;
        AnimString *next = realloc(*items, next_cap * sizeof(*next));
        if (!next) die("out of memory while collecting strings", NULL);
        *items = next;
        *cap = next_cap;
    }
    AnimString *item = &(*items)[(*count)++];
    item->offset = offset;
    item->length = (uint16_t)len;
    item->text = malloc(len + 1);
    if (!item->text) die("out of memory while copying string", NULL);
    memcpy(item->text, data, len);
    item->text[len] = '\0';
}

static bool is_string_byte(unsigned char ch) {
    return ch >= 32 && ch <= 126;
}

static void collect_strings(const unsigned char *data, size_t size,
                            AnimString **items, size_t *count) {
    size_t cap = 0;
    size_t i = 0;
    while (i < size) {
        while (i < size && !is_string_byte(data[i])) ++i;
        size_t start = i;
        while (i < size && is_string_byte(data[i])) ++i;
        size_t len = i - start;
        if (len >= 4) {
            append_string(items, count, &cap, (uint32_t)start, data + start, len);
        }
    }
}

static void emit_c_string(FILE *out, const unsigned char *data, size_t len) {
    fputc('"', out);
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = data[i];
        switch (ch) {
        case '\\': fputs("\\\\", out); break;
        case '"': fputs("\\\"", out); break;
        case '\n': fputs("\\n", out); break;
        case '\r': fputs("\\r", out); break;
        case '\t': fputs("\\t", out); break;
        default:
            if (ch >= 32 && ch <= 126) {
                fputc((int)ch, out);
            } else {
                fprintf(out, "\\x%02x", ch);
            }
            break;
        }
    }
    fputc('"', out);
}

static void emit_bytes(FILE *out, const unsigned char *data, size_t size, const char *symbol) {
    fprintf(out, "static const unsigned char %s_bytes[%zu] = {\n", symbol, size);
    for (size_t i = 0; i < size; ++i) {
        if (i % 12 == 0) fputs("    ", out);
        fprintf(out, "0x%02x%s", data[i], i + 1 == size ? "" : ", ");
        if (i % 12 == 11 || i + 1 == size) fputc('\n', out);
    }
    fputs("};\n\n", out);
}

static void emit_strings(FILE *out, const AnimString *items, size_t count, const char *symbol) {
    fputs("typedef struct {\n", out);
    fputs("    uint32_t offset;\n", out);
    fputs("    uint16_t length;\n", out);
    fputs("    const char *text;\n", out);
    fputs("} AnimString;\n\n", out);

    fprintf(out, "static const AnimString %s_strings[%zu] = {\n", symbol, count);
    for (size_t i = 0; i < count; ++i) {
        fputs("    { ", out);
        fprintf(out, "%u, %u, ", items[i].offset, items[i].length);
        emit_c_string(out, (const unsigned char *)items[i].text, items[i].length);
        fputs(" },\n", out);
    }
    fputs("};\n\n", out);
    fprintf(out, "static const size_t %s_string_count = sizeof(%s_strings) / sizeof(%s_strings[0]);\n\n",
            symbol, symbol, symbol);
}

static void free_strings(AnimString *items, size_t count) {
    if (!items) return;
    for (size_t i = 0; i < count; ++i) {
        free(items[i].text);
    }
    free(items);
}

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s <input.fin> <output.c> [symbol]\n", argv[0]);
        return 1;
    }

    const char *input_path = argv[1];
    const char *output_path = argv[2];
    char symbol[128];
    if (argc >= 4) {
        snprintf(symbol, sizeof(symbol), "%s", argv[3]);
    } else {
        sanitize_symbol(input_path, symbol, sizeof(symbol));
    }

    size_t size = 0;
    unsigned char *data = read_file(input_path, &size);

    AnimString *strings = NULL;
    size_t string_count = 0;
    collect_strings(data, size, &strings, &string_count);

    FILE *out = fopen(output_path, "wb");
    if (!out) {
        free_strings(strings, string_count);
        free(data);
        die(strerror(errno), output_path);
    }

    fprintf(out, "/* Generated from %s. */\n", input_path);
    fputs("#include <stddef.h>\n#include <stdint.h>\n\n", out);
    fprintf(out, "static const size_t %s_size = %zu;\n\n", symbol, size);
    emit_bytes(out, data, size, symbol);
    emit_strings(out, strings, string_count, symbol);
    fputs("/* End of generated ANIM dump. */\n", out);

    fclose(out);
    free_strings(strings, string_count);
    free(data);

    printf("Wrote %s (%zu bytes, %zu strings) to %s\n", symbol, size, string_count, output_path);
    return 0;
}
