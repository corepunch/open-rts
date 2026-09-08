#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    FIN_HEADER_SIZE = 8,
    FIN_DEPENDENCY_SIZE = 8,
    FIN_LABEL_SIZE = 20,
    FIN_FRAME_SIZE = 164,
    FIN_DRAW_PART_SIZE = 22,
    FIN_LABEL_NAME_SIZE = 16,
    FIN_SPRITE_NAME_SIZE = 8,
    FIN_MAX_PARTS = 100
};

typedef struct {
    unsigned char *data;
    size_t size;
    const char *path;
    uint16_t default_ticks;
    uint16_t frame_count;
    uint16_t label_count;
    uint16_t dependency_count;
    size_t label_offset;
    size_t frame_offset;
    size_t draw_part_offset;
    size_t draw_part_count;
    size_t draw_part_size;
    bool parsed;
} FinFile;

static void die(const char *message, const char *path) {
    fprintf(stderr, "dc_fin_extract: %s: %s\n", path, message);
    exit(1);
}

static unsigned char *read_file(const char *path, size_t *size_out) {
    FILE *file = fopen(path, "rb");
    if (!file) die(strerror(errno), path);
    if (fseek(file, 0, SEEK_END) != 0) die("seek failed", path);
    long size = ftell(file);
    if (size < 0) die("ftell failed", path);
    if (fseek(file, 0, SEEK_SET) != 0) die("rewind failed", path);
    unsigned char *data = malloc((size_t)size);
    if (!data) die("out of memory", path);
    if (size > 0 && fread(data, 1, (size_t)size, file) != (size_t)size)
        die("read failed", path);
    fclose(file);
    *size_out = (size_t)size;
    return data;
}

static uint16_t read_u16(const unsigned char *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int16_t read_i16(const unsigned char *data) {
    return (int16_t)read_u16(data);
}

static void copy_string(char *out, size_t out_size, const unsigned char *data,
                        size_t data_size) {
    size_t length = 0;
    while (length + 1 < out_size && length < data_size && data[length] != '\0') {
        out[length] = (char)data[length];
        length++;
    }
    out[length] = '\0';
}

static void json_string(FILE *out, const char *value) {
    fputc('"', out);
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor; ++cursor) {
        switch (*cursor) {
        case '\\': fputs("\\\\", out); break;
        case '"': fputs("\\\"", out); break;
        case '\n': fputs("\\n", out); break;
        case '\r': fputs("\\r", out); break;
        case '\t': fputs("\\t", out); break;
        default:
            if (*cursor >= 32 && *cursor <= 126) fputc(*cursor, out);
            else fprintf(out, "\\u%04x", *cursor);
            break;
        }
    }
    fputc('"', out);
}

static void json_indent(FILE *out, int depth) {
    for (int index = 0; index < depth; ++index) fputs("  ", out);
}

static FinFile load_fin(const char *path) {
    FinFile fin;
    memset(&fin, 0, sizeof(fin));
    fin.path = path;
    fin.data = read_file(path, &fin.size);
    if (fin.size < FIN_HEADER_SIZE) die("FIN too small", path);

    fin.default_ticks = read_u16(fin.data + 0);
    fin.frame_count = read_u16(fin.data + 2);
    fin.label_count = read_u16(fin.data + 4);
    fin.dependency_count = read_u16(fin.data + 6);
    fin.label_offset = FIN_HEADER_SIZE + (size_t)fin.dependency_count * FIN_DEPENDENCY_SIZE;
    fin.frame_offset = fin.label_offset + (size_t)fin.label_count * FIN_LABEL_SIZE;
    fin.draw_part_offset = fin.frame_offset + (size_t)fin.frame_count * FIN_FRAME_SIZE;
    if (fin.label_offset > fin.size || fin.frame_offset > fin.size ||
        fin.draw_part_offset > fin.size) {
        return fin;
    }
    if ((fin.size - fin.draw_part_offset) % FIN_DRAW_PART_SIZE == 0) {
        fin.draw_part_size = FIN_DRAW_PART_SIZE;
    } else if ((fin.size - fin.draw_part_offset) % 20 == 0) {
        fin.draw_part_size = 20;
    } else {
        return fin;
    }
    fin.draw_part_count = (fin.size - fin.draw_part_offset) / fin.draw_part_size;
    fin.parsed = true;
    return fin;
}

static int frame_part_start(const FinFile *fin, int frame_index) {
    int start = 0;
    for (int index = 0; index < frame_index; ++index)
        start += read_u16(fin->data + fin->frame_offset + (size_t)index * FIN_FRAME_SIZE);
    return start;
}

static void write_draw_part(FILE *out, const unsigned char *data, int part_index,
                            int depth, bool has_flags) {
    char sprite[FIN_SPRITE_NAME_SIZE + 1];
    copy_string(sprite, sizeof(sprite), data, FIN_SPRITE_NAME_SIZE);
    json_indent(out, depth);
    fputs("{\n", out);
    json_indent(out, depth + 1); fputs("\"index\": ", out); fprintf(out, "%d,\n", part_index);
    json_indent(out, depth + 1); fputs("\"sprite\": ", out); json_string(out, sprite); fputs(",\n", out);
    json_indent(out, depth + 1); fputs("\"sprite_frame\": ", out); fprintf(out, "%d,\n", read_i16(data + 8));
    json_indent(out, depth + 1); fputs("\"x\": ", out); fprintf(out, "%d,\n", read_i16(data + 10));
    json_indent(out, depth + 1); fputs("\"y\": ", out); fprintf(out, "%d,\n", read_i16(data + 12));
    json_indent(out, depth + 1); fputs("\"remap\": ", out); fprintf(out, "%d,\n", read_i16(data + 14));
    json_indent(out, depth + 1); fputs("\"intensity\": ", out); fprintf(out, "%d,\n", read_i16(data + 16));
    json_indent(out, depth + 1); fputs("\"layer\": ", out); fprintf(out, "%d,\n", read_i16(data + 18));
    json_indent(out, depth + 1); fputs("\"flags\": ", out);
    if (has_flags) fprintf(out, "%d\n", read_i16(data + 20));
    else fputs("null\n", out);
    json_indent(out, depth); fputc('}', out);
}

static void write_json(FILE *out, const FinFile *fin) {
    if (!fin->parsed) {
        fprintf(out, "{\n  \"format\": \"Dark Colony FIN blob\",\n  \"source\": ");
        json_string(out, fin->path);
        fprintf(out, ",\n  \"parsed\": false,\n  \"size\": %zu,\n  \"header_u16\": [%u, %u, %u, %u],\n  \"raw_hex\": \"",
                fin->size, fin->default_ticks, fin->frame_count,
                fin->label_count, fin->dependency_count);
        for (size_t index = 0; index < fin->size; ++index)
            fprintf(out, "%02x", fin->data[index]);
        fputs("\"\n}\n", out);
        return;
    }
    fprintf(out, "{\n");
    fputs("  \"format\": \"Dark Colony FIN\",\n  \"source\": ", out);
    json_string(out, fin->path);
    fprintf(out, ",\n  \"default_ticks\": %u,\n  \"frame_count\": %u,\n",
            fin->default_ticks, fin->frame_count);
    fprintf(out, "  \"label_count\": %u,\n  \"dependency_count\": %u,\n",
            fin->label_count, fin->dependency_count);
        fprintf(out, "  \"draw_part_size\": %zu,\n  \"draw_part_count\": %zu,\n  \"labels\": [\n",
            fin->draw_part_size, fin->draw_part_count);
    for (int label_index = 0; label_index < fin->label_count; ++label_index) {
        const unsigned char *data = fin->data + fin->label_offset + (size_t)label_index * FIN_LABEL_SIZE;
        char name[FIN_LABEL_NAME_SIZE + 1];
        copy_string(name, sizeof(name), data, FIN_LABEL_NAME_SIZE);
        json_indent(out, 2); fputs("{\n", out);
        json_indent(out, 3); fputs("\"index\": ", out); fprintf(out, "%d,\n", label_index);
        json_indent(out, 3); fputs("\"name\": ", out); json_string(out, name); fputs(",\n", out);
        json_indent(out, 3); fputs("\"start\": ", out); fprintf(out, "%u,\n", read_u16(data + 16));
        json_indent(out, 3); fputs("\"end\": ", out); fprintf(out, "%u,\n", read_u16(data + 18));
        json_indent(out, 3); fputs("\"frames\": [\n", out);
        int start = read_u16(data + 16);
        int end = read_u16(data + 18);
        for (int frame_index = start; frame_index <= end && frame_index < fin->frame_count; ++frame_index) {
            const unsigned char *frame = fin->data + fin->frame_offset + (size_t)frame_index * FIN_FRAME_SIZE;
            int part_count = read_u16(frame);
            if (part_count > FIN_MAX_PARTS) die("FIN frame has invalid part count", fin->path);
            int part_start = frame_part_start(fin, frame_index);
            json_indent(out, 4); fputs("{\n", out);
            json_indent(out, 5); fputs("\"index\": ", out); fprintf(out, "%d,\n", frame_index);
            json_indent(out, 5); fputs("\"ticks\": ", out); fprintf(out, "%u,\n", read_u16(frame + 2));
            json_indent(out, 5); fputs("\"parts\": [\n", out);
            for (int part = 0; part < part_count; ++part) {
                int part_index = part_start + part;
                if (part_index >= (int)fin->draw_part_count) die("FIN part index out of range", fin->path);
                write_draw_part(out, fin->data + fin->draw_part_offset +
                                (size_t)part_index * fin->draw_part_size,
                                part_index, 6, fin->draw_part_size >= FIN_DRAW_PART_SIZE);
                if (part + 1 < part_count) fputc(',', out);
                fputc('\n', out);
            }
            json_indent(out, 5); fputs("]\n", out);
            json_indent(out, 4); fputc('}', out);
            if (frame_index < end && frame_index + 1 < fin->frame_count) fputc(',', out);
            fputc('\n', out);
        }
        json_indent(out, 3); fputs("]\n", out);
        json_indent(out, 2); fputc('}', out);
        if (label_index + 1 < fin->label_count) fputc(',', out);
        fputc('\n', out);
    }
    fputs("  ]\n}\n", out);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input.fin> <output.json>\n", argv[0]);
        return 1;
    }
    FinFile fin = load_fin(argv[1]);
    FILE *output = fopen(argv[2], "wb");
    if (!output) die(strerror(errno), argv[2]);
    write_json(output, &fin);
    fclose(output);
    free(fin.data);
        printf("Wrote %s (%s, %u labels, %u frames, %zu draw parts) to %s\n",
            argv[1], fin.parsed ? "parsed" : "raw", fin.label_count,
            fin.frame_count, fin.draw_part_count, argv[2]);
    return 0;
}