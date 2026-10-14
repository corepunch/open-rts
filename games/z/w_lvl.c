#define _DEFAULT_SOURCE
#include "z_local.h"
#include "info.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* LEVELS.DAT: 240-byte entries; ZED.EXE copies these names at 0x4f740. */
enum {
    LV_SIZE = 240, LV_MAP = 0x14, LV_CPUPLR = 0x2e, LV_OBJECTS = 0x61,
    LV_WORLD = 0x6e, LV_BUILDINGS = 0x84, LV_NAME = 13,
};

/* xxxxx.MAP version 0xf7, as ZED.EXE 0x73fe6 reads it: the version byte,
 * zone and bridge blocks, then 32 bytes of tileset names with the width and
 * height at +0x1c, the tile plane and the attribute plane (128x128 each,
 * attribute bit 7 picks the second 256 BLK tiles, the rest is the terrain). */
enum {
    MAP_VERSION = 0xf7, MAP_HEADER = 1 + 0x1a00 + 0xac8 + 0x2a8,
    MAP_TILES = MAP_HEADER + 0x20, MAP_TERRAIN = MAP_TILES + 0x4000,
    MAP_SIZE = MAP_TERRAIN + 0x4000 + 0x1e0 + 0x3300,
};

/* Robots' terrain table (image 0x116c00, the neutral row of 0x148f00):
 * nonzero stops them. Terrain ids run to 66. */
static const uint8_t robot_blocked[67] = {
    0,0,0,0,0,0,0,0,0,1, 0,1,1,1,1,1,1,0,0,1, 0,0,0,0,1,1,0,1,0,1,
    0,1,1,0,0,0,0,1,1,1, 1,0,1,1,0,0,0,1,1,1, 1,0,1,0,0,0,1,1,1,0,
    0,0,0,0,0,0,0,
};

/* Native fort marker: tile 0xee of the second bank. */
enum { FORT_TILE = 0xee, FORT_SKIP = 0x680 };

static void level_path(char *out, size_t size, const char *dir, const char *name) {
    char upper[LV_NAME + 1];
    size_t i = 0;
    for (; i < LV_NAME && name[i]; ++i) upper[i] = (char)toupper((unsigned char)name[i]);
    upper[i] = '\0';
    M_PathJoin(out, size, dir, upper);
}

static void destroy_zmap(void *opaque) {
    zmap_t *map = opaque;
    if (!map) return;
    W_FreeFile(&map->rsc);
    free(map);
}

int Z_TeamAt(const zmap_t *map, ivec2_t pixel) {
    ivec2_t cell = {pixel.x >> 4, pixel.y >> 4};
    for (int i = 0; i < map->territory_count; ++i) {
        const zterritory_t *t = &map->territories[i];
        if (cell.x >= t->min.x && cell.x < t->max.x && cell.y >= t->min.y && cell.y < t->max.y)
            return t->team < Z_NEUTRAL ? t->team : Z_NEUTRAL;
    }
    return Z_NEUTRAL;
}

bool Z_SpriteSize(const zmap_t *map, int index, isize2_t *size) {
    const blob_t *rsc = &map->rsc;
    if (index < 0 || index >= (int)read_u16_le(rsc->bytes)) return false;
    uint32_t offset = read_u32_le(rsc->bytes + 2 + (size_t)index * 4);
    if (offset > rsc->size - 2) return false;
    *size = (isize2_t){rsc->bytes[offset + 1] * 8, rsc->bytes[offset]};
    return true;
}

/* CPUPLRnn.DAT: 64 territories of 30 bytes, cell rectangle then the team;
 * ZED.EXE 0x9dc69 finds a building's team through them. */
static bool load_territories(zmap_t *map, const char *name) {
    char path[1024];
    level_path(path, sizeof(path), map->dir, name);
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return false;
    bool ok = blob.size >= Z_MAX_TERRITORIES * 30;
    for (int i = 0; ok && i < Z_MAX_TERRITORIES; ++i) {
        const uint8_t *r = blob.bytes + i * 30;
        if (read_u16_le(r) == 0xffff) continue;
        map->territories[map->territory_count++] = (zterritory_t){
            {read_u16_le(r), read_u16_le(r + 2)}, {read_u16_le(r + 4), read_u16_le(r + 6)}, r[8]};
    }
    W_FreeFile(&blob);
    return ok;
}

/* ZED.EXE 0x4ec1c: the first marker found holds the gated fort; the search
 * for the second resumes 13 rows on. */
static void find_forts(zmap_t *map, const uint8_t *tiles) {
    for (int i = 0; i < Z_MAP_STRIDE * Z_MAP_STRIDE && map->fort_count < Z_MAX_FORTS; ++i) {
        if (tiles[i] != FORT_TILE || !(map->terrain[i] & 0x80)) continue;
        map->forts[map->fort_count++] = (ivec2_t){(i % Z_MAP_STRIDE) * Z_TILE, (i / Z_MAP_STRIDE) * Z_TILE};
        i += FORT_SKIP;
    }
}

bool Z_LoadLevel(const char *map_path, level_t *out) {
    memset(out, 0, sizeof(*out));
    zmap_t *map = calloc(1, sizeof(*map));
    if (!map) return false;
    out->native_data = map;
    out->destroy_native_data = destroy_zmap;
    blob_t levels = {0}, file = {0};
    char path[1024];

    const char *name = M_FileName(map_path);
    snprintf(map->dir, sizeof(map->dir), "%.*s", (int)(name - map_path - (name > map_path)), map_path);
    const char *dir_name = M_FileName(map->dir);
    snprintf(map->root, sizeof(map->root), "%.*s",
             (int)(dir_name - map->dir - (dir_name > map->dir)), map->dir);
    if (!map->root[0]) snprintf(map->root, sizeof(map->root), ".");

    M_PathJoin(path, sizeof(path), map->root, "LEVELS.DAT");
    if (!W_ReadFile(path, &levels)) goto fail;
    const uint8_t *entry = NULL;
    for (size_t at = 0; at + LV_SIZE <= levels.size; at += LV_SIZE)
        if (!strncasecmp((const char *)levels.bytes + at + LV_MAP, name, LV_NAME)) {
            entry = levels.bytes + at;
            break;
        }
    if (!entry) {
        fprintf(stderr, "z: %s is not in %s\n", name, path);
        goto fail;
    }
    map->world = entry[LV_WORLD];
    snprintf(map->objects, sizeof(map->objects), "%.*s", LV_NAME, (const char *)entry + LV_OBJECTS);
    snprintf(map->buildings, sizeof(map->buildings), "%.*s", LV_NAME, (const char *)entry + LV_BUILDINGS);
    char cpuplr[LV_NAME + 1];
    snprintf(cpuplr, sizeof(cpuplr), "%.*s", LV_NAME, (const char *)entry + LV_CPUPLR);
    if (map->world >= Z_WORLDS || !load_territories(map, cpuplr)) goto fail;

    if (!W_ReadFile(map_path, &file)) goto fail;
    if (file.size != MAP_SIZE || file.bytes[0] != MAP_VERSION) {
        fprintf(stderr, "z: %s: expected a %d-byte version 0x%02x map\n", map_path, MAP_SIZE, MAP_VERSION);
        goto fail;
    }
    const uint8_t *tiles = file.bytes + MAP_TILES;
    memcpy(map->terrain, file.bytes + MAP_TERRAIN, sizeof(map->terrain));
    out->width = read_u16_le(file.bytes + MAP_TILES - 4);
    out->height = read_u16_le(file.bytes + MAP_TILES - 2);
    if (out->width <= 0 || out->width > Z_MAP_STRIDE || out->height <= 0 || out->height > Z_MAP_STRIDE)
        goto fail;
    size_t cells = (size_t)out->width * out->height;
    out->tile_ids = calloc(cells, sizeof(*out->tile_ids));
    out->blocked = calloc(cells, 1);
    if (!out->tile_ids || !out->blocked) goto fail;
    for (int y = 0; y < out->height; ++y)
        for (int x = 0; x < out->width; ++x) {
            int i = y * Z_MAP_STRIDE + x;
            uint8_t terrain = map->terrain[i] & 0x7f;
            out->tile_ids[y * out->width + x] = (uint16_t)(tiles[i] | (map->terrain[i] & 0x80) << 1);
            out->blocked[y * out->width + x] = terrain >= sizeof(robot_blocked) || robot_blocked[terrain];
        }
    find_forts(map, tiles);

    M_PathJoin(path, sizeof(path), map->root, "SPRITES.RSC");
    if (!W_ReadFile(path, &map->rsc)) goto fail;
    size_t count = map->rsc.size >= 2 ? read_u16_le(map->rsc.bytes) : 0;
    if (!count || map->rsc.size < 2 + count * 4 + count + 768) {
        fprintf(stderr, "z: %s: bad sprite index\n", path);
        goto fail;
    }

    static const char *const worlds[Z_WORLDS] = {"DESERT", "ARCTIC", "CITY", "VOLCAN", "JUNGLE"};
    snprintf(out->tileset_name, sizeof(out->tileset_name), "%s", worlds[map->world]);
    W_FreeFile(&file);
    W_FreeFile(&levels);
    return true;
fail:
    W_FreeFile(&file);
    W_FreeFile(&levels);
    P_FreeLevel(out);
    return false;
}

/* One spawned native thing; owner 7 holds whatever no team does. */
static mobj_t *spawn(int type, int state, ivec2_t pixel, isize2_t size, int team) {
    fixed2_t centre = {(pixel.x * 2 + size.w) * (FIXED_ONE / (2 * Z_TILE)),
                       (pixel.y * 2 + size.h) * (FIXED_ONE / (2 * Z_TILE))};
    mobj_t *mo = P_SpawnMobj(fixed3_from_fixed2(centre, 0), (uint16_t)type);
    if (!mo) return NULL;
    if (state != mobjinfo[type].spawnstate) P_SetMobjState(mo, state);
    mo->team = (uint8_t)team;
    mo->owner = team < Z_NEUTRAL ? (uint8_t)team : 7;
    mo->allegiance = team == Z_NEUTRAL ? ALLEGIANCE_NEUTRAL :
                     team == 0 ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
    return mo;
}

static bool read_level_file(const zmap_t *map, const char *name, blob_t *out) {
    char path[1024];
    level_path(path, sizeof(path), map->dir, name);
    return W_ReadFile(path, out);
}

int Z_LoadThings(void) {
    const zmap_t *map = level.native_data;
    if (!map) return 0;
    int count = 0;
    for (int i = 0; i < map->fort_count; ++i) {
        int team = Z_TeamAt(map, map->forts[i]);
        if (spawn(MT_FORT, i ? S_FORT_CPU : S_FORT, map->forts[i], (isize2_t){160, 176}, team)) count++;
        if (team == 0) {
            level.has_camera = true;
            level.camera = (fvec2_t){(map->forts[i].x + 80) / (float)Z_TILE,
                                     (map->forts[i].y + 88) / (float)Z_TILE};
        }
    }

    /* BUILDnn.DAT: 35 records of 80 bytes; ZED.EXE keeps them as its
     * building structures (x, y, type, hit points at +0x1a). */
    blob_t blob;
    if (read_level_file(map, map->buildings, &blob)) {
        for (size_t at = 0; at + 80 <= blob.size; at += 80) {
            const uint8_t *r = blob.bytes + at;
            int type = 0;
#define Z_BUILDING(native, name, label, w, h, ...) if (r[4] == native) type = MT_##name;
#include "buildings.inc"
#undef Z_BUILDING
            if (!type) {
                if (r[4] != 0xff) fprintf(stderr, "z: building type %d is not drawn yet\n", r[4]);
                continue;
            }
            ivec2_t at_px = {read_u16_le(r), read_u16_le(r + 2)};
            isize2_t size;
            if (!Z_SpriteSize(map, Z_BuildingSprite(r[4], map->world), &size)) continue;
            mobj_t *mo = spawn(type, mobjinfo[type].spawnstate, at_px, size, Z_TeamAt(map, at_px));
            if (!mo) continue;
            if (read_u16_le(r + 0x1a)) mo->hp = mo->max_hp = read_u16_le(r + 0x1a);
            count++;
        }
        W_FreeFile(&blob);
    }

    /* OBJECTnn.DAT: 150 records of 10 bytes, x, y and the object type. */
    if (read_level_file(map, map->objects, &blob)) {
        for (size_t at = 0; at + 10 <= blob.size; at += 10) {
            const uint8_t *r = blob.bytes + at;
            int type = r[4] + 1;
            if (read_u16_le(r) == 0xffff || type >= MT_FORT) continue;
            isize2_t size;
            if (!Z_SpriteSize(map, states[mobjinfo[type].spawnstate].frame, &size)) continue;
            if (spawn(type, mobjinfo[type].spawnstate, (ivec2_t){read_u16_le(r), read_u16_le(r + 2)},
                      size, Z_NEUTRAL)) count++;
        }
        W_FreeFile(&blob);
    }
    return count;
}
