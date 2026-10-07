#include "w2_local.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* STRDAT.WAR and SNDDAT.WAR carry retail dialog text. A string table is a u16
 * count, that many u16 offsets, then NUL-terminated CP866 strings. String 0
 * names the dialog. A string that carries a highlight (0x04 starts it, 0x01
 * ends it) or a control byte starts with the hotkey byte, which is dropped:
 * the menus keep their own keys and use only the span to highlight. */

enum { MAX_ENTRIES = 128, MAX_STRINGS = 96 };

static w2_archive_t archives[2];
static struct { int count; const uint8_t *text[MAX_STRINGS]; } dialogs[2][MAX_ENTRIES];
static w2_blob_t blobs[2][MAX_ENTRIES];

bool w2_strings_load(const char *root) {
    w2_strings_free();
    char path[1100];
    snprintf(path, sizeof(path), "%s/DATA/STRDAT.WAR", root && root[0] ? root : "data/WAR2");
    if (!w2_archive_open(&archives[0], path)) return false;
    snprintf(path, sizeof(path), "%s/DATA/SNDDAT.WAR", root && root[0] ? root : "data/WAR2");
    if (!w2_archive_open(&archives[1], path)) {
        w2_strings_free();
        return false;
    }
    return true;
}

void w2_strings_free(void) {
    for (int bank = 0; bank < 2; ++bank) {
        for (int e = 0; e < MAX_ENTRIES; ++e) {
            w2_blob_free(&blobs[bank][e]);
            dialogs[bank][e].count = 0;
        }
        w2_archive_close(&archives[bank]);
    }
}

bool w2_label(int entry, int index, w2_text_t *out) {
    if (entry < 0 || entry >= MAX_ENTRIES) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    return w2_resource_label(4000 + entry, index, out);
}

bool w2_resource_label(int resource, int index, w2_text_t *out) {
    memset(out, 0, sizeof(*out));
    int bank = resource / 1000 == 4 ? 0 : resource / 1000 == 2 ? 1 : -1;
    int entry = resource % 1000;
    if (bank < 0 || entry < 0 || entry >= MAX_ENTRIES || index < 0) return false;
    w2_blob_t *blob = &blobs[bank][entry];
    if (!blob->data) {
        if (!w2_archive_extract(&archives[bank], entry, blob) || blob->size < 2) return false;
        const uint8_t *data = blob->data;
        int count = read_u16_le(data);
        if (count > MAX_STRINGS || blob->size < 2 + (size_t)count * 2) return false;
        for (int i = 0; i < count; ++i) {
            size_t at = read_u16_le(data + 2 + i * 2);
            if (at >= blob->size || !memchr(data + at, 0, blob->size - at)) break;
            dialogs[bank][entry].text[i] = data + at;
            ++dialogs[bank][entry].count;
        }
    }
    if (index >= dialogs[bank][entry].count) return false;
    const uint8_t *s = dialogs[bank][entry].text[index];
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
