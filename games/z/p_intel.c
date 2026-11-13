#include "z_local.h"

/* ZED.EXE 0x9830b: estimates borrow a seed from the value and restore the
 * game's random stream. Repeated observations therefore have the same error. */
int Z_AIEstimate(int value, int intelligence) {
    if (intelligence == 127) return value;
    uint16_t seed = (uint16_t)value;
    bool subtract = Z_NextRandom(&seed) & 0x100;
    unsigned roll = Z_NextRandom(&seed) / (65535u / (128 - intelligence) + 1);
    int delta = (int)((uint64_t)roll * (uint32_t)value >> 7);
    return subtract ? value - delta : value + delta;
}

int Z_AITerritory(const zmap_t *map, fixed2_t at) {
    ivec2_t cell = fixed2_cell(at);
    for (int i = 0; i < map->territory_count; ++i) {
        const zterritory_t *t = &map->territories[i];
        if (cell.x >= t->min.x && cell.y >= t->min.y &&
            cell.x < t->max.x && cell.y < t->max.y) return i;
    }
    return -1;
}

/* Native units store the sprite's top-left in +44/+46. */
ivec2_t Z_AITopLeft(const mobj_t *mo) {
    const zmap_t *map = level.native_data;
    fixed2_t half = {0};
    if (!Z_UnitCentre(map, Z_UnitType(mo), (ivec2_t){0}, &half)) return (ivec2_t){0};
    fixed2_t at = fixed2_sub(fixed3_xy(mo->core.position), half);
    at = fixed2_sub(at, (fixed2_t){0,mo->core.position.z});
    return fixed2_cell(fixed2_scale(at, Z_TILE * FIXED_ONE));
}

/* 0x9a29c divides only AX, retaining the score's upper word. Byte
 * subtraction and arithmetic shift precede the SQROOT.BIN lookup. */
uint32_t Z_AIRank(int value, ivec2_t from, ivec2_t to, const uint8_t *roots) {
    int dx = (int8_t)(to.x - from.x) >> 2, dy = (int8_t)(to.y - from.y) >> 2;
    int i = dx * dx + dy * dy;
    if (i > 1023) i = 1023;
    uint32_t score = value < 0 ? 0 : (uint32_t)value;
    return (score & 0xffff0000u) | (uint16_t)score / roots[i];
}

/* 0x97fad and 0x9b679: the percentage is an allocation to the reserve,
 * not an aggression multiplier. Home means territories with asset bit 7. */
int Z_AIMobileBudget(int total, int imbalance, int home_human, int home_computer, int allocation) {
    int credit = 0, deficit = 0;
    if (home_human >= home_computer) {
        int difference = home_human - home_computer;
        credit = difference + (allocation - 50) * difference / 100;
    } else deficit = home_human + (allocation - 50) * home_human / 100;
    int mobile = total * (100 - allocation) / 100 + imbalance - credit;
    if (mobile < 0) mobile = 0;
    if (mobile > total) mobile = total;
    int shortage = deficit - (total - mobile);
    if (shortage > 0) mobile -= shortage;
    return mobile < 0 ? 0 : mobile;
}

/* The relocated tables at 0x152a78/0x152ac4 point into MULT.DAT. */
static const uint16_t robot_weights[] = {7,14,21,7,21,21,21,14,14,14,14,14,14,14,14,7,7,7,7};
static const uint16_t weapon_weights[] = {
    0x1c,0x3b,0x5a,0x79,0xc2,0xe1,0x100,0x11f,0x13e,0x15d,
    0xa3,0x17c,0x19b,0x1ba,0x1d9,0x1f8,0x217,0x236,0x255,
};

static int weighted(const uint8_t *weights, const int *values, int count, int factor) {
    unsigned sum = 0, fields = 0;
    for (int i = 0; i < count; ++i)
        if (weights[i] != 255) { sum += weights[i] * values[i]; fields++; }
    return sum && fields ? (int)(sum * factor / fields) : 0;
}

static const uint8_t *weapon(const zmap_t *map, const mobj_t *mo) {
    int t = Z_UnitType(mo);
    size_t at = 22000 + (size_t)t * 42;
    return t >= 0 && at + 42 <= map->roster.size ? map->roster.bytes + at : NULL;
}

int Z_AIUnitValue(const mobj_t *mo) {
    const zmap_t *map = level.native_data;
    const uint8_t *w = weapon(map, mo);
    if (!w || w[1] >= 19 || !mo->z.robot || mo->z.robot * 44u > 18000) return 0;
    const uint8_t *r = map->roster.bytes + (mo->z.robot - 1) * 44;
    bool robot = Z_IsRobot(Z_UnitType(mo));
    int health = robot && mo->max_hp ? r[32] * mo->hp / mo->max_hp : r[32];
    int reach = (uint8_t)(read_u16_le(w + 2) >> 3);
    int human[] = {r[28],r[29],(uint8_t)(reach * 2),r[31],health,r[29],r[36]};
    int arm[] = {reach * 8,0,0,w[32],w[34],w[35],0,0,0,0};
    if (!robot) {
        int body = mo->max_hp ? 25 * w[5] * mo->hp / mo->max_hp : 0;
        /* 0x50c23 copies weapon +37 to vehicle +35. Boarding follows
         * object +27 to vehicle +35 instead (0x63a73). */
        arm[1] = w[4]; arm[2] = body; arm[6] = 8 * w[36];
        arm[7] = w[37]; arm[8] = w[38]; arm[9] = w[39];
    }
    const uint8_t *rw = map->weights.bytes + robot_weights[w[1]];
    const uint8_t *ww = map->weights.bytes + weapon_weights[w[1]];
    return weighted(rw, human, 7, 1) + weighted(ww, arm, 10, ww[10]) + 25 * mo->z.grenades;
}

int Z_AIEmptyValue(const mobj_t *mo) {
    const zmap_t *map = level.native_data;
    const uint8_t *w = weapon(map, mo);
    int object = (int)mo->z.object - 1, cls;
    switch (object) {
    case 5: cls = 2; break; case 8: cls = 1; break; case 35: cls = 12; break;
    case 26: cls = 8; break; case 29: cls = 9; break; case 31: cls = 10; break;
    case 21: cls = 4; break; case 22: cls = 5; break; case 23: cls = 6; break;
    case 33: cls = 11; break; case 24: cls = 7; break;
    default: return object < 0 ? 0 : object; /* Native early return, including Fire Truck. */
    }
    if (!w) return 0;
    int values[] = {(uint8_t)(read_u16_le(w + 2) >> 3) * 8,w[4],mo->z.object_hp * 25,
                    w[32],w[34],w[35],8 * w[36],w[37],w[38],w[39]};
    const uint8_t *ww = map->weights.bytes + weapon_weights[cls];
    return weighted(ww, values, 10, ww[10]);
}
