#include "t_local.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II native dialog records", #c)

int main(void) {
    w2_archive_t archive;
    CHECK(w2_archive_open(&archive, "data/WAR2/DATA/REZDAT.WAR"));
    CHECK(archive.count == 91);
    CHECK(w2_strings_load("data/WAR2"));
    unsigned kinds = 0;
    int controls = 0;
    for (int entry = 33; entry < archive.count; ++entry) {
        w2_blob_t blob;
        CHECK(w2_archive_extract(&archive, entry, &blob));
        CHECK(blob.size >= 72 && blob.size % 72 == 0);
        CHECK(read_u32_le(blob.data + 28) == 0 && read_u32_le(blob.data + 64) == 72);
        menuitem_t items[80];
        int count;
        CHECK(w2_decode_scene(&blob, items, 80, &count));
        CHECK(count == (int)(blob.size / 72));
        for (size_t at = 72; at < blob.size; at += 72) {
            const uint8_t *record = blob.data + at;
            unsigned next = read_u32_le(record);
            unsigned kind = read_u32_le(record + 28);
            CHECK(next == (at + 72 < blob.size ? at + 72 : 0));
            CHECK(kind > 0 && kind <= 13);
            CHECK(read_u16_le(record + 12) > 0 && read_u16_le(record + 14) > 0);
            const menuitem_t *item = &items[at / 72];
            CHECK(item->id == (int16_t)read_u16_le(record + 26));
            CHECK(item->rect.x == items[0].rect.x + read_u16_le(record + 4));
            CHECK(item->rect.y == items[0].rect.y + read_u16_le(record + 6));
            CHECK(item->rect.w == read_u16_le(record + 12) && item->rect.h == read_u16_le(record + 14));
            CHECK(item->visible == ((read_u16_le(record + 24) & 8) != 0));
            CHECK(item->enabled == ((read_u16_le(record + 24) & 16) != 0));
            if (kind == 6) CHECK(item->kind == MI_SLIDER);
            kinds |= 1u << kind;
            ++controls;
        }
        if (entry == 41 || entry == 43) {
            CHECK(read_u32_le(blob.data + 60) == (unsigned)(entry == 41 ? 4004 : 4006));
            CHECK(blob.size == (size_t)(entry == 41 ? 6 : 4) * 72);
            const uint8_t *button = blob.data + 72;
            CHECK(read_u16_le(button + 4) == 208 && read_u16_le(button + 6) == 240);
            CHECK(read_u16_le(button + 12) == 224 && read_u16_le(button + 14) == 28);
            CHECK(read_u32_le(button + 20) == 2 && read_u32_le(button + 28) == 2);
            w2_text_t label;
            CHECK(w2_label(entry == 41 ? 4 : 6, 1, &label));
            CHECK(!strcmp(items[1].text, label.text));
        }
        /* Malformed spans and links must fail without replacing the table. */
        CHECK(!w2_decode_scene(&blob, items, count - 1, &count) && count == 0);
        uint8_t next = blob.data[72];
        blob.data[72] ^= 1;
        CHECK(!w2_decode_scene(&blob, items, 80, &count) && count == 0);
        blob.data[72] = next;
        --blob.size;
        CHECK(!w2_decode_scene(&blob, items, 80, &count) && count == 0);
        ++blob.size;
        if (entry == 89) {
            CHECK(read_u32_le(blob.data + 16) == 3012 && read_u32_le(blob.data + 60) == 4062);
            CHECK(read_u16_le(blob.data + 4) == 144 && read_u16_le(blob.data + 6) == 64);
            const uint8_t *list = blob.data + 3 * 72;
            CHECK(read_u32_le(list + 28) == 12);
            CHECK(read_u16_le(list + 4) == 22 && read_u16_le(list + 6) == 122);
            CHECK(read_u16_le(list + 12) == 300 && read_u16_le(list + 14) == 112);
        }
        w2_blob_free(&blob);
    }
    CHECK(kinds == 0x3fde); /* types 1–4 and 6–13 occur; no type 5 in these scenes */
    w2_archive_close(&archive);
    w2_strings_free();
    printf("PASS: 58 native Warcraft II dialogs, %d controls, linked 72-byte records and original menu/scenario geometry\n", controls);
    return 0;
}
