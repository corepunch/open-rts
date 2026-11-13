#include "z_local.h"

/* Moving-target lead of the fixed-gun aim, ZED.EXE 0x640ff and the
 * speed-byte branch at 0x763ed..0x764e3 (see docs/Z_EXE_FINDINGS.md,
 * "Moving-target lead records"). Shooters of native type 0x16 (Gatling),
 * 0x21 and 0x22 take this branch; every other gun takes the movement-pattern
 * branch at 0x7628f, whose cursor has no counterpart here. */

/* 0x70d37 draws `rand & 0x7f` and 0x640ff compares it with the accuracy
 * byte plus a quarter of it (the bonus is dropped when shooter +0x67 bit 7
 * is set, a flag that is not represented). The lead is applied when the
 * threshold is not below the draw; a 8-bit sum with bit 7 set always passes
 * (the `jns` at 0x64110). */
bool Z_LeadGate(int accuracy, unsigned roll) {
    int byte = accuracy & 0xff, threshold = (byte + (byte >> 2)) & 0xff; /* 8-bit add. */
    if (threshold & 0x80) return true;
    return threshold >= (int)(roll & 0x7f);
}

/* 0x764a7 jump table 0x14a7a8: north, north-east, ... north-west, each
 * adding the whole step to both axes of a diagonal. */
static ivec2_t facing_step(int facing, int amount) {
    static const ivec2_t unit[8] = {{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
    ivec2_t step = unit[facing & 7];
    return (ivec2_t){step.x * amount, step.y * amount};
}

/* 0x76447..0x764e0: the square root table index is the sum of the squares of
 * the low signed bytes of the source-to-aim differences shifted right by
 * four (`imul al`), clamped to 0x3ff. The table byte times 16 is divided by
 * the shot speed (8 when zero) and the 8-bit quotient times the target speed
 * byte is shifted right by three before it is added along the target's
 * facing. */
ivec2_t Z_LeadAim(const uint8_t *roots, ivec2_t source, ivec2_t aim, int target_speed, int facing, int shot_speed) {
    int dx = (int8_t)(((int16_t)(source.x - aim.x)) >> 4);
    int dy = (int8_t)(((int16_t)(source.y - aim.y)) >> 4);
    int index = dx * dx + dy * dy;
    if (index > 0x3ff) index = 0x3ff;
    if (!roots) return aim;
    if (!shot_speed) shot_speed = 8;
    int quotient = (roots[index] << 4) / shot_speed & 0xff;
    int amount = (quotient * (target_speed & 0xff) & 0xffff) >> 3;
    return ivec2_add(aim, facing_step(facing, amount));
}
