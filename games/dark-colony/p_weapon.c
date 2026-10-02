#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include "gamestat.h"
#include <stdlib.h>
#include <string.h>

/* DC.EXE: 0x4746ac sampled every 32 entries; atan at 0x4756ae. */
static const int16_t dc_sine[65] = {
0, 50, 100, 150, 200, 250, 300, 350, 399, 448, 497, 546,
594, 642, 689, 737, 783, 829, 875, 920, 965, 1009, 1052, 1095,
1137, 1179, 1219, 1259, 1299, 1337, 1375, 1412, 1448, 1483, 1517, 1550,
1583, 1614, 1644, 1674, 1702, 1730, 1756, 1781, 1806, 1829, 1851, 1872,
1892, 1910, 1928, 1944, 1959, 1973, 1986, 1998, 2008, 2017, 2025, 2032,
2038, 2042, 2045, 2047, 2048,
};
static const int16_t dc_atan[257] = {
0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55,
61, 66, 71, 76, 81, 86, 91, 96, 101, 106, 111, 116,
121, 126, 131, 137, 142, 147, 152, 157, 162, 167, 172, 177,
182, 187, 192, 197, 202, 207, 212, 216, 221, 226, 231, 236,
241, 246, 251, 256, 261, 266, 271, 275, 280, 285, 290, 295,
300, 304, 309, 314, 319, 324, 328, 333, 338, 343, 348, 352,
357, 362, 366, 371, 376, 380, 385, 390, 394, 399, 404, 408,
413, 417, 422, 427, 431, 436, 440, 445, 449, 454, 458, 463,
467, 472, 476, 481, 485, 489, 494, 498, 503, 507, 511, 516,
520, 524, 529, 533, 537, 541, 546, 550, 554, 558, 563, 567,
571, 575, 579, 583, 588, 592, 596, 600, 604, 608, 612, 616,
620, 624, 628, 632, 636, 640, 644, 648, 652, 656, 660, 664,
668, 671, 675, 679, 683, 687, 691, 694, 698, 702, 706, 709,
713, 717, 720, 724, 728, 731, 735, 739, 742, 746, 750, 753,
757, 760, 764, 767, 771, 774, 778, 781, 785, 788, 792, 795,
798, 802, 805, 809, 812, 815, 819, 822, 825, 829, 832, 835,
838, 842, 845, 848, 851, 855, 858, 861, 864, 867, 870, 874,
877, 880, 883, 886, 889, 892, 895, 898, 901, 904, 907, 910,
913, 916, 919, 922, 925, 928, 931, 934, 937, 940, 942, 945,
948, 951, 954, 957, 959, 962, 965, 968, 971, 973, 976, 979,
981, 984, 987, 990, 992, 995, 998, 1000, 1003, 1005, 1008, 1011,
1013, 1016, 1018, 1021, 0,
};

struct dc_weapons_s { dc_fin_t artillery[2]; };

void DC_FreeWeapons(level_t *map) {
    if (!map->weapons) return;
    for (int i = 0; i < 2; ++i) DC_FreeFIN(&map->weapons->artillery[i]);
    free(map->weapons);
    map->weapons = NULL;
}

bool DC_LoadWeapons(level_t *map, const char *root) {
    DC_FreeWeapons(map);
    map->weapons = calloc(1, sizeof(*map->weapons));
    if (!map->weapons) return false;
    const char *names[] = {"BARR", "ATRIL"};
    for (int i = 0; i < 2; ++i) {
        if (!DC_LoadFIN(M_va("%s/ANIMATE/%s.FIN", root, names[i]), &map->weapons->artillery[i])) {
            DC_FreeWeapons(map);
            return false;
        }
    }
    return true;
}

mobj_t *DC_FireMissiles(mobj_t *source, mobj_t *target, uint16_t type) {
    (void)P_DC_Random(); /* 0x41228d: attack-animation choice, also for one variant. */
    const dc_fin_t *fin = NULL;
    const dc_fin_label_t *label = NULL;
    if (level.weapons && (source->type_id == MT_THUNDERBOLT || source->type_id == MT_ATRIL)) {
        int atril = source->type_id == MT_ATRIL;
        fin = &level.weapons->artillery[atril];
        /* Native facing rounded to sixteen, then animation's eight rotations. */
        int direction = (16 - dc_angle_to_direction(source->core.angle)) & 15;
        direction &= ~1;
        label = DC_FINLabel(fin, M_va("%sFIREA%d", atril ? "ATRIL" : "BARR", direction));
        if (label && (SDL_SwapLE16(label->end) >= SDL_SwapLE16(fin->header->frame_count) ||
                      SDL_SwapLE16(label->start) > SDL_SwapLE16(label->end))) label = NULL;
    }
    mobj_t *first = NULL;
    int elapsed = 0, count = 0;
    if (label) {
        /* 0x423d00 returns up to eight channel-7 launch records. */
        for (int frame = SDL_SwapLE16(label->start); frame <= SDL_SwapLE16(label->end) && count < 8; ++frame) {
            const dc_fin_frame_t *record = &fin->frames[frame];
            const dc_fin_point_t *point = &record->points[7];
            if (point->name[0] && strncmp(point->name, "NONAME", sizeof(point->name))) {
                mobj_t *shot = P_SpawnMissile(source, target, type);
                if (!shot) break;
                fixed3_t offset = {(int16_t)SDL_SwapLE16(point->x) * (8 * 256),
                                  -(int16_t)SDL_SwapLE16(point->y) * (8 * 256), 0};
                shot->core.position = fixed3_add(shot->core.position, offset);
                shot->missile.wait = elapsed * 4;
                if (shot->missile.wait) shot->traits &= ~MF_RENDERABLE;
                DC_ScatterMissile(shot, target->core.position);
                if (!first) first = shot;
                count++;
            }
            int ticks = SDL_SwapLE16(record->ticks);
            elapsed += ((ticks ? ticks : 15) + 3) * 15 / 100;
        }
    }
    if (!count) {
        first = P_SpawnMissile(source, target, type);
        if (first) DC_ScatterMissile(first, target->core.position);
    }
    return first;
}

int DC_WeaponLevel(const mobj_t *actor) {
    if (!actor || actor->native_type_id >= 106 || actor->owner >= 8) return 0;
    int upgrade = level.upgrades[actor->native_type_id][actor->owner].weapon;
    return upgrade <= 2 ? upgrade : 0;
}

int DC_DefendedDamage(const mobj_t *victim, int damage) {
    if (!victim->info || victim->native_type_id >= 106 || victim->owner >= 8) return damage;
    int upgrade = level.upgrades[victim->native_type_id][victim->owner].armor;
    int factor = upgrade <= 2 ? victim->info->defense[upgrade] : 0;
    return factor ? damage * factor / 256 : damage;
}

/* DC.EXE 0x43ecbd-0x43ece1: a direct hit flags the shot when the shooter's
 * GAMESTAT race (type +0x04) is human at night or alien by day; 0x43df64
 * then applies (damage*3)>>2 after defense. The test reads phase +0x53c, not
 * the blended weight. Area blasts pass zero (0x43e678) and stay unchanged. */
int DC_DaylightDamage(const mobj_t *shooter, int damage) {
    if (!shooter || shooter->native_type_id >= GAMESTAT_UNIT_COUNT) return damage;
    int race = dc_gamestat_units[shooter->native_type_id].values[GAMESTAT_UNIT_RACE];
    bool night = level.daylight.phase == 1;
    if ((race == 0 && night) || (race == 1 && !night)) return damage * 3 >> 2;
    return damage;
}

void DC_AimMissile(mobj_t *missile, fixed3_t destination) {
    /* 0x43d930 / 0x411614: atan ratio, quadrant, then 256 headings. */
    fixed3_t delta = fixed3_sub(destination, missile->core.position);
    int x = delta.x / 256, y = delta.y / 256;
    int ax = abs(x), ay = abs(y), angle = 0;
    if (ax || ay) {
        angle = ax >= ay ? dc_atan[ay * 256 / ax] : 2048 - dc_atan[ax * 256 / ay];
        if (x < 0) angle = 4096 - angle;
        if (y < 0) angle = (8192 - angle) & 8191;
    }
    int heading = angle / 32;
    int quarter = heading / 64, index = heading % 64;
    int sine = dc_sine[index], cosine = dc_sine[64 - index];
    ivec2_t direction;
    switch (quarter) {
        case 0: direction = (ivec2_t){cosine,sine}; break;
        case 1: direction = (ivec2_t){-sine,cosine}; break;
        case 2: direction = (ivec2_t){-cosine,-sine}; break;
        default: direction = (ivec2_t){sine,-cosine}; break;
    }
    int speed = missile->info->missile.step / 256;
    ivec2_t velocity = {direction.x * speed / 2048, direction.y * speed / 2048};
    int duration = abs(velocity.x) > abs(velocity.y) ? x / velocity.x :
                   velocity.y ? y / velocity.y : 0;
    missile->missile.duration = duration;
    missile->core.momentum = (fixed3_t){velocity.x * 256, velocity.y * 256,
        duration ? (delta.z / 256 / duration) * 256 : 0};
    missile->core.angle = (angle_t)heading << 24;
}

void DC_ScatterMissile(mobj_t *missile, fixed3_t destination) {
    if (!missile->info->blast.size) return;
    /* Fire consumes a random byte even for center-only BOOMSTAT rows. */
    unsigned value = P_DC_Random() & 255;
    if (missile->type_id == MT_CANNONBALL || missile->type_id == MT_PUS_BOMB) {
        static const uint8_t weights[] = {3,10,3,10,48,10,3,10,3};
        int index = 0;
        while (index < 9 && value >= weights[index] * 256u / 100u) {
            value -= weights[index] * 256u / 100u;
            index++;
        }
        /* The floored weights total 250. Retail exits with row=column=3
         * for bytes 250..255, producing the unusual (+2,+2) aim offset. */
        ivec2_t offset = index == 9 ? (ivec2_t){2,2} : (ivec2_t){index % 3 - 1,index / 3 - 1};
        destination = fixed3_add(destination,
            (fixed3_t){offset.x * FIXED_ONE, offset.y * FIXED_ONE, 0});
    }
    DC_AimMissile(missile, destination);
}

void A_DC_ArtilleryExplode(mobj_t *actor) {
    /* 0x43e501..0x43e521 selects one BOOMSTAT animation, not both. */
    int state = P_DC_Random() % 2 ? S_GASY1 : S_NUKE1;
    A_Explode(actor);
    P_SetMobjState(actor, state);
}
