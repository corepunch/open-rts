/* Print 7th Legion unit and building stats as the retail game loads them.

   legion.exe holds default tables; at startup (0x004023a3 -> 0x004716e0) it
   overwrites them from DATA/stuff.dat. Field meanings come from the game's
   own "Debug Edit" stats screen (0x00471d33..0x00472100). See
   docs/7LEGION_EXE_FINDINGS.md.

   Usage:
     7legion_units_extract <7legion-root>
*/
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    VEHICLES = 0x4c0860, VEHICLE_SIZE = 76, VEHICLE_COUNT = 44,
    BUILDINGS = 0x43d820 + 1000 * 516, BUILDING_SIZE = 516, BUILDING_COUNT = 23,
    VT_NAMES = 0x4baa70, BT_NAMES = 0x4ba688, NAME_SIZE = 20,
    SIDES = 0x4c4aa0, DAMAGE = 0x4b5ee8, DAMAGE_SIZE = 1600,
    WEAPON_NAME_CASES = 0x473a90, WEAPON_CLASS_CASES = 0x471354,
};

/* Jump-target order of the weapon-name (0x00473a58) and damage-class
   (0x00471318) switches; both index weapon - 2, weapons 2..43. */
static const char *const WEAPON_NAMES[] = {
    "Laser", "Shell 1", "Plasma Rifle", "Shell 2", "Rocket", "Grenade", "Zapper",
    "Laser Bolt", "Missile Pack", "Fast Laser", "Flame Thrower", "Priest thing",
    "Mortar Round", "None",
};
static const int WEAPON_CLASSES[] = { 1, 2, 0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
static const char *const ARMOURS[] = {
    "Body", "Light", "Medium", "Heavy", "Structure",
};

typedef struct { uint8_t *data; long size; } file_t;
typedef struct { uint32_t va, size, raw, raw_size; } section_t;

static section_t sections[16];
static int section_count;
static file_t exe;

static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int16_t i16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

static file_t read_file(const char *path) {
    file_t f = { 0 };
    FILE *in = fopen(path, "rb");
    if (!in) return f;
    fseek(in, 0, SEEK_END);
    f.size = ftell(in);
    rewind(in);
    f.data = f.size > 0 ? malloc((size_t)f.size) : NULL;
    if (!f.data || fread(f.data, 1, (size_t)f.size, in) != (size_t)f.size) {
        free(f.data);
        f = (file_t){ 0 };
    }
    fclose(in);
    return f;
}

static int load_sections(void) {
    if (exe.size < 0x40) return 0;
    uint32_t pe = u32(exe.data + 0x3c);
    if (pe + 24 > (uint32_t)exe.size || memcmp(exe.data + pe, "PE\0\0", 4) != 0) return 0;
    int count = exe.data[pe + 6] | exe.data[pe + 7] << 8;
    uint32_t table = pe + 24 + (exe.data[pe + 20] | exe.data[pe + 21] << 8);
    for (int i = 0; i < count && i < 16; ++i) {
        const uint8_t *s = exe.data + table + i * 40;
        if (table + i * 40 + 40 > (uint32_t)exe.size) return 0;
        sections[section_count++] = (section_t){
            0x400000 + u32(s + 12), u32(s + 8), u32(s + 20), u32(s + 16) };
    }
    return section_count;
}

/* Initialized bytes at a virtual address, or NULL. */
static const uint8_t *at(uint32_t va, uint32_t size) {
    for (int i = 0; i < section_count; ++i) {
        const section_t *s = &sections[i];
        if (va >= s->va && va + size <= s->va + s->raw_size &&
            s->raw + (va - s->va) + size <= (uint32_t)exe.size)
            return exe.data + s->raw + (va - s->va);
    }
    return NULL;
}

static const char *weapon_name(int weapon) {
    if (weapon < 2 || weapon > 43) return "None";
    return WEAPON_NAMES[*at(WEAPON_NAME_CASES + weapon - 2, 1)];
}

static int weapon_class(int weapon) {
    if (weapon < 2 || weapon > 43) return -1;
    int c = *at(WEAPON_CLASS_CASES + weapon - 2, 1);
    return c < 14 ? WEAPON_CLASSES[c] : -1;
}

static const char *armour_name(int armour) {
    return armour >= 0 && armour < 5 ? ARMOURS[armour] : "None";
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: 7legion_units_extract <7legion-root>\n");
        return 1;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/legion.exe", argv[1]);
    exe = read_file(path);
    if (!exe.data || !load_sections() ||
        !at(VEHICLES, VEHICLE_SIZE * VEHICLE_COUNT) || !at(DAMAGE, DAMAGE_SIZE) ||
        !at(BUILDINGS, BUILDING_SIZE * BUILDING_COUNT)) {
        fprintf(stderr, "7legion_units_extract: cannot read tables from %s\n", path);
        return 1;
    }
    snprintf(path, sizeof(path), "%s/DATA/stuff.dat", argv[1]);
    file_t stuff = read_file(path);
    if (!stuff.data) {
        fprintf(stderr, "7legion_units_extract: cannot read %s\n", path);
        return 1;
    }

    /* Copy the defaults, then apply stuff.dat exactly as 0x004716e0 does. */
    uint8_t vehicles[VEHICLE_SIZE * VEHICLE_COUNT], buildings[BUILDING_SIZE * BUILDING_COUNT];
    uint8_t damage[DAMAGE_SIZE], overridden[VEHICLE_COUNT + BUILDING_COUNT] = { 0 };
    memcpy(vehicles, at(VEHICLES, sizeof(vehicles)), sizeof(vehicles));
    memcpy(buildings, at(BUILDINGS, sizeof(buildings)), sizeof(buildings));
    memcpy(damage, at(DAMAGE, sizeof(damage)), sizeof(damage));
    long pos = 0;
    for (;;) {
        if (pos + 4 > stuff.size) goto truncated;
        int32_t id = (int32_t)u32(stuff.data + pos);
        pos += 4;
        if (id == -1) break;
        bool building = id >= 1000;
        int fields = building ? 5 : 9;
        int index = building ? id - 1000 : id;
        if (pos + fields * 4 > stuff.size || index < 0 ||
            index >= (building ? BUILDING_COUNT : VEHICLE_COUNT)) goto truncated;
        const uint8_t *v = stuff.data + pos;
        pos += fields * 4;
        if (building) {
            uint8_t *b = buildings + index * BUILDING_SIZE;
            memcpy(b + 0x04, v, 4);               /* cost */
            memcpy(b + 0x14, v + 4, 2);           /* armour */
            memcpy(b + 0x18, v + 8, 2);           /* weapon */
            memcpy(b + 0x00, v + 12, 2);          /* health */
            memcpy(b + 0x08, v + 16, 2);          /* build time */
            overridden[VEHICLE_COUNT + index] = 1;
        } else {
            uint8_t *r = vehicles + index * VEHICLE_SIZE;
            static const int offsets[9] = { 0x04, 0x14, 0x0e, 0x10, 0x1c, 0x20, 0x02, 0x08, 0x12 };
            for (int i = 0; i < 9; ++i)
                memcpy(r + offsets[i], v + i * 4, offsets[i] == 0x04 || offsets[i] == 0x1c ? 4 : 2);
            overridden[index] = 1;
        }
    }
    if (pos + DAMAGE_SIZE != stuff.size) goto truncated;
    memcpy(damage, stuff.data + pos, DAMAGE_SIZE);

    printf("%-3s %-14s %-4s %5s %5s %6s %-9s %-13s %-13s %6s %5s %6s %-10s %s\n",
           "id", "vehicle", "side", "cost", "build", "health", "armour", "weapon",
           "weapon2", "speed", "turn", "reload", "source", "routine");
    for (int i = 0; i < VEHICLE_COUNT; ++i) {
        const uint8_t *r = vehicles + i * VEHICLE_SIZE;
        int side = i16(r + 0x24);
        const char *prefix = side >= 0 && side < 2 ? (const char *)at(SIDES + side * 8, 8) : "?";
        printf("%-3d %-14.20s %-4s %5u %5d %6d %-9s %-13s %-13s %6.3f %5d %6d %s 0x%08x\n", i,
               (const char *)at(VT_NAMES + i * NAME_SIZE, NAME_SIZE) + 3, prefix, u32(r + 0x04),
               i16(r + 0x08), i16(r + 0x02), armour_name(i16(r + 0x14)),
               weapon_name(i16(r + 0x0e)), weapon_name(i16(r + 0x10)),
               (int32_t)u32(r + 0x1c) / 65536.0, i16(r + 0x20), i16(r + 0x12),
               overridden[i] ? "stuff.dat" : "legion.exe", u32(r + 0x38));
    }

    printf("\n%-3s %-14s %5s %5s %6s %-9s %-13s %s\n",
           "id", "building", "cost", "build", "health", "armour", "weapon", "source");
    for (int i = 0; i < BUILDING_COUNT; ++i) {
        const uint8_t *b = buildings + i * BUILDING_SIZE;
        printf("%-4d %-13.20s %5u %5d %6d %-9s %-13s %s\n", 1000 + i,
               (const char *)at(BT_NAMES + i * NAME_SIZE, NAME_SIZE) + 3, u32(b + 0x04),
               i16(b + 0x08), i16(b + 0x00), armour_name(i16(b + 0x14)),
               weapon_name(i16(b + 0x18)), overridden[VEHICLE_COUNT + i] ? "stuff.dat" : "legion.exe");
    }

    printf("\n%-13s %5s %5s %6s %5s %9s\n", "weapon", ARMOURS[0], ARMOURS[1], ARMOURS[2],
           ARMOURS[3], ARMOURS[4]);
    for (int w = 2; w <= 43; ++w) {
        int c = weapon_class(w);
        if (c < 0) continue;
        printf("%-13s", weapon_name(w));
        for (int a = 0; a < 5; ++a)
            printf(" %5d", (int32_t)u32(damage + (c * 20 + a) * 4) >> 16);
        printf("   (weapon %d, class %d)\n", w, c);
    }
    free(stuff.data);
    free(exe.data);
    return 0;

truncated:
    fprintf(stderr, "7legion_units_extract: unexpected stuff.dat layout at byte %ld\n", pos);
    free(stuff.data);
    free(exe.data);
    return 1;
}
