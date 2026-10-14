#include "z_local.h"

/* Destructible cliffs. The map keeps them as twenty 138-byte blocks (u16 x,
 * y in pixels, the height in cells at +4, a 32x32 cell bitmask at +0xa, most
 * significant bit first), and ZED.EXE 0x452f2 builds their tiles into the
 * planes when a level starts. This follows 0x45352 and its helpers. */

#include "cliffs.inc"

enum { BLOCKS = 20, BLOCK_SIZE = 0x8a, PLANE = Z_MAP_STRIDE * Z_MAP_STRIDE };

/* 0x1faa94 and 0x1faad4: inside tiles by variant, edge tiles by the sides
 * with no cliff next to them (bit 0 up, 1 right, 2 down, 3 left). */
static const uint8_t inside[16] = {12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};
static const uint8_t edges[16] = {10, 3, 28, 6, 34, 43, 37, 46, 9, 0, 55, 52, 31, 40, 58, 49};

typedef struct {
    uint8_t *tile, *attr;
    const uint8_t *blocks;
    const uint16_t *t;
    uint16_t seed;
} cliffs_t;

/* 0x70d37: a 16-bit shift register stepped through rcr/rcl. */
static uint16_t next_random(uint16_t *seed) {
    uint8_t lo = *seed & 0xff, hi = *seed >> 8, al = hi, cf = 0, out;
#define RCR() (out = al & 1, al = (uint8_t)(al >> 1 | cf << 7), cf = out)
    /* xor and and clear the carry the next rcr shifts in. */
    RCR(); hi ^= al; cf = 0;
    RCR(); RCR(); hi ^= al; cf = 0;
    RCR(); lo ^= al; cf = 0;
    RCR(); RCR();
#undef RCR
    al &= 1;
    cf = lo >> 7;
    lo = (uint8_t)(lo << 1);
    hi = (uint8_t)(hi << 1 | cf);
    lo = (uint8_t)(lo + al);
    *seed = (uint16_t)(hi << 8 | lo);
    return *seed;
}

/* Bit tests read straight through the blocks, as the original does: the
 * last row's down test looks into the next block. */
static bool cell_bit(const uint8_t *block, int bit) {
    return block[0xa + (bit >> 3)] & (0x80 >> (bit & 7));
}

static uint16_t cell(const cliffs_t *c, int i) {
    return i >= 0 && i < PLANE ? (uint16_t)(c->tile[i] | (c->attr[i] & 0x80) << 8) : 0;
}

static uint8_t terrain(const cliffs_t *c, int i) {
    return i >= 0 && i < PLANE ? c->attr[i] & 0x7f : 0;
}

static void put(cliffs_t *c, int i, uint16_t value) {
    if (i < 0 || i >= PLANE) return;
    c->tile[i] = value & 0xff;
    c->attr[i] = value >> 8;
}

/* 0x45db1: whether the cell right of `i` is unshadeable ground, or under
 * another block's cliff (its cell or one of the rows its face covers). */
static bool covered(const cliffs_t *c, const uint8_t *self, int i) {
    uint8_t ground = terrain(c, i + 1);
    if (ground >= sizeof(cliff_shades) || !cliff_shades[ground]) return true;
    ivec2_t px = {((i + 1) & 0x7f) * Z_TILE, ((i + 1) >> 7) * Z_TILE};
    for (int b = 0; b < BLOCKS; ++b) {
        const uint8_t *block = c->blocks + b * BLOCK_SIZE;
        if (block == self) continue;
        int dx = (int16_t)(px.x - read_u16_le(block));
        if (dx < 0 || dx >= 0x200) continue;
        int y = px.y;
        for (int rows = block[4] + 1; rows > 0; --rows, y -= Z_TILE) {
            int dy = (int16_t)(y - read_u16_le(block + 2));
            if (dy < 0 || dy >= 0x200) break;
            if (cell_bit(block, (dx >> 4) + (dy >> 4) * 32)) return true;
        }
    }
    return false;
}

/* 0x455ba: the shadow tile that suits what already lies right of `i`. */
static uint16_t shadow_for(const cliffs_t *c, int i, uint16_t fallback) {
    const uint16_t *t = c->t;
    uint16_t right = cell(c, i + 1);
    if (right == t[87]) return t[88];
    if (right == t[88]) return right;
    if (right == (uint16_t)(t[49] + 0x14)) return t[87];
    if (right == (uint16_t)(t[49] + 0x28)) return t[88];
    if (right == t[65] || right == t[66] || right == t[67] || right == t[68]) return t[61];
    static const uint8_t from[] = {77, 71, 80, 69, 78, 72, 81, 70, 79, 73, 82};
    static const uint8_t to[] = {62, 87, 88, 83, 85, 87, 88, 84, 86, 87, 88};
    for (size_t k = 0; k < sizeof(from); ++k)
        if (right == t[from[k]]) return t[to[k]];
    return fallback;
}

/* 0x454a7: shade the column right of the cliff face below `i`. */
static void shade(cliffs_t *c, const uint8_t *block, int i, int8_t height) {
    const uint16_t *t = c->t;
    uint16_t top = (uint16_t)(c->tile[i] | terrain(c, i) << 8);
    if (top != 0x0c && top != 0x0e && top != 0x10 && terrain(c, i + 1) == 0x12)
        put(c, i + 1, shadow_for(c, i, t[62]) | 0x1200);
    for (int8_t rows = height;; --rows) {
        i += Z_MAP_STRIDE;
        if (i + 1 >= PLANE) return;
        if (covered(c, block, i)) continue;
        if (rows < 1) return;
        if (rows == 1) {
            put(c, i + 1, t[64]);
            if (i - 127 >= 0 && c->tile[i - 127] == (t[64] & 0xff) && c->attr[i - 127] == t[64] >> 8)
                put(c, i - 127, t[63]);
            return;
        }
        uint16_t value = t[63];
        uint16_t above = cell(c, i - 127);
        if (terrain(c, i + 1) == 0x12 && (above == t[87] || above == t[88]))
            value = t[88] | 0x1200;
        else if (terrain(c, i + 1) == 0x12)
            value = shadow_for(c, i, t[62]) | 0x1200;
        put(c, i + 1, value);
    }
}

/* 0x456f4: the cliff top for one cell; returns the tile its face hangs from. */
static uint16_t top(cliffs_t *c, const uint8_t *block, int bit, int i, int variant) {
    const uint16_t *t = c->t;
    int sides = 0xf;
    if (bit >= 32 && cell_bit(block, bit - 32)) sides &= 0xe;
    if ((bit & 31) != 31 && cell_bit(block, bit + 1)) sides &= 0xd;
    if (bit <= 0x3e0 && cell_bit(block, bit + 32)) sides &= 0xb;
    if ((bit & 31) && cell_bit(block, bit - 1)) sides &= 0x7;
    uint16_t value = t[sides ? edges[sides] : inside[variant]];
    value = (uint16_t)(value + (block[4] << 8));
    put(c, i, value);
    if (!sides) return t[inside[variant]];
    uint8_t bank = (value >> 8) & 0x80;
    if ((value & 0xff) == (t[58] & 0xff) && bank == t[58] >> 8) value = t[49];
    if ((value & 0xff) == (t[44] & 0xff) && bank == t[44] >> 8) value = t[35];
    if ((value & 0xff) == (t[45] & 0xff) && bank == t[45] >> 8) value = t[36];
    if ((value & 0xff) == (t[40] & 0xff)) return t[31];
    if ((value & 0xff) == (t[43] & 0xff)) return t[34];
    if ((value & 0xff) == (t[46] & 0xff)) return t[37];
    return value;
}

/* 0x45352 for one block: tops row by row, each with its face below. */
static void stamp(cliffs_t *c, const uint8_t *block) {
    ivec2_t at = {read_u16_le(block) >> 4, read_u16_le(block + 2) >> 4};
    int8_t height = (int8_t)block[4];
    for (int bit = 0; bit < 32 * 32; ++bit) {
        if (!cell_bit(block, bit)) continue;
        int variant = next_random(&c->seed) & 0xf;
        int i = (at.y + bit / 32) * Z_MAP_STRIDE + at.x + bit % 32;
        uint16_t face = top(c, block, bit, i, variant);
        shade(c, block, i, height);
        uint8_t tile = (uint8_t)((face & 0xff) + 0x14);
        uint8_t attr = (uint8_t)(((face >> 8) & 0x80) + 0x12);
        for (int8_t rows = height; rows >= 1; --rows) {
            i += Z_MAP_STRIDE;
            if (rows == 1) tile = (uint8_t)(tile + 0x14);
            put(c, i, (uint16_t)(attr << 8 | tile));
        }
    }
}

/* Level start (0x136aa). The single-player seed is whatever the front end
 * left behind; use the network game's fixed 0x3039 (0x49c77), which only
 * picks among equivalent inside tiles. */
void Z_StampCliffs(uint8_t *tiles, uint8_t *attrs, const uint8_t *blocks, int world) {
    cliffs_t c = {tiles, attrs, blocks, cliff_tiles[world], 0x3039};
    for (int b = 0; b < BLOCKS; ++b) stamp(&c, blocks + b * BLOCK_SIZE);
}
