#include "engine.h"
#include "info.h"
#include "t_local.h"
#include <math.h>

#define CHECK(c) RTS_CHECK(c, "determinism fixed", #c)

/* Floor sqrt must be exact for every 64-bit input: r*r <= n < (r+1)^2. */
static int isqrt_exact(void) {
    static const uint64_t edge[] = {
        0, 1, 2, 3, 4, 15, 16, 17, 65535, 65536, 65537, UINT64_C(0xFFFFFFFF),
        UINT64_C(0x100000000), UINT64_C(0x7FFFFFFFFFFFFFFF), UINT64_C(0x8000000000000000),
        UINT64_C(0xFFFFFFFFFFFFFFFE), UINT64_C(0xFFFFFFFFFFFFFFFF),
        UINT64_C(0xFFFFFFFE00000001), UINT64_C(0xFFFFFFFE00000000), UINT64_C(0xFFFFFFFE00000002),
    };
    for (size_t i = 0; i < sizeof(edge) / sizeof(edge[0]); ++i) {
        uint64_t n = edge[i];
        uint64_t r = fixed_isqrt64(n);
        CHECK(r * r <= n);
        CHECK(r == 0xFFFFFFFFu || (r + 1) * (r + 1) > n);
    }
    uint64_t x = UINT64_C(0x9E3779B97F4A7C15);
    for (int i = 0; i < 200000; ++i) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        uint64_t n = i & 1 ? x : x >> (i % 48);
        uint64_t r = fixed_isqrt64(n);
        CHECK(r * r <= n && (r == 0xFFFFFFFFu || (r + 1) * (r + 1) > n));
    }
    for (uint64_t r = 0; r < 70000; r += 7) { /* squares and their neighbours */
        CHECK(fixed_isqrt64(r * r) == r);
        if (r) CHECK(fixed_isqrt64(r * r - 1) == r - 1);
        CHECK(fixed_isqrt64(r * r + 2 * r) == r);
    }
    CHECK(fixed_sqrt32(4 * FIXED_ONE) == 2 * FIXED_ONE);
    CHECK(fixed_sqrt32(FIXED_ONE / 4) == FIXED_ONE / 2);
    CHECK(fixed_hypot32(3 * FIXED_ONE, 4 * FIXED_ONE) == 5 * FIXED_ONE);
    return 0;
}

/* The facing LUT: exact at the axes, pinned at samples, and close to libm. */
static int facing_lut(void) {
    CHECK(fixed_sin_bam(0) == 0 && fixed_cos_bam(0) == FIXED_ONE);
    CHECK(fixed_sin_bam(ANG90) == FIXED_ONE && fixed_cos_bam(ANG90) == 0);
    CHECK(fixed_sin_bam(ANG180) == 0 && fixed_cos_bam(ANG180) == -FIXED_ONE);
    CHECK(fixed_sin_bam(ANG270) == -FIXED_ONE && fixed_cos_bam(ANG270) == 0);
    CHECK(fixed_sin_bam(ANG45) == 46341 && fixed_cos_bam(ANG45) == 46341);
    CHECK(fixed_sin_bam(ANG90 / 3) == 32768);          /* sin 30 degrees */
    CHECK(fixed_cos_bam(ANG90 / 3) == 56756);          /* cos 30 degrees */
    CHECK(fixed_sin_bam(ANG270 + ANG45) == -46341);
    CHECK(fixed_sin_quarter[0] == 0 && fixed_sin_quarter[1024] == FIXED_ONE);
    CHECK(fixed_sin_quarter[512] == 46341);
    for (uint64_t a = 0; a < UINT64_C(0x100000000); a += 0x01234567u) {
        double radians = (double)a * (6.28318530717958647692 / 4294967296.0);
        int32_t s = fixed_sin_bam((uint32_t)a), c = fixed_cos_bam((uint32_t)a);
        CHECK(llabs((long long)s - llround(sin(radians) * 65536.0)) <= 1);
        CHECK(llabs((long long)c - llround(cos(radians) * 65536.0)) <= 1);
    }
    /* Round trip through the integer angle code. */
    for (uint32_t a = 0x01000000u; a; a += 0x07654321u) {
        fixed_t dx, dy;
        angle_to_screen_vector_fixed(a, &dx, &dy);
        CHECK(angle_distance(angle_from_screen_vector_fixed(dx, dy), a) < (ANG90 >> 11));
        if (a > 0xF0000000u) break;
    }
    CHECK(angle_from_screen_vector_fixed(FIXED_ONE, 0) == 0);
    CHECK(angle_distance(angle_from_screen_vector_fixed(0, -FIXED_ONE), ANG90) <= 1);
    CHECK(angle_distance(angle_from_screen_vector_fixed(FIXED_ONE, -FIXED_ONE), ANG45) <= 1);
    CHECK(angle_from_screen_vector_fixed(0, 0) == 0);
    return 0;
}

static uint32_t fnv(uint32_t hash, uint32_t value) {
    for (int i = 0; i < 4; ++i) { hash = (hash ^ (value & 255)) * UINT32_C(16777619); value >>= 8; }
    return hash;
}

static mobj_t *spawn(int type, fixed_t x, fixed_t y) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2((fixed2_t){ x, y }, 0), type);
    if (!unit) return NULL;
    unit->traits = MF_MOBILE | MF_SELECTABLE | MF_RENDERABLE;
    unit->speed = 4 * FIXED_ONE;
    unit->radius = FIXED_LIT(0.4);
    unit->owner = unit->team = 0;
    return unit;
}

/* Two columns cross a wall through a door and each other: this drives path
 * following, avoidance, separation, facing and the arrival rules. The result
 * is hashed over positions and facing only, so it does not depend on which
 * game's unit tables are linked in. */
static uint32_t scripted_run(int type, uint32_t *consistency) {
    P_FreeLevel(&level);
    level = (level_t){.width = 40, .height = 20};
    level.blocked = calloc(40 * 20, 1);
    for (int y = 0; y < 20; ++y) if (y != 9 && y != 10) level.blocked[L_Index(&level, 20, y)] = 1;
    P_FreeThinkers();
    enum { N = 6 };
    mobj_t *west[N], *east[N];
    for (int i = 0; i < N; ++i) {
        west[i] = spawn(type, FIXED_LIT(3.5) + (i % 3) * FIXED_ONE, FIXED_LIT(7.5) + (i / 3) * FIXED_ONE);
        east[i] = spawn(type, FIXED_LIT(36.5) - (i % 3) * FIXED_ONE, FIXED_LIT(11.5) - (i / 3) * FIXED_ONE);
        if (!west[i] || !east[i]) return 0;
    }
    P_MoveUnitsAt(&level, west, N, FIXED2_LIT(34.5, 10.5));
    P_MoveUnitsAt(&level, east, N, FIXED2_LIT(5.5, 9.5));
    uint32_t hash = UINT32_C(2166136261);
    for (int tic = 0; tic < 450; ++tic) {
        P_Ticker();
        if (tic % 50 == 49)
            for (int i = 0; i < N; ++i) {
                const mobj_t *u[2] = {west[i], east[i]};
                for (int k = 0; k < 2; ++k) {
                    hash = fnv(hash, (uint32_t)u[k]->core.position.x);
                    hash = fnv(hash, (uint32_t)u[k]->core.position.y);
                    hash = fnv(hash, (uint32_t)u[k]->core.angle);
                }
            }
    }
    if (consistency) *consistency = G_Consistency();
    return hash;
}

/* Pinned on x86-64 and arm64 alike: if this changes, simulation math changed
 * and every networked peer must be rebuilt together.  * changed when G_Consistency() began hashing each unit's speed and radius as
 * fixed-point integers, and again when it became the per-subsystem vector that
 * also folds in the ruleset patch hash, and again when Dark Colony's mine detection became the shared
 * MF_CLOAKED rule (no detected_by to hash) and Warcraft II began hashing a transport's ferry job; the position/facing hash did not move.) Per game because the
 * unit tables behind G_Consistency() differ. */
#if defined(RTS_GAME_DARK_COLONY)
#define EXPECTED_SIM_HASH UINT32_C(0x9e9f07fa)
#define EXPECTED_CONSISTENCY UINT32_C(0x71b75015)
#elif defined(RTS_GAME_DARK_REIGN)
#define EXPECTED_SIM_HASH UINT32_C(0xf0822fde)
#define EXPECTED_CONSISTENCY UINT32_C(0x0228d525)
#elif defined(RTS_GAME_7LEGION)
#define EXPECTED_SIM_HASH UINT32_C(0xf0822fde)
#define EXPECTED_CONSISTENCY UINT32_C(0x1e8e39ca)
#elif defined(RTS_GAME_WARCRAFT_2)
#define EXPECTED_SIM_HASH UINT32_C(0xb4bb45e7)
#define EXPECTED_CONSISTENCY UINT32_C(0x8eb4e104)
#else /* KKnD */
#define EXPECTED_SIM_HASH UINT32_C(0x0ee82de6)
#define EXPECTED_CONSISTENCY UINT32_C(0x330a13ab)
#endif

static int scripted_sim(void) {
    int type = -1;
    for (int i = 0; i < num_actor_types; ++i)
        if ((actor_types[i].traits & (MF_MOBILE | MF_FLY)) == MF_MOBILE &&
            mobjinfo[actor_types[i].id].seestate) { type = actor_types[i].id; break; }
    CHECK(type >= 0);
    uint32_t consistency_a = 0, consistency_b = 0;
    uint32_t first = scripted_run(type, &consistency_a);
    uint32_t second = scripted_run(type, &consistency_b);
    CHECK(first != 0 && first == second);                 /* replay is bit-identical */
    CHECK(consistency_a == consistency_b);                /* ... including G_Consistency() */
    if (getenv("PRINT_SIM_HASH")) printf("sim hash %08x consistency %08x\n", first, consistency_a);
    CHECK(first == EXPECTED_SIM_HASH);
    CHECK(consistency_a == EXPECTED_CONSISTENCY);
    return 0;
}

int main(void) {
    G_InitGame(); P_InitThinkers();
    RTS_RUN(isqrt_exact());
    RTS_RUN(facing_lut());
    RTS_RUN(scripted_sim());
    puts("PASS: fixed-point simulation math (isqrt, facing LUT, scripted sim hash)");
    return 0;
}
