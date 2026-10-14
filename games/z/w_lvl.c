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
    LV_SIZE = 240, LV_MAP = 0x14, LV_ROBOTS = 0x21, LV_CPUPLR = 0x2e, LV_GROUPS = 0x55,
    LV_OBJECTS = 0x61, LV_WORLD = 0x6e, LV_BUILDINGS = 0x84, LV_NAME = 13,
};

/* ROBOTS.DAT, as ZED.EXE 0x50665 reads it: 44-byte robots from 0 (name at
 * +2, weapon type at +0x22), 82-byte groups from 0x4650 (in use when the
 * first byte is set; up to 21 robot numbers at +0x1c, 0xffff ends) and the
 * weapon types at 0x55f0 (units.inc). */
enum {
    RB_ROBOT = 44, RB_WEAPON = 0x22, RB_GROUPS = 0x4650, RB_GROUP = 0x52,
    RB_MEMBERS = 0x1c, RB_MAX_MEMBERS = 21,
};

/* CPUPLRnn.DAT: 64 territories of 30 bytes, then at 0x792 the computer's
 * starting groups, 10 bytes each: group, a flag, u16 x and y. */
enum { CP_TERRITORY = 30, CP_PLACED = 0x792, CP_RECORD = 10 };

#include "starts.inc"

/* First picture and handling of each unit type. Robots stand in 66
 * (0x14b10c action 0); vehicles have no one picture (-1) and stand on a
 * 16-pixel cell, which ZED.EXE draws them around; a turning gun sits 2
 * left and 8 up of its slot (0x5092d). */
#define PICTURE_ROBOT(sprite) 66
#define PICTURE_GUN(sprite) sprite
#define PICTURE_VEHICLE(sprite) -1
#define PICTURE_STILL(sprite) sprite
static const struct { int sprite; bool mobile, turns; } unit_kinds[] = {
#define Z_UNIT(t, type, label, kind, mobile, hp, dmg, spd, reach, reload, sprite, turn, ...) \
    {PICTURE_##kind(sprite), mobile, (turn) > 0},
#include "units.inc"
#undef Z_UNIT
};
enum { Z_UNIT_TYPES = sizeof(unit_kinds) / sizeof(unit_kinds[0]) };

/* objects.inc flags 0x32: guns and vehicles, standing empty. */
static const uint8_t object_flags[] = {
#define Z_OBJECT(native, type, label, sprite, flags, w, h, dead) flags,
#include "objects.inc"
#undef Z_OBJECT
};

/* xxxxx.MAP version 0xf7, as ZED.EXE 0x73fe6 reads it: the version byte,
 * a zone block, the cliff blocks (w_cliff.c), tile brushes, then 32 bytes of tileset names with the width and
 * height at +0x1c, the tile plane and the attribute plane (128x128 each,
 * attribute bit 7 picks the second 256 BLK tiles, the rest is the terrain). */
enum {
    MAP_VERSION = 0xf7, MAP_CLIFFS = 1 + 0x1a00, MAP_HEADER = MAP_CLIFFS + 0xac8 + 0x2a8,
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

/* Territories are a cell rectangle then the team; ZED.EXE 0x9dc69 finds a
 * building's team through them, 0x2a89f plants their flags. A placement's flag is set on every one
 * near the computer's fort and clear on leftovers, so only flagged ones
 * are kept (what else it means is not decoded). */
static bool load_cpuplr(zmap_t *map, const char *name) {
    char path[1024];
    level_path(path, sizeof(path), map->dir, name);
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return false;
    bool ok = blob.size >= CP_PLACED;
    for (int i = 0; ok && i < Z_MAX_TERRITORIES; ++i) {
        const uint8_t *r = blob.bytes + i * CP_TERRITORY;
        if (read_u16_le(r) == 0xffff) continue;
        map->territories[map->territory_count++] = (zterritory_t){
            {read_u16_le(r), read_u16_le(r + 2)}, {read_u16_le(r + 4), read_u16_le(r + 6)}, r[8],
            {r[0x12], r[0x13]}, r[0x18]};
    }
    for (size_t at = CP_PLACED; ok && at + CP_RECORD <= blob.size &&
                                map->placed_count < Z_MAX_PLACED; at += CP_RECORD) {
        const uint8_t *r = blob.bytes + at;
        if (r[1]) map->placed[map->placed_count++] =
            (zplaced_t){r[0], {read_u16_le(r + 2), read_u16_le(r + 4)}};
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
    map->number = (int)((entry - levels.bytes) / LV_SIZE);
    map->world = entry[LV_WORLD];
    memcpy(map->groups, entry + LV_GROUPS, Z_MAX_GROUPS);
    snprintf(map->robots, sizeof(map->robots), "%.*s", LV_NAME, (const char *)entry + LV_ROBOTS);
    snprintf(map->objects, sizeof(map->objects), "%.*s", LV_NAME, (const char *)entry + LV_OBJECTS);
    snprintf(map->buildings, sizeof(map->buildings), "%.*s", LV_NAME, (const char *)entry + LV_BUILDINGS);
    char cpuplr[LV_NAME + 1];
    snprintf(cpuplr, sizeof(cpuplr), "%.*s", LV_NAME, (const char *)entry + LV_CPUPLR);
    if (map->world >= Z_WORLDS || map->number >= Z_LEVELS || !load_cpuplr(map, cpuplr)) goto fail;

    if (!W_ReadFile(map_path, &file)) goto fail;
    if (file.size != MAP_SIZE || file.bytes[0] != MAP_VERSION) {
        fprintf(stderr, "z: %s: expected a %d-byte version 0x%02x map\n", map_path, MAP_SIZE, MAP_VERSION);
        goto fail;
    }
    uint8_t *tiles = file.bytes + MAP_TILES;
    memcpy(map->terrain, file.bytes + MAP_TERRAIN, sizeof(map->terrain));
    out->width = read_u16_le(file.bytes + MAP_TILES - 4);
    out->height = read_u16_le(file.bytes + MAP_TILES - 2);
    if (out->width <= 0 || out->width > Z_MAP_STRIDE || out->height <= 0 || out->height > Z_MAP_STRIDE)
        goto fail;
    find_forts(map, tiles);
    Z_StampCliffs(tiles, map->terrain, file.bytes + MAP_CLIFFS, map->world);
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

void Z_SetTeam(mobj_t *mo, int team) {
    if (team > Z_NEUTRAL) team = Z_NEUTRAL;
    mo->team = (uint8_t)team;
    mo->owner = team < Z_NEUTRAL ? (uint8_t)team : 7;
    mo->allegiance = team == Z_NEUTRAL ? ALLEGIANCE_NEUTRAL :
                     team == 0 ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
}

static fixed2_t pixel_centre(ivec2_t pixel, isize2_t size) {
    return (fixed2_t){(pixel.x * 2 + size.w) * (FIXED_ONE / (2 * Z_TILE)),
                      (pixel.y * 2 + size.h) * (FIXED_ONE / (2 * Z_TILE))};
}

/* One spawned native thing; owner 7 holds whatever no team does. */
static mobj_t *spawn(int type, int state, ivec2_t pixel, isize2_t size, int team) {
    mobj_t *mo = P_SpawnMobj(fixed3_from_fixed2(pixel_centre(pixel, size), 0), (uint16_t)type);
    if (!mo) return NULL;
    if (state != mobjinfo[type].spawnstate) P_SetMobjState(mo, state);
    Z_SetTeam(mo, team);
    return mo;
}

int Z_UnitType(const mobj_t *mo) {
    return mo && mo->type_id >= MT_UNITS && mo->type_id < MT_UNITS_END ? mo->type_id - MT_UNITS : -1;
}

bool Z_IsRobot(int t) {
    return t >= 0 && t < Z_UNIT_TYPES && unit_kinds[t].sprite == PICTURE_ROBOT(0);
}

static bool unit_size(const zmap_t *map, int t, isize2_t *size) {
    *size = (isize2_t){Z_TILE, Z_TILE};
    return t >= 0 && t < Z_UNIT_TYPES &&
           (unit_kinds[t].sprite < 0 || Z_SpriteSize(map, unit_kinds[t].sprite, size));
}

bool Z_UnitCentre(const zmap_t *map, int t, ivec2_t pixel, fixed2_t *centre) {
    isize2_t size;
    if (!unit_size(map, t, &size)) return false;
    *centre = pixel_centre(pixel, size);
    return true;
}

static bool read_level_file(const zmap_t *map, const char *name, blob_t *out) {
    char path[1024];
    level_path(path, sizeof(path), map->dir, name);
    return W_ReadFile(path, out);
}

/* A unit of weapon type `t` with its first picture's top-left at `pixel`,
 * facing north as ZED.EXE starts them (0x51019). A gun over a fort
 * stands a cell up on its walls: drawn where ZED.EXE draws it, over the
 * fort. */
static mobj_t *spawn_unit(const zmap_t *map, int t, ivec2_t pixel, int team) {
    isize2_t size;
    if (!unit_size(map, t, &size)) return NULL;
    bool wall = false;
    for (int i = 0; i < map->fort_count && !unit_kinds[t].mobile; ++i) {
        ivec2_t at = ivec2_sub(pixel, map->forts[i]);
        wall |= at.x + size.w > 0 && at.x < 160 && at.y + size.h > 0 && at.y < 176;
    }
    mobj_t *mo = spawn(MT_UNITS + t, mobjinfo[MT_UNITS + t].spawnstate,
                       ivec2_add(pixel, (ivec2_t){0, wall ? Z_TILE : 0}), size, team);
    if (!mo) return NULL;
    mo->core.angle = P_PointToAngle(0, -FIXED_ONE);
    if (wall) mo->core.position.z = FIXED_ONE;
    return mo;
}

/* Weapon type of a group's member, -1 past its last. */
static int member_type(const blob_t *robots, int group, int member) {
    size_t at = RB_GROUPS + (size_t)group * RB_GROUP;
    if (group < 0 || at + RB_GROUP > robots->size || !robots->bytes[at] || member >= RB_MAX_MEMBERS)
        return -1;
    int robot = read_u16_le(robots->bytes + at + RB_MEMBERS + member * 2);
    if (robot == 0xffff || (size_t)(robot + 1) * RB_ROBOT > RB_GROUPS) return -1;
    return robots->bytes[robot * RB_ROBOT + RB_WEAPON];
}

/* ZED.EXE 0x50665: the first member at `at`, the rest four pixels apart in
 * rows of four; they spread out once they move. Members share a squad,
 * which shares grenades (0x79bea). */
static uint8_t squads;

static int spawn_group(const zmap_t *map, const blob_t *robots, int group, ivec2_t at, int team) {
    int count = 0;
    uint8_t squad = ++squads ? squads : ++squads;
    for (int i = 0, t; (t = member_type(robots, group, i)) >= 0; ++i) {
        mobj_t *mo = spawn_unit(map, t, ivec2_add(at, (ivec2_t){(i & 3) * 4, i & ~3}), team);
        if (!mo) continue;
        mo->z.squad = squad;
        count++;
    }
    return count;
}

/* The player's groups wait around their fort (0x503dc); guns take the
 * fort's emplacements and do not use a start position. The computer's
 * come from CPUPLRnn.DAT, owned by the territory they stand in or else by
 * the other fort. */
static int spawn_armies(const zmap_t *map, ivec2_t fort, int cpu_team) {
    char path[1024], name[LV_NAME + 1];
    blob_t robots;
    snprintf(name, sizeof(name), "%s", map->robots);
    for (char *c = name; *c; ++c) *c = (char)toupper((unsigned char)*c);
    M_PathJoin(path, sizeof(path), map->root, name);
    if (!W_ReadFile(path, &robots)) return 0;
    int count = 0, start = 0, gun = 0;
    for (int g = 0; fort.x >= 0 && g < Z_MAX_GROUPS && map->groups[g] != 0xff; ++g) {
        int t = member_type(&robots, map->groups[g], 0);
        if (t < 0 || t >= Z_UNIT_TYPES) continue;
        if (unit_kinds[t].mobile) {
            const int16_t *at = start_offsets[start_table[map->number]][start++];
            count += spawn_group(map, &robots, map->groups[g], ivec2_add(fort, (ivec2_t){at[0], at[1]}), 0);
            continue;
        }
        if (gun >= 6) continue;
        const int16_t *slot = gun_slots[gun_remap[gun_order[map->number * 6 + gun++] - 'A'] - 'A'];
        ivec2_t at = {slot[0], slot[1]};
        if (unit_kinds[t].turns) at = ivec2_add(at, (ivec2_t){-2, -8});
        if (spawn_unit(map, t, ivec2_add(fort, at), 0)) count++;
    }
    for (int i = 0; i < map->placed_count; ++i) {
        int team = Z_TeamAt(map, map->placed[i].at);
        count += spawn_group(map, &robots, map->placed[i].group, map->placed[i].at,
                             team < Z_NEUTRAL ? team : cpu_team);
    }
    W_FreeFile(&robots);
    return count;
}

int Z_LoadThings(void) {
    const zmap_t *map = level.native_data;
    if (!map) return 0;
    int count = 0, cpu_team = 1;
    ivec2_t player_fort = {-1, -1};
    squads = 0;
    for (int i = 0; i < map->fort_count; ++i) {
        int team = Z_TeamAt(map, map->forts[i]);
        if (spawn(MT_FORT, i ? S_FORT_CPU : S_FORT, map->forts[i], (isize2_t){160, 176}, team)) count++;
        if (team == 0 || getenv("ZCAM")) {
            if (team == 0) player_fort = map->forts[i];
            level.has_camera = true;
            level.camera = (fvec2_t){(map->forts[i].x + 80) / (float)Z_TILE,
                                     (map->forts[i].y + 88) / (float)Z_TILE};
        } else if (team < Z_NEUTRAL) {
            cpu_team = team;
        }
    }
    if (getenv("ZCAM")) { level.camera = (fvec2_t){(map->forts[!(Z_TeamAt(map, map->forts[0]) == 1)].x + 80) / (float)Z_TILE, (map->forts[!(Z_TeamAt(map, map->forts[0]) == 1)].y + 88) / (float)Z_TILE}; }
    count += spawn_armies(map, player_fort, cpu_team);

    /* 0x2a89f: a flag on each territory's flag cell, in its team's colour. */
    for (int i = 0; i < map->territory_count; ++i) {
        const zterritory_t *t = &map->territories[i];
        if (t->options & 3) continue;
        mobj_t *flag = spawn(MT_FLAG, S_FLAG, ivec2_scale(t->flag, Z_TILE), (isize2_t){Z_TILE, Z_TILE},
                             t->team);
        if (!flag) continue;
        flag->z.territory = (uint8_t)(i + 1);
        /* draw_flags sorts a flag 32 pixels lower so a fort's roof does
         * not hide it: stand it two cells down and as high, which draws it
         * in place and after the ground. */
        flag->core.position.y += 2 * FIXED_ONE;
        flag->core.position.z = 2 * FIXED_ONE;
        count++;
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
            ivec2_t pixel = {read_u16_le(r), read_u16_le(r + 2)};
            isize2_t size;
            if (!Z_SpriteSize(map, states[mobjinfo[type].spawnstate].frame, &size)) continue;
            /* An empty gun or vehicle names its weapon type in byte 5.
             * Nobody drives it: it keeps the object's crewless picture
             * and neither moves nor fights until a robot gets in
             * (p_capture.c); it can still be picked as the place to go. */
            if (object_flags[r[4]] == 0x32 && r[5] < Z_UNIT_TYPES) {
                mobj_t *mo = spawn(MT_UNITS + r[5], mobjinfo[type].spawnstate, pixel, size, Z_NEUTRAL);
                if (mo) {
                    mo->traits &= ~(MF_MOBILE | MF_ATTACK);
                    count++;
                }
                continue;
            }
            if (spawn(type, mobjinfo[type].spawnstate, pixel, size, Z_NEUTRAL)) count++;
        }
        W_FreeFile(&blob);
    }
    return count;
}
