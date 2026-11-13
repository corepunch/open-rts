/* APC scanner-mode salvo. ZED.EXE action 0x38 (0x1e9dc) runs the scanner
 * 0x1ea0d; a Pyro driver's accepted ports create travelling flames through
 * 0x5a099 (0x1f111), the creator solo Pyros use. Details and the parts that
 * are not modeled are in docs/Z_EXE_FINDINGS.md. */
#include "z_local.h"
#include "info.h"

enum { PORT_ATTEMPTS = 12, PYRO_PHASE_LIMIT = 10 };

/* 0x12db1e (port group 0), 0x12dabe (1), 0x12db7e (2), 0x12da5e (3): x,y
 * indexed facing * 3 + port / 4. 0x1ed92 and its siblings subtract (6,6). */
static const int8_t port_xy[4][24][2] = {
    {{11,2},{14,2},{17,2},{20,2},{22,4},{24,6},{27,9},{27,11},{27,13},{23,17},{21,19},{19,21},
     {17,20},{14,20},{11,20},{9,21},{7,19},{5,17},{2,14},{2,12},{2,10},{5,7},{7,5},{9,3}},
    {{26,5},{26,10},{26,15},{20,17},{17,20},{14,23},{6,17},{11,17},{16,17},{8,18},{5,15},{2,12},
     {3,15},{3,10},{3,5},{6,7},{9,4},{12,1},{13,3},{18,3},{23,3},{21,3},{24,6},{27,9}},
    {{11,16},{14,16},{17,16},{9,19},{7,17},{5,15},{1,13},{1,11},{1,9},{2,7},{4,5},{6,3},
     {17,4},{14,4},{11,4},{24,6},{22,4},{20,2},{29,9},{29,11},{29,13},{21,20},{23,18},{25,16}},
    {{4,5},{4,10},{4,15},{8,3},{5,6},{2,9},{6,3},{11,3},{16,3},{22,7},{19,4},{16,1},
     {26,15},{26,10},{26,5},{20,19},{23,16},{26,13},{13,17},{18,17},{23,17},{8,16},{11,19},{14,22}},
};
/* 0x12dbfe: the Pyro branch 0x1f0d1 adds this pair for the facing. */
static const int8_t flame_xy[8][2] = {
    {-3,-16},{-6,-16},{-3,-16},{-8,-16},{-3,-16},{-3,-16},{-3,-16},{-6,-16},
};
/* 0x1f158..0x1f19d scan directions facing + {0, 2, 4, -2} for port % 4 =
 * 0, 1, 2, 3 (masks 0, 2, 4, 0xfe passed to 0x78f61). */
static const uint8_t scan_offset[4] = {0, 2, 4, 6};

/* 0x793e6..0x7948c: the eight acquisition sectors, indexed by direction
 * (0 north .. 7 north-west). dx, dy are target minus APC. */
static bool in_sector(int direction, int dx, int dy) {
    switch (direction & 7) {
    case 0: return abs(dx) <= -dy;
    case 1: return dx >= 0 && dy < 0;
    case 2: return dx >= abs(dy);
    case 3: return dx >= 0 && dy >= 0;
    case 4: return abs(dx) <= dy;
    case 5: return dx < 0 && dy >= 0;
    case 6: return -dx >= abs(dy);
    default: return dx < 0 && dy < 0;
    }
}

static uint8_t *phase_of(mobj_t *apc, int member) {
    return apc->z.crew_count ? &apc->z.crew[member].flame_phase : &apc->z.flame_phase;
}

/* Every group member's +0xb0 phase advances once per salvo (0x1eaac). */
void Z_APCAdvancePhases(mobj_t *apc) {
    int count = apc->z.crew_count ? apc->z.crew_count : 1;
    for (int i = 0; i < count && i < Z_CREW_MAX; ++i) {
        uint8_t *phase = phase_of(apc, i);
        *phase = (*phase + 1) & 15;
    }
}

/* One scanner pass (0x1ea5c..0x1eb02) against the single target the engine
 * acquired. The four scan groups that contain it are the ports that can
 * fire; all 12 port attempts, successful or not, share one counter. */
bool Z_APCScannerPyro(mobj_t *apc, mobj_t *target) {
    if (apc->z.crew_count > Z_CREW_MAX) return true;
    int facing = angle_to_direction(apc->core.angle, 8, ANG90, true);
    ivec2_t from = Z_AITopLeft(apc), to = Z_AITopLeft(target);
    bool group[4];
    for (int g = 0; g < 4; ++g)
        group[g] = in_sector(facing + scan_offset[g], to.x - from.x, to.y - from.y);
    int count = apc->z.crew_count ? apc->z.crew_count : 1;
    int attempts = 0;
    for (int member = 0; member < count && attempts < PORT_ATTEMPTS; ++member) {
        uint8_t *phase = phase_of(apc, member);
        *phase = (*phase + 1) & 15;
        if (*phase >= PYRO_PHASE_LIMIT) continue;
        while (attempts < PORT_ATTEMPTS) {
            int port = attempts++, g = port & 3;
            if (!group[g]) continue;
            const int8_t *pair = port_xy[g][facing * 3 + port / 4];
            ivec2_t start = {from.x + pair[0] - 6 + flame_xy[facing][0],
                             from.y + pair[1] - 6 + flame_xy[facing][1]};
            Z_FireFlameFrom(apc, target, &start);
            break;
        }
    }
    return true;
}
