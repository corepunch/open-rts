#define _DEFAULT_SOURCE
#include "z_local.h"

#include <stdlib.h>
#include <string.h>

/* The in-level panel art. SHEADFX.PAC holds the 640x480 set: NSNEWPAN.DAT
 * (the panel, its labels, buttons, digits and bars), SHEADBI0..4.DAT (one
 * robot face each, in pieces that animate) and SHEADBAK.DAT (the scenery
 * behind a face, one a world). HEADFX.PAC holds the same at half size.
 *
 * Each file is a picture list: u16 zero, u16 count, then count records of
 * u16 offset, u16 height, u16 width and u16 header size, the offset being
 * the low sixteen bits of where the picture starts. The pictures follow
 * the records in order, one byte a pixel in the world palette, 0 where a
 * picture shows what is under it. */

static bool load_list(const char *path, const char *name, spritesheet_t *out) {
    blob_t blob;
    if (!Z_ReadPac(path, name, &blob)) return false;
    memset(out, 0, sizeof(*out));
    bool ok = false;
    if (blob.size < 4) goto done;
    int count = read_u16_le(blob.bytes + 2);
    size_t at = 4 + (size_t)count * 8;
    if (count <= 0 || at > blob.size || !R_AllocSpriteCells(out, count)) goto done;
    for (int i = 0; i < count; ++i) {
        const uint8_t *record = blob.bytes + 4 + (size_t)i * 8;
        int h = read_u16_le(record + 2), w = read_u16_le(record + 4);
        size_t size = (size_t)w * h;
        if ((at & 0xffff) != read_u16_le(record) || w <= 0 || h <= 0 || blob.size - at < size) {
            R_FreeSprite(out);
            goto done;
        }
        uint8_t *pixels = malloc(size);
        if (!pixels) {
            R_FreeSprite(out);
            goto done;
        }
        memcpy(pixels, blob.bytes + at, size);
        out->lumps[i].indices = pixels;
        out->cells[i].rect = out->cells[i].bounds = (irect_t){0, 0, w, h};
        if (w > out->frame_size.w) out->frame_size.w = w;
        if (h > out->frame_size.h) out->frame_size.h = h;
        at += size;
    }
    out->indexed = true;
    ok = true;
done:
    W_FreeFile(&blob);
    return ok;
}

bool Z_LoadPanel(const char *root, zpanel_t *out) {
    static const char *const heads[Z_FACES] = {
        "SHEADBI0.DAT", "SHEADBI1.DAT", "SHEADBI2.DAT", "SHEADBI3.DAT", "SHEADBI4.DAT"};
    char path[1024];
    memset(out, 0, sizeof(*out));
    M_PathJoin(path, sizeof(path), root && root[0] ? root : g_game_default_root, "SHEADFX.PAC");
    bool ok = load_list(path, "NSNEWPAN.DAT", &out->pieces) && load_list(path, "SHEADBAK.DAT", &out->backs);
    for (int i = 0; ok && i < Z_FACES; ++i) ok = load_list(path, heads[i], &out->heads[i]);
    if (!ok) Z_FreePanel(out);
    return ok;
}

void Z_FreePanel(zpanel_t *panel) {
    R_FreeSprite(&panel->pieces);
    R_FreeSprite(&panel->backs);
    for (int i = 0; i < Z_FACES; ++i) R_FreeSprite(&panel->heads[i]);
    memset(panel, 0, sizeof(*panel));
}

/* The pieces are drawn in the world's palette; index 0 is black where a
 * piece is drawn opaque. */
static void colour(spritesheet_t *sheet, const uint32_t palette[256]) {
    if (!sheet->cells) return;
    memcpy(sheet->palette, palette, sizeof(sheet->palette));
    sheet->palette[0] = 0xff000000u;
    memcpy(sheet->source_palette, sheet->palette, sizeof(sheet->palette));
}

void Z_PanelPalette(zpanel_t *panel, const uint32_t palette[256]) {
    colour(&panel->pieces, palette);
    colour(&panel->backs, palette);
    for (int i = 0; i < Z_FACES; ++i) colour(&panel->heads[i], palette);
}
