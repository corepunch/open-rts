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

enum { SCAN_BINS = 256, BIN_CAPACITY = 32 };

typedef struct {
    mobj_t *unit;
    ivec2_t at;
} scan_unit_t;

typedef struct {
    unsigned count;
    scan_unit_t units[BIN_CAPACITY];
} scan_bin_t;

static int scan_bin(ivec2_t at) {
    const int span = 16 * 128;
    if (at.x < 0 || at.y < 0 || at.x >= span || at.y >= span) return -1;
    return (at.x >> 7) + 16 * (at.y >> 7);
}

static ivec2_t ray_cell(ivec2_t at, int offset) {
    return (ivec2_t){(at.x + offset) / Z_TILE, (at.y + offset) / Z_TILE};
}

/* 0x6e583/0x6e928: height uses +8 even when class 1's ray starts at +16. */
static bool scan_ray(const zmap_t *map, ivec2_t from, const scan_unit_t *candidate) {
    ivec2_t a = ray_cell(from, 8), b = ray_cell(candidate->at, 8);
    int offset = Z_IsClassOneUnit(Z_UnitType(candidate->unit)) ? 16 : 8;
    return Z_TerrainRayClear(map, ray_cell(from, 16), Z_TerrainHeight(map, a.x, a.y),
                             ray_cell(candidate->at, offset), Z_TerrainHeight(map, b.x, b.y));
}

static mobj_t *scan_sector(const zmap_t *map, const mobj_t *apc, const scan_bin_t *bins,
                           int origin, ivec2_t from, int direction, int range) {
    const scan_unit_t *best = NULL;
    int best_rank = 255;
    /* Nine lists of at most 32 give the native 288-candidate ceiling.
     * Native offsets use linear bin bounds even at the sides of the map. */
    for (int i = 0; i < 9; ++i) {
        int bin = origin + z_scan_sectors[direction][i];
        if (bin < 0 || bin >= SCAN_BINS) continue;
        for (unsigned j = 0; j < bins[bin].count; ++j) {
            const scan_unit_t *candidate = &bins[bin].units[j];
            ivec2_t delta = ivec2_sub(candidate->at, from);
            if (candidate->unit->team == apc->team ||
                !Z_InScanSector(direction, delta) ||
                !P_CanTarget(apc, candidate->unit) || !P_VisibleTo(apc, candidate->unit)) continue;
            int dx = (int8_t)(delta.x >> 3), dy = (int8_t)(delta.y >> 3);
            int index = dx * dx + dy * dy;
            int rank = index < 1024 ? map->square_roots.bytes[index] : 48;
            if (rank > best_rank || !scan_ray(map, from, candidate)) continue;
            best = candidate;
            best_rank = rank;
        }
    }
    /* 0x1f1b4 applies 0x591ca only to the winner. The CPU veto at
     * 0x9de02 depends on a retail low-memory read with an unknown value. */
    return best && ivec2_distance_squared64(best->at, from) <= range * range ? best->unit : NULL;
}

bool Z_UsesAPCScanner(const mobj_t *apc) {
    return apc->type_id == MT_UNIT_APC && apc->z.driver == MT_UNIT_PYRO - MT_UNITS + 1 &&
           apc->z.crew_count <= Z_CREW_MAX;
}

static bool scan_targets(const mobj_t *apc, int facing, ivec2_t from, mobj_t *targets[4]) {
    const zmap_t *map = level.native_data;
    int origin = scan_bin(from);
    if (!map || origin < 0 || !map->square_roots.bytes || map->square_roots.size < 1024 ||
        !thinkercap.next || !G_UnitCommandable(apc)) return false;

    scan_bin_t bins[SCAN_BINS];
    for (int i = 0; i < SCAN_BINS; ++i) bins[i].count = 0;
    /* The thinker ring preserves creation order through save/load. It is
     * the engine substitute for the native reusable unit-slot order. */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (Z_UnitType(unit) < 0 || unit->remove || unit->hp <= 0 ||
            unit->team >= Z_NEUTRAL || unit->allegiance == ALLEGIANCE_NEUTRAL) continue;
        ivec2_t at = Z_AITopLeft(unit);
        int bin = scan_bin(at);
        if (bin < 0 || bins[bin].count == BIN_CAPACITY) continue;
        bins[bin].units[bins[bin].count++] = (scan_unit_t){unit, at};
    }

    const mobjtype_t *driver = P_ActorType(MT_UNIT_PYRO);
    int range = (int)((int64_t)driver->attack.range * Z_TILE / FIXED_ONE) + 8;
    bool found = false;
    for (int g = 0; g < 4; ++g) {
        targets[g] = scan_sector(map, apc, bins, origin, from, (facing + scan_offset[g]) & 7, range);
        found |= targets[g] != NULL;
    }
    return found;
}

bool Z_APCScannerReady(const mobj_t *apc) {
    if (!level.native_data) return false;
    int facing = angle_to_direction(apc->core.angle, 8, ANG90, true);
    mobj_t *targets[4];
    return scan_targets(apc, facing, Z_AITopLeft(apc), targets);
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

/* One scanner pass (0x1ea5c..0x1eb02) with independent port targets. */
bool Z_APCScannerPyro(mobj_t *apc) {
    if (!level.native_data) return false;
    int facing = angle_to_direction(apc->core.angle, 8, ANG90, true);
    ivec2_t from = Z_AITopLeft(apc);
    mobj_t *targets[4];
    if (!scan_targets(apc, facing, from, targets)) return false;
    int count = apc->z.crew_count ? apc->z.crew_count : 1;
    int attempts = 0;
    for (int member = 0; member < count && attempts < PORT_ATTEMPTS; ++member) {
        uint8_t *phase = phase_of(apc, member);
        *phase = (*phase + 1) & 15;
        if (*phase >= PYRO_PHASE_LIMIT) continue;
        while (attempts < PORT_ATTEMPTS) {
            int port = attempts++, g = port & 3;
            if (!targets[g]) continue;
            const int8_t *pair = port_xy[g][facing * 3 + port / 4];
            ivec2_t start = {from.x + pair[0] - 6 + flame_xy[facing][0],
                             from.y + pair[1] - 6 + flame_xy[facing][1]};
            Z_FireFlameFrom(apc, targets[g], &start);
            break;
        }
    }
    return true;
}
