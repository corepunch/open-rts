#include "engine.h"
#include "info.h"
#include "t_local.h"

#define CHECK(c) RTS_CHECK(c, "shared consistency", #c)

/* Which subsystems of the vector changed after `change` ran. */
static unsigned changed(void (*change)(mobj_t *), mobj_t *actor) {
    uint32_t before[CONSISTENCY_COUNT], after[CONSISTENCY_COUNT];
    G_ConsistencyVector(before);
    uint32_t folded = G_Consistency();
    change(actor);
    G_ConsistencyVector(after);
    unsigned mask = 0;
    for (int i = 0; i < CONSISTENCY_COUNT; ++i) mask |= (before[i] != after[i]) << i;
    CHECK(mask ? G_Consistency() != folded : G_Consistency() == folded);
    return mask;
}

static void tweak_resources(mobj_t *a) { (void)a; level.player_resources[1][0] += 5; }
static void tweak_upgrades(mobj_t *a) { (void)a; level.upgrades[1][2].weapon++; }
static void tweak_time(mobj_t *a) { (void)a; ++leveltime; }
static void tweak_hp(mobj_t *a) { --a->hp; }
static void tweak_position(mobj_t *a) { a->core.position.x += 1; }
static void tweak_selection(mobj_t *a) { P_MobjSetSelected(a, !P_MobjIsSelected(a)); }

int main(void) {
    G_InitGame(); P_InitThinkers();
    level = (level_t){.width = 32, .height = 32};
    mobj_t *actor = spawn_mobj_fixture((mobj_t){.hp = 50, .max_hp = 50, .speed = 4 * FIXED_ONE,
        .traits = MF_MOBILE | MF_SELECTABLE, .core.position = fixed3_from_fixed2(fixed2_from_fvec2((fvec2_t){4.5f, 4.5f}), 0),
        .harvest.target = -1});

    uint32_t first[CONSISTENCY_COUNT], second[CONSISTENCY_COUNT];
    G_ConsistencyVector(first);
    G_ConsistencyVector(second);
    CHECK(!memcmp(first, second, sizeof(first)));
    for (int i = 0; i < CONSISTENCY_COUNT; ++i) CHECK(g_consistency_names[i] && *g_consistency_names[i]);
    CHECK(G_ConsistencyExtra(1234) == 1234 || first[CONSISTENCY_GAME] != 2166136261u);

    /* Each kind of divergence is named by exactly the subsystem that holds it. */
    CHECK(changed(tweak_resources, actor) == 1u << CONSISTENCY_RESOURCES);
    CHECK(changed(tweak_upgrades, actor) == 1u << CONSISTENCY_UPGRADES);
    CHECK(changed(tweak_time, actor) == 1u << CONSISTENCY_GLOBALS);
    CHECK(changed(tweak_hp, actor) == 1u << CONSISTENCY_THINKERS);
    CHECK(changed(tweak_position, actor) == 1u << CONSISTENCY_THINKERS);
    /* Local selection is not simulation state. */
    CHECK(changed(tweak_selection, actor) == 0);

    P_FreeLevel(&level);
    puts("PASS: consistency vector names the diverging subsystem and ignores selection");
    return 0;
}
