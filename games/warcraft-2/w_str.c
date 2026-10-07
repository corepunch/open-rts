#include "w2_local.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* STRDAT.WAR is the retail dialog text. Entry n is a string table: a u16
 * count, that many u16 offsets, then NUL-terminated CP866 strings. String 0
 * names the dialog. A string that carries a highlight (0x04 starts it, 0x01
 * ends it) or a control byte starts with the hotkey byte, which is dropped:
 * the menus keep their own keys and use only the span to highlight. */

enum { MAX_ENTRIES = 128, MAX_STRINGS = 96 };

static w2_archive_t archive;
static bool loaded;
static struct { int count; const uint8_t *text[MAX_STRINGS]; } dialogs[MAX_ENTRIES];
static w2_blob_t blobs[MAX_ENTRIES];

bool w2_strings_load(const char *root) {
    w2_strings_free();
    char path[1100];
    snprintf(path, sizeof(path), "%s/DATA/STRDAT.WAR", root && root[0] ? root : "data/WAR2");
    if (!w2_archive_open(&archive, path)) return false;
    loaded = true;
    for (int e = 0; e < archive.count && e < MAX_ENTRIES; ++e) {
        if (!w2_archive_extract(&archive, e, &blobs[e])) continue;
        const uint8_t *data = blobs[e].data;
        size_t size = blobs[e].size;
        if (size < 2) continue;
        int count = data[0] | data[1] << 8;
        if (count > MAX_STRINGS || size < 2 + (size_t)count * 2) continue;
        int good = 0;
        for (int i = 0; i < count; ++i) {
            size_t at = data[2 + i * 2] | data[3 + i * 2] << 8;
            if (at >= size || !memchr(data + at, 0, size - at)) break;
            dialogs[e].text[i] = data + at;
            ++good;
        }
        dialogs[e].count = good;
    }
    return true;
}

void w2_strings_free(void) {
    for (int e = 0; e < MAX_ENTRIES; ++e) {
        w2_blob_free(&blobs[e]);
        dialogs[e].count = 0;
    }
    if (loaded) w2_archive_close(&archive);
    loaded = false;
}

bool w2_label(int entry, int index, w2_text_t *out) {
    memset(out, 0, sizeof(*out));
    if (entry < 0 || entry >= MAX_ENTRIES || index < 0 || index >= dialogs[entry].count) return false;
    const uint8_t *s = dialogs[entry].text[index];
    bool marked = strchr((const char *)s, 4) != NULL;
    if (marked || (s[0] && s[0] < 0x20)) ++s;
    size_t n = 0;
    out->mark_at = 0;
    for (; *s && n + 1 < sizeof(out->text); ++s) {
        if (*s == 4) out->mark_at = (int)n;
        else if (*s == 1) out->mark_len = (int)n - out->mark_at;
        else if (*s == '\t') for (int k = 0; k < 4 && n + 1 < sizeof(out->text); ++k) out->text[n++] = ' ';
        else if (*s >= 0x20 || *s == '\n') out->text[n++] = (char)*s;
    }
    out->text[n] = '\0';
    if (out->mark_len < 0) out->mark_len = 0;
    return n > 0;
}

/* Several strings of one entry as one text, a line each. */
size_t w2_label_lines(int entry, int first, int last, char *out, size_t size) {
    size_t n = 0;
    for (int i = first; i <= last && n + 2 < size; ++i) {
        w2_text_t line;
        if (!w2_label(entry, i, &line)) continue;
        n += (size_t)snprintf(out + n, size - n, "%s\n", line.text);
        if (n >= size) { n = size - 1; break; }
    }
    if (n < size) out[n] = '\0';
    return n;
}
