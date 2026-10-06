#include "w2_local.h"

#include "info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PUD sections are tag + u32 length. Unknown sections are skipped. Movement
 * comes from SQM: bit 0x80 is forest (impassable to land and sea), bit 0x40
 * is water, otherwise land. Forests are not cell_solid, so air can cross
 * them. Building footprints, and wall tiles, are cell_solid. */

static const char *era_names[] = { "forest", "winter", "wasteland", "swamp" };

static void destroy_pud(void *pointer) {
    w2_pud_t *pud = pointer;
    if (!pud) return;
    free(pud->units);
    free(pud);
}

static bool tag_is(const uint8_t *p, const char *tag) {
    return memcmp(p, tag, 4) == 0;
}

static bool wall_tile(uint16_t value) {
    return (value & 0xfff0u) == 0x00a0u || (value & 0xfff0u) == 0x00c0u ||
           (value & 0xff00u) == 0x0900u || (value & 0x00f0u) == 0x0090u ||
           (value & 0xfff0u) == 0x00b0u || (value & 0xff00u) == 0x0800u;
}

static uint8_t terrain_of(uint16_t sqm) {
    if (sqm & 0x0080u) return 2;
    if (sqm & 0x0040u) return 1;
    return 0;
}

static void fill_speeds(terrainspeeds_t *speeds) {
    speeds->class_count = 4;
    speeds->terrain[1][0] = 100;
    speeds->terrain[2][1] = 100;
    speeds->terrain[3][0] = 100;
    speeds->terrain[3][1] = 100;
    speeds->terrain[3][2] = 100;
}

static bool take_section(const uint8_t **p, const uint8_t *end,
                         const uint8_t **payload, uint32_t *length) {
    if ((size_t)(end - *p) < 8) return false;
    uint32_t len = read_u32_le(*p + 4);
    if ((uint64_t)len > (uint64_t)(end - (*p + 8))) return false;
    *payload = *p + 8;
    *length = len;
    *p += 8 + len;
    return true;
}

bool w2_load_pud(const char *path, level_t *out) {
    if (!out) return false;
    P_FreeLevel(out);
    blob_t file;
    if (!W_ReadFile(path, &file)) {
        fprintf(stderr, "warcraft-2: cannot read %s\n", path);
        return false;
    }
    const uint8_t *p = file.bytes;
    const uint8_t *end = file.bytes + file.size;
    const uint8_t *payload = NULL;
    uint32_t length = 0;
    if (!take_section(&p, end, &payload, &length) || !tag_is(file.bytes, "TYPE") ||
        length < 8 || memcmp(payload, "WAR2 MAP", 8) != 0) {
        fprintf(stderr, "warcraft-2: %s is not a WAR2 MAP\n", path);
        W_FreeFile(&file);
        return false;
    }

    w2_pud_t *pud = calloc(1, sizeof(*pud));
    if (!pud) { W_FreeFile(&file); return false; }
    pud->view_player = -1;
    int width = 0, height = 0, ver = -1;
    const uint8_t *mtxm = NULL, *sqm = NULL, *units = NULL;
    uint32_t mtxm_len = 0, sqm_len = 0, unit_len = 0;
    bool failed = false;

    while (p < end) {
        const uint8_t *header = p;
        if (!take_section(&p, end, &payload, &length)) { failed = true; break; }
        if (tag_is(header, "VER ")) {
            if (length < 1) { failed = true; break; }
            ver = length >= 2 ? read_u16_le(payload) : payload[0];
            if (ver != 0x11 && ver != 0x13) {
                fprintf(stderr, "warcraft-2: %s version %d is not 0x11 or 0x13\n", path, ver);
                failed = true;
                break;
            }
        } else if (tag_is(header, "OWNR")) {
            int n = length < 16 ? (int)length : 16;
            memcpy(pud->owners, payload, (size_t)n);
            for (int i = 0; i < n; ++i)
                if (pud->owners[i] == 5 && pud->view_player < 0) pud->view_player = i;
        } else if (tag_is(header, "ERA ") || tag_is(header, "ERAX")) {
            if (length < 1) { failed = true; break; }
            pud->era = payload[0];
            if (pud->era < 0 || pud->era > 3) {
                fprintf(stderr, "warcraft-2: era %d is outside 0..3\n", pud->era);
                pud->era = 0;
            }
        } else if (tag_is(header, "DIM ")) {
            if (length < 4) { failed = true; break; }
            width = read_u16_le(payload);
            height = read_u16_le(payload + 2);
        } else if (tag_is(header, "SIDE")) {
            int n = length < 16 ? (int)length : 16;
            memcpy(pud->sides, payload, (size_t)n);
        } else if (tag_is(header, "SGLD") || tag_is(header, "SLBR") || tag_is(header, "SOIL")) {
            int which = tag_is(header, "SGLD") ? 0 : tag_is(header, "SLBR") ? 1 : 2;
            int slots = (int)(length / 2);
            if (slots > 16) slots = 16;
            for (int i = 0; i < slots && i < 8; ++i)
                out->player_resources[i][which] = read_u16_le(payload + (size_t)i * 2);
        } else if (tag_is(header, "MTXM")) {
            mtxm = payload;
            mtxm_len = length;
        } else if (tag_is(header, "SQM ")) {
            sqm = payload;
            sqm_len = length;
        } else if (tag_is(header, "UNIT")) {
            units = payload;
            unit_len = length;
        }
    }
    pud->ver = ver;
    if (failed || width < 1 || height < 1 || width > 256 || height > 256 || !mtxm) {
        fprintf(stderr, "warcraft-2: %s is missing DIM or MTXM\n", path);
        destroy_pud(pud);
        W_FreeFile(&file);
        return false;
    }
    size_t cells = (size_t)width * (size_t)height;
    if (mtxm_len < cells * 2u) {
        fprintf(stderr, "warcraft-2: MTXM is %u bytes for a %dx%d map\n", mtxm_len, width, height);
        destroy_pud(pud);
        W_FreeFile(&file);
        return false;
    }

    out->width = width;
    out->height = height;
    out->tile_ids = calloc(cells, sizeof(uint16_t));
    out->blocked = calloc(cells, 1);
    out->cell_terrain = calloc(cells, 1);
    out->cell_solid = calloc(cells, 1);
    out->speeds = calloc(1, sizeof(terrainspeeds_t));
    if (!out->tile_ids || !out->blocked || !out->cell_terrain || !out->cell_solid || !out->speeds) {
        destroy_pud(pud);
        W_FreeFile(&file);
        P_FreeLevel(out);
        return false;
    }
    fill_speeds(out->speeds);
    snprintf(out->tileset_name, sizeof(out->tileset_name), "%s",
             era_names[pud->era >= 0 && pud->era <= 3 ? pud->era : 0]);
    for (size_t i = 0; i < cells; ++i) {
        uint16_t value = read_u16_le(mtxm + i * 2u);
        out->tile_ids[i] = value;
        if (wall_tile(value)) {
            out->cell_solid[i] = 1;
            out->blocked[i] = 1;
        }
    }
    if (!sqm || sqm_len < cells * 2u) {
        fprintf(stderr, "warcraft-2: %s has no SQM; treating every cell as land\n", path);
    } else {
        for (size_t i = 0; i < cells; ++i) {
            uint8_t terrain = terrain_of(read_u16_le(sqm + i * 2u));
            out->cell_terrain[i] = terrain;
            if (terrain != 0) out->blocked[i] = 1;
        }
    }
    if (units && unit_len >= 8) {
        int count = (int)(unit_len / 8u);
        pud->units = calloc((size_t)count, sizeof(w2_pud_unit_t));
        if (!pud->units) {
            destroy_pud(pud);
            W_FreeFile(&file);
            P_FreeLevel(out);
            return false;
        }
        pud->unit_count = count;
        for (int i = 0; i < count; ++i) {
            const uint8_t *rec = units + (size_t)i * 8u;
            pud->units[i] = (w2_pud_unit_t){
                .x = read_u16_le(rec),
                .y = read_u16_le(rec + 2),
                .type = rec[4],
                .player = rec[5],
                .data = read_u16_le(rec + 6),
            };
        }
    }
    out->has_camera = true;
    out->camera = (fvec2_t){ (float)width * 0.5f, (float)height * 0.5f };
    if (pud->view_player >= 0) {
        for (int i = 0; i < pud->unit_count; ++i) {
            const w2_pud_unit_t *unit = &pud->units[i];
            if (unit->player != pud->view_player) continue;
            if (unit->type != 94 && unit->type != 95) continue;
            out->camera = (fvec2_t){ unit->x + 0.5f, unit->y + 0.5f };
            break;
        }
    }
    snprintf(out->map_path, sizeof(out->map_path), "%s", path);
    out->native_data = pud;
    out->destroy_native_data = destroy_pud;
    W_FreeFile(&file);
    if (!w2_init_resources(out)) { P_FreeLevel(out); return false; }
    return true;
}

void w2_mark_footprint(int x, int y, isize2_t foot) {
    for (int yy = 0; yy < foot.h; ++yy)
        for (int xx = 0; xx < foot.w; ++xx) {
            int cx = x + xx, cy = y + yy;
            if (!L_Contains(&level, cx, cy)) continue;
            int index = L_Index(&level, cx, cy);
            level.cell_solid[index] = 1;
            level.blocked[index] = 1;
        }
}

static uint8_t allegiance_for(const w2_pud_t *pud, uint8_t player) {
    if (player < 8 && player == (uint8_t)consoleplayer) return ALLEGIANCE_PLAYER;
    if (player >= 8) return ALLEGIANCE_NEUTRAL;
    if (pud && pud->owners[player] == 2) return ALLEGIANCE_NEUTRAL;
    return ALLEGIANCE_ENEMY;
}

int w2_spawn_units(void) {
    const w2_pud_t *pud = level.native_data;
    if (!pud) return 0;
    int spawned = 0;
    for (int i = 0; i < pud->unit_count; ++i) {
        const w2_pud_unit_t *rec = &pud->units[i];
        if (rec->type >= W2_TYPE_COUNT) continue;
        const mobjinfo_t *info = &mobjinfo[rec->type + 1];
        if (!info->name || (info->w2.flags & W2_SKIP)) continue;
        isize2_t foot = info->w2.footprint;
        fvec2_t at = { rec->x + foot.w * 0.5f, rec->y + foot.h * 0.5f };
        mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2(at, 0), (uint16_t)(rec->type + 1));
        if (!unit) break;
        unit->owner = rec->player;
        unit->team = rec->player < 8 ? rec->player : 8;
        unit->allegiance = allegiance_for(pud, rec->player);
        unit->core.angle = ANG270;
        if (info->w2.flags & W2_STRUCTURE) w2_mark_footprint(rec->x, rec->y, foot);
        if (rec->type == 92 && L_Contains(&level, rec->x + foot.w - 1, rec->y + foot.h - 1))
            level.resource_vents[level.resource_vent_count++] = (resourcevent_t){
                .cell = {rec->x, rec->y}, .attachment = at, .footprint = foot,
                .amount = rec->data * 2500, .rate = 100, .resource_type = 0,
                .active = rec->data > 0, .source_id = unit->id,
            };
        spawned++;
    }
    return spawned;
}

int w2_era_palette(int era) {
    if (era == 1) return 18;
    if (era == 2 || era == 3) return 10;
    return 2;
}

static const char *maindat_path(const char *root, char *dst, size_t dst_size) {
    snprintf(dst, dst_size, "%s/DATA/MAINDAT.WAR", root ? root : "data/WAR2");
    return dst;
}

static bool load_named_sprite(const w2_archive_t *arc, const uint32_t palette[256],
                              int era, int pud, spritesheet_t *out, int *phases) {
    const mobjinfo_t *unit = &mobjinfo[pud + 1];
    int entry = w2_grp_entry(unit, era, arc->count);
    if (!entry) {
        fprintf(stderr, "warcraft-2: no GRP entry for %s\n", unit->name ? unit->name : "unit");
        return false;
    }
    w2_blob_t blob;
    if (!w2_archive_extract(arc, entry, &blob)) {
        fprintf(stderr, "warcraft-2: cannot extract entry %d (%s)\n",
                entry, unit->name ? unit->name : "unit");
        return false;
    }
    bool directional = (unit->w2.flags & W2_MOBILE) != 0;
    bool ok = w2_decode_grp(&blob, palette, out, directional, phases);
    w2_blob_free(&blob);
    if (!ok) {
        fprintf(stderr, "warcraft-2: GRP %d (%s) did not decode\n",
                entry, unit->name ? unit->name : "unit");
        return false;
    }
    int matched = w2_install_team_colors(out, palette);
    if (matched < 4)
        fprintf(stderr, "warcraft-2: palette matched %d/4 red team shades\n", matched);
    w2_limit_walk(pud, phases ? *phases : 1);
    return true;
}

bool w2_load_assets(const char *data_root, const level_t *map, const char *sprite_name,
                    tileset_t *tileset, spritesheet_t *unit_sprite) {
    char path[1024];
    w2_archive_t arc;
    if (!w2_archive_open(&arc, maindat_path(data_root, path, sizeof(path)))) return false;
    const w2_pud_t *pud = map ? map->native_data : NULL;
    int era = pud ? pud->era : 0;
    if (!w2_decode_tileset(&arc, era, tileset)) {
        w2_archive_close(&arc);
        return false;
    }
    int named = w2_pud_named(sprite_name);
    if (named < 0) named = 0;
    uint32_t palette[256];
    w2_blob_t pal;
    int phases = 1;
    bool ok = w2_archive_extract(&arc, w2_era_palette(era), &pal) &&
              w2_decode_palette(&pal, palette) &&
              load_named_sprite(&arc, palette, era, named, unit_sprite, &phases);
    w2_blob_free(&pal);
    w2_archive_close(&arc);
    if (!ok) R_FreeTileset(tileset);
    return ok;
}

bool w2_load_runtime_sprites(const char *data_root, const level_t *map,
                             mobj_t *const *units, int unit_count, spritecache_t *cache) {
    char path[1024];
    w2_archive_t arc;
    if (!w2_archive_open(&arc, maindat_path(data_root, path, sizeof(path)))) return false;
    const w2_pud_t *pud = map ? map->native_data : NULL;
    int era = pud ? pud->era : 0;
    w2_blob_t pal;
    uint32_t palette[256];
    if (!w2_archive_extract(&arc, w2_era_palette(era), &pal) || !w2_decode_palette(&pal, palette)) {
        w2_blob_free(&pal);
        w2_archive_close(&arc);
        return false;
    }
    w2_blob_free(&pal);
    bool ok = true;
    for (int i = 0; i < unit_count; ++i) {
        const char *name = units[i] ? units[i]->core.sprite_name : NULL;
        int type = w2_pud_named(name);
        if (type < 0 || R_CacheFind(cache, name)) continue;
        if (cache->count >= MAX_DECORATION_SPRITES) { ok = false; break; }
        cachedsprite_t *slot = &cache->entries[cache->count];
        memset(slot, 0, sizeof(*slot));
        int phases = 1;
        if (!load_named_sprite(&arc, palette, era, type, &slot->sprite, &phases)) {
            R_FreeSprite(&slot->sprite);
            ok = false;
            continue;
        }
        snprintf(slot->name, sizeof(slot->name), "%s", name);
        cache->count++;
    }
    if (!w2_load_carriers(&arc, palette, cache)) ok = false;
    w2_archive_close(&arc);
    if (!R_BindSprites(cache, &game_info)) ok = false;
    return ok;
}

bool w2_cache_unit_sprite(const char *root, spritecache_t *cache, int pud) {
    if (!cache || pud < 0 || pud >= W2_TYPE_COUNT || !mobjinfo[pud + 1].name) return false;
    if (R_CacheFind(cache, mobjinfo[pud + 1].name)) return true;
    if (cache->count >= MAX_DECORATION_SPRITES) return false;
    char path[1024];
    w2_archive_t arc;
    if (!w2_archive_open(&arc, maindat_path(root, path, sizeof(path)))) return false;
    const w2_pud_t *map = level.native_data;
    int era = map ? map->era : 0;
    w2_blob_t pal = { 0 };
    uint32_t palette[256];
    bool ok = w2_archive_extract(&arc, w2_era_palette(era), &pal) && w2_decode_palette(&pal, palette);
    w2_blob_free(&pal);
    if (!ok) {
        w2_archive_close(&arc);
        return false;
    }
    cachedsprite_t *slot = &cache->entries[cache->count];
    memset(slot, 0, sizeof(*slot));
    int phases = 1;
    if (!load_named_sprite(&arc, palette, era, pud, &slot->sprite, &phases)) {
        R_FreeSprite(&slot->sprite);
        w2_archive_close(&arc);
        return false;
    }
    snprintf(slot->name, sizeof(slot->name), "%s", mobjinfo[pud + 1].name);
    cache->count++;
    w2_archive_close(&arc);
    return R_BindSprites(cache, &game_info);
}

bool w2_load_carriers(const w2_archive_t *arc, const uint32_t palette[256], spritecache_t *cache) {
    static const char *const names[] = { "peasant-gold", "peasant-lumber", "peon-gold", "peon-lumber" };
    static const int entries[] = { 124, 122, 125, 123 };
    for (int i = 0; i < 4; ++i) {
        if (R_CacheFind(cache, names[i])) continue;
        if (cache->count >= MAX_DECORATION_SPRITES) return false;
        cachedsprite_t *slot = &cache->entries[cache->count];
        w2_blob_t blob = {0};
        bool ok = w2_archive_extract(arc, entries[i], &blob) &&
                  w2_decode_grp(&blob, palette, &slot->sprite, true, NULL);
        w2_blob_free(&blob);
        if (!ok) { R_FreeSprite(&slot->sprite); return false; }
        w2_install_team_colors(&slot->sprite, palette);
        snprintf(slot->name, sizeof(slot->name), "%s", names[i]);
        ++cache->count;
    }
    return true;
}
