/*
 * dc_spr_extract — extract all Dark Colony SPR files, one BMP sheet per file.
 *
 * Usage: dc_spr_extract <data_root> <output_dir>
 *   e.g. dc_spr_extract data/DCOLONY /tmp/dc_sprites
 *
 * Each cell is rendered into a grid with its index drawn in yellow in the
 * top-left corner. Transparent pixels show a dark checkerboard background.
 * Output is one 32-bit BGRA BMP per SPR, named after the sprite (e.g.
 * SPRITES_TRSC.bmp, INTRFACE_DCSS.bmp).
 */

#define _DEFAULT_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ── helpers ────────────────────────────────────────────────────────────── */

static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static uint16_t u16le(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t u32le(const uint8_t *p) {
    return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) |
                      ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static uint8_t *read_file(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0 || sz <= 0) { fclose(f); return NULL; }
    uint8_t *data = malloc((size_t)sz);
    if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        free(data); fclose(f); return NULL;
    }
    fclose(f);
    *size_out = (size_t)sz;
    return data;
}

static void mkdir_p(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* ── SPR / Juice ─────────────────────────────────────────────────────────── */

typedef struct {
    uint16_t width, height, dis_x, dis_y;
    uint32_t data_size;
    uint8_t *data;
} Cell;

typedef struct {
    uint16_t cell_count;
    bool chunked;
    uint32_t palette[256]; /* ARGB8888, index 0 = transparent */
    Cell *cells;
} Juice;

static void juice_free(Juice *j) {
    if (!j) return;
    for (int i = 0; i < j->cell_count; i++) free(j->cells[i].data);
    free(j->cells);
    j->cells = NULL;
    j->cell_count = 0;
}

static bool juice_load(const char *path, Juice *out) {
    memset(out, 0, sizeof(*out));
    size_t size = 0;
    uint8_t *b = read_file(path, &size);
    if (!b || size < 8 + 256 * 3) { free(b); return false; }

    uint16_t flags = u16le(b + 0);
    out->cell_count = u16le(b + 2);
    out->chunked = (flags & 0x180) != 0;
    if (out->cell_count == 0 || out->cell_count > 1024) { free(b); return false; }

    const uint8_t *p = b + 8;
    for (int i = 0; i < 256; i++) {
        int r  = clamp255((int)p[i*3+0] * 4 + 3);
        int g  = clamp255((int)p[i*3+1] * 4 + 3);
        int bv = clamp255((int)p[i*3+2] * 4 + 3);
        out->palette[i] = i == 0 ? 0u :
            (0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bv);
    }

    size_t desc_off = 8 + 256 * 3;
    size_t data_off = desc_off + (size_t)out->cell_count * 8;
    if (data_off > size) { free(b); return false; }

    out->cells = calloc(out->cell_count, sizeof(*out->cells));
    if (!out->cells) { free(b); return false; }

    for (int i = 0; i < out->cell_count; i++) {
        const uint8_t *d = b + desc_off + (size_t)i * 8;
        out->cells[i].width  = u16le(d + 0);
        out->cells[i].height = u16le(d + 2);
        out->cells[i].dis_x  = u16le(d + 4);
        out->cells[i].dis_y  = u16le(d + 6);
        if (out->cells[i].width > 512 || out->cells[i].height > 512) {
            juice_free(out); free(b); return false;
        }
    }

    size_t pos = data_off;
    if (out->chunked) {
        for (int i = 0; i < out->cell_count; i++) {
            if (pos + 4 > size) { juice_free(out); free(b); return false; }
            uint32_t chunk_len = u32le(b + pos); pos += 4;
            if (pos + chunk_len > size) { juice_free(out); free(b); return false; }
            out->cells[i].data_size = chunk_len;
            out->cells[i].data = malloc(chunk_len > 0 ? chunk_len : 1);
            if (!out->cells[i].data) { juice_free(out); free(b); return false; }
            if (chunk_len > 0) memcpy(out->cells[i].data, b + pos, chunk_len);
            pos += chunk_len;
        }
    } else {
        for (int i = 0; i < out->cell_count; i++) {
            uint32_t n = (uint32_t)out->cells[i].width * out->cells[i].height;
            out->cells[i].data_size = n;
            if (n == 0) continue;
            if (pos + n > size) { juice_free(out); free(b); return false; }
            out->cells[i].data = malloc(n);
            if (!out->cells[i].data) { juice_free(out); free(b); return false; }
            memcpy(out->cells[i].data, b + pos, n);
            pos += n;
        }
    }
    free(b);
    return true;
}

/* ── 4×6 pixel digit font (bit 3 = leftmost pixel per row) ─────────────── */

static const uint8_t FONT4x6[10][6] = {
    {0x6, 0x9, 0x9, 0x9, 0x9, 0x6}, /* 0 */
    {0x4, 0xc, 0x4, 0x4, 0x4, 0xe}, /* 1 */
    {0x6, 0x9, 0x1, 0x2, 0x4, 0xf}, /* 2 */
    {0xe, 0x1, 0x6, 0x1, 0x1, 0xe}, /* 3 */
    {0x3, 0x5, 0x9, 0xf, 0x1, 0x1}, /* 4 */
    {0xf, 0x8, 0xe, 0x1, 0x1, 0xe}, /* 5 */
    {0x3, 0x4, 0xe, 0x9, 0x9, 0x6}, /* 6 */
    {0xf, 0x1, 0x2, 0x4, 0x4, 0x4}, /* 7 */
    {0x6, 0x9, 0x6, 0x9, 0x9, 0x6}, /* 8 */
    {0x6, 0x9, 0x9, 0x7, 0x1, 0x6}, /* 9 */
};

static void draw_digit(uint32_t *px, int stride, int x0, int y0, int digit, uint32_t color) {
    const uint8_t *g = FONT4x6[digit];
    for (int row = 0; row < 6; row++)
        for (int col = 0; col < 4; col++)
            if (g[row] & (0x8u >> col))
                px[(y0 + row) * stride + (x0 + col)] = color;
}

static void draw_number(uint32_t *px, int stride, int x0, int y0, int n, uint32_t color) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", n);
    for (int i = 0; buf[i]; i++)
        draw_digit(px, stride, x0 + i * 5, y0, buf[i] - '0', color);
}

/* ── BMP writer (32-bit BGRA, top-down via negative biHeight) ───────────── */

static void w16(FILE *f, uint16_t v) {
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    fwrite(b, 1, 2, f);
}
static void w32(FILE *f, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v>>8), (uint8_t)(v>>16), (uint8_t)(v>>24)};
    fwrite(b, 1, 4, f);
}

static bool write_bmp(const char *path, const uint32_t *argb, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint32_t pix_off = 14 + 108;
    uint32_t row_bytes = (uint32_t)w * 4;
    uint32_t pix_size = row_bytes * (uint32_t)h;
    uint32_t file_size = pix_off + pix_size;

    /* BITMAPFILEHEADER */
    fwrite("BM", 1, 2, f);
    w32(f, file_size);
    w16(f, 0); w16(f, 0);
    w32(f, pix_off);

    /* BITMAPV4HEADER (108 bytes) */
    w32(f, 108);
    w32(f, (uint32_t)w);
    w32(f, (uint32_t)(-(int32_t)h)); /* negative = top-down */
    w16(f, 1);  w16(f, 32);
    w32(f, 3);  /* BI_BITFIELDS */
    w32(f, pix_size);
    w32(f, 2835); w32(f, 2835);
    w32(f, 0); w32(f, 0);
    w32(f, 0x00FF0000u); /* R mask */
    w32(f, 0x0000FF00u); /* G mask */
    w32(f, 0x000000FFu); /* B mask */
    w32(f, 0xFF000000u); /* A mask */
    w32(f, 0x42475273u); /* CSType = sRGB */
    for (int i = 0; i < 12; i++) w32(f, 0); /* endpoints + gammas */

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t p = argb[(size_t)y * w + x];
            uint8_t px[4] = {(uint8_t)p, (uint8_t)(p>>8), (uint8_t)(p>>16), (uint8_t)(p>>24)};
            fwrite(px, 1, 4, f);
        }
    }
    fclose(f);
    return true;
}

/* ── sheet renderer ──────────────────────────────────────────────────────── */

#define MARGIN   1
#define LABEL_H  8   /* 6 font rows + 1 top pad + 1 bottom pad */

static uint32_t checker(int x, int y) {
    return (((x >> 3) + (y >> 3)) & 1) ? 0xFF404040u : 0xFF303030u;
}

static bool render_sheet(const Juice *j, const char *out_path) {
    int n = j->cell_count;
    int max_w = 1, max_h = 1;
    for (int i = 0; i < n; i++) {
        if (j->cells[i].width  > max_w) max_w = j->cells[i].width;
        if (j->cells[i].height > max_h) max_h = j->cells[i].height;
    }

    int cell_w = max_w + MARGIN;
    int cell_h = max_h + LABEL_H + MARGIN;
    int cols = (int)ceil(sqrt((double)n));
    int rows = (n + cols - 1) / cols;
    int img_w = cols * cell_w + MARGIN;
    int img_h = rows * cell_h + MARGIN;

    uint32_t *pixels = calloc((size_t)img_w * (size_t)img_h, sizeof(uint32_t));
    if (!pixels) return false;

    /* dark grid background */
    for (int i = 0; i < img_w * img_h; i++)
        pixels[i] = 0xFF1A1A1Au;

    for (int ci = 0; ci < n; ci++) {
        const Cell *cell = &j->cells[ci];
        int col = ci % cols;
        int row = ci / cols;
        int ox = MARGIN + col * cell_w;
        int oy = MARGIN + row * cell_h;

        /* checkerboard background for the sprite area */
        for (int y = 0; y < max_h; y++)
            for (int x = 0; x < max_w; x++)
                pixels[(size_t)(oy + LABEL_H + y) * img_w + (ox + x)] = checker(x, y);

        int w = cell->width, h = cell->height;

        if (w > 0 && h > 0 && cell->data) {
            if (j->chunked) {
                const uint8_t *src = cell->data;
                size_t pos = 0;
                int written = 0, total = w * h;
                while (pos < cell->data_size && written < total) {
                    int8_t cmd = (int8_t)src[pos++];
                    if (cmd < 0) {
                        written += -(int)cmd;
                    } else {
                        int count = cmd + 1;
                        if (pos + (size_t)count > cell->data_size) break;
                        for (int p = 0; p < count && written < total; p++, written++) {
                            uint8_t idx = src[pos + p];
                            if (idx != 0) {
                                int px = written % w;
                                int py = written / w;
                                pixels[(size_t)(oy + LABEL_H + py) * img_w + (ox + px)] =
                                    j->palette[idx];
                            }
                        }
                        pos += (size_t)count;
                    }
                }
            } else {
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        uint8_t idx = cell->data[(size_t)y * w + x];
                        if (idx != 0)
                            pixels[(size_t)(oy + LABEL_H + y) * img_w + (ox + x)] =
                                j->palette[idx];
                    }
                }
            }
        }

        /* index label: black shadow then yellow text */
        draw_number(pixels, img_w, ox + 1, oy + 2, ci, 0xFF000000u);
        draw_number(pixels, img_w, ox,     oy + 1, ci, 0xFFFFFF00u);
    }

    bool ok = write_bmp(out_path, pixels, img_w, img_h);
    free(pixels);
    return ok;
}

/* ── directory scan ──────────────────────────────────────────────────────── */

static void process_spr(const char *spr_path, const char *out_dir,
                        const char *subdir, const char *filename) {
    Juice j;
    if (!juice_load(spr_path, &j)) {
        fprintf(stderr, "  skip (invalid): %s/%s\n", subdir, filename);
        return;
    }

    /* output: <out_dir>/<SUBDIR>/<stem>.bmp */
    char sub_out[1024];
    snprintf(sub_out, sizeof(sub_out), "%s/%s", out_dir, subdir);
    mkdir_p(sub_out);

    char stem[64];
    snprintf(stem, sizeof(stem), "%s", filename);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = '\0';

    char out_path[1024];
    snprintf(out_path, sizeof(out_path), "%s/%s.bmp", sub_out, stem);

    if (render_sheet(&j, out_path))
        printf("  %-40s  %4d cells\n", out_path + (int)(strlen(out_dir) + 1), j.cell_count);
    else
        fprintf(stderr, "  failed: %s\n", out_path);

    juice_free(&j);
}

static void scan_dir(const char *data_root, const char *subdir, const char *out_dir) {
    char dir_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", data_root, subdir);
    DIR *d = opendir(dir_path);
    if (!d) { fprintf(stderr, "cannot open %s\n", dir_path); return; }

    printf("[%s]\n", subdir);

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len < 5) continue;
        const char *ext = name + len - 4;
        if (strcasecmp(ext, ".SPR") != 0 && strcasecmp(ext, ".JUS") != 0) continue;

        char spr_path[1024];
        snprintf(spr_path, sizeof(spr_path), "%s/%s", dir_path, name);
        process_spr(spr_path, out_dir, subdir, name);
    }
    closedir(d);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: dc_spr_extract <data_root> <output_dir>\n");
        fprintf(stderr, "  e.g. dc_spr_extract data/DCOLONY /tmp/dc_sprites\n");
        return 1;
    }
    const char *data_root = argv[1];
    const char *out_dir = argv[2];
    mkdir_p(out_dir);

    static const char *const subdirs[] = {
        "SPRITES", "CURSOR", "ENCYCLO", "INTRFACE", NULL,
    };
    for (int i = 0; subdirs[i]; i++)
        scan_dir(data_root, subdirs[i], out_dir);

    return 0;
}
