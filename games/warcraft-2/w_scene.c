#include "w2_local.h"

#include <stdio.h>
#include <string.h>

enum { RECORD_SIZE = 72 };

bool w2_decode_scene(const w2_blob_t *blob, menuitem_t *items, int capacity, int *count) {
    *count = 0;
    if (!blob || !blob->data || !items || capacity < 1 || blob->size < RECORD_SIZE || blob->size % RECORD_SIZE ||
        blob->size / RECORD_SIZE > (size_t)capacity || read_u32_le(blob->data + 28)) return false;
    /* Validate the entire chain before changing the caller's table. The
     * shipped resources visit each consecutive record exactly once. */
    for (size_t at = RECORD_SIZE; at < blob->size; at += RECORD_SIZE) {
        const uint8_t *record = blob->data + at;
        unsigned kind = read_u32_le(record + 28);
        if (read_u32_le(record) != (at + RECORD_SIZE < blob->size ? at + RECORD_SIZE : 0) ||
            kind < 1 || kind > 13 || kind == 5) return false;
    }
    if (read_u32_le(blob->data + 64) != (blob->size > RECORD_SIZE ? RECORD_SIZE : 0)) return false;
    const menuitemkind_t kinds[] = {
        MI_STATIC, MI_BUTTON, MI_BUTTON, MI_CHECK, MI_CHECK, MI_STATIC,
        MI_SLIDER, MI_SCROLLBAR, MI_TEXTFIELD, MI_STATIC, MI_STATIC, MI_STATIC,
        MI_LIST, MI_DROPDOWN,
    };
    irect_t window = {read_u16_le(blob->data + 4), read_u16_le(blob->data + 6),
                     read_u16_le(blob->data + 12), read_u16_le(blob->data + 14)};
    unsigned strings = read_u32_le(blob->data + 60);
    for (size_t at = 0; at < blob->size; at += RECORD_SIZE) {
        const uint8_t *record = blob->data + at;
        unsigned kind = read_u32_le(record + 28);
        unsigned flags = read_u16_le(record + 24);
        menuitem_t *item = &items[at / RECORD_SIZE];
        *item = (menuitem_t){.kind = kinds[kind], .id = (int16_t)read_u16_le(record + 26),
                            .visible = (flags & 8) != 0, .enabled = (flags & 16) != 0,
                            .link = -1, .flags = W2_ITEM_FLAGS(kind, flags)};
        item->rect = at ? (irect_t){window.x + read_u16_le(record + 4),
                                    window.y + read_u16_le(record + 6),
                                    read_u16_le(record + 12), read_u16_le(record + 14)} : window;
        item->align = kind == 11 ? MALIGN_RIGHT : kind == 10 || kind == 1 || kind == 2 ?
                      MALIGN_CENTER : MALIGN_LEFT;
        /* The root string is an internal dialog name, never a caption. */
        unsigned slot = read_u32_le(record + 20);
        w2_text_t label;
        if (at && slot && w2_resource_label((int)strings, (int)slot - 1, &label)) {
            snprintf(item->text, sizeof(item->text), "%.*s", (int)sizeof(item->text) - 1, label.text);
            item->mark_at = label.mark_at;
            item->mark_len = label.mark_len;
        }
        ++*count;
    }
    return true;
}

bool w2_load_scene(const char *root, int resource, menuitem_t *items, int capacity, int *count) {
    char path[1200];
    *count = 0;
    int bank = resource / 1000;
    if (bank != 3 && bank != 6) return false;
    M_PathJoin(path, sizeof(path), root, bank == 3 ? "DATA/REZDAT.WAR" : "DATA/MUDDAT.CUD");
    w2_archive_t archive;
    if (!w2_archive_open(&archive, path)) return false;
    w2_blob_t blob = {0};
    bool ok = w2_archive_extract(&archive, resource % 1000, &blob) &&
              w2_decode_scene(&blob, items, capacity, count);
    w2_blob_free(&blob);
    w2_archive_close(&archive);
    return ok;
}
